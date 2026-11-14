#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace bsl {
namespace {

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

float auxiliaryTaper(float amount, float decibelsPerHalving)
{
    return amount > 0.0f
        ? std::pow(10.0f, decibelsPerHalving * std::log2(amount) / 20.0f) : 0.0f;
}

float padDriveAmount(float gainDb)
{
    return std::clamp(std::pow((std::pow(10.0f, std::clamp(gainDb, 0.0f, 24.0f) / 20.0f) - 1.0f)
                              / 14.848931925f, 0.431693899f), 0.0f, 1.0f);
}

struct CrushParameters
{
    std::size_t holdFrames;
    float quantizationDivisor;
};

CrushParameters crushParameters(float amount)
{
    const auto fromNormalized = [](float value, float minimum, float maximum, float skew) {
        const float curved = value > 0.0f ? std::exp(std::log(value) / skew) : 0.0f;
        return minimum + (maximum - minimum) * curved;
    };
    const auto hold = static_cast<std::size_t>(
        fromNormalized(amount, 1.0f, 64.0f, 0.16730017960071564f));
    const auto bits = static_cast<int>(
        fromNormalized(1.0f - amount, 3.0f, 16.0f, 0.27023816108703613f));
    return { hold, bits < 16 ? std::ldexp(1.0f, bits) : 0.0f };
}

float crushQuantize(float value, float divisor)
{
    if (divisor == 0.0f)
        return value;
    const float quantized = std::nearbyint((value + 1.0f) * divisor * 0.5f);
    return (quantized + quantized) / divisor - 1.0f;
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
    // The native layer Volume endpoint is +6 dB before the insert chain.
    // Pad volume and the 0.3 output trim follow compression.
    settings.gain = state.gain;
    settings.layerInputGain = 1.9952623844146729f; // Captured native float: 0x3fff64c2.
    settings.outputTrim = 0.3f;
    settings.pan = state.pan;
    settings.attackSeconds = state.attackSeconds;
    settings.releaseSeconds = state.releaseSeconds;
    settings.attackNormalized = state.attackNormalized;
    settings.damp = state.damp;
    settings.pitchSemitones = state.pitchSemitones;
    settings.fineCents = state.fineCents;
    settings.highPass = state.highPass;
    settings.highPassEnabled = !state.hpfBypass;
    settings.cutoff = state.cutoff;
    settings.resonance = state.resonance;
    settings.filterEnabled = state.filterEnabled && !state.lpfBypass;
    settings.driveEnabled = state.driveEnabled && !state.driveBypass;
    settings.driveGainDb = state.driveGainDb;
    settings.nativeDriveInsert = state.nativeDriveInsert;
    settings.distortionMix = state.distortionMix;
    settings.independentDistortion = state.independentDistortion;
    settings.distortionDrive = state.distortionDrive;
    settings.compression = state.compression;
    settings.crush = state.crushBypass ? 0.0f : state.crush;
    settings.reverb = state.reverb;
    settings.room = state.room;
    settings.delay = state.delay;
    settings.auxGain = state.auxGain;
    settings.oneShot = state.oneShot;
    settings.mono = state.mono;
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
    settings.distortionReturn = state.distortionReturn;
    settings.distortion = state.distortion;
    settings.reverb = state.reverb;
    settings.room = state.room;
    settings.compression = state.compression;
    settings.crush = state.crush;
    settings.gain = gain;
    return settings;
}

void AudioEngine::LinearRamp::setTarget(float value, std::size_t frames) noexcept
{
    const float difference = std::abs(value - target);
    if (difference <= std::numeric_limits<float>::min()
        || difference <= std::max(std::abs(value), std::abs(target))
                             * std::numeric_limits<float>::epsilon())
        return;
    target = value;
    remaining = frames;
    if (frames == 0)
        current = target;
    else
        step = (target - current) / static_cast<float>(frames);
}

float AudioEngine::LinearRamp::next() noexcept
{
    if (remaining > 0)
        current = --remaining == 0 ? target : current + step;
    return current;
}

void AudioEngine::Compressor::setAmount(float value, double sampleRate) noexcept
{
    // Native c794c0 prepares 10 ms amount and 100 ms makeup-gain ramps.
    // The amount duration is float precision before the double-rate product.
    amount.setTarget(value, static_cast<std::size_t>(sampleRate * static_cast<double>(0.01f)));
    gainFrames = static_cast<std::size_t>(sampleRate * 0.1);
    attack = static_cast<float>(std::exp(-6283.185307179586 / (sampleRate * 70.0)));
    release = static_cast<float>(std::exp(-6283.185307179586 / (sampleRate * 10.0)));
}

void AudioEngine::Compressor::snap(float value) noexcept
{
    amount = { value, value };
    const float makeup = 1.0f + value * 0.5499999523162842f;
    gain = { makeup, makeup };
    envelope.fill(0.0f);
}

void AudioEngine::Compressor::process(float& left, float& right) noexcept
{
    const float compression = amount.next();
    if (compression <= 0.001f)
        return;

    // c79ae0/d45d90: independent peak envelopes, no hard clipping. Bypass
    // freezes both envelopes and the makeup-gain ramp for pad and master alike.
    const float thresholdDb = 20.0f * std::log10(0.2f + (1.0f - compression) * 0.8f);
    const float threshold = std::pow(10.0f, thresholdDb * 0.05f);
    const float exponent = 1.0f / (4.0f + compression * 6.0f) - 1.0f;
    gain.setTarget(1.0f + compression * 0.5499999523162842f, gainFrames);
    const float postGain = gain.next();
    const auto compressChannel = [&](float input, std::size_t channel) {
        const float magnitude = std::abs(input);
        auto& level = envelope[channel];
        const float coefficient = magnitude > level ? attack : release;
        level = magnitude + coefficient * (level - magnitude);
        const float reduction = level < threshold ? 1.0f
            : std::pow(level / threshold, exponent);
        return input * reduction * postGain;
    };
    left = compressChannel(left, 0);
    right = compressChannel(right, 1);
}

void AudioEngine::prepare(double newSampleRate, std::size_t maximumBlockSize)
{
    if (std::isfinite(newSampleRate) && newSampleRate > 0.0)
        sampleRate_ = newSampleRate;
    distortion_.prepare(sampleRate_);
    for (auto& send : distortionSends_)
        send.assign(std::max<std::size_t>(1, maximumBlockSize), 0.0f);
    settings_.fill({});
    master_ = {};
    const auto delayLength = std::max<std::size_t>(2, static_cast<std::size_t>(sampleRate_ * 0.5));
    for (auto& send : ambienceSends_)
        send.assign(std::max<std::size_t>(1, maximumBlockSize), 0.0f);
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
    voices_.fill({});
    nextVoiceOrder_ = 0;
    if (ambience_ != nullptr)
        ambience_->reset();
    padCompressors_.fill({});
    padGains_.fill({});
    padAuxGains_.fill({});
    padDistortionSends_.fill({});
    distortionReturn_ = {};
    distortion_.reset();
    padDspResetPending_.fill(false);
    clearMasterState();
}

void AudioEngine::clearMasterState() noexcept
{
    std::fill(masterEchoLeft_.begin(), masterEchoLeft_.end(), 0.0f);
    std::fill(masterEchoRight_.begin(), masterEchoRight_.end(), 0.0f);
    masterCompressor_ = {};
    masterLowPassLeft_ = {};
    masterLowPassRight_ = {};
    masterCrushLeft_ = 0.0f;
    masterCrushRight_ = 0.0f;
    masterCrushFramesElapsed_ = 0;
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
    for (auto& voice : voices_)
        if (voice.pad == padIndex)
            voice = {};
}

void AudioEngine::setPadLayerSample(std::size_t padIndex, std::size_t layerIndex, SampleData sample)
{
    if (padIndex >= padCount || layerIndex >= 2)
        return;
    samples_[padIndex][layerIndex] = sample.isValid() ? std::move(sample) : SampleData {};
    for (auto& voice : voices_)
        if (voice.pad == padIndex)
            voice = {};
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
    settings.releaseSeconds = std::isfinite(settings.releaseSeconds)
        ? std::clamp(settings.releaseSeconds, 0.0f, 10.0f) : 0.03f;
    settings.attackNormalized = std::clamp(settings.attackNormalized, 0.0f, 1.0f);
    settings.damp = std::clamp(settings.damp, 0.0f, 1.0f);
    // The native Tune parameter is integer-valued, with nearest-even rounding.
    settings.pitchSemitones = std::nearbyint(std::clamp(settings.pitchSemitones, -12.0f, 12.0f));
    settings.fineCents = std::clamp(settings.fineCents, -100.0f, 100.0f);
    settings.highPass = std::clamp(settings.highPass, 0.0f, 1.0f);
    settings.cutoff = std::clamp(settings.cutoff, 0.0f, 1.0f);
    settings.resonance = std::clamp(settings.resonance, 0.0f, 1.0f);
    settings.driveGainDb = settings.nativeDriveInsert
        ? std::clamp(settings.driveGainDb, 0.0f, 24.0f)
        : std::clamp(settings.driveGainDb, -60.0f, 60.0f);
    settings.distortionMix = std::clamp(settings.distortionMix, 0.0f, 1.0f);
    settings.distortionDrive = std::clamp(settings.distortionDrive, 0.0f, 1.0f);
    settings.compression = std::clamp(settings.compression, 0.0f, 1.0f);
    settings.crush = std::clamp(settings.crush, 0.0f, 1.0f);
    settings.reverb = std::clamp(settings.reverb, 0.0f, 1.0f);
    settings.room = std::clamp(settings.room, 0.0f, 1.0f);
    settings.delay = std::clamp(settings.delay, 0.0f, 1.0f);
    settings.auxGain = std::clamp(settings.auxGain, 0.0f, 1.0f);
    settings_[padIndex] = settings;
}

void AudioEngine::setMasterSettings(MasterPlaybackSettings settings) noexcept
{
    settings.cutoff = std::clamp(settings.cutoff, 0.0f, 1.0f);
    settings.resonance = std::clamp(settings.resonance, 0.0f, 1.0f);
    settings.delay = std::clamp(settings.delay, 0.0f, 1.0f);
    settings.distortionReturn = std::clamp(settings.distortionReturn, 0.0f, 1.0f);
    settings.distortion = std::clamp(settings.distortion, 0.0f, 1.0f);
    settings.reverb = std::clamp(settings.reverb, 0.0f, 1.0f);
    settings.room = std::clamp(settings.room, 0.0f, 1.0f);
    settings.compression = std::clamp(settings.compression, 0.0f, 1.0f);
    settings.crush = std::clamp(settings.crush, 0.0f, 1.0f);
    settings.gain = std::clamp(settings.gain, 0.0f, 4.0f);
    master_ = settings;
}

AudioEngine::Voice& AudioEngine::findVoiceToStart(std::size_t pad) noexcept
{
    for (auto& voice : voices_)
        if (!voice.active)
            return voice;

    // Native MPE stealing protects the first held lowest/highest pool slots.
    // Within each priority tier, the oldest note wins, regardless of slot order.
    Voice* lowest = nullptr;
    Voice* highest = nullptr;
    for (auto& voice : voices_)
        if (voice.keyDown)
        {
            if (lowest == nullptr || voice.pad < lowest->pad)
                lowest = &voice;
            if (highest == nullptr || voice.pad > highest->pad)
                highest = &voice;
        }
    if (highest == lowest)
        highest = nullptr;
    const auto priority = [&](const Voice& voice) {
        if (voice.pad == pad) return 0;
        if (&voice != lowest && &voice != highest)
            return voice.keyDown ? 2 : 1;
        return &voice == highest ? 3 : 4;
    };
    auto* oldest = &voices_.front();
    for (auto& voice : voices_)
        if (priority(voice) < priority(*oldest)
            || (priority(voice) == priority(*oldest)
                && voice.noteOnOrder < oldest->noteOnOrder))
            oldest = &voice;
    return *oldest;
}

void AudioEngine::noteOn(std::uint8_t midiNote, float velocity, std::size_t gateFrames,
                         bool followsNoteOff, std::uint8_t midiChannel) noexcept
{
    startVoice(midiNote, velocity, gateFrames, followsNoteOff, midiChannel, false);
}

void AudioEngine::sequencerNoteOn(std::uint8_t midiNote, float velocity, std::size_t laneIndex) noexcept
{
    sequencerNoteOff(midiNote, laneIndex);
    startVoice(midiNote, velocity, 0, false, 1, true, laneIndex);
}

void AudioEngine::sequencerNoteOff(std::uint8_t midiNote, std::size_t laneIndex) noexcept
{
    if (midiNote < 36 || midiNote >= 36 + padCount)
        return;
    const auto pad = static_cast<std::size_t>(midiNote - 36);
    for (auto& voice : voices_)
        if (voice.active && voice.pad == pad && voice.sequencerOwned
            && voice.sequencerLane == laneIndex)
        {
            voice.keyDown = false;
            if (!settings_[pad].oneShot)
                voice.releasing = true;
        }
}

void AudioEngine::startVoice(std::uint8_t midiNote, float velocity, std::size_t gateFrames,
                             bool followsNoteOff, std::uint8_t midiChannel,
                             bool sequencerOwned, std::size_t sequencerLane) noexcept
{
    if (midiNote < 36 || midiNote >= 36 + padCount || midiChannel < 1 || midiChannel > 16)
        return;
    const auto pad = static_cast<std::size_t>(midiNote - 36);
    const auto channelIndex = pad * 16 + midiChannel - 1;
    const auto hostNoteId = gateFrames == 0 && followsNoteOff ? ++hostNotesStarted_[channelIndex] : 0;
    if (!samples_[pad][0].isValid() && !samples_[pad][1].isValid())
        return;
    // Native gated MIDI retriggers release the preceding held voice. One Shot
    // hits remain polyphonic; scheduled gates and previews own their lifetimes.
    if (hostNoteId != 0 && !settings_[pad].oneShot)
        for (auto& voice : voices_)
            if (voice.active && voice.pad == pad && voice.hostNoteId != 0
                && voice.hostMidiChannel == midiChannel)
            {
                voice.keyDown = false;
                voice.releasing = true;
            }
    if (std::none_of(voices_.begin(), voices_.end(),
                     [pad](const Voice& voice) { return voice.active && voice.pad == pad; }))
        padDspResetPending_[pad] = true;
    if (settings_[pad].mono)
        for (auto& voice : voices_)
            if (voice.pad == pad)
                voice.active = false;
    auto& voice = findVoiceToStart(pad);
    // c97811 resets the native LPF on note start, but preserves the HPF. Its
    // history belongs to this global voice, even when reassigned to another pad.
    const auto highPassLeft = voice.highPassLeft;
    const auto highPassRight = voice.highPassRight;
    voice = {};
    voice.highPassLeft = highPassLeft;
    voice.highPassRight = highPassRight;
    voice.pad = pad;
    voice.noteOnOrder = ++nextVoiceOrder_;
    // A new native voice starts at the current Drive gain, even while bypassed.
    const float driveGain = 0.4f + 0.6f * (1.0f - padDriveAmount(settings_[pad].driveGainDb));
    voice.driveGainCurrent = driveGain;
    voice.driveGainTarget = driveGain;
    // Native captures show a 5% floor for positive note velocities.
    // Keep MIDI/host normalization outside this voice amplitude law.
    const float normalizedVelocity = std::isfinite(velocity) ? std::clamp(velocity, 0.0f, 1.0f) : 0.0f;
    voice.velocity = normalizedVelocity > 0.0f ? 0.05f + 0.95f * normalizedVelocity : 0.0f;
    voice.gateFrames = gateFrames;
    voice.sequencerOwned = sequencerOwned;
    voice.sequencerLane = sequencerLane;
    voice.hostNoteId = hostNoteId;
    voice.hostMidiChannel = midiChannel;
    voice.active = true;
}

void AudioEngine::noteOff(std::uint8_t midiNote, std::uint8_t midiChannel) noexcept
{
    if (midiNote < 36 || midiNote >= 36 + padCount || midiChannel < 1 || midiChannel > 16)
        return;
    const auto pad = static_cast<std::size_t>(midiNote - 36);
    const auto channelIndex = pad * 16 + midiChannel - 1;
    if (hostNotesReleased_[channelIndex] == hostNotesStarted_[channelIndex])
        return;
    ++hostNotesReleased_[channelIndex];
    for (auto& voice : voices_)
        if (voice.active && voice.pad == pad && voice.hostNoteId != 0
            && voice.hostMidiChannel == midiChannel)
        {
            // MPE key state changes even when One Shot ignores the release.
            voice.keyDown = false;
            if (!settings_[pad].oneShot)
                voice.releasing = true;
        }
}

void AudioEngine::stopMidiChannel(std::uint8_t midiChannel, bool allSoundOff) noexcept
{
    if (midiChannel < 1 || midiChannel > 16)
        return;
    for (std::size_t pad = 0; pad < padCount; ++pad)
    {
        const auto index = pad * 16 + midiChannel - 1;
        hostNotesReleased_[index] = hostNotesStarted_[index];
    }
    bool anyActive = false;
    for (auto& voice : voices_)
    {
        if (voice.active && voice.hostMidiChannel == midiChannel)
        {
            voice.keyDown = false;
            if (allSoundOff || (voice.hostNoteId != 0 && !settings_[voice.pad].oneShot))
                voice.active = false;
        }
        anyActive = anyActive || voice.active;
    }
    // Shared effects cannot be separated by MIDI channel. Clear residual tails
    // when the last voice is silenced, without resetting other channels' voices.
    if (allSoundOff && !anyActive)
        reset();
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
    for (auto& send : ambienceSends_)
        std::fill_n(send.begin(), end - start, 0.0f);
    std::fill_n(padDelaySendLeft_.begin(), end - start, 0.0f);
    std::fill_n(padDelaySendRight_.begin(), end - start, 0.0f);
    const bool hasSolo = std::any_of(settings_.begin(), settings_.end(), [](const auto& settings) {
        return settings.solo;
    });

    masterGain_.setTarget(master_.gain, static_cast<std::size_t>(sampleRate_ * 0.1));
    distortionReturn_.setTarget(auxiliaryTaper(master_.distortionReturn, 18.0f),
                                static_cast<std::size_t>(sampleRate_ * 0.1));
    for (auto& send : distortionSends_) std::fill_n(send.begin(), end - start, 0.0f);
    constexpr float halfPi = 1.57079632679489661923f;
    for (std::size_t pad = 0; pad < padCount; ++pad)
    {
        const auto& padSamples = samples_[pad];
        const auto& settings = settings_[pad];
        // Aux4 ramps from zero over 100 ms, including silent time before a hit.
        // It is independent of per-voice and idle-to-active resets.
        auto& auxGain = padAuxGains_[pad];
        auxGain.setTarget(auxiliaryTaper(settings.auxGain, 12.0f),
                          static_cast<std::size_t>(sampleRate_ * 0.1));
        auto& distortionSend = padDistortionSends_[pad];
        distortionSend.setTarget(settings.independentDistortion
                                     ? auxiliaryTaper(settings.distortionMix, 12.0f) : 0.0f,
                                 static_cast<std::size_t>(sampleRate_ * 0.1));
        const std::size_t target = routingMode_ == RoutingMode::stereo ? 0 : 2 + pad * 2;
        if ((!padSamples[0].isValid() && !padSamples[1].isValid())
            || target + 1 >= output.size())
        {
            for (std::size_t frame = start; frame < end; ++frame)
            { auxGain.next(); distortionSend.next(); }
            continue;
        }

        std::array<Voice*, voiceCount> padVoiceStorage {};
        std::size_t padVoiceCount = 0;
        for (auto& voice : voices_)
            if (voice.active && voice.pad == pad)
                padVoiceStorage[padVoiceCount++] = &voice;
        const std::span<Voice*> padVoices(padVoiceStorage.data(), padVoiceCount);

        // Native layer mix runs from layer 2 to layer 1 with a -3 dB centre.
        // Preserve the Volume range's float log/divide/exp order: an equivalent
        // power curve rounds differently before the recursive filters.
        const auto layerWeight = [](float amount) {
            return amount > 0.0f
                       ? std::exp(std::log(amount) / 1.0034332275390625f) : 0.0f;
        };
        const float layerAngle = std::clamp(settings.layerMix, 0.0f, 1.0f) * halfPi;
        const float layerWeights[] {
            layerWeight(std::sin(layerAngle)), layerWeight(std::cos(layerAngle))
        };
        const float leftPan = std::cos((settings.pan + 1.0f) * 0.5f * halfPi);
        const float rightPan = std::sin((settings.pan + 1.0f) * 0.5f * halfPi);

        const double semitones = static_cast<double>(settings.pitchSemitones)
                                 + static_cast<double>(settings.fineCents) / 100.0;
        const double pitchRatio = std::pow(2.0, semitones / 12.0);
        const float attackSeconds = settings.attackSeconds > 0.0f
                                        ? settings.attackSeconds
                                        : settings.attackNormalized;
        const auto attackFrames = static_cast<std::size_t>(attackSeconds * sampleRate_);
        // Native Length fades linearly even at zero. Its full duration follows
        // the longest loaded layer, independent of the layer crossfade position.
        double longestLayerSeconds = 0.0;
        for (const auto& sample : padSamples)
            if (sample.isValid())
                longestLayerSeconds = std::max(longestLayerSeconds,
                    static_cast<double>(sample.length()) / sample.sampleRate);
        const double lengthFrames = sampleRate_
            * (0.01 + (longestLayerSeconds - 0.01) * (1.0 - settings.damp));
        const float lowPassFrequency = std::min(filterCutoffHz(settings.cutoff),
                                                static_cast<float>(sampleRate_ * 0.499));
        const float lowPassG = std::tan(3.14159265358979323846f * lowPassFrequency
                                        / static_cast<float>(sampleRate_));
        const float lowPassR = 1.0f / (0.1f + 2.4f * settings.resonance);
        const float lowPassA = 1.0f / (lowPassG * lowPassG + lowPassR * lowPassG + 1.0f);
        const auto lowPass = [=](float input, auto& stages) {
            for (auto& state : stages)
            {
                const float high = ((input - (lowPassR + lowPassG) * state[0]) - state[1]) * lowPassA;
                const float band = lowPassG * high + state[0];
                const float low = lowPassG * band + state[1];
                state[0] = band + lowPassG * high;
                state[1] = low + lowPassG * band;
                input = low;
            }
            return input;
        };
        const float highPassFrequency = std::min(std::max(10.0f, padHighPassCutoffHz(settings.highPass)),
                                                 static_cast<float>(sampleRate_ * 0.499));
        const float highPassT = std::tan(3.14159265358979323846f * highPassFrequency
                                        / static_cast<float>(sampleRate_));
        // Native type-1 biquad, Q=.5, in transposed direct form II.
        const float highPassB0 = 1.0f / (1.0f + 2.0f * highPassT + highPassT * highPassT);
        const float highPassB1 = -2.0f * highPassB0;
        const float highPassA1 = 2.0f * (highPassT * highPassT - 1.0f) * highPassB0;
        const float highPassA2 = (1.0f - 2.0f * highPassT + highPassT * highPassT) * highPassB0;
        const auto highPass = [=](float input, auto& state) {
            const float result = input * highPassB0 + state[0];
            state[0] = (input * highPassB1 - highPassA1 * result) + state[1];
            state[1] = input * highPassB0 - highPassA2 * result;
            return result;
        };
        // Native Drive uses the gain parameter's normalized position to
        // shape the insert. It is independent of the Aux3 distortion send.
        const bool nativeDriveActive = settings.nativeDriveInsert && settings.driveEnabled
                                       && settings.driveGainDb > 0.0f;
        const float driveAmount = nativeDriveActive ? padDriveAmount(settings.driveGainDb) : 0.0f;
        const float driveThreshold = 0.1f + 0.9f * (1.0f - driveAmount);
        const float drivePostGain = 0.4f + 0.6f * (1.0f - driveAmount);
        const auto nativeDrive = [=](float input) {
            const float x = std::clamp(input, -driveThreshold, driveThreshold) / driveThreshold;
            return 1.5f * x - 0.5f * x * x * x;
        };
        // Native post-gain ramps over floor(sampleRate * .1) frames. The
        // threshold changes immediately; bypass freezes each voice's ramp.
        if (nativeDriveActive)
        {
            const auto rampFrames = static_cast<std::size_t>(sampleRate_ * 0.1);
            for (auto* current : padVoices)
            {
                auto& voice = *current;
                if (voice.active
                    && std::abs(drivePostGain - voice.driveGainTarget)
                           > std::numeric_limits<float>::epsilon()
                                 * std::max(drivePostGain, voice.driveGainTarget))
                {
                    voice.driveGainTarget = drivePostGain;
                    voice.driveRampRemaining = rampFrames;
                    if (rampFrames == 0)
                        voice.driveGainCurrent = drivePostGain;
                    else
                        voice.driveGainStep = (drivePostGain - voice.driveGainCurrent)
                                                  / static_cast<float>(rampFrames);
                }
            }
        }
        auto& compressor = padCompressors_[pad];
        compressor.setAmount(settings.compression, sampleRate_);
        auto& padGain = padGains_[pad];
        padGain.setTarget(settings.gain, static_cast<std::size_t>(sampleRate_ * 0.1));
        if (padDspResetPending_[pad])
        {
            // Native c7f800/c7a000 snaps only on idle-to-active transitions.
            compressor.snap(settings.compression);
            padGain = { settings.gain, settings.gain };
            padDspResetPending_[pad] = false;
        }
        const auto crusher = crushParameters(settings.crush);
        std::array<bool, voiceCount> finishedSamples {};
        for (std::size_t frame = start; frame < end; ++frame)
        {
            float dryLeft = 0.0f;
            float dryRight = 0.0f;
            const bool audible = !settings.muted && (!hasSolo || settings.solo);
            for (auto* current : padVoices)
            {
                auto& voice = *current;
                // Each sequenced voice owns its gate so a shorter overlapping
                // hit cannot cut off another voice on the same pad.
                if (voice.gateFrames > 0 && voice.elapsedFrames >= voice.gateFrames)
                {
                    voice.keyDown = false;
                    if (!settings.oneShot)
                        voice.releasing = true;
                }
                if (!voice.active)
                {
                    continue;
                }
                    float envelope = 1.0f;
                    float releaseInputGain = 1.0f;
                    if (attackFrames > 0)
                        envelope = std::min(1.0f, static_cast<float>(voice.elapsedFrames + 1)
                                                       / static_cast<float>(attackFrames));
                    if (voice.releasing)
                    {
                        if (voice.releaseFramesElapsed == 0)
                        {
                            voice.releaseStartLevel = attackFrames > 0
                                ? std::min(1.0f, static_cast<float>(voice.elapsedFrames)
                                    / static_cast<float>(attackFrames)) : 1.0f;
                            voice.releaseInputStep = voice.releaseInputLevel
                                / (static_cast<float>(sampleRate_) * 0.1f);
                        }
                        ++voice.releaseFramesElapsed;
                        const double releaseFrames = settings.releaseSeconds * sampleRate_;
                        const float remaining = releaseFrames > 0.0
                            ? std::max(0.0f, 1.0f - static_cast<float>(
                                static_cast<double>(voice.releaseFramesElapsed) / releaseFrames)) : 0.0f;
                        // The native sampler has its own 100 ms float release
                        // before the inserts. It keeps advancing after the pad's
                        // output release reaches silence, preserving filter history.
                        voice.releaseInputLevel = std::max(0.0f,
                            voice.releaseInputLevel - voice.releaseInputStep);
                        releaseInputGain = voice.releaseInputLevel;
                        envelope = voice.releaseStartLevel * remaining;
                    }
                    // Native updates a float accumulator before rendering each
                    // frame; a closed-form fade loses its rate-dependent rounding.
                    if (settings.lengthEnvelopeEnabled && settings.oneShot)
                        voice.lengthEnvelope = std::max(0.0f, voice.lengthEnvelope
                            - (lengthFrames > 0.0 ? static_cast<float>(1.0 / lengthFrames) : 1.0f));
                    // Gated voices sustain; Length belongs to the One Shot envelope.
                    const float lengthEnvelope = settings.lengthEnvelopeEnabled && settings.oneShot
                        ? voice.lengthEnvelope : 1.0f;
                    const float inputEnvelope = lengthEnvelope * releaseInputGain;
                    float left = 0.0f;
                    float right = 0.0f;
                    bool anyLayerPlaying = false;
                    const bool bothLayers = padSamples[0].isValid() && padSamples[1].isValid();
                    for (std::size_t layer = 0; layer < 2; ++layer)
                    {
                        const auto& sample = padSamples[layer];
                        if (!sample.isValid())
                            continue;
                        if (voice.positions[layer] > 1.0
                            || inputEnvelope <= 0.0f)
                            continue;
                        const float weight = !bothLayers ? 1.0f
                            : layerWeights[layer];
                        // Native playback maps normalized phase onto the last source frame.
                        // Emit the clamped endpoint once before advancing to the finished sentinel.
                        const double position = voice.positions[layer]
                            * static_cast<double>(sample.length() - 1);
                        const auto index = static_cast<std::size_t>(position);
                        const float fraction = static_cast<float>(position - static_cast<double>(index));
                        const auto interpolate = [&](const auto& channel) {
                            const float previous = channel[index > 0 ? index - 1 : 0];
                            const float current = channel[index];
                            const float next = channel[std::min(index + 1, sample.length() - 1)];
                            const float following = channel[std::min(index + 2, sample.length() - 1)];
                            // Preserve the native polynomial's float operation order.
                            // Horner evaluation changes the input to recursive filters.
                            const float squared = fraction * fraction;
                            const float cubed = squared * fraction;
                            const float quadratic = (4.0f * next
                                + (previous + previous + current * -5.0f)) - following;
                            const float linear = (current + current) + (next - previous) * fraction;
                            const float cubic = (next * -3.0f + (current * 3.0f - previous)) + following;
                            return (cubic * cubed + (quadratic * squared + linear)) * 0.5f;
                        };
                        const float sourceLeft = interpolate(sample.channels.front());
                        const float sourceRight = sample.channels.size() == 1
                            ? sourceLeft : interpolate(sample.channels[1]);
                        // Each native layer applies its envelope and Volume
                        // before summing into HPF, Drive and the other inserts.
                        const float layerGain = weight * settings.layerInputGain;
                        left += (sourceLeft * inputEnvelope) * layerGain;
                        right += (sourceRight * inputEnvelope) * layerGain;
                        voice.positions[layer] = voice.positions[layer] >= 1.0 ? 2.0
                            : std::min(1.0, voice.positions[layer]
                                + (1.0 / sampleRate_)
                                    / (static_cast<double>(sample.length()) / sample.sampleRate)
                                    * pitchRatio);
                        anyLayerPlaying = true;
                    }
                    // The native outer voice runs its inserts over the remainder
                    // of the current render span after the sampler finishes.
                    // Retire it at the span boundary, preserving that final tail.
                    if (!anyLayerPlaying)
                        finishedSamples[static_cast<std::size_t>(&voice - voices_.data())] = true;
                    if (settings.highPassEnabled)
                    {
                        left = highPass(left, voice.highPassLeft);
                        right = highPass(right, voice.highPassRight);
                    }
                    if (nativeDriveActive)
                    {
                        if (voice.driveRampRemaining > 0)
                        {
                            --voice.driveRampRemaining;
                            voice.driveGainCurrent = voice.driveRampRemaining == 0
                                ? voice.driveGainTarget : voice.driveGainCurrent + voice.driveGainStep;
                        }
                        left = nativeDrive(left) * voice.driveGainCurrent;
                        right = nativeDrive(right) * voice.driveGainCurrent;
                    }
                    else if (!settings.nativeDriveInsert && settings.driveEnabled
                             && (settings.independentDistortion || settings.distortionMix > 0.0f))
                    {
                        const float gain = std::pow(10.0f, settings.driveGainDb / 20.0f);
                        const float mix = settings.independentDistortion ? 1.0f : settings.distortionMix;
                        left = parallelSaturate(left, mix, gain);
                        right = parallelSaturate(right, mix, gain);
                    }
                    if (crusher.holdFrames >= 2)
                    {
                        if (voice.crushFramesElapsed >= crusher.holdFrames)
                            voice.crushFramesElapsed = 0;
                        if (voice.crushFramesElapsed == 0)
                        {
                            voice.crushLeft = left;
                            voice.crushRight = right;
                        }
                        left = voice.crushLeft;
                        right = voice.crushRight;
                        ++voice.crushFramesElapsed;
                    }
                    // Quantize the held raw sample with the current depth so
                    // automation changes the sound even between hold boundaries.
                    if (crusher.quantizationDivisor != 0.0f)
                    {
                        left = crushQuantize(left, crusher.quantizationDivisor);
                        right = crushQuantize(right, crusher.quantizationDivisor);
                    }
                    if (settings.filterEnabled)
                    {
                        left = lowPass(left, voice.lowPassLeft);
                        right = lowPass(right, voice.lowPassRight);
                    }
                    left *= envelope;
                    right *= envelope;
                    if (audible)
                    {
                        dryLeft += left * leftPan * voice.velocity;
                        dryRight += right * rightPan * voice.velocity;
                    }
                    ++voice.elapsedFrames;
            }

            // Pad volume follows the compressor; its detector sees the fixed boost.
            // All sends receive the resulting post-volume, post-compressor signal.
            const float padOutputGain = padGain.next();
            compressor.process(dryLeft, dryRight);
            dryLeft *= padOutputGain;
            dryRight *= padOutputGain;
            dryLeft *= settings.outputTrim;
            dryRight *= settings.outputTrim;

            const float parallelGain = auxGain.next();
            const float distortionGain = distortionSend.next();
            if (audible)
            {
                // Sum all post-pad sends before the shared nonlinear processor.
                distortionSends_[0][frame - start] += dryLeft * distortionGain;
                distortionSends_[1][frame - start] += dryRight * distortionGain;
                if (auxEnabled_)
                {
                    output[0][frame] += dryLeft * parallelGain;
                    output[1][frame] += dryRight * parallelGain;
                }
            }
            if (audible)
            {
                padPeaks_[pad] = std::max(padPeaks_[pad],
                    std::max(std::abs(dryLeft), std::abs(dryRight)));
                output[target][frame] += dryLeft;
                output[target + 1][frame] += dryRight;
                ambienceSends_[0][frame - start] += dryLeft * settings.room;
                ambienceSends_[1][frame - start] += dryRight * settings.room;
                ambienceSends_[2][frame - start] += dryLeft * settings.reverb;
                ambienceSends_[3][frame - start] += dryRight * settings.reverb;
            }
            if (routingMode_ == RoutingMode::stereo)
            {
                padDelaySendLeft_[frame - start] += dryLeft * settings.delay;
                padDelaySendRight_[frame - start] += dryRight * settings.delay;
            }
        }
        for (auto* voice : padVoices)
        {
            if (finishedSamples[static_cast<std::size_t>(voice - voices_.data())])
                voice->active = false;
            if (settings.highPassEnabled)
                for (auto* state : { &voice->highPassLeft, &voice->highPassRight })
                    for (auto& value : *state)
                        if (std::abs(value) <= 1.0e-8f)
                            value = 0.0f;
        }
    }

    distortion_.process(distortionSends_[0].data(), distortionSends_[1].data(),
                        end - start, master_.distortionDrive);
    for (std::size_t frame = start; frame < end; ++frame)
    {
        const float gain = distortionReturn_.next() * 1.995262315f;
        if (auxEnabled_)
        {
            output[0][frame] += distortionSends_[0][frame - start] * gain;
            output[1][frame] += distortionSends_[1][frame - start] * gain;
        }
    }
    if (ambience_ != nullptr)
    {
        ambience_->process(ambienceSends_[0].data(), ambienceSends_[1].data(),
                           ambienceSends_[2].data(), ambienceSends_[3].data(), end - start);
        // Native shared returns feed the main stereo bus in both routing modes,
        // before master processing. Room is +3 dB and Plate is +6 dB.
        constexpr float roomReturnGain = 1.4125375f;
        constexpr float plateReturnGain = 1.9952623f;
        // Keep shared processors advancing while their audible returns are off.
        for (std::size_t frame = start; auxEnabled_ && frame < end; ++frame)
        {
            const auto offset = frame - start;
            output[0][frame] += ambienceSends_[0][offset] * roomReturnGain
                                + ambienceSends_[2][offset] * plateReturnGain;
            output[1][frame] += ambienceSends_[1][offset] * roomReturnGain
                                + ambienceSends_[3][offset] * plateReturnGain;
        }
    }

    if (routingMode_ == RoutingMode::stereo)
    {
        const auto crusher = crushParameters(master_.crush);
        if (masterEnabled_)
            masterCompressor_.setAmount(master_.compression, sampleRate_);
        // Native master LPF: two serial TPT stages, Q 0.1..2.5. Original-plugin
        // captures at 48 and 96 kHz retain a 44.1 kHz coefficient rate, including
        // after a cutoff retune. Preserve that response independently of the host.
        constexpr float masterFilterRate = 44100.0f;
        const float frequency = filterCutoffHz(master_.cutoff);
        const float g = std::tan(3.14159265358979323846f * frequency / masterFilterRate);
        const float inverseQ = 1.0f / (0.1f + 2.4f * master_.resonance);
        const float h = 1.0f / (1.0f + inverseQ * g + g * g);
        const auto lowPass = [g, inverseQ, h](float input, auto& stages) {
            for (auto& state : stages)
            {
                const float high = (input - (g + inverseQ) * state[0] - state[1]) * h;
                const float band = g * high + state[0];
                const float low = g * band + state[1];
                state[0] = 2.0f * band - state[0];
                state[1] = 2.0f * low - state[1];
                input = low;
            }
            return input;
        };
        for (std::size_t frame = start; frame < end; ++frame)
        {
            float left = output[0][frame];
            float right = output[1][frame];
            if (masterEnabled_)
            {
                masterCompressor_.process(left, right);
                if (crusher.holdFrames >= 2)
                {
                    if (masterCrushFramesElapsed_ >= crusher.holdFrames)
                        masterCrushFramesElapsed_ = 0;
                    if (masterCrushFramesElapsed_ == 0)
                    {
                        masterCrushLeft_ = left;
                        masterCrushRight_ = right;
                    }
                    left = masterCrushLeft_;
                    right = masterCrushRight_;
                    ++masterCrushFramesElapsed_;
                }
                // Both native crusher instances hold the raw signal and apply the
                // current depth afterward; a one-frame hold freezes the saved phase.
                left = crushQuantize(left, crusher.quantizationDivisor);
                right = crushQuantize(right, crusher.quantizationDivisor);
                // Native bypass freezes both filter stages until re-enabled.
                if (master_.filterEnabled && master_.cutoff < 0.9999f)
                {
                    left = lowPass(left, masterLowPassLeft_);
                    right = lowPass(right, masterLowPassRight_);
                }
                if (master_.distortion > 0.0f)
                {
                    const float gain = 1.0f + master_.distortion * 15.0f;
                    left = parallelSaturate(left, master_.distortion, gain);
                    right = parallelSaturate(right, master_.distortion, gain);
                }
            }

            const auto echoPosition = masterEchoPosition_;
            const auto delayFrames = std::min(masterEchoLeft_.size() - 1,
                                              static_cast<std::size_t>(sampleRate_ * 0.25));
            const auto echoTap = (echoPosition + masterEchoLeft_.size() - delayFrames)
                                 % masterEchoLeft_.size();
            const float delayedLeft = masterEchoLeft_[echoTap];
            const float delayedRight = masterEchoRight_[echoTap];
            const float wetLeft = auxEnabled_ ? delayedLeft * master_.delay * 0.65f : 0.0f;
            const float wetRight = auxEnabled_ ? delayedRight * master_.delay * 0.65f : 0.0f;
            masterEchoLeft_[echoPosition] = padDelaySendLeft_[frame - start]
                                             + delayedLeft * master_.delay * 0.38f;
            masterEchoRight_[echoPosition] = padDelaySendRight_[frame - start]
                                              + delayedRight * master_.delay * 0.38f;
            masterEchoPosition_ = (echoPosition + 1) % masterEchoLeft_.size();
            const float gain = masterGain_.next();
            output[0][frame] = (left + wetLeft) * gain;
            output[1][frame] = (right + wetRight) * gain;
        }
    }
    else
    {
        // The native master-DSP bypass still applies master volume to main.
        // Individual Multi pad outputs bypass this gain.
        for (std::size_t frame = start; frame < end; ++frame)
        {
            const float gain = masterGain_.next();
            output[0][frame] *= gain;
            output[1][frame] *= gain;
        }
    }
}

} // namespace bsl
