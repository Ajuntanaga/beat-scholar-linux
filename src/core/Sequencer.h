#pragma once

#include "core/Model.h"

#include <compare>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace bsl {

struct TransportFrame
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    std::int64_t absoluteSampleStart = 0;
    std::int32_t blockSize = 0;
    bool playing = false;
    double swingPercent = std::numeric_limits<double>::quiet_NaN();
    double quarterNotePosition = std::numeric_limits<double>::quiet_NaN();
    double patternPhaseQuarterNotes = std::numeric_limits<double>::quiet_NaN();
};

struct MidiEvent
{
    std::int32_t sampleOffset = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint8_t channel = 1;
    std::uint8_t padIndex = 0;
    // Internal sampler duration; zero leaves duration to incoming MIDI.
    std::size_t gateFrames = 0;

    auto operator<=>(const MidiEvent&) const = default;
};

// A tie-breaker preserves every event when a batch ends between simultaneous hits.
struct SequencerBatchEvent
{
    MidiEvent event;
    std::size_t ordinal = 0;
    auto operator<=>(const SequencerBatchEvent&) const = default;
};

struct SequencerBatchCursor
{
    SequencerBatchEvent after;
    bool started = false;
    bool exhausted = false;
};

struct MidiTickEvent
{
    std::int64_t tick = 0;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::int64_t gateTicks = 1;

    auto operator<=>(const MidiTickEvent&) const = default;
};

class Sequencer
{
public:
    static std::vector<MidiEvent> render(const ProjectState& state,
                                         const TransportFrame& transport);
    static std::vector<MidiEvent> render(const ProjectState& state,
                                         const TransportFrame& transport,
                                         std::size_t patternIndex);
    static std::size_t renderInto(const ProjectState& state,
                                  const TransportFrame& transport,
                                  std::size_t patternIndex,
                                  std::span<MidiEvent> destination) noexcept;
    // Repeated calls enumerate all events in chronological order using caller storage.
    // Keep state, transport and patternIndex unchanged until the cursor is exhausted.
    static std::size_t renderBatch(const ProjectState&, const TransportFrame&,
                                   std::size_t patternIndex,
                                   std::span<SequencerBatchEvent>, SequencerBatchCursor&) noexcept;
    static std::vector<MidiTickEvent> exportPattern(const ProjectState& state,
                                                    std::size_t patternIndex,
                                                    int ticksPerQuarter = 960);
    static double applySwing(double quarterNotePosition,
                             double swingPercent,
                             SwingReference reference);
};

} // namespace bsl
