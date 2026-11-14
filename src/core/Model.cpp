#include "core/Model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace bsl {
namespace {

constexpr std::array<const char*, padCount> padNames {
    "Kick", "Snare", "Rim", "Clap", "Hi Hat - Closed", "Hi Hat - Open",
    "Crash 1", "Ride", "Crash 2", "Tom - Hi", "Tom - Mid", "Tom - Low",
    "Shaker", "Triangle", "FX 1", "FX 2"
};

bool isSupportedDenominator(int denominator)
{
    return denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16;
}

bool isValidSubdivision(std::size_t subdivision)
{
    return subdivision >= 1 && subdivision <= maximumSubdivision;
}

} // namespace

float padHighPassCutoffHz(float normalized) noexcept
{
    // Native Hipass FreqParameter: 1..3000 Hz, with 400 Hz at the centre.
    constexpr float skew = 0.343640147081788f;
    const float position = std::clamp(normalized, 0.0f, 1.0f);
    return 1.0f + 2999.0f * (position > 0.0f ? std::exp(std::log(position) / skew) : 0.0f);
}

float normalizedPadHighPassCutoff(float frequencyHz) noexcept
{
    constexpr float skew = 0.343640147081788f;
    const float position = (std::clamp(frequencyHz, 1.0f, 3000.0f) - 1.0f) / 2999.0f;
    return position > 0.0f ? std::exp(std::log(position) * skew) : 0.0f;
}

float filterCutoffHz(float normalized) noexcept
{
    // Native FreqParameter: 10 Hz..20 kHz, with 2 kHz at the centre.
    constexpr float skew = 0.30044121569325083f;
    const float position = std::clamp(normalized, 0.0f, 1.0f);
    return 10.0f + 19990.0f * (position > 0.0f ? std::exp(std::log(position) / skew) : 0.0f);
}

float normalizedFilterCutoff(float frequencyHz) noexcept
{
    constexpr float skew = 0.30044121569325083f;
    const float position = (std::clamp(frequencyHz, 10.0f, 20000.0f) - 10.0f) / 19990.0f;
    return position > 0.0f ? std::exp(std::log(position) * skew) : 0.0f;
}

BeatState BeatState::withSubdivision(std::size_t subdivision,
                                     std::uint8_t padIndex,
                                     float velocity)
{
    BeatState beat;
    if (!isValidSubdivision(subdivision))
        return beat;

    beat.slices.resize(subdivision);
    for (auto& slice : beat.slices)
    {
        slice.padIndex = padIndex;
        slice.velocity = std::clamp(velocity, 0.0f, 1.0f);
        slice.enabled = true;
        slice.velocityAssigned = true;
    }
    return beat;
}

void BeatState::setSubdivision(std::size_t subdivision)
{
    if (!isValidSubdivision(subdivision))
        return;

    const auto fallback = slices.empty() ? SliceState {} : slices.front();
    slices.resize(subdivision, fallback);
}

void BeatState::rotate(int amount)
{
    if (slices.empty())
        return;
    const auto size = static_cast<int>(slices.size());
    const auto normalized = ((amount % size) + size) % size;
    std::rotate(slices.rbegin(), slices.rbegin() + normalized, slices.rend());
}

void BeatState::mirror()
{
    std::reverse(slices.begin(), slices.end());
}

void BeatState::reset() noexcept
{
    for (auto& slice : slices)
        slice.enabled = false;
}

BarState BarState::make(int newNumerator, int newDenominator, std::size_t)
{
    BarState bar;
    bar.numerator = std::clamp(newNumerator, 1, 64);
    bar.denominator = isSupportedDenominator(newDenominator) ? newDenominator : 4;
    return bar;
}

bool BarState::isValid() const noexcept
{
    return numerator >= 1 && numerator <= 64 && isSupportedDenominator(denominator);
}

double BarState::durationInQuarterNotes() const
{
    return static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
}

LaneState LaneState::make(std::size_t beatCount)
{
    LaneState lane;
    lane.beats.resize(beatCount, BeatState::withSubdivision(1, 0, 0.8f));
    for (auto& beat : lane.beats)
    {
        beat.slices.front().enabled = false;
        beat.slices.front().velocityAssigned = false;
    }
    return lane;
}

double PatternState::durationInQuarterNotes() const
{
    double result = 0.0;
    for (const auto& bar : bars)
        result += bar.durationInQuarterNotes();
    return result;
}

RandomizerSettings RandomizerSettings::makeInitPreset()
{
    RandomizerSettings settings;
    settings.selectedSubdivisions = { 2, 4, 8 };
    settings.minimumVelocity = 0.0f;
    return settings;
}

bool RandomizerSettings::isValid() const noexcept
{
    std::array<bool, padCount> padsSeen {};
    for (const auto pad : selectedPads)
    {
        if (pad >= padCount || padsSeen[pad])
            return false;
        padsSeen[pad] = true;
    }
    std::array<bool, maximumSubdivision + 1> subdivisionsSeen {};
    for (const auto subdivision : selectedSubdivisions)
    {
        if (subdivision < 1 || subdivision > maximumSubdivision || subdivisionsSeen[subdivision])
            return false;
        subdivisionsSeen[subdivision] = true;
    }
    return std::isfinite(triggerChance) && triggerChance >= 0.0f && triggerChance <= 1.0f
           && std::isfinite(spacing) && spacing >= 0.0f && spacing <= 1.0f
           && std::isfinite(minimumVelocity) && std::isfinite(maximumVelocity)
           && minimumVelocity >= 0.0f && maximumVelocity <= 1.0f
           && minimumVelocity <= maximumVelocity;
}

ProjectState ProjectState::makeDefault()
{
    ProjectState state;

    for (std::size_t index = 0; index < state.kit.pads.size(); ++index)
    {
        state.kit.pads[index].name = padNames[index];
        state.kit.pads[index].midiNote = static_cast<std::uint8_t>(36 + index);
    }

    PatternState pattern;
    pattern.bars.push_back(BarState::make(4, 4));
    for (std::size_t laneIndex = 0; laneIndex < 4; ++laneIndex)
    {
        auto lane = LaneState::make(4);
        lane.name = state.kit.pads[laneIndex].name;
        for (auto& beat : lane.beats)
            beat.slices.front().padIndex = static_cast<std::uint8_t>(laneIndex);
        pattern.lanes.push_back(std::move(lane));
    }
    state.patterns.push_back(std::move(pattern));
    return state;
}

ProjectState ProjectState::makeMainInit()
{
    auto state = makeDefault();
    // Native sampler Init callbacks select these factory resources. Kick's
    // body explicitly overrides the category selection with DW Sub.
    const std::array<std::array<const char*, 2>, padCount> samples {{
        {{ "SRC-DW_INST-BD_TYPE-Body_NAME-DW Sub.flac", "SRC-707_INST-BD_TYPE-Hit_NAME-SvN Kick.flac" }},
        {{ "SRC-707_INST-Snare_TYPE-Body_NAME-SvN 1.flac", "SRC-707_INST-Snare_TYPE-Hit_NAME-SvN 2.flac" }},
        {{ "SRC-707_INST-Rim_NAME-SvN Rim.flac", "" }},
        {{ "SRC-707_INST-Clap_NAME-SvN Clap.flac", "" }},
        {{ "SRC-707_INST-Hat_TYPE-Closed_NAME-SvN Hat 1.flac", "" }},
        {{ "SRC-707_INST-Hat_TYPE-Open_NAME-SvNHat Open.flac", "" }},
        {{ "SRC-707_INST-Cymbal_TYPE-Crash_NAME-SvN Crash.flac", "" }},
        {{ "SRC-707_INST-Cymbal_TYPE-Ride_NAME-SvN Ride.flac", "" }},
        {{ "SRC-808_INST-Cymbal_TYPE-Crash_NAME-GR8 Crash.flac", "" }},
        {{ "SRC-707_INST-Tom_TYPE-Hi_NAME-SvN Tom Hi.flac", "" }},
        {{ "SRC-707_INST-Tom_TYPE-Mid_NAME-SvN Tom Mid.flac", "" }},
        {{ "SRC-707_INST-Tom_TYPE-Lo_NAME-SvN Tom Low.flac", "" }},
        {{ "SRC-707_INST-Shaker_NAME-SvN Tambo.flac", "" }},
        {{ "SRC-808_INST-Triangle_NAME-GR8 Beep.flac", "" }},
        {{ "SRC-DW_INST-FX_NAME-Finger Snap.flac", "" }},
        {{ "SRC-DW_INST-FX_NAME-Single Snap 1.flac", "" }},
    }};
    for (std::size_t pad = 0; pad < samples.size(); ++pad)
    {
        state.kit.pads[pad].layerMix = 0.5f;
        for (std::size_t layer = 0; layer < samples[pad].size(); ++layer)
            state.kit.pads[pad].factorySampleIds[layer] = samples[pad][layer];
    }
    auto& pattern = state.patterns.front();
    pattern.name = "A";
    pattern.triggerNote = 60; // JUCE's middle-C-3 display labels MIDI 60 as C3.
    pattern.lanes.resize(3);
    return state;
}

bool ProjectState::isValid() const
{
    if (schemaVersion != 2 || patterns.empty() || activePattern >= patterns.size()
        || !randomizer.isValid() || ui.selectedPad >= padCount
        || (ui.middleCOctave != 3 && ui.middleCOctave != 4))
        return false;

    if (!std::isfinite(global.swingPercent) || global.swingPercent < -200.0
        || global.swingPercent > 200.0 || !std::isfinite(global.beatsPerMinute)
        || global.beatsPerMinute <= 0.0 || !std::isfinite(global.defaultVelocity)
        || global.defaultVelocity < 0.0f || global.defaultVelocity > 1.0f)
        return false;

    if (global.quantization
        && (static_cast<int>(*global.quantization) <= static_cast<int>(PatternQuantization::off)
            || static_cast<int>(*global.quantization) > static_cast<int>(PatternQuantization::thirtySecond)))
        return false;

    const auto unit = [](float value) {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
    };
    if (!unit(master.cutoff)
        || !std::isfinite(master.resonance) || master.resonance < 0.0f || master.resonance > 1.0f
        || !unit(master.delay) || !unit(master.distortionReturn) || !unit(master.distortion) || !unit(master.reverb)
        || !unit(master.room)
        || !std::isfinite(master.crush) || master.crush < 0.0f || master.crush > 1.0f
        || !std::isfinite(master.compression) || master.compression < 0.0f
        || master.compression > 1.0f || !std::isfinite(master.gain) || master.gain < 0.0f
        || master.gain > 4.0f)
        return false;

    for (const auto& pad : kit.pads)
    {
        const auto basenameOnly = [](const std::string& value) {
            return value.empty()
                   || (value != "." && value != ".."
                       && value.find('/') == std::string::npos
                       && value.find('\\') == std::string::npos
                       && value.find('\0') == std::string::npos);
        };
        if (!basenameOnly(pad.factorySampleIds[0]) || !basenameOnly(pad.factorySampleIds[1])
            || pad.midiNote > 127 || pad.rootNote > 127
            || !unit(pad.layerMix) || !std::isfinite(pad.gain) || pad.gain < 0.0f
            || pad.gain > 4.0f || !std::isfinite(pad.pan) || pad.pan < -1.0f || pad.pan > 1.0f
            || !std::isfinite(pad.attackSeconds) || pad.attackSeconds < 0.0f
            || pad.attackSeconds > 10.0f || !unit(pad.attackNormalized) || !unit(pad.damp)
            || !std::isfinite(pad.releaseSeconds) || pad.releaseSeconds < 0.0f
            || pad.releaseSeconds > 10.0f
            || !std::isfinite(pad.pitchSemitones) || pad.pitchSemitones < -12.0f
            || pad.pitchSemitones > 12.0f || !std::isfinite(pad.fineCents)
            || pad.fineCents < -100.0f || pad.fineCents > 100.0f || !unit(pad.highPass)
            || !unit(pad.cutoff) || !unit(pad.resonance) || !std::isfinite(pad.driveGainDb)
            || pad.driveGainDb < -60.0f || pad.driveGainDb > 60.0f
            || !unit(pad.distortionMix) || !unit(pad.distortionDrive)
            || !unit(pad.compression) || !unit(pad.crush)
            || !unit(pad.reverb) || !unit(pad.room) || !unit(pad.delay) || !unit(pad.auxGain))
            return false;
        if (!pad.customSampleLayered && !pad.customSamples[1].empty())
            return false;
        for (const auto& sample : pad.customSamples)
        {
            if (!basenameOnly(sample.name) || !std::isfinite(sample.sampleRate)
                || sample.sampleRate < 0.0)
                return false;
            if (!sample.empty()
                && (sample.name.empty() || sample.sha256.size() != 64 || sample.base64.empty()))
                return false;
        }
    }

    std::array<bool, 128> triggerNotes {};
    for (const auto& pattern : patterns)
    {
        if (pattern.triggerNote > 127 || triggerNotes[pattern.triggerNote])
            return false;
        triggerNotes[pattern.triggerNote] = true;
    }
    return std::all_of(patterns.begin(), patterns.end(), [](const PatternState& pattern) {
        const auto quantization = static_cast<int>(pattern.quantization);
        if (pattern.bars.empty() || pattern.lanes.empty()
            || pattern.lanes.size() > maximumLaneCount || quantization < 0
            || quantization > static_cast<int>(PatternQuantization::thirtySecond))
            return false;

        const auto expectedBeatCount = std::accumulate(
            pattern.bars.begin(), pattern.bars.end(), std::size_t { 0 },
            [](std::size_t total, const BarState& bar) {
                return total + static_cast<std::size_t>(bar.numerator);
            });

        for (const auto& bar : pattern.bars)
            if (!bar.isValid())
                return false;

        for (const auto& lane : pattern.lanes)
        {
            if (lane.beats.size() != expectedBeatCount)
                return false;
            for (const auto& beat : lane.beats)
            {
                if (!isValidSubdivision(beat.slices.size()))
                    return false;
                for (const auto& slice : beat.slices)
                    if (slice.padIndex >= padCount || !std::isfinite(slice.velocity)
                        || slice.velocity < 0.0f || slice.velocity > 1.0f)
                        return false;
            }
        }
        return pattern.durationInQuarterNotes() > 0.0;
    });
}

std::optional<std::uint8_t> ProjectState::unusedPatternTriggerNote(int preferred) const noexcept
{
    std::array<bool, 128> used {};
    for (const auto& pattern : patterns)
        if (pattern.triggerNote <= 127)
            used[pattern.triggerNote] = true;
    for (int offset = 0; offset < 128; ++offset)
    {
        const auto note = static_cast<std::uint8_t>((std::clamp(preferred, 0, 127) + offset) % 128);
        if (!used[note]) return note;
    }
    return std::nullopt;
}

bool ProjectState::normalizePatternTriggerNotes() noexcept
{
    if (patterns.size() > 128)
        return false;
    std::array<bool, 128> reserved {}, seen {};
    for (const auto& pattern : patterns)
    {
        if (pattern.triggerNote > 127) return false;
        reserved[pattern.triggerNote] = true;
    }
    // Reserve every original mapping before repairing duplicates. Otherwise a
    // repair could steal a later pattern's already reachable MIDI note.
    for (auto& pattern : patterns)
    {
        const auto original = pattern.triggerNote;
        if (seen[original])
            for (int offset = 1; offset < 128; ++offset)
            {
                const auto note = static_cast<std::uint8_t>((original + offset) % 128);
                if (reserved[note]) continue;
                pattern.triggerNote = note;
                reserved[note] = true;
                break;
            }
        seen[original] = true;
    }
    return true;
}

bool ProjectState::selectPatternForTrigger(std::uint8_t note) noexcept
{
    const auto found = std::find_if(patterns.begin(), patterns.end(), [note](const PatternState& pattern) {
        return pattern.triggerNote == note;
    });
    if (found == patterns.end())
        return false;
    activePattern = static_cast<std::size_t>(std::distance(patterns.begin(), found));
    return true;
}

std::optional<std::size_t> ProjectState::padIndexForMidiNote(std::uint8_t note) const noexcept
{
    const auto found = std::find_if(kit.pads.begin(), kit.pads.end(), [note](const PadState& pad) {
        return pad.midiNote == note;
    });
    if (found == kit.pads.end())
        return std::nullopt;
    return static_cast<std::size_t>(std::distance(kit.pads.begin(), found));
}

std::optional<std::size_t> ProjectState::padIndexForInputMidiNote(std::uint8_t note) const noexcept
{
    // The native sampler uses a fixed input map. PadState::midiNote is the
    // editable output map and must not change which incoming note plays a pad.
    constexpr std::array<std::uint8_t, padCount> inputNotes {
        36, 38, 37, 39, 42, 46, 49, 51, 57, 55, 43, 41, 54, 40, 53, 56
    };
    const auto found = std::find(inputNotes.begin(), inputNotes.end(), note);
    if (found == inputNotes.end())
        return std::nullopt;
    return static_cast<std::size_t>(std::distance(inputNotes.begin(), found));
}

} // namespace bsl
