#include "core/PatternScheduler.h"
#include "core/Sequencer.h"

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

void testAllQuantizationModesUseTheNextBoundary()
{
    struct Case
    {
        bsl::PatternQuantization quantization;
        int expectedOffset;
    };
    constexpr std::array cases {
        Case { bsl::PatternQuantization::off, 100 },
        Case { bsl::PatternQuantization::eighth, 12000 },
        Case { bsl::PatternQuantization::quarter, 24000 },
        Case { bsl::PatternQuantization::half, 48000 },
        Case { bsl::PatternQuantization::whole, 96000 },
        Case { static_cast<bsl::PatternQuantization>(5), 6000 },
        Case { static_cast<bsl::PatternQuantization>(6), 3000 },
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
    const auto count = scheduler.planBlock(transport(999999, 20000, 3.5), triggers, segments);
    check(count == 2 && segments[1].sampleOffset == 12000,
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
    check(count == 2 && segments[1].sampleOffset == 12000,
          "absolute samples and BPM provide the grid when host PPQ is absent");
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
    check(count == 2, "same-boundary triggers produce one change");
    check(count == 2 && segments[1].sampleOffset == 24000 && segments[1].patternId == 2,
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
    check(count == 2 && segments[1].sampleOffset == 24000
              && segments[1].patternId == 65,
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
        bsl::PatternTrigger { 1000, 4, bsl::PatternQuantization::eighth }
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
    check(eventCount == 1 && events[0].sampleOffset == 24000 && events[0].padIndex == 1,
          "segmented render emits the new pattern at its boundary and never before it");
}

void testPendingTriggerSurvivesUntilFutureBlock()
{
    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const std::array trigger {
        bsl::PatternTrigger { 1, 1, bsl::PatternQuantization::quarter }
    };
    auto count = scheduler.planBlock(transport(0, 12000, 0.0), trigger, segments);
    check(count == 1 && segments[0].patternId == 0,
          "future boundary does not activate in an earlier block");
    const std::array noTriggers = std::array<bsl::PatternTrigger, 0> {};
    count = scheduler.planBlock(transport(12000, 16000, 0.5), noTriggers, segments);
    check(count == 2 && segments[1].sampleOffset == 12000 && segments[1].patternId == 1,
          "queued trigger activates in the block containing its boundary");
}

void testStopClearsPendingAndPhaseState()
{
    bsl::PatternScheduler scheduler(0);
    std::array<bsl::PatternRenderSegment, 3> segments {};
    const std::array trigger {
        bsl::PatternTrigger { 100, 1, bsl::PatternQuantization::whole }
    };
    scheduler.planBlock(transport(0, 1000, 0.0), trigger, segments);
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
    scheduler.planBlock(transport(0, 1000, 0.0), trigger, segments);
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
    testAllQuantizationModesUseTheNextBoundary();
    testHostPpqOverridesSampleTimeline();
    testAbsoluteSampleAndBpmFallbackChoosesBoundary();
    testLatestTriggerForOneBoundaryWins();
    testBoundedCollectorRetainsTheLatestSameBoundaryTrigger();
    testMidBlockSplitAdvancesOldPhaseAndZerosNewPhase();
    testSegmentedRenderingEmitsNewPatternOnlyAtBoundary();
    testPendingTriggerSurvivesUntilFutureBlock();
    testStopClearsPendingAndPhaseState();
    testTransportDiscontinuityClearsPendingAndPhaseState();
    testHostStartAndSeekPhaseLockThePattern();
    testAtomicResetRequestIsConsumedExactlyOnce();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All scheduler tests passed\n";
    return EXIT_SUCCESS;
}
