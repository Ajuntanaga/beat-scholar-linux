#pragma once

#include "core/Model.h"
#include "audio/AmbienceProcessor.h"
#include "audio/AuxDistortion.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace bsl {

using AudioBlock = std::vector<std::vector<float>>;

struct SampleData
{
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::size_t length() const;
};

enum class RoutingMode
{
    stereo,
    multi
};

struct TimedNoteEvent
{
    std::size_t sampleOffset = 0;
    std::uint8_t note = 0;
    float velocity = 0.0f;
    bool noteOn = true;
    std::size_t gateFrames = 0;
};

struct PadPlaybackSettings
{
    float layerMix = 0.0f;
    float gain = 1.0f;
    float pan = 0.0f;
    float attackSeconds = 0.0f;
    float attackNormalized = 0.0f;
    float damp = 0.0f;
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;
    float highPass = 0.0f;
    bool highPassEnabled = true;
    float cutoff = 1.0f;
    float resonance = defaultFilterResonance;
    bool filterEnabled = true;
    bool driveEnabled = false;
    float driveGainDb = 0.0f;
    bool nativeDriveInsert = true;
    float distortionMix = 0.0f;
    bool independentDistortion = false;
    float distortionDrive = 0.5f;
    float compression = 0.0f;
    float crush = 0.0f;
    float reverb = 0.0f;
    float room = 0.0f;
    float delay = 0.0f; // Older own-format echo send.
    float auxGain = 0.0f; // Native Aux4 immediate parallel gain send.
    bool oneShot = true;
    bool mono = false;
    bool muted = false;
    bool solo = false;
};

struct MasterPlaybackSettings
{
    bool filterEnabled = true;
    float cutoff = 1.0f;
    float resonance = defaultFilterResonance;
    float delay = 0.0f; // Older own-format echo return.
    float distortionReturn = 1.0f;
    float distortionDrive = 0.5f;
    float distortion = 0.0f;
    float reverb = 0.0f;
    float room = 0.0f;
    float compression = 0.0f;
    float crush = 0.0f;
    float gain = 1.0f;
};

[[nodiscard]] PadPlaybackSettings makePadPlaybackSettings(const PadState& state) noexcept;
[[nodiscard]] MasterPlaybackSettings makeMasterPlaybackSettings(const MasterState& state,
                                                                 float gain) noexcept;

class AudioEngine
{
public:
    void prepare(double newSampleRate, std::size_t maximumBlockSize = 1024);
    void reset();
    void setDistortionResponse(std::array<std::vector<float>, 2> response)
    { distortion_.setResponse(std::move(response)); }
    // The owner prepares the processor before audio starts and keeps it alive
    // until this engine has stopped. Changing it requires stopped processing.
    void setAmbienceProcessor(AmbienceProcessor* processor) noexcept { ambience_ = processor; }
    void setRoutingMode(RoutingMode newMode) noexcept;
    void setPadSample(std::size_t padIndex, SampleData sample);
    void setPadLayerSample(std::size_t padIndex, std::size_t layerIndex, SampleData sample);
    void setPadGainPan(std::size_t padIndex, float gain, float pan) noexcept;
    void setPadSettings(std::size_t padIndex, PadPlaybackSettings settings) noexcept;
    void setMasterSettings(MasterPlaybackSettings settings) noexcept;
    void setModuleEnables(bool auxEnabled, bool masterEnabled) noexcept
    {
        auxEnabled_ = auxEnabled;
        masterEnabled_ = masterEnabled;
    }
    // Only ungated host notes pair with noteOff; previews opt out.
    void noteOn(std::uint8_t midiNote, float velocity, std::size_t gateFrames = 0,
                bool followsNoteOff = true, std::uint8_t midiChannel = 1) noexcept;
    void noteOff(std::uint8_t midiNote, std::uint8_t midiChannel = 1) noexcept;
    void stopMidiChannel(std::uint8_t midiChannel, bool allSoundOff) noexcept;
    // Streaming callers clear once, then render monotonically between note events.
    void beginBlock(AudioBlock& output, std::size_t sampleCount) noexcept;
    void renderRange(AudioBlock& output, std::size_t start, std::size_t end) noexcept;
    void process(AudioBlock& output, std::size_t sampleCount) noexcept;
    void process(AudioBlock& output, std::size_t sampleCount,
                 std::span<const TimedNoteEvent> events) noexcept;

    [[nodiscard]] const std::array<float, padCount>& padPeaks() const noexcept { return padPeaks_; }

private:
    std::array<float, padCount> padPeaks_ {};
    struct Voice
    {
        std::array<double, 2> positions {};
        std::size_t elapsedFrames = 0;
        std::size_t gateFrames = 0;
        std::uint64_t hostNoteId = 0;
        std::uint8_t hostMidiChannel = 1;
        float velocity = 0.0f;
        std::array<std::array<float, 2>, 3> lowPassLeft {};
        std::array<std::array<float, 2>, 3> lowPassRight {};
        std::array<float, 2> highPassLeft {};
        std::array<float, 2> highPassRight {};
        float driveGainCurrent = 1.0f;
        float driveGainTarget = 1.0f;
        float driveGainStep = 0.0f;
        std::size_t driveRampRemaining = 0;
        float crushLeft = 0.0f;
        float crushRight = 0.0f;
        std::size_t crushFramesElapsed = 0;
        bool active = false;
    };

    void renderPreparedRange(AudioBlock& output, std::size_t start, std::size_t end) noexcept;
    void clearMasterState() noexcept;

    std::array<std::array<SampleData, 2>, padCount> samples_;
    std::array<std::array<Voice, 4>, padCount> voices_;
    std::array<std::size_t, padCount> nextVoice_ {};
    std::array<std::uint64_t, padCount * 16> hostNotesStarted_ {};
    std::array<std::uint64_t, padCount * 16> hostNotesReleased_ {};
    std::array<PadPlaybackSettings, padCount> settings_;
    MasterPlaybackSettings master_;
    bool auxEnabled_ = true;
    bool masterEnabled_ = true;
    AmbienceProcessor* ambience_ = nullptr;
    std::array<std::vector<float>, 4> ambienceSends_;
    AuxDistortion distortion_;
    std::array<std::vector<float>, 2> distortionSends_;
    struct LinearRamp
    {
        float current = 0.0f;
        float target = 0.0f;
        float step = 0.0f;
        std::size_t remaining = 0;

        void setTarget(float value, std::size_t frames) noexcept;
        float next() noexcept;
    };
    struct Compressor
    {
        LinearRamp amount;
        LinearRamp gain;
        std::array<float, 2> envelope {};
        std::size_t gainFrames = 0;
        float attack = 0.0f;
        float release = 0.0f;

        void setAmount(float value, double sampleRate) noexcept;
        void snap(float value) noexcept;
        void process(float& left, float& right) noexcept;
    };
    std::array<Compressor, padCount> padCompressors_;
    std::array<LinearRamp, padCount> padGains_;
    std::array<LinearRamp, padCount> padAuxGains_;
    std::array<LinearRamp, padCount> padDistortionSends_;
    LinearRamp distortionReturn_;
    std::array<bool, padCount> padDspResetPending_ {};
    Compressor masterCompressor_;
    LinearRamp masterGain_;
    std::array<std::array<float, 2>, 2> masterLowPassLeft_ {};
    std::array<std::array<float, 2>, 2> masterLowPassRight_ {};
    std::vector<float> masterEchoLeft_;
    std::vector<float> masterEchoRight_;
    std::size_t masterEchoPosition_ = 0;
    std::vector<float> padDelaySendLeft_;
    std::vector<float> padDelaySendRight_;
    float masterCrushLeft_ = 0.0f;
    float masterCrushRight_ = 0.0f;
    std::size_t masterCrushFramesElapsed_ = 0;
    double sampleRate_ = 48000.0;
    RoutingMode routingMode_ = RoutingMode::stereo;
};

} // namespace bsl
