#include "plugin/PluginProcessor.h"
#include "plugin/DataPaths.h"
#include "plugin/PluginEditor.h"
#include "plugin/StateCodec.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int totalOutputChannels = 34;
constexpr std::array<const char*, 4> padBypassNames { "Lpf", "Hpf", "Drive", "Crush" };
constexpr std::array<const char*, 4> padBypassLabels { "LPF", "HPF", "Drive", "Crush" };

juce::String padBypassId(std::size_t pad, std::size_t effect)
{
    return "pad" + juce::String(static_cast<int>(pad + 1)) + padBypassNames[effect] + "Bypass";
}

std::optional<bsl::SampleData> decodeSampleBytes(const void* data, std::size_t size)
{
    if (data == nullptr || size == 0) return std::nullopt;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto input = std::make_unique<juce::MemoryInputStream>(data, size, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(input)));
    if (reader == nullptr || !BeatScholarLinuxProcessor::supportsDecodedSample(reader->lengthInSamples, reader->numChannels)
        || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0)
        return std::nullopt;
    const auto length = static_cast<std::size_t>(reader->lengthInSamples);
    bsl::SampleData sample;
    sample.sampleRate = reader->sampleRate;
    sample.channels.resize(std::min<unsigned int>(2, reader->numChannels));
    juce::AudioBuffer<float> decoded(static_cast<int>(sample.channels.size()),
                                     static_cast<int>(length));
    if (!reader->read(&decoded, 0, static_cast<int>(length), 0, true, true))
        return std::nullopt;
    for (int channel = 0; channel < decoded.getNumChannels(); ++channel)
        for (int frame = 0; frame < decoded.getNumSamples(); ++frame)
            if (!std::isfinite(decoded.getSample(channel, frame))) return std::nullopt;
    for (std::size_t channel = 0; channel < sample.channels.size(); ++channel)
        sample.channels[channel].assign(decoded.getReadPointer(static_cast<int>(channel)),
                                        decoded.getReadPointer(static_cast<int>(channel)) + length);
    return sample;
}

bool validEmbeddedAudio(const bsl::KitState& kit)
{
    for (const auto& pad : kit.pads)
    {
        const auto& encoded = pad.customSampleBase64.value();
        if (encoded.empty()) continue;
        juce::MemoryOutputStream bytes;
        if (!juce::Base64::convertFromBase64(bytes, juce::String(encoded))
            || !decodeSampleBytes(bytes.getData(), bytes.getDataSize())) return false;
    }
    return true;
}

bool readableStateFile(const juce::File& file)
{
    return file.existsAsFile() && file.getSize() > 0
        && file.getSize() <= static_cast<juce::int64>(bsl::statecodec::maximumSerializedBytes);
}

juce::String legacyPresetKitFile(const juce::String& json)
{
    const auto sampler = juce::JSON::parse(json).getProperty("Sampler", {});
    const auto id = sampler.getProperty("id", {}).toString();
    const auto name = sampler.getProperty("name", {}).toString();
    if (id.isEmpty() && name.isEmpty()) return {};
    juce::StringArray idMatches, nameMatches;
    for (const auto& file : bsl::dataRoot().getChildFile("Factory/Kits")
                               .findChildFiles(juce::File::findFiles, true, "*.json"))
    {
        if (!readableStateFile(file)) continue;
        const auto kit = juce::JSON::parse(file);
        const auto pads = kit.getProperty("Pads", {}).getProperty("Pads", {});
        if (!kit.isObject() || !pads.isArray() || pads.size() == 0) continue;
        if (id.isNotEmpty() && kit.getProperty("id", {}).toString() == id)
            idMatches.add(file.getFullPathName());
        if (name.isNotEmpty() && file.getFileNameWithoutExtension() == name)
            nameMatches.add(file.getFullPathName());
    }
    if (idMatches.size() == 1) return idMatches[0];
    if (idMatches.size() > 1) return {};
    return nameMatches.size() == 1 ? nameMatches[0] : juce::String {};
}

} // namespace

juce::AudioProcessor::BusesProperties BeatScholarLinuxProcessor::makeBuses()
{
    BusesProperties result;
    result = result.withOutput("Main", juce::AudioChannelSet::stereo(), true);
    for (int pad = 0; pad < static_cast<int>(bsl::padCount); ++pad)
        result = result.withOutput("Pad " + juce::String(pad + 1), juce::AudioChannelSet::stereo(), false);
    return result;
}

juce::AudioProcessorValueTreeState::ParameterLayout BeatScholarLinuxProcessor::makeParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "swing", 1 }, "Swing", juce::NormalisableRange<float> { -200.0f, 200.0f, 0.1f }, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "bpm", 1 }, "Standalone BPM", juce::NormalisableRange<float> { 20.0f, 300.0f, 0.01f }, 120.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "routing", 1 }, "Routing", juce::StringArray { "Stereo", "Multi" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "masterGain", 1 }, "Master Gain", juce::NormalisableRange<float> { 0.0f, 1.5f, 0.0001f }, 1.0f));
    // Append the documented host-only switches so existing parameter indices
    // and IDs retain their meaning in saved DAW projects.
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
        for (std::size_t effect = 0; effect < padBypassNames.size(); ++effect)
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID { padBypassId(pad, effect), 1 },
                "Pad " + juce::String(static_cast<int>(pad + 1)) + " "
                    + padBypassLabels[effect] + " Bypass", false));
    return layout;
}

BeatScholarLinuxProcessor::BeatScholarLinuxProcessor()
    : AudioProcessor(makeBuses()),
      parameters_(*this, nullptr, "BeatScholarLinuxState", makeParameterLayout())
{
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
        for (std::size_t effect = 0; effect < padBypassNames.size(); ++effect)
            padBypassParameters_[pad][effect] = parameters_.getRawParameterValue(padBypassId(pad, effect));
    // Defaults belong only to a new instance. Audio re-preparation must never
    // replace an intentionally empty or partially populated restored kit.
    loadFactoryKitIfAvailable();
    syncParametersFromProject();
    pendingMidiOffFrames_.fill(-1);
    for (auto& snapshot : stateSnapshots_)
        snapshot = projectEditor_.state();
    const auto& state = projectEditor_.state();
    lastPublishedActivePattern_ = state.patterns[state.activePattern].runtimeId;
    audioPattern_.store(lastPublishedActivePattern_);
}

std::optional<juce::String> BeatScholarLinuxProcessor::getNameForMidiNoteNumber(int note, int midiChannel)
{
    if (note < 0 || note > 127 || midiChannel < 1 || midiChannel > 16) return std::nullopt;
    // Host metadata queries use the published project. Holding the publication
    // mutex prevents its slot being reused while the string is copied; the
    // audio callback never takes this mutex.
    const std::lock_guard lock(statePublishMutex_);
    const auto& state = stateSnapshots_[static_cast<std::size_t>(publishedStateIndex_.load(std::memory_order_acquire))];
    if (const auto pad = state.padIndexForMidiNote(static_cast<std::uint8_t>(note)))
        return juce::String(state.kit.pads[*pad].name);
    return std::nullopt;
}

int BeatScholarLinuxProcessor::takeRequestedPatternForUi() noexcept
{
    const auto id = requestedPatternForUi_.exchange(0);
    if (pendingUiPattern_.load(std::memory_order_acquire) != 0) return -1;
    const auto& patterns = projectEditor_.state().patterns;
    const auto found = std::find_if(patterns.begin(), patterns.end(), [id](const auto& pattern) {
        return pattern.runtimeId == id;
    });
    return found == patterns.end() ? -1 : static_cast<int>(std::distance(patterns.begin(), found));
}

void BeatScholarLinuxProcessor::selectPatternFromUi(std::size_t index)
{
    auto& state = projectEditor_.state();
    if (index >= state.patterns.size()) return;
    state.activePattern = index;
    markProjectDirty();
    publishProjectStateIfNeeded();
    requestedPatternForUi_.store(0, std::memory_order_release);
    // A direct click cancels a queued switch, even when the model already has
    // this selection and the audio thread is ahead of the display timer.
    pendingUiPattern_.store(state.patterns[index].runtimeId, std::memory_order_release);
}

void BeatScholarLinuxProcessor::publishProjectStateIfNeeded()
{
    const std::lock_guard lock(statePublishMutex_);
    const auto editorRevision = projectEditor_.revision();
    const auto explicitRevision = explicitRevision_.load(std::memory_order_relaxed);
    if (editorRevision == lastPublishedEditorRevision_
        && explicitRevision == lastPublishedExplicitRevision_)
        return;
    const int published = publishedStateIndex_.load(std::memory_order_acquire);
    const int reading = readingStateIndex_.load(std::memory_order_acquire);
    int target = 0;
    while (target == published || target == reading)
        ++target;
    stateSnapshots_[static_cast<std::size_t>(target)] = projectEditor_.state();
    const auto& snapshot = stateSnapshots_[static_cast<std::size_t>(target)];
    const auto activePattern = snapshot.patterns[snapshot.activePattern].runtimeId;
    publishedStateIndex_.store(target, std::memory_order_release);
    if (activePattern != lastPublishedActivePattern_)
    {
        audioPattern_.store(activePattern, std::memory_order_release);
        lastPublishedActivePattern_ = activePattern;
    }
    lastPublishedEditorRevision_ = editorRevision;
    lastPublishedExplicitRevision_ = explicitRevision;
}

bool BeatScholarLinuxProcessor::restoreProjectHistory(bool redo)
{
    struct SampleIdentity
    {
        std::array<std::string, 2> factory;
        std::string customHash;
        bool hasCustom = false;
    };
    if (redo ? !projectEditor_.canRedo() : !projectEditor_.canUndo()) return false;
    std::array<SampleIdentity, bsl::padCount> samples;
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        const auto& sound = projectEditor_.state().kit.pads[pad];
        samples[pad] = { sound.factorySampleIds, sound.customSampleSha256, !sound.customSampleBase64.empty() };
    }
    if (!(redo ? projectEditor_.redo() : projectEditor_.undo())) return false;
    const auto& state = projectEditor_.state();
    const auto parameters = projectEditor_.restoredParameters();
    if (parameters == bsl::HistoryParameters::masterGain || parameters == bsl::HistoryParameters::kit)
        if (auto* master = parameters_.getParameter("masterGain"))
            master->setValueNotifyingHost(master->convertTo0to1(state.master.gain));
    std::array<bool, bsl::padCount> changedSamples {};
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        const auto& sound = state.kit.pads[pad];
        const std::array bypass { sound.lpfBypass, sound.hpfBypass, sound.driveBypass, sound.crushBypass };
        for (std::size_t effect = 0; effect < bypass.size(); ++effect)
            if (parameters == bsl::HistoryParameters::kit)
                parameters_.getParameter(padBypassId(pad, effect))->setValueNotifyingHost(bypass[effect] ? 1.0f : 0.0f);
        changedSamples[pad] = samples[pad].factory != sound.factorySampleIds
            || samples[pad].customHash != sound.customSampleSha256
            || samples[pad].hasCustom != !sound.customSampleBase64.empty();
    }
    if (std::any_of(changedSamples.begin(), changedSamples.end(), [](bool changed) { return changed; }))
    {
        suspendProcessing(true);
        for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
            if (changedSamples[pad])
            {
                loadBuiltInPreviewKit(pad);
                loadFactorySamples(pad);
            }
        suspendProcessing(false);
    }
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

void BeatScholarLinuxProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto maximumBlockSize = static_cast<std::size_t>(std::max(1, maximumExpectedSamplesPerBlock));
    ambience_.prepareFromFactory(bsl::dataRoot().getChildFile("Factory/ImpulseResponses"),
                                  sampleRate, maximumBlockSize);
    audioEngine_.setAmbienceProcessor(&ambience_);
    audioEngine_.prepare(sampleRate, maximumBlockSize);
    const auto driveResponse = bsl::AmbienceConvolution::loadDriveResponse(
        bsl::dataRoot().getChildFile("Factory/ImpulseResponses"), sampleRate);
    std::array<std::vector<float>, 2> driveChannels;
    if (driveResponse.getNumChannels() == 2)
        for (int channel = 0; channel < 2; ++channel)
            driveChannels[static_cast<std::size_t>(channel)].assign(
                driveResponse.getReadPointer(channel),
                driveResponse.getReadPointer(channel) + driveResponse.getNumSamples());
    audioEngine_.setDistortionResponse(std::move(driveChannels));
    scratch_.assign(totalOutputChannels,
                    std::vector<float>(static_cast<std::size_t>(std::max(1, maximumExpectedSamplesPerBlock))));
    for (auto& peak : padPeaks_) peak.store(0.0f, std::memory_order_relaxed);
    for (auto& peak : outputPeaks_) peak.store(0.0f, std::memory_order_relaxed);
    pendingMidiOffFrames_.fill(-1);
    midiWasPlaying_ = false;
    samplerWasEnabled_ = true;
    fallbackSamplePosition_.store(0);
    patternScheduler_.reset(audioPattern_.load(std::memory_order_acquire));
    loadBuiltInPreviewKit();
    loadFactorySamples();
    markProjectDirty();
    publishProjectStateIfNeeded();
}

void BeatScholarLinuxProcessor::releaseResources()
{
    resetRequest_.request();
}

bool BeatScholarLinuxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
    {
        const auto set = layouts.getChannelSet(false, bus);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void BeatScholarLinuxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int samplesInBlock = buffer.getNumSamples();
    if (samplesInBlock <= 0 || scratch_.empty()
        || static_cast<std::size_t>(samplesInBlock) > scratch_.front().size())
    {
        buffer.clear();
        return;
    }

    const bool resetRequested = resetRequest_.consume();
    if (resetRequested)
    {
        audioEngine_.reset();
        fallbackSamplePosition_.store(0, std::memory_order_release);
        patternScheduler_.reset(audioPattern_.load(std::memory_order_acquire));
    }

    int snapshotIndex = 0;
    do
    {
        snapshotIndex = publishedStateIndex_.load(std::memory_order_acquire);
        readingStateIndex_.store(snapshotIndex, std::memory_order_release);
    } while (snapshotIndex != publishedStateIndex_.load(std::memory_order_acquire));
    const auto& state = stateSnapshots_[static_cast<std::size_t>(snapshotIndex)];
    // Disabling the sampler ends its voices and tails. Pausing its renderer
    // alone would replay stale audio on enable, including notes released while off.
    if (samplerWasEnabled_ && !state.global.samplerEnabled && !resetRequested)
        audioEngine_.reset();
    samplerWasEnabled_ = state.global.samplerEnabled;
    const auto findPattern = [&](bsl::PatternId id) {
        return std::find_if(state.patterns.begin(), state.patterns.end(), [id](const auto& pattern) {
            return pattern.runtimeId == id;
        });
    };
    auto uiPattern = pendingUiPattern_.load(std::memory_order_acquire);
    const bool directSelection = uiPattern != 0 && findPattern(uiPattern) != state.patterns.end();
    auto requestedAudioPattern = directSelection ? uiPattern : audioPattern_.load(std::memory_order_acquire);
    if (findPattern(requestedAudioPattern) == state.patterns.end())
        requestedAudioPattern = state.patterns[state.activePattern].runtimeId;
    if (directSelection || requestedAudioPattern != patternScheduler_.activePattern())
        patternScheduler_.reset(requestedAudioPattern);
    if (directSelection)
    {
        audioPattern_.store(requestedAudioPattern, std::memory_order_release);
        requestedPatternForUi_.store(0, std::memory_order_release);
        // A newer, different click arriving during this callback stays pending.
        pendingUiPattern_.compare_exchange_strong(uiPattern, 0, std::memory_order_acq_rel);
    }
    else if (uiPattern != 0 && snapshotIndex == publishedStateIndex_.load(std::memory_order_acquire))
    {
        // The clicked pattern may have been removed or replaced before audio
        // saw it. Keep a request only while this snapshot predates publication.
        pendingUiPattern_.compare_exchange_strong(uiPattern, 0, std::memory_order_acq_rel);
    }
    // Cancel before planning: skipping a deleted target's render segment would
    // otherwise leave a silent gap and make its reused list position playable.
    patternScheduler_.removeMissingPatterns(state.patterns);

    const bool standalone = wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    // The standalone wrapper enables only the main stereo bus.
    const auto routing = !standalone && parameters_.getRawParameterValue("routing")->load() >= 0.5f
                             ? bsl::RoutingMode::multi : bsl::RoutingMode::stereo;
    audioEngine_.setRoutingMode(routing);

    patternTriggerCollector_.clear();
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const auto note = static_cast<std::uint8_t>(message.getNoteNumber());
            const auto found = std::find_if(state.patterns.begin(), state.patterns.end(),
                                            [note](const auto& pattern) {
                                                return pattern.triggerNote == note;
                                            });
            if (found != state.patterns.end())
            {
                patternTriggerCollector_.push({
                    std::clamp(metadata.samplePosition, 0, samplesInBlock),
                    found->runtimeId,
                    found->quantization
                });
            }
        }
    }

    // This instrument's MIDI output is its sequencer stream, not MIDI thru.
    // Input still triggers pads/patterns above, but sharing its notes with
    // generated gates would give two owners to the same channel/pitch lifetime.
    incomingMidi_.swapWith(midi);
    midi.clear();

    bsl::TransportFrame frame;
    frame.sampleRate = getSampleRate();
    frame.bpm = parameters_.getRawParameterValue("bpm")->load();
    frame.absoluteSampleStart = fallbackSamplePosition_.load();
    frame.blockSize = samplesInBlock;
    // Undefined is the unwrapped processor used by offline tools/tests.
    // Hosted wrappers require affirmative transport information from the host.
    frame.playing = (standalone || wrapperType == juce::AudioProcessor::wrapperType_Undefined)
        && standalonePlaying_.load();
    frame.swingPercent = parameters_.getRawParameterValue("swing")->load();
    // AudioProcessorPlayer supplies device time, not musical transport.
    // Its clock keeps advancing while paused and its isPlaying stays false.
    if (!standalone)
        if (const auto position = getPlayHead() != nullptr ? getPlayHead()->getPosition() : std::nullopt)
        {
            frame.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm())
                frame.bpm = *bpm;
            if (const auto sample = position->getTimeInSamples())
                frame.absoluteSampleStart = *sample;
            if (const auto ppq = position->getPpqPosition())
                frame.quarterNotePosition = *ppq;
        }
    effectiveBpm_.store(frame.bpm, std::memory_order_relaxed);
    transportPlaying_.store(frame.playing, std::memory_order_release);
    const bool midiPlaying = frame.playing && state.global.sequencerEnabled;
    const bool midiDiscontinuity = midiWasPlaying_
        && (frame.absoluteSampleStart != midiExpectedSample_
            || (std::isfinite(frame.quarterNotePosition) && std::isfinite(midiExpectedPpq_)
                && std::abs(frame.quarterNotePosition - midiExpectedPpq_) > 1.0e-4));
    if (resetRequested || !midiPlaying || midiDiscontinuity)
        for (std::size_t note = 0; note < pendingMidiOffFrames_.size(); ++note)
            if (pendingMidiOffFrames_[note] >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, static_cast<int>(note)), 0);
                pendingMidiOffFrames_[note] = -1;
            }
    midiWasPlaying_ = midiPlaying;
    midiExpectedSample_ = frame.absoluteSampleStart + samplesInBlock;
    midiExpectedPpq_ = frame.quarterNotePosition
        + static_cast<double>(samplesInBlock) * frame.bpm / (frame.sampleRate * 60.0);


    const bsl::SchedulerTransport schedulerTransport {
        frame.sampleRate,
        frame.bpm,
        frame.absoluteSampleStart,
        frame.blockSize,
        frame.playing,
        frame.quarterNotePosition
    };
    const auto segmentCount = patternScheduler_.planBlock(
        schedulerTransport,
        patternTriggerCollector_.triggers(),
        renderSegments_);
    const auto scheduledPattern = patternScheduler_.activePattern();
    if (scheduledPattern != audioPattern_.load(std::memory_order_acquire))
    {
        audioPattern_.store(scheduledPattern, std::memory_order_release);
        requestedPatternForUi_.store(scheduledPattern, std::memory_order_release);
    }

    for (std::size_t pad = 0; pad < state.kit.pads.size(); ++pad)
    {
        const auto& sound = state.kit.pads[pad];
        auto settings = bsl::makePadPlaybackSettings(sound);
        const auto& bypass = padBypassParameters_[pad];
        // Read cached atomics directly: no string construction, model mutation,
        // or allocation on the audio thread. Automation preserves knob values.
        settings.filterEnabled = sound.filterEnabled && bypass[0]->load() < 0.5f;
        settings.highPassEnabled = bypass[1]->load() < 0.5f;
        settings.driveEnabled = sound.driveEnabled && bypass[2]->load() < 0.5f;
        settings.crush = bypass[3]->load() >= 0.5f ? 0.0f : sound.crush;
        audioEngine_.setPadSettings(pad, settings);
    }
    auto masterSettings = bsl::makeMasterPlaybackSettings(
        state.master, parameters_.getRawParameterValue("masterGain")->load());
    masterSettings.distortionDrive = state.kit.pads[0].distortionDrive;
    audioEngine_.setMasterSettings(masterSettings);
    audioEngine_.setModuleEnables(state.global.auxEnabled, state.global.masterEnabled);
    audioEngine_.beginBlock(scratch_, static_cast<std::size_t>(samplesInBlock));
    std::size_t audioCursor = 0;
    const auto advanceAudio = [&](int offset) {
        const auto target = static_cast<std::size_t>(std::clamp(offset, 0, samplesInBlock));
        if (state.global.samplerEnabled && target > audioCursor)
            audioEngine_.renderRange(scratch_, audioCursor, target);
        audioCursor = target;
    };
    auto input = incomingMidi_.begin();
    const auto inputEnd = incomingMidi_.end();
    const auto consumeInputThrough = [&](int offset) {
        while (input != inputEnd && (*input).samplePosition <= offset)
        {
            const auto metadata = *input++;
            const auto message = metadata.getMessage();
            if (!state.global.samplerEnabled)
                continue;
            if (message.isAllSoundOff() || message.isAllNotesOff())
            {
                advanceAudio(metadata.samplePosition);
                audioEngine_.stopMidiChannel(static_cast<std::uint8_t>(message.getChannel()),
                                             message.isAllSoundOff());
                continue;
            }
            if (!message.isNoteOn() && !message.isNoteOff())
                continue;
            if (const auto pad = state.padIndexForMidiNote(static_cast<std::uint8_t>(message.getNoteNumber())))
            {
                advanceAudio(metadata.samplePosition);
                if (message.isNoteOn())
                    audioEngine_.noteOn(static_cast<std::uint8_t>(36 + *pad), message.getFloatVelocity(),
                                        0, true, static_cast<std::uint8_t>(message.getChannel()));
                else
                    audioEngine_.noteOff(static_cast<std::uint8_t>(36 + *pad),
                                         static_cast<std::uint8_t>(message.getChannel()));
            }
        }
    };
    int auditionStart1, auditionCount1, auditionStart2, auditionCount2;
    auditionFifo_.prepareToRead(auditionFifo_.getNumReady(),
                               auditionStart1, auditionCount1, auditionStart2, auditionCount2);
    const auto playAuditions = [&](int start, int count) {
        if (!state.global.samplerEnabled) return;
        for (int i = start; i < start + count; ++i)
        {
            const auto& audition = pendingTriggers_[static_cast<std::size_t>(i)];
            audioEngine_.noteOn(static_cast<std::uint8_t>(36 + audition.pad), audition.velocity, 0, false);
        }
    };
    playAuditions(auditionStart1, auditionCount1);
    playAuditions(auditionStart2, auditionCount2);
    auditionFifo_.finishedRead(auditionCount1 + auditionCount2);

    for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
    {
        const auto& segment = renderSegments_[segmentIndex];
        const auto pattern = findPattern(segment.patternId);
        if (pattern == state.patterns.end()) continue;
        const auto patternIndex = static_cast<std::size_t>(std::distance(state.patterns.begin(), pattern));
        auto segmentFrame = frame;
        segmentFrame.absoluteSampleStart += segment.sampleOffset;
        segmentFrame.blockSize = segment.sampleCount;
        segmentFrame.patternPhaseQuarterNotes = segment.phaseQuarterNotes;
        if (std::isfinite(segmentFrame.quarterNotePosition))
        {
            const double samplesPerQuarter = segmentFrame.sampleRate * 60.0 / segmentFrame.bpm;
            segmentFrame.quarterNotePosition += static_cast<double>(segment.sampleOffset) / samplesPerQuarter;
        }
        bsl::SequencerBatchCursor cursor;
        while (!cursor.exhausted)
        {
            const auto count = bsl::Sequencer::renderBatch(state, segmentFrame, patternIndex,
                                                          sequencerEvents_, cursor);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& event = sequencerEvents_[index].event;
                const int offset = event.sampleOffset + segment.sampleOffset;
                consumeInputThrough(offset);
                advanceAudio(offset);
                auto& pendingOff = pendingMidiOffFrames_[event.note];
                if (pendingOff >= 0)
                    midi.addEvent(juce::MidiMessage::noteOff(1, event.note),
                                  static_cast<int>(std::min<std::int64_t>(pendingOff, offset)));
                pendingOff = static_cast<std::int64_t>(offset)
                             + std::max<std::int64_t>(1, static_cast<std::int64_t>(event.gateFrames));
                midi.addEvent(juce::MidiMessage::noteOn(1, event.note, static_cast<juce::uint8>(event.velocity)), offset);
                if (state.global.samplerEnabled && event.padIndex < bsl::padCount)
                    audioEngine_.noteOn(static_cast<std::uint8_t>(36 + event.padIndex),
                                        static_cast<float>(event.velocity) / 127.0f, event.gateFrames);
            }
        }
    }
    consumeInputThrough(samplesInBlock);
    advanceAudio(samplesInBlock);

    for (std::size_t note = 0; note < pendingMidiOffFrames_.size(); ++note)
    {
        auto& pendingOff = pendingMidiOffFrames_[note];
        if (pendingOff < 0) continue;
        if (pendingOff < samplesInBlock)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, static_cast<int>(note)),
                          static_cast<int>(pendingOff));
            pendingOff = -1;
        }
        else
            pendingOff -= samplesInBlock;
    }

    const auto publishPeak = [](std::atomic<float>& meter, float peak) {
        if (!std::isfinite(peak)) return;
        auto previous = meter.load(std::memory_order_relaxed);
        while (previous < peak && !meter.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {}
    };
    if (state.global.samplerEnabled)
        for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
            publishPeak(padPeaks_[pad], audioEngine_.padPeaks()[pad]);
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        float peak = 0.0f;
        for (int frameIndex = 0; frameIndex < samplesInBlock; ++frameIndex)
            peak = std::max(peak, std::abs(scratch_[channel][static_cast<std::size_t>(frameIndex)]));
        publishPeak(outputPeaks_[channel], peak);
    }
    buffer.clear();
    // JUCE packs only enabled buses into the callback buffer. The engine's
    // scratch channels retain fixed logical bus positions, so copy by bus.
    for (int bus = 0; bus < getBusCount(false); ++bus)
        for (int channel = 0; channel < getChannelCountOfBus(false, bus); ++channel)
        {
            const int destination = getChannelIndexInProcessBlockBuffer(false, bus, channel);
            const int source = bus * 2 + channel;
            if (destination < buffer.getNumChannels() && source < totalOutputChannels)
                buffer.copyFrom(destination, 0, scratch_[static_cast<std::size_t>(source)].data(), samplesInBlock);
        }
    if (frame.playing)
        fallbackSamplePosition_.store(frame.absoluteSampleStart + samplesInBlock);
    readingStateIndex_.store(-1, std::memory_order_release);
}

void BeatScholarLinuxProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = projectEditor_.state();
    copyParameterValuesToState(state);
    destination = bsl::statecodec::encode(state);
}

void BeatScholarLinuxProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;
    auto restored = projectEditor_.state();
    if (!bsl::statecodec::decode(data, static_cast<std::size_t>(size), restored)
        || !validEmbeddedAudio(restored.kit))
        return;
    projectEditor_ = bsl::ProjectEditor(std::move(restored));
    syncParametersFromProject();
    suspendProcessing(true);
    loadBuiltInPreviewKit();
    loadFactorySamples();
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
}

void BeatScholarLinuxProcessor::copyParameterValuesToState(bsl::ProjectState& state) const
{
    state.global.swingPercent = parameters_.getRawParameterValue("swing")->load();
    state.global.beatsPerMinute = parameters_.getRawParameterValue("bpm")->load();
    state.global.multiOutput = wrapperType != juce::AudioProcessor::wrapperType_Standalone
        && parameters_.getRawParameterValue("routing")->load() >= 0.5f;
    state.master.gain = parameters_.getRawParameterValue("masterGain")->load();
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        auto& sound = state.kit.pads[pad];
        const auto& bypass = padBypassParameters_[pad];
        sound.lpfBypass = bypass[0]->load() >= 0.5f;
        sound.hpfBypass = bypass[1]->load() >= 0.5f;
        sound.driveBypass = bypass[2]->load() >= 0.5f;
        sound.crushBypass = bypass[3]->load() >= 0.5f;
    }
}

void BeatScholarLinuxProcessor::syncParametersFromProject()
{
    auto& state = projectEditor_.state();
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        state.global.multiOutput = false;
    if (auto* swing = parameters_.getParameter("swing"))
        swing->setValueNotifyingHost(swing->convertTo0to1(static_cast<float>(state.global.swingPercent)));
    if (auto* bpm = parameters_.getParameter("bpm"))
        bpm->setValueNotifyingHost(bpm->convertTo0to1(static_cast<float>(state.global.beatsPerMinute)));
    if (auto* routing = parameters_.getParameter("routing"))
        routing->setValueNotifyingHost(state.global.multiOutput ? 1.0f : 0.0f);
    if (auto* master = parameters_.getParameter("masterGain"))
        master->setValueNotifyingHost(master->convertTo0to1(state.master.gain));
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        const auto& sound = state.kit.pads[pad];
        const std::array bypass { sound.lpfBypass, sound.hpfBypass, sound.driveBypass, sound.crushBypass };
        for (std::size_t effect = 0; effect < bypass.size(); ++effect)
            parameters_.getParameter(padBypassId(pad, effect))
                ->setValueNotifyingHost(bypass[effect] ? 1.0f : 0.0f);
    }
}

void BeatScholarLinuxProcessor::triggerPad(std::size_t pad, float velocity) noexcept
{
    if (pad >= bsl::padCount || !std::isfinite(velocity) || velocity <= 0.0f) return;
    int start1, count1, start2, count2;
    auditionFifo_.prepareToWrite(1, start1, count1, start2, count2);
    const PadAudition audition { static_cast<std::uint8_t>(pad), std::min(velocity, 1.0f) };
    if (count1 > 0) pendingTriggers_[static_cast<std::size_t>(start1)] = audition;
    if (count2 > 0) pendingTriggers_[static_cast<std::size_t>(start2)] = audition;
    auditionFifo_.finishedWrite(count1 + count2);
}

void BeatScholarLinuxProcessor::stopStandalone() noexcept
{
    standalonePlaying_.store(false);
    resetRequest_.request();
}

bool BeatScholarLinuxProcessor::supportsDecodedSample(std::int64_t frames, unsigned int channels) noexcept
{
    if (frames <= 0 || channels == 0 || frames > std::numeric_limits<int>::max()) return false;
    const auto storedChannels = std::min(2u, channels);
    return static_cast<std::uint64_t>(frames)
        <= maximumDecodedSampleBytes / (sizeof(float) * storedChannels);
}

bool BeatScholarLinuxProcessor::installSampleBytes(std::size_t pad, const void* data, std::size_t size)
{
    if (pad >= bsl::padCount) return false;
    auto sample = decodeSampleBytes(data, size);
    if (!sample) return false;
    audioEngine_.setPadSample(pad, std::move(*sample));
    return true;
}

juce::Result BeatScholarLinuxProcessor::loadCustomSample(std::size_t pad, const juce::File& file)
{
    if (pad >= bsl::padCount || !file.existsAsFile() || file.getSize() <= 0)
        return juce::Result::fail("Could not read sample");
    if (file.getSize() > static_cast<juce::int64>(bsl::statecodec::maximumEmbeddedSampleBytes))
        return juce::Result::fail("Sample exceeds the 128 MiB project storage limit");
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes))
        return juce::Result::fail("Could not read sample");
    if (!bsl::statecodec::canReplaceEmbeddedSample(projectEditor_.state().kit, pad, bytes.getSize()))
        return juce::Result::fail("Custom sample storage limit reached (128 MiB per project)");
    suspendProcessing(true);
    const bool installed = installSampleBytes(pad, bytes.getData(), bytes.getSize());
    suspendProcessing(false);
    if (!installed)
        return juce::Result::fail("Could not decode sample audio (128 MiB decoded limit)");
    projectEditor_.checkpoint();
    auto& state = projectEditor_.state().kit.pads[pad];
    state.customSampleName = file.getFileName().toStdString();
    state.customSampleSha256 = juce::SHA256(bytes).toHexString().toStdString();
    state.customSampleBase64 = juce::Base64::toBase64(bytes.getData(), bytes.getSize()).toStdString();
    markProjectDirty();
    publishProjectStateIfNeeded();
    return juce::Result::ok();
}

bool BeatScholarLinuxProcessor::setFactorySample(std::size_t pad, std::size_t layer,
                                                  const juce::String& sampleId)
{
    if (pad >= bsl::padCount || layer >= 2 || (layer == 1 && pad > 1)
        || sampleId.isEmpty() || juce::File(sampleId).getFileName() != sampleId)
        return false;
    const auto file = bsl::dataRoot().getChildFile("Factory/Samples")
                          .getChildFile(sampleId);
    if (!file.existsAsFile())
        return false;
    projectEditor_.checkpoint();
    auto& state = projectEditor_.state().kit.pads[pad];
    state.customSampleName.clear();
    state.customSampleSha256.clear();
    state.customSampleBase64.clear();
    state.factorySampleIds[layer] = sampleId.toStdString();
    if (pad > 1)
        state.factorySampleIds[1].clear();
    suspendProcessing(true);
    loadBuiltInPreviewKit(pad);
    loadFactorySamples(pad);
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
    return true;
}

void BeatScholarLinuxProcessor::clearCustomSample(std::size_t pad)
{
    if (pad >= bsl::padCount)
        return;
    projectEditor_.checkpoint();
    auto& state = projectEditor_.state().kit.pads[pad];
    state.customSampleName.clear();
    state.customSampleSha256.clear();
    state.customSampleBase64.clear();
    suspendProcessing(true);
    loadBuiltInPreviewKit(pad);
    loadFactorySamples(pad);
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
}

bool BeatScholarLinuxProcessor::loadPresetFile(const juce::File& file)
{
    if (!readableStateFile(file))
        return false;
    auto restored = projectEditor_.state();
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes))
        return false;
    const bool hostEnvelope = bytes.getSize() >= 4
                              && std::memcmp(bytes.getData(), "BSLX", 4) == 0;
    const auto json = hostEnvelope ? juce::String {}
        : juce::String::fromUTF8(static_cast<const char*>(bytes.getData()), static_cast<int>(bytes.getSize()));
    const bool loaded = hostEnvelope
        ? bsl::statecodec::decode(bytes.getData(), bytes.getSize(), restored)
        : (file.hasFileExtension("bslpreset")
               ? bsl::statecodec::decodePresetJson(json, restored)
               : bsl::statecodec::importLegacyPresetJson(json, restored));
    if (!loaded)
        return false;
    // Resolve the displayed kit identity without reloading or replacing the
    // sampler settings embedded in this preset. Native state keeps its path.
    if (!hostEnvelope && !file.hasFileExtension("bslpreset"))
        restored.ui.kitFile = legacyPresetKitFile(json).toStdString();
    const bool preserveKit = projectEditor_.state().global.kitLocked;
    auto current = projectEditor_.state();
    copyParameterValuesToState(current);
    const auto preservedKit = current.kit;
    if (preserveKit)
    {
        restored.kit = preservedKit;
        restored.ui.kitFile = current.ui.kitFile;
        restored.global.kitLocked = true;
    }
    if (!validEmbeddedAudio(restored.kit)) return false;
    restored.ui.presetFile = file.getFullPathName().toStdString();
    projectEditor_ = bsl::ProjectEditor(std::move(restored));
    syncParametersFromProject();
    suspendProcessing(true);
    loadBuiltInPreviewKit();
    loadFactorySamples();
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

bool BeatScholarLinuxProcessor::savePresetFile(const juce::File& file)
{
    auto state = projectEditor_.state();
    copyParameterValuesToState(state);
    state.ui.presetFile = file.getFullPathName().toStdString();
    const auto json = bsl::statecodec::encodePresetJson(state);
    if (json.isEmpty() || !file.replaceWithText(json)) return false;
    projectEditor_.state().ui.presetFile = state.ui.presetFile;
    markProjectDirty();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

bool BeatScholarLinuxProcessor::loadKitFile(const juce::File& file)
{
    if (!readableStateFile(file))
        return false;
    auto kit = projectEditor_.state().kit;
    auto master = projectEditor_.state().master;
    const auto json = file.loadFileAsString();
    const bool loaded = file.hasFileExtension("bslkit")
                            ? bsl::statecodec::decodeKitJson(json, kit, master)
                            : bsl::statecodec::importLegacyKitJson(json, kit, master);
    if (!loaded || !validEmbeddedAudio(kit))
        return false;
    // Capture the audible host-automated values before replacing the kit.
    // This also prevents a kit load from resetting unrelated global automation.
    copyParameterValuesToState(projectEditor_.state());
    projectEditor_.checkpoint(bsl::HistoryParameters::kit);
    projectEditor_.state().kit = std::move(kit);
    projectEditor_.state().master = master;
    projectEditor_.state().ui.kitFile = file.getFullPathName().toStdString();
    syncParametersFromProject();
    suspendProcessing(true);
    loadBuiltInPreviewKit();
    loadFactorySamples();
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

bool BeatScholarLinuxProcessor::saveKitFile(const juce::File& file)
{
    auto state = projectEditor_.state();
    copyParameterValuesToState(state);
    const auto json = bsl::statecodec::encodeKitJson(state.kit, state.master);
    if (json.isEmpty() || !file.replaceWithText(json)) return false;
    projectEditor_.state().ui.kitFile = file.getFullPathName().toStdString();
    markProjectDirty();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

void BeatScholarLinuxProcessor::resetPresetToInit()
{
    auto current = projectEditor_.state();
    copyParameterValuesToState(current);
    auto initialized = bsl::ProjectState::makeMainInit();

    // Display preferences are application choices, not preset content.
    initialized.ui.selectedPad = current.ui.selectedPad;
    initialized.ui.darkMode = current.ui.darkMode;
    initialized.ui.midiNoteNames = current.ui.midiNoteNames;
    initialized.ui.midiFlats = current.ui.midiFlats;
    initialized.ui.middleCOctave = current.ui.middleCOctave;
    if (current.global.kitLocked)
    {
        initialized.kit = std::move(current.kit);
        initialized.ui.kitFile = std::move(current.ui.kitFile);
        initialized.global.kitLocked = true;
    }

    // Main preset loads replace the undo domain; Init follows the same scope.
    projectEditor_ = bsl::ProjectEditor(std::move(initialized));
    syncParametersFromProject();
    suspendProcessing(true);
    loadBuiltInPreviewKit();
    loadFactorySamples();
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
}

void BeatScholarLinuxProcessor::resetKitToInit()
{
    auto initialized = bsl::ProjectState::makeDefault();
    copyParameterValuesToState(projectEditor_.state());
    projectEditor_.checkpoint(bsl::HistoryParameters::kit);
    projectEditor_.state().kit = std::move(initialized.kit);
    projectEditor_.state().master = initialized.master;
    projectEditor_.state().ui.kitFile.clear();
    syncParametersFromProject();
    suspendProcessing(true);
    loadBuiltInPreviewKit();
    loadFactorySamples();
    suspendProcessing(false);
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
}

void BeatScholarLinuxProcessor::resetRandomizerToInit()
{
    projectEditor_.checkpoint();
    projectEditor_.state().randomizer = bsl::RandomizerSettings::makeInitPreset();
    projectEditor_.state().ui.randomizerFile.clear();
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
}

bool BeatScholarLinuxProcessor::loadRandomizerFile(const juce::File& file)
{
    if (!readableStateFile(file))
        return false;
    auto settings = projectEditor_.state().randomizer;
    const auto json = file.loadFileAsString();
    const bool loaded = file.hasFileExtension("bslrandom")
                            ? bsl::statecodec::decodeRandomizerJson(json, settings)
                            : bsl::statecodec::importLegacyRandomizerJson(json, settings);
    if (!loaded)
        return false;
    projectEditor_.checkpoint();
    projectEditor_.state().randomizer = std::move(settings);
    projectEditor_.state().ui.randomizerFile = file.getFullPathName().toStdString();
    markProjectDirty();
    publishProjectStateIfNeeded();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

bool BeatScholarLinuxProcessor::saveRandomizerFile(const juce::File& file)
{
    const auto json = bsl::statecodec::encodeRandomizerJson(projectEditor_.state().randomizer);
    if (json.isEmpty() || !file.replaceWithText(json)) return false;
    projectEditor_.state().ui.randomizerFile = file.getFullPathName().toStdString();
    markProjectDirty();
    restoredStateRevision_.fetch_add(1, std::memory_order_release);
    return true;
}

void BeatScholarLinuxProcessor::loadFactoryKitIfAvailable()
{
    const auto& firstPad = projectEditor_.state().kit.pads.front();
    if (!firstPad.factorySampleIds.front().empty() || !firstPad.customSampleBase64.empty())
        return;
    const auto kitFile = bsl::dataRoot().getChildFile("Factory/Kits/Acoustic Power/Acoustic Power.json");
    if (!readableStateFile(kitFile))
        return;
    auto kit = projectEditor_.state().kit;
    auto master = projectEditor_.state().master;
    if (bsl::statecodec::importLegacyKitJson(kitFile.loadFileAsString(), kit, master)
        && validEmbeddedAudio(kit))
    {
        projectEditor_.state().kit = std::move(kit);
        projectEditor_.state().master = master;
    }
}

void BeatScholarLinuxProcessor::loadFactorySamples(std::optional<std::size_t> onlyPad)
{
    const auto samples = bsl::dataRoot().getChildFile("Factory/Samples");
    for (std::size_t pad = 0; pad < projectEditor_.state().kit.pads.size(); ++pad)
    {
        if (onlyPad && pad != *onlyPad) continue;
        const auto& state = projectEditor_.state().kit.pads[pad];
        if (!state.customSampleBase64.empty())
            continue;
        for (std::size_t layer = 0; layer < state.factorySampleIds.size(); ++layer)
        {
            const auto& id = state.factorySampleIds[layer];
            if (id.empty())
                continue;
            juce::MemoryBlock bytes;
            const auto file = samples.getChildFile(juce::String(id));
            if (file.getSize() > static_cast<juce::int64>(bsl::statecodec::maximumEmbeddedSampleBytes)
                || !file.loadFileAsData(bytes))
                continue;
            auto sample = decodeSampleBytes(bytes.getData(), bytes.getSize());
            if (sample) audioEngine_.setPadLayerSample(pad, layer, std::move(*sample));
        }
    }
}

void BeatScholarLinuxProcessor::loadBuiltInPreviewKit(std::optional<std::size_t> onlyPad)
{
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto length = static_cast<std::size_t>(rate * 0.22);
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
    {
        if (onlyPad && pad != *onlyPad) continue;
        bsl::SampleData sample;
        sample.sampleRate = rate;
        sample.channels.resize(1);
        sample.channels.front().resize(length);
        const double frequency = 48.0 + static_cast<double>(pad) * 28.0;
        for (std::size_t frame = 0; frame < length; ++frame)
        {
            const double time = static_cast<double>(frame) / rate;
            const double envelope = std::exp(-time * (14.0 + static_cast<double>(pad) * 0.45));
            sample.channels.front()[frame] = static_cast<float>(std::sin(time * frequency * juce::MathConstants<double>::twoPi)
                                                                * envelope * 0.55);
        }
        audioEngine_.setPadSample(pad, std::move(sample));
    }

    for (std::size_t pad = 0; pad < projectEditor_.state().kit.pads.size(); ++pad)
    {
        if (onlyPad && pad != *onlyPad) continue;
        const auto& encoded = projectEditor_.state().kit.pads[pad].customSampleBase64.value();
        if (encoded.empty())
            continue;
        juce::MemoryBlock bytes;
        juce::MemoryOutputStream decoded(bytes, false);
        if (juce::Base64::convertFromBase64(decoded, juce::String(encoded)))
        {
            decoded.flush();
            installSampleBytes(pad, bytes.getData(), bytes.getSize());
        }
    }
}

juce::AudioProcessorEditor* BeatScholarLinuxProcessor::createEditor()
{
    return new BeatScholarLinuxEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BeatScholarLinuxProcessor();
}
