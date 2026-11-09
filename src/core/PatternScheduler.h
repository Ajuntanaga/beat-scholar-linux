#pragma once

#include "core/Model.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace bsl {

struct SchedulerTransport
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    std::int64_t absoluteSampleStart = 0;
    std::int32_t blockSize = 0;
    bool playing = false;
    double quarterNotePosition = std::numeric_limits<double>::quiet_NaN();
};

struct PatternTrigger
{
    std::int32_t sampleOffset = 0;
    PatternId patternId = 0;
    PatternQuantization quantization = PatternQuantization::whole;
};

struct PatternRenderSegment
{
    std::int32_t sampleOffset = 0;
    std::int32_t sampleCount = 0;
    PatternId patternId = 0;
    double phaseQuarterNotes = 0.0;
};

class AudioThreadResetRequest
{
public:
    void request() noexcept { requested_.store(true, std::memory_order_release); }
    [[nodiscard]] bool consume() noexcept
    {
        return requested_.exchange(false, std::memory_order_acq_rel);
    }

private:
    std::atomic<bool> requested_ { false };
};

class PatternScheduler
{
public:
    static constexpr std::size_t maximumPendingTriggers = 64;

    explicit PatternScheduler(PatternId activePattern = 0) noexcept;

    void reset(PatternId activePattern) noexcept;
    void removeMissingPatterns(std::span<const PatternState> patterns) noexcept;
    [[nodiscard]] PatternId activePattern() const noexcept { return activePattern_; }

    std::size_t planBlock(const SchedulerTransport& transport,
                          std::span<const PatternTrigger> triggers,
                          std::span<PatternRenderSegment> destination) noexcept;

private:
    struct PendingTrigger
    {
        double targetQuarterNote = 0.0;
        PatternId patternId = 0;
        std::uint64_t sequence = 0;
    };

    struct DueTrigger
    {
        std::int32_t sampleOffset = 0;
        PatternId patternId = 0;
        std::uint64_t sequence = 0;
    };

    void clearTimelineState() noexcept;
    void addOrReplace(PendingTrigger trigger) noexcept;

    std::array<PendingTrigger, maximumPendingTriggers> pending_ {};
    std::size_t pendingCount_ = 0;
    PatternId activePattern_ = 0;
    std::uint64_t nextSequence_ = 0;
    std::int64_t expectedSampleStart_ = 0;
    double expectedQuarterNote_ = 0.0;
    double phaseQuarterNotes_ = 0.0;
    bool wasPlaying_ = false;
};

class PatternTriggerCollector
{
public:
    void clear() noexcept { count_ = 0; }
    void push(PatternTrigger trigger) noexcept
    {
        if (count_ < triggers_.size())
        {
            triggers_[count_++] = trigger;
            return;
        }
        for (std::size_t index = 1; index < triggers_.size(); ++index)
            triggers_[index - 1] = triggers_[index];
        triggers_.back() = trigger;
    }
    [[nodiscard]] std::span<const PatternTrigger> triggers() const noexcept
    {
        return { triggers_.data(), count_ };
    }

private:
    std::array<PatternTrigger, PatternScheduler::maximumPendingTriggers> triggers_ {};
    std::size_t count_ = 0;
};

} // namespace bsl
