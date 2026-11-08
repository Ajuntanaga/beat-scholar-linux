#include "core/Model.h"
#include "core/ProjectEditor.h"
#include "core/Randomizer.h"
#include "core/Sequencer.h"
#include "content/FactoryResourceScanner.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

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



void testHistorySharesImmutableSamplePayloadsAndTracksParameterOwnership()
{
    auto state = bsl::ProjectState::makeDefault();
    state.kit.pads[0].customSampleName = "synthetic.wav";
    state.kit.pads[0].customSampleSha256 = std::string(64, '0');
    state.kit.pads[0].customSampleBase64 = std::string(4 * 1024 * 1024, 'A');
    const auto* originalStorage = state.kit.pads[0].customSampleBase64.value().data();
    bsl::ProjectEditor editor(state);
    for (int step = 0; step < 128; ++step)
    {
        editor.checkpoint();
        editor.state().kit.pads[0].pan = step % 2 == 0 ? 0.25f : -0.25f;
    }
    for (int step = 0; step < 128; ++step)
    {
        check(editor.undo(), "retained history entry is undoable");
        check(editor.state().kit.pads[0].customSampleBase64.value().data() == originalStorage,
              "every undo snapshot shares the same immutable sample bytes");
        check(editor.restoredParameters() == bsl::HistoryParameters::none,
              "ordinary edits do not own host parameters");
    }
    editor.checkpoint(bsl::HistoryParameters::kit);
    editor.state().kit.pads[0].customSampleBase64 = std::string("TQ==");
    check(state.kit.pads[0].customSampleBase64.value().data() == originalStorage,
          "replacing a payload leaves earlier project copies immutable");
    check(editor.undo() && editor.restoredParameters() == bsl::HistoryParameters::kit
              && editor.state().kit.pads[0].customSampleBase64.value().data() == originalStorage,
          "kit undo restores shared payload and explicit parameter ownership");
    check(editor.redo() && editor.restoredParameters() == bsl::HistoryParameters::kit
              && editor.state().kit.pads[0].customSampleBase64.value() == "TQ==",
          "kit redo restores replacement payload and ownership");
    editor.checkpoint(bsl::HistoryParameters::masterGain);
    editor.state().master.gain = 0.7f;
    check(editor.undo() && editor.restoredParameters() == bsl::HistoryParameters::masterGain,
          "master-only edit has distinct history ownership");
}

void testQuantizedEventsBelongToExactlyOneAudioBlock()
{
    auto state = bsl::ProjectState::makeDefault();
    state.patterns[0].lanes = { bsl::LaneState::make(4) };
    for (auto& beat : state.patterns[0].lanes[0].beats)
        for (auto& slice : beat.slices) slice.enabled = false;
    state.patterns[0].lanes[0].beats[0].slices[0].enabled = true;
    bsl::TransportFrame frame;
    frame.playing = true;
    frame.blockSize = 64;
    for (const double fraction : { 0.4, 0.5 })
    {
        frame.patternPhaseQuarterNotes = (95936.0 + fraction) / 24000.0;
        const auto first = bsl::Sequencer::render(state, frame);
        frame.patternPhaseQuarterNotes += 64.0 / 24000.0;
        const auto second = bsl::Sequencer::render(state, frame);
        check(first.empty() && second.size() == 1 && second[0].sampleOffset == 0,
              "rounded boundary hit belongs once to the following callback");
    }
    frame.patternPhaseQuarterNotes = -0.001;
    const auto leadIn = bsl::Sequencer::render(state, frame);
    check(leadIn.size() == 1 && leadIn[0].sampleOffset == 24,
          "quantized membership retains negative lead-in timing");

    state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(7, 0, 0.8f);
    frame.patternPhaseQuarterNotes = std::numeric_limits<double>::quiet_NaN();
    frame.bpm = 137.0;
    frame.blockSize = 90000;
    const auto reference = bsl::Sequencer::render(state, frame);
    for (const int block : { 17, 64, 257, 511 })
    {
        std::vector<bsl::MidiEvent> actual;
        for (int start = 0; start < 90000; start += block)
        {
            frame.absoluteSampleStart = start;
            frame.blockSize = std::min(block, 90000 - start);
            for (auto event : bsl::Sequencer::render(state, frame))
            {
                check(event.sampleOffset >= 0 && event.sampleOffset < frame.blockSize,
                      "sequencer emits only valid callback-local offsets");
                event.sampleOffset += start;
                actual.push_back(event);
            }
        }
        check(actual == reference, "fractional-tempo notes and gates are independent of callback size");
    }
}

void testDefaultProjectMatchesDocumentedLimits()
{
    const auto state = bsl::ProjectState::makeDefault();
    check(state.patterns.size() == 1, "default project has one pattern");
    check(state.patterns.front().lanes.size() == 4, "default pattern has four lanes");
    check(state.kit.pads.size() == 16, "kit has sixteen pads");
    check(state.global.swingPercent == 0.0, "swing defaults to zero");
    check(state.global.swingReference == bsl::SwingReference::quarter,
          "The Swing Slider tutorial defaults to a quarter-note reference");
    check(bsl::Sequencer::applySwing(0.5, 100.0, state.global.swingReference) > 0.5
              && bsl::Sequencer::applySwing(1.0, 100.0, state.global.swingReference) == 1.0,
          "default swing delays eighth notes while leaving quarter notes fixed");
    check(std::abs(state.global.defaultVelocity - 0.8f) < 1.0e-7f,
          "velocity brush defaults to the documented 80 percent");
    check(state.global.auditionDuringPlayback, "pad audition during playback defaults on");
    check(!state.global.kitLocked, "kit selection defaults unlocked");
}

void testRandomizerInitPreset()
{
    const auto settings = bsl::RandomizerSettings::makeInitPreset();
    check(settings.isValid(), "named Randomizer Init is valid");
    check(settings.selectedPads.empty(), "Randomizer Init has no selected note filter");
    check(settings.selectedSubdivisions == std::vector<std::size_t> { 2, 4, 8 },
          "Randomizer Init selects subdivisions 2, 4 and 8");
    check(settings.noteSelectionEnabled && settings.slicingEnabled
              && settings.triggeringEnabled && settings.velocityEnabled,
          "Randomizer Init enables all four modules");
    check(!settings.fillEmptyOnly && settings.triggerChance == 0.5f && settings.spacing == 0.5f,
          "Randomizer Init replaces notes with 50 percent trigger chance and spacing");
    check(settings.minimumVelocity == 0.0f && settings.maximumVelocity == 1.0f,
          "Randomizer Init uses the full velocity range");
}

void testSubdivisionRemembersDisabledVelocity()
{
    auto beat = bsl::BeatState::withSubdivision(3, 0, 0.8f);
    beat.slices[1].velocity = 0.25f;
    beat.slices[1].enabled = false;
    beat.slices[1].enabled = true;
    check(std::abs(beat.slices[1].velocity - 0.25f) < 1.0e-7f,
          "disabled slices retain velocity");
}

void testSequencerProducesSampleAccurateTripletEvents()
{
    auto state = bsl::ProjectState::makeDefault();
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(4, 4, 1) };
    pattern.lanes = { bsl::LaneState::make(4) };
    pattern.lanes.front().beats[0] = bsl::BeatState::withSubdivision(3, 0, 1.0f);

    bsl::TransportFrame transport;
    transport.sampleRate = 48000.0;
    transport.bpm = 120.0;
    transport.absoluteSampleStart = 0;
    transport.blockSize = 48000;
    transport.playing = true;

    const auto events = bsl::Sequencer::render(state, transport);
    check(events.size() == 3, "triplet produces three MIDI events");
    if (events.size() == 3)
    {
        check(events[0].sampleOffset == 0, "first triplet starts at block start");
        check(events[1].sampleOffset == 8000, "second triplet is sample accurate");
        check(events[2].sampleOffset == 16000, "third triplet is sample accurate");
        check(events[0].gateFrames == 8000 && events[1].gateFrames == 8000
                  && events[2].gateFrames == 8000,
              "each triplet owns exactly one slice of the beat");
    }
}

void testRealtimeSequencerRenderUsesCallerOwnedBoundedStorage()
{
    auto state = bsl::ProjectState::makeDefault();
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(4, 4) };
    pattern.lanes = { bsl::LaneState::make(4) };
    pattern.lanes.front().beats[0] = bsl::BeatState::withSubdivision(3, 0, 1.0f);

    bsl::TransportFrame transport;
    transport.sampleRate = 48000.0;
    transport.bpm = 120.0;
    transport.blockSize = 48000;
    transport.playing = true;

    std::array<bsl::MidiEvent, 2> bounded {};
    const auto count = bsl::Sequencer::renderInto(state, transport, 0, bounded);
    check(count == bounded.size(), "real-time render respects caller-owned capacity");
    check(bounded[0].sampleOffset == 0 && bounded[1].sampleOffset == 8000,
          "bounded real-time render preserves sorted sample offsets");
}

void testNegativeSwingAdvancesOffGridEvents()
{
    auto state = bsl::ProjectState::makeDefault();
    state.global.swingReference = bsl::SwingReference::eighth; // This regression targets eighth-note swing.
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(4, 4) };
    pattern.lanes = { bsl::LaneState::make(4) };
    pattern.lanes.front().beats[0] = bsl::BeatState::withSubdivision(4, 0, 1.0f);
    bsl::TransportFrame frame;
    frame.sampleRate = 48000.0;
    frame.bpm = 120.0;
    frame.blockSize = 24000;
    frame.playing = true;
    frame.swingPercent = -100.0;
    const auto events = bsl::Sequencer::render(state, frame);
    check(events.size() == 4, "swung beat emits four slice gates");
    if (events.size() == 4)
    {
        check(events[0].gateFrames == 4000 && events[1].gateFrames == 8000
                  && events[2].gateFrames == 4000 && events[3].gateFrames == 8000,
              "slice gates follow swung boundaries, including the final beat boundary");
    }
    const auto straight = bsl::Sequencer::applySwing(0.25, 0.0, bsl::SwingReference::eighth);
    const auto pushed = bsl::Sequencer::applySwing(0.25, -100.0, bsl::SwingReference::eighth);
    check(std::abs(straight - 0.25) < 1.0e-12, "zero swing preserves beat position");
    check(pushed < straight, "negative swing advances off-grid events");
}


void testAddBarRejectsInvalidMeterWithoutHistory()
{
    for (const bsl::BarState meter : { bsl::BarState {65, 4}, bsl::BarState {4, 3}, bsl::BarState {0, 4} })
    {
        bsl::ProjectEditor editor(bsl::ProjectState::makeDefault());
        const auto bars = editor.state().patterns[0].bars.size();
        check(!editor.addBar(0, meter), "invalid raw bar meter rejected");
        check(editor.state().isValid() && editor.state().patterns[0].bars.size() == bars,
              "invalid meter leaves state valid and unchanged");
        check(!editor.undo(), "invalid meter creates no undo entry");
    }
}

void testSetBarMeterRejectsInvalidInputWithoutChangingHistory()
{
    for (const bool createUndoStep : { false, true })
        for (const bsl::BarState meter : { bsl::BarState {0, 3}, bsl::BarState {65, 4}, bsl::BarState {4, 3} })
        {
            auto state = bsl::ProjectState::makeDefault();
            for (auto& lane : state.patterns[0].lanes)
                for (auto& beat : lane.beats)
                    beat = bsl::BeatState::withSubdivision(3, 7, 0.3f);
            bsl::ProjectEditor editor(state);
            const auto revision = editor.revision();
            check(!editor.setBarMeter(0, 0, meter.numerator, meter.denominator, createUndoStep),
                  "invalid meter edit is rejected before normalization");
            const auto& pattern = editor.state().patterns[0];
            check(pattern.bars[0].numerator == 4 && pattern.bars[0].denominator == 4,
                  "invalid meter edit preserves the bar signature");
            for (std::size_t lane = 0; lane < pattern.lanes.size(); ++lane)
                check(pattern.lanes[lane].beats == state.patterns[0].lanes[lane].beats,
                      "invalid meter edit preserves every beat and slice");
            check(editor.revision() == revision && !editor.canUndo() && !editor.canRedo(),
                  "invalid meter edit leaves revision and history untouched");
        }
}

void testBarEditingKeepsEveryLaneAligned()
{
    bsl::ProjectEditor editor(bsl::ProjectState::makeDefault());
    check(editor.addBar(0, bsl::BarState::make(3, 8)), "adds a 3/8 bar");
    const auto& pattern = editor.state().patterns.front();
    check(pattern.bars.size() == 2, "pattern contains the new bar");
    for (const auto& lane : pattern.lanes)
        check(lane.beats.size() == 7, "bar edit adds three beat pizzas to every lane");
    check(editor.setBarMeter(0, 0, 5, 4), "changes the first bar meter");
    for (const auto& lane : editor.state().patterns.front().lanes)
        check(lane.beats.size() == 8, "meter edit resizes every lane without desynchronising it");
    check(editor.state().isValid(), "edited arrangement remains valid");
}

void testSameMeterBarPasteRetainsUndoCheckpoint()
{
    bsl::ProjectEditor editor(bsl::ProjectState::makeDefault());
    // The editor's bar-paste path applies the source meter before copying its
    // beats. Equal source/destination meters must still anchor the paste undo.
    editor.setBarMeter(0, 0, 4, 4);
    editor.state().patterns.front().lanes.front().beats.front() =
        bsl::BeatState::withSubdivision(3, 7, 0.5f);
    check(editor.undo(), "pasting a bar with the same meter is undoable");
    const auto& restored = editor.state().patterns.front().lanes.front().beats.front();
    check(restored.slices.size() == 1 && !restored.slices.front().enabled,
          "same-meter paste undo restores the previous beat contents");
}

void testUndoRedoRestoresCanonicalProjectState()
{
    bsl::ProjectEditor editor(bsl::ProjectState::makeDefault());
    check(editor.addLane(0), "adds a lane");
    check(editor.state().patterns.front().lanes.size() == 5, "lane was added");
    check(editor.canUndo() && editor.undo(), "undo is available");
    check(editor.state().patterns.front().lanes.size() == 4, "undo restores lane count");
    check(editor.canRedo() && editor.redo(), "redo is available");
    check(editor.state().patterns.front().lanes.size() == 5, "redo reapplies lane edit");
}

void testBeatTransformsPreserveNotesAndVelocities()
{
    auto beat = bsl::BeatState::withSubdivision(4, 0, 0.8f);
    beat.slices[0] = { 0, 0.2f, true };
    beat.slices[1] = { 1, 0.4f, false };
    beat.slices[2] = { 2, 0.6f, true };
    beat.slices[3] = { 3, 0.8f, false };
    beat.rotate(1);
    check(beat.slices[1].padIndex == 0 && std::abs(beat.slices[1].velocity - 0.2f) < 1.0e-7f,
          "rotation moves the entire slice state");
    beat.mirror();
    check(beat.slices[2].padIndex == 0, "mirror reverses slice order");
    beat.reset();
    check(std::none_of(beat.slices.begin(), beat.slices.end(), [](const auto& slice) {
              return slice.enabled;
          }), "reset disables all slices");
}

void testRandomizerIsDeterministicAndRespectsFillEmpty()
{
    bsl::RandomizerSettings settings;
    settings.selectedPads = { 1, 3, 5 };
    settings.selectedSubdivisions = { 3, 5, 7 };
    settings.slicingEnabled = true;
    settings.triggeringEnabled = true;
    settings.velocityEnabled = true;
    settings.fillEmptyOnly = true;
    settings.triggerChance = 0.55f;
    settings.spacing = 0.35f;
    settings.minimumVelocity = 0.2f;
    settings.maximumVelocity = 0.7f;

    auto first = bsl::BeatState::withSubdivision(4, 0, 0.9f);
    for (auto& slice : first.slices)
        slice.enabled = false;
    first.slices[0].enabled = true;
    first.slices[0].padIndex = 11;
    const auto originalFirstSlice = first.slices[0];
    auto second = first;
    bsl::Randomizer::apply(first, settings, 0x12345678u);
    bsl::Randomizer::apply(second, settings, 0x12345678u);

    check(first.slices == second.slices, "equal seeds produce identical randomizer output");
    check(first.slices.front() == originalFirstSlice,
          "fill-empty mode preserves an already enabled first slice through reslicing");
    check(first.slices.size() == 3 || first.slices.size() == 5 || first.slices.size() == 7,
          "randomizer chooses only configured subdivision values");
    for (const auto& slice : first.slices)
        if (slice.enabled && slice.padIndex != 11)
        {
            check(slice.padIndex == 1 || slice.padIndex == 3 || slice.padIndex == 5,
                  "randomizer chooses only selected pads");
            check(slice.velocity >= 0.2f && slice.velocity <= 0.7f,
                  "randomizer velocity stays in the configured range");
        }
}

void testFillEmptySpacingFollowsPreservedHits()
{
    bsl::RandomizerSettings settings;
    settings.slicingEnabled = false;
    settings.noteSelectionEnabled = false;
    settings.velocityEnabled = false;
    settings.triggeringEnabled = true;
    settings.fillEmptyOnly = true;
    settings.triggerChance = 0.0f;
    settings.spacing = 0.0f;

    auto beat = bsl::BeatState::withSubdivision(2, 11, 0.25f);
    beat.slices[1].enabled = false;
    const auto preserved = beat.slices[0];
    bsl::Randomizer::apply(beat, settings, 7u);
    check(beat.slices[0] == preserved, "Fill Empty leaves the existing hit untouched");
    check(beat.slices[1].enabled, "Spacing follows a preserved hit in Fill Empty mode");

    settings.spacing = 1.0f;
    settings.triggerChance = 0.9f;
    auto separated = bsl::BeatState::withSubdivision(2, 11, 0.25f);
    separated.slices[1].enabled = false;
    bsl::Randomizer::apply(separated, settings, 7u);
    check(separated.slices[0] == preserved && !separated.slices[1].enabled,
          "high Spacing separates hits even with 90 percent Trigger Chance");
    settings.triggerChance = 1.0f;
    bsl::Randomizer::apply(separated, settings, 7u);
    check(separated.slices[1].enabled,
          "100 percent Trigger Chance still enables every candidate at high Spacing");
    settings.triggerChance = 0.0f;
    settings.spacing = 0.0f;

    auto state = bsl::ProjectState::makeDefault();
    state.patterns[0].bars = { bsl::BarState::make(2, 4) };
    state.patterns[0].lanes = { bsl::LaneState::make(2) };
    state.patterns[0].lanes[0].beats[0].slices[0] = preserved;
    state.randomizer = settings;
    check(state.isValid(), "Fill Empty lane fixture is a valid project");
    bsl::ProjectEditor editor(state);
    check(editor.randomizeLane(0, 0, 7u), "Fill Empty lane edit succeeds");
    const auto& lane = editor.state().patterns[0].lanes[0];
    check(lane.beats[0].slices[0] == preserved,
          "lane Fill Empty keeps the existing hit's pad and velocity");
    check(lane.beats[1].slices[0].enabled,
          "Spacing follows a preserved hit across the beat boundary");
}

void testLaneRandomizerSpacingCrossesBeatBoundaries()
{
    auto state = bsl::ProjectState::makeDefault();
    state.patterns[0].bars = { bsl::BarState::make(2, 4) };
    state.patterns[0].lanes = { bsl::LaneState::make(2) };
    check(state.isValid(), "lane spacing fixture is a valid project");
    auto& settings = state.randomizer;
    settings.slicingEnabled = false;
    settings.noteSelectionEnabled = false;
    settings.velocityEnabled = false;
    settings.triggeringEnabled = true;
    settings.fillEmptyOnly = false;
    settings.triggerChance = 0.5f;
    settings.spacing = 0.0f;
    for (const auto seed : { 1u, 5u })
    {
        auto lane = state.patterns[0].lanes[0];
        bsl::Randomizer::apply(lane, settings, seed);
        check(lane.beats[0].slices[0].enabled,
              "spacing fixture starts with a triggered step");
        check(lane.beats[1].slices[0].enabled,
              "low spacing clusters into the following beat");
        bsl::ProjectEditor editor(state);
        check(editor.randomizeLane(0, 0, seed), "lane spacing edit succeeds");
        const auto& edited = editor.state().patterns[0].lanes[0];
        check(edited.beats[0].slices[0].enabled && edited.beats[1].slices[0].enabled,
              "lane randomizer control preserves clusters across beats");
        check(edited.beats == lane.beats,
              "lane randomizer control uses the lane randomizer semantics");
    }
}

void testRandomizerUsesDefaultVelocityWhenVelocityModuleIsOff()
{
    bsl::RandomizerSettings settings;
    settings.slicingEnabled = false;
    settings.noteSelectionEnabled = false;
    settings.triggeringEnabled = true;
    settings.velocityEnabled = false;
    settings.triggerChance = 1.0f;
    auto beat = bsl::BeatState::withSubdivision(3, 0, 0.91f);
    for (auto& slice : beat.slices)
        slice.enabled = false;
    bsl::Randomizer::apply(beat, settings, 123u, 0.37f);
    check(std::all_of(beat.slices.begin(), beat.slices.end(), [](const auto& slice) {
              return slice.enabled && std::abs(slice.velocity - 0.37f) < 1.0e-7f;
          }), "disabled randomizer velocity module applies the brush default velocity");
}

void testPatternLifecycleKeepsTriggerNotesUnique()
{
    bsl::ProjectEditor editor(bsl::ProjectState::makeDefault());
    check(editor.addPattern() && editor.addPattern(), "add two uniquely triggered patterns");
    check(editor.removePattern(1) && editor.addPattern(), "remove and replace a middle pattern");
    const auto distinct = [&editor] {
        std::array<bool, 128> used {};
        for (const auto& pattern : editor.state().patterns)
        {
            if (used[pattern.triggerNote]) return false;
            used[pattern.triggerNote] = true;
        }
        return true;
    };
    check(distinct(), "add after deletion does not reuse an occupied trigger note");
    check(editor.removePattern(2) && editor.duplicatePattern(0), "duplicate after deletion");
    check(distinct(), "duplicate after deletion does not reuse an occupied trigger note");
    bsl::ProjectEditor mapped(bsl::ProjectState::makeDefault());
    check(mapped.addPattern() && mapped.addPattern(), "prepare occupied trigger notes");
    check(mapped.shiftPatternTriggerNote(0, 1) && mapped.state().patterns[0].triggerNote == 15,
          "trigger-note up skips notes already assigned to other patterns");
    check(mapped.undo() && mapped.state().patterns[0].triggerNote == 12,
          "trigger-note adjustment undoes in one step");
    check(mapped.shiftPatternTriggerNote(2, -1) && mapped.state().patterns[2].triggerNote == 11,
          "trigger-note down skips notes already assigned to other patterns");
    auto invalid = bsl::ProjectState::makeDefault();
    invalid.patterns.push_back(invalid.patterns.front());
    check(!invalid.isValid(), "model rejects unreachable duplicate pattern triggers");
}


void testRemovingEarlierPatternPreservesSelection()
{
    bsl::ProjectEditor editor(bsl::ProjectState::makeDefault());
    editor.addPattern();
    editor.addPattern();
    editor.state().patterns[1].name = "Keep selected";
    editor.state().activePattern = 1;
    check(editor.removePattern(0), "remove pattern before selection");
    check(editor.state().patterns[editor.state().activePattern].name == "Keep selected",
          "removing an earlier pattern preserves the selected pattern");
    check(editor.undo() && editor.state().activePattern == 1,
          "undo restores the selected pattern's original position");
}

void testPatternTriggerNotesSelectPatterns()
{
    auto state = bsl::ProjectState::makeDefault();
    auto second = state.patterns.front();
    second.name = "Pattern 2";
    second.triggerNote = 13;
    state.patterns.push_back(second);
    check(state.selectPatternForTrigger(13), "matching MIDI note selects pattern");
    check(state.activePattern == 1, "active pattern follows its trigger note");
    check(!state.selectPatternForTrigger(90), "unassigned MIDI note leaves pattern unchanged");
}

void testPatternMidiExportPreservesTripletTimingAndVelocity()
{
    auto state = bsl::ProjectState::makeDefault();
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(3, 4) };
    pattern.lanes = { bsl::LaneState::make(3) };
    state.kit.pads[6].midiNote = 42;
    pattern.lanes.front().beats[0] = bsl::BeatState::withSubdivision(3, 6, 0.5f);
    const auto events = bsl::Sequencer::exportPattern(state, 0, 960);
    check(events.size() == 3, "MIDI export emits every enabled triplet slice");
    if (events.size() == 3)
    {
        check(events[0].tick == 0 && events[1].tick == 320 && events[2].tick == 640,
              "MIDI export keeps exact triplet PPQ positions");
        check(events[0].note == 42 && events[0].velocity == 64,
              "MIDI export keeps note mapping and rounded velocity");
        check(std::all_of(events.begin(), events.end(), [](const auto& event) {
            return event.gateTicks == 320;
        }), "MIDI export gates span the triplet subdivision");
    }
}

void testSwungMidiExportKeepsThePlaybackLoopPhase()
{
    auto state = bsl::ProjectState::makeDefault();
    state.global.swingReference = bsl::SwingReference::eighth; // This regression targets eighth-note swing.
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(1, 16) };
    pattern.lanes = { bsl::LaneState::make(1) };
    auto& beat = pattern.lanes.front().beats.front();
    beat = bsl::BeatState::withSubdivision(3, 0, 0.8f);
    beat.slices[0].enabled = beat.slices[1].enabled = false;
    state.global.swingPercent = 200.0;
    const auto tail = bsl::Sequencer::exportPattern(state, 0, 960);
    check(tail.size() == 1 && tail[0].tick == 27,
          "swung tail wraps into the MIDI clip at the same phase as looped playback");

    for (const auto reference : { bsl::SwingReference::eighth, bsl::SwingReference::quarter,
                                   bsl::SwingReference::half })
        for (const double swing : { -200.0, 0.0, 200.0 })
            for (const int subdivisions : { 3, 7, 45 })
            {
                state.global.swingReference = reference;
                state.global.swingPercent = swing;
                beat = bsl::BeatState::withSubdivision(subdivisions, 0, 0.8f);
                const auto exported = bsl::Sequencer::exportPattern(state, 0, 960);
                bsl::TransportFrame frame;
                frame.sampleRate = 1920.0; // One sample per MIDI tick at 120 BPM.
                frame.bpm = 120.0;
                frame.absoluteSampleStart = 240;
                frame.blockSize = 240;
                frame.playing = true;
                const auto played = bsl::Sequencer::render(state, frame);
                check(exported.size() == played.size(), "export retains every swung playback hit");
                for (std::size_t index = 0; index < std::min(exported.size(), played.size()); ++index)
                {
                    check(exported[index].tick == played[index].sampleOffset,
                          "exported short-meter swing agrees with the playback loop");
                    check(exported[index].gateTicks == played[index].gateFrames,
                          "exported short-meter gates agree with swung playback");
                }
                check(std::all_of(exported.begin(), exported.end(), [](const auto& event) {
                    return event.tick >= 0 && event.tick < 240;
                }), "every swung export note starts inside the meter-defined clip");
            }
}

void testPadIdentityIsIndependentOfMidiMapping()
{
    auto state = bsl::ProjectState::makeDefault();
    state.kit.pads[0].midiNote = 77;
    state.patterns.front().lanes.front().beats.front().slices.front() = { 0, 1.0f, true };
    bsl::TransportFrame transport;
    transport.sampleRate = 48000.0;
    transport.bpm = 120.0;
    transport.blockSize = 1;
    transport.playing = true;
    const auto events = bsl::Sequencer::render(state, transport);
    check(events.size() == 1 && events.front().note == 77 && events.front().padIndex == 0,
          "sequencer maps a stable pad identity through the configured MIDI note");
    check(state.patterns.front().lanes.front().beats.front().slices.front().padIndex == 0,
          "remapping MIDI does not mutate slice pad identity");
    check(state.padIndexForMidiNote(77) == std::optional<std::size_t> { 0 },
          "incoming remapped MIDI selects the configured pad");
    check(!state.padIndexForMidiNote(36),
          "incoming MIDI no longer assumes the legacy 36-based pad identity");
}

void testLaneRandomizationUsesOneUndoCheckpointAndStableBeatSeeds()
{
    auto state = bsl::ProjectState::makeDefault();
    state.randomizer.selectedPads = { 2, 4, 6 };
    state.randomizer.selectedSubdivisions = { 3, 5 };
    const auto original = state.patterns.front().lanes.front().beats;
    bsl::ProjectEditor first(state);
    bsl::ProjectEditor second(state);
    check(first.randomizeLane(0, 0, 0x10203040u), "lane randomization succeeds");
    check(second.randomizeLane(0, 0, 0x10203040u), "repeat lane randomization succeeds");
    check(first.state().patterns.front().lanes.front().beats
              == second.state().patterns.front().lanes.front().beats,
          "lane randomization is deterministic for a seed");
    check(first.canUndo() && first.undo(), "lane randomization creates one undo checkpoint");
    check(first.state().patterns.front().lanes.front().beats == original,
          "one undo restores the complete lane");
    check(!first.canUndo(), "lane randomization does not create per-beat checkpoints");
}

void testLaneDefaultsContainValidPadIndices()
{
    const auto lane = bsl::LaneState::make(4);
    auto state = bsl::ProjectState::makeDefault();
    state.patterns.front().lanes = { lane };
    check(state.isValid(), "fresh lane defaults use pad index zero and validate");
}

void testFactoryScannerRejectsIncidentalFlacTextAndWildcardName()
{
    const std::vector<std::uint8_t> bytes {
        'x', 'f', 'L', 'a', 'C', 't', 'e', 'x', 't', 0,
        'f', 'L', 'a', 'C', 0, 0, 0, 34, 1, 2, 3, 4,
        0, 'K', 'i', 'c', 'k', '.', 'f', 'l', 'a', 'c', 0,
        '*', '.', 'w', 'a', 'v', ';', '*', '.', 'f', 'l', 'a', 'c', 0,
        'S', 'n', 'a', 'r', 'e', '.', 'f', 'l', 'a', 'c', 0
    };
    const auto offsets = bsl::content::findFlacStreams(bytes);
    const auto names = bsl::content::findFlacResourceNames(bytes, 20);
    check(offsets == std::vector<std::size_t>({ 10 }),
          "scanner accepts only FLAC streams with a valid STREAMINFO header");
    check(names == std::vector<std::string>({ "Kick.flac", "Snare.flac" }),
          "scanner keeps resource names and excludes chooser wildcards");
}

void testFactoryNamesFollowPePointersNotPhysicalStringOrder()
{
    std::vector<std::uint8_t> bytes(0x1000, 0);
    const auto write = [&](std::size_t offset, std::uint64_t value, int length) {
        for (int i = 0; i < length; ++i)
            bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    bytes[0] = 'M'; bytes[1] = 'Z';
    write(0x3c, 0x80, 4);
    bytes[0x80] = 'P'; bytes[0x81] = 'E';
    write(0x86, 1, 2); // one section
    write(0x94, 0xf0, 2); // optional header length
    write(0x98, 0x20b, 2); // PE32+
    write(0xb0, 0x180000000ULL, 8);
    write(0x188 + 12, 0x1000, 4); // section RVA
    write(0x188 + 16, 0x800, 4); // raw size
    write(0x188 + 20, 0x200, 4); // raw offset
    const std::string snare = "Snare.flac", kick = "Kick.flac";
    std::copy(snare.begin(), snare.end(), bytes.begin() + 0x300);
    std::copy(kick.begin(), kick.end(), bytes.begin() + 0x340);
    write(0x600, 0x180001140ULL, 8); // Kick first in resource array
    write(0x608, 0x180001100ULL, 8); // Snare second
    check(bsl::content::findPeFlacResourceNames(bytes, 0x300)
              == std::vector<std::string>({ "Kick.flac", "Snare.flac" }),
          "factory filenames follow the PE pointer array, not linker string order");
    write(0x608, 0x180001140ULL, 8);
    check(bsl::content::findPeFlacResourceNames(bytes, 0x300).empty(),
          "duplicate or incomplete filename mapping is rejected");
    check(bsl::content::findPeFlacResourceNames(std::span(bytes).first(0x99)).empty(),
          "truncated PE headers are rejected");
}

void testSequencerBatchesPreserveSimultaneousEvents()
{
    auto state = bsl::ProjectState::makeDefault();
    auto& pattern = state.patterns[0];
    pattern.bars = { bsl::BarState::make(1, 16) };
    pattern.lanes.assign(16, bsl::LaneState::make(1));
    for (auto& lane : pattern.lanes)
        lane.beats[0] = bsl::BeatState::withSubdivision(45, 0, 0.5f);
    bsl::TransportFrame frame { 48000.0, 300.0, 0, 16384, true };
    for (const auto swing : { -200.0, 0.0, 200.0 })
    {
        frame.swingPercent = swing;
        const auto expected = bsl::Sequencer::render(state, frame);
        std::array<bsl::SequencerBatchEvent, 17> storage;
        bsl::SequencerBatchCursor cursor;
        std::vector<bsl::MidiEvent> actual;
        while (!cursor.exhausted)
        {
            const auto count = bsl::Sequencer::renderBatch(state, frame, 0, storage, cursor);
            for (std::size_t n = 0; n < count; ++n) actual.push_back(storage[n].event);
        }
        check(actual == expected, "bounded batches preserve all simultaneous events at extreme swing");
    }
}

void testBatchWindowsMatchFullRender()
{
    auto state = bsl::ProjectState::makeDefault();
    auto& pattern = state.patterns[0];
    for (const int beats : { 1, 64 })
    {
        pattern.bars = { bsl::BarState::make(beats, 16) };
        pattern.lanes.assign(16, bsl::LaneState::make(static_cast<std::size_t>(beats)));
        for (std::size_t laneIndex = 0; laneIndex < pattern.lanes.size(); ++laneIndex)
            for (auto& beat : pattern.lanes[laneIndex].beats)
                beat = bsl::BeatState::withSubdivision(45, static_cast<std::uint8_t>(laneIndex), 0.5f);
        for (const auto reference : { bsl::SwingReference::eighth, bsl::SwingReference::quarter, bsl::SwingReference::half })
        for (const double swing : { -200.0, 0.0, 200.0 })
        for (const double phase : { 0.0, 0.24996, 16.125 })
        {
            state.global.swingReference = reference;
            bsl::TransportFrame frame { 48000.0, 300.0, 0, 165000, true };
            frame.swingPercent = swing;
            frame.patternPhaseQuarterNotes = phase;
            const auto expected = bsl::Sequencer::render(state, frame);
            std::array<bsl::SequencerBatchEvent, 4096> storage;
            bsl::SequencerBatchCursor cursor;
            std::vector<bsl::MidiEvent> actual;
            while (!cursor.exhausted && actual.size() <= expected.size())
            {
                const auto count = bsl::Sequencer::renderBatch(state, frame, 0, storage, cursor);
                for (std::size_t n = 0; n < count; ++n) actual.push_back(storage[n].event);
            }
            check(cursor.exhausted && actual == expected,
                  "large batch windows match full render across swing references and loop boundaries");
        }
    }
}

} // namespace

int main()
{
    testSequencerBatchesPreserveSimultaneousEvents();
    testBatchWindowsMatchFullRender();
    testHistorySharesImmutableSamplePayloadsAndTracksParameterOwnership();
    testQuantizedEventsBelongToExactlyOneAudioBlock();
    testDefaultProjectMatchesDocumentedLimits();
    testRandomizerInitPreset();
    testSubdivisionRemembersDisabledVelocity();
    testSequencerProducesSampleAccurateTripletEvents();
    testRealtimeSequencerRenderUsesCallerOwnedBoundedStorage();
    testNegativeSwingAdvancesOffGridEvents();
    testAddBarRejectsInvalidMeterWithoutHistory();
    testSetBarMeterRejectsInvalidInputWithoutChangingHistory();
    testBarEditingKeepsEveryLaneAligned();
    testSameMeterBarPasteRetainsUndoCheckpoint();
    testUndoRedoRestoresCanonicalProjectState();
    testBeatTransformsPreserveNotesAndVelocities();
    testRandomizerIsDeterministicAndRespectsFillEmpty();
    testFillEmptySpacingFollowsPreservedHits();
    testLaneRandomizerSpacingCrossesBeatBoundaries();
    testRandomizerUsesDefaultVelocityWhenVelocityModuleIsOff();
    testPatternLifecycleKeepsTriggerNotesUnique();
    testRemovingEarlierPatternPreservesSelection();
    testPatternTriggerNotesSelectPatterns();
    testPatternMidiExportPreservesTripletTimingAndVelocity();
    testSwungMidiExportKeepsThePlaybackLoopPhase();
    testPadIdentityIsIndependentOfMidiMapping();
    testLaneRandomizationUsesOneUndoCheckpointAndStableBeatSeeds();
    testLaneDefaultsContainValidPadIndices();
    testFactoryScannerRejectsIncidentalFlacTextAndWildcardName();
    testFactoryNamesFollowPePointersNotPhysicalStringOrder();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
