#include "core/PatternScheduler.h"

#include <algorithm>
#include <cmath>

namespace bsl {
namespace {

double gridQuarterNotes(PatternQuantization quantization) noexcept
{
    switch (quantization)
    {
        case PatternQuantization::off: return 0.0;
        case PatternQuantization::eighth: return 0.5;
        case PatternQuantization::quarter: return 1.0;
        case PatternQuantization::half: return 2.0;
        case PatternQuantization::whole: return 4.0;
        case PatternQuantization::sixteenth: return 0.25;
        case PatternQuantization::thirtySecond: return 0.125;
    }
    return 4.0;
}

double boundaryAtOrAfter(double position, double grid) noexcept
{
    if (grid <= 0.0)
        return position;
    constexpr double boundaryTolerance = 1.0e-10;
    return std::ceil(position / grid - boundaryTolerance) * grid;
}

} // namespace

PatternScheduler::PatternScheduler(PatternId activePattern) noexcept
    : activePattern_(activePattern)
{
}

void PatternScheduler::clearTimelineState() noexcept
{
    pendingCount_ = 0;
    phaseQuarterNotes_ = 0.0;
}

void PatternScheduler::reset(PatternId activePattern) noexcept
{
    activePattern_ = activePattern;
    wasPlaying_ = false;
    clearTimelineState();
}

void PatternScheduler::removeMissingPatterns(std::span<const PatternState> patterns) noexcept
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < pendingCount_; ++i)
        if (std::any_of(patterns.begin(), patterns.end(), [&](const auto& pattern) {
                return pattern.runtimeId == pending_[i].patternId;
            }))
            pending_[count++] = pending_[i];
    pendingCount_ = count;
}

void PatternScheduler::addOrReplace(PendingTrigger trigger) noexcept
{
    for (std::size_t index = 0; index < pendingCount_; ++index)
    {
        const bool same = std::abs(pending_[index].targetQuarterNote
                                   - trigger.targetQuarterNote) < 1.0e-10;
        if (same)
        {
            pending_[index] = trigger;
            return;
        }
    }

    if (pendingCount_ < pending_.size())
    {
        pending_[pendingCount_++] = trigger;
        return;
    }

    const auto oldest = std::min_element(
        pending_.begin(), pending_.end(), [](const auto& left, const auto& right) {
            return left.sequence < right.sequence;
        });
    *oldest = trigger;
}

std::size_t PatternScheduler::planBlock(const SchedulerTransport& transport,
                                        std::span<const PatternTrigger> triggers,
                                        std::span<PatternRenderSegment> destination) noexcept
{
    const bool valid = transport.playing && std::isfinite(transport.sampleRate)
                       && transport.sampleRate > 0.0 && std::isfinite(transport.bpm)
                       && transport.bpm > 0.0 && transport.blockSize > 0;
    if (!valid)
    {
        wasPlaying_ = false;
        clearTimelineState();
        return 0;
    }

    const double samplesPerQuarter = transport.sampleRate * 60.0 / transport.bpm;
    const bool hasHostPpq = std::isfinite(transport.quarterNotePosition);
    bool discontinuity = wasPlaying_ && transport.absoluteSampleStart != expectedSampleStart_;
    if (!discontinuity && wasPlaying_ && hasHostPpq)
        discontinuity = std::abs(transport.quarterNotePosition - expectedQuarterNote_) > 1.0e-4;
    // Integrate contiguous fallback blocks at their own tempos. Recomputing
    // the entire sample timeline at today's BPM moves the musical grid.
    const double blockQuarterNote = hasHostPpq ? transport.quarterNotePosition
        : (!wasPlaying_ || discontinuity
               ? static_cast<double>(transport.absoluteSampleStart) / samplesPerQuarter
               : expectedQuarterNote_);
    if (!wasPlaying_ || discontinuity)
    {
        clearTimelineState();
        // Starting/seeking the DAW follows its timeline. A MIDI-triggered
        // switch below still deliberately starts the new pattern at zero.
        phaseQuarterNotes_ = blockQuarterNote;
    }

    wasPlaying_ = true;

    for (const auto& source : triggers)
    {
        const auto offset = std::clamp(source.sampleOffset, 0, transport.blockSize);
        const double grid = gridQuarterNotes(source.quantization);
        PendingTrigger trigger;
        trigger.patternId = source.patternId;
        trigger.sequence = ++nextSequence_;
        const double eventPpq = blockQuarterNote
                                + static_cast<double>(offset) / samplesPerQuarter;
        trigger.targetQuarterNote = boundaryAtOrAfter(eventPpq, grid);
        addOrReplace(trigger);
    }

    std::array<DueTrigger, maximumPendingTriggers> due {};
    std::array<PendingTrigger, maximumPendingTriggers> survivors {};
    std::size_t dueCount = 0;
    std::size_t survivorCount = 0;
    for (std::size_t index = 0; index < pendingCount_; ++index)
    {
        const auto& pending = pending_[index];
        const auto relative = static_cast<std::int64_t>(std::llround(
            (pending.targetQuarterNote - blockQuarterNote) * samplesPerQuarter));
        if (relative < static_cast<std::int64_t>(transport.blockSize))
        {
            due[dueCount++] = {
                static_cast<std::int32_t>(std::clamp<std::int64_t>(
                    relative, 0, transport.blockSize)),
                pending.patternId,
                pending.sequence
            };
        }
        else
        {
            survivors[survivorCount++] = pending;
        }
    }
    pending_ = survivors;
    pendingCount_ = survivorCount;

    std::sort(due.begin(), due.begin() + static_cast<std::ptrdiff_t>(dueCount),
              [](const auto& left, const auto& right) {
                  if (left.sampleOffset != right.sampleOffset)
                      return left.sampleOffset < right.sampleOffset;
                  return left.sequence < right.sequence;
              });

    std::array<DueTrigger, maximumPendingTriggers> changes {};
    std::size_t changeCount = 0;
    for (std::size_t index = 0; index < dueCount; ++index)
    {
        if (changeCount > 0
            && changes[changeCount - 1].sampleOffset == due[index].sampleOffset)
            changes[changeCount - 1] = due[index];
        else
            changes[changeCount++] = due[index];
    }

    std::size_t segmentCount = 0;
    std::int32_t cursor = 0;
    double phase = phaseQuarterNotes_;
    const auto appendSegment = [&](std::int32_t start, std::int32_t end,
                                   PatternId pattern, double startPhase) {
        if (end <= start || segmentCount >= destination.size())
            return;
        destination[segmentCount++] = { start, end - start, pattern, startPhase };
    };

    for (std::size_t index = 0; index < changeCount; ++index)
    {
        const auto offset = changes[index].sampleOffset;
        appendSegment(cursor, offset, activePattern_, phase);
        phase += static_cast<double>(offset - cursor) / samplesPerQuarter;
        activePattern_ = changes[index].patternId;
        phase = 0.0;
        cursor = offset;
    }
    appendSegment(cursor, transport.blockSize, activePattern_, phase);
    phaseQuarterNotes_ = phase
                         + static_cast<double>(transport.blockSize - cursor) / samplesPerQuarter;

    expectedSampleStart_ = transport.absoluteSampleStart + transport.blockSize;
    expectedQuarterNote_ = blockQuarterNote
                           + static_cast<double>(transport.blockSize) / samplesPerQuarter;
    return segmentCount;
}

} // namespace bsl
