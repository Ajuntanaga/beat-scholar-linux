#include "plugin/StateCodec.h"
#include "audio/AudioEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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


void testEmbeddedSampleBudgetMatchesLoaderReplacementSemantics()
{
    using namespace bsl::statecodec;
    check(fitsEmbeddedSampleBudget(0, maximumEmbeddedSampleBytes), "one sample may fill the storage budget");
    check(!fitsEmbeddedSampleBudget(1, maximumEmbeddedSampleBytes), "aggregate budget rejects an extra byte");
    check(!fitsEmbeddedSampleBudget(maximumEmbeddedSampleBytes + 1, 0), "retained overflow is rejected");
    check(!fitsEmbeddedSampleBudget(1, std::numeric_limits<std::size_t>::max()), "incoming size cannot overflow budget arithmetic");
    auto kit = bsl::KitState {};
    kit.pads[0].customSampleBase64 = "TQ=="; // One decoded byte.
    kit.pads[1].customSampleBase64 = "TWE="; // Two decoded bytes.
    check(canReplaceEmbeddedSample(kit, 0, maximumEmbeddedSampleBytes - 2),
          "replacing a pad excludes its old payload and counts other-pad padding");
    check(!canReplaceEmbeddedSample(kit, 0, maximumEmbeddedSampleBytes - 1),
          "replacement rejects aggregate overflow");
    check(canReplaceEmbeddedSample(kit, 1, maximumEmbeddedSampleBytes - 1),
          "replacement retains only the other pad's one decoded byte");
    check(!canReplaceEmbeddedSample(kit, 16, 1), "invalid replacement pad rejected");
}

void testNativeStateRoundTripIncludesProjectAndEmbeddedSample()
{
    auto source = bsl::ProjectState::makeDefault();
    source.global.swingPercent = -37.5;
    source.global.defaultVelocity = 0.63f;
    source.global.auditionDuringPlayback = false;
    source.global.kitLocked = true;
    source.patterns.front().name = "Odd Meter";
    source.patterns.front().quantization = bsl::PatternQuantization::half;
    source.patterns.front().bars = { bsl::BarState::make(7, 8) };
    for (auto& lane : source.patterns.front().lanes)
        lane.beats.assign(7, bsl::BeatState::withSubdivision(5, 2, 0.42f));
    source.kit.pads[2].customSampleName = "private-rim.flac";
    source.kit.pads[2].customSampleSha256 =
        "95f9e7a66bfb10d3c609f2c88ef3566ae3239ab4e025d0e661f7779f62c02ada";
    source.kit.pads[2].customSampleBase64 = "RkxBQw==";
    source.kit.pads[2].pitchSemitones = -4.0f;
    source.kit.pads[2].filterEnabled = false;
    source.kit.pads[2].attackNormalized = 0.35f;
    source.kit.pads[2].rootNote = 61;
    source.kit.pads[2].driveEnabled = true;
    source.kit.pads[2].driveGainDb = 8.0f;
    source.kit.pads[2].distortionMix = 0.27f;
    source.kit.pads[2].independentDistortion = true;
    source.kit.pads[2].distortionDrive = 0.73f;
    source.kit.pads[2].delay = 0.42f;
    source.master.compression = 0.63f;
    source.master.delay = 0.2f;
    source.master.distortion = 0.3f;
    source.master.reverb = 0.4f;
    source.master.room = 0.5f;
    source.randomizer.selectedPads = { 0, 3, 7 };
    source.randomizer.selectedSubdivisions = { 3, 11, 17 };
    source.ui.selectedPad = 2;
    source.ui.presetFile = "/synthetic/Named Preset.bslpreset";
    source.ui.kitFile = "/synthetic/Named Kit.bslkit";
    source.ui.randomizerFile = "/synthetic/Named Randomizer.bslrandom";

    check(source.isValid(), "source state validates before encoding");
    check(bsl::statecodec::encodePresetJson(source).isNotEmpty(),
          "source embedded sample passes codec validation");
    const auto encoded = bsl::statecodec::encode(source);
    check(encoded.getSize() > 16, "encoded state has a versioned compressed payload");
    check(std::memcmp(encoded.getData(), "BSLX", 4) == 0, "encoded state uses BSLX magic");
    juce::MemoryInputStream encodedInput(encoded.getData(), encoded.getSize(), false);
    encodedInput.skipNextBytes(8);
    juce::GZIPDecompressorInputStream compressedJson(&encodedInput, false);
    const auto nativeJson = compressedJson.readEntireStreamAsString();
    check(nativeJson.isNotEmpty(), "compressed native JSON can be read at the envelope boundary");
    check(juce::JSON::parse(nativeJson).isObject(), "native JSON parses before model conversion");

    auto restored = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::decode(encoded.getData(), encoded.getSize(), restored),
          "native state decodes successfully");
    check(restored.isValid(), "decoded project validates before publication");
    check(restored.ui.presetFile == source.ui.presetFile
          && restored.ui.kitFile == source.ui.kitFile
          && restored.ui.randomizerFile == source.ui.randomizerFile,
          "content identity survives the host-state envelope");
    check(restored.global.swingPercent == -37.5, "global settings round trip");
    check(std::abs(restored.global.defaultVelocity - 0.63f) < 1.0e-6f
              && !restored.global.auditionDuringPlayback && restored.global.kitLocked,
          "manual kit and velocity workflow settings round trip");
    check(restored.patterns.front().bars.front().numerator == 7
              && restored.patterns.front().bars.front().denominator == 8,
          "variable meter arrangement round trips");
    check(restored.patterns.front().quantization == bsl::PatternQuantization::half,
          "pattern quantization round trips");
    check(restored.patterns.front().lanes.front().beats.front().slices.size() == 5,
          "subdivision state round trips");
    check(restored.kit.pads[2].customSampleBase64.value() == "RkxBQw==",
          "embedded custom sample bytes round trip");
    check(std::abs(restored.master.compression - 0.63f) < 1.0e-6f,
          "master processing state round trips");
    check(!restored.kit.pads[2].filterEnabled && restored.kit.pads[2].driveEnabled
              && restored.kit.pads[2].independentDistortion
              && std::abs(restored.kit.pads[2].distortionDrive - 0.73f) < 1.0e-6f
              && std::abs(restored.kit.pads[2].driveGainDb - 8.0f) < 1.0e-6f
              && std::abs(restored.kit.pads[2].distortionMix - 0.27f) < 1.0e-6f
              && std::abs(restored.kit.pads[2].delay - 0.42f) < 1.0e-6f,
          "expanded pad processing state round trips");
    check(std::abs(restored.master.delay - 0.2f) < 1.0e-6f
              && std::abs(restored.master.room - 0.5f) < 1.0e-6f,
          "expanded master processing state round trips");
    check(restored.randomizer.selectedSubdivisions == std::vector<std::size_t>({ 3, 11, 17 }),
          "randomizer state round trips");
}

void testExtendedPatternQuantizationCodecRoundTrips()
{
    struct Case
    {
        bsl::PatternQuantization value;
        const char* text;
    };
    // Values are deliberately numeric until production adds named enum members.
    constexpr std::array cases {
        Case { static_cast<bsl::PatternQuantization>(5), "sixteenth" },
        Case { static_cast<bsl::PatternQuantization>(6), "thirtySecond" },
    };
    for (const auto test : cases)
    {
        auto source = bsl::ProjectState::makeDefault();
        source.patterns.front().quantization = test.value;
        const auto json = bsl::statecodec::encodePresetJson(source);
        check(json.contains(juce::String(test.text)),
              "extended pattern quantization writes its stable text name");
        auto restored = bsl::ProjectState::makeDefault();
        check(bsl::statecodec::decodePresetJson(json, restored)
                  && restored.patterns.front().quantization == test.value,
              "extended pattern quantization reads its stable text name");
    }
}
void testDuplicateNativePatternTriggersAreNormalizedOnLoad()
{
    auto source = bsl::ProjectState::makeDefault();
    auto second = source.patterns.front();
    second.name = "Pattern 2";
    second.triggerNote = 13;
    source.patterns.push_back(second);
    auto third = second;
    third.name = "Pattern 3";
    third.triggerNote = 14;
    source.patterns.push_back(third);
    auto root = juce::JSON::parse(bsl::statecodec::encodePresetJson(source));
    auto* patterns = root.getProperty("patterns", {}).getArray();
    patterns->getReference(1).getDynamicObject()->setProperty("triggerNote", 12);

    patterns->getReference(2).getDynamicObject()->setProperty("triggerNote", 13);
    auto restored = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::decodePresetJson(juce::JSON::toString(root), restored),
          "legacy native state with duplicate pattern trigger notes still loads");
    check(restored.patterns.size() == 3 && restored.patterns[0].triggerNote == 12
              && restored.patterns[1].triggerNote == 14
              && restored.patterns[2].triggerNote == 13,
          "load keeps the first trigger note and deterministically repairs the collision");
}
void testCorruptStateIsRejectedAtomically()
{
    auto destination = bsl::ProjectState::makeDefault();
    destination.patterns.front().name = "Keep Me";
    const char corrupt[] = "BSLX\x02\x00\x00\x00garbage";
    check(!bsl::statecodec::decode(corrupt, sizeof(corrupt), destination),
          "unsupported or corrupt state is rejected");
    check(destination.patterns.front().name == "Keep Me",
          "failed decode leaves published state untouched");
}

juce::String makeVersionOnePreset()
{
    auto source = bsl::ProjectState::makeDefault();
    source.kit.pads[0].midiNote = 60;
    source.kit.pads[1].midiNote = 60;
    source.kit.pads[2].midiNote = 99;
    source.patterns.front().lanes.front().beats.front() =
        bsl::BeatState::withSubdivision(2, 0, 0.75f);
    auto root = juce::JSON::parse(bsl::statecodec::encodePresetJson(source));
    root.getDynamicObject()->setProperty("schemaVersion", 1);
    if (auto* pads = root.getProperty("pads", {}).getArray())
        for (auto& encodedPad : *pads)
        {
            auto* object = encodedPad.getDynamicObject();
            object->setProperty("note", object->getProperty("midiNote"));
            object->removeProperty("midiNote");
        }
    if (auto* patterns = root.getProperty("patterns", {}).getArray())
        for (auto& encodedPattern : *patterns)
        {
            encodedPattern.getDynamicObject()->removeProperty("quantization");
            if (auto* lanes = encodedPattern.getProperty("lanes", {}).getArray())
                for (auto& lane : *lanes)
                    if (auto* beats = lane.getProperty("beats", {}).getArray())
                        for (auto& beat : *beats)
                            if (auto* slices = beat.getArray())
                                for (auto& slice : *slices)
                                {
                                    auto* object = slice.getDynamicObject();
                                    const int padIndex = static_cast<int>(object->getProperty("padIndex"));
                                    object->setProperty("note", 36 + padIndex);
                                    object->removeProperty("padIndex");
                                }
        }
    auto* firstBeat = root.getProperty("patterns", {}).getArray()->getReference(0)
                          .getProperty("lanes", {}).getArray()->getReference(0)
                          .getProperty("beats", {}).getArray()->getReference(0).getArray();
    firstBeat->getReference(0).getDynamicObject()->setProperty("note", 60);
    firstBeat->getReference(1).getDynamicObject()->setProperty("note", 38);
    return juce::JSON::toString(root, true);
}

void testVersionOneMigrationDecouplesPadIdentityFromMidi()
{
    auto versionOne = juce::JSON::parse(makeVersionOnePreset());
    auto* embeddedPad = versionOne.getProperty("pads", {}).getArray()->getReference(0)
                            .getDynamicObject();
    const juce::MemoryBlock embeddedBytes("FLAC", 4);
    embeddedPad->setProperty("customSampleName", "legacy.flac");
    embeddedPad->setProperty("customSampleSha256",
                             "95f9e7a66bfb10d3c609f2c88ef3566ae3239ab4e025d0e661f7779f62c02ada");
    embeddedPad->setProperty("customSampleBase64", embeddedBytes.toBase64Encoding());

    auto restored = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::decodePresetJson(juce::JSON::toString(versionOne), restored),
          "schema v1 preset migrates to schema v2");
    check(restored.schemaVersion == 2, "migration publishes schema v2");
    const auto& slices = restored.patterns.front().lanes.front().beats.front().slices;
    check(slices[0].padIndex == 0,
          "v1 MIDI note resolves to the first matching configured pad");
    check(slices[1].padIndex == 2,
          "unmatched v1 MIDI note falls back to the legacy 36-based pad index");
    check(restored.patterns.front().quantization == bsl::PatternQuantization::whole,
          "v1 patterns migrate to whole-note quantization");
    check(restored.kit.pads[0].customSampleBase64.value() == "RkxBQw==",
          "bounded v1 JUCE base64 migrates to canonical standard base64");
}

void testMalformedEmbeddedDataAndSampleNamesAreRejectedAtomically()
{
    auto source = bsl::ProjectState::makeDefault();
    source.kit.pads[0].customSampleName = "kick.flac";
    source.kit.pads[0].customSampleBase64 = "RkxBQw==";
    source.kit.pads[0].customSampleSha256 =
        "95f9e7a66bfb10d3c609f2c88ef3566ae3239ab4e025d0e661f7779f62c02ada";
    const auto validJson = bsl::statecodec::encodePresetJson(source);
    check(validJson.isNotEmpty(), "valid canonical embedded sample encodes");

    const auto rejectMutation = [&validJson](const char* property, const juce::var& value,
                                              std::string_view message) {
        auto root = juce::JSON::parse(validJson);
        auto* pad = root.getProperty("pads", {}).getArray()->getReference(0).getDynamicObject();
        pad->setProperty(property, value);
        auto destination = bsl::ProjectState::makeDefault();
        destination.patterns.front().name = "Keep Me";
        check(!bsl::statecodec::decodePresetJson(juce::JSON::toString(root), destination), message);
        check(destination.patterns.front().name == "Keep Me",
              "malformed embedded data rejection is atomic");
    };
    rejectMutation("customSampleName", "../kick.flac", "path-bearing sample name is rejected");
    rejectMutation("customSampleBase64", "RkxBQw", "non-canonical base64 is rejected");
    rejectMutation("customSampleSha256",
                   "05f9e7a66bfb10d3c609f2c88ef3566ae3239ab4e025d0e661f7779f62c02ada",
                   "embedded sample SHA-256 mismatch is rejected");
    rejectMutation("gain", 99.0, "out-of-range pad value is rejected");
    rejectMutation("delay", 1.01, "out-of-range pad delay is rejected");

    auto priorVersionTwo = juce::JSON::parse(validJson);
    priorVersionTwo.getProperty("pads", {}).getArray()->getReference(0).getDynamicObject()
        ->removeProperty("delay");
    auto priorDestination = bsl::ProjectState::makeDefault();
    priorDestination.kit.pads[0].delay = 0.75f;
    check(bsl::statecodec::decodePresetJson(
              juce::JSON::toString(priorVersionTwo), priorDestination),
          "prior schema-v2 preset without pad delay remains compatible");
    check(priorDestination.kit.pads[0].delay == 0.0f,
          "missing schema-v2 pad delay defaults to disabled");

    auto badFactory = juce::JSON::parse(validJson);
    juce::Array<juce::var> identifiers;
    identifiers.add("../factory.flac");
    identifiers.add("");
    badFactory.getProperty("pads", {}).getArray()->getReference(0).getDynamicObject()
        ->setProperty("factorySampleIds", identifiers);
    auto destination = bsl::ProjectState::makeDefault();
    check(!bsl::statecodec::decodePresetJson(juce::JSON::toString(badFactory), destination),
          "path-bearing factory sample identifier is rejected");

    auto badMaster = juce::JSON::parse(validJson);
    badMaster.getProperty("master", {}).getDynamicObject()->setProperty("delay", -0.1);
    check(!bsl::statecodec::decodePresetJson(juce::JSON::toString(badMaster), destination),
          "out-of-range master value is rejected");

    auto oversizedLegacy = juce::JSON::parse(makeVersionOnePreset());
    auto* legacyPad = oversizedLegacy.getProperty("pads", {}).getArray()->getReference(0)
                          .getDynamicObject();
    legacyPad->setProperty("customSampleName", "oversized.flac");
    legacyPad->setProperty("customSampleSha256", juce::String::repeatedString("0", 64));
    legacyPad->setProperty("customSampleBase64", "134217729.");
    destination.patterns.front().name = "Keep Me";
    check(!bsl::statecodec::decodePresetJson(juce::JSON::toString(oversizedLegacy), destination),
          "oversized v1 JUCE base64 declaration is rejected before decoding");
    check(destination.patterns.front().name == "Keep Me",
          "oversized v1 JUCE base64 rejection is atomic");
}

void testOutOfRangeMidiNotesAreRejectedBeforeEncoding()
{
    auto state = bsl::ProjectState::makeDefault();
    state.kit.pads[0].midiNote = static_cast<std::uint8_t>(128);
    check(!state.isValid(), "MIDI note above 127 invalidates the project");
    check(bsl::statecodec::encodePresetJson(state).isEmpty() && bsl::statecodec::encode(state).isEmpty(),
          "MIDI note above 127 is rejected by both encoders");

    state = bsl::ProjectState::makeDefault();
    state.kit.pads[0].rootNote = static_cast<std::uint8_t>(255);
    check(!state.isValid(), "root note above 127 invalidates the project");
    check(bsl::statecodec::encodePresetJson(state).isEmpty() && bsl::statecodec::encode(state).isEmpty(),
          "root note above 127 is rejected by both encoders");
}

void testNativePresetIsUtf8JsonAndRejectsInvalidInputAtomically()
{
    auto source = bsl::ProjectState::makeDefault();
    source.patterns.front().name = "Native JSON Preset";
    source.kit.pads[0].layerMix = 0.37f;
    const auto json = bsl::statecodec::encodePresetJson(source);
    check(json.trimStart().startsWithChar('{'), "native preset is plain UTF-8 JSON");

    auto restored = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::decodePresetJson(json, restored), "native JSON preset decodes");
    check(restored.patterns.front().name == "Native JSON Preset",
          "native JSON preset restores project state");
    check(std::abs(restored.kit.pads[0].layerMix - 0.37f) < 1.0e-6f,
          "native JSON preset restores layer mix");

    restored.patterns.front().name = "Keep Me";
    check(!bsl::statecodec::decodePresetJson("{\"schemaVersion\":1}", restored),
          "incomplete native preset is rejected");
    check(restored.patterns.front().name == "Keep Me",
          "failed native preset load leaves state untouched");
}

void testLegacyRandomizerAdapterUsesInstalledSchema()
{
    const juce::String json = R"json({
      "Randomize": {
        "noteNumbers": true, "noteOnOff": true, "onlyFillEmpty": true,
        "possibleNotes": [{"value":1},{"value":4}],
        "possibleSlices": [{"value":3},{"value":7}],
        "slice": true, "spacing": 0.44, "triggerChance": 0.38,
        "velocity": true, "velocityRange": {"start":0.2,"end":0.7}
      }
    })json";
    bsl::RandomizerSettings settings;
    check(juce::JSON::parse(json).isObject(), "legacy JSON parses before schema conversion");
    check(bsl::statecodec::importLegacyRandomizerJson(json, settings),
          "legacy randomizer JSON imports");
    check(settings.selectedPads == std::vector<std::uint8_t>({ 1, 4 }),
          "legacy pad selection imports");
    check(settings.selectedSubdivisions == std::vector<std::size_t>({ 3, 7 }),
          "legacy slice selection imports");
    check(settings.fillEmptyOnly && settings.isValid(), "legacy randomizer is validated");
}

void testLegacyDeclaredLoopAndStandaloneTempo()
{
    const auto makePreset = [](int beats) {
        juce::String sequence;
        for (int index = 0; index < beats; ++index)
        {
            if (index != 0) sequence += ",";
            sequence += "{\"Steps\":[{\"enabled\":true,\"Event\":{\"noteNum\":"
                     + juce::String(36 + index) + ",\"velocity\":0.6}}]}";
        }
        return juce::String(R"json({"Seq":{"Seq":{"Transport":{"BPM":{"value":"137"}}},
            "Patterns":{"Patterns":[{"Events":{"LoopTime":{"Bars":[{"num":4,"denum":4}]},
            "Sequences":[{"Sequences":[)json") + sequence + "]}]}}]}}}";
    };
    auto state = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::importLegacyPresetJson(makePreset(4), state), "synthetic legacy loop imports");
    check(std::abs(state.global.beatsPerMinute - 137.0) < 1e-6,
          "legacy standalone tempo is imported from the saved transport");
    const bool imported = bsl::statecodec::importLegacyPresetJson(makePreset(9), state);
    check(imported, "legacy preset accepts retained beats beyond its declared loop");
    if (imported)
    {
        const auto& pattern = state.patterns[0];
        check(pattern.bars.size() == 1 && pattern.lanes[0].beats.size() == 4,
              "legacy surplus does not extend the declared musical loop");
        for (std::size_t beat = 0; beat < 4; ++beat)
            check(pattern.lanes[0].beats[beat].slices[0].enabled
                  && pattern.lanes[0].beats[beat].slices[0].padIndex == beat,
                  "legacy import preserves every in-loop note while excluding enabled tail notes");
    }
    const auto before = bsl::statecodec::encodePresetJson(state);
    check(!bsl::statecodec::importLegacyPresetJson(makePreset(3), state)
          && bsl::statecodec::encodePresetJson(state) == before,
          "a short legacy lane still fails without altering the destination");
}

void testLegacyPresetAdapterPublishesOnlyValidatedProject()
{
    const juce::String json = R"json({
      "Params":{"Swing":-21.5},
      "Sampler":{"Pads":{"Pads":[
        {"name":"Kick","Layers":[{"Sample":{"Binary":{"name":"kick.flac"}}}],
         "Params":{"LayerMix":0.25,"OneShot":"On","Pan":0.5,"Length":0.8,
                   "Volume":"0.00 dB","Hipass":"120 Hz",
                   "ADSR":{"Attack":"35%"},"Note":"C4",
                   "Pitch":{"Root":"E3","Tune":"-2","Fine":"7"},
                   "Compressor":0.3,"BitCrush":0.1,
                   "Filter":{"Enable":"Off","Cutoff":"6.0 kHz","Reso":"1.5"},
                   "Drive":{"Enable":"On","Gain":"9.5 dB"},
                   "Aux":{"Aux1":"40%","Aux2":"20%","Aux3":"55%","Aux4":"50%"}}},
        {"name":"Quiet","Params":{"Volume":"-6.0206 dB",
                                     "Drive":{"Enable":"On","Gain":"6 dB"}}}
      ]},"Params":{"Master":{"Gain":"-6.0206 dB"},
                    "MasterComp":0.4,"MasterCrush":0.2,"Delay":"10%",
                    "DistDrive":"20%","Reverb":"30%","Room":"40%",
                    "Filter":{"Enable":"Off","Cutoff":"10 kHz","Reso":"1.5"}}},
      "Seq":{"Patterns":{"Patterns":[{
        "name":"Imported 3/8","Events":{
          "LoopTime":{"Bars":[{"num":3,"denum":8}]},
          "Sequences":[{"name":"Lane 1","mute":false,"solo":false,"Sequences":[
            {"Steps":[{"enabled":true,"Event":{"noteNum":36,"velocity":0.9}}]},
            {"Steps":[{"enabled":false,"Event":{"noteNum":37,"velocity":0.4}},
                       {"enabled":true,"Event":{"noteNum":48,"velocity":0.6}}]},
            {"Steps":[{"enabled":true,"Event":{"noteNum":39,"velocity":0.7}}]}
          ]}]
        }}]}}
      }
    })json";
    auto state = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::importLegacyPresetJson(json, state), "legacy preset JSON imports");
    check(state.isValid(), "legacy preset is validated before publication");
    check(state.patterns.front().name == "Imported 3/8", "legacy pattern name imports");
    check(state.patterns.front().bars.front().numerator == 3
              && state.patterns.front().bars.front().denominator == 8,
          "legacy variable meter imports");
    check(state.patterns.front().lanes.front().beats[1].slices.size() == 2,
          "legacy beat slicing imports");
    check(state.patterns.front().lanes.front().beats[1].slices[1].padIndex == 12,
          "legacy MIDI note 48 resolves to pad index 12");
    check(state.kit.pads.front().factorySampleIds.front() == "kick.flac",
          "legacy factory sample identity imports");
    check(std::abs(state.kit.pads.front().pitchSemitones + 2.0f) < 1.0e-6f,
          "legacy sound processing imports");
    check(!state.kit.pads.front().filterEnabled && state.kit.pads.front().driveEnabled
              && state.kit.pads.front().midiNote == 60
              && state.kit.pads.front().rootNote == 52
              && std::abs(state.kit.pads.front().attackNormalized - 0.35f) < 1.0e-6f
              && std::abs(state.kit.pads.front().driveGainDb - 9.5f) < 1.0e-6f
              && std::abs(state.kit.pads.front().distortionMix - 0.55f) < 1.0e-6f
              && std::abs(state.kit.pads.front().delay - 0.5f) < 1.0e-6f
              && std::abs(state.kit.pads.front().gain - 1.0f) < 1.0e-6f
              && state.kit.pads.front().highPass > 0.06f
              && state.kit.pads.front().highPass < 0.08f
              && state.kit.pads.front().cutoff > 0.67f
              && state.kit.pads.front().cutoff < 0.69f,
          "legacy pad Params retain nested controls, note names, and frequency units");
    check(!state.master.filterEnabled && std::abs(state.master.delay - 0.1f) < 1.0e-6f
              && state.master.distortion == 0.0f && state.master.reverb == 0.0f
              && state.master.room == 0.0f
              && std::abs(state.master.gain - std::pow(10.0f, -6.0206f / 20.0f)) < 1.0e-6f
              && state.master.cutoff > 0.80f && state.master.cutoff < 0.82f,
          "legacy master Params retain send-bus delay and master filter/gain, without auxiliary inserts");
    check(std::abs(state.kit.pads[0].reverb - 0.12f) < 1e-6f
              && std::abs(state.kit.pads[0].room - 0.08f) < 1e-6f
              && std::abs(state.kit.pads[0].distortionDrive - 0.2f) < 1e-6f,
          "legacy auxiliary return levels and drive configure the pad effects");
    check(std::abs(state.kit.pads[1].gain - 0.5f) < 1.0e-5f,
          "legacy negative-dB pad volume converts to linear gain");
    check(state.kit.pads[1].driveEnabled
              && state.kit.pads[1].independentDistortion && state.kit.pads[1].distortionMix == 0.0f,
          "legacy Drive remains enabled independently of a missing Aux3 send");

    const auto renderImportedPad = [&state](bool driveEnabled) {
        bsl::AudioEngine engine;
        engine.prepare(48000.0);
        auto settings = bsl::makePadPlaybackSettings(state.kit.pads.front());
        settings.driveEnabled = driveEnabled;
        settings.highPass = 0.0f;
        settings.attackSeconds = 0.0f;
        settings.attackNormalized = 0.0f;
        engine.setPadSettings(0, settings);
        engine.setPadSample(0, { 48000.0, { { 0.7f } } });
        engine.noteOn(36, 1.0f);
        bsl::AudioBlock output(34, std::vector<float>(1));
        engine.process(output, 1);
        return output[0][0];
    };
    check(std::abs(renderImportedPad(true) - renderImportedPad(false)) > 0.02f,
          "sanitized legacy Drive gain and Aux3 mix produce audible rendered distortion");
}

void testLegacyAuxSettingsDoNotProcessUnsentMasterAudio()
{
    const juce::String json = R"json({"Pads":{"Pads":[{"Params":{
      "Volume":"0 dB","Drive":{"Enable":"Off"},
      "Aux":{"Aux1":"0%","Aux2":"0%","Aux3":"0%","Aux4":"0%"}}}]},
      "Params":{"Master":"0 dB","DistDrive":"78%","Reverb":"100%","Room":"100%"}})json";
    auto state = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::importLegacyKitJson(json, state.kit, state.master), "aux regression kit imports");
    bsl::AudioEngine engine;
    engine.prepare(48000.0);
    engine.setPadSample(0, { 48000.0, { { 0.1f } } });
    engine.setPadSettings(0, bsl::makePadPlaybackSettings(state.kit.pads[0]));
    engine.setMasterSettings(bsl::makeMasterPlaybackSettings(state.master, state.master.gain));
    engine.noteOn(36, 1.0f);
    bsl::AudioBlock output(34, std::vector<float>(24000));
    engine.process(output, 24000);
    check(std::abs(output[0][0] - 0.1f * std::sqrt(0.5f)) < 1e-7f,
          "unsent drum is not distorted by global auxiliary drive configuration");
    float tailPeak = 0.0f;
    for (std::size_t i = 1; i < output[0].size(); ++i) tailPeak = std::max(tailPeak, std::abs(output[0][i]));
    check(tailPeak == 0.0f, "zero auxiliary sends cannot create a master ambience tail");

    auto settings = bsl::makePadPlaybackSettings(state.kit.pads[0]);
    settings.distortionMix = 0.5f;
    engine.reset();
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    engine.process(output, 1);
    check(output[0][0] > 0.1f, "Aux3 distortion works even with the separate Drive insert off");

    settings.distortionMix = 0.0f;
    settings.driveEnabled = true;
    settings.driveGainDb = 12.0f;
    engine.reset();
    engine.setPadSettings(0, settings);
    engine.noteOn(36, 1.0f);
    engine.process(output, 1);
    check(output[0][0] > 0.1f, "Drive insert works independently of a zero Aux3 send");
}

void testLegacyKitAdapterImportsLayersAndProcessing()
{
    const juce::String json = R"json({
      "Pads":{"Pads":[
        {"name":"Layered Kick","Layers":[
           {"Sample":{"Binary":{"name":"kick-a.flac"}}},
           {"Sample":{"Binary":{"name":"kick-b.flac"}}}],
         "Params":{"LayerMix":0.75,"OneShot":"Off","Pan":0.25,"Length":0.6,
                   "Pitch":{"Tune":"3","Fine":"-12"},"Compressor":0.4,"BitCrush":0.2,
                   "Aux":{"Aux1":"30%","Aux2":"10%"}}}
      ]},
      "Params":{"Master":"-3.41 dB","MasterComp":0.55,"MasterCrush":0.15,
                "Filter":{"Reso":"2.0"}}
    })json";
    bsl::KitState kit;
    bsl::MasterState master;
    check(bsl::statecodec::importLegacyKitJson(json, kit, master), "legacy kit JSON imports");
    check(kit.pads[0].factorySampleIds[0] == "kick-a.flac"
              && kit.pads[0].factorySampleIds[1] == "kick-b.flac",
          "legacy kit imports both sample layers");
    check(std::abs(kit.pads[0].layerMix - 0.75f) < 1.0e-6f,
          "legacy kit imports layer mix");
    check(!kit.pads[0].oneShot && std::abs(kit.pads[0].damp - 0.6f) < 1.0e-6f,
          "legacy kit imports gated length behavior");
    check(std::abs(master.compression - 0.55f) < 1.0e-6f,
          "legacy kit imports master processing");
    const float expectedMasterGain = std::pow(10.0f, -3.41f / 20.0f);
    check(std::abs(master.gain - expectedMasterGain) < 1.0e-6f,
          "scalar legacy Params.Master dB value converts exactly to linear gain");
}

void testNativeKitAndRandomizerJsonRoundTrip()
{
    auto state = bsl::ProjectState::makeDefault();
    state.kit.pads[1].factorySampleIds = { "snare-a.flac", "snare-b.flac" };
    state.kit.pads[1].layerMix = 0.42f;
    state.master.crush = 0.33f;
    const auto kitJson = bsl::statecodec::encodeKitJson(state.kit, state.master);
    bsl::KitState restoredKit;
    bsl::MasterState restoredMaster;
    check(bsl::statecodec::decodeKitJson(kitJson, restoredKit, restoredMaster),
          "native kit JSON decodes");
    check(restoredKit.pads[1].factorySampleIds[1] == "snare-b.flac"
              && std::abs(restoredKit.pads[1].layerMix - 0.42f) < 1.0e-6f,
          "native kit preserves both layers and mix");
    check(std::abs(restoredMaster.crush - 0.33f) < 1.0e-6f,
          "native kit preserves master settings");

    state.randomizer.selectedPads = { 2, 8, 15 };
    state.randomizer.selectedSubdivisions = { 5, 13, 45 };
    const auto randomJson = bsl::statecodec::encodeRandomizerJson(state.randomizer);
    bsl::RandomizerSettings restoredRandom;
    check(bsl::statecodec::decodeRandomizerJson(randomJson, restoredRandom),
          "native randomizer JSON decodes");
    check(restoredRandom.selectedPads == std::vector<std::uint8_t>({ 2, 8, 15 })
              && restoredRandom.selectedSubdivisions == std::vector<std::size_t>({ 5, 13, 45 }),
          "native randomizer preserves pad and subdivision selections");
}


void testRandomizerIndicesRejectNarrowingAndNonIntegers()
{
    for (const juce::var value : { juce::var(-1), juce::var(256), juce::var(juce::int64(1) << 40),
                                  juce::var(1.5), juce::var("2") })
    {
        bsl::RandomizerSettings original;
        original.selectedPads = { 7 };
        auto candidate = original;
        auto native = juce::JSON::parse(bsl::statecodec::encodeRandomizerJson(original));
        native.getDynamicObject()->getProperty("randomizer").getDynamicObject()
            ->setProperty("selectedPads", juce::Array<juce::var> { value });
        check(!bsl::statecodec::decodeRandomizerJson(juce::JSON::toString(native), candidate),
              "native randomizer rejects invalid pad index before narrowing");
        check(candidate.selectedPads == original.selectedPads, "native index rejection preserves destination");
        auto legacy = juce::JSON::parse(R"({"Randomize":{"possibleNotes":[{"value":1}]}})");
        legacy.getDynamicObject()->getProperty("Randomize").getDynamicObject()
            ->getProperty("possibleNotes").getArray()->getReference(0).getDynamicObject()->setProperty("value", value);
        check(!bsl::statecodec::importLegacyRandomizerJson(juce::JSON::toString(legacy), candidate),
              "legacy randomizer rejects invalid pad index before narrowing");
        check(candidate.selectedPads == original.selectedPads, "legacy index rejection preserves destination");
    }
}

void testNativeBarMetersRejectInvalidRawValuesAtomically()
{
    struct Case { const char* field; juce::var value; int beatCount; };
    const std::array cases {
        Case { "numerator", 0, 1 }, Case { "numerator", 65, 64 },
        Case { "denominator", 3, 4 }, Case { "denominator", 0, 4 },
        Case { "numerator", juce::var((juce::int64(1) << 32) + 4), 4 },
        Case { "denominator", juce::var((juce::int64(1) << 32) + 4), 4 },
        Case { "numerator", 4.5, 4 }, Case { "denominator", "4", 4 }
    };
    auto destination = bsl::ProjectState::makeDefault();
    destination.patterns[0].name = "Preserve on invalid meter";
    const auto before = bsl::statecodec::encodePresetJson(destination);
    for (const auto& item : cases)
    {
        auto source = bsl::ProjectState::makeDefault();
        source.patterns[0].bars = {{item.beatCount, 4}};
        source.patterns[0].lanes = {bsl::LaneState::make(static_cast<std::size_t>(item.beatCount))};
        auto json = juce::JSON::parse(bsl::statecodec::encodePresetJson(source));
        auto& bar = json.getDynamicObject()->getProperty("patterns").getArray()->getReference(0)
            .getDynamicObject()->getProperty("bars").getArray()->getReference(0);
        bar.getDynamicObject()->setProperty(item.field, item.value);
        check(!bsl::statecodec::decodePresetJson(juce::JSON::toString(json), destination),
              "native preset rejects raw invalid meter before coercion or normalization");
        check(bsl::statecodec::encodePresetJson(destination) == before,
              "invalid bar meter rejection preserves the complete destination");
        destination = bsl::ProjectState::makeDefault();
        destination.patterns[0].name = "Preserve on invalid meter";
    }
}

void testSliceVelocityAssignmentRoundTripAndMigration()
{
    auto source = bsl::ProjectState::makeDefault();
    source.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(1, 0, 0.3f);
    source.patterns[0].lanes[0].beats[0].slices[0].enabled = false;
    auto restored = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::decodePresetJson(bsl::statecodec::encodePresetJson(source), restored),
          "note velocity assignment state decodes");
    check(restored.patterns[0].lanes[0].beats == source.patterns[0].lanes[0].beats,
          "round trip distinguishes a disabled remembered note from untouched cells");
    auto json = juce::JSON::parse(bsl::statecodec::encodePresetJson(source));
    auto& oldSlice = json.getDynamicObject()->getProperty("patterns").getArray()->getReference(0)
        .getDynamicObject()->getProperty("lanes").getArray()->getReference(0)
        .getDynamicObject()->getProperty("beats").getArray()->getReference(0)
        .getArray()->getReference(0);
    oldSlice.getDynamicObject()->removeProperty("velocityAssigned");
    check(bsl::statecodec::decodePresetJson(juce::JSON::toString(json), restored),
          "older native projects without velocity assignment metadata still decode");
    const auto& note = restored.patterns[0].lanes[0].beats[0].slices[0];
    check(note.velocityAssigned && !note.enabled && note.velocity == 0.3f,
          "older disabled notes retain their stored velocity");
}

void testCompressedStateExpansionIsBounded()
{
    auto state = bsl::ProjectState::makeDefault();
    state.patterns[0].name = "Preserve on oversized import";
    const auto small = bsl::statecodec::encode(state);
    const auto json = bsl::statecodec::encodePresetJson(state);
    juce::MemoryOutputStream envelope;
    envelope.write(small.getData(), 8);
    {
        juce::GZIPCompressorOutputStream gzip(envelope, 1);
        gzip.write(json.toRawUTF8(), json.getNumBytesAsUTF8());
        const std::string padding(64 * 1024, ' ');
        for (int block = 0; block < 192 * 16; ++block) gzip.write(padding.data(), padding.size());
    }
    auto destination = state;
    check(!bsl::statecodec::decode(envelope.getData(), envelope.getDataSize(), destination),
          "compressed JSON exceeding 192 MiB is rejected before unlimited expansion");
    check(destination.patterns[0].name == state.patterns[0].name, "oversized state rejection is atomic");
}

} // namespace

int main()
{
    testEmbeddedSampleBudgetMatchesLoaderReplacementSemantics();
    testNativeStateRoundTripIncludesProjectAndEmbeddedSample();
    testDuplicateNativePatternTriggersAreNormalizedOnLoad();
    testCorruptStateIsRejectedAtomically();
    testExtendedPatternQuantizationCodecRoundTrips();
    testVersionOneMigrationDecouplesPadIdentityFromMidi();
    testMalformedEmbeddedDataAndSampleNamesAreRejectedAtomically();
    testOutOfRangeMidiNotesAreRejectedBeforeEncoding();
    testNativePresetIsUtf8JsonAndRejectsInvalidInputAtomically();
    testLegacyRandomizerAdapterUsesInstalledSchema();
    testLegacyDeclaredLoopAndStandaloneTempo();
    testLegacyPresetAdapterPublishesOnlyValidatedProject();
    testLegacyAuxSettingsDoNotProcessUnsentMasterAudio();
    testLegacyKitAdapterImportsLayersAndProcessing();
    testRandomizerIndicesRejectNarrowingAndNonIntegers();
    testNativeBarMetersRejectInvalidRawValuesAtomically();
    testSliceVelocityAssignmentRoundTripAndMigration();
    testCompressedStateExpansionIsBounded();
    testNativeKitAndRandomizerJsonRoundTrip();
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All state tests passed\n";
    return EXIT_SUCCESS;
}
