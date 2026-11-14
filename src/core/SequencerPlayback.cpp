#include "core/SequencerPlayback.h"

#include <algorithm>
#include <cmath>

namespace bsl {

std::size_t SequencerPlayback::renderBatch(const ProjectState& project,
    const TransportFrame& frame, std::size_t patternIndex,
    std::span<PlaybackEvent> destination, PlaybackCursor& cursor) noexcept
{
    if (cursor.exhausted || destination.empty()) return 0;
    if (!cursor.started) initial_ = notes_;
    auto notes = initial_;
    std::size_t count = 0, ordinal = 0, remaining = 0;
    const auto emit = [&](int offset, std::uint8_t pitch, std::uint8_t velocity, std::uint8_t pad, std::size_t laneIndex, std::uint8_t releaseVelocity = 0) {
        PlaybackEvent candidate { { offset, pitch, velocity, 1, pad, 0 }, ordinal++, laneIndex, releaseVelocity };
        if (cursor.started && candidate <= cursor.after) return;
        ++remaining;
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
    };
    const bool playing = frame.playing && project.global.sequencerEnabled
        && patternIndex < project.patterns.size() && project.isValid()
        && std::isfinite(frame.sampleRate) && frame.sampleRate > 0
        && std::isfinite(frame.bpm) && frame.bpm > 0 && frame.blockSize > 0;
    if (playing)
    {
        const auto& pattern = project.patterns[patternIndex];
        const double increment = 1.0 / ((frame.sampleRate * 60.0) / frame.bpm);
        const double host = std::isfinite(frame.quarterNotePosition) ? frame.quarterNotePosition
            : static_cast<double>(frame.absoluteSampleStart) / frame.sampleRate * frame.bpm / 60.0;
        const double origin = std::isfinite(frame.patternPhaseQuarterNotes)
            ? host - frame.patternPhaseQuarterNotes : 0.0;
        const auto timeline = [&](int index) { return host + static_cast<double>(index) * increment - origin; };
        const double first = timeline(0), last = timeline(frame.blockSize - 1);
        const auto offset = [&](double position, double cycleOrigin = 0.0) {
            // Adjacent native timeline intervals are lower-inclusive. Binary
            // search preserves their arithmetic instead of rounding position*SPQ.
            int low = 0, high = frame.blockSize - 1;
            while (low < high)
            {
                const int middle = low + (high - low + 1) / 2;
                if (timeline(middle) - cycleOrigin <= position) low = middle;
                else high = middle - 1;
            }
            return low;
        };
        const double duration = pattern.durationInQuarterNotes();
        const double swing = std::isfinite(frame.swingPercent) ? frame.swingPercent : project.global.swingPercent;
        const auto warp = [&](double position) {
            return Sequencer::applySwing(position, swing, project.global.swingReference);
        };
        const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
            [](const LaneState& lane) { return lane.solo; });
        const auto firstCycle = static_cast<std::int64_t>(std::floor((first - warp(duration)) / duration));
        for (auto cycle = firstCycle; static_cast<double>(cycle) * duration <= last; ++cycle)
        {
            const double cycleStart = static_cast<double>(cycle) * duration;
            double beatStart = 0;
            std::size_t beatIndex = 0;
            for (const auto& bar : pattern.bars)
            {
                const double beatLength = 4.0 / static_cast<double>(bar.denominator);
                for (int beat = 0; beat < bar.numerator; ++beat, ++beatIndex, beatStart += beatLength)
                {
                    if (cycleStart + warp(beatStart + beatLength) <= first) continue;
                    if (cycleStart + warp(beatStart) >= last) continue;
                    for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
                    {
                        const auto& lane = pattern.lanes[laneIndex];
                        if (lane.muted || (hasSolo && !lane.solo)) continue;
                        const auto& slices = lane.beats[beatIndex].slices;
                        const double step = beatLength / static_cast<double>(slices.size());
                        for (std::size_t index = 0; index < slices.size(); ++index)
                        {
                            const auto& slice = slices[index];
                            if (!slice.enabled) continue;
                            const double straight = beatStart + (static_cast<double>(index) / static_cast<double>(slices.size())) * beatLength;
                            const double start = cycleStart + warp(straight);
                            const double end = cycleStart + warp(straight + step * 0.98);
                            if (start < 0 || end <= first || last <= start) continue;
                            const auto pitch = project.kit.pads[slice.padIndex].midiNote;
                            auto& note = notes[laneIndex][pitch];
                            const auto on = [&](double position, double cycleOrigin = 0.0) {
                                const int sample = offset(position, cycleOrigin);
                                if (note.state != 1) emit(sample, pitch, 0, note.pad, laneIndex, note.velocity);
                                const auto velocity = static_cast<std::uint8_t>(std::clamp(
                                    std::lround(static_cast<double>(slice.velocity) * 127.0), 1L, 127L));
                                emit(sample, pitch, velocity, slice.padIndex, laneIndex);
                                note = { 0, slice.padIndex, velocity };
                            };
                            if (first <= start && start < last) on(warp(straight), cycleStart);
                            if (note.state == 1) on(first);
                            else if (note.state == 2) note.state = 0;
                            if (first <= end && end < last && note.state != 1)
                            {
                                emit(offset(warp(straight + step * 0.98), cycleStart), pitch, 0, note.pad, laneIndex, note.velocity);
                                note.state = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    for (std::size_t laneIndex = 0; laneIndex < notes.size(); ++laneIndex)
    for (std::size_t pitch = 0; pitch < notes[laneIndex].size(); ++pitch)
    {
        auto& note = notes[laneIndex][pitch];
        if (playing && note.state == 0) note.state = 2;
        else if (note.state != 1)
        {
            emit(0, static_cast<std::uint8_t>(pitch), 0, note.pad, laneIndex, note.velocity);
            note.state = 1;
        }
    }
    std::sort(destination.begin(), destination.begin() + static_cast<std::ptrdiff_t>(count));
    if (count != 0) cursor.after = destination[count - 1];
    cursor.started = true;
    cursor.exhausted = remaining <= destination.size();
    if (cursor.exhausted) notes_ = notes;
    return count;
}

} // namespace bsl
