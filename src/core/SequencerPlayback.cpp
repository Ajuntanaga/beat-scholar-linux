#include "core/SequencerPlayback.h"

#include <algorithm>
#include <cmath>

namespace bsl {

namespace {

// Adapted from Microsoft STL's sort/heap algorithms; simplified for PlaybackStep.
// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// See third_party/licenses/Microsoft-STL.txt. Equal-time permutation is audible:
// simultaneous hits choose voices with different retained insert histories.
// std::sort on Linux has a different permutation; stable_sort also disagrees.
void medianStep(std::span<PlaybackStep> steps, std::size_t a, std::size_t b, std::size_t c)
{
    if (steps[b].start < steps[a].start) std::swap(steps[a], steps[b]);
    if (steps[c].start < steps[b].start)
    {
        std::swap(steps[b], steps[c]);
        if (steps[b].start < steps[a].start) std::swap(steps[a], steps[b]);
    }
}

std::pair<std::size_t, std::size_t> partitionSteps(std::span<PlaybackStep> steps)
{
    const auto size = steps.size(), middle = size / 2, last = size - 1;
    if (last > 40)
    {
        const auto stride = size / 8;
        medianStep(steps, 0, stride, 2 * stride);
        medianStep(steps, middle - stride, middle, middle + stride);
        medianStep(steps, last - 2 * stride, last - stride, last);
        medianStep(steps, stride, middle, last - stride);
    }
    else medianStep(steps, 0, middle, last);
    auto pivotFirst = middle, pivotLast = middle + 1;
    while (pivotFirst > 0 && steps[pivotFirst - 1].start == steps[pivotFirst].start) --pivotFirst;
    while (pivotLast < size && steps[pivotLast].start == steps[pivotFirst].start) ++pivotLast;
    auto right = pivotLast, left = pivotFirst;
    for (;;)
    {
        for (; right < size; ++right)
        {
            if (steps[pivotFirst].start < steps[right].start) continue;
            if (steps[right].start < steps[pivotFirst].start) break;
            if (pivotLast != right) std::swap(steps[pivotLast], steps[right]);
            ++pivotLast;
        }
        for (; left > 0; --left)
        {
            if (steps[left - 1].start < steps[pivotFirst].start) continue;
            if (steps[pivotFirst].start < steps[left - 1].start) break;
            --pivotFirst;
            if (pivotFirst != left - 1) std::swap(steps[pivotFirst], steps[left - 1]);
        }
        if (left == 0 && right == size) return { pivotFirst, pivotLast };
        if (left == 0)
        {
            if (pivotLast != right) std::swap(steps[pivotFirst], steps[pivotLast]);
            ++pivotLast;
            std::swap(steps[pivotFirst++], steps[right++]);
        }
        else if (right == size)
        {
            --left;
            --pivotFirst;
            if (left != pivotFirst) std::swap(steps[left], steps[pivotFirst]);
            std::swap(steps[pivotFirst], steps[--pivotLast]);
        }
        else std::swap(steps[right++], steps[--left]);
    }
}

void adjustStepHeap(std::span<PlaybackStep> steps, std::size_t hole, PlaybackStep value)
{
    const auto top = hole;
    while (hole < steps.size() / 2)
    {
        auto child = 2 * hole + 1;
        // The reference chooses the right child for equal keys.
        if (child + 1 < steps.size() && !(steps[child + 1].start < steps[child].start)) ++child;
        steps[hole] = steps[child];
        hole = child;
    }
    while (hole > top && steps[(hole - 1) / 2].start < value.start)
    {
        steps[hole] = steps[(hole - 1) / 2];
        hole = (hole - 1) / 2;
    }
    steps[hole] = value;
}

void sortSteps(std::span<PlaybackStep> steps, std::size_t ideal)
{
    while (steps.size() > 32)
    {
        if (ideal == 0)
        {
            for (auto parent = steps.size() / 2; parent > 0;)
            {
                --parent;
                adjustStepHeap(steps, parent, steps[parent]);
            }
            for (auto size = steps.size(); size > 1; --size)
            {
                const auto value = steps[size - 1];
                steps[size - 1] = steps.front();
                adjustStepHeap(steps.first(size - 1), 0, value);
            }
            return;
        }
        const auto [first, last] = partitionSteps(steps);
        ideal = ideal / 2 + ideal / 4;
        if (first < steps.size() - last)
        {
            sortSteps(steps.first(first), ideal);
            steps = steps.subspan(last);
        }
        else
        {
            sortSteps(steps.subspan(last), ideal);
            steps = steps.first(first);
        }
    }
    for (std::size_t index = 1; index < steps.size(); ++index)
    {
        const auto value = steps[index];
        auto hole = index;
        while (hole > 0 && value.start < steps[hole - 1].start)
        {
            steps[hole] = steps[hole - 1];
            --hole;
        }
        steps[hole] = value;
    }
}

} // namespace

PatternPlaybackPlan SequencerPlayback::preparePattern(const PatternState& pattern)
{
    PatternPlaybackPlan steps;
    const bool hasSolo = std::any_of(pattern.lanes.begin(), pattern.lanes.end(),
        [](const LaneState& lane) { return lane.solo; });
    for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
    {
        const auto& lane = pattern.lanes[laneIndex];
        if (lane.muted || (hasSolo && !lane.solo)) continue;
        double beatStart = 0;
        std::size_t beatIndex = 0;
        for (const auto& bar : pattern.bars)
        {
            const double beatLength = 4.0 / static_cast<double>(bar.denominator);
            for (int beat = 0; beat < bar.numerator; ++beat, ++beatIndex, beatStart += beatLength)
            {
                const auto& slices = lane.beats[beatIndex].slices;
                const double step = beatLength / static_cast<double>(slices.size());
                for (std::size_t index = 0; index < slices.size(); ++index)
                {
                    const auto& slice = slices[index];
                    if (!slice.enabled) continue;
                    const double start = beatStart + (static_cast<double>(index) / static_cast<double>(slices.size())) * beatLength;
                    steps.push_back({ start, start + step * 0.98, laneIndex, slice.padIndex,
                        static_cast<std::uint8_t>(std::clamp(std::lround(static_cast<double>(slice.velocity) * 127.0), 1L, 127L)) });
                }
            }
        }
    }
    sortSteps(steps, steps.size());
    return steps;
}

std::vector<PatternPlaybackPlan> SequencerPlayback::prepareProject(const ProjectState& project)
{
    std::vector<PatternPlaybackPlan> plans;
    plans.reserve(project.patterns.size());
    for (const auto& pattern : project.patterns) plans.push_back(preparePattern(pattern));
    return plans;
}


std::size_t SequencerPlayback::renderBatch(const ProjectState& project,
    const TransportFrame& frame, std::size_t patternIndex,
    std::span<const PlaybackStep> steps, std::span<PlaybackEvent> destination, PlaybackCursor& cursor) noexcept
{
    if (cursor.exhausted || destination.empty()) return 0;
    if (!cursor.started) initial_ = notes_;
    auto notes = initial_;
    std::size_t count = 0, ordinal = 0, remaining = 0;
    const auto emit = [&](int offset, std::uint8_t pitch, std::uint8_t velocity, std::uint8_t pad, std::size_t laneIndex, std::uint8_t channel, std::uint8_t releaseVelocity = 0) {
        PlaybackEvent candidate { { offset, pitch, velocity, channel, pad, 0 }, ordinal++, laneIndex, releaseVelocity };
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
            for (const auto& step : steps)
            {
                const auto laneIndex = step.laneIndex;
                const auto& lane = pattern.lanes[laneIndex];
                if (lane.muted || (hasSolo && !lane.solo)) continue;
                const double start = cycleStart + warp(step.start);
                const double end = cycleStart + warp(step.end);
                if (start < 0 || end <= first || last <= start) continue;
                const auto pitch = project.kit.pads[step.pad].midiNote;
                auto& note = notes[laneIndex][pitch];
                const auto on = [&](double position, double cycleOrigin = 0.0) {
                    const int sample = offset(position, cycleOrigin);
                    if (note.state != 1) emit(sample, pitch, 0, note.pad, laneIndex, note.channel, note.velocity);
                    const auto channel = project.global.midiChannelForLane(laneIndex);
                    emit(sample, pitch, step.velocity, step.pad, laneIndex, channel);
                    note = { 0, step.pad, step.velocity, channel };
                };
                if (first <= start && start < last) on(warp(step.start), cycleStart);
                if (note.state == 1) on(first);
                else if (note.state == 2) note.state = 0;
                if (first <= end && end < last && note.state != 1)
                {
                    emit(offset(warp(step.end), cycleStart), pitch, 0, note.pad, laneIndex, note.channel, note.velocity);
                    note.state = 1;
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
            emit(0, static_cast<std::uint8_t>(pitch), 0, note.pad, laneIndex, note.channel, note.velocity);
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
