#include "core/Sequencer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace bsl {
namespace {

// Native sequencer stores each note at 98% of its straight slice.
// Warp the shortened endpoint so swing affects both onset and release.
constexpr double sliceGateRatio = 0.98;

double referenceLength(SwingReference reference)
{
    switch (reference)
    {
        case SwingReference::eighth: return 0.5;
        case SwingReference::quarter: return 1.0;
        case SwingReference::half: return 2.0;
    }
    return 0.5;
}

std::uint8_t midiVelocity(float velocity)
{
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(static_cast<double>(velocity) * 127.0), 1L, 127L));
}

} // namespace

double Sequencer::applySwing(double quarterNotePosition,
                             double swingPercent,
                             SwingReference reference)
{
    if (!std::isfinite(quarterNotePosition))
        return quarterNotePosition;

    const double length = referenceLength(reference);
    const double grid = std::floor(quarterNotePosition / length);
    const double start = grid * length;
    const double phase = std::clamp((quarterNotePosition - start) / length, 0.0, 1.0);

    // Native swing uses a float normalized parameter, compresses its extremes,
    // then skews the whole interval (including fractional subdivisions).
    double midpoint = std::clamp(static_cast<double>(static_cast<float>(
        (std::clamp(swingPercent, -200.0, 200.0) + 200.0) / 400.0)), 0.001, 0.999);
    if (midpoint > 0.8)
        midpoint = (midpoint - 0.8) * (0.9 - 0.8) / (1.0 - 0.8) + 0.8;
    else if (midpoint < 0.2)
        midpoint = midpoint * 0.1 / 0.2 + 0.1;

    const double end = start + length;
    const double rangeMidpoint = (start + midpoint * (end - start) - start) / (end - start);
    const double skew = std::log(0.5) / std::log(rangeMidpoint);
    const double warpedPhase = phase > 0.0 && skew != 1.0
        ? std::exp(std::log(phase) / skew) : phase;
    return start + (end - start) * warpedPhase;
}

std::vector<MidiEvent> Sequencer::render(const ProjectState& state,
                                         const TransportFrame& transport)
{
    return render(state, transport, state.activePattern);
}

std::vector<MidiEvent> Sequencer::render(const ProjectState& state,
                                         const TransportFrame& transport,
                                         std::size_t patternIndex)
{
    std::vector<MidiEvent> result;
    if (!transport.playing || !state.global.sequencerEnabled || !state.isValid()
        || patternIndex >= state.patterns.size() || transport.sampleRate <= 0.0
        || transport.bpm <= 0.0 || transport.blockSize <= 0)
        return result;

    const auto& pattern = state.patterns[patternIndex];
    const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
                                     [](const LaneState& lane) { return lane.solo; });
    std::size_t enabledEventsPerCycle = 0;
    for (const auto& lane : pattern.lanes)
        if (!lane.muted && (!hasSolo || lane.solo))
            for (const auto& beat : lane.beats)
                enabledEventsPerCycle += static_cast<std::size_t>(std::count_if(
                    beat.slices.begin(), beat.slices.end(),
                    [](const SliceState& slice) { return slice.enabled; }));
    const double samplesPerQuarter = transport.sampleRate * 60.0 / transport.bpm;
    const double patternSamples = pattern.durationInQuarterNotes() * samplesPerQuarter;
    const auto overlappingCycles = static_cast<std::size_t>(
        std::ceil(static_cast<double>(transport.blockSize) / patternSamples)) + 2;
    result.resize(std::max<std::size_t>(enabledEventsPerCycle * overlappingCycles, 1));
    const auto count = renderInto(state, transport, patternIndex, result);
    result.resize(count);
    return result;
}

namespace {
template <typename Emit>
void forEachEvent(const ProjectState& state, const TransportFrame& transport,
                  std::size_t patternIndex, Emit&& emit) noexcept
{
    if (!transport.playing || !state.global.sequencerEnabled || !state.isValid()
        || patternIndex >= state.patterns.size() || transport.sampleRate <= 0.0
        || transport.bpm <= 0.0 || transport.blockSize <= 0)
        return;

    const auto& pattern = state.patterns[patternIndex];
    const double samplesPerQuarter = transport.sampleRate * 60.0 / transport.bpm;
    const double patternSamples = pattern.durationInQuarterNotes() * samplesPerQuarter;
    if (!std::isfinite(patternSamples) || patternSamples <= 0.0)
        return;

    const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
                                     [](const LaneState& lane) { return lane.solo; });
    double beatStart = 0.0;
    std::size_t beatIndex = 0;

    for (const auto& bar : pattern.bars)
    {
        const double beatLength = 4.0 / static_cast<double>(bar.denominator);
        for (int beat = 0; beat < bar.numerator; ++beat, ++beatIndex)
        {
            for (const auto& lane : pattern.lanes)
            {
                if (lane.muted || (hasSolo && !lane.solo))
                    continue;

                const auto& beatState = lane.beats[beatIndex];
                const double subdivisionLength = beatLength / static_cast<double>(beatState.slices.size());
                for (std::size_t sliceIndex = 0; sliceIndex < beatState.slices.size(); ++sliceIndex)
                {
                    const auto& slice = beatState.slices[sliceIndex];
                    if (!slice.enabled)
                        continue;

                    const double straightPosition = beatStart + static_cast<double>(sliceIndex) * subdivisionLength;
                    const double swing = std::isfinite(transport.swingPercent)
                                             ? transport.swingPercent : state.global.swingPercent;
                    const double swungPosition = Sequencer::applySwing(straightPosition,
                                                            swing,
                                                            state.global.swingReference);
                    const double localSample = swungPosition * samplesPerQuarter;
                    const double gateSamples = (Sequencer::applySwing(straightPosition + subdivisionLength * sliceGateRatio,
                                                            swing, state.global.swingReference)
                                                - swungPosition) * samplesPerQuarter;
                    const double timelineStart = std::isfinite(transport.patternPhaseQuarterNotes)
                                                     ? transport.patternPhaseQuarterNotes
                                                           * samplesPerQuarter
                                                     : static_cast<double>(transport.absoluteSampleStart);
                    const auto firstCycle = static_cast<std::int64_t>(
                        std::floor((timelineStart - localSample) / patternSamples));

                    for (std::int64_t cycle = firstCycle; ; ++cycle)
                    {
                        const double absolute = localSample + static_cast<double>(cycle) * patternSamples;
                        if (absolute < 0.0)
                            continue;
                        const double relative = absolute - timelineStart;
                        if (relative >= static_cast<double>(transport.blockSize))
                            break;
                        if (relative < -0.5)
                            continue;
                        // Decide block ownership after quantization. A hit in
                        // the final half sample belongs at offset zero of the
                        // next callback, including a slightly negative relative
                        // position there. floor(x + .5) has consistent tie
                        // behavior on both sides of a callback boundary.
                        const auto offset = static_cast<std::int32_t>(std::floor(relative + 0.5));
                        if (offset >= transport.blockSize)
                            break;
                        {
                            emit(MidiEvent {
                                    offset,
                                    state.kit.pads[slice.padIndex].midiNote,
                                    midiVelocity(slice.velocity),
                                    1,
                                    slice.padIndex,
                                    static_cast<std::size_t>(std::max(1LL,
                                        static_cast<long long>(std::floor(relative + gateSamples + 0.5)) - offset))
                            });
                        }
                    }
                }
            }
            beatStart += beatLength;
        }
    }

}
} // namespace

std::size_t Sequencer::renderInto(const ProjectState& state,
                                  const TransportFrame& transport,
                                  std::size_t patternIndex,
                                  std::span<MidiEvent> destination) noexcept
{
    std::size_t count = 0;
    forEachEvent(state, transport, patternIndex, [&](const MidiEvent& event) {
        if (count < destination.size()) destination[count++] = event;
    });
    std::sort(destination.begin(), destination.begin() + static_cast<std::ptrdiff_t>(count));
    return count;
}

std::size_t Sequencer::renderBatch(const ProjectState& state,
                                   const TransportFrame& transport,
                                   std::size_t patternIndex,
                                   std::span<SequencerBatchEvent> destination,
                                   SequencerBatchCursor& cursor) noexcept
{
    if (cursor.exhausted || destination.empty()) return 0;
    // State and transport stay immutable for the cursor's lifetime. Validate
    // once, then restrict each pass to the time range still able to enter the heap.
    if (!transport.playing || !state.global.sequencerEnabled
        || (!cursor.started && !state.isValid())
        || patternIndex >= state.patterns.size() || transport.sampleRate <= 0.0
        || transport.bpm <= 0.0 || transport.blockSize <= 0)
    {
        cursor.exhausted = true;
        return 0;
    }
    const auto& pattern = state.patterns[patternIndex];
    const double samplesPerQuarter = transport.sampleRate * 60.0 / transport.bpm;
    const double duration = pattern.durationInQuarterNotes();
    const double patternSamples = duration * samplesPerQuarter;
    if (!std::isfinite(patternSamples) || patternSamples <= 0.0)
    {
        cursor.exhausted = true;
        return 0;
    }
    const double swing = std::isfinite(transport.swingPercent)
                             ? transport.swingPercent : state.global.swingPercent;
    const auto warped = [&](double position) {
        return applySwing(position, swing, state.global.swingReference) * samplesPerQuarter;
    };
    const double timelineStart = std::isfinite(transport.patternPhaseQuarterNotes)
        ? transport.patternPhaseQuarterNotes * samplesPerQuarter
        : static_cast<double>(transport.absoluteSampleStart);
    const double lower = cursor.started ? cursor.after.event.sampleOffset - 0.5 : -0.5;
    // Swing can carry the last hit beyond the straight pattern boundary.
    // Include that previous cycle, even when its cycle index is negative.
    const auto firstCycle = static_cast<std::int64_t>(
        std::floor((timelineStart - warped(duration)) / patternSamples));
    const auto resumeCycle = static_cast<std::int64_t>(
        std::floor((timelineStart + lower - warped(duration)) / patternSamples));
    const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
        [](const LaneState& lane) { return lane.solo; });
    const auto totalBeats = pattern.lanes.front().beats.size();
    std::size_t count = 0;
    const auto upper = [&] {
        return count == destination.size()
            ? std::min(static_cast<double>(transport.blockSize) - 0.5,
                       destination.front().event.sampleOffset + 0.5)
            : static_cast<double>(transport.blockSize) - 0.5;
    };
    for (auto cycle = std::max(firstCycle, resumeCycle); ; ++cycle)
    {
        const double cycleStart = static_cast<double>(cycle) * patternSamples;
        if (cycleStart - timelineStart >= upper()) break;
        double beatStart = 0.0;
        std::size_t beatIndex = 0;
        bool pastUpper = false;
        for (const auto& bar : pattern.bars)
        {
            const double beatLength = 4.0 / static_cast<double>(bar.denominator);
            for (int beat = 0; beat < bar.numerator; ++beat, ++beatIndex, beatStart += beatLength)
            {
                if (cycleStart + warped(beatStart) - timelineStart >= upper())
                {
                    pastUpper = true;
                    break;
                }
                if (cycleStart + warped(beatStart + beatLength) - timelineStart < lower)
                    continue;
                for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
                {
                    const auto& lane = pattern.lanes[laneIndex];
                    if (lane.muted || (hasSolo && !lane.solo)) continue;
                    const auto& slices = lane.beats[beatIndex].slices;
                    const double subdivisionLength = beatLength / static_cast<double>(slices.size());
                    for (std::size_t sliceIndex = 0; sliceIndex < slices.size(); ++sliceIndex)
                    {
                        const auto& slice = slices[sliceIndex];
                        if (!slice.enabled) continue;
                        const double straight = beatStart + static_cast<double>(sliceIndex) * subdivisionLength;
                        const double swungPosition = applySwing(straight, swing, state.global.swingReference);
                        const double absolute = cycleStart + swungPosition * samplesPerQuarter;
                        const double relative = absolute - timelineStart;
                        if (relative >= upper()) break;
                        if (absolute < 0.0 || relative < lower) continue;
                        const auto offset = static_cast<std::int32_t>(std::floor(relative + 0.5));
                        const double gateSamples = (applySwing(straight + subdivisionLength * sliceGateRatio,
                            swing, state.global.swingReference) - swungPosition) * samplesPerQuarter;
                        // Positional identity is stable when earlier beats/cycles are skipped.
                        const auto ordinal = ((static_cast<std::size_t>(cycle - firstCycle) * totalBeats
                            + beatIndex) * maximumLaneCount + laneIndex) * maximumSubdivision + sliceIndex;
                        const SequencerBatchEvent candidate {
                            { offset, state.kit.pads[slice.padIndex].midiNote,
                              midiVelocity(slice.velocity), 1, slice.padIndex,
                              static_cast<std::size_t>(std::max(1LL,
                                  static_cast<long long>(std::floor(relative + gateSamples + 0.5)) - offset)) },
                            ordinal };
                        if (cursor.started && candidate <= cursor.after) continue;
                        if (count < destination.size())
                        {
                            destination[count++] = candidate;
                            std::push_heap(destination.begin(), destination.begin() + static_cast<std::ptrdiff_t>(count));
                        }
                        else if (candidate < destination.front())
                        {
                            std::pop_heap(destination.begin(), destination.end());
                            destination.back() = candidate;
                            std::push_heap(destination.begin(), destination.end());
                        }
                    }
                }
            }
            if (pastUpper) break;
        }
    }
    const auto end = destination.begin() + static_cast<std::ptrdiff_t>(count);
    std::sort_heap(destination.begin(), end);
    cursor.exhausted = count < destination.size();
    if (count != 0)
    {
        cursor.after = destination[count - 1];
        cursor.started = true;
    }
    return count;
}

std::vector<MidiTickEvent> Sequencer::exportPattern(const ProjectState& state,
                                                    std::size_t patternIndex,
                                                    int ticksPerQuarter)
{
    std::vector<MidiTickEvent> result;
    if (!state.isValid() || patternIndex >= state.patterns.size() || ticksPerQuarter <= 0)
        return result;
    const auto& pattern = state.patterns[patternIndex];
    const double duration = pattern.durationInQuarterNotes();
    const auto loopTicks = std::max<std::int64_t>(1, std::llround(duration * ticksPerQuarter));
    const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
                                     [](const LaneState& lane) { return lane.solo; });
    double beatStart = 0.0;
    std::size_t beatIndex = 0;
    for (const auto& bar : pattern.bars)
    {
        const double beatLength = 4.0 / static_cast<double>(bar.denominator);
        for (int beat = 0; beat < bar.numerator; ++beat, ++beatIndex)
        {
            for (const auto& lane : pattern.lanes)
            {
                if (lane.muted || (hasSolo && !lane.solo))
                    continue;
                const auto& beatState = lane.beats[beatIndex];
                const double subdivisionLength = beatLength / static_cast<double>(beatState.slices.size());
                for (std::size_t sliceIndex = 0; sliceIndex < beatState.slices.size(); ++sliceIndex)
                {
                    const auto& slice = beatState.slices[sliceIndex];
                    if (!slice.enabled)
                        continue;
                    const double straight = beatStart + static_cast<double>(sliceIndex) * subdivisionLength;
                    const double swung = applySwing(straight, state.global.swingPercent,
                                                    state.global.swingReference);
                    // Playback carries swung tails across the pattern boundary.
                    // A one-loop MIDI clip must put them at their cyclic phase;
                    // quantization can also round a final hit onto the boundary.
                    const double local = std::fmod(swung, duration);
                    const auto unwrappedTick = std::llround(local * ticksPerQuarter);
                    const auto tick = unwrappedTick % loopTicks;
                    const double swungEnd = applySwing(straight + subdivisionLength * sliceGateRatio,
                        state.global.swingPercent, state.global.swingReference);
                    const auto gateTicks = std::max<std::int64_t>(1,
                        std::llround((local + swungEnd - swung) * ticksPerQuarter) - unwrappedTick);
                    result.push_back({ tick, state.kit.pads[slice.padIndex].midiNote,
                                       midiVelocity(slice.velocity), slice.padIndex, gateTicks });
                }
            }
            beatStart += beatLength;
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace bsl
