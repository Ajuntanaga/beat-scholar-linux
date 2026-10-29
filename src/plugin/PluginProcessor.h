#pragma once

#include "audio/AudioEngine.h"
#include "core/Model.h"
#include "core/PatternScheduler.h"
#include "core/ProjectEditor.h"
#include "core/Sequencer.h"

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
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    std::optional<juce::String> getNameForMidiNoteNumber(int note, int midiChannel) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Beat Scholar Linux"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

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
    static constexpr std::size_t maximumDecodedSampleBytes = 128u * 1024u * 1024u;
    [[nodiscard]] static bool supportsDecodedSample(std::int64_t frames, unsigned int channels) noexcept;
    juce::Result loadCustomSample(std::size_t pad, const juce::File& file);
    bool setFactorySample(std::size_t pad, std::size_t layer, const juce::String& sampleId);
    void clearCustomSample(std::size_t pad);
    bool loadPresetFile(const juce::File& file);
    bool savePresetFile(const juce::File& file);
    bool loadKitFile(const juce::File& file);
    bool saveKitFile(const juce::File& file);
    bool loadRandomizerFile(const juce::File& file);
    bool saveRandomizerFile(const juce::File& file);
    void markProjectDirty() noexcept { explicitRevision_.fetch_add(1, std::memory_order_relaxed); }
    void publishProjectStateIfNeeded();
    bool restoreProjectHistory(bool redo);
    int takeRequestedPatternForUi() noexcept { return requestedPatternForUi_.exchange(-1); }
    std::uint64_t restoredStateRevision() const noexcept
    {
        return restoredStateRevision_.load(std::memory_order_acquire);
    }
    // Message-thread consumer; peaks are accumulated by the callback until read.
    float takePadPeak(std::size_t pad) noexcept { return padPeaks_[pad].exchange(0.0f, std::memory_order_relaxed); }
    float takeOutputPeak(std::size_t channel) noexcept { return outputPeaks_[channel].exchange(0.0f, std::memory_order_relaxed); }
    bsl::ProjectState& projectState() noexcept { return projectEditor_.state(); }
    bsl::ProjectEditor& projectEditor() noexcept { return projectEditor_; }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }

private:
    static constexpr std::size_t maximumSequencerEvents = 4096;
    static BusesProperties makeBuses();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    void loadBuiltInPreviewKit(std::optional<std::size_t> onlyPad = std::nullopt);
    void loadFactoryKitIfAvailable();
    void loadFactorySamples(std::optional<std::size_t> onlyPad = std::nullopt);
    void syncParametersFromProject();
    void copyParameterValuesToState(bsl::ProjectState&) const;
    bool installSampleBytes(std::size_t pad, const void* data, std::size_t size);

    bsl::ProjectEditor projectEditor_ { bsl::ProjectState::makeDefault() };
    bsl::AudioEngine audioEngine_;
    std::array<std::atomic<float>, bsl::padCount> padPeaks_ {};
    std::array<std::atomic<float>, 2> outputPeaks_ {};
    bsl::AudioBlock scratch_;
    juce::AudioProcessorValueTreeState parameters_;
    std::array<std::array<std::atomic<float>*, 4>, bsl::padCount> padBypassParameters_ {};
    std::array<std::atomic<float>, bsl::padCount> pendingTriggers_ {};
    juce::MidiBuffer incomingMidi_;
    std::array<bsl::SequencerBatchEvent, maximumSequencerEvents> sequencerEvents_ {};
    bsl::PatternTriggerCollector patternTriggerCollector_;
    std::array<bsl::PatternRenderSegment,
               bsl::PatternScheduler::maximumPendingTriggers + 1> renderSegments_ {};
    std::array<bsl::ProjectState, 3> stateSnapshots_;
    std::atomic<int> publishedStateIndex_ { 0 };
    std::atomic<int> readingStateIndex_ { -1 };
    std::atomic<std::size_t> audioPattern_ { 0 };
    std::atomic<int> requestedPatternForUi_ { -1 };
    std::atomic<std::uint64_t> explicitRevision_ { 1 };
    std::atomic<std::uint64_t> restoredStateRevision_ { 0 };
    std::mutex statePublishMutex_;
    std::uint64_t lastPublishedEditorRevision_ = 0;
    std::uint64_t lastPublishedExplicitRevision_ = 0;
    std::size_t lastPublishedActivePattern_ = 0;
    std::atomic<std::int64_t> fallbackSamplePosition_ { 0 };
    std::atomic<bool> standalonePlaying_ { true };
    std::atomic<bool> transportPlaying_ { false };
    // Audio-thread-only pending MIDI gates, relative to the next callback.
    std::array<std::int64_t, 128> pendingMidiOffFrames_;
    std::int64_t midiExpectedSample_ = 0;
    double midiExpectedPpq_ = std::numeric_limits<double>::quiet_NaN();
    bool midiWasPlaying_ = false;
    bsl::PatternScheduler patternScheduler_;
    bsl::AudioThreadResetRequest resetRequest_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatScholarLinuxProcessor)
};
