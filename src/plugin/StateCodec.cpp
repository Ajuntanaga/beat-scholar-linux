#include "plugin/StateCodec.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>

namespace bsl::statecodec {
namespace {

constexpr int stateVersion = 2;
constexpr int oldestSupportedStateVersion = 1;

juce::var object()
{
    return juce::var(new juce::DynamicObject());
}

void set(juce::var& value, const char* name, juce::var property)
{
    value.getDynamicObject()->setProperty(name, std::move(property));
}

juce::var get(const juce::var& value, const char* name)
{
    if (const auto* source = value.getDynamicObject())
        return source->getProperty(name);
    return {};
}

juce::var arrayFromStrings(const std::array<std::string, 2>& values)
{
    juce::Array<juce::var> result;
    for (const auto& value : values)
        result.add(juce::String(value));
    return result;
}

const char* quantizationName(PatternQuantization value)
{
    switch (value)
    {
        case PatternQuantization::off: return "off";
        case PatternQuantization::eighth: return "eighth";
        case PatternQuantization::quarter: return "quarter";
        case PatternQuantization::half: return "half";
        case PatternQuantization::whole: return "whole";
        case PatternQuantization::sixteenth: return "sixteenth";
        case PatternQuantization::thirtySecond: return "thirtySecond";
    }
    return "whole";
}

juce::var sliceToVar(const SliceState& slice)
{
    auto result = object();
    set(result, "padIndex", static_cast<int>(slice.padIndex));
    set(result, "velocity", slice.velocity);
    set(result, "enabled", slice.enabled);
    set(result, "velocityAssigned", slice.velocityAssigned);
    return result;
}

juce::var beatToVar(const BeatState& beat)
{
    juce::Array<juce::var> slices;
    for (const auto& slice : beat.slices)
        slices.add(sliceToVar(slice));
    return slices;
}

juce::var laneToVar(const LaneState& lane)
{
    auto result = object();
    set(result, "name", juce::String(lane.name));
    set(result, "muted", lane.muted);
    set(result, "solo", lane.solo);
    juce::Array<juce::var> beats;
    for (const auto& beat : lane.beats)
        beats.add(beatToVar(beat));
    set(result, "beats", beats);
    return result;
}

juce::var patternToVar(const PatternState& pattern)
{
    auto result = object();
    set(result, "name", juce::String(pattern.name));
    set(result, "triggerNote", static_cast<int>(pattern.triggerNote));
    set(result, "quantization", quantizationName(pattern.quantization));
    juce::Array<juce::var> bars;
    for (const auto& bar : pattern.bars)
    {
        auto encoded = object();
        set(encoded, "numerator", bar.numerator);
        set(encoded, "denominator", bar.denominator);
        bars.add(encoded);
    }
    set(result, "bars", bars);
    juce::Array<juce::var> lanes;
    for (const auto& lane : pattern.lanes)
        lanes.add(laneToVar(lane));
    set(result, "lanes", lanes);
    return result;
}

juce::var padToVar(const PadState& pad)
{
    auto result = object();
    set(result, "name", juce::String(pad.name));
    set(result, "factorySampleIds", arrayFromStrings(pad.factorySampleIds));
    set(result, "customSampleName", juce::String(pad.customSampleName));
    set(result, "customSampleSha256", juce::String(pad.customSampleSha256));
    set(result, "customSampleBase64", juce::String(pad.customSampleBase64.value()));
    set(result, "midiNote", static_cast<int>(pad.midiNote));
    set(result, "rootNote", static_cast<int>(pad.rootNote));
    set(result, "layerMix", pad.layerMix);
    set(result, "gain", pad.gain);
    set(result, "pan", pad.pan);
    set(result, "attackSeconds", pad.attackSeconds);
    set(result, "attackNormalized", pad.attackNormalized);
    set(result, "damp", pad.damp);
    set(result, "pitchSemitones", pad.pitchSemitones);
    set(result, "fineCents", pad.fineCents);
    set(result, "highPass", pad.highPass);
    set(result, "filterEnabled", pad.filterEnabled);
    set(result, "cutoff", pad.cutoff);
    set(result, "resonance", pad.resonance);
    set(result, "driveEnabled", pad.driveEnabled);
    set(result, "driveGainDb", pad.driveGainDb);
    set(result, "distortionMix", pad.distortionMix);
    set(result, "independentDistortion", pad.independentDistortion);
    set(result, "distortionDrive", pad.distortionDrive);
    set(result, "compression", pad.compression);
    set(result, "crush", pad.crush);
    set(result, "reverb", pad.reverb);
    set(result, "room", pad.room);
    set(result, "delay", pad.delay);
    set(result, "oneShot", pad.oneShot);
    set(result, "muted", pad.muted);
    set(result, "solo", pad.solo);
    set(result, "lpfBypass", pad.lpfBypass);
    set(result, "hpfBypass", pad.hpfBypass);
    set(result, "driveBypass", pad.driveBypass);
    set(result, "crushBypass", pad.crushBypass);
    return result;
}

juce::var randomizerToVar(const RandomizerSettings& settings)
{
    auto result = object();
    juce::Array<juce::var> pads;
    for (const auto value : settings.selectedPads)
        pads.add(static_cast<int>(value));
    set(result, "selectedPads", pads);
    juce::Array<juce::var> subdivisions;
    for (const auto value : settings.selectedSubdivisions)
        subdivisions.add(static_cast<juce::int64>(value));
    set(result, "selectedSubdivisions", subdivisions);
    set(result, "noteSelectionEnabled", settings.noteSelectionEnabled);
    set(result, "slicingEnabled", settings.slicingEnabled);
    set(result, "triggeringEnabled", settings.triggeringEnabled);
    set(result, "velocityEnabled", settings.velocityEnabled);
    set(result, "fillEmptyOnly", settings.fillEmptyOnly);
    set(result, "triggerChance", settings.triggerChance);
    set(result, "spacing", settings.spacing);
    set(result, "minimumVelocity", settings.minimumVelocity);
    set(result, "maximumVelocity", settings.maximumVelocity);
    return result;
}

juce::var projectToVar(const ProjectState& state)
{
    auto root = object();
    set(root, "schemaVersion", state.schemaVersion);
    set(root, "activePattern", static_cast<juce::int64>(state.activePattern));

    auto global = object();
    set(global, "swingPercent", state.global.swingPercent);
    set(global, "swingReference", static_cast<int>(state.global.swingReference));
    set(global, "beatsPerMinute", state.global.beatsPerMinute);
    set(global, "defaultVelocity", state.global.defaultVelocity);
    set(global, "sequencerEnabled", state.global.sequencerEnabled);
    set(global, "samplerEnabled", state.global.samplerEnabled);
    set(global, "auditionDuringPlayback", state.global.auditionDuringPlayback);
    set(global, "kitLocked", state.global.kitLocked);
    set(global, "multiOutput", state.global.multiOutput);
    set(root, "global", global);

    auto master = object();
    set(master, "filterEnabled", state.master.filterEnabled);
    set(master, "cutoff", state.master.cutoff);
    set(master, "resonance", state.master.resonance);
    set(master, "delay", state.master.delay);
    set(master, "distortion", state.master.distortion);
    set(master, "reverb", state.master.reverb);
    set(master, "room", state.master.room);
    set(master, "crush", state.master.crush);
    set(master, "compression", state.master.compression);
    set(master, "gain", state.master.gain);
    set(root, "master", master);
    set(root, "randomizer", randomizerToVar(state.randomizer));

    auto ui = object();
    set(ui, "selectedPad", static_cast<juce::int64>(state.ui.selectedPad));
    set(ui, "selectedLane", static_cast<juce::int64>(state.ui.selectedLane));
    set(ui, "selectedBar", static_cast<juce::int64>(state.ui.selectedBar));
    set(ui, "patternMode", state.ui.patternMode);
    set(ui, "randomizerVisible", state.ui.randomizerVisible);
    // Omit defaults so older native presets retain their canonical encoding.
    if (state.ui.darkMode) set(ui, "darkMode", true);
    if (!state.ui.midiNoteNames) set(ui, "midiNoteNames", false);
    if (state.ui.midiFlats) set(ui, "midiFlats", true);
    if (state.ui.middleCOctave != 3) set(ui, "middleCOctave", state.ui.middleCOctave);
    if (!state.ui.presetFile.empty()) set(ui, "presetFile", juce::String(state.ui.presetFile));
    if (!state.ui.kitFile.empty()) set(ui, "kitFile", juce::String(state.ui.kitFile));
    if (!state.ui.randomizerFile.empty()) set(ui, "randomizerFile", juce::String(state.ui.randomizerFile));
    set(root, "ui", ui);

    juce::Array<juce::var> pads;
    for (const auto& pad : state.kit.pads)
        pads.add(padToVar(pad));
    set(root, "pads", pads);

    juce::Array<juce::var> patterns;
    for (const auto& pattern : state.patterns)
        patterns.add(patternToVar(pattern));
    set(root, "patterns", patterns);
    return root;
}

bool readBool(const juce::var& parent, const char* name, bool fallback)
{
    const auto value = get(parent, name);
    return value.isVoid() ? fallback : static_cast<bool>(value);
}

int readInt(const juce::var& parent, const char* name, int fallback)
{
    const auto value = get(parent, name);
    return value.isVoid() ? fallback : static_cast<int>(value);
}

float readFloat(const juce::var& parent, const char* name, float fallback)
{
    const auto value = get(parent, name);
    return value.isVoid() ? fallback : static_cast<float>(value);
}

double readDouble(const juce::var& parent, const char* name, double fallback)
{
    const auto value = get(parent, name);
    return value.isVoid() ? fallback : static_cast<double>(value);
}

std::string readString(const juce::var& parent, const char* name, const std::string& fallback = {})
{
    const auto value = get(parent, name);
    return value.isVoid() ? fallback : value.toString().toStdString();
}

std::uint8_t legacyNoteToPadIndex(int note, const KitState& kit)
{
    const auto matching = std::find_if(kit.pads.begin(), kit.pads.end(), [note](const PadState& pad) {
        return static_cast<int>(pad.midiNote) == note;
    });
    if (matching != kit.pads.end())
        return static_cast<std::uint8_t>(std::distance(kit.pads.begin(), matching));
    return note >= 36 && note < 36 + static_cast<int>(padCount)
               ? static_cast<std::uint8_t>(note - 36) : std::uint8_t { 0 };
}

bool readSlice(const juce::var& encoded, SliceState& slice, int version,
               const KitState& kit)
{
    if (!encoded.isObject())
        return false;
    const int identity = readInt(encoded, version == 1 ? "note" : "padIndex",
                                 version == 1 ? 36 : 0);
    if ((version == 1 && (identity < 0 || identity > 127))
        || (version == 2 && (identity < 0 || identity >= static_cast<int>(padCount))))
        return false;
    slice.padIndex = version == 1 ? legacyNoteToPadIndex(identity, kit)
                                  : static_cast<std::uint8_t>(identity);
    slice.velocity = readFloat(encoded, "velocity", 0.8f);
    slice.enabled = readBool(encoded, "enabled", false);
    // Older projects already stored a velocity for every note, including disabled ones.
    slice.velocityAssigned = readBool(encoded, "velocityAssigned", true);
    return true;
}

bool readBeat(const juce::var& encoded, BeatState& beat, int version,
              const KitState& kit)
{
    const auto* slices = encoded.getArray();
    if (slices == nullptr || slices->isEmpty()
        || slices->size() > static_cast<int>(maximumSubdivision))
        return false;
    beat.slices.clear();
    for (const auto& value : *slices)
    {
        SliceState slice;
        if (!readSlice(value, slice, version, kit))
            return false;
        beat.slices.push_back(slice);
    }
    return true;
}

bool readIndex(const juce::var& value, juce::int64 minimum, juce::int64 maximum,
               juce::int64& result)
{
    if (!value.isInt() && !value.isInt64()) return false;
    result = static_cast<juce::int64>(value);
    return result >= minimum && result <= maximum;
}

bool readPattern(const juce::var& encoded, PatternState& pattern, int version,
                 const KitState& kit)
{
    if (!encoded.isObject())
        return false;
    pattern.name = readString(encoded, "name", "Pattern");
    const int trigger = readInt(encoded, "triggerNote", 12);
    if (trigger < 0 || trigger > 127)
        return false;
    pattern.triggerNote = static_cast<std::uint8_t>(trigger);
    int quantization = static_cast<int>(PatternQuantization::whole);
    if (version != 1)
    {
        const auto value = get(encoded, "quantization");
        if (value.isInt() || value.isInt64())
            quantization = static_cast<int>(value);
        else
        {
            const auto text = value.toString();
            if (text == "off") quantization = static_cast<int>(PatternQuantization::off);
            else if (text == "eighth") quantization = static_cast<int>(PatternQuantization::eighth);
            else if (text == "quarter") quantization = static_cast<int>(PatternQuantization::quarter);
            else if (text == "half") quantization = static_cast<int>(PatternQuantization::half);
            else if (text == "whole") quantization = static_cast<int>(PatternQuantization::whole);
            else if (text == "sixteenth") quantization = static_cast<int>(PatternQuantization::sixteenth);
            else if (text == "thirtySecond") quantization = static_cast<int>(PatternQuantization::thirtySecond);
            else return false;
        }
    }
    if (quantization < static_cast<int>(PatternQuantization::off)
        || quantization > static_cast<int>(PatternQuantization::thirtySecond))
        return false;
    pattern.quantization = static_cast<PatternQuantization>(quantization);
    const auto* bars = get(encoded, "bars").getArray();
    const auto* lanes = get(encoded, "lanes").getArray();
    if (bars == nullptr || bars->isEmpty() || lanes == nullptr || lanes->isEmpty()
        || lanes->size() > static_cast<int>(maximumLaneCount))
        return false;
    pattern.bars.clear();
    for (const auto& value : *bars)
    {
        if (!value.isObject()) return false;
        const auto numerator = get(value, "numerator");
        const auto denominator = get(value, "denominator");
        juce::int64 n = 4, d = 4;
        if ((!numerator.isVoid() && !readIndex(numerator, 1, 64, n))
            || (!denominator.isVoid() && !readIndex(denominator, 2, 16, d)))
            return false;
        const BarState bar { static_cast<int>(n), static_cast<int>(d) };
        if (!bar.isValid()) return false;
        pattern.bars.push_back(bar);
    }
    pattern.lanes.clear();
    for (const auto& value : *lanes)
    {
        if (!value.isObject())
            return false;
        LaneState lane;
        lane.name = readString(value, "name", "Lane");
        lane.muted = readBool(value, "muted", false);
        lane.solo = readBool(value, "solo", false);
        const auto* beats = get(value, "beats").getArray();
        if (beats == nullptr)
            return false;
        for (const auto& encodedBeat : *beats)
        {
            BeatState beat;
            if (!readBeat(encodedBeat, beat, version, kit))
                return false;
            lane.beats.push_back(std::move(beat));
        }
        pattern.lanes.push_back(std::move(lane));
    }
    return true;
}

bool readPad(const juce::var& encoded, PadState& pad, int version)
{
    if (!encoded.isObject())
        return false;
    pad.name = readString(encoded, "name", pad.name);
    if (const auto* ids = get(encoded, "factorySampleIds").getArray())
        for (int index = 0; index < std::min(2, ids->size()); ++index)
            pad.factorySampleIds[static_cast<std::size_t>(index)] = ids->getReference(index).toString().toStdString();
    pad.customSampleName = readString(encoded, "customSampleName");
    pad.customSampleSha256 = readString(encoded, "customSampleSha256");
    pad.customSampleBase64 = readString(encoded, "customSampleBase64");
    const int midiNote = readInt(encoded, version == 1 ? "note" : "midiNote", pad.midiNote);
    const int rootNote = readInt(encoded, "rootNote", pad.rootNote);
    if (midiNote < 0 || midiNote > 127 || rootNote < 0 || rootNote > 127)
        return false;
    pad.midiNote = static_cast<std::uint8_t>(midiNote);
    pad.rootNote = static_cast<std::uint8_t>(rootNote);
    pad.layerMix = readFloat(encoded, "layerMix", pad.layerMix);
    pad.gain = readFloat(encoded, "gain", pad.gain);
    pad.pan = readFloat(encoded, "pan", pad.pan);
    pad.attackSeconds = readFloat(encoded, "attackSeconds", pad.attackSeconds);
    pad.attackNormalized = readFloat(encoded, "attackNormalized", pad.attackNormalized);
    pad.damp = readFloat(encoded, "damp", pad.damp);
    pad.pitchSemitones = readFloat(encoded, "pitchSemitones", pad.pitchSemitones);
    pad.fineCents = readFloat(encoded, "fineCents", pad.fineCents);
    pad.highPass = readFloat(encoded, "highPass", pad.highPass);
    pad.filterEnabled = readBool(encoded, "filterEnabled", pad.filterEnabled);
    pad.cutoff = readFloat(encoded, "cutoff", pad.cutoff);
    pad.resonance = readFloat(encoded, "resonance", pad.resonance);
    if (version == 1)
    {
        const float legacyDrive = readFloat(encoded, "drive", 0.0f);
        pad.driveEnabled = legacyDrive > 0.0f;
        pad.distortionMix = legacyDrive;
    }
    else
    {
        pad.driveEnabled = readBool(encoded, "driveEnabled", pad.driveEnabled);
        pad.driveGainDb = readFloat(encoded, "driveGainDb", pad.driveGainDb);
        pad.distortionMix = readFloat(encoded, "distortionMix", pad.distortionMix);
        pad.independentDistortion = readBool(encoded, "independentDistortion", false);
        pad.distortionDrive = readFloat(encoded, "distortionDrive", 0.5f);
    }
    pad.compression = readFloat(encoded, "compression", pad.compression);
    pad.crush = readFloat(encoded, "crush", pad.crush);
    pad.reverb = readFloat(encoded, "reverb", pad.reverb);
    pad.room = readFloat(encoded, "room", pad.room);
    pad.delay = readFloat(encoded, "delay", pad.delay);
    pad.oneShot = readBool(encoded, "oneShot", pad.oneShot);
    pad.muted = readBool(encoded, "muted", pad.muted);
    pad.solo = readBool(encoded, "solo", pad.solo);
    pad.lpfBypass = readBool(encoded, "lpfBypass", false);
    pad.hpfBypass = readBool(encoded, "hpfBypass", false);
    pad.driveBypass = readBool(encoded, "driveBypass", false);
    pad.crushBypass = readBool(encoded, "crushBypass", false);
    return true;
}

bool readRandomizer(const juce::var& encoded, RandomizerSettings& settings)
{
    if (!encoded.isObject())
        return false;
    settings.selectedPads.clear();
    if (const auto* pads = get(encoded, "selectedPads").getArray())
        for (const auto& value : *pads)
        {
            juce::int64 index;
            if (!readIndex(value, 0, padCount - 1, index)) return false;
            settings.selectedPads.push_back(static_cast<std::uint8_t>(index));
        }
    settings.selectedSubdivisions.clear();
    if (const auto* values = get(encoded, "selectedSubdivisions").getArray())
        for (const auto& value : *values)
        {
            juce::int64 index;
            if (!readIndex(value, 1, maximumSubdivision, index)) return false;
            settings.selectedSubdivisions.push_back(static_cast<std::size_t>(index));
        }
    settings.noteSelectionEnabled = readBool(encoded, "noteSelectionEnabled", settings.noteSelectionEnabled);
    settings.slicingEnabled = readBool(encoded, "slicingEnabled", settings.slicingEnabled);
    settings.triggeringEnabled = readBool(encoded, "triggeringEnabled", settings.triggeringEnabled);
    settings.velocityEnabled = readBool(encoded, "velocityEnabled", settings.velocityEnabled);
    settings.fillEmptyOnly = readBool(encoded, "fillEmptyOnly", settings.fillEmptyOnly);
    settings.triggerChance = readFloat(encoded, "triggerChance", settings.triggerChance);
    settings.spacing = readFloat(encoded, "spacing", settings.spacing);
    settings.minimumVelocity = readFloat(encoded, "minimumVelocity", settings.minimumVelocity);
    settings.maximumVelocity = readFloat(encoded, "maximumVelocity", settings.maximumVelocity);
    return settings.isValid();
}

bool validateAndNormalizeEmbedded(KitState& kit, int version);
void readMaster(const juce::var& encoded, MasterState& master, int version);

bool projectFromVar(const juce::var& root, ProjectState& state)
{
    const int version = readInt(root, "schemaVersion", 0);
    if (!root.isObject() || version < oldestSupportedStateVersion || version > stateVersion)
        return false;
    ProjectState candidate = ProjectState::makeDefault();
    candidate.schemaVersion = stateVersion;
    candidate.activePattern = static_cast<std::size_t>(static_cast<juce::int64>(get(root, "activePattern")));
    const auto global = get(root, "global");
    candidate.global.swingPercent = readDouble(global, "swingPercent", 0.0);
    const int swingReference = readInt(global, "swingReference", 0);
    if (swingReference < 0 || swingReference > 2)
        return false;
    candidate.global.swingReference = static_cast<SwingReference>(swingReference);
    candidate.global.beatsPerMinute = readDouble(global, "beatsPerMinute", 120.0);
    candidate.global.defaultVelocity = readFloat(global, "defaultVelocity", 0.8f);
    candidate.global.sequencerEnabled = readBool(global, "sequencerEnabled", true);
    candidate.global.samplerEnabled = readBool(global, "samplerEnabled", true);
    candidate.global.auditionDuringPlayback = readBool(global, "auditionDuringPlayback", true);
    candidate.global.kitLocked = readBool(global, "kitLocked", false);
    candidate.global.multiOutput = readBool(global, "multiOutput", false);

    readMaster(get(root, "master"), candidate.master, version);
    if (!readRandomizer(get(root, "randomizer"), candidate.randomizer))
        return false;

    const auto ui = get(root, "ui");
    candidate.ui.selectedPad = static_cast<std::size_t>(static_cast<juce::int64>(get(ui, "selectedPad")));
    candidate.ui.selectedLane = static_cast<std::size_t>(static_cast<juce::int64>(get(ui, "selectedLane")));
    candidate.ui.selectedBar = static_cast<std::size_t>(static_cast<juce::int64>(get(ui, "selectedBar")));
    candidate.ui.patternMode = readBool(ui, "patternMode", false);
    candidate.ui.randomizerVisible = readBool(ui, "randomizerVisible", false);
    candidate.ui.darkMode = readBool(ui, "darkMode", false);
    candidate.ui.midiNoteNames = readBool(ui, "midiNoteNames", true);
    candidate.ui.midiFlats = readBool(ui, "midiFlats", false);
    candidate.ui.middleCOctave = static_cast<int>(ui.getProperty("middleCOctave", 3));
    candidate.ui.presetFile = readString(ui, "presetFile");
    candidate.ui.kitFile = readString(ui, "kitFile");
    candidate.ui.randomizerFile = readString(ui, "randomizerFile");

    const auto* pads = get(root, "pads").getArray();
    if (pads == nullptr || pads->size() != static_cast<int>(padCount))
        return false;
    for (std::size_t index = 0; index < padCount; ++index)
        if (!readPad(pads->getReference(static_cast<int>(index)), candidate.kit.pads[index], version))
            return false;
    if (!validateAndNormalizeEmbedded(candidate.kit, version))
        return false;

    const auto* patterns = get(root, "patterns").getArray();
    if (patterns == nullptr || patterns->isEmpty())
        return false;
    candidate.patterns.clear();
    for (const auto& encoded : *patterns)
    {
        PatternState pattern;
        if (!readPattern(encoded, pattern, version, candidate.kit))
            return false;
        candidate.patterns.push_back(std::move(pattern));
    }
    if (!candidate.normalizePatternTriggerNotes() || !candidate.isValid())
        return false;
    state = std::move(candidate);
    return true;
}

float legacyNumber(const juce::var& value, float fallback)
{
    if (value.isDouble() || value.isInt() || value.isInt64())
        return static_cast<float>(value);
    const auto text = value.toString().trim();
    return text.isEmpty() ? fallback : text.getFloatValue();
}

float percentOrUnit(const juce::var& value, float fallback)
{
    const auto text = value.toString();
    const float number = legacyNumber(value, fallback);
    return std::clamp(text.containsChar('%') ? number / 100.0f : number, 0.0f, 1.0f);
}

float legacyLinearGain(const juce::var& value, float fallback)
{
    if (value.isVoid())
        return fallback;
    const auto text = value.toString().trim();
    const float number = legacyNumber(value, fallback);
    const float gain = text.containsIgnoreCase("dB") ? std::pow(10.0f, number / 20.0f)
                                                     : number;
    return std::clamp(gain, 0.0f, 4.0f);
}

float legacyCutoff(const juce::var& value, float fallback)
{
    if (value.isVoid())
        return fallback;

    const auto text = value.toString().trim();
    const float number = legacyNumber(value, fallback);
    if (text.containsChar('%'))
        return std::clamp(number / 100.0f, 0.0f, 1.0f);

    const bool hasFrequencyUnit = text.containsIgnoreCase("Hz");
    if (!hasFrequencyUnit && number >= 0.0f && number <= 1.0f)
        return number;

    float frequency = number;
    if (text.containsIgnoreCase("kHz"))
        frequency *= 1000.0f;
    frequency = std::clamp(frequency, 20.0f, 20000.0f);
    const float logarithmicPosition = std::log(frequency / 20.0f) / std::log(1000.0f);
    return std::clamp(logarithmicPosition * logarithmicPosition, 0.0f, 1.0f);
}

int legacyMidiNote(const juce::var& value, int fallback)
{
    if (value.isInt() || value.isInt64() || value.isDouble())
        return std::clamp(static_cast<int>(value), 0, 127);

    auto text = value.toString().trim();
    if (text.isEmpty())
        return fallback;
    if (text.containsOnly("+-0123456789"))
        return std::clamp(text.getIntValue(), 0, 127);

    const auto letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    int semitone = 0;
    switch (letter)
    {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default: return fallback;
    }

    int octaveStart = 1;
    if (text.length() > 1 && (text[1] == '#' || text[1] == 'b' || text[1] == 'B'))
    {
        semitone += text[1] == '#' ? 1 : -1;
        octaveStart = 2;
    }
    const auto octaveText = text.substring(octaveStart).trim();
    if (octaveText.isEmpty() || !octaveText.containsOnly("+-0123456789"))
        return fallback;
    const int note = (octaveText.getIntValue() + 1) * 12 + semitone;
    return note >= 0 && note <= 127 ? note : fallback;
}

bool legacyEnabled(const juce::var& value, bool fallback)
{
    if (value.isBool())
        return static_cast<bool>(value);
    if (value.isInt() || value.isInt64() || value.isDouble())
        return std::abs(static_cast<double>(value)) > std::numeric_limits<double>::epsilon();
    const auto text = value.toString().trim();
    if (text.equalsIgnoreCase("On") || text.equalsIgnoreCase("true") || text == "1")
        return true;
    if (text.equalsIgnoreCase("Off") || text.equalsIgnoreCase("false") || text == "0")
        return false;
    return fallback;
}

float legacyControlAmount(const juce::var& parent, const char* name, float fallback)
{
    const auto value = get(parent, name);
    if (!value.isObject())
        return percentOrUnit(value, fallback);
    for (const auto* property : { "Amount", "Mix", "Gain", "Value" })
    {
        const auto nested = get(value, property);
        if (!nested.isVoid())
            return percentOrUnit(nested, fallback);
    }
    return fallback;
}

float legacyHighPass(const juce::var& params, float fallback)
{
    auto value = get(params, "Hipass");
    if (value.isObject())
    {
        for (const auto* property : { "Cutoff", "Amount", "Value" })
        {
            const auto nested = get(value, property);
            if (!nested.isVoid())
            {
                value = nested;
                break;
            }
        }
    }
    return value.isVoid() ? fallback : legacyCutoff(value, fallback);
}

void readLegacyPad(const juce::var& sourcePad, PadState& pad)
{
    pad.name = readString(sourcePad, "name", pad.name);
    if (const auto* layers = get(sourcePad, "Layers").getArray())
        for (int layer = 0; layer < std::min(2, layers->size()); ++layer)
            pad.factorySampleIds[static_cast<std::size_t>(layer)] =
                readString(get(get(layers->getReference(layer), "Sample"), "Binary"), "name");

    const auto params = get(sourcePad, "Params");
    pad.layerMix = percentOrUnit(get(params, "LayerMix"), pad.layerMix);
    pad.oneShot = legacyEnabled(get(params, "OneShot"), pad.oneShot);
    pad.pan = std::clamp(readFloat(params, "Pan", 0.5f) * 2.0f - 1.0f, -1.0f, 1.0f);
    pad.damp = percentOrUnit(get(params, "Length"), 0.0f);
    pad.attackNormalized = percentOrUnit(get(get(params, "ADSR"), "Attack"), 0.0f);
    pad.attackSeconds = pad.attackNormalized;
    pad.gain = legacyLinearGain(get(params, "Volume"), pad.gain);
    const auto pitch = get(params, "Pitch");
    const auto configuredNote = get(params, "Note").isVoid() ? get(sourcePad, "Note")
                                                               : get(params, "Note");
    pad.midiNote = static_cast<std::uint8_t>(legacyMidiNote(configuredNote, pad.midiNote));
    pad.rootNote = static_cast<std::uint8_t>(legacyMidiNote(get(pitch, "Root"), pad.rootNote));
    pad.pitchSemitones = legacyNumber(get(pitch, "Tune"), 0.0f);
    pad.fineCents = legacyNumber(get(pitch, "Fine"), 0.0f);
    pad.highPass = legacyHighPass(params, 0.0f);
    const auto filter = get(params, "Filter");
    pad.filterEnabled = legacyEnabled(get(filter, "Enable"), true);
    pad.cutoff = legacyCutoff(get(filter, "Cutoff"), pad.cutoff);
    pad.resonance = std::clamp(legacyNumber(get(filter, "Reso"), 0.0f) / 10.0f, 0.0f, 1.0f);
    const auto drive = get(params, "Drive");
    pad.driveEnabled = legacyEnabled(get(drive, "Enable"), false);
    pad.driveGainDb = std::clamp(legacyNumber(get(drive, "Gain"), 0.0f), -60.0f, 60.0f);
    pad.compression = percentOrUnit(get(params, "Compressor"), 0.0f);
    pad.crush = percentOrUnit(get(params, "BitCrush"), 0.0f);
    const auto aux = get(params, "Aux");
    pad.reverb = percentOrUnit(get(aux, "Aux1"), 0.0f);
    pad.room = percentOrUnit(get(aux, "Aux2"), 0.0f);
    pad.delay = percentOrUnit(get(aux, "Aux4"), 0.0f);
    const auto distortionSend = get(aux, "Aux3");
    pad.independentDistortion = true;
    pad.distortionMix = percentOrUnit(distortionSend, 0.0f);
}

void readLegacyAuxiliaryConfiguration(const juce::var& params, KitState& kit)
{
    const float reverbReturn = legacyControlAmount(params, "Reverb", 1.0f);
    const float roomReturn = legacyControlAmount(params, "Room", 1.0f);
    const float distortionDrive = legacyControlAmount(params, "DistDrive", 0.5f);
    for (auto& pad : kit.pads)
    {
        pad.reverb *= reverbReturn;
        pad.room *= roomReturn;
        pad.distortionDrive = distortionDrive;
    }
}

void readLegacyMaster(const juce::var& params, MasterState& master)
{
    const auto masterParams = get(params, "Master");
    master.compression = percentOrUnit(get(params, "MasterComp"), 0.0f);
    master.crush = percentOrUnit(get(params, "MasterCrush"), 0.0f);
    master.delay = legacyControlAmount(params, "Delay", 0.0f);
    // These legacy values configure the pad auxiliaries, not master inserts.
    // Delay already consumes the explicit pad-send bus in AudioEngine.
    master.distortion = master.reverb = master.room = 0.0f;
    if (masterParams.isObject())
    {
        const auto masterVolume = get(masterParams, "Volume");
        master.gain = legacyLinearGain(masterVolume.isVoid() ? get(masterParams, "Gain")
                                                             : masterVolume,
                                       master.gain);
    }
    else
    {
        master.gain = legacyLinearGain(masterParams, master.gain);
    }
    const auto filter = get(params, "Filter");
    master.filterEnabled = legacyEnabled(get(filter, "Enable"), true);
    master.cutoff = legacyCutoff(get(filter, "Cutoff"), master.cutoff);
    master.resonance = std::clamp(legacyNumber(get(filter, "Reso"), 0.0f) / 10.0f,
                                  0.0f, 1.0f);
}

juce::var parseJson(const juce::String& json)
{
    if (json.getNumBytesAsUTF8() > maximumJsonBytes) return {};
    return juce::JSON::parse(json);
}

bool decodeCanonicalBase64(const std::string& encoded, juce::MemoryBlock& decoded)
{
    if (encoded.empty() || encoded.size() > ((maximumEmbeddedSampleBytes + 2u) / 3u) * 4u)
        return false;
    juce::MemoryOutputStream output(decoded, false);
    if (!juce::Base64::convertFromBase64(output, juce::String(encoded)))
        return false;
    output.flush();
    return juce::Base64::toBase64(decoded.getData(), decoded.getSize()).toStdString() == encoded;
}

bool decodeLegacyJuceBase64(const std::string& encoded, std::size_t remainingBytes,
                            juce::MemoryBlock& decoded)
{
    const auto separator = encoded.find('.');
    if (separator == std::string::npos || separator == 0)
        return false;
    if (separator > 1 && encoded.front() == '0')
        return false;

    std::size_t declaredBytes = 0;
    for (std::size_t index = 0; index < separator; ++index)
    {
        const auto character = static_cast<unsigned char>(encoded[index]);
        if (!std::isdigit(character))
            return false;
        const auto digit = static_cast<std::size_t>(character - static_cast<unsigned char>('0'));
        if (declaredBytes > remainingBytes / 10u
            || (declaredBytes == remainingBytes / 10u && digit > remainingBytes % 10u))
            return false;
        declaredBytes = declaredBytes * 10u + digit;
        if (declaredBytes > remainingBytes)
            return false;
    }

    if (declaredBytes > (std::numeric_limits<std::size_t>::max() - 5u) / 8u)
        return false;
    const std::size_t expectedCharacters = (declaredBytes * 8u + 5u) / 6u;
    if (encoded.size() - separator - 1u != expectedCharacters)
        return false;
    constexpr std::string_view alphabet =
        ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";
    if (!std::all_of(encoded.begin() + static_cast<std::ptrdiff_t>(separator + 1u),
                     encoded.end(), [&](char character) {
                         return alphabet.find(character) != std::string_view::npos;
                     }))
        return false;

    if (!decoded.fromBase64Encoding(juce::String(encoded))
        || decoded.getSize() != declaredBytes
        || decoded.toBase64Encoding().toStdString() != encoded)
        return false;
    return true;
}

bool validateAndNormalizeEmbedded(KitState& kit, int version)
{
    std::size_t totalBytes = 0;
    for (auto& pad : kit.pads)
    {
        const bool hasAny = !pad.customSampleName.empty() || !pad.customSampleSha256.empty()
                            || !pad.customSampleBase64.empty();
        if (!hasAny)
            continue;
        if (pad.customSampleName.empty() || pad.customSampleSha256.empty()
            || pad.customSampleBase64.empty())
            return false;

        juce::MemoryBlock decoded;
        if (!decodeCanonicalBase64(pad.customSampleBase64.value(), decoded))
        {
            if (version != 1
                || !decodeLegacyJuceBase64(pad.customSampleBase64.value(),
                                           maximumEmbeddedSampleBytes - totalBytes, decoded))
                return false;
            pad.customSampleBase64 = juce::Base64::toBase64(decoded.getData(), decoded.getSize())
                                         .toStdString();
        }
        if (!fitsEmbeddedSampleBudget(totalBytes, decoded.getSize()))
            return false;
        totalBytes += decoded.getSize();
        if (juce::SHA256(decoded).toHexString().toStdString() != pad.customSampleSha256)
            return false;
    }
    return true;
}

bool validateContent(const ProjectState& state)
{
    if (!state.isValid())
        return false;
    auto kit = state.kit;
    return validateAndNormalizeEmbedded(kit, stateVersion);
}

void readMaster(const juce::var& encoded, MasterState& master, int version)
{
    master.filterEnabled = readBool(encoded, "filterEnabled", master.filterEnabled);
    master.cutoff = readFloat(encoded, "cutoff", master.cutoff);
    master.resonance = readFloat(encoded, "resonance", master.resonance);
    master.delay = readFloat(encoded, "delay", master.delay);
    master.distortion = readFloat(encoded, "distortion", master.distortion);
    master.reverb = readFloat(encoded, "reverb", master.reverb);
    master.room = readFloat(encoded, "room", master.room);
    master.crush = readFloat(encoded, "crush", master.crush);
    master.compression = readFloat(encoded, "compression", master.compression);
    master.gain = readFloat(encoded, "gain", master.gain);
    if (version == 1)
        master.filterEnabled = true;
}

void writeMaster(juce::var& encoded, const MasterState& master)
{
    set(encoded, "filterEnabled", master.filterEnabled);
    set(encoded, "cutoff", master.cutoff);
    set(encoded, "resonance", master.resonance);
    set(encoded, "delay", master.delay);
    set(encoded, "distortion", master.distortion);
    set(encoded, "reverb", master.reverb);
    set(encoded, "room", master.room);
    set(encoded, "crush", master.crush);
    set(encoded, "compression", master.compression);
    set(encoded, "gain", master.gain);
}

} // namespace

bool canReplaceEmbeddedSample(const KitState& kit, std::size_t replacementPad,
                              std::size_t incomingBytes) noexcept
{
    if (replacementPad >= kit.pads.size()) return false;
    std::size_t retained = 0;
    for (std::size_t pad = 0; pad < kit.pads.size(); ++pad)
    {
        if (pad == replacementPad) continue;
        const auto& encoded = kit.pads[pad].customSampleBase64.value();
        if (encoded.empty()) continue;
        // Active states use canonical Base64; full decoding and SHA validation
        // remain the codec's responsibility. Count bytes without another copy.
        if (encoded.size() % 4 != 0
            || encoded.size() > ((maximumEmbeddedSampleBytes + 2u) / 3u) * 4u)
            return false;
        const auto padding = static_cast<std::size_t>(encoded.back() == '=')
                           + static_cast<std::size_t>(encoded[encoded.size() - 2] == '=');
        const auto bytes = encoded.size() / 4u * 3u - padding;
        if (!fitsEmbeddedSampleBudget(retained, bytes)) return false;
        retained += bytes;
    }
    return fitsEmbeddedSampleBudget(retained, incomingBytes);
}

juce::MemoryBlock encode(const ProjectState& state)
{
    if (!validateContent(state))
        return {};
    const auto json = encodePresetJson(state);
    if (json.isEmpty()) return {};
    juce::MemoryOutputStream output;
    output.write("BSLX", 4);
    output.writeInt(stateVersion);
    {
        juce::GZIPCompressorOutputStream gzip(&output, 9, false);
        gzip.write(json.toRawUTF8(), static_cast<std::size_t>(json.getNumBytesAsUTF8()));
    }
    return output.getMemoryBlock();
}

juce::String encodePresetJson(const ProjectState& state)
{
    if (!validateContent(state)) return {};
    auto json = juce::JSON::toString(projectToVar(state), true);
    return json.getNumBytesAsUTF8() <= maximumJsonBytes ? json : juce::String {};
}

bool decodePresetJson(const juce::String& json, ProjectState& destination)
{
    if (json.isEmpty())
        return false;
    auto candidate = destination;
    if (!projectFromVar(parseJson(json), candidate))
        return false;
    destination = std::move(candidate);
    return true;
}

juce::String encodeKitJson(const KitState& kit, const MasterState& master)
{
    auto validation = ProjectState::makeDefault();
    validation.kit = kit;
    validation.master = master;
    if (!validateContent(validation))
        return {};
    auto root = object();
    set(root, "schemaVersion", stateVersion);
    juce::Array<juce::var> pads;
    for (const auto& pad : kit.pads)
        pads.add(padToVar(pad));
    set(root, "pads", pads);
    auto encodedMaster = object();
    writeMaster(encodedMaster, master);
    set(root, "master", encodedMaster);
    return juce::JSON::toString(root, true);
}

bool decodeKitJson(const juce::String& json, KitState& kit, MasterState& master)
{
    const auto root = parseJson(json);
    const auto* pads = get(root, "pads").getArray();
    const int version = readInt(root, "schemaVersion", 0);
    if (!root.isObject() || version < oldestSupportedStateVersion || version > stateVersion
        || pads == nullptr || pads->size() != static_cast<int>(padCount))
        return false;
    auto candidateKit = ProjectState::makeDefault().kit;
    for (std::size_t index = 0; index < padCount; ++index)
        if (!readPad(pads->getReference(static_cast<int>(index)), candidateKit.pads[index], version))
            return false;
    if (!validateAndNormalizeEmbedded(candidateKit, version))
        return false;
    auto candidateMaster = MasterState {};
    readMaster(get(root, "master"), candidateMaster, version);
    auto validation = ProjectState::makeDefault();
    validation.kit = candidateKit;
    validation.master = candidateMaster;
    if (!validation.isValid())
        return false;
    kit = std::move(candidateKit);
    master = candidateMaster;
    return true;
}

juce::String encodeRandomizerJson(const RandomizerSettings& settings)
{
    if (!settings.isValid())
        return {};
    auto root = object();
    set(root, "schemaVersion", stateVersion);
    set(root, "randomizer", randomizerToVar(settings));
    return juce::JSON::toString(root, true);
}

bool decodeRandomizerJson(const juce::String& json, RandomizerSettings& destination)
{
    const auto root = parseJson(json);
    const int version = readInt(root, "schemaVersion", 0);
    if (!root.isObject() || version < oldestSupportedStateVersion || version > stateVersion)
        return false;
    auto candidate = destination;
    if (!readRandomizer(get(root, "randomizer"), candidate))
        return false;
    destination = std::move(candidate);
    return true;
}

bool decode(const void* data, std::size_t size, ProjectState& destination)
{
    if (data == nullptr || size < 12 || size > maximumSerializedBytes)
        return false;
    juce::MemoryInputStream input(data, size, false);
    char magic[4] {};
    if (input.read(magic, 4) != 4 || std::memcmp(magic, "BSLX", 4) != 0)
        return false;
    const int envelopeVersion = input.readInt();
    if (envelopeVersion < oldestSupportedStateVersion || envelopeVersion > stateVersion)
        return false;
    juce::GZIPDecompressorInputStream gzip(&input, false);
    juce::MemoryOutputStream expanded;
    std::array<char, 65536> chunk;
    for (;;)
    {
        const int count = gzip.read(chunk.data(), static_cast<int>(chunk.size()));
        if (count <= 0) break;
        if (static_cast<std::size_t>(count) > maximumJsonBytes - expanded.getDataSize())
            return false;
        expanded.write(chunk.data(), static_cast<std::size_t>(count));
    }
    const auto json = juce::String::fromUTF8(static_cast<const char*>(expanded.getData()),
                                          static_cast<int>(expanded.getDataSize()));
    if (json.isEmpty())
        return false;
    const auto parsed = parseJson(json);
    if (readInt(parsed, "schemaVersion", 0) != envelopeVersion)
        return false;
    auto candidate = destination;
    return projectFromVar(parsed, candidate) ? (destination = std::move(candidate), true) : false;
}

bool importLegacyRandomizerJson(const juce::String& json, RandomizerSettings& destination)
{
    const auto root = parseJson(json);
    const auto source = get(root, "Randomize");
    if (!source.isObject())
        return false;
    RandomizerSettings candidate;
    candidate.noteSelectionEnabled = readBool(source, "noteNumbers", true);
    candidate.triggeringEnabled = readBool(source, "noteOnOff", true);
    candidate.fillEmptyOnly = readBool(source, "onlyFillEmpty", false);
    candidate.slicingEnabled = readBool(source, "slice", true);
    candidate.velocityEnabled = readBool(source, "velocity", true);
    candidate.spacing = readFloat(source, "spacing", 0.5f);
    candidate.triggerChance = readFloat(source, "triggerChance", 0.5f);
    if (const auto* values = get(source, "possibleNotes").getArray())
        for (const auto& value : *values)
        {
            juce::int64 index;
            if (!readIndex(get(value, "value"), 0, padCount - 1, index)) return false;
            candidate.selectedPads.push_back(static_cast<std::uint8_t>(index));
        }
    if (const auto* values = get(source, "possibleSlices").getArray())
        for (const auto& value : *values)
        {
            juce::int64 index;
            if (!readIndex(get(value, "value"), 1, maximumSubdivision, index)) return false;
            candidate.selectedSubdivisions.push_back(static_cast<std::size_t>(index));
        }
    const auto range = get(source, "velocityRange");
    candidate.minimumVelocity = readFloat(range, "start", 0.4f);
    candidate.maximumVelocity = readFloat(range, "end", 1.0f);
    if (!candidate.isValid())
        return false;
    destination = std::move(candidate);
    return true;
}

bool importLegacyPresetJson(const juce::String& json, ProjectState& destination)
{
    const auto root = parseJson(json);
    if (!root.isObject())
        return false;
    ProjectState candidate = ProjectState::makeDefault();
    candidate.global.swingPercent = legacyNumber(get(get(root, "Params"), "Swing"), 0.0f);
    const auto transport = get(get(get(root, "Seq"), "Seq"), "Transport");
    candidate.global.beatsPerMinute = legacyNumber(get(get(transport, "BPM"), "value"), 120.0f);

    const auto sequenceRoot = get(get(root, "Seq"), "Patterns");
    const auto* patterns = get(sequenceRoot, "Patterns").getArray();
    if (patterns == nullptr || patterns->isEmpty())
        return false;
    candidate.patterns.clear();
    for (int patternIndex = 0; patternIndex < patterns->size(); ++patternIndex)
    {
        const auto& sourcePattern = patterns->getReference(patternIndex);
        PatternState pattern;
        pattern.name = readString(sourcePattern, "name", "Pattern " + std::to_string(patternIndex + 1));
        pattern.triggerNote = static_cast<std::uint8_t>(std::min(127, 12 + patternIndex));
        const auto events = get(sourcePattern, "Events");
        const auto* bars = get(get(events, "LoopTime"), "Bars").getArray();
        const auto* lanes = get(events, "Sequences").getArray();
        if (bars == nullptr || bars->isEmpty() || lanes == nullptr || lanes->isEmpty())
            return false;
        for (const auto& sourceBar : *bars)
            pattern.bars.push_back(BarState::make(readInt(sourceBar, "num", 4),
                                                  readInt(sourceBar, "denum", 4)));
        std::size_t loopBeatCount = 0;
        for (const auto& bar : pattern.bars)
            loopBeatCount += static_cast<std::size_t>(bar.numerator);
        for (const auto& sourceLane : *lanes)
        {
            LaneState lane;
            lane.name = readString(sourceLane, "name", "Lane");
            lane.muted = readBool(sourceLane, "mute", false);
            lane.solo = readBool(sourceLane, "solo", false);
            const auto* beats = get(sourceLane, "Sequences").getArray();
            if (beats == nullptr || static_cast<std::size_t>(beats->size()) < loopBeatCount)
                return false;
            // Legacy files retain records after a shortened loop. Bars define
            // the active pattern extent; surplus records must not extend it.
            for (std::size_t beatIndex = 0; beatIndex < loopBeatCount; ++beatIndex)
            {
                const auto& sourceBeat = beats->getReference(static_cast<int>(beatIndex));
                const auto* steps = get(sourceBeat, "Steps").getArray();
                if (steps == nullptr || steps->isEmpty()
                    || steps->size() > static_cast<int>(maximumSubdivision))
                    return false;
                BeatState beat;
                for (const auto& step : *steps)
                {
                    const auto event = get(step, "Event");
                    SliceState slice;
                    slice.padIndex = static_cast<std::uint8_t>(
                        std::clamp(readInt(event, "noteNum", 36), 0, 127));
                    slice.velocity = readFloat(event, "velocity", 0.8f);
                    slice.enabled = readBool(step, "enabled", false);
                    slice.velocityAssigned = true;
                    beat.slices.push_back(slice);
                }
                lane.beats.push_back(std::move(beat));
            }
            pattern.lanes.push_back(std::move(lane));
        }
        candidate.patterns.push_back(std::move(pattern));
    }

    const auto sampler = get(root, "Sampler");
    if (const auto* pads = get(get(sampler, "Pads"), "Pads").getArray())
    {
        for (int index = 0; index < std::min(static_cast<int>(padCount), pads->size()); ++index)
        {
            const auto& sourcePad = pads->getReference(index);
            auto& pad = candidate.kit.pads[static_cast<std::size_t>(index)];
            readLegacyPad(sourcePad, pad);
        }
    }
    readLegacyMaster(get(sampler, "Params"), candidate.master);
    readLegacyAuxiliaryConfiguration(get(sampler, "Params"), candidate.kit);

    for (auto& pattern : candidate.patterns)
        for (auto& lane : pattern.lanes)
            for (auto& beat : lane.beats)
                for (auto& slice : beat.slices)
                    slice.padIndex = legacyNoteToPadIndex(slice.padIndex, candidate.kit);

    if (!candidate.normalizePatternTriggerNotes() || !candidate.isValid())
        return false;
    destination = std::move(candidate);
    return true;
}

bool importLegacyKitJson(const juce::String& json, KitState& kit, MasterState& masterState)
{
    const auto root = parseJson(json);
    const auto* pads = get(get(root, "Pads"), "Pads").getArray();
    if (!root.isObject() || pads == nullptr || pads->isEmpty())
        return false;
    auto candidateKit = ProjectState::makeDefault().kit;
    auto candidateMaster = MasterState {};
    for (int index = 0; index < std::min(static_cast<int>(padCount), pads->size()); ++index)
    {
        const auto& sourcePad = pads->getReference(index);
        auto& pad = candidateKit.pads[static_cast<std::size_t>(index)];
        readLegacyPad(sourcePad, pad);
    }
    readLegacyMaster(get(root, "Params"), candidateMaster);
    readLegacyAuxiliaryConfiguration(get(root, "Params"), candidateKit);
    auto validation = ProjectState::makeDefault();
    validation.kit = candidateKit;
    validation.master = candidateMaster;
    if (!validation.isValid())
        return false;
    kit = std::move(candidateKit);
    masterState = candidateMaster;
    return true;
}

} // namespace bsl::statecodec
