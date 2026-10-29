#pragma once

#include "core/Model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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
    float cutoff = 1.0f;
    float resonance = 0.0f;
    bool filterEnabled = true;
    bool driveEnabled = false;
    float driveGainDb = 0.0f;
    float distortionMix = 0.0f;
    bool independentDistortion = false;
    float distortionDrive = 0.5f;
    float compression = 0.0f;
    float crush = 0.0f;
    float reverb = 0.0f;
    float room = 0.0f;
    float delay = 0.0f;
    bool oneShot = true;
    bool muted = false;
    bool solo = false;
};

struct MasterPlaybackSettings
{
    bool filterEnabled = true;
    float cutoff = 1.0f;
    float resonance = 0.0f;
    float delay = 0.0f;
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
    void setRoutingMode(RoutingMode newMode) noexcept;
    void setPadSample(std::size_t padIndex, SampleData sample);
    void setPadLayerSample(std::size_t padIndex, std::size_t layerIndex, SampleData sample);
    void setPadGainPan(std::size_t padIndex, float gain, float pan) noexcept;
    void setPadSettings(std::size_t padIndex, PadPlaybackSettings settings) noexcept;
    void setMasterSettings(MasterPlaybackSettings settings) noexcept;
    void noteOn(std::uint8_t midiNote, float velocity, std::size_t gateFrames = 0) noexcept;
    void noteOff(std::uint8_t midiNote) noexcept;
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
        float velocity = 0.0f;
        std::array<float, 5> lowPassLeft {};
        std::array<float, 5> lowPassRight {};
        float highPassLeft = 0.0f;
        float highPassRight = 0.0f;
        float crushLeft = 0.0f;
        float crushRight = 0.0f;
        std::size_t crushFramesRemaining = 0;
        bool active = false;
    };

    void renderPreparedRange(AudioBlock& output, std::size_t start, std::size_t end) noexcept;
    void clearMasterState() noexcept;

    std::array<std::array<SampleData, 2>, padCount> samples_;
    std::array<std::array<Voice, 4>, padCount> voices_;
    std::array<std::size_t, padCount> nextVoice_ {};
    std::array<PadPlaybackSettings, padCount> settings_;
    MasterPlaybackSettings master_;
    std::array<std::vector<float>, padCount> delayLeft_;
    std::array<std::vector<float>, padCount> delayRight_;
    std::array<std::size_t, padCount> delayPosition_ {};
    std::array<float, 5> masterLowPassLeft_ {};
    std::array<float, 5> masterLowPassRight_ {};
    std::vector<float> masterDelayLeft_;
    std::vector<float> masterDelayRight_;
    std::size_t masterDelayPosition_ = 0;
    std::vector<float> masterEchoLeft_;
    std::vector<float> masterEchoRight_;
    std::size_t masterEchoPosition_ = 0;
    std::vector<float> padDelaySendLeft_;
    std::vector<float> padDelaySendRight_;
    float masterCrushLeft_ = 0.0f;
    float masterCrushRight_ = 0.0f;
    std::size_t masterCrushFramesRemaining_ = 0;
    double sampleRate_ = 48000.0;
    RoutingMode routingMode_ = RoutingMode::stereo;
};

} // namespace bsl
