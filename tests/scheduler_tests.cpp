#include "native_dense_timing.h"
#include "core/PatternScheduler.h"
#include "core/Sequencer.h"
#include "core/SequencerPlayback.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bsl::SchedulerTransport transport(std::int64_t sampleStart,
                                  int blockSize,
                                  double ppq = std::numeric_limits<double>::quiet_NaN())
{
    return { 48000.0, 120.0, sampleStart, blockSize, true, ppq };
}

void testUnquantizedTriggerUsesEventOffset()
{
    struct Case
    {
        bsl::PatternQuantization quantization;
        int expectedOffset;
    };
    constexpr std::array cases {
        Case { bsl::PatternQuantization::off, 100 },
        // Quantized callback timing is covered by captured fixtures below.
    };

    for (const auto& test : cases)
    {
        bsl::PatternScheduler scheduler(0);
        std::array<bsl::PatternTrigger, 1> triggers {
            bsl::PatternTrigger { 100, 1, test.quantization }
        };
        std::array<bsl::PatternRenderSegment, 3> segments {};
        const auto count = scheduler.planBlock(
            transport(0, 100000, 0.0), triggers, segments);
        check(count == 2, "quantized change splits the block once");
        if (count == 2)
        {
            check(segments[0].patternId == 0 && segments[0].sampleOffset == 0,
                  "old pattern renders before its boundary");
            check(segments[1].patternId == 1
                      && segments[1].sampleOffset == test.expectedOffset,
                  "new pattern starts at the selected quantization boundary");
            check(std::abs(segments[1].phaseQuarterNotes) < 1.0e-12,
                  "new pattern phase is zero at the boundary");
        }
    }
}

void testHostPpqOverridesSampleTimeline()
{
    bsl::PatternScheduler scheduler(0);
    const std::array triggers {
        bsl::PatternTrigger { 0, 1, bsl::PatternQuantization::quarter }
    };
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const auto count = scheduler.planBlock(transport(999999, 20000, 3.1), triggers, segments);
    check(count == 1 && segments[0].patternId == 0 && segments[0].sampleOffset == 0,
          "host PPQ, not absolute samples, chooses the boundary when PPQ is available");
}

void testAbsoluteSampleAndBpmFallbackChoosesBoundary()
{
    bsl::PatternScheduler scheduler(0);
    const std::array triggers {
        bsl::PatternTrigger { 0, 1, bsl::PatternQuantization::quarter }
    };
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const auto count = scheduler.planBlock(transport(12000, 20000), triggers, segments);
    check(count == 1 && segments[0].patternId == 1 && segments[0].sampleOffset == 0,
          "absolute samples and BPM provide the grid when host PPQ is absent");
}

void testTempoChangesPreserveTheQuantizationGrid()
{
    for (const bool hostPpq : { false, true })
        for (const bool queuedBeforeChange : { false, true })
            for (const double nextBpm : { 60.0, 240.0 })
            {
                bsl::PatternScheduler scheduler(0);
                std::array<bsl::PatternRenderSegment, 4> segments {};
                const std::array initialTriggers {
                    bsl::PatternTrigger { 6000, 3, bsl::PatternQuantization::off },
                    bsl::PatternTrigger { 6001, 1, bsl::PatternQuantization::quarter },
                };
                // Start off-grid so the callback-level trigger remains pending.
                auto first = transport(2400, 12000);
                if (hostPpq) first.quarterNotePosition = 0.1;
                scheduler.planBlock(first,
                    std::span(initialTriggers).first(queuedBeforeChange ? 2 : 1), segments);
                check(scheduler.activePattern() == 3,
                      "quantized trigger remains queued before the next beat");

                auto second = transport(14400, 1000);
                second.bpm = nextBpm;
                if (hostPpq) second.quarterNotePosition = 0.6;
                const std::array trigger {
                    bsl::PatternTrigger { 0, 1, bsl::PatternQuantization::quarter }
                };
                scheduler.planBlock(second,
                    std::span(trigger).first(queuedBeforeChange ? 0 : 1), segments);
                check(scheduler.activePattern() == 3,
                      "tempo change does not consume a boundary beyond this callback");
                auto third = transport(15400, 20000);
                third.bpm = nextBpm;
                if (hostPpq)
                    third.quarterNotePosition = 0.6 + 1000.0 / (48000.0 * 60.0 / nextBpm);
                const auto count = scheduler.planBlock(third, {}, segments);
                check(count == 1 && segments[0].patternId == 1
                          && segments[0].sampleOffset == 0
                          && std::abs(segments[0].phaseQuarterNotes) < 1.0e-12,
                      "new and queued triggers switch at the callback containing the next beat");
            }
}

void testLatestTriggerForOneBoundaryWins()
{
    bsl::PatternScheduler scheduler(0);
    const std::array triggers {
        bsl::PatternTrigger { 100, 1, bsl::PatternQuantization::quarter },
        bsl::PatternTrigger { 500, 2, bsl::PatternQuantization::quarter },
    };
    std::array<bsl::PatternRenderSegment, 4> segments {};
    const auto count = scheduler.planBlock(transport(0, 30000, 0.0), triggers, segments);
    check(count == 1, "same-boundary triggers produce one change");
    check(count == 1 && segments[0].sampleOffset == 0 && segments[0].patternId == 2,
          "last trigger targeting the boundary wins");
}

void testBoundedCollectorRetainsTheLatestSameBoundaryTrigger()
{
    bsl::PatternTriggerCollector collector;
    for (std::size_t index = 1; index <= 65; ++index)
        collector.push({ static_cast<std::int32_t>(index), index,
                         bsl::PatternQuantization::quarter });

    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const auto count = scheduler.planBlock(
        transport(0, 30000, 0.0), collector.triggers(), segments);
    check(count == 1 && segments[0].sampleOffset == 0
              && segments[0].patternId == 65,
          "bounded process-block collection retains the 65th and latest same-boundary trigger");
}

void testMidBlockSplitAdvancesOldPhaseAndZerosNewPhase()
{
    bsl::PatternScheduler scheduler(3);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const std::array noTriggers = std::array<bsl::PatternTrigger, 0> {};
    auto count = scheduler.planBlock(transport(0, 6000, 0.0), noTriggers, segments);
    check(count == 1, "initial block renders one pattern");

    const std::array trigger {
        bsl::PatternTrigger { 6000, 4, bsl::PatternQuantization::off }
    };
    count = scheduler.planBlock(transport(6000, 10000, 0.25), trigger, segments);
    check(count == 2, "mid-block boundary splits old and new patterns");
    if (count == 2)
    {
        check(segments[0].patternId == 3
                  && std::abs(segments[0].phaseQuarterNotes - 0.25) < 1.0e-12,
              "old pattern resumes at its accumulated phase");
        check(segments[0].sampleCount == 6000,
              "old pattern stops exactly at the half-quarter boundary");
        check(segments[1].patternId == 4 && segments[1].sampleOffset == 6000
                  && std::abs(segments[1].phaseQuarterNotes) < 1.0e-12,
              "new pattern starts at phase zero without pre-boundary emission");
    }
}

void testSegmentedRenderingEmitsNewPatternOnlyAtBoundary()
{
    auto state = bsl::ProjectState::makeDefault();
    auto second = state.patterns.front();
    second.name = "Pattern 2";
    second.triggerNote = 13;
    second.quantization = bsl::PatternQuantization::quarter;
    second.lanes.front().beats.front().slices.front() = { 1, 1.0f, true };
    state.patterns.push_back(second);

    bsl::PatternScheduler scheduler(0);
    const std::array trigger {
        bsl::PatternTrigger { 100, 1, bsl::PatternQuantization::quarter }
    };
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const auto segmentCount = scheduler.planBlock(
        transport(0, 30000, 0.0), trigger, segments);

    std::array<bsl::MidiEvent, 16> events {};
    std::size_t eventCount = 0;
    for (std::size_t index = 0; index < segmentCount; ++index)
    {
        bsl::TransportFrame frame;
        frame.sampleRate = 48000.0;
        frame.bpm = 120.0;
        frame.absoluteSampleStart = segments[index].sampleOffset;
        frame.blockSize = segments[index].sampleCount;
        frame.playing = true;
        frame.patternPhaseQuarterNotes = segments[index].phaseQuarterNotes;
        const auto count = bsl::Sequencer::renderInto(
            state, frame, segments[index].patternId,
            std::span<bsl::MidiEvent>(events).subspan(eventCount));
        for (std::size_t event = 0; event < count; ++event)
            events[eventCount + event].sampleOffset += segments[index].sampleOffset;
        eventCount += count;
    }
    check(eventCount == 1 && events[0].sampleOffset == 0 && events[0].padIndex == 1,
          "segmented render emits the new pattern at its boundary and never before it");
}

void testPendingTriggerSurvivesUntilFutureBlock()
{
    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const std::array trigger {
        bsl::PatternTrigger { 1, 1, bsl::PatternQuantization::quarter }
    };
    auto count = scheduler.planBlock(transport(2400, 12000, 0.1), trigger, segments);
    check(count == 1 && segments[0].patternId == 0,
          "future boundary does not activate in an earlier block");
    const std::array noTriggers = std::array<bsl::PatternTrigger, 0> {};
    count = scheduler.planBlock(transport(14400, 16000, 0.6), noTriggers, segments);
    check(count == 1 && segments[0].sampleOffset == 0 && segments[0].patternId == 1,
          "queued trigger activates in the block containing its boundary");
}

void testStopClearsPendingAndPhaseState()
{
    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const std::array trigger {
        bsl::PatternTrigger { 100, 1, bsl::PatternQuantization::whole }
    };
    scheduler.planBlock(transport(0, 1000, 0.1), trigger, segments);
    auto stopped = transport(1000, 1000, 0.041666666666666664);
    stopped.playing = false;
    check(scheduler.planBlock(stopped, {}, segments) == 0,
          "stopped transport renders no pattern segments");
    const auto count = scheduler.planBlock(transport(0, 1000, 0.0), {}, segments);
    check(count == 1 && segments[0].patternId == 0
              && std::abs(segments[0].phaseQuarterNotes) < 1.0e-12,
          "restart clears the pending trigger and restarts phase at zero");
}

void testTransportDiscontinuityClearsPendingAndPhaseState()
{
    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const std::array trigger {
        bsl::PatternTrigger { 100, 1, bsl::PatternQuantization::whole }
    };
    scheduler.planBlock(transport(0, 1000, 0.1), trigger, segments);
    const auto count = scheduler.planBlock(transport(50000, 1000, 8.0), {}, segments);
    check(count == 1 && segments[0].patternId == 0
              && std::abs(segments[0].phaseQuarterNotes - 8.0) < 1.0e-12,
          "sample-position discontinuity drops pending work and aligns to the host phase");
}

void testHostStartAndSeekPhaseLockThePattern()
{
    auto state = bsl::ProjectState::makeDefault();
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(4, 4) };
    pattern.lanes = { bsl::LaneState::make(4) };
    for (auto& beat : pattern.lanes.front().beats)
    {
        beat = bsl::BeatState::withSubdivision(1, 0, 1.0f);
        beat.slices.front().enabled = false;
    }
    pattern.lanes.front().beats.front().slices.front().enabled = true;

    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    // Deliberately disagreeing sample/PPQ clocks prove host PPQ wins. A
    // beat-zero-only 4/4 pattern must not restart at this PPQ=1 position.
    auto count = scheduler.planBlock(transport(999999, 64, 1.0), {}, segments);
    check(count == 1 && std::abs(segments[0].phaseQuarterNotes - 1.0) < 1.0e-12,
          "initial host PPQ becomes the active pattern phase");
    bsl::TransportFrame frame { 48000.0, 120.0, 999999, 64, true };
    frame.patternPhaseQuarterNotes = segments[0].phaseQuarterNotes;
    std::array<bsl::MidiEvent, 4> events {};
    check(bsl::Sequencer::renderInto(state, frame, 0, events) == 0,
          "starting the host one quarter into the loop does not replay beat zero");

    // A seek is a discontinuity. PPQ 3.999 is 24 samples before the next
    // 4/4 loop boundary at this rate/BPM, so the beat-zero hit must land 24
    // samples into this block rather than disappearing or restarting at zero.
    count = scheduler.planBlock(transport(95976, 64, 3.999), {}, segments);
    check(count == 1 && std::abs(segments[0].phaseQuarterNotes - 3.999) < 1.0e-12,
          "seeked host PPQ replaces rather than clears pattern phase");
    frame.absoluteSampleStart = 95976;
    frame.patternPhaseQuarterNotes = segments[0].phaseQuarterNotes;
    const auto eventCount = bsl::Sequencer::renderInto(state, frame, 0, events);
    check(eventCount == 1 && events[0].sampleOffset == 24,
          "seeked host phase emits the next loop start at its exact offset");

    // Backward seek through a negative lead-in must retain the negative PPQ
    // phase. The next beat-zero event is 24 samples ahead, not at block zero.
    count = scheduler.planBlock(transport(-24, 64, -0.001), {}, segments);
    check(count == 1 && std::abs(segments[0].phaseQuarterNotes + 0.001) < 1.0e-12,
          "backward negative-PPQ seek becomes the active pattern phase");
    frame.absoluteSampleStart = -24;
    frame.patternPhaseQuarterNotes = segments[0].phaseQuarterNotes;
    const auto negativeLeadInCount = bsl::Sequencer::renderInto(state, frame, 0, events);
    check(negativeLeadInCount == 1 && events[0].sampleOffset == 24,
          "negative lead-in emits the first loop beat at the correct future offset");

    // Unlike host start/seek, an explicit MIDI-triggered switch deliberately
    // restarts the newly selected pattern at phase zero.
    const std::array trigger { bsl::PatternTrigger { 0, 1, bsl::PatternQuantization::off } };
    count = scheduler.planBlock(transport(48000, 64, 2.0), trigger, segments);
    check(count == 1 && segments[0].patternId == 1
              && std::abs(segments[0].phaseQuarterNotes) < 1.0e-12,
          "explicit pattern trigger restarts its pattern at phase zero");
}
void testHostPhaseDoesNotAccumulateCallbackDrift()
{
    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 8> segments {};
    double origin = 0.0;
    bool exact = true;
    for (int block = 0; block < 1000; ++block)
    {
        auto frame = transport(block * 64, 64, static_cast<double>(block * 64) / 48000.0 * 100.0 / 60.0);
        frame.bpm = 100.0;
        const std::array trigger { bsl::PatternTrigger { 0, 1, bsl::PatternQuantization::off } };
        const bool switchPattern = block == 127;
        if (switchPattern) origin = frame.quarterNotePosition;
        const auto count = scheduler.planBlock(frame,
            switchPattern ? std::span<const bsl::PatternTrigger>(trigger) : std::span<const bsl::PatternTrigger>(), segments);
        exact = exact && count == 1 && segments[0].phaseQuarterNotes == frame.quarterNotePosition - origin;
    }
    check(exact, "host phase uses a fixed pattern origin without callback accumulation drift");
}

void testCapturedDenseTiming()
{
    for (int block : {128, 16384})
    {
        auto state = bsl::ProjectState::makeDefault();
        auto& pattern = state.patterns[0];
        pattern.bars = {bsl::BarState::make(1, 16)};
        pattern.lanes.assign(1, bsl::LaneState::make(1));
        pattern.lanes[0].beats[0] = bsl::BeatState::withSubdivision(45, 0, .5f);
        bsl::SequencerPlayback playback;
        std::vector<int> observed;
        std::array<bsl::PlaybackEvent, 16> events;
        for (int base = 0; base < 16384; base += block)
        {
            bsl::TransportFrame frame {48000.0, 300.0, base, block, true};
            bsl::PlaybackCursor cursor;
            while (!cursor.exhausted)
            {
                auto count = playback.renderBatch(state, frame, 0, events, cursor);
                for (std::size_t i = 0; i < count; ++i)
                {
                    check((events[i].event.velocity != 0) == (observed.size() % 2 == 0),
                        "dense native event polarity matches");
                    observed.push_back(base + events[i].event.sampleOffset);
                }
            }
        }
        const auto& expected = block == 128 ? nativeDense128 : nativeDense16384;
        check(std::equal(observed.begin(), observed.end(), expected.begin(), expected.end()),
            "dense native onset and release samples match captured callback size");
    }
}

void testCapturedCallbackReleaseLifecycle()
{
    struct Case { double swing; int quantum; std::array<int, 6> offsets; };
    const std::array cases {
        Case { -200, 64, { 0, 705, 754, 7264, 7510, 28168 } },
        Case { -100, 64, { 0, 3073, 3200, 12545, 12800, 28417 } },
        Case { 50, 64, { 0, 13487, 13673, 21728, 21877, 28669 } },
        Case { 100, 64, { 0, 18102, 18254, 24237, 24339, 28720 } },
        Case { 150, 64, { 0, 21632, 21743, 25895, 25962, 28750 } },
        Case { 200, 64, { 0, 24274, 24349, 27028, 27069, 28770 } },
        Case { 150, 128, { 0, 21743, 21743, 25895, 25962, 28750 } },
    };
    for (const auto& test : cases)
        for (const std::size_t capacity : { 1U, 16U })
        {
            auto state = bsl::ProjectState::makeDefault();
            for (auto& lane : state.patterns[0].lanes)
                for (auto& beat : lane.beats)
                    for (auto& slice : beat.slices) slice.enabled = false;
            state.patterns[0].lanes[0].beats[0].slices.assign(3, { 0, 1.0f, true });
            state.global.swingPercent = test.swing;
            state.global.swingReference = bsl::SwingReference::quarter;
            bsl::SequencerPlayback playback;
            std::vector<std::pair<int, bool>> observed;
            std::array<bsl::PlaybackEvent, 16> events;
            for (int base = 0; base < 28800; base += test.quantum)
            {
                bsl::TransportFrame frame { 48000.0, 100.0, base, test.quantum, true };
                frame.quarterNotePosition = static_cast<double>(base) / 48000.0 * 100.0 / 60.0;
                frame.patternPhaseQuarterNotes = frame.quarterNotePosition;
                bsl::PlaybackCursor cursor;
                while (!cursor.exhausted)
                {
                    const auto count = playback.renderBatch(state, frame, 0,
                        std::span(events).first(capacity), cursor);
                    for (std::size_t i = 0; i < count; ++i)
                        observed.emplace_back(base + events[i].event.sampleOffset, events[i].event.velocity == 0);
                }
            }
            bool matches = observed.size() == test.offsets.size();
            for (std::size_t i = 0; i < std::min(observed.size(), test.offsets.size()); ++i)
                matches = matches && observed[i].first == test.offsets[i] && observed[i].second == (i % 2 == 1);
            check(matches, "captured native triplet lifecycle matches at both callback and batch sizes");
            if (!matches)
            {
                std::cerr << "swing=" << test.swing << " block=" << test.quantum << " batch=" << capacity << ':';
                for (const auto& event : observed) std::cerr << ' ' << event.first << (event.second ? "off" : "on");
                std::cerr << '\n';
            }
        }
}

void testCapturedOverlappingLanes()
{
    for (int shortLane : { 0, 1 })
        for (std::size_t capacity : { 1U, 16U })
        {
            auto state = bsl::ProjectState::makeDefault();
            for (auto& lane : state.patterns[0].lanes)
                for (auto& beat : lane.beats)
                    for (auto& slice : beat.slices) slice.enabled = false;
            for (int lane = 0; lane < 2; ++lane)
            {
                auto& slices = state.patterns[0].lanes[lane].beats[0].slices;
                slices.assign(lane == shortLane ? 4 : 1, { 0, 0.75f, false });
                slices[0].enabled = true;
            }
            bsl::SequencerPlayback playback;
            std::vector<std::pair<int, bool>> observed;
            std::array<bsl::PlaybackEvent, 16> events;
            for (int base = 0; base < 28800; base += 64)
            {
                bsl::TransportFrame frame { 48000.0, 100.0, base, 64, true };
                frame.quarterNotePosition = static_cast<double>(base) / 48000.0 * 100.0 / 60.0;
                frame.patternPhaseQuarterNotes = frame.quarterNotePosition;
                bsl::PlaybackCursor cursor;
                while (!cursor.exhausted)
                {
                    auto count = playback.renderBatch(state, frame, 0, std::span(events).first(capacity), cursor);
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        observed.emplace_back(base + events[i].event.sampleOffset, events[i].event.velocity == 0);
                        if (events[i].event.velocity == 0)
                            check(events[i].releaseVelocity == 95, "native release retains onset velocity");
                    }
                }
            }
            const std::vector<std::pair<int, bool>> expected {{0,false},{0,false},{7056,true},{28224,true}};
            check(observed == expected, "native overlapping lanes release independently at either batch capacity");
        }
}

void testCapturedWholePatternCallbackTiming()
{
    for (const auto block : {64, 257, 1024})
        for (const auto input : {1001, 10001})
        {
            if (input == 10001 && block != 1024)
                continue;
            bsl::PatternScheduler scheduler(0);
            std::array<bsl::PatternRenderSegment, 3> segments {};
            int switched = -1;
            for (int cursor = 0; cursor < 84000; cursor += block)
            {
                auto frame = transport(cursor, block,
                    static_cast<double>(cursor) / 48000.0 * 140.0 / 60.0);
                frame.bpm = 140.0;
                const std::array trigger { bsl::PatternTrigger {
                    input - cursor, 1, bsl::PatternQuantization::whole } };
                const bool receive = input >= cursor && input < cursor + block;
                const auto count = scheduler.planBlock(frame,
                    receive ? std::span<const bsl::PatternTrigger>(trigger)
                            : std::span<const bsl::PatternTrigger>(), segments);
                for (std::size_t i = 0; i < count; ++i)
                    if (switched < 0 && segments[i].patternId == 1)
                        switched = cursor + segments[i].sampleOffset;
            }
            const int expected = input < block ? 0 : (82285 / block) * block;
            check(switched == expected,
                  "captured whole-pattern switch uses the containing callback start");
        }
}

void testCapturedQuarterPatternCallbackTiming()
{
    for (const auto block : {1024})
        for (const auto input : {1001, 10001})
        {
            if (input == 10001 && block != 1024)
                continue;
            bsl::PatternScheduler scheduler(0);
            std::array<bsl::PatternRenderSegment, 3> segments {};
            int switched = -1;
            for (int cursor = 0; cursor < 84000; cursor += block)
            {
                auto frame = transport(cursor, block,
                    static_cast<double>(cursor) / 48000.0 * 140.0 / 60.0);
                frame.bpm = 140.0;
                const std::array trigger { bsl::PatternTrigger {
                    input - cursor, 1, bsl::PatternQuantization::quarter } };
                const bool receive = input >= cursor && input < cursor + block;
                const auto count = scheduler.planBlock(frame,
                    receive ? std::span<const bsl::PatternTrigger>(trigger)
                            : std::span<const bsl::PatternTrigger>(), segments);
                for (std::size_t i = 0; i < count; ++i)
                    if (switched < 0 && segments[i].patternId == 1)
                        switched = cursor + segments[i].sampleOffset;
            }
            const int expected = input < block ? 0 : 20480;
            check(switched == expected,
                  "captured quarter-pattern switch uses the containing callback start");
        }
}

void testCapturedRemainingPatternCallbackTiming()
{
    for (const auto mode : {bsl::PatternQuantization::eighth, bsl::PatternQuantization::half,
                            bsl::PatternQuantization::sixteenth, bsl::PatternQuantization::thirtySecond})
      for (const auto block : {1024})
        for (const auto input : {10001})
        {
            if (input == 10001 && block != 1024)
                continue;
            bsl::PatternScheduler scheduler(0);
            std::array<bsl::PatternRenderSegment, 3> segments {};
            int switched = -1;
            for (int cursor = 0; cursor < 84000; cursor += block)
            {
                auto frame = transport(cursor, block,
                    static_cast<double>(cursor) / 48000.0 * 140.0 / 60.0);
                frame.bpm = 140.0;
                const std::array trigger { bsl::PatternTrigger {
                    input - cursor, 1, mode } };
                const bool receive = input >= cursor && input < cursor + block;
                const auto count = scheduler.planBlock(frame,
                    receive ? std::span<const bsl::PatternTrigger>(trigger)
                            : std::span<const bsl::PatternTrigger>(), segments);
                for (std::size_t i = 0; i < count; ++i)
                    if (switched < 0 && segments[i].patternId == 1)
                        switched = cursor + segments[i].sampleOffset;
            }
            const int expected = mode == bsl::PatternQuantization::half ? 40960 : 10240;
            check(switched == expected,
                  "captured remaining quantized-pattern switch uses the containing callback start");
        }
}

void testAtomicResetRequestIsConsumedExactlyOnce()
{
    bsl::AudioThreadResetRequest request;
    check(!request.consume(), "fresh reset request is clear");
    request.request();
    request.request();
    check(request.consume(), "audio thread consumes a requested reset");
    check(!request.consume(), "one consume clears coalesced reset requests");
}

} // namespace

int main()
{
    testUnquantizedTriggerUsesEventOffset();
    testHostPpqOverridesSampleTimeline();
    testAbsoluteSampleAndBpmFallbackChoosesBoundary();
    testTempoChangesPreserveTheQuantizationGrid();
    testLatestTriggerForOneBoundaryWins();
    testBoundedCollectorRetainsTheLatestSameBoundaryTrigger();
    testMidBlockSplitAdvancesOldPhaseAndZerosNewPhase();
    testSegmentedRenderingEmitsNewPatternOnlyAtBoundary();
    testPendingTriggerSurvivesUntilFutureBlock();
    testStopClearsPendingAndPhaseState();
    testTransportDiscontinuityClearsPendingAndPhaseState();
    testHostStartAndSeekPhaseLockThePattern();
    testHostPhaseDoesNotAccumulateCallbackDrift();
    testCapturedWholePatternCallbackTiming();
    testCapturedQuarterPatternCallbackTiming();
    testCapturedRemainingPatternCallbackTiming();
    testAtomicResetRequestIsConsumedExactlyOnce();
    testCapturedDenseTiming();
    testCapturedCallbackReleaseLifecycle();
    testCapturedOverlappingLanes();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All scheduler tests passed\n";
    return EXIT_SUCCESS;
}
