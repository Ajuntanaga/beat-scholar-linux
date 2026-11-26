#pragma once

#include "audio/AudioEngine.h"
#include "plugin/AmbienceConvolution.h"
#include "core/Model.h"
#include "core/PatternScheduler.h"
#include "core/ProjectEditor.h"
#include "core/SequencerPlayback.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

class BeatScholarLinuxProcessor final : public juce::AudioProcessor
{
public:
    BeatScholarLinuxProcessor();
    ~BeatScholarLinuxProcessor() override = default;

    using juce::AudioProcessor::processBlock;
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    // Read while processing is stopped, after prepareToPlay has loaded local assets.
    [[nodiscard]] bool hasFactoryAmbience() const noexcept { return ambience_.isAvailable(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    std::optional<juce::String> getNameForMidiNoteNumber(int note, int midiChannel) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Beat Scholar Linux"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    // Original VST3 advertises no tail; voices and effects still render while stopped.
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void triggerPad(std::size_t pad, float velocity = 0.9f) noexcept;
    void playStandalone() noexcept { standalonePlaying_.store(true); }
    void pauseStandalone() noexcept { standalonePlaying_.store(false); }
    void stopStandalone() noexcept;
    [[nodiscard]] bool isStandalonePlaying() const noexcept { return standalonePlaying_.load(); }
    [[nodiscard]] bool isTransportPlaying() const noexcept { return transportPlaying_.load(); }
    [[nodiscard]] double effectiveBpm() const noexcept { return effectiveBpm_.load(std::memory_order_relaxed); }
    static constexpr std::size_t maximumDecodedSampleBytes = 128u * 1024u * 1024u;
    [[nodiscard]] static bool supportsDecodedSample(std::int64_t frames, unsigned int channels) noexcept;
    juce::Result loadCustomSample(std::size_t pad, const juce::File& file);
    bool setFactorySample(std::size_t pad, std::size_t layer, const juce::String& sampleId);
    void clearCustomSample(std::size_t pad);
    bool loadPresetFile(const juce::File& file);
    bool savePresetFile(const juce::File& file);
    bool loadKitFile(const juce::File& file);
    bool saveKitFile(const juce::File& file);
    void resetPresetToInit();
    void resetKitToInit();
    void resetRandomizerToInit();
    bool loadRandomizerFile(const juce::File& file);
    bool saveRandomizerFile(const juce::File& file);
    void markProjectDirty() noexcept { explicitRevision_.fetch_add(1, std::memory_order_relaxed); }
    void publishProjectStateIfNeeded();
    bool restoreProjectHistory(bool redo);
    void syncAutomatedValuesToProject() { copyParameterValuesToState(projectEditor_.state()); }
    static juce::String padControlId(std::size_t pad, std::size_t control);
    int takeRequestedPatternForUi() noexcept;
    void selectPatternFromUi(std::size_t index);
    std::uint64_t restoredStateRevision() const noexcept
    {
        return restoredStateRevision_.load(std::memory_order_acquire);
    }
    // Message-thread consumer; peaks are accumulated by the callback until read.
    float takePadPeak(std::size_t pad) noexcept { return padPeaks_[pad].exchange(0.0f, std::memory_order_relaxed); }
    float takeOutputPeak(std::size_t channel) noexcept { return outputPeaks_[channel].exchange(0.0f, std::memory_order_relaxed); }
    // Latest rendered RMS, not a peak hold. The independent UI timer shapes it.
    float padGlowRms(std::size_t pad) const noexcept { return padGlowRms_[pad].load(std::memory_order_relaxed); }
    struct PlaybackActivity
    {
        bsl::PatternId pattern = 0;
        std::size_t lane = 0, beat = 0, slice = 0, pad = 0;
        std::uint64_t frame = 0, epoch = 0;
        bool flashPad = true;
        std::uint64_t endFrame = 0; // Zero for pad-only trigger events.
    };
    // Single message-thread consumer; the audio callback never waits for the UI.
    bool takePlaybackActivity(PlaybackActivity&) noexcept;
    int playbackActivityCount() const noexcept { return activityFifo_.getNumReady(); }
    std::uint64_t playbackActivityFrame() const noexcept { return activityFrame_.load(std::memory_order_acquire); }
    std::uint64_t playbackActivityEpoch() const noexcept { return activityEpoch_.load(std::memory_order_acquire); }
    void requestPlaybackActivityRefresh() noexcept { activityRefreshRequested_.store(true, std::memory_order_release); }
    bsl::ProjectState& projectState() noexcept { return projectEditor_.state(); }
    bsl::ProjectEditor& projectEditor() noexcept { return projectEditor_; }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }

private:
    friend struct BeatScholarLinuxProcessorTestAccess;
    static constexpr std::size_t maximumSequencerEvents = 4096;
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    void loadBuiltInPreviewKit(std::optional<std::size_t> onlyPad = std::nullopt);
    void loadFactoryDefaultsIfAvailable();
    void loadFactorySamples(std::optional<std::size_t> onlyPad = std::nullopt);
    void syncParametersFromProject();
    void copyParameterValuesToState(bsl::ProjectState&) const;
    bool installSampleBytes(std::size_t pad, const void* data, std::size_t size,
                            double sampleRate = 0.0,
                            std::optional<std::size_t> layer = std::nullopt);

    bsl::ProjectEditor projectEditor_ { bsl::ProjectState::makeDefault() };
    bsl::AmbienceConvolution ambience_;
    bsl::AudioEngine audioEngine_;
    std::array<std::atomic<float>, bsl::padCount> padPeaks_ {};
    std::array<std::atomic<float>, bsl::padCount> padGlowRms_ {};
    std::array<std::atomic<float>, 2> outputPeaks_ {};
    bsl::AudioBlock scratch_;
    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::array<std::atomic<float>*, 1>, bsl::padCount> padEnableParameters_ {};
    std::array<std::atomic<float>*, bsl::padCount> padOneShotParameters_ {};
    std::array<std::array<std::atomic<float>*, 4>, bsl::padCount> padBypassParameters_ {};
    std::array<std::array<std::atomic<float>*, 19>, bsl::padCount> padControlParameters_ {};
    struct PadAudition { std::uint8_t pad; float velocity; };
    // The editor produces auditions on the message thread; audio consumes them.
    static constexpr int auditionQueueSize = 256;
    juce::AbstractFifo auditionFifo_ { auditionQueueSize };
    std::array<PadAudition, auditionQueueSize> pendingTriggers_ {};
    static constexpr int activityQueueSize = 2048;
    juce::AbstractFifo activityFifo_ { activityQueueSize };
    std::array<PlaybackActivity, activityQueueSize> pendingActivity_ {};
    std::atomic<std::uint64_t> activityFrame_ { 0 }, activityEpoch_ { 0 };
    std::uint64_t audioActivityFrame_ = 0;
    std::atomic<bool> activityRefreshRequested_ { false };
    std::array<PlaybackActivity, bsl::maximumLaneCount> lastVisualActivities_ {};
    bool publishPlaybackActivity(const PlaybackActivity&) noexcept;
    juce::MidiBuffer incomingMidi_;
    std::array<bsl::PlaybackEvent, maximumSequencerEvents> sequencerEvents_ {};
    std::array<bsl::VisualSliceEvent, maximumSequencerEvents> visualSliceEvents_ {};
    bsl::PatternTriggerCollector patternTriggerCollector_;
    std::array<bsl::PatternRenderSegment,
               bsl::PatternScheduler::maximumPendingTriggers + 1> renderSegments_ {};
    std::array<bsl::ProjectState, 3> stateSnapshots_;
    std::array<std::vector<bsl::PatternPlaybackPlan>, 3> playbackPlans_;
    std::array<std::vector<bsl::PatternVisualSlicePlan>, 3> visualSlicePlans_;
    std::atomic<int> publishedStateIndex_ { 0 };
    std::atomic<int> readingStateIndex_ { -1 };
    std::atomic<bsl::PatternId> audioPattern_ { 0 };
    std::atomic<bsl::PatternId> requestedPatternForUi_ { 0 };
    std::atomic<bsl::PatternId> pendingUiPattern_ { 0 };
    std::atomic<std::uint64_t> explicitRevision_ { 1 };
    std::atomic<std::uint64_t> restoredStateRevision_ { 0 };
    std::mutex statePublishMutex_;
    std::uint64_t lastPublishedEditorRevision_ = 0;
    std::uint64_t lastPublishedExplicitRevision_ = 0;
    bsl::PatternId lastPublishedActivePattern_ = 0;
    std::atomic<std::int64_t> fallbackSamplePosition_ { 0 };
    std::atomic<bool> standalonePlaying_ { true };
    std::atomic<bool> transportPlaying_ { false };
    std::atomic<double> effectiveBpm_ { 120.0 };
    // Audio-thread-only callback lifecycle shared by sequenced MIDI and sampler voices.
    bsl::SequencerPlayback sequencerPlayback_;
    // MIDI gate ownership is separate from sampler voices and visual activity.
    // Zero means that an input retrigger already closed this sequencer gate.
    std::array<std::array<std::uint8_t, 128>, bsl::maximumLaneCount> sequencerMidiChannels_ {};
    // 0: no input gate, 1: held input, 2: replaced by a sequencer hit.
    std::array<std::array<std::uint8_t, 128>, 16> inputMidiGates_ {};
    std::int64_t midiExpectedSample_ = 0;
    double midiExpectedPpq_ = std::numeric_limits<double>::quiet_NaN();
    bool midiWasPlaying_ = false;
    bool samplerWasEnabled_ = true;
    bsl::PatternScheduler patternScheduler_;
    bsl::AudioThreadResetRequest resetRequest_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatScholarLinuxProcessor)
};
