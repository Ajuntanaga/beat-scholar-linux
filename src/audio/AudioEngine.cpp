#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bsl {
namespace {

float cutoffFrequency(float normalized, double sampleRate)
{
    const double maximum = std::min(20000.0, sampleRate * 0.45);
    return static_cast<float>(20.0 * std::pow(maximum / 20.0,
                                              std::sqrt(std::clamp(static_cast<double>(normalized),
                                                                   0.0, 1.0))));
}

float compress(float value, float amount)
{
    if (amount <= 0.0f)
        return value;
    const float gain = 1.0f + amount * 0.490625f;
    return std::clamp(value * gain, -1.0f, 1.0f);
}

float saturate(float value, float gain)
{
    const float denominator = std::tanh(gain);
    return denominator > 0.0f ? std::tanh(value * gain) / denominator : value;
}

float parallelSaturate(float value, float mix, float gain)
{
    const float wet = saturate(value, gain);
    return value + (wet - value) * mix;
}

std::size_t crushHoldFrames(float amount)
{
    return 1 + static_cast<std::size_t>(std::lround(std::clamp(amount, 0.0f, 1.0f) * 20.0f));
}

float crushQuantize(float value, float amount)
{
    const auto bitDepth = std::clamp(
        static_cast<int>(std::lround((1.0f - std::clamp(amount, 0.0f, 1.0f)) * 16.0f)),
        1, 16);
    const float levels = static_cast<float>(1u << (bitDepth - 1));
    return std::round(value * levels) / levels;
}

} // namespace

bool SampleData::isValid() const
{
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || channels.empty()
        || channels.size() > 2 || channels.front().empty())
        return false;
    const auto expected = channels.front().size();
    return std::all_of(channels.begin(), channels.end(), [expected](const auto& channel) {
        return channel.size() == expected;
    });
}

std::size_t SampleData::length() const
{
    return channels.empty() ? 0 : channels.front().size();
}

PadPlaybackSettings makePadPlaybackSettings(const PadState& state) noexcept
{
    PadPlaybackSettings settings;
    settings.layerMix = state.layerMix;
    settings.gain = state.gain;
    settings.pan = state.pan;
    settings.attackSeconds = state.attackSeconds;
    settings.attackNormalized = state.attackNormalized;
    settings.damp = state.damp;
    settings.pitchSemitones = state.pitchSemitones;
    settings.fineCents = state.fineCents;
    settings.highPass = state.hpfBypass ? 0.0f : state.highPass;
    settings.cutoff = state.cutoff;
    settings.resonance = state.resonance;
    settings.filterEnabled = state.filterEnabled && !state.lpfBypass;
    settings.driveEnabled = state.driveEnabled && !state.driveBypass;
    settings.driveGainDb = state.driveGainDb;
    settings.distortionMix = state.distortionMix;
    settings.independentDistortion = state.independentDistortion;
    settings.distortionDrive = state.distortionDrive;
    settings.compression = state.compression;
    settings.crush = state.crushBypass ? 0.0f : state.crush;
    settings.reverb = state.reverb;
    settings.room = state.room;
    settings.delay = state.delay;
    settings.oneShot = state.oneShot;
    settings.muted = state.muted;
    settings.solo = state.solo;
    return settings;
}

MasterPlaybackSettings makeMasterPlaybackSettings(const MasterState& state, float gain) noexcept
{
    MasterPlaybackSettings settings;
    settings.filterEnabled = state.filterEnabled;
    settings.cutoff = state.cutoff;
    settings.resonance = state.resonance;
    settings.delay = state.delay;
    settings.distortion = state.distortion;
    settings.reverb = state.reverb;
    settings.room = state.room;
    settings.compression = state.compression;
    settings.crush = state.crush;
    settings.gain = gain;
    return settings;
}

void AudioEngine::prepare(double newSampleRate, std::size_t maximumBlockSize)
{
    if (std::isfinite(newSampleRate) && newSampleRate > 0.0)
        sampleRate_ = newSampleRate;
    settings_.fill({});
    master_ = {};
    nextVoice_.fill(0);
    delayPosition_.fill(0);
    const auto delayLength = std::max<std::size_t>(2, static_cast<std::size_t>(sampleRate_ * 0.5));
    for (std::size_t pad = 0; pad < padCount; ++pad)
    {
        delayLeft_[pad].assign(delayLength, 0.0f);
        delayRight_[pad].assign(delayLength, 0.0f);
    }
    masterDelayLeft_.assign(delayLength, 0.0f);
    masterDelayRight_.assign(delayLength, 0.0f);
    masterEchoLeft_.assign(delayLength, 0.0f);
    masterEchoRight_.assign(delayLength, 0.0f);
    padDelaySendLeft_.assign(std::max<std::size_t>(1, maximumBlockSize), 0.0f);
    padDelaySendRight_.assign(padDelaySendLeft_.size(), 0.0f);
    reset();
}

void AudioEngine::reset()
{
    padPeaks_.fill(0.0f);
    hostNotesStarted_.fill(0);
    hostNotesReleased_.fill(0);
    for (auto& padVoices : voices_)
        padVoices.fill({});
    for (auto& delay : delayLeft_)
        std::fill(delay.begin(), delay.end(), 0.0f);
    for (auto& delay : delayRight_)
        std::fill(delay.begin(), delay.end(), 0.0f);
    delayPosition_.fill(0);
    clearMasterState();
}

void AudioEngine::clearMasterState() noexcept
{
    std::fill(masterDelayLeft_.begin(), masterDelayLeft_.end(), 0.0f);
    std::fill(masterDelayRight_.begin(), masterDelayRight_.end(), 0.0f);
    std::fill(masterEchoLeft_.begin(), masterEchoLeft_.end(), 0.0f);
    std::fill(masterEchoRight_.begin(), masterEchoRight_.end(), 0.0f);
    masterLowPassLeft_.fill(0.0f);
    masterLowPassRight_.fill(0.0f);
    masterCrushLeft_ = 0.0f;
    masterCrushRight_ = 0.0f;
    masterCrushFramesRemaining_ = 0;
    masterDelayPosition_ = 0;
    masterEchoPosition_ = 0;
}

void AudioEngine::setRoutingMode(RoutingMode newMode) noexcept
{
    if (routingMode_ != newMode)
        clearMasterState();
    routingMode_ = newMode;
}

void AudioEngine::setPadSample(std::size_t padIndex, SampleData sample)
{
    if (padIndex >= padCount)
        return;
    samples_[padIndex][0] = sample.isValid() ? std::move(sample) : SampleData {};
    samples_[padIndex][1] = {};
    voices_[padIndex].fill({});
}

void AudioEngine::setPadLayerSample(std::size_t padIndex, std::size_t layerIndex, SampleData sample)
{
    if (padIndex >= padCount || layerIndex >= 2)
        return;
    samples_[padIndex][layerIndex] = sample.isValid() ? std::move(sample) : SampleData {};
    voices_[padIndex].fill({});
}

void AudioEngine::setPadGainPan(std::size_t padIndex, float gain, float pan) noexcept
{
    if (padIndex >= padCount)
        return;
    settings_[padIndex].gain = std::clamp(gain, 0.0f, 4.0f);
    settings_[padIndex].pan = std::clamp(pan, -1.0f, 1.0f);
}

void AudioEngine::setPadSettings(std::size_t padIndex, PadPlaybackSettings settings) noexcept
{
    if (padIndex >= padCount)
        return;
    settings.layerMix = std::clamp(settings.layerMix, 0.0f, 1.0f);
    settings.gain = std::clamp(settings.gain, 0.0f, 4.0f);
    settings.pan = std::clamp(settings.pan, -1.0f, 1.0f);
    settings.attackSeconds = std::clamp(settings.attackSeconds, 0.0f, 10.0f);
    settings.attackNormalized = std::clamp(settings.attackNormalized, 0.0f, 1.0f);
    settings.damp = std::clamp(settings.damp, 0.0f, 1.0f);
    settings.pitchSemitones = std::clamp(settings.pitchSemitones, -12.0f, 12.0f);
    settings.fineCents = std::clamp(settings.fineCents, -100.0f, 100.0f);
    settings.highPass = std::clamp(settings.highPass, 0.0f, 1.0f);
    settings.cutoff = std::clamp(settings.cutoff, 0.0f, 1.0f);
    settings.resonance = std::clamp(settings.resonance, 0.0f, 1.0f);
    settings.driveGainDb = std::clamp(settings.driveGainDb, -60.0f, 60.0f);
    settings.distortionMix = std::clamp(settings.distortionMix, 0.0f, 1.0f);
    settings.distortionDrive = std::clamp(settings.distortionDrive, 0.0f, 1.0f);
    settings.compression = std::clamp(settings.compression, 0.0f, 1.0f);
    settings.crush = std::clamp(settings.crush, 0.0f, 1.0f);
    settings.reverb = std::clamp(settings.reverb, 0.0f, 1.0f);
    settings.room = std::clamp(settings.room, 0.0f, 1.0f);
    settings.delay = std::clamp(settings.delay, 0.0f, 1.0f);
    settings_[padIndex] = settings;
}

void AudioEngine::setMasterSettings(MasterPlaybackSettings settings) noexcept
{
    settings.cutoff = std::clamp(settings.cutoff, 0.0f, 1.0f);
    settings.resonance = std::clamp(settings.resonance, 0.0f, 1.0f);
    settings.delay = std::clamp(settings.delay, 0.0f, 1.0f);
    settings.distortion = std::clamp(settings.distortion, 0.0f, 1.0f);
    settings.reverb = std::clamp(settings.reverb, 0.0f, 1.0f);
    settings.room = std::clamp(settings.room, 0.0f, 1.0f);
    settings.compression = std::clamp(settings.compression, 0.0f, 1.0f);
    settings.crush = std::clamp(settings.crush, 0.0f, 1.0f);
    settings.gain = std::clamp(settings.gain, 0.0f, 4.0f);
    master_ = settings;
}

void AudioEngine::noteOn(std::uint8_t midiNote, float velocity, std::size_t gateFrames,
                         bool followsNoteOff) noexcept
{
    if (midiNote < 36 || midiNote >= 36 + padCount)
        return;
    const auto pad = static_cast<std::size_t>(midiNote - 36);
    const auto hostNoteId = gateFrames == 0 && followsNoteOff ? ++hostNotesStarted_[pad] : 0;
    if (!samples_[pad][0].isValid() && !samples_[pad][1].isValid())
        return;
    auto& padVoices = voices_[pad];
    auto voiceIndex = nextVoice_[pad];
    for (std::size_t index = 0; index < padVoices.size(); ++index)
        if (!padVoices[index].active)
        {
            voiceIndex = index;
            break;
        }
    padVoices[voiceIndex] = {};
    padVoices[voiceIndex].velocity = std::clamp(velocity, 0.0f, 1.0f);
    padVoices[voiceIndex].gateFrames = gateFrames;
    padVoices[voiceIndex].hostNoteId = hostNoteId;
    padVoices[voiceIndex].active = true;
    nextVoice_[pad] = (voiceIndex + 1) % padVoices.size();
}

void AudioEngine::noteOff(std::uint8_t midiNote) noexcept
{
    if (midiNote < 36 || midiNote >= 36 + padCount)
        return;
    const auto pad = static_cast<std::size_t>(midiNote - 36);
    if (hostNotesReleased_[pad] == hostNotesStarted_[pad])
        return;
    // Consume releases in note-on order even when an old voice has ended
    // or been stolen, so its release cannot cut a newer note.
    const auto releasedId = ++hostNotesReleased_[pad];
    if (!settings_[pad].oneShot)
        for (auto& voice : voices_[pad])
            if (voice.hostNoteId == releasedId)
                voice.active = false;
}

void AudioEngine::process(AudioBlock& output, std::size_t sampleCount) noexcept
{
    process(output, sampleCount, {});
}

void AudioEngine::beginBlock(AudioBlock& output, std::size_t sampleCount) noexcept
{
    padPeaks_.fill(0.0f);
    const auto available = output.empty() ? std::size_t { 0 }
                                          : std::min(sampleCount, output.front().size());
    for (auto& channel : output)
        std::fill_n(channel.begin(), std::min(available, channel.size()), 0.0f);

}

void AudioEngine::process(AudioBlock& output, std::size_t sampleCount,
                          std::span<const TimedNoteEvent> events) noexcept
{
    beginBlock(output, sampleCount);
    const auto available = output.empty() ? std::size_t { 0 }
                                          : std::min(sampleCount, output.front().size());

    if (available == 0 || output.size() < 2)
        return;

    std::size_t cursor = 0;
    for (const auto& event : events)
    {
        const auto offset = std::min(event.sampleOffset, available);
        if (offset > cursor)
            renderRange(output, cursor, offset);
        if (event.noteOn && event.velocity > 0.0f)
            noteOn(event.note, event.velocity, event.gateFrames);
        else
            noteOff(event.note);
        cursor = offset;
    }
    if (cursor < available)
        renderRange(output, cursor, available);
}

void AudioEngine::renderRange(AudioBlock& output, std::size_t start, std::size_t end) noexcept
{
    // Large offline blocks use the same prepared scratch storage in chunks.
    // Neither a first callback nor a block-size change allocates on this path.
    const auto capacity = padDelaySendLeft_.size();
    if (capacity == 0) return;
    while (start < end)
    {
        const auto next = start + std::min(capacity, end - start);
        renderPreparedRange(output, start, next);
        start = next;
    }
}

void AudioEngine::renderPreparedRange(AudioBlock& output, std::size_t start, std::size_t end) noexcept
{
    std::fill_n(padDelaySendLeft_.begin(), end - start, 0.0f);
    std::fill_n(padDelaySendRight_.begin(), end - start, 0.0f);
    const bool hasSolo = std::any_of(settings_.begin(), settings_.end(), [](const auto& settings) {
        return settings.solo;
    });

    constexpr float halfPi = 1.57079632679489661923f;
    for (std::size_t pad = 0; pad < padCount; ++pad)
    {
        const auto& padSamples = samples_[pad];
        const auto& settings = settings_[pad];
        if (!padSamples[0].isValid() && !padSamples[1].isValid())
            continue;

        const float leftPan = std::cos((settings.pan + 1.0f) * 0.5f * halfPi);
        const float rightPan = std::sin((settings.pan + 1.0f) * 0.5f * halfPi);
        const std::size_t target = routingMode_ == RoutingMode::stereo ? 0 : 2 + pad * 2;
        if (target + 1 >= output.size())
            continue;

        const double semitones = static_cast<double>(settings.pitchSemitones)
                                 + static_cast<double>(settings.fineCents) / 100.0;
        // The original's normalized tune range measures 0.992 semitones per
        // displayed semitone; keep the display/state semantic while matching playback.
        constexpr double measuredPitchScale = 0.992;
        const double pitchRatio = std::pow(2.0, semitones * measuredPitchScale / 12.0);
        const float attackSeconds = settings.attackSeconds > 0.0f
                                        ? settings.attackSeconds
                                        : settings.attackNormalized;
        const auto attackFrames = static_cast<std::size_t>(attackSeconds * sampleRate_);
        const float lowPassCoefficient = std::exp(-2.0f * 3.14159265358979323846f
                                                   * cutoffFrequency(settings.cutoff, sampleRate_)
                                                   / static_cast<float>(sampleRate_));
        const float highPassCoefficient = std::exp(-2.0f * 3.14159265358979323846f
                                                    * cutoffFrequency(settings.highPass, sampleRate_)
                                                    / static_cast<float>(sampleRate_));
        auto& delayLeft = delayLeft_[pad];
        auto& delayRight = delayRight_[pad];
        const auto shortDelay = std::min(delayLeft.size() - 1,
                                         static_cast<std::size_t>(sampleRate_ * 0.08));
        const auto longDelay = std::min(delayLeft.size() - 1,
                                        static_cast<std::size_t>(sampleRate_ * 0.33));
        for (std::size_t frame = start; frame < end; ++frame)
        {
            float dryLeft = 0.0f;
            float dryRight = 0.0f;
            const bool audible = !settings.muted && (!hasSolo || settings.solo);
            for (auto& voice : voices_[pad])
            {
                // Each sequenced voice owns its gate so a shorter overlapping
                // hit cannot cut off another voice on the same pad.
                if (!settings.oneShot && voice.gateFrames > 0
                    && voice.elapsedFrames >= voice.gateFrames)
                    voice.active = false;
                if (!voice.active)
                {
                    continue;
                }
                    float envelope = 1.0f;
                    if (attackFrames > 0)
                        envelope = std::min(1.0f, static_cast<float>(voice.elapsedFrames + 1)
                                                       / static_cast<float>(attackFrames));
                    float lengthEnvelope = 1.0f;
                    if (settings.damp > 0.0f)
                    {
                        constexpr double measuredMaximumLengthSeconds = 0.8604166666666667;
                        constexpr double measuredLengthCurve = 0.45;
                        const double decayFrames = sampleRate_ * measuredMaximumLengthSeconds
                                                   * (1.0 - settings.damp);
                        const double remaining = decayFrames > 0.0
                            ? std::max(0.0, 1.0 - static_cast<double>(voice.elapsedFrames)
                                                     / decayFrames)
                            : 0.0;
                        lengthEnvelope = static_cast<float>(std::pow(remaining,
                                                                     measuredLengthCurve));
                    }
                    float left = 0.0f;
                    float right = 0.0f;
                    bool anyLayerPlaying = false;
                    const bool bothLayers = padSamples[0].isValid() && padSamples[1].isValid();
                    for (std::size_t layer = 0; layer < 2; ++layer)
                    {
                        const auto& sample = padSamples[layer];
                        if (!sample.isValid())
                            continue;
                        const auto index = static_cast<std::size_t>(voice.positions[layer]);
                        if (index >= sample.length() || lengthEnvelope <= 0.0f)
                            continue;
                        const float weight = !bothLayers ? 1.0f
                            : (layer == 0 ? 1.0f - settings.layerMix : settings.layerMix);
                        const auto nextIndex = std::min(index + 1, sample.length() - 1);
                        const float fraction = static_cast<float>(voice.positions[layer]
                                                                  - static_cast<double>(index));
                        const float sourceLeft = sample.channels.front()[index]
                            + (sample.channels.front()[nextIndex]
                               - sample.channels.front()[index]) * fraction;
                        const float sourceRight = sample.channels.size() == 1
                            ? sourceLeft
                            : sample.channels[1][index]
                                + (sample.channels[1][nextIndex] - sample.channels[1][index])
                                    * fraction;
                        left += sourceLeft * weight;
                        right += sourceRight * weight;
                        voice.positions[layer] += sample.sampleRate / sampleRate_ * pitchRatio;
                        anyLayerPlaying = true;
                    }
                    if (!anyLayerPlaying)
                    {
                        voice.active = false;
                        continue;
                    }
                    left *= envelope * lengthEnvelope;
                    right *= envelope * lengthEnvelope;
                    if (settings.highPass > 0.0f)
                    {
                        voice.highPassLeft = (1.0f - highPassCoefficient) * left
                                             + highPassCoefficient * voice.highPassLeft;
                        voice.highPassRight = (1.0f - highPassCoefficient) * right
                                              + highPassCoefficient * voice.highPassRight;
                        left -= voice.highPassLeft;
                        right -= voice.highPassRight;
                    }
                    if (settings.filterEnabled && settings.cutoff < 0.999f)
                    {
                        for (std::size_t stage = 0; stage < voice.lowPassLeft.size(); ++stage)
                        {
                            voice.lowPassLeft[stage] = (1.0f - lowPassCoefficient) * left
                                                       + lowPassCoefficient * voice.lowPassLeft[stage];
                            voice.lowPassRight[stage] = (1.0f - lowPassCoefficient) * right
                                                        + lowPassCoefficient * voice.lowPassRight[stage];
                            left = voice.lowPassLeft[stage];
                            right = voice.lowPassRight[stage];
                        }
                        left *= 1.0f + settings.resonance * 0.3f;
                        right *= 1.0f + settings.resonance * 0.3f;
                    }
                    if (settings.driveEnabled && (settings.independentDistortion || settings.distortionMix > 0.0f))
                    {
                        const float gain = std::pow(10.0f, settings.driveGainDb / 20.0f);
                        const float mix = settings.independentDistortion ? 1.0f : settings.distortionMix;
                        left = parallelSaturate(left, mix, gain);
                        right = parallelSaturate(right, mix, gain);
                    }
                    if (settings.crush > 0.0f)
                    {
                        if (voice.crushFramesRemaining == 0)
                        {
                            voice.crushLeft = crushQuantize(left, settings.crush);
                            voice.crushRight = crushQuantize(right, settings.crush);
                            voice.crushFramesRemaining = crushHoldFrames(settings.crush);
                        }
                        left = voice.crushLeft;
                        right = voice.crushRight;
                        --voice.crushFramesRemaining;
                    }
                    else
                    {
                        voice.crushFramesRemaining = 0;
                    }
                    left = compress(left, settings.compression);
                    right = compress(right, settings.compression);
                    if (audible)
                    {
                        dryLeft += left * leftPan * settings.gain * voice.velocity;
                        dryRight += right * rightPan * settings.gain * voice.velocity;
                    }
                    ++voice.elapsedFrames;
            }

            // Legacy Drive is an insert; Aux3/Dist is an independent parallel
            // drum control. Its drive configuration must never saturate the
            // entire master when this pad's send is zero.
            if (settings.independentDistortion && settings.distortionMix > 0.0f)
            {
                const float gain = 1.0f + settings.distortionDrive * 15.0f;
                dryLeft = parallelSaturate(dryLeft, settings.distortionMix, gain);
                dryRight = parallelSaturate(dryRight, settings.distortionMix, gain);
            }
            const auto position = delayPosition_[pad];
            const auto shortPosition = (position + delayLeft.size() - shortDelay) % delayLeft.size();
            const auto longPosition = (position + delayLeft.size() - longDelay) % delayLeft.size();
            const float shortLeft = delayLeft[shortPosition];
            const float shortRight = delayRight[shortPosition];
            const float longLeft = delayLeft[longPosition];
            const float longRight = delayRight[longPosition];
            const float wetLeft = shortLeft * settings.room * 0.55f
                                  + longLeft * settings.reverb * 0.55f;
            const float wetRight = shortRight * settings.room * 0.55f
                                   + longRight * settings.reverb * 0.55f;
            delayLeft[position] = dryLeft + shortLeft * settings.room * 0.24f
                                  + longLeft * settings.reverb * 0.46f;
            delayRight[position] = dryRight + shortRight * settings.room * 0.24f
                                   + longRight * settings.reverb * 0.46f;
            delayPosition_[pad] = (position + 1) % delayLeft.size();
            // Keep the delay history advancing while the pad is inaudible.
            if (audible)
            {
                padPeaks_[pad] = std::max(padPeaks_[pad],
                    std::max(std::abs(dryLeft + wetLeft), std::abs(dryRight + wetRight)));
                output[target][frame] += dryLeft + wetLeft;
                output[target + 1][frame] += dryRight + wetRight;
            }
            if (routingMode_ == RoutingMode::stereo)
            {
                padDelaySendLeft_[frame - start] += dryLeft * settings.delay;
                padDelaySendRight_[frame - start] += dryRight * settings.delay;
            }
        }
    }

    if (routingMode_ == RoutingMode::stereo)
    {
        const float coefficient = std::exp(-2.0f * 3.14159265358979323846f
                                            * cutoffFrequency(master_.cutoff, sampleRate_)
                                            / static_cast<float>(sampleRate_));
        for (std::size_t frame = start; frame < end; ++frame)
        {
            float left = output[0][frame];
            float right = output[1][frame];
            if (master_.filterEnabled && master_.cutoff < 0.999f)
            {
                for (std::size_t stage = 0; stage < masterLowPassLeft_.size(); ++stage)
                {
                    masterLowPassLeft_[stage] = (1.0f - coefficient) * left
                                                + coefficient * masterLowPassLeft_[stage];
                    masterLowPassRight_[stage] = (1.0f - coefficient) * right
                                                 + coefficient * masterLowPassRight_[stage];
                    left = masterLowPassLeft_[stage];
                    right = masterLowPassRight_[stage];
                }
                left *= 1.0f + master_.resonance * 0.3f;
                right *= 1.0f + master_.resonance * 0.3f;
            }
            if (master_.distortion > 0.0f)
            {
                const float gain = 1.0f + master_.distortion * 15.0f;
                left = parallelSaturate(left, master_.distortion, gain);
                right = parallelSaturate(right, master_.distortion, gain);
            }
            if (master_.crush > 0.0f)
            {
                if (masterCrushFramesRemaining_ == 0)
                {
                    masterCrushLeft_ = crushQuantize(left, master_.crush);
                    masterCrushRight_ = crushQuantize(right, master_.crush);
                    masterCrushFramesRemaining_ = crushHoldFrames(master_.crush);
                }
                left = masterCrushLeft_;
                right = masterCrushRight_;
                --masterCrushFramesRemaining_;
            }
            else
            {
                masterCrushFramesRemaining_ = 0;
            }
            left = compress(left, master_.compression);
            right = compress(right, master_.compression);

            const auto position = masterDelayPosition_;
            const auto echoPosition = masterEchoPosition_;
            const auto delayFrames = std::min(masterDelayLeft_.size() - 1,
                                              static_cast<std::size_t>(sampleRate_ * 0.25));
            const auto roomFrames = std::min(masterDelayLeft_.size() - 1,
                                             static_cast<std::size_t>(sampleRate_ * 0.065));
            const auto reverbFrames = std::min(masterDelayLeft_.size() - 1,
                                               static_cast<std::size_t>(sampleRate_ * 0.37));
            const auto tap = [position, size = masterDelayLeft_.size()](std::size_t offset) {
                return (position + size - offset) % size;
            };
            const auto echoTap = [echoPosition, size = masterEchoLeft_.size()](std::size_t offset) {
                return (echoPosition + size - offset) % size;
            };
            const float delayedLeft = masterEchoLeft_[echoTap(delayFrames)];
            const float delayedRight = masterEchoRight_[echoTap(delayFrames)];
            const float roomLeft = masterDelayLeft_[tap(roomFrames)];
            const float roomRight = masterDelayRight_[tap(roomFrames)];
            const float reverbLeft = masterDelayLeft_[tap(reverbFrames)];
            const float reverbRight = masterDelayRight_[tap(reverbFrames)];
            const float wetLeft = delayedLeft * master_.delay * 0.65f
                                  + roomLeft * master_.room * 0.5f
                                  + reverbLeft * master_.reverb * 0.55f;
            const float wetRight = delayedRight * master_.delay * 0.65f
                                   + roomRight * master_.room * 0.5f
                                   + reverbRight * master_.reverb * 0.55f;
            masterEchoLeft_[echoPosition] = padDelaySendLeft_[frame - start]
                                             + delayedLeft * master_.delay * 0.38f;
            masterEchoRight_[echoPosition] = padDelaySendRight_[frame - start]
                                              + delayedRight * master_.delay * 0.38f;
            masterDelayLeft_[position] = left + roomLeft * master_.room * 0.18f
                                               + reverbLeft * master_.reverb * 0.4f;
            masterDelayRight_[position] = right + roomRight * master_.room * 0.18f
                                                 + reverbRight * master_.reverb * 0.4f;
            masterDelayPosition_ = (position + 1) % masterDelayLeft_.size();
            masterEchoPosition_ = (echoPosition + 1) % masterEchoLeft_.size();
            output[0][frame] = (left + wetLeft) * master_.gain;
            output[1][frame] = (right + wetRight) * master_.gain;
        }
    }
}

} // namespace bsl
