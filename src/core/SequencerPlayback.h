#pragma once

#include "core/Sequencer.h"
#include <array>
#include <tuple>

namespace bsl {

struct PlaybackEvent
{
    MidiEvent event; // velocity zero denotes release.
    std::size_t ordinal = 0;
    std::size_t laneIndex = 0;
    std::uint8_t releaseVelocity = 0;
    auto operator<=>(const PlaybackEvent& other) const noexcept
    {
        return std::tie(event.sampleOffset, ordinal)
            <=> std::tie(other.event.sampleOffset, other.ordinal);
    }
    bool operator==(const PlaybackEvent& other) const noexcept = default;
};

struct PlaybackCursor
{
    PlaybackEvent after;
    bool started = false;
    bool exhausted = false;
};

// Callback-aware native note ownership. Each batch rescans from the callback's
// initial state so bounded caller storage never drops dense or simultaneous hits.
class SequencerPlayback
{
public:
    void reset() noexcept { notes_ = {}; }
    std::size_t renderBatch(const ProjectState&, const TransportFrame&, std::size_t patternIndex,
                            std::span<PlaybackEvent>, PlaybackCursor&) noexcept;
private:
    struct Note
    {
        std::uint8_t state = 1; // 1: off, 0: touched this callback, 2: previous callback.
        std::uint8_t pad = 0;
        std::uint8_t velocity = 0;
    };
    std::array<std::array<Note, 128>, maximumLaneCount> notes_ {};
    std::array<std::array<Note, 128>, maximumLaneCount> initial_ {};
};

} // namespace bsl
