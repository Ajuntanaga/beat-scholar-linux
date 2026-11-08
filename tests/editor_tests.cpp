#include "plugin/PluginEditor.h"
#include "plugin/StateCodec.h"
#include "plugin/DataPaths.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <map>

// Provide deterministic convolution history without shipping factory IRs to CI.
struct BeatScholarLinuxProcessorTestAccess
{
    static void prepareDistortion(BeatScholarLinuxProcessor& processor)
    {
        processor.audioEngine_.setDistortionResponse({ std::vector<float>{1.0f}, std::vector<float>{1.0f} });
    }

    static void prepareAmbience(BeatScholarLinuxProcessor& processor)
    {
        juce::AudioBuffer<float> room(2, 4097), plate(2, 8193);
        room.clear();
        plate.clear();
        for (int channel = 0; channel < 2; ++channel)
        {
            room.setSample(channel, 4096, 1.0f);
            plate.setSample(channel, 8192, 1.0f);
        }
        processor.ambience_.prepare(std::move(room), std::move(plate), 48000, 48000, 64);
    }
};

namespace {
int failures = 0;
void check(bool condition, const juce::String& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// These fixtures compare steady-state sample identity, routing or voice levels.
// Let the native-style main gain ramp settle before sending their first note;
// startup/retarget timing itself is covered without warmup in audio_tests.cpp.
void settleMainGain(BeatScholarLinuxProcessor& processor, int blockSize)
{
    int remaining = static_cast<int>(std::ceil(processor.getSampleRate() * 0.1));
    while (remaining > 0)
    {
        const int frames = std::min(remaining, blockSize);
        juce::AudioBuffer<float> audio(processor.getTotalNumOutputChannels(), frames);
        audio.clear();
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
        remaining -= frames;
    }
}

bool similar(juce::Colour a, juce::Colour b)
{
    return std::abs(int(a.getRed()) - int(b.getRed())) <= 8
        && std::abs(int(a.getGreen()) - int(b.getGreen())) <= 8
        && std::abs(int(a.getBlue()) - int(b.getBlue())) <= 8;
}

void testSectors(const juce::File& output)
{
    // Exercise actual editor painting and mouse handling, with distinct slice
    // identities anchored to six o'clock, clockwise, as shown in Modalics'
    // Basic Beat Making & Editing tutorial. Two slices start on the left half.
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns.front().lanes = { bsl::LaneState::make(4) };
    state.ui.selectedPad = 15;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    const auto centre = juce::Point<float>(62.0f + 1070.0f / 8.0f, 306.0f);
    constexpr float radius = (1070.0f / 4.0f) * 0.36f;
    const std::array colours { 0xffbf316bU, 0xff4058bdU, 0xff2eb574U, 0xffd51e42U,
        0xffb9983aU, 0xffa95c35U, 0xff55349dU, 0xff4e8d2dU, 0xff8a2b2dU };
    for (std::size_t count = 1; count <= bsl::maximumSubdivision; ++count)
    {
        auto& beat = state.patterns.front().lanes.front().beats.front();
        beat = bsl::BeatState::withSubdivision(count, 0, 1.0f);
        for (std::size_t i = 0; i < count; ++i)
            beat.slices[i].padIndex = static_cast<std::uint8_t>(i % colours.size());
        const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
        auto stream = output.getChildFile("slices-" + juce::String(int(count)) + ".png").createOutputStream();
        if (stream != nullptr) { stream->setPosition(0); stream->truncate(); }
        check(stream != nullptr && juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
              "write editor screenshot");
        for (std::size_t i = 0; i < count; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float(i) + 0.5f) / float(count);
            const auto expected = juce::Colour(colours[i % colours.size()])
                                      .withSaturation(0.62f).withBrightness(0.72f);
            for (const float distance : { 0.35f, 0.80f })
            {
                // At 45 slices use the outer point, safely clear of antialiased edges.
                if (count > 9 && distance < 0.5f) continue;
                const auto point = centre + juce::Point<float>(-std::sin(angle), std::cos(angle))
                                              * (radius * distance);
                check(similar(snapshot.getPixelAt(juce::roundToInt(point.x), juce::roundToInt(point.y)), expected),
                      "radial fill/clockwise identity: " + juce::String(int(count)) + "/" + juce::String(int(i)));
            }
        }
        // A click in each visual sector must change that sector exactly once.
        for (std::size_t i = 0; i < count; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float(i) + 0.5f) / float(count);
            const auto point = centre + juce::Point<float>(-std::sin(angle), std::cos(angle)) * (radius * 0.8f);
            const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                editor.get(), editor.get(), juce::Time::getCurrentTime(), point,
                juce::Time::getCurrentTime(), 1, false);
            editor->mouseDown(click);
            editor->mouseUp(click);
            const auto& changed = state.patterns.front().lanes.front().beats.front().slices;
            check(changed[i].enabled && changed[i].padIndex == 15, "click changes the visible sector");
            for (std::size_t j = i + 1; j < count; ++j)
                check(changed[j].padIndex == j % colours.size(), "click leaves other sectors alone");
        }
    }
}

void testStateRefreshPreservesSoundControls(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& initial = processor->projectState();
    initial.ui.selectedPad = 0;
    initial.kit.pads[0].gain = 0.20f;
    initial.kit.pads[0].pan = -0.80f;
    initial.kit.pads[0].oneShot = true;
    initial.kit.pads[0].pitchSemitones = -4.0f;
    initial.master.gain = 0.70f;

    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);
    editor->setVisible(true);

    // This mimics a host restoring BSLX state while the plug-in editor is already open.
    auto restored = processor->projectState();
    restored.ui.selectedPad = 0;
    restored.kit.pads[0].gain = 1.60f;
    restored.kit.pads[0].pan = 0.65f;
    restored.kit.pads[0].oneShot = false;
    restored.kit.pads[0].pitchSemitones = 5.25f;
    restored.kit.pads[0].cutoff = 0.73f;
    restored.master.gain = 1.25f;
    const auto encoded = bsl::statecodec::encode(restored);
    processor->setStateInformation(encoded.getData(), static_cast<int>(encoded.getSize()));

    // These coordinates target the real controls after the editor's 1280x720 layout.
    auto* volume = dynamic_cast<juce::Slider*>(editor->getComponentAt(1011, 650));
    auto* pan = dynamic_cast<juce::Slider*>(editor->getComponentAt(1011, 590));
    auto* pitch = dynamic_cast<juce::Slider*>(editor->getComponentAt(639, 605));
    auto* cutoff = dynamic_cast<juce::Slider*>(editor->getComponentAt(759, 605));
    auto* master = dynamic_cast<juce::Slider*>(editor->getComponentAt(1219, 635));
    auto* oneShot = dynamic_cast<juce::TextButton*>(editor->getComponentAt(470, 652));
    check(volume != nullptr && pan != nullptr && pitch != nullptr && cutoff != nullptr
              && master != nullptr && oneShot != nullptr,
          "state-refresh test locates visible sound controls");
    if (volume == nullptr || pan == nullptr || pitch == nullptr || cutoff == nullptr
        || master == nullptr || oneShot == nullptr)
        return;

    // The first edit is deliberately immediate: no editor timer callback has
    // had a chance to run since restoration.  A state-revision implementation
    // must synchronize before this write, rather than letting the stale panel
    // clobber the restored values.
    cutoff->setValue(0.42, juce::sendNotificationSync);
    {
        const auto& pad = processor->projectState().kit.pads[0];
        const auto& masterState = processor->projectState().master;
        check(std::abs(pad.cutoff - 0.42f) < 1.0e-6f,
              "an immediate FX edit reaches project state after restore");
        check(std::abs(pad.gain - 1.60f) < 1.0e-6f && std::abs(pad.pan - 0.65f) < 1.0e-6f
                  && !pad.oneShot && std::abs(pad.pitchSemitones - 5.25f) < 1.0e-6f,
              "an immediate FX edit preserves restored volume, pan, One Shot, and pitch");
        check(std::abs(masterState.gain - 1.25f) < 1.0e-6f,
              "an immediate FX edit preserves restored master gain");
    }

    // Restore again without touching a control: the periodic refresh must also
    // update a visible editor when the user does nothing.
    auto secondRestore = processor->projectState();
    secondRestore.kit.pads[0].gain = 1.10f;
    const auto secondEncoded = bsl::statecodec::encode(secondRestore);
    processor->setStateInformation(secondEncoded.getData(), static_cast<int>(secondEncoded.getSize()));

    // Pump pending timers until the refresh arrives. A single fixed sleep
    // leaves JUCE's timer thread waiting for an earlier undelivered message.
    // Keep the later keyboard test as the sole owner of the dispatch loop.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    do
    {
        juce::Timer::callPendingTimersSynchronously();
        if (std::abs(volume->getValue() - 1.10) < 1.0e-6) break;
        juce::Thread::sleep(10);
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    check(std::abs(volume->getValue() - 1.10) < 1.0e-6,
          "restored volume refreshes in an open editor");
    check(std::abs(pan->getValue() - 0.65) < 1.0e-6,
          "restored pan refreshes in an open editor");
    check(std::abs(pitch->getValue() - 5.0) < 1.0e-6,
          "restored pitch refreshes in an open editor");
    check(!oneShot->getToggleState(),
          "restored One Shot state refreshes in an open editor");
    check(std::abs(master->getValue() - 1.25) < 1.0e-6,
          "restored master value refreshes in an open editor");

    // The earlier one-control edit must remain intact after the timed UI
    // refresh, with no restored parameter regressing to stale UI state.
    const auto& pad = processor->projectState().kit.pads[0];
    const auto& masterState = processor->projectState().master;
    check(std::abs(pad.cutoff - 0.42f) < 1.0e-6f,
          "the selected effect change reaches project state");
    check(std::abs(pad.gain - 1.10f) < 1.0e-6f && std::abs(pad.pan - 0.65f) < 1.0e-6f
              && !pad.oneShot && std::abs(pad.pitchSemitones - 5.25f) < 1.0e-6f,
          "an FX edit preserves restored volume, pan, One Shot, and pitch");
    check(std::abs(masterState.gain - 1.25f) < 1.0e-6f,
          "an FX edit preserves restored master gain");
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    auto stream = output.getChildFile("restored-controls.png").createOutputStream();
    if (stream != nullptr) { stream->setPosition(0); stream->truncate(); }
    check(stream != nullptr && juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
          "write restored-control screenshot");
}


void testSoundControlEditsPreserveOtherValues()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& pad = processor->projectState().kit.pads[0];
    pad.attackSeconds = 0.0f;
    pad.attackNormalized = 0.3754321f;
    pad.damp = 0.234567f;
    pad.pitchSemitones = 5.25f;
    pad.fineCents = 17.23f;
    pad.highPass = 0.987654f;
    pad.cutoff = 0.753219f;
    const auto before = pad;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* level = dynamic_cast<juce::Slider*>(editor->findChildWithID("sound.5"));
    auto* pitch = dynamic_cast<juce::Slider*>(editor->findChildWithID("sound.2"));
    check(level != nullptr && pitch != nullptr, "find sound controls for independent editing");
    if (level == nullptr || pitch == nullptr) return;
    check(pitch->getInterval() == 1.0, "coarse Pitch uses whole semitone steps");
    level->setValue(1.25, juce::sendNotificationSync);
    check(pad.gain == 1.25f, "Level edit reaches the selected pad");
    check(pad.attackSeconds == before.attackSeconds && pad.attackNormalized == before.attackNormalized,
          "Level edit preserves the restored attack representation");
    check(pad.damp == before.damp && pad.pitchSemitones == before.pitchSemitones
              && pad.fineCents == before.fineCents,
          "Level edit leaves unrelated sound values at their saved precision");
    check(pad.highPass == before.highPass && pad.cutoff == before.cutoff,
          "Level edit leaves unrelated effect values at their saved precision");
    pitch->setValue(3.4, juce::sendNotificationSync);
    check(pad.pitchSemitones == 3.0f && pad.fineCents == before.fineCents,
          "Pitch edit selects an integer semitone without changing Fine");
}

void appendLe16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendLe32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    appendLe16(bytes, static_cast<std::uint16_t>(value & 0xffffU));
    appendLe16(bytes, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

// One channel of constant signed PCM16, with no filesystem or factory data.
std::vector<std::uint8_t> constantPcm16Wav(int sampleRate, int frames)
{
    const auto dataBytes = static_cast<std::uint32_t>(frames * 2);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(44 + dataBytes);
    bytes.insert(bytes.end(), { 'R', 'I', 'F', 'F' });
    appendLe32(bytes, 36U + dataBytes);
    bytes.insert(bytes.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    appendLe32(bytes, 16);
    appendLe16(bytes, 1); // PCM
    appendLe16(bytes, 1); // mono
    appendLe32(bytes, static_cast<std::uint32_t>(sampleRate));
    appendLe32(bytes, static_cast<std::uint32_t>(sampleRate * 2));
    appendLe16(bytes, 2);
    appendLe16(bytes, 16);
    bytes.insert(bytes.end(), { 'd', 'a', 't', 'a' });
    appendLe32(bytes, dataBytes);
    for (int frame = 0; frame < frames; ++frame)
        appendLe16(bytes, 16384); // +0.5, intentionally non-decaying
    return bytes;
}

std::vector<float> renderSequencedGateCase(int sampleRate, int blockSize, bool oneShot)
{
    // A 4/4 pattern has just one enabled one-slice beat at time zero.  Its
    // documented gate is one beat = 60 / 120 seconds; the pattern cannot
    // retrigger until the four-beat bar begins again at 2 seconds.
    constexpr double bpm = 120.0;
    const auto gateFrames = static_cast<int>(sampleRate * 60.0 / bpm);
    auto project = bsl::ProjectState::makeDefault();
    project.global.beatsPerMinute = bpm;
    project.patterns.front().bars = { bsl::BarState::make(4, 4) };
    project.patterns.front().lanes = { bsl::LaneState::make(4) };
    for (auto& beat : project.patterns.front().lanes.front().beats)
    {
        beat = bsl::BeatState::withSubdivision(1, 0, 1.0f);
        beat.slices.front().enabled = false;
    }
    project.patterns.front().lanes.front().beats.front().slices.front().enabled = true;

    // The source is two beats long, so One Shot must remain audible after the
    // gate while the non-One-Shot case must be silent at it.
    const auto wav = constantPcm16Wav(sampleRate, gateFrames * 2);
    const juce::MemoryBlock wavBytes(wav.data(), wav.size());
    auto& pad = project.kit.pads.front();
    pad.factorySampleIds = {};
    pad.customSampleName = "sequenced-gate.wav";
    pad.customSampleSha256 = juce::SHA256(wavBytes).toHexString().toStdString();
    pad.customSampleBase64 = juce::Base64::toBase64(wavBytes.getData(), wavBytes.getSize()).toStdString();
    pad.oneShot = oneShot;
    pad.hpfBypass = true; // Keep this DC fixture audible while isolating the note gate.
    const auto state = bsl::statecodec::encode(project);
    check(state.getSize() > 0, "synthetic sequenced-gate state encodes");

    BeatScholarLinuxProcessor processor;
    processor.setRateAndBufferSizeDetails(sampleRate, blockSize);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    processor.prepareToPlay(sampleRate, blockSize);
    processor.playStandalone();

    // Render exactly gate + 2 frames.  A partial last buffer is deliberate:
    // it detects block-size-specific scheduling rather than rounding duration
    // up to the host block.
    std::vector<float> output;
    output.reserve(static_cast<std::size_t>(gateFrames + 2));
    juce::MidiBuffer midi;
    for (int rendered = 0; rendered < gateFrames + 2;)
    {
        const int frames = std::min(blockSize, gateFrames + 2 - rendered);
        juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), frames);
        midi.clear();
        processor.processBlock(buffer, midi);
        for (int frame = 0; frame < frames; ++frame)
            output.push_back(buffer.getSample(0, frame));
        rendered += frames;
    }
    processor.releaseResources();
    return output;
}

float renderAuditionEnergy(std::initializer_list<float> velocities)
{
    auto project = bsl::ProjectState::makeDefault();
    const auto wav = constantPcm16Wav(48000, 1024);
    const juce::MemoryBlock bytes(wav.data(), wav.size());
    auto& pad = project.kit.pads.front();
    pad.factorySampleIds = {};
    pad.customSampleName = "audition.wav";
    pad.customSampleSha256 = juce::SHA256(bytes).toHexString().toStdString();
    pad.customSampleBase64 = juce::Base64::toBase64(bytes.getData(), bytes.getSize()).toStdString();
    pad.oneShot = true;
    const auto encoded = bsl::statecodec::encode(project);
    BeatScholarLinuxProcessor processor;
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.setStateInformation(encoded.getData(), static_cast<int>(encoded.getSize()));
    processor.prepareToPlay(48000, 256);
    processor.stopStandalone();
    settleMainGain(processor, 256);
    for (const auto velocity : velocities) processor.triggerPad(0, velocity);
    juce::AudioBuffer<float> audio(processor.getTotalNumOutputChannels(), 256);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    float energy = 0.0f;
    for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        energy += std::abs(audio.getSample(0, frame));
    processor.releaseResources();
    return energy;
}

void testHostNoteOffKeepsMidiChannelsIndependent()
{
    // The same pad can be played simultaneously by tracks on different MIDI
    // channels. Releases must follow their input channel even after pad remap.
    for (const int note : { 36, 72 })
    {
        auto project = bsl::ProjectState::makeDefault();
        project.global.sequencerEnabled = false;
        const auto wav = constantPcm16Wav(48000, 4096);
        const juce::MemoryBlock bytes(wav.data(), wav.size());
        auto& pad = project.kit.pads.front();
        pad.factorySampleIds = {};
        pad.customSampleName = "channel-gate.wav";
        pad.customSampleSha256 = juce::SHA256(bytes).toHexString().toStdString();
        pad.customSampleBase64 = juce::Base64::toBase64(bytes.getData(), bytes.getSize()).toStdString();
        pad.midiNote = static_cast<std::uint8_t>(note);
        pad.oneShot = false;
        pad.hpfBypass = true;
        pad.lpfBypass = true;
        const auto state = bsl::statecodec::encode(project);
        BeatScholarLinuxProcessor processor;
        processor.setRateAndBufferSizeDetails(48000, 16);
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        processor.prepareToPlay(48000, 16);
        settleMainGain(processor, 16);
        juce::AudioBuffer<float> audio(processor.getTotalNumOutputChannels(), 16);
        const auto render = [&](std::initializer_list<juce::MidiMessage> messages) {
            juce::MidiBuffer midi;
            for (const auto& message : messages) midi.addEvent(message, 0);
            processor.processBlock(audio, midi);
            return audio.getSample(0, 8);
        };
        const auto all = render({ juce::MidiMessage::noteOn(1, note, juce::uint8(16)),
                                  juce::MidiMessage::noteOn(2, note, juce::uint8(32)),
                                  juce::MidiMessage::noteOn(16, note, juce::uint8(48)) });
        check(all > 0.05f, "three MIDI channels trigger the configured pad");
        check(std::abs(render({ juce::MidiMessage::noteOff(3, note) }) - all) < 1e-6f,
              "unmatched MIDI channel cannot release another channel's held pad");
        check(std::abs(render({ juce::MidiMessage::noteOff(2, note) }) - all * (64.0f / 96.0f)) < 1e-6f,
              "MIDI channel 2 releases its own voice despite channel 1 starting first");
        check(std::abs(render({ juce::MidiMessage::noteOff(16, note) }) - all * (16.0f / 96.0f)) < 1e-6f,
              "MIDI channel 16 releases its own voice while channel 1 keeps playing");
        check(std::abs(render({ juce::MidiMessage::noteOff(1, note) })) < 1e-6f,
              "final matching MIDI release stops the remaining held pad voice");
        processor.releaseResources();
    }
}

void testPadAuditionsPreserveRepeatedHits()
{
    const auto single = renderAuditionEnergy({1.0f});
    const auto repeated = renderAuditionEnergy({0.25f, 1.0f});
    check(single > 1.0f, "synthetic pad audition is audible");
    check(std::abs(repeated / single - 1.25f) < 0.001f,
          "two auditions before one callback retain both hits and velocities");
    check(std::abs(renderAuditionEnergy({1.0f, 0.0f}) - single) < 0.001f,
          "zero-velocity audition does not discard a pending hit");
    check(std::abs(renderAuditionEnergy({1.0f, std::numeric_limits<float>::quiet_NaN()}) - single) < 0.001f,
          "invalid audition velocity does not discard a pending hit");
}

void testSequencedOneShotGateUsesBeatDurationAcrossRatesAndBlocks()
{
    constexpr std::array rates { 44100, 48000, 96000 };
    constexpr std::array blockSizes { 17, 64, 511 };
    for (const auto sampleRate : rates)
    for (const auto blockSize : blockSizes)
    {
        const auto gate = static_cast<std::size_t>(sampleRate / 2); // 120 BPM
        const auto gated = renderSequencedGateCase(sampleRate, blockSize, false);
        const auto oneShot = renderSequencedGateCase(sampleRate, blockSize, true);
        check(std::abs(gated[gate - 1]) > 0.1f,
              "sequenced non-One-Shot renders until the final beat frame");
        check(std::abs(gated[gate]) < 1.0e-6f && std::abs(gated[gate + 1]) < 1.0e-6f,
              "sequenced non-One-Shot stops exactly at the beat gate");
        check(std::abs(oneShot[gate]) > 0.1f && std::abs(oneShot[gate + 1]) > 0.1f,
              "sequenced One Shot continues beyond the beat gate");
    }
}

juce::MouseEvent visualEvent(BeatScholarLinuxEditor& editor, juce::Point<float> point,
                             juce::ModifierKeys modifiers = juce::ModifierKeys::leftButtonModifier)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), point, modifiers,
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor, &editor,
             juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), 1, false };
}

void saveVisual(juce::Component& editor, const juce::File& output, const char* name)
{
    const auto snapshot = editor.createComponentSnapshot(editor.getLocalBounds());
    auto stream = output.getChildFile(name).createOutputStream();
    if (stream != nullptr) { stream->setPosition(0); stream->truncate(); }
    check(stream != nullptr && juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
          "write public-feature screenshot");
}

// These commands and their order are visible at 5:14 and 8:50 in Modalics'
// Basic Beat Making & Editing tutorial. Run in the existing isolated GUI loop.
class TutorialArrangementMenuTests final : private juce::Timer
{
public:
    explicit TutorialArrangementMenuTests(const juce::File& output) : output_(output)
    {
        processor_.projectEditor().addPattern();
        auto& state = processor_.projectState();
        auto& source = state.patterns[0];
        source.name = "Source";
        source.bars = {{3, 8}};
        source.lanes = {bsl::LaneState::make(3)};
        source.lanes[0].name = "Source lane";
        source.lanes[0].beats[0] = bsl::BeatState::withSubdivision(5, 2, 0.7f);
        source.lanes[0].beats[0].slices[2].enabled = false;
        source.lanes[0].beats[2] = bsl::BeatState::withSubdivision(2, 7, 0.3f);
        auto& destination = state.patterns[1];
        destination.name = "Destination";
        destination.bars = {{4, 4}, {2, 4}};
        destination.lanes = {bsl::LaneState::make(6), bsl::LaneState::make(6)};
        for (std::size_t lane = 0; lane < 2; ++lane)
        {
            destination.lanes[lane].name = "Destination lane " + std::to_string(lane);
            destination.lanes[lane].muted = lane == 1;
            for (std::size_t beat = 0; beat < 6; ++beat)
                destination.lanes[lane].beats[beat] = bsl::BeatState::withSubdivision(
                    beat < 4 ? 3 : 7, static_cast<std::uint8_t>(lane + beat), 0.4f);
        }
        editor_ = std::make_unique<BeatScholarLinuxEditor>(processor_);
        editor_->setSize(1280, 720);
        window_.setContentNonOwned(editor_.get(), true);
    }

    ~TutorialArrangementMenuTests() override { window_.clearContentComponent(); }

    void start(std::function<void()> done)
    {
        done_ = std::move(done);
        window_.setVisible(true);
        window_.toFront(true);
        startTimer(75);
    }

private:
    struct Step { const char* label; bool pattern; std::size_t target; const char* image; };
    const std::array<Step, 13> steps_ {{
        {"Copy", false, 0, "tutorial-bar-menu.png"},
        {"Paste", false, 0, "tutorial-bar-paste-menu.png"},
        {"Clear", false, 0, nullptr},
        {"Duplicate", false, 0, nullptr},
        {"Delete", false, 0, nullptr},
        {"Insert Here", false, 1, nullptr},
        {"Append", false, 0, nullptr},
        {"Copy", true, 0, "tutorial-pattern-menu.png"},
        {"Paste", true, 1, "tutorial-pattern-paste-menu.png"},
        {"Duplicate", true, 0, nullptr},
        {"Insert", true, 1, nullptr},
        {"Delete", true, 0, nullptr},
        {"Trigger note up", true, 1, nullptr}
    }};

    static juce::AccessibilityHandler* findItem(juce::Component& component, const juce::String& title)
    {
        if (auto* handler = component.getAccessibilityHandler();
            handler != nullptr && handler->getTitle() == title)
            return handler;
        for (auto* child : component.getChildren())
            if (auto* item = findItem(*child, title)) return item;
        return nullptr;
    }

    void checkResult()
    {
        const auto& step = steps_[index_];
        const auto label = juce::String(step.pattern ? "pattern " : "bar ") + step.label;
        const auto& state = processor_.projectState();
        auto expected = before_;
        auto& destination = expected.patterns[1];
        if (index_ == 1)
        {
            destination.bars = {{3, 8}, {2, 4}};
            const auto tail = std::vector<bsl::BeatState>(destination.lanes[0].beats.begin() + 4,
                                                        destination.lanes[0].beats.end());
            destination.lanes[0].beats = expected.patterns[0].lanes[0].beats;
            destination.lanes[0].beats.insert(destination.lanes[0].beats.end(), tail.begin(), tail.end());
            auto& extra = destination.lanes[1].beats;
            extra.erase(extra.begin() + 3);
            for (std::size_t beat = 0; beat < 3; ++beat) extra[beat].reset();
        }
        else if (index_ == 2)
            for (auto& lane : destination.lanes)
                for (std::size_t beat = 0; beat < 4; ++beat) lane.beats[beat].reset();
        else if (index_ == 3)
        {
            destination.bars = {{4, 4}, {4, 4}, {2, 4}};
            for (auto& lane : destination.lanes)
            {
                const std::vector<bsl::BeatState> first(lane.beats.begin(), lane.beats.begin() + 4);
                lane.beats.insert(lane.beats.begin() + 4, first.begin(), first.end());
            }
        }
        else if (index_ == 4)
        {
            destination.bars = {{2, 4}};
            for (auto& lane : destination.lanes) lane.beats.erase(lane.beats.begin(), lane.beats.begin() + 4);
        }
        else if (index_ == 5 || index_ == 6)
        {
            // The manual does not define placement/default meter: Insert Here
            // uses the selected position; Append uses the end; both start 4/4.
            destination.bars = index_ == 5 ? std::vector<bsl::BarState>{{4, 4}, {4, 4}, {2, 4}}
                                           : std::vector<bsl::BarState>{{4, 4}, {2, 4}, {4, 4}};
            for (std::size_t lane = 0; lane < destination.lanes.size(); ++lane)
            {
                auto empty = bsl::LaneState::make(1).beats[0];
                empty.slices[0].padIndex = static_cast<std::uint8_t>(lane);
                auto& beats = destination.lanes[lane].beats;
                beats.insert(beats.begin() + (index_ == 5 ? 4 : 6), 4, empty);
            }
        }
        else if (index_ == 8)
        {
            destination.bars = expected.patterns[0].bars;
            destination.lanes = expected.patterns[0].lanes;
            destination.quantization = expected.patterns[0].quantization;
            check(state.patterns[1].runtimeId == before_.patterns[1].runtimeId,
                  label + " preserves destination identity");
        }
        else if (index_ == 9 || index_ == 10)
        {
            check(state.patterns.size() == 3, label + " adds one pattern");
            if (state.patterns.size() != 3) return;
            auto inserted = index_ == 9 ? expected.patterns[0] : bsl::ProjectState::makeDefault().patterns[0];
            inserted.name = index_ == 9 ? "Source Copy" : "Pattern 3";
            inserted.triggerNote = 14;
            expected.patterns.insert(expected.patterns.begin() + 1, inserted);
            expected.activePattern = 1;
            check(state.patterns[1].runtimeId != 0
                      && state.patterns[1].runtimeId != before_.patterns[0].runtimeId
                      && state.patterns[1].runtimeId != before_.patterns[1].runtimeId,
                  label + " assigns a new pattern identity");
            check(state.patterns[0].runtimeId == before_.patterns[0].runtimeId
                      && state.patterns[2].runtimeId == before_.patterns[1].runtimeId,
                  label + " preserves both neighboring pattern identities");
        }
        else if (index_ == 11)
        {
            expected.patterns.erase(expected.patterns.begin());
            expected.activePattern = 0;
            check(state.patterns[0].runtimeId == before_.patterns[1].runtimeId,
                  label + " preserves the surviving pattern identity");
        }
        else if (index_ == 12)
        {
            ++expected.patterns[1].triggerNote;
            checkTriggeredPlayback();
        }
        check(state.isValid(), label + " leaves a valid project");
        check(bsl::statecodec::encode(state) == bsl::statecodec::encode(expected),
              label + " edits the intended content and preserves neighboring music/settings");
    }

    void checkTriggeredPlayback()
    {
        struct PlayHead final : juce::AudioPlayHead
        {
            PositionInfo info;
            juce::Optional<PositionInfo> getPosition() const override { return info; }
        } clock;
        clock.info.setIsPlaying(true);
        clock.info.setBpm(120.0);
        processor_.setPlayHead(&clock);
        std::vector<std::pair<int, int>> notes;
        // The editor timer publishes the menu edit. Do not manually publish or
        // reload state here: that would hide a broken UI-to-audio handoff.
        for (int block = 0; block < 25; ++block)
        {
            clock.info.setTimeInSamples(block * 1024);
            clock.info.setPpqPosition(block * 1024.0 / 24000.0);
            juce::AudioBuffer<float> audio(2, 1024);
            juce::MidiBuffer midi;
            if (block == 0)
            {
                // Opening B's menu selects B. Trigger A exactly at the first
                // boundary, then request the newly assigned B note mid-beat.
                midi.addEvent(juce::MidiMessage::noteOn(1, 12, 0.8f), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, 14, 0.8f), 100);
            }
            processor_.processBlock(audio, midi);
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    notes.emplace_back(block * 1024 + event.samplePosition,
                                       event.getMessage().getNoteNumber());
        }
        check(notes == std::vector<std::pair<int, int>> {{0, 36}, {24000, 37}},
              "editor-assigned trigger switches at the quarter boundary with no leaked input");
        check(processor_.takeRequestedPatternForUi() == 1,
              "editor-assigned trigger reports the playing destination to the UI");
        processor_.setPlayHead(nullptr);
        processor_.releaseResources();
    }

    void timerCallback() override
    {
        const auto& step = steps_[index_];
        const auto label = juce::String(step.pattern ? "pattern " : "bar ") + step.label;
        if (phase_ == 0)
        {
            auto& state = processor_.projectState();
            state.ui.patternMode = step.pattern;
            state.activePattern = step.pattern ? step.target : index_ == 0 ? 0 : 1;
            if (index_ == 12)
            {
                state.activePattern = 0;
                state.global.samplerEnabled = false;
                for (std::size_t i = 0; i < state.patterns.size(); ++i)
                {
                    auto& pattern = state.patterns[i];
                    pattern.triggerNote = static_cast<std::uint8_t>(12 + i);
                    pattern.quantization = bsl::PatternQuantization::quarter;
                    pattern.bars = {{4, 4}};
                    pattern.lanes = {bsl::LaneState::make(4)};
                    for (auto& beat : pattern.lanes[0].beats)
                        beat = bsl::BeatState::withSubdivision(1, static_cast<std::uint8_t>(i), 0.8f);
                }
                processor_.markProjectDirty();
                processor_.publishProjectStateIfNeeded();
                processor_.setRateAndBufferSizeDetails(48000, 1024);
                processor_.prepareToPlay(48000, 1024);
            }
            const juce::Point<float> point = step.pattern
                ? juce::Point<float>{1205.0f, step.target == 0 ? 146.0f : 218.0f}
                : juce::Point<float>{step.target == 0 ? 180.0f : 900.0f, 80.0f};
            juce::Desktop::setMousePosition(editor_->localPointToGlobal(point.toInt()));
            editor_->mouseDown(visualEvent(*editor_, point, juce::ModifierKeys::rightButtonModifier));
            before_ = state;
            revision_ = processor_.projectEditor().revision();
            phase_ = 1;
        }
        else if (phase_ == 1)
        {
            auto* menu = juce::Component::getCurrentlyModalComponent();
            check(menu != nullptr, label + " context menu opens");
            auto* item = menu != nullptr ? findItem(*menu, step.label) : nullptr;
            check(item != nullptr, label + " is present under its tutorial label");
            if (menu != nullptr && step.image != nullptr) saveVisual(*menu, output_, step.image);
            invoked_ = item != nullptr && item->getActions().invoke(juce::AccessibilityActionType::press);
            check(invoked_, label + " can be selected");
            if (!invoked_) juce::PopupMenu::dismissAllActiveMenus();
            phase_ = 2;
        }
        else
        {
            if (invoked_)
            {
                checkResult();
                const bool copy = index_ == 0 || index_ == 7;
                check(processor_.projectEditor().revision() == revision_ + (copy ? 0 : 1),
                      label + (copy ? " does not edit the project" : " creates exactly one edit"));
                if (!copy)
                    check(processor_.projectEditor().undo()
                              && bsl::statecodec::encode(processor_.projectState()) == bsl::statecodec::encode(before_),
                          label + " is completely restored by one undo");
            }
            juce::PopupMenu::dismissAllActiveMenus();
            phase_ = 0;
            if (++index_ == steps_.size())
            {
                stopTimer();
                window_.setVisible(false);
                done_();
            }
        }
    }

    juce::File output_;
    BeatScholarLinuxProcessor processor_;
    std::unique_ptr<BeatScholarLinuxEditor> editor_;
    juce::DocumentWindow window_{"Tutorial arrangement menus", juce::Colours::black,
                                juce::DocumentWindow::closeButton};
    bsl::ProjectState before_;
    std::function<void()> done_;
    std::uint64_t revision_ = 0;
    std::size_t index_ = 0;
    int phase_ = 0;
    bool invoked_ = false;
};


void testMixedMeterGeometry(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    auto& pattern = state.patterns.front();
    pattern.bars = { bsl::BarState::make(2, 2), bsl::BarState::make(2, 4),
                     bsl::BarState::make(2, 8), bsl::BarState::make(2, 16) };
    pattern.lanes = { bsl::LaneState::make(8) };
    for (std::size_t beat = 0; beat < 8; ++beat)
        pattern.lanes.front().beats[beat] = bsl::BeatState::withSubdivision(1, static_cast<std::uint8_t>(beat), 1.0f);
    state.ui.selectedPad = 15;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    const std::array lengths { 2.0f, 2.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.25f, 0.25f };
    const std::array colours { 0xffbf316bU, 0xff4058bdU, 0xff2eb574U, 0xffd51e42U,
                              0xffb9983aU, 0xffa95c35U, 0xff55349dU, 0xff4e8d2dU };
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    saveVisual(*editor, output, "mixed-meters.png");
    float start = 0.0f;
    for (std::size_t beat = 0; beat < lengths.size(); ++beat)
    {
        const juce::Point<float> centre { 62.0f + (start + lengths[beat] * 0.5f) * 1070.0f / 7.5f, 306.0f };
        const auto expected = juce::Colour(colours[beat]).withSaturation(0.62f).withBrightness(0.72f);
        check(similar(snapshot.getPixelAt(juce::roundToInt(centre.x), juce::roundToInt(centre.y)), expected),
              "mixed-meter beat is drawn at its time-proportional centre");
        const float radius = std::min(1.0f, lengths[beat]) * 1070.0f / 7.5f * 0.36f;
        check(!similar(snapshot.getPixelAt(juce::roundToInt(centre.x + radius * 1.2f),
                                           juce::roundToInt(centre.y)), expected),
              "long beats retain quarter-note circle size as shown in the public visual");
        const auto click = visualEvent(*editor, centre);
        editor->mouseDown(click);
        editor->mouseUp(click);
        check(pattern.lanes.front().beats[beat].slices.front().padIndex == 15,
              "mixed-meter click changes the visually targeted beat");
        start += lengths[beat];
    }

    juce::MouseWheelDetails zoom;
    zoom.deltaY = std::log(2.0f) / 1.8f;
    editor->mouseWheelMove(visualEvent(*editor, { 100, 300 }, juce::ModifierKeys::altModifier), zoom);
    juce::MouseWheelDetails scroll;
    scroll.deltaX = -0.8f; // scroll exactly three quarter notes at 2x zoom
    editor->mouseWheelMove(visualEvent(*editor, { 100, 300 }, {}), scroll);
    const auto headerClick = visualEvent(*editor, { 400, 80 });
    editor->mouseDown(headerClick);
    editor->mouseUp(headerClick);
    check(state.ui.selectedBar == 1, "zoomed/scrolled header selects the bar beneath it");
    saveVisual(*editor, output, "mixed-meters-zoomed.png");
}

void testBarHeaderActions(const juce::File& output)
{
    for (const int width : { 960, 1280, 1600 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        processor->projectEditor().addBar(0, bsl::BarState::make(4, 4));
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(width, width * 9 / 16);
        const float scale = width / 1280.0f;
        const auto click = [&](float x) {
            const auto event = visualEvent(*editor, { x * scale, 80.0f * scale });
            editor->mouseDown(event);
            editor->mouseUp(event);
        };
        const auto barCount = [&] { return processor->projectState().patterns.front().bars.size(); };
        // Reference centers: 70% along each bar, then 33 design pixels apart.
        click(436);
        check(barCount() == 3, "bar-header duplicate follows tutorial placement at each window size");
        check(processor->projectEditor().undo() && barCount() == 2,
              "one Undo restores header duplication");
        check(processor->projectEditor().redo() && barCount() == 3,
              "one Redo restores header duplication");
        click(377);
        check(barCount() == 2, "bar-header remove follows resized bar after duplication");
        click(469);
        check(barCount() == 3, "bar-header add appends a bar at tutorial placement");
        click(410);
        check(barCount() == 3, "empty far-right header space no longer activates remove");
        if (width == 1280) saveVisual(*editor, output, "tutorial-bar-header-three.png");

        processor->projectEditor().addBar(0, bsl::BarState::make(4, 4));
        // Four bars at fit zoom: the tutorial hides all three controls.
        for (const float x : { 249.0f, 282.0f, 315.0f }) click(x);
        check(barCount() == 4, "zoomed-out hidden header actions cannot mutate bars");
        if (width == 1280) saveVisual(*editor, output, "tutorial-bar-header-hidden.png");

        juce::MouseWheelDetails zoom;
        zoom.deltaY = std::log(2.0f) / 1.8f;
        editor->mouseWheelMove(visualEvent(*editor, { 100.0f * scale, 300.0f * scale },
            juce::ModifierKeys(juce::ModifierKeys::altModifier)), zoom);
        juce::MouseWheelDetails scroll;
        scroll.deltaX = -0.75f; // 6 quarter notes: bar two is partially clipped.
        editor->mouseWheelMove(visualEvent(*editor, { 100.0f * scale, 300.0f * scale }, {}), scroll);
        if (width == 1280) saveVisual(*editor, output, "tutorial-bar-header-scrolled.png");
        click(168);
        check(barCount() == 5, "partially clipped header retains the visible duplicate target");
        check(processor->projectEditor().undo() && barCount() == 4,
              "one Undo restores the scrolled header action");
    }
}

void testCustomSampleAndMasterVisualStates(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& pad = processor->projectState().kit.pads[0];
    pad.customSampleName = "Speshel Kick.wav";
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    int visibleLayers = 0;
    for (auto* child : editor->getChildren())
        visibleLayers += child->isVisible() && child->getComponentID() == "sample.layer";
    check(visibleLayers == 0, "custom sample display hides both factory-layer selectors");
    auto* mix = editor->findChildWithID("sound.4");
    check(mix != nullptr && !mix->isVisible(), "custom sample display hides factory layer mixing");
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    const auto background = snapshot.getPixelAt(410, 700);
    check(background.getSaturation() < 0.75f && background.getBrightness() < 0.25f,
          "custom sample tile uses the manual's dark background");
    int greenPixels = 0;
    for (int y = 612; y < 704; ++y)
        for (int x = 1233; x < 1249; ++x)
        {
            const auto colour = snapshot.getPixelAt(x, y);
            greenPixels += colour.getGreen() > colour.getRed() * 1.4f
                        && colour.getGreen() > colour.getBlue() * 1.4f;
        }
    check(greenPixels == 0, "master fader does not display fabricated green audio levels at silence");
    saveVisual(*editor, output, "custom-sample.png");
}

template <typename Component>
Component* childWithId(juce::Component& owner, const char* id)
{
    return dynamic_cast<Component*>(owner.findChildWithID(id));
}


void testMonoButtonEditsOnlySelectedPadAndSupportsUndo(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);
    editor->setVisible(true); // JUCE hit testing requires a visible root component.
    auto* mono = childWithId<juce::TextButton>(*editor, "sample.mono");
    check(mono != nullptr, "sampler exposes the documented Mono control");
    if (mono == nullptr) return;
    check(mono->isVisible() && !mono->getToggleState(), "Mono starts disabled for older projects");
    for (const int width : { 960, 1280, 1920 })
    {
        editor->setSize(width, width * 9 / 16);
        const float scale = static_cast<float>(width) / 1280.0f;
        check(editor->getComponentAt(mono->getBounds().getCentre()) == mono,
              "Mono header button is reachable without another control covering it");
        for (const auto point : { juce::Point<float>(419, 539), juce::Point<float>(451, 539) })
        {
            auto* sibling = dynamic_cast<juce::TextButton*>(editor->getComponentAt((point * scale).roundToInt()));
            check(sibling != nullptr && sibling != mono
                      && sibling->getY() == mono->getY() && sibling->getHeight() == mono->getHeight()
                      && !sibling->getBounds().intersects(mono->getBounds()),
                  "Mono shares the M/S header row without covering either button");
        }
        for (const auto* child : editor->getChildren())
            if (child->getComponentID() == "sample.layer")
                check(!mono->getBounds().intersects(child->getBounds()),
                      "Mono header control leaves both sample selectors unobstructed");
    }
    editor->setSize(1280, 720);
    const auto click = visualEvent(*editor, mono->getBounds().getCentre().toFloat())
                           .getEventRelativeTo(mono);
    static_cast<juce::Component*>(mono)->mouseDown(click);
    static_cast<juce::Component*>(mono)->mouseUp(click);
    check(mono->getToggleState() && processor->projectState().kit.pads[0].mono
              && !processor->projectState().kit.pads[1].mono,
          "Mono click updates the selected pad only");
    saveVisual(*editor, output, "mono-enabled.png");
    check(processor->projectEditor().undo() && !processor->projectState().kit.pads[0].mono,
          "Mono is included in project undo");
}

void testRoutingLabelTogglesAndTracksHost(const juce::File& output)
{
    struct Listener final : juce::AudioProcessorParameter::Listener
    {
        std::vector<bool> gestures;
        void parameterValueChanged(int, float) override {}
        void parameterGestureChanged(int, bool starting) override { gestures.push_back(starting); }
    } listener;
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);
    auto* routing = childWithId<juce::TextButton>(*editor, "routing.mode");
    check(routing != nullptr, "routing uses the manual's single-click text toggle");
    if (routing == nullptr) return;
    auto* parameter = processor->parameters().getParameter("routing");
    parameter->addListener(&listener);
    check(routing->getButtonText() == "Stereo" && !routing->getToggleState(),
          "routing initially shows Stereo");
    const auto click = visualEvent(*editor, routing->getBounds().getCentre().toFloat());
    static_cast<juce::Component*>(routing)->mouseDown(click);
    static_cast<juce::Component*>(routing)->mouseUp(click);
    check(routing->getToggleState() && routing->getButtonText() == "Multi"
          && parameter->getValue() == 1.0f,
          "one routing click selects Multi and updates the host");
    check(listener.gestures == std::vector<bool>({true, false}),
          "routing click reports one complete host automation gesture");
    saveVisual(*editor, output, "routing-multi.png");
    listener.gestures.clear();
    const auto reset = visualEvent(*editor, routing->getBounds().getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    static_cast<juce::Component*>(routing)->mouseDown(reset);
    static_cast<juce::Component*>(routing)->mouseUp(reset);
    check(!routing->getToggleState() && routing->getButtonText() == "Stereo"
              && parameter->getValue() == 0.0f,
          "Alt-click resets routing to Stereo");
    check(listener.gestures == std::vector<bool>({true, false}),
          "routing reset reports exactly one complete host automation gesture");
    listener.gestures.clear();
    static_cast<juce::Component*>(routing)->mouseDown(reset);
    static_cast<juce::Component*>(routing)->mouseUp(reset);
    check(!routing->getToggleState() && parameter->getValue() == 0.0f
              && listener.gestures.empty(),
          "Alt-click at default routing is a no-op without a host gesture");
    parameter->setValueNotifyingHost(1.0f);
    check(routing->getToggleState() && routing->getButtonText() == "Multi",
          "host can restore Multi after a routing reset");
    parameter->setValueNotifyingHost(0.0f);
    check(!routing->getToggleState() && routing->getButtonText() == "Stereo",
          "host routing changes update the displayed label");
    check(listener.gestures.empty(), "host routing recall does not emit a touch gesture");
    saveVisual(*editor, output, "routing-stereo.png");
    parameter->removeListener(&listener);
}

void testRandomizerTabRestoresSoundPanel()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);

    auto* dice = childWithId<juce::TextButton>(*editor, "brush.dice");
    check(dice != nullptr, "randomizer test locates dice brush");
    if (dice == nullptr)
        return;
    dice->onClick();
    check(childWithId<juce::Slider>(*editor, "random.0") != nullptr,
          "dice brush exposes randomizer controls");

    editor->keyPressed(juce::KeyPress(juce::KeyPress::tabKey));
    auto* soundAfterFirstTab = childWithId<juce::Slider>(*editor, "sound.0");
    check(soundAfterFirstTab != nullptr && soundAfterFirstTab->isVisible(),
          "Tab from randomizer returns the sound panel with the drum brush");

    editor->keyPressed(juce::KeyPress(juce::KeyPress::tabKey));
    auto* randomAfterSecondTab = childWithId<juce::Slider>(*editor, "random.0");
    check(randomAfterSecondTab != nullptr && randomAfterSecondTab->isVisible(),
          "repeated Tab restores randomizer panel with the randomizer brush");
}

void testRandomizerTriggerModuleToggleIsIndependent(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& settings = processor->projectState().randomizer;
    settings.slicingEnabled = false;
    settings.noteSelectionEnabled = true;
    settings.triggeringEnabled = true;
    settings.velocityEnabled = false;
    settings.fillEmptyOnly = true;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);

    auto* dice = childWithId<juce::TextButton>(*editor, "brush.dice");
    check(dice != nullptr, "trigger-module test locates dice brush");
    if (dice == nullptr)
        return;
    dice->onClick();

    auto* triggerEnabled = childWithId<juce::TextButton>(*editor, "random.module.2");
    check(triggerEnabled != nullptr && triggerEnabled->isVisible(),
          "randomizer exposes a visible Step Triggering enable control");
    if (triggerEnabled == nullptr || !triggerEnabled->isVisible())
        return;

    triggerEnabled->onClick();
    check(!settings.triggeringEnabled, "Step Triggering control disables triggering");
    check(!settings.slicingEnabled && settings.noteSelectionEnabled
              && !settings.velocityEnabled && settings.fillEmptyOnly,
          "Step Triggering control preserves other randomizer submodules");

    triggerEnabled->onClick();
    check(settings.triggeringEnabled, "Step Triggering control re-enables triggering");
    check(!settings.slicingEnabled && settings.noteSelectionEnabled
              && !settings.velocityEnabled && settings.fillEmptyOnly,
          "Step Triggering re-enable preserves other randomizer submodules");
    saveVisual(*editor, output, "randomizer-modules.png");
}

void testRandomizerFillEmptyIsClickToggle(const juce::File& output)
{
    for (const int width : { 960, 1280 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(width, width * 9 / 16);
        editor->setVisible(true);
        childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
        const auto centre = juce::Point<float>(838.0f, 630.0f) * (static_cast<float>(width) / 1280.0f);
        auto* control = editor->getComponentAt(centre.toInt());
        check(control != nullptr && control != editor.get(), "Fill Empty has a visible control");
        if (control == nullptr || control == editor.get()) continue;
        const auto click = visualEvent(*editor, centre).getEventRelativeTo(control);
        control->mouseDown(click);
        control->mouseUp(click);
        check(processor->projectState().randomizer.fillEmptyOnly,
              "one click enables Fill Empty without dragging");
        check(processor->projectEditor().canUndo(), "Fill Empty click creates an undo checkpoint");
        if (width == 1280) saveVisual(*editor, output, "randomizer-fill-empty.png");
        childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
        check(!processor->projectState().randomizer.fillEmptyOnly, "undo restores Fill Empty off");
        childWithId<juce::TextButton>(*editor, "toolbar.redo")->onClick();
        check(processor->projectState().randomizer.fillEmptyOnly, "redo restores Fill Empty on");
        childWithId<juce::Slider>(*editor, "random.0")->setValue(0.7, juce::sendNotificationSync);
        check(processor->projectState().randomizer.fillEmptyOnly,
              "changing Trigger Chance preserves Fill Empty state");
        control->mouseDown(click);
        control->mouseUp(click);
        check(!processor->projectState().randomizer.fillEmptyOnly,
              "a second click disables Fill Empty without dragging");
    }
}

void testRandomizerPadSelectionIsUndoable()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);

    auto* dice = childWithId<juce::TextButton>(*editor, "brush.dice");
    auto* kick = childWithId<juce::TextButton>(*editor, "pad.0");
    check(dice != nullptr && kick != nullptr, "pad-selection test locates editor controls");
    if (dice == nullptr || kick == nullptr)
        return;
    dice->onClick();
    check(!processor->projectEditor().canUndo(),
          "entering randomizer mode does not create an unrelated undo step");

    kick->onClick();
    const auto& selected = processor->projectState().randomizer.selectedPads;
    check(selected == std::vector<std::uint8_t> { 0 },
          "randomizer pad click selects Kick");
    check(processor->projectEditor().canUndo(),
          "randomizer pad selection creates an undo checkpoint");
    check(processor->projectEditor().undo(), "randomizer pad selection can be undone");
    check(processor->projectState().randomizer.selectedPads.empty(),
          "undo restores the prior randomizer pad selection");
}

void configureExportPattern(bsl::ProjectState& state, int numerator, int denominator,
                            const juce::String& name, bool empty, bool finalTriplet)
{
    auto& pattern = state.patterns.front();
    pattern.name = name.toStdString();
    pattern.bars = { bsl::BarState::make(numerator, denominator) };
    pattern.lanes = { bsl::LaneState::make(static_cast<std::size_t>(numerator)) };
    for (auto& beat : pattern.lanes.front().beats)
    {
        beat = bsl::BeatState::withSubdivision(1, 0, 1.0f);
        beat.slices.front().enabled = false;
    }
    if (empty)
        return;
    if (finalTriplet)
    {
        auto& finalBeat = pattern.lanes.front().beats.back();
        finalBeat = bsl::BeatState::withSubdivision(3, 0, 1.0f);
        for (auto& slice : finalBeat.slices)
            slice.enabled = false;
        finalBeat.slices.back().enabled = true;
    }
    else
    {
        pattern.lanes.front().beats.front().slices.front().enabled = true;
    }
}

void testPatternMidiExportPreservesNameAndMeterDuration(const juce::File& output)
{
    struct Case
    {
        int numerator;
        int denominator;
        const char* name;
        bool empty;
        bool finalTriplet;
        double swing = 0.0;
    };
    constexpr std::array cases {
        Case { 3, 8, "Sparse 3-8", false, false },
        Case { 7, 16, "Late 7-16", false, true },
        Case { 1, 16, "Swung 1-16", false, true, 200.0 },
        Case { 3, 8, "Empty 3-8", true, false },
        Case { 7, 16, "Empty 7-16", true, false },
        Case { 5, 8, "Malyan / 5:8 — café?!", false, false },
    };
    constexpr double ticksPerQuarter = 960.0;
    for (const auto test : cases)
    {
        BeatScholarLinuxProcessor processor;
        configureExportPattern(processor.projectState(), test.numerator, test.denominator,
                               juce::String::fromUTF8(test.name), test.empty, test.finalTriplet);
        processor.projectState().global.swingPercent = test.swing;
        BeatScholarLinuxEditor editor(processor);

        // Proposed public test seam: writer accepts an explicit directory rather
        // than writing into ~/.cache during tests.
        const auto file = editor.writePatternMidi(0, output.getChildFile("midi-export"));
        check(file.existsAsFile(), "empty and sparse patterns export MIDI files");
        juce::FileInputStream input(file);
        juce::MidiFile midi;
        check(input.openedOk() && midi.readFrom(input) && midi.getNumTracks() == 1,
              "exported pattern is a readable single-track MIDI file");
        if (midi.getNumTracks() != 1)
            continue;
        const auto* track = midi.getTrack(0);
        const auto expectedEnd = static_cast<double>(test.numerator) * 4.0
                                 / static_cast<double>(test.denominator) * ticksPerQuarter;
        bool hasName = false;
        bool hasEnd = false;
        double endTick = -1.0;
        double latestNoteOff = -1.0;
        double latestNoteOn = -1.0;
        for (int index = 0; index < track->getNumEvents(); ++index)
        {
            const auto& message = track->getEventPointer(index)->message;
            if (message.isTrackNameEvent())
                hasName = message.getTextFromTextMetaEvent() == juce::String::fromUTF8(test.name);
            if (message.isEndOfTrackMetaEvent())
            {
                hasEnd = true;
                endTick = message.getTimeStamp();
            }
            if (message.isNoteOn())
            {
                latestNoteOn = message.getTimeStamp();
                check(latestNoteOn >= 0.0 && latestNoteOn < expectedEnd,
                      "exported note-on lies inside the complete clip");
            }
            if (message.isNoteOff())
            {
                check(latestNoteOn >= 0.0 && message.getTimeStamp() >= latestNoteOn,
                      "exported note-off follows its single enabled note-on");
                latestNoteOff = std::max(latestNoteOff, message.getTimeStamp());
            }
        }
        check(hasName, "exported MIDI carries the pattern name as track metadata");
        check(hasEnd && std::abs(endTick - expectedEnd) < 1.0e-9,
              "exported MIDI ends at the complete meter-defined pattern duration");
        check(latestNoteOff <= expectedEnd,
              "exported final note-off is clamped to the end-of-track tick");
        if (test.swing == 200.0)
            check(latestNoteOn == 27.0 && latestNoteOff == 160.0,
                  "swung short-meter MIDI retains a playable note and the cyclic start position");
    }
}
void testDrumBrushDoesNotInheritSubdivision()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns.front().lanes = { bsl::LaneState::make(4) };
    state.ui.selectedPad = 15;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);

    // The public keyboard path selects a non-default subdivision. The
    // subsequent explicit Drum selection must take precedence for the click.
    check(editor->keyPressed(juce::KeyPress('4', {}, '4')),
          "4 selects a four-slice subdivision");
    auto* drum = childWithId<juce::TextButton>(*editor, "brush.drum");
    check(drum != nullptr, "drum/subdivision test locates Drum brush");
    if (drum == nullptr)
        return;
    drum->onClick();

    const auto click = visualEvent(*editor, { 196.0f, 306.0f });
    editor->mouseDown(click);
    editor->mouseUp(click);
    const auto& beat = state.patterns.front().lanes.front().beats.front();
    check(beat.slices.size() == 1,
          "Drum brush places/replaces instead of retaining the prior slicer subdivision");
    check(beat.slices.front().enabled && beat.slices.front().padIndex == 15,
          "Drum brush still applies the selected pad");
}

void testTogglingNoteRemembersVelocity()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns[0].lanes = { bsl::LaneState::make(4) };
    state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(3, 7, 0.3f);
    state.ui.selectedPad = 7;
    state.global.defaultVelocity = 0.8f;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    const auto original = state.patterns[0].lanes[0].beats;
    const auto click = visualEvent(*editor, { 150.0f, 335.0f });
    editor->mouseDown(click);
    editor->mouseUp(click);
    check(!state.patterns[0].lanes[0].beats[0].slices[0].enabled,
          "Drum click disables the targeted note");
    editor->mouseDown(click);
    editor->mouseUp(click);
    check(state.patterns[0].lanes[0].beats == original,
          "off/on restores the note with its remembered velocity and untouched neighbours");
    state.ui.selectedPad = 15;
    editor->mouseDown(click);
    editor->mouseUp(click);
    const auto& replacement = state.patterns[0].lanes[0].beats[0].slices[0];
    check(replacement.enabled && replacement.padIndex == 15 && replacement.velocity == 0.8f,
          "placing a different pad still uses the default velocity");
}

void testFreshNoteUsesDefaultAndKeepsItAfterRecall()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns[0].lanes = { bsl::LaneState::make(4) };
    state.ui.selectedPad = 0;
    state.global.defaultVelocity = 0.45f;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    const auto click = visualEvent(*editor, { 236.0f, 266.0f });
    editor->mouseDown(click);
    editor->mouseUp(click);
    check(state.patterns[0].lanes[0].beats[0].slices[0].velocity == 0.45f,
          "a fresh cell with the same pad identity uses the current toolbar default");
    editor->mouseDown(click);
    editor->mouseUp(click);
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);
    processor->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    editor.reset();
    state.global.defaultVelocity = 0.9f;
    editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    const auto recalledClick = visualEvent(*editor, { 236.0f, 266.0f });
    editor->mouseDown(recalledClick);
    editor->mouseUp(recalledClick);
    const auto& restored = state.patterns[0].lanes[0].beats[0].slices[0];
    check(restored.enabled && restored.velocity == 0.45f,
          "off/on after host recall remembers the note instead of the changed default");
}

void testShiftBeatFillOverridesBrushAndRemembersVelocities()
{
    for (const char* brush : { "brush.drum", "brush.slicer", "brush.dice", "brush.velocity" })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto& state = processor->projectState();
        state.patterns[0].lanes = { bsl::LaneState::make(4) };
        state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(3, 7, 0.3f);
        state.patterns[0].lanes[0].beats[0].slices[1].enabled = false;
        state.ui.selectedPad = 7;
        state.randomizer.selectedSubdivisions = {5};
        state.randomizer.triggerChance = 1.0f;
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->keyPressed(juce::KeyPress('5', {}, '5'));
        auto* button = childWithId<juce::TextButton>(*editor, brush);
        check(button != nullptr, "Shift-beat test locates active brush");
        if (button == nullptr) continue;
        button->onClick();
        const auto original = state.patterns[0].lanes[0].beats;
        auto filled = original;
        for (auto& slice : filled[0].slices) slice.enabled = true;
        auto empty = original;
        for (auto& slice : empty[0].slices) slice.enabled = false;
        const auto click = visualEvent(*editor, { 236.0f, 266.0f },
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
        editor->mouseDown(click);
        editor->mouseUp(click);
        check(state.patterns[0].lanes[0].beats == filled,
              juce::String(brush) + ": Shift fills a mixed beat without reslicing or changing remembered velocities");
        editor->mouseDown(click);
        editor->mouseUp(click);
        check(state.patterns[0].lanes[0].beats == empty,
              juce::String(brush) + ": Shift empties a filled beat uniformly");
        check(processor->projectEditor().undo() && state.patterns[0].lanes[0].beats == filled,
              "one undo restores the filled beat");
        check(processor->projectEditor().undo() && state.patterns[0].lanes[0].beats == original,
              "second undo restores the original mixed beat");
    }
}

void testShiftLaneResetOverridesActiveBrush()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns.front().lanes = { bsl::LaneState::make(4) };
    for (auto& beat : state.patterns.front().lanes.front().beats)
        beat = bsl::BeatState::withSubdivision(3, 7, 0.5f);
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);

    auto* slicer = childWithId<juce::TextButton>(*editor, "brush.slicer");
    check(slicer != nullptr, "Shift-reset test locates Slicer brush");
    if (slicer == nullptr)
        return;
    slicer->onClick();

    const auto shiftClick = visualEvent(*editor, { 20.0f, 306.0f },
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier
                           | juce::ModifierKeys::shiftModifier));
    editor->mouseDown(shiftClick);
    editor->mouseUp(shiftClick);

    for (const auto& beat : state.patterns.front().lanes.front().beats)
    {
        check(beat.slices.size() == 3,
              "Shift-click Lane Arrow resets without changing its subdivision");
        for (const auto& slice : beat.slices)
            check(!slice.enabled, "Shift-click Lane Arrow resets every slice");
    }
}

void testShiftLaneResetOverridesRandomizer()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns.front().lanes = { bsl::LaneState::make(4) };
    for (auto& beat : state.patterns.front().lanes.front().beats)
        beat = bsl::BeatState::withSubdivision(3, 7, 0.5f);
    state.randomizer.triggerChance = 1.0f;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);

    auto* slicer = childWithId<juce::TextButton>(*editor, "brush.dice");
    check(slicer != nullptr, "Shift-reset test locates Randomizer brush");
    if (slicer == nullptr)
        return;
    slicer->onClick();

    const auto shiftClick = visualEvent(*editor, { 20.0f, 306.0f },
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier
                           | juce::ModifierKeys::shiftModifier));
    editor->mouseDown(shiftClick);
    editor->mouseUp(shiftClick);

    for (const auto& beat : state.patterns.front().lanes.front().beats)
    {
        check(beat.slices.size() == 3,
              "Shift-click Lane Arrow resets without changing its subdivision");
        for (const auto& slice : beat.slices)
            check(!slice.enabled, "Shift-click Lane Arrow resets every slice");
    }
}

void testBarMeterDragIsOneUndoStep(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);
    editor->mouseDown(visualEvent(*editor, { 580.0f, 80.0f }));
    for (const float y : { 68.0f, 56.0f, 44.0f })
        editor->mouseDrag(visualEvent(*editor, { 580.0f, y }));
    editor->mouseUp(visualEvent(*editor, { 580.0f, 44.0f }));
    check(processor->projectState().patterns.front().bars.front().numerator == 7,
          "bar meter drag crosses three numerator steps");
    check(processor->projectEditor().undo(), "meter gesture has an undo step");
    const auto& original = processor->projectState().patterns.front();
    check(original.bars.front().numerator == 4 && original.lanes.front().beats.size() == 4,
          "one undo restores the meter and lane contents before the drag gesture");
    check(processor->projectEditor().redo()
              && processor->projectState().patterns.front().bars.front().numerator == 7,
          "one redo restores the full meter drag gesture");
    check(processor->projectEditor().undo(), "restore meter before arrangement-pad edits");
    editor->mouseDown(visualEvent(*editor, { 1190.0f, 146.0f }));
    for (const float y : { 134.0f, 122.0f, 110.0f })
        editor->mouseDrag(visualEvent(*editor, { 1190.0f, y }));
    editor->mouseUp(visualEvent(*editor, { 1190.0f, 110.0f }));
    check(processor->projectState().patterns[0].bars[0].numerator == 7,
          "left number on arrangement bar pad changes the beat count by vertical drag");
    check(processor->projectState().patterns[0].bars[0].denominator == 4,
          "arrangement beat-count drag retains note value");
    check(processor->projectEditor().undo()
              && processor->projectState().patterns[0].bars[0].numerator == 4,
          "arrangement beat-count gesture is one undo step");
    editor->mouseDown(visualEvent(*editor, { 1220.0f, 146.0f }));
    for (const float y : { 134.0f, 122.0f })
        editor->mouseDrag(visualEvent(*editor, { 1220.0f, y }));
    editor->mouseUp(visualEvent(*editor, { 1220.0f, 122.0f }));
    check(processor->projectState().patterns[0].bars[0].denominator == 16,
          "right number on arrangement bar pad changes note value by vertical drag");
    check(processor->projectState().patterns[0].bars[0].numerator == 4,
          "arrangement note-value drag retains beat count");
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    auto stream = output.getChildFile("arrangement-meter-drag.png").createOutputStream();
    if (stream) { stream->setPosition(0); stream->truncate(); }
    check(stream && juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
          "capture arrangement meter after actual drag");
    check(processor->projectEditor().undo()
              && processor->projectState().patterns[0].bars[0].denominator == 4,
          "arrangement note-value gesture is one undo step");
}

void testArrowTransformRequiresClick()
{
    for (const auto key : { juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::downKey })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto& pattern = processor->projectState().patterns[0];
        pattern.lanes = { bsl::LaneState::make(4) };
        auto& beat = pattern.lanes[0].beats[0];
        beat = bsl::BeatState::withSubdivision(3, 2, 0.7f);
        beat.slices[1].padIndex = 5;
        beat.slices[2].padIndex = 8;
        const auto original = beat;
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        const auto event = visualEvent(*editor, { 236.0f, 266.0f });
        editor->mouseDown(event);
        editor->mouseUp(event);
        const auto clicked = processor->projectState().patterns[0].lanes[0].beats[0];
        check(clicked != original, "ordinary beat click changes the target note");
        editor->keyPressed(juce::KeyPress(key));
        check(processor->projectState().patterns[0].lanes[0].beats[0] == clicked,
              "bare arrow after a completed click does not transform the last beat");
        check(processor->projectEditor().undo()
                  && processor->projectState().patterns[0].lanes[0].beats[0] == original
                  && !processor->projectEditor().canUndo(),
              "bare arrow adds no history beyond the ordinary click");
    }
}

// Manual p17 and Samples & Sounds 2:23-2:40: the folder opens a chooser
// directly; X restores the factory sample. Exercise the actual async dialog
// on the isolated Linux display, where JUCE uses its built-in file browser.
class TutorialSampleChooserTests final : private juce::Timer
{
public:
    explicit TutorialSampleChooserTests(const juce::File& output) : output_(output)
    {
        const auto bytes = constantPcm16Wav(48000, 4096);
        sample_ = output.getChildFile("tutorial-chooser.wav");
        check(sample_.replaceWithData(bytes.data(), bytes.size()), "write synthetic chooser fixture");
        expectedHash_ = juce::SHA256(bytes.data(), bytes.size()).toHexString().toStdString();
        editor_ = std::make_unique<BeatScholarLinuxEditor>(processor_);
        editor_->setSize(1280, 720);
        window_.setContentNonOwned(editor_.get(), true);
    }

    ~TutorialSampleChooserTests() override { window_.clearContentComponent(); }

    void start(std::function<void()> done)
    {
        done_ = std::move(done);
        if (juce::FileChooser::isPlatformDialogAvailable())
        {
            std::cout << "SKIP: tutorial chooser requires JUCE's built-in dialog (no native picker)\n";
            done_();
            return;
        }
        before_ = processor_.projectState();
        revision_ = processor_.projectEditor().revision();
        window_.setVisible(true);
        window_.toFront(true);
        startTimer(100);
    }

private:
    static bool samePad(const bsl::PadState& actual, const bsl::PadState& expected)
    {
        auto a = bsl::ProjectState::makeDefault();
        auto b = a;
        a.kit.pads[0] = actual;
        b.kit.pads[0] = expected;
        return bsl::statecodec::encode(a) == bsl::statecodec::encode(b);
    }

    static bool samePads(const bsl::KitState& actual, const bsl::KitState& expected)
    {
        auto a = bsl::ProjectState::makeDefault();
        auto b = a;
        a.kit = actual;
        b.kit = expected;
        return bsl::statecodec::encode(a) == bsl::statecodec::encode(b);
    }

    static juce::FileBrowserComponent* findBrowser(juce::Component& component)
    {
        if (auto* browser = dynamic_cast<juce::FileBrowserComponent*>(&component)) return browser;
        for (auto* child : component.getChildren())
            if (auto* browser = findBrowser(*child)) return browser;
        return nullptr;
    }

    void finish()
    {
        stopTimer();
        window_.setVisible(false);
        done_();
    }

    void timerCallback() override
    {
        if (phase_ == 0)
        {
            auto* folder = childWithId<juce::TextButton>(*editor_, "sample.folder");
            check(folder != nullptr, "sample folder control exists");
            if (folder == nullptr) { finish(); return; }
            factoryFolderBounds_ = folder->getBounds();
            folder->onClick();
            phase_ = 1;
        }
        else if (phase_ == 1)
        {
            dialog_ = dynamic_cast<juce::FileChooserDialogBox*>(juce::Component::getCurrentlyModalComponent());
            auto* browser = dialog_ != nullptr ? findBrowser(*dialog_) : nullptr;
            check(browser != nullptr, "sample folder opens the file chooser directly without an extra menu");
            if (browser == nullptr)
            {
                juce::PopupMenu::dismissAllActiveMenus();
                finish();
                return;
            }
            if (case_ == 0) saveVisual(*dialog_, output_, "tutorial-sample-chooser.png");
            else
            {
                browser->setRoot(sample_.getParentDirectory());
                browser->setFileName(sample_.getFileName());
            }
            if (case_ == 2)
            {
                auto restored = before_;
                restored.kit.pads[0].pitchSemitones = 7.0f;
                const auto bytes = bsl::statecodec::encode(restored);
                processor_.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
                before_ = processor_.projectState();
                revision_ = processor_.projectEditor().revision();
            }
            phase_ = 2;
        }
        else if (phase_ == 2)
        {
            if (case_ != 0)
            {
                auto* browser = dialog_ != nullptr ? findBrowser(*dialog_) : nullptr;
                check(browser != nullptr && browser->getSelectedFile(0) == sample_,
                      "chooser selects the synthetic WAV before accepting");
            }
            if (dialog_ != nullptr) dialog_->exitModalState(case_ == 0 ? 0 : 1);
            phase_ = 3;
        }
        else
        {
            const auto& pads = processor_.projectState().kit.pads;
            if (case_ == 1)
            {
                check(pads[0].customSampleName == sample_.getFileName().toStdString()
                          && pads[0].customSampleSha256 == expectedHash_
                          && !pads[0].customSampleBase64.empty(),
                      "accepting the folder chooser loads and embeds the selected sample");
                for (std::size_t index = 1; index < pads.size(); ++index)
                    check(samePad(pads[index], before_.kit.pads[index]), "folder import preserves every other pad");
                check(processor_.projectEditor().revision() == revision_ + 1,
                      "folder import adds one undo step");
                auto* clear = childWithId<juce::TextButton>(*editor_, "sample.clear");
                check(clear != nullptr && clear->isVisible(), "custom sample exposes the X removal control");
                auto* folder = childWithId<juce::TextButton>(*editor_, "sample.folder");
                check(folder != nullptr && folder->getBounds() == factoryFolderBounds_,
                      "imported sample keeps the folder beside the waveform");
                saveVisual(*editor_, output_, "tutorial-sample-loaded.png");
                editor_->setSize(960, 540);
                saveVisual(*editor_, output_, "tutorial-sample-loaded-960.png");
                editor_->setSize(1280, 720);
                if (clear != nullptr) clear->onClick();
                check(samePad(pads[0], before_.kit.pads[0]), "X restores the original factory sample settings");
                check(clear != nullptr && !clear->isVisible(), "removal hides X and restores the factory layout");
                saveVisual(*editor_, output_, "tutorial-sample-removed.png");
                auto* undo = childWithId<juce::TextButton>(*editor_, "toolbar.undo");
                auto* redo = childWithId<juce::TextButton>(*editor_, "toolbar.redo");
                check(undo != nullptr && redo != nullptr, "sample removal undo controls exist");
                if (undo != nullptr && redo != nullptr)
                {
                    undo->onClick();
                    check(processor_.projectState().kit.pads[0].customSampleSha256 == expectedHash_,
                          "undo of X restores the imported sample");
                    redo->onClick();
                    check(samePad(processor_.projectState().kit.pads[0], before_.kit.pads[0]),
                          "redo of X restores factory settings again");
                }
            }
            else
                check(samePads(processor_.projectState().kit, before_.kit) && processor_.projectEditor().revision() == revision_,
                      case_ == 0 ? "cancelled chooser preserves samples and undo history"
                                 : "chooser opened before host restore cannot overwrite restored samples");
            if (++case_ == 3) { finish(); return; }
            phase_ = 0;
        }
    }

    juce::File output_, sample_;
    std::string expectedHash_;
    BeatScholarLinuxProcessor processor_;
    std::unique_ptr<BeatScholarLinuxEditor> editor_;
    juce::DocumentWindow window_ { "Tutorial sample import", juce::Colours::black,
                                   juce::DocumentWindow::closeButton };
    juce::Component::SafePointer<juce::FileChooserDialogBox> dialog_;
    bsl::ProjectState before_;
    juce::Rectangle<int> factoryFolderBounds_;
    std::uint64_t revision_ = 0;
    int case_ = 0, phase_ = 0;
    std::function<void()> done_;
};

bsl::ProjectState makeBypassTestProject(int padIndex);

class TutorialHeaderAndFactoryMenus final : private juce::Timer
{
public:
    explicit TutorialHeaderAndFactoryMenus(const juce::File& output) : output_(output) {}
    ~TutorialHeaderAndFactoryMenus() override { window_.clearContentComponent(); }
    void start(std::function<void()> done)
    {
        done_ = std::move(done);
        previousXdg_ = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", "");
        const auto xdg = output_.getChildFile("factory-menu-xdg");
        const auto samples = xdg.getChildFile("BeatScholarLinux/Factory/Samples");
        check(samples.createDirectory().wasOk(), "create factory menu fixtures");
        juce::MemoryOutputStream wav;
        const auto fixture = makeBypassTestProject(0);
        check(juce::Base64::convertFromBase64(wav, juce::String(fixture.kit.pads[0].customSampleBase64.value())),
              "decode factory menu WAV fixture");
        for (const auto* name : {"test_INST-BD_TYPE-Body_NAME-Wav Body.wav",
                                 "test_INST-BD_TYPE-Hit_NAME-Wav Hit.WAV",
                                 "test_INST-SD_TYPE-Body_NAME-Wrong Pad.wav"})
            check(samples.getChildFile(name).replaceWithData(wav.getData(), wav.getDataSize()),
                  "write factory menu WAV fixture");
        samples.getChildFile("test_INST-BD_TYPE-Body_NAME-Not Audio.txt").replaceWithText("not audio");
        setenv("XDG_DATA_HOME", xdg.getFullPathName().toRawUTF8(), 1);
        processor_.projectEditor().addPattern();
        processor_.projectState().activePattern = 0;
        processor_.projectState().ui.selectedPad = 0;
        processor_.projectState().ui.patternMode = true;
        editor_ = std::make_unique<BeatScholarLinuxEditor>(processor_);
        window_.setContentNonOwned(editor_.get(), true);
        window_.setVisible(true);
        window_.toFront(true);
        startTimer(75);
    }
private:
    static juce::AccessibilityHandler* item(juce::Component& component, const juce::String& title)
    {
        if (auto* h = component.getAccessibilityHandler(); h != nullptr && h->getTitle() == title) return h;
        for (auto* child : component.getChildren()) if (auto* h = item(*child, title)) return h;
        return nullptr;
    }
    void timerCallback() override
    {
        // Seven real choices at three scales, then no-op, cancel, host recall,
        // changed pattern identity, Bars mode, and both factory sample layers.
        const bool factory = step_ >= 26;
        const int choice = step_ < 21 ? step_ % 7 : 0;
        const std::array<bsl::PatternQuantization, 7> values {
            bsl::PatternQuantization::whole, bsl::PatternQuantization::half,
            bsl::PatternQuantization::quarter, bsl::PatternQuantization::eighth,
            bsl::PatternQuantization::sixteenth, bsl::PatternQuantization::thirtySecond,
            bsl::PatternQuantization::off };
        const std::array<const char*, 7> labels {"4/4", "2/4", "1/4", "1/8", "1/16", "1/32", "Off"};
        const auto label = juce::String("header/factory menu case ") + juce::String(step_);
        if (phase_ == 0)
        {
            auto& state = processor_.projectState();
            state.ui.patternMode = step_ != 25;
            state.activePattern = 0;
            state.patterns[0].quantization = step_ == 21 ? values[choice]
                : values[choice] == bsl::PatternQuantization::off ? bsl::PatternQuantization::whole
                                                                : bsl::PatternQuantization::off;
            const int width = step_ < 21 ? std::array<int, 3>{960, 1280, 1920}[step_ / 7] : 1280;
            editor_->setSize(width, width * 9 / 16);
            if (factory)
            {
                std::vector<juce::Button*> buttons;
                for (auto* child : editor_->getChildren())
                    if (child->getComponentID() == "sample.layer")
                        if (auto* button = dynamic_cast<juce::Button*>(child)) buttons.push_back(button);
                std::sort(buttons.begin(), buttons.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
                check(buttons.size() == 2, label + " exposes both sample knobs");
                if (buttons.size() == 2) buttons[static_cast<std::size_t>(step_ - 26)]->onClick();
            }
            else
            {
                const juce::Point<float> point {1209.0f * width / 1280.0f, 97.0f * width / 1280.0f};
                juce::Desktop::setMousePosition(editor_->localPointToGlobal(point.toInt()));
                editor_->mouseDown(visualEvent(*editor_, point));
            }
            before_ = state;
            revision_ = processor_.projectEditor().revision();
            phase_ = 1;
        }
        else if (phase_ == 1)
        {
            auto* menu = juce::Component::getCurrentlyModalComponent();
            check((menu != nullptr) == (step_ != 25), label + " opens only the intended menu");
            if (step_ == 23)
            {
                auto recalled = before_;
                recalled.patterns[0].name = "Host recalled";
                const auto bytes = bsl::statecodec::encode(recalled);
                processor_.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
                before_ = processor_.projectState();
                revision_ = processor_.projectEditor().revision();
            }
            else if (step_ == 24)
            {
                std::swap(processor_.projectState().patterns[0], processor_.projectState().patterns[1]);
                before_ = processor_.projectState();
            }
            if (menu != nullptr && step_ != 22)
            {
                const char* title = factory ? (step_ == 26 ? "Wav Body" : "Wav Hit") : labels[choice];
                if (factory)
                {
                    check(item(*menu, "Wrong Pad") == nullptr && item(*menu, "Not Audio") == nullptr
                              && item(*menu, step_ == 26 ? "Wav Hit" : "Wav Body") == nullptr,
                          label + " excludes other pads, other layer and non-audio files");
                }
                if (step_ == 0 || step_ == 7 || step_ == 14 || factory)
                    saveVisual(*menu, output_, ("tutorial-header-factory-menu-" + juce::String(step_) + ".png").toRawUTF8());
                auto* action = item(*menu, title);
                check(action != nullptr && action->getActions().invoke(juce::AccessibilityActionType::press),
                      label + " selects the visible menu item");
                if (action == nullptr) juce::PopupMenu::dismissAllActiveMenus();
            }
            else juce::PopupMenu::dismissAllActiveMenus();
            phase_ = 2;
        }
        else
        {
            auto expected = before_;
            const bool edits = step_ < 21 || factory;
            if (step_ < 21) expected.patterns[0].quantization = values[choice];
            if (factory)
                expected.kit.pads[0].factorySampleIds[static_cast<std::size_t>(step_ - 26)] = step_ == 26
                    ? "test_INST-BD_TYPE-Body_NAME-Wav Body.wav" : "test_INST-BD_TYPE-Hit_NAME-Wav Hit.WAV";
            check(bsl::statecodec::encode(processor_.projectState()) == bsl::statecodec::encode(expected),
                  label + " changes exactly the selected setting or rejects stale/cancelled input");
            check(processor_.projectEditor().revision() == revision_ + (edits ? 1 : 0),
                  label + " records exactly the intended edit count");
            if (edits)
                check(processor_.projectEditor().undo()
                          && bsl::statecodec::encode(processor_.projectState()) == bsl::statecodec::encode(before_),
                      label + " restores all state with one undo");
            juce::PopupMenu::dismissAllActiveMenus();
            phase_ = 0;
            if (++step_ == 28)
            {
                stopTimer();
                window_.setVisible(false);
                if (previousXdg_.isEmpty()) unsetenv("XDG_DATA_HOME");
                else setenv("XDG_DATA_HOME", previousXdg_.toRawUTF8(), 1);
                done_();
            }
        }
    }
    juce::File output_;
    juce::String previousXdg_;
    BeatScholarLinuxProcessor processor_;
    std::unique_ptr<BeatScholarLinuxEditor> editor_;
    juce::DocumentWindow window_{"Tutorial header and sample menus", juce::Colours::black,
                                juce::DocumentWindow::closeButton};
    bsl::ProjectState before_;
    std::function<void()> done_;
    std::uint64_t revision_ = 0;
    int step_ = 0, phase_ = 0;
};

class InitPresetMenuTests final : private juce::Timer
{
public:
    explicit InitPresetMenuTests(const juce::File& output) : output_(output)
    {
        auto& state = processor_.projectState();
        state.patterns[0].name = "Edited pattern";
        state.ui.presetFile = "Edited Main.bslpreset";
        state.ui.kitFile = "Edited Kit.bslkit";
        state.kit.pads[0].name = "Edited kick";
        editor_ = std::make_unique<BeatScholarLinuxEditor>(processor_);
        editor_->setSize(1280, 720);
        window_.setContentNonOwned(editor_.get(), true);
    }

    ~InitPresetMenuTests() override { window_.clearContentComponent(); }

    void start(std::function<void()> done)
    {
        done_ = std::move(done);
        window_.setVisible(true);
        window_.toFront(true);
        startTimer(75);
    }

private:
    static juce::AccessibilityHandler* item(juce::Component& component, const juce::String& title)
    {
        if (auto* h = component.getAccessibilityHandler(); h != nullptr && h->getTitle() == title) return h;
        for (auto* child : component.getChildren())
            if (auto* h = item(*child, title)) return h;
        return nullptr;
    }

    void timerCallback() override
    {
        if (phase_ == 0)
        {
            auto* button = childWithId<juce::TextButton>(*editor_, "toolbar.preset");
            check(button != nullptr, "Main Init is reachable from the preset button");
            if (button != nullptr) button->onClick();
        }
        else if (phase_ == 1)
        {
            auto* menu = juce::Component::getCurrentlyModalComponent();
            check(menu != nullptr, "Main preset button opens its menu");
            if (menu != nullptr)
            {
                saveVisual(*menu, output_, "main-init-menu.png");
                auto* action = item(*menu, "Init Preset");
                check(action != nullptr && action->getActions().invoke(juce::AccessibilityActionType::press),
                      "Main menu exposes an actionable Init Preset item");
                if (action == nullptr) juce::PopupMenu::dismissAllActiveMenus();
            }
        }
        else if (phase_ == 2)
        {
            auto* button = childWithId<juce::TextButton>(*editor_, "toolbar.preset");
            check(processor_.projectState().patterns[0].name == "A"
                      && button != nullptr && button->getButtonText() == "Init",
                  "Main menu action applies Init and clears its old identity");
            saveVisual(*editor_, output_, "main-init-state.png");
            auto& state = processor_.projectState();
            state.patterns[0].name = "Sequence survives kit init";
            state.ui.kitFile = "Edited Kit.bslkit";
            state.kit.pads[0].name = "Edited kick";
            auto* kit = childWithId<juce::TextButton>(*editor_, "kit.menu");
            check(kit != nullptr, "Kit Init is reachable from the kit button");
            if (kit != nullptr) kit->onClick();
        }
        else if (phase_ == 3)
        {
            auto* menu = juce::Component::getCurrentlyModalComponent();
            check(menu != nullptr, "Kit preset button opens its menu");
            if (menu != nullptr)
            {
                saveVisual(*menu, output_, "kit-init-menu.png");
                auto* action = item(*menu, "Init Preset");
                check(action != nullptr && action->getActions().invoke(juce::AccessibilityActionType::press),
                      "Kit menu exposes an actionable Init Preset item");
                if (action == nullptr) juce::PopupMenu::dismissAllActiveMenus();
            }
        }
        else
        {
            auto* button = childWithId<juce::TextButton>(*editor_, "kit.menu");
            check(processor_.projectState().patterns[0].name == "Sequence survives kit init"
                      && button != nullptr && button->getButtonText() == "Init",
                  "Kit menu action preserves the sequence and clears its old identity");
            saveVisual(*editor_, output_, "kit-init-state.png");
            stopTimer();
            window_.setVisible(false);
            done_();
            return;
        }
        ++phase_;
    }

    juce::File output_;
    BeatScholarLinuxProcessor processor_;
    std::unique_ptr<BeatScholarLinuxEditor> editor_;
    juce::DocumentWindow window_ { "Init preset menus", juce::Colours::black,
                                   juce::DocumentWindow::closeButton };
    std::function<void()> done_;
    int phase_ = 0;
};

void testKeyboardFocus(const juce::File& output)
{
    InitPresetMenuTests initPresetMenus(output);
    TutorialHeaderAndFactoryMenus headerAndFactory(output);
    TutorialArrangementMenuTests arrangementMenus(output);
    TutorialSampleChooserTests sampleChooser(output);
    bsl::ui::BeatScholarLookAndFeel look;
    juce::DocumentWindow parent("Beat Scholar focus regression", juce::Colours::black,
                                juce::DocumentWindow::closeButton);
    juce::TextButton button;
    juce::Slider rotary, linear;
    parent.setLookAndFeel(&look);
    parent.setBounds(0, 0, 300, 120);
    parent.setWantsKeyboardFocus(true);
    parent.setVisible(true);
    parent.toFront(true);
    button.setComponentID("toolbar.play");
    button.setBounds(10, 10, 60, 60);
    rotary.setBounds(90, 10, 60, 60);
    rotary.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    linear.setBounds(170, 10, 110, 60);
    linear.setSliderStyle(juce::Slider::LinearHorizontal);
    for (auto* slider : { &rotary, &linear })
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    for (auto* control : std::array<juce::Component*, 3> { &button, &rotary, &linear })
    {
        parent.addAndMakeVisible(control);
        control->setWantsKeyboardFocus(true);
    }
    auto laneProcessor = std::make_unique<BeatScholarLinuxProcessor>();
    laneProcessor->projectState().patterns[0].lanes = { bsl::LaneState::make(4) };
    auto laneEditor = std::make_unique<BeatScholarLinuxEditor>(*laneProcessor);
    const std::array<const char*, 3> keys { "Left", "Right", "Down" };
    const std::array<int, 3> keyCodes { juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::downKey };
    // X11 commands finish before JUCE necessarily receives the events. Let the
    // message loop deliver focus/key changes, and fail with a bounded deadline.
    juce::String inputBarrierDetail, lastInjectedKey;
    std::function<void(std::function<bool()>, std::function<void()>, juce::String, double)> poll;
    poll = [&](auto ready, auto next, auto label, double deadline) {
        const bool received = ready();
        if (received || juce::Time::getMillisecondCounterHiRes() >= deadline)
        {
            check(received, label + (received ? juce::String() : ": " + inputBarrierDetail));
            // The editor also reflects momentary shortcuts in its 20 Hz timer.
            juce::Timer::callAfterDelay(65, next);
        }
        else
            juce::Timer::callAfterDelay(10, [&, ready, next, label, deadline] {
                poll(ready, next, label, deadline);
            });
    };
    const auto waitUntil = [&](std::function<bool()> ready, std::function<void()> next,
                               juce::String label) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.0;
        juce::Timer::callAfterDelay(10, [&, ready, next, label, deadline] {
            poll(ready, next, label, deadline);
        });
    };
    const auto focusEditor = [&](std::function<void()> next) {
        auto* content = parent.getContentComponent();
        juce::Desktop::setMousePosition(content->localPointToGlobal(juce::Point<int>{650, 400}));
        const auto window = juce::String(static_cast<juce::uint64>(
            reinterpret_cast<std::uintptr_t>(parent.getPeer()->getNativeHandle())));
        juce::ChildProcess command;
        const bool started = command.start(juce::StringArray { "xdotool", "windowfocus", "--sync", window });
        check(started && command.waitForProcessToFinish(2000) && command.getExitCode() == 0,
              "focus editor window only on isolated Xvfb display");
        waitUntil([&] { return parent.getPeer()->isFocused(); }, [&, content, next] {
            content->grabKeyboardFocus();
            waitUntil([content] { return content->hasKeyboardFocus(true); }, next,
                      "editor receives isolated keyboard focus");
        }, "X11 delivers focus to the editor peer");
    };
    std::map<juce::String, bool> expectedKeys;
    const auto sendKey = [&](const char* action, const char* key) {
        juce::ChildProcess command;
        const bool started = command.start(juce::StringArray { "xdotool", action, key });
        check(started && command.waitForProcessToFinish(2000) && command.getExitCode() == 0,
              "send key only to isolated Xvfb display");
        expectedKeys[key] = juce::String(action) == "keydown";
        lastInjectedKey = juce::String(action) + " " + key;
    };
    std::function<bool()> pendingUiInput;
    const auto afterInput = [&](std::function<void()> next) {
        const auto uiReady = std::move(pendingUiInput);
        pendingUiInput = nullptr;
        waitUntil([&, uiReady] {
            // Observe JUCE's delivered state without XQueryPointer. Realtime
            // queries can consume X input into Xlib's queue before the event FD
            // wakes JUCE, stranding the very key this barrier is waiting for.
            const auto mods = juce::ModifierKeys::getCurrentModifiers();
            for (const auto& [key, expected] : expectedKeys)
            {
                const int code = key == "Left" ? juce::KeyPress::leftKey
                               : key == "Right" ? juce::KeyPress::rightKey
                               : key == "Down" ? juce::KeyPress::downKey
                               : key == "slash" ? '/' : static_cast<int>(key[0]);
                const bool down = key == "Alt_L" ? mods.isAltDown()
                                : key == "Shift_L" ? mods.isShiftDown()
                                : juce::KeyPress::isKeyCurrentlyDown(code);
                if (down != expected)
                {
                    inputBarrierDetail = "after " + lastInjectedKey + ", " + key
                        + " expected=" + juce::String(expected ? 1 : 0)
                        + " actual=" + juce::String(down ? 1 : 0);
                    return false;
                }
            }
            if (uiReady && (!uiReady() || mods.isLeftButtonDown()))
            {
                inputBarrierDetail = "waiting for the injected mouse/menu action";
                return false;
            }
            return true;
        }, next, "JUCE receives every injected key, mouse and menu action");
    };
    auto shortcutProcessor = std::make_unique<BeatScholarLinuxProcessor>();
    shortcutProcessor->projectState().patterns[0].lanes = { bsl::LaneState::make(4) };
    auto shortcutEditor = std::make_unique<BeatScholarLinuxEditor>(*shortcutProcessor);
    const auto brushSelected = [&](const char* id) {
        auto* control = childWithId<juce::TextButton>(*shortcutEditor, id);
        return control != nullptr && control->getToggleState();
    };
    const auto chooseBrush = [&](const char* id) {
        auto* control = childWithId<juce::TextButton>(*shortcutEditor, id);
        check(control != nullptr, "momentary test locates brush");
        if (control != nullptr) control->onClick();
        shortcutEditor->grabKeyboardFocus();
    };
    const auto randomPanelVisible = [&] {
        auto* control = childWithId<juce::Slider>(*shortcutEditor, "random.0");
        return control != nullptr && control->isVisible();
    };
    // Use the real dropdown and X11 beat/lane clicks: selecting a brush alone
    // cannot establish that its temporary action or restored subdivision works.
    std::vector<bsl::BeatState> shortcutOriginalBeats;
    const auto clickShortcutEditor = [&](juce::Point<int> position, std::function<bool()> ready) {
        const auto screen = shortcutEditor->localPointToGlobal(position);
        juce::ChildProcess command;
        const bool started = command.start(juce::StringArray { "xdotool", "mousemove",
            juce::String(screen.x), juce::String(screen.y), "click", "1" });
        check(started && command.waitForProcessToFinish(2000) && command.getExitCode() == 0,
              "click shortcut editor only on isolated Xvfb display");
        pendingUiInput = std::move(ready);
    };
    const auto clickShortcutGrid = [&](juce::Point<int> position) {
        const auto revision = shortcutProcessor->projectEditor().revision();
        clickShortcutEditor(position, [&, revision] {
            return shortcutProcessor->projectEditor().revision() > revision;
        });
    };
    const auto openSubdivisionMenu = [&] {
        clickShortcutEditor({789, 30}, [] {
            return juce::Component::getCurrentlyModalComponent() != nullptr;
        });
    };
    std::function<juce::AccessibilityHandler*(juce::Component&)> findFour;
    findFour = [&](juce::Component& component) -> juce::AccessibilityHandler* {
        if (auto* handler = component.getAccessibilityHandler();
            handler != nullptr && handler->getTitle() == "4") return handler;
        for (auto* child : component.getChildren())
            if (auto* item = findFour(*child)) return item;
        return nullptr;
    };
    const auto selectFour = [&] {
        auto* menu = juce::Component::getCurrentlyModalComponent();
        auto* item = menu != nullptr ? findFour(*menu) : nullptr;
        const bool selected = item != nullptr
            && item->getActions().invoke(juce::AccessibilityActionType::press);
        check(selected, "select four slices through the actual Slicer dropdown");
        if (!selected) juce::PopupMenu::dismissAllActiveMenus();
        pendingUiInput = [] { return juce::Component::getCurrentlyModalComponent() == nullptr; };
    };
    const auto undoShortcutEdit = [&] {
        check(shortcutProcessor->projectEditor().undo()
                  && shortcutProcessor->projectState().patterns[0].lanes[0].beats == shortcutOriginalBeats
                  && !shortcutProcessor->projectEditor().canUndo(),
              "one undo restores the complete pre-shortcut lane without extra history");
    };
    const auto randomizedTriple = [](const bsl::BeatState& beat) {
        return beat.slices.size() == 3
            && std::all_of(beat.slices.begin(), beat.slices.end(), [](const auto& slice) {
                return slice.enabled && slice.padIndex == 7 && slice.velocity == 0.5f
                    && slice.velocityAssigned;
            });
    };
    const auto testBooleanResets = [&] {
        check(juce::ModifierKeys::getCurrentModifiers().isAltDown(),
              "isolated editor observes held Alt for parameter resets");
        const auto reset = [&](const char* id, const char* label, auto value, bool defaultValue) {
            juce::TextButton* control = id == nullptr ? nullptr
                : childWithId<juce::TextButton>(*shortcutEditor, id);
            if (control == nullptr && label != nullptr)
                for (auto* child : shortcutEditor->getChildren())
                    if (auto* button = dynamic_cast<juce::TextButton*>(child);
                        button != nullptr && button->getButtonText() == label)
                        control = button;
            check(control != nullptr, "reset test locates boolean parameter");
            if (control == nullptr) return;
            value() = !defaultValue;
            control->onClick();
            check(value() == defaultValue, "Alt-click restores boolean default");
            control->onClick();
            check(value() == defaultValue, "Alt-click at boolean default does not toggle");
            check(shortcutProcessor->projectEditor().undo() && value() == !defaultValue
                      && !shortcutProcessor->projectEditor().canUndo(),
                  "boolean reset is one undoable action and default reset adds no history");
        };
        reset("kit.audition", nullptr, [&]() -> bool& {
            return shortcutProcessor->projectState().global.auditionDuringPlayback; }, true);
        reset("kit.lock", nullptr, [&]() -> bool& {
            return shortcutProcessor->projectState().global.kitLocked; }, false);
        reset(nullptr, "S", [&]() -> bool& {
            return shortcutProcessor->projectState().kit.pads[0].solo; }, false);
        reset(nullptr, "M", [&]() -> bool& {
            return shortcutProcessor->projectState().kit.pads[0].muted; }, false);
        reset("sample.oneshot", nullptr, [&]() -> bool& {
            return shortcutProcessor->projectState().kit.pads[0].oneShot; }, true);
        reset("sample.mono", nullptr, [&]() -> bool& {
            return shortcutProcessor->projectState().kit.pads[0].mono; }, false);
        chooseBrush("brush.dice");
        for (int index = 0; index < 4; ++index)
            reset(("random.module." + juce::String(index)).toRawUTF8(), nullptr, [&]() -> bool& {
                auto& r = shortcutProcessor->projectState().randomizer;
                return *std::array<bool*, 4>{ &r.noteSelectionEnabled, &r.slicingEnabled,
                    &r.triggeringEnabled, &r.velocityEnabled }[static_cast<std::size_t>(index)];
            }, true);
        reset("random.fillEmpty", nullptr, [&]() -> bool& {
            return shortcutProcessor->projectState().randomizer.fillEmptyOnly; }, false);
        chooseBrush("brush.drum");
        for (int index = 0; index < 2; ++index)
        {
            const auto value = [&]() -> bool& {
                auto& lane = shortcutProcessor->projectState().patterns[0].lanes[0];
                return index == 0 ? lane.solo : lane.muted;
            };
            value() = true;
            const auto click = visualEvent(*shortcutEditor, {33.0f, index == 0 ? 275.5f : 295.5f},
                juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            shortcutEditor->mouseDown(click);
            shortcutEditor->mouseUp(click);
            check(!value(), "Alt-click resets lane Solo or Mute");
            shortcutEditor->mouseDown(click);
            shortcutEditor->mouseUp(click);
            check(!value(), "Alt-click at default lane state does not toggle");
            check(shortcutProcessor->projectEditor().undo() && value()
                      && !shortcutProcessor->projectEditor().canUndo(),
                  "lane reset adds exactly one undo action");
        }
    };
    const std::vector<std::function<void()>> momentarySteps {
        [&] {
            parent.setContentNonOwned(shortcutEditor.get(), true);
            parent.toFront(true);
            juce::Desktop::setMousePosition(shortcutEditor->localPointToGlobal(juce::Point<int>{650, 400}));
            chooseBrush("brush.slicer");
        },
        [&] {
            // Let the window focus event arrive before injecting the first shortcut.
            check(shortcutEditor->hasKeyboardFocus(true), "shortcut editor receives keyboard focus");
            sendKey("keydown", "Alt_L");
        },
        [&] {
            check(juce::ModifierKeys::getCurrentModifiers().isAltDown(),
                  "Alt key event reaches the isolated editor test display");
            check(brushSelected("brush.velocity") && !brushSelected("brush.slicer"),
                  "holding Alt visibly selects Velocity instead of the previous brush");
            saveVisual(*shortcutEditor, output, "alt-velocity-brush.png");
            sendKey("keyup", "Alt_L");
        },
        [&] {
            check(brushSelected("brush.slicer"), "releasing Alt restores the Slicer brush");
            chooseBrush("brush.dice");
            sendKey("keydown", "Alt_L");
        },
        [&] {
            check(brushSelected("brush.velocity") && randomPanelVisible(),
                  "Alt selects Velocity while keeping randomizer controls available for reset");
            sendKey("keydown", "3");
        },
        [&] {
            check(brushSelected("brush.velocity"), "Alt takes priority over a held number shortcut");
            sendKey("keyup", "Alt_L");
        },
        [&] {
            check(brushSelected("brush.slicer"), "releasing Alt resumes the still-held number shortcut");
            sendKey("keyup", "3");
        },
        [&] {
            check(brushSelected("brush.dice") && randomPanelVisible(),
                  "releasing both momentary shortcuts restores Randomizer and its panel");
            chooseBrush("brush.drum");
            sendKey("keydown", "slash");
        },
        [&] {
            check(brushSelected("brush.dice") && randomPanelVisible(),
                  "holding slash selects Randomizer and its panel");
            sendKey("keydown", "Alt_L");
        },
        [&] {
            check(brushSelected("brush.velocity") && randomPanelVisible(),
                  "Alt temporarily overrides the slash brush without hiding its controls");
            sendKey("keyup", "slash");
        },
        [&] {
            check(brushSelected("brush.velocity") && !randomPanelVisible(),
                  "releasing slash preserves held Alt and restores the original panel");
            sendKey("keyup", "Alt_L");
        },
        [&] {
            check(brushSelected("brush.drum") && !shortcutProcessor->projectEditor().canUndo(),
                  "releasing every shortcut restores Drum without editing history");
            sendKey("keydown", "Alt_L");
        },
        [&] { testBooleanResets(); sendKey("keyup", "Alt_L"); },
        [&] { chooseBrush("brush.slicer"); sendKey("keydown", "Alt_L"); },
        [&] {
            check(brushSelected("brush.velocity"), "Alt is active before host state restoration");
            auto restored = shortcutProcessor->projectState();
            restored.ui.randomizerVisible = false;
            const auto bytes = bsl::statecodec::encode(restored);
            shortcutProcessor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        },
        [&] {
            check(brushSelected("brush.velocity"), "held Alt remains active across host state restoration");
            sendKey("keyup", "Alt_L");
        },
        [&] {
            check(brushSelected("brush.slicer"), "Alt release after restoration returns to the base brush");
            sendKey("keydown", "slash");
        },
        [&] {
            check(brushSelected("brush.dice") && randomPanelVisible(), "slash is active before restoration");
            auto restored = shortcutProcessor->projectState();
            restored.ui.randomizerVisible = false;
            const auto bytes = bsl::statecodec::encode(restored);
            shortcutProcessor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        },
        [&] {
            check(brushSelected("brush.dice") && randomPanelVisible(),
                  "held slash temporarily overrides the restored panel");
            sendKey("keyup", "slash");
        },
        [&] {
            check(brushSelected("brush.slicer") && !randomPanelVisible(),
                  "slash release restores the loaded panel and base brush");
            auto& state = shortcutProcessor->projectState();
            state.patterns[0].lanes = { bsl::LaneState::make(4) };
            shortcutOriginalBeats = state.patterns[0].lanes[0].beats;
            state.randomizer.selectedPads = {7};
            state.randomizer.selectedSubdivisions = {3};
            state.randomizer.noteSelectionEnabled = state.randomizer.slicingEnabled = true;
            state.randomizer.triggeringEnabled = state.randomizer.velocityEnabled = true;
            state.randomizer.fillEmptyOnly = false;
            state.randomizer.triggerChance = 1.0f;
            state.randomizer.minimumVelocity = state.randomizer.maximumVelocity = 0.5f;
            openSubdivisionMenu();
        },
        [&] { selectFour(); },
        [&] { clickShortcutGrid({196, 306}); },
        [&] {
            check(shortcutProcessor->projectState().patterns[0].lanes[0].beats[0].slices.size() == 4,
                  "dropdown selection establishes a persistent four-slice Slicer brush");
            undoShortcutEdit();
            sendKey("keydown", "slash");
        },
        [&] {
            check(brushSelected("brush.dice") && randomPanelVisible(),
                  "slash from Slicer displays the tutorial's momentary Randomizer panel");
            clickShortcutGrid({196, 306});
        },
        [&] {
            const auto& beats = shortcutProcessor->projectState().patterns[0].lanes[0].beats;
            check(randomizedTriple(beats[0])
                      && std::equal(beats.begin() + 1, beats.end(), shortcutOriginalBeats.begin() + 1),
                  "held slash plus beat click applies current Randomizer settings only to that beat");
            saveVisual(*shortcutEditor, output, "tutorial-slash-beat-held.png");
            undoShortcutEdit();
            clickShortcutGrid({33, 306});
        },
        [&] {
            const auto& beats = shortcutProcessor->projectState().patterns[0].lanes[0].beats;
            check(std::all_of(beats.begin(), beats.end(), randomizedTriple),
                  "held slash plus Lane Arrow randomizes every beat using current settings");
            saveVisual(*shortcutEditor, output, "tutorial-slash-lane-held.png");
            undoShortcutEdit();
            sendKey("keyup", "slash");
        },
        [&] {
            check(brushSelected("brush.slicer") && !randomPanelVisible(),
                  "slash release after beat and lane edits restores Slicer and sound controls");
            clickShortcutGrid({196, 306});
        },
        [&] {
            check(shortcutProcessor->projectState().patterns[0].lanes[0].beats[0].slices.size() == 4,
                  "slash release preserves the Slicer subdivision chosen in its dropdown");
            saveVisual(*shortcutEditor, output, "tutorial-slash-released-slicer.png");
            undoShortcutEdit();
            openSubdivisionMenu();
        },
        [&] { selectFour(); },
        [&] { sendKey("keydown", "Alt_L"); },
        [&] {
            check(brushSelected("brush.velocity"), "Alt temporarily replaces the four-slice Slicer");
            sendKey("keyup", "Alt_L");
        },
        [&] { clickShortcutGrid({196, 306}); },
        [&] {
            check(brushSelected("brush.slicer")
                      && shortcutProcessor->projectState().patterns[0].lanes[0].beats[0].slices.size() == 4,
                  "Alt release preserves the Slicer subdivision chosen in its dropdown");
            undoShortcutEdit();
            openSubdivisionMenu();
        },
        [&] { selectFour(); },
        [&] { sendKey("keydown", "2"); },
        [&] { clickShortcutGrid({196, 306}); },
        [&] {
            check(shortcutProcessor->projectState().patterns[0].lanes[0].beats[0].slices.size() == 2,
                  "held number temporarily overrides the dropdown's four-slice selection");
            undoShortcutEdit();
            sendKey("keyup", "2");
        },
        [&] { clickShortcutGrid({196, 306}); },
        [&] {
            check(brushSelected("brush.slicer")
                      && shortcutProcessor->projectState().patterns[0].lanes[0].beats[0].slices.size() == 4,
                  "number release restores the Slicer subdivision chosen before the shortcut");
            undoShortcutEdit();
            chooseBrush("brush.drum");
            sendKey("keydown", "Shift_L");
        },
        [&] {
            check(juce::ModifierKeys::getCurrentModifiers().isShiftDown(),
                  "isolated editor observes held Shift for lane reset");
            const auto view = shortcutEditor->createComponentSnapshot(shortcutEditor->getLocalBounds());
            check(similar(view.getPixelAt(33, 306), juce::Colour(look.palette().accentBright)),
                  "held Shift displays the manual's lane-wide action arrow");
            saveVisual(*shortcutEditor, output, "lane-shift-held.png");
            sendKey("keyup", "Shift_L");
        },
        [&] {
            const auto view = shortcutEditor->createComponentSnapshot(shortcutEditor->getLocalBounds());
            check(!similar(view.getPixelAt(33, 306), juce::Colour(look.palette().accentBright)),
                  "releasing lane shortcut restores the ordinary lane controls");
            arrangementMenus.start([&] {
                sampleChooser.start([&] {
                    headerAndFactory.start([&] {
                        initPresetMenus.start([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
                    });
                });
            });
        }
    };
    std::function<void(std::size_t)> testMomentaryShortcuts;
    testMomentaryShortcuts = [&](std::size_t step) {
        momentarySteps[step]();
        if (step == 0)
            focusEditor([&] { testMomentaryShortcuts(1); });
        else if (step + 1 < momentarySteps.size())
            afterInput([&, step] { testMomentaryShortcuts(step + 1); });
    };
    std::function<void(int)> testBarPaste;
    testBarPaste = [&](int meterCase) {
        auto& state = laneProcessor->projectState();
        state.ui.patternMode = false;
        state.activePattern = 0;
        const int copiedBeats = meterCase == 0 ? 4 : 3;
        const int copiedDenominator = meterCase == 0 ? 4 : 8;
        auto& source = state.patterns[0];
        source.bars = {{copiedBeats, copiedDenominator}};
        source.lanes = {bsl::LaneState::make(static_cast<std::size_t>(copiedBeats))};
        source.lanes[0].beats[0] = bsl::BeatState::withSubdivision(5, 2, 0.7f);
        source.lanes[0].beats[0].slices[1].enabled = false;
        const auto copied = source.lanes[0].beats;
        auto& destination = state.patterns[1];
        destination.bars = {{4, 4}, {2, 4}};
        destination.lanes = {bsl::LaneState::make(6), bsl::LaneState::make(6)};
        destination.lanes[1].name = "Extra lane";
        destination.lanes[1].muted = true;
        for (auto& beat : destination.lanes[1].beats)
            beat = bsl::BeatState::withSubdivision(3, 8, 0.4f);
        destination.lanes[0].beats[4] = bsl::BeatState::withSubdivision(7, 6, 0.9f);
        const auto before = destination;
        sendKey("keydown", "c");
        afterInput([&, meterCase, copiedBeats, copiedDenominator, copied, before] {
            laneEditor->mouseDown(visualEvent(*laneEditor, {180.0f, 80.0f}));
            laneProcessor->projectState().activePattern = 1;
            sendKey("keyup", "c");
            sendKey("keydown", "v");
            afterInput([&, meterCase, copiedBeats, copiedDenominator, copied, before] {
                laneEditor->mouseDown(visualEvent(*laneEditor, {180.0f, 80.0f}));
                sendKey("keyup", "v");
                const auto& pasted = laneProcessor->projectState().patterns[1];
                check(pasted.bars.size() == 2 && pasted.bars[0].numerator == copiedBeats
                          && pasted.bars[0].denominator == copiedDenominator,
                      "bar paste replaces the destination meter");
                check(pasted.lanes.size() == 2 && pasted.lanes[1].name == "Extra lane"
                          && pasted.lanes[1].muted,
                      "bar paste preserves destination lane topology and controls");
                check(std::equal(copied.begin(), copied.end(), pasted.lanes[0].beats.begin()),
                      "bar paste overwrites every copied beat attribute");
                bool extraLaneEmpty = true;
                for (int beat = 0; beat < copiedBeats; ++beat)
                    for (const auto& slice : pasted.lanes[1].beats[static_cast<std::size_t>(beat)].slices)
                        extraLaneEmpty &= !slice.enabled;
                check(extraLaneEmpty, "bar paste clears old hits in destination-only lanes");
                for (std::size_t lane = 0; lane < 2; ++lane)
                    check(pasted.lanes[lane].beats.size() == static_cast<std::size_t>(copiedBeats + 2)
                              && std::equal(before.lanes[lane].beats.begin() + 4,
                                            before.lanes[lane].beats.end(),
                                            pasted.lanes[lane].beats.begin() + copiedBeats),
                          "bar paste preserves all later bars across a meter change");
                saveVisual(*laneEditor, output, "bar-paste-cross-pattern.png");
                check(laneProcessor->projectEditor().undo(), "bar paste creates an undo step");
                const auto& restored = laneProcessor->projectState().patterns[1];
                check(restored.bars[0].numerator == 4 && restored.bars[0].denominator == 4
                          && restored.lanes[0].beats == before.lanes[0].beats
                          && restored.lanes[1].beats == before.lanes[1].beats,
                      "one undo restores the full destination bar and all lane content");
                afterInput([&, meterCase] {
                    if (meterCase == 0) testBarPaste(1);
                    else testMomentaryShortcuts(0);
                });
            });
        });
    };
    const auto testPatternPaste = [&] {
        check(laneProcessor->projectEditor().addPattern(), "add pattern paste destination");
        auto& state = laneProcessor->projectState();
        state.ui.patternMode = true;
        state.activePattern = 0;
        state.patterns[0].name = "Source";
        state.patterns[1].name = "Destination";
        state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(5, 2, 0.7f);
        const auto destinationId = state.patterns[1].runtimeId;
        const auto destinationNote = state.patterns[1].triggerNote;
        sendKey("keydown", "c");
        afterInput([&, destinationId, destinationNote] {
            laneEditor->mouseDown(visualEvent(*laneEditor, {1205.0f, 146.0f}));
            sendKey("keyup", "c");
            sendKey("keydown", "v");
            afterInput([&, destinationId, destinationNote] {
                laneEditor->mouseDown(visualEvent(*laneEditor, {1205.0f, 218.0f}));
                sendKey("keyup", "v");
                const auto& patterns = laneProcessor->projectState().patterns;
                check(patterns[1].lanes[0].beats[0] == patterns[0].lanes[0].beats[0],
                      "C/V pattern-pad interaction pastes beat content");
                check(patterns[1].runtimeId == destinationId && destinationId != patterns[0].runtimeId
                          && patterns[1].triggerNote == destinationNote && patterns[1].name == "Destination",
                      "pattern paste preserves destination identity, MIDI trigger and name");
                check(laneProcessor->projectEditor().undo()
                          && laneProcessor->projectState().patterns[1].runtimeId == destinationId,
                      "undo pattern paste preserves destination identity");
                afterInput([&] { testBarPaste(0); });
            });
        });
    };
    const auto testVelocityBrushPaste = [&] {
        auto& pattern = laneProcessor->projectState().patterns[0];
        pattern.bars = {{4, 4}};
        pattern.lanes = { bsl::LaneState::make(4) };
        pattern.lanes[0].beats[0] = bsl::BeatState::withSubdivision(5, 2, 0.7f);
        pattern.lanes[0].beats[0].slices[1].enabled = false;
        pattern.lanes[0].beats[0].slices[2].velocity = 0.31f;
        pattern.lanes[0].beats[1] = bsl::BeatState::withSubdivision(3, 8, 0.4f);
        const auto before = pattern.lanes[0].beats;
        auto* velocity = childWithId<juce::TextButton>(*laneEditor, "brush.velocity");
        check(velocity != nullptr, "velocity brush is available for clipboard regression");
        if (velocity == nullptr) { testPatternPaste(); return; }
        velocity->onClick();
        sendKey("keydown", "c");
        afterInput([&, before] {
            check(juce::KeyPress::isKeyCurrentlyDown('c'), "isolated editor observes held C key");
            const auto copy = visualEvent(*laneEditor, {196.0f, 306.0f});
            laneEditor->mouseDown(copy);
            laneEditor->mouseUp(copy);
            check(laneProcessor->projectState().patterns[0].lanes[0].beats == before
                      && !laneProcessor->projectEditor().canUndo(),
                  "C-click with Velocity brush copies without editing or history");
            const auto baseline = laneEditor->createComponentSnapshot(laneEditor->getLocalBounds());
            saveVisual(*laneEditor, output, "held-v-before-preview.png");
            sendKey("keyup", "c");
            sendKey("keydown", "v");
            afterInput([&, before, baseline] {
                check(juce::KeyPress::isKeyCurrentlyDown('v'), "isolated editor observes held V key");
                const auto hover = visualEvent(*laneEditor, {463.0f, 306.0f}, juce::ModifierKeys::noModifiers);
                laneEditor->mouseMove(hover);
                const auto preview = laneEditor->createComponentSnapshot(laneEditor->getLocalBounds());
                int changedPixels = 0;
                for (int y = 220; y < 392; ++y)
                    for (int x = 380; x < 546; ++x)
                        changedPixels += preview.getPixelAt(x, y) != baseline.getPixelAt(x, y);
                check(changedPixels > 1000, "held V visibly previews the copied beat at the hovered destination");
                check(laneProcessor->projectState().patterns[0].lanes[0].beats == before
                          && !laneProcessor->projectEditor().canUndo(),
                      "paste preview changes neither the pattern nor undo history");
                saveVisual(*laneEditor, output, "held-v-preview.png");
                laneEditor->mouseExit(hover);
                const auto exited = laneEditor->createComponentSnapshot(laneEditor->getLocalBounds());
                bool restored = true;
                for (int y = 220; y < 392; ++y)
                    for (int x = 380; x < 546; ++x)
                        restored = restored && exited.getPixelAt(x, y) == baseline.getPixelAt(x, y);
                check(restored, "leaving the editor removes the paste preview");

                // A fresh editor has no clipboard: V must not fall through to the drum brush.
                {
                    BeatScholarLinuxProcessor emptyProcessor;
                    emptyProcessor.projectState().patterns[0].lanes = { bsl::LaneState::make(4) };
                    BeatScholarLinuxEditor emptyEditor(emptyProcessor);
                    const auto emptyBefore = emptyProcessor.projectState().patterns[0].lanes[0].beats;
                    const auto emptyPaste = visualEvent(emptyEditor, {463.0f, 306.0f});
                    emptyEditor.mouseDown(emptyPaste);
                    emptyEditor.mouseDrag(visualEvent(emptyEditor, {730.0f, 306.0f}));
                    emptyEditor.mouseUp(emptyPaste);
                    check(emptyProcessor.projectState().patterns[0].lanes[0].beats == emptyBefore
                              && !emptyProcessor.projectEditor().canUndo(),
                          "held V with an empty clipboard leaves beats and history unchanged");
                }
                const auto source = visualEvent(*laneEditor, {196.0f, 306.0f});
                laneEditor->mouseDown(source);
                laneEditor->mouseUp(source);
                check(!laneProcessor->projectEditor().canUndo(),
                      "pasting identical beat content does not create an undo action");

                laneEditor->mouseMove(hover);
                const auto paste = visualEvent(*laneEditor, {463.0f, 306.0f});
                laneEditor->mouseDown(paste);
                laneEditor->mouseDrag(visualEvent(*laneEditor, {730.0f, 306.0f}));
                laneEditor->mouseDrag(visualEvent(*laneEditor, {998.0f, 306.0f}));
                laneEditor->mouseDrag(visualEvent(*laneEditor, {730.0f, 306.0f}));
                laneEditor->mouseUp(paste);
                auto expected = before;
                expected[1] = expected[2] = expected[3] = before[0];
                check(laneProcessor->projectState().patterns[0].lanes[0].beats == expected,
                      "held V drag fills each visited beat with every copied attribute");
                sendKey("keyup", "v");
                afterInput([&, before] {
                    saveVisual(*laneEditor, output, "held-v-drag-filled.png");
                    const auto filled = laneProcessor->projectState().patterns[0].lanes[0].beats;
                    check(laneEditor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 26))
                              && laneProcessor->projectState().patterns[0].lanes[0].beats == before
                              && !laneProcessor->projectEditor().canUndo(),
                          "X11 Ctrl+Z restores a multi-beat paste drag in one undo");
                    check(laneEditor->keyPressed(juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 25))
                              && laneProcessor->projectState().patterns[0].lanes[0].beats == filled,
                          "X11 Ctrl+Y restores every pasted beat");
                    check(laneEditor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 26))
                              && laneProcessor->projectState().patterns[0].lanes[0].beats == before
                              && !laneProcessor->projectEditor().canUndo(),
                          "uppercase command key code also restores the complete drag");
                    testPatternPaste();
                });
            });
        });
    };
    std::function<void(int)> testFillMenu;
    testFillMenu = [&](int index) {
        // Exercise every context-menu transform and both ends of each slice group.
        static const std::array<const char*, 13> labels {
            "Fill", "Clear", "Mirror", "Shift Right", "Shift Left",
            "1", "9", "10", "19", "20", "29", "30", "45"};
        static const std::array<int, 8> counts {1, 9, 10, 19, 20, 29, 30, 45};
        auto& state = laneProcessor->projectState();
        auto& beat = state.patterns[0].lanes[0].beats[0];
        beat = bsl::BeatState::withSubdivision(3, 7, 0.3f);
        beat.slices[1].enabled = false;
        beat.slices[2].padIndex = 4;
        beat.slices[2].velocity = 0.8f;
        const auto before = state;
        auto expected = before;
        auto& target = expected.patterns[0].lanes[0].beats[0];
        if (index < 2)
            for (auto& slice : target.slices) slice.enabled = index == 0;
        else if (index == 2)
            std::reverse(target.slices.begin(), target.slices.end());
        else if (index == 3)
            std::rotate(target.slices.begin(), target.slices.end() - 1, target.slices.end());
        else if (index == 4)
            std::rotate(target.slices.begin(), target.slices.begin() + 1, target.slices.end());
        else
            target.setSubdivision(static_cast<std::size_t>(counts[index - 5]));
        const auto revision = laneProcessor->projectEditor().revision();
        const juce::Point<float> point {597.0f, 306.0f};
        juce::Desktop::setMousePosition(laneEditor->localPointToGlobal(point.toInt()));
        laneEditor->mouseDown(visualEvent(*laneEditor, point, juce::ModifierKeys::rightButtonModifier));
        juce::Timer::callAfterDelay(75, [&, index, before, expected, revision] {
            auto press = [](const juce::String& title) {
                std::function<juce::AccessibilityHandler*(juce::Component&)> find;
                find = [&](juce::Component& component) -> juce::AccessibilityHandler* {
                    if (auto* handler = component.getAccessibilityHandler();
                        handler != nullptr && handler->getTitle() == title) return handler;
                    for (auto* child : component.getChildren())
                        if (auto* item = find(*child)) return item;
                    return nullptr;
                };
                auto& desktop = juce::Desktop::getInstance();
                for (int i = desktop.getNumComponents(); --i >= 0;)
                    if (auto* item = find(*desktop.getComponent(i)))
                        return item->getActions().invoke(juce::AccessibilityActionType::press);
                return false;
            };
            auto* menu = juce::Component::getCurrentlyModalComponent();
            check(menu != nullptr, "tutorial beat context menu opens");
            if (index == 0 && menu != nullptr) saveVisual(*menu, output, "tutorial-beat-menu.png");
            if (index >= 5)
            {
                const int count = counts[index - 5];
                const auto group = count < 10 ? "Slice 1-9" : count < 20 ? "Slice 10-19"
                    : count < 30 ? "Slice 20-29" : "Slice 30-45";
                check(press(group), juce::String(group) + " submenu opens");
            }
            juce::Timer::callAfterDelay(75, [&, index, before, expected, revision, press] {
                check(press(labels[index]), juce::String("beat menu selects ") + labels[index]);
                juce::Timer::callAfterDelay(75, [&, index, before, expected, revision] {
                    check(bsl::statecodec::encode(laneProcessor->projectState()) == bsl::statecodec::encode(expected),
                          juce::String("beat menu changes only the target: ") + labels[index]);
                    check(laneProcessor->projectEditor().revision() == revision + 1,
                          "beat context-menu action makes exactly one edit");
                    check(laneProcessor->projectEditor().undo()
                              && bsl::statecodec::encode(laneProcessor->projectState()) == bsl::statecodec::encode(before),
                          "one undo restores the complete beat menu edit");
                    juce::PopupMenu::dismissAllActiveMenus();
                    if (index + 1 < static_cast<int>(labels.size())) testFillMenu(index + 1);
                    else focusEditor(testVelocityBrushPaste);
                });
            });
        });
    };
    std::function<void(int)> testArrow;
    testArrow = [&](int index) {
        if (index >= 3)
        {
            // Exercise both a removed beat and a still-valid same-numbered beat.
            const auto event = visualEvent(*laneEditor, { index == 3 ? 998.0f : 597.0f, 306.0f }, juce::ModifierKeys::rightButtonModifier);
            laneEditor->mouseDown(event);
            auto shorter = bsl::ProjectState::makeDefault();
            shorter.patterns[0].bars = {{1, 4}};
            shorter.patterns[0].lanes = {bsl::LaneState::make(1)};
            const auto bytes = bsl::statecodec::encode(shorter);
            laneProcessor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
            juce::Timer::callAfterDelay(75, [&, index] {
                auto* menu = juce::Component::getCurrentlyModalComponent();
                check(menu != nullptr, "beat menu remains open across host state recall");
                if (menu != nullptr)
                {
                    // Select Fill, skipping disabled Paste, so this really attempts an edit.
                    for (int key = 0; key < 3; ++key)
                        menu->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
                    menu->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
                }
                juce::Timer::callAfterDelay(75, [&, index] {
                    check(!laneProcessor->projectEditor().canUndo(),
                          index == 3 ? "stale beat menu action after shorter host recall is a no-op"
                                     : "same-index beat menu action after host recall is a no-op");
                    check(laneProcessor->projectState().patterns[0].lanes[0].beats.size() == 1,
                          "stale menu preserves recalled pattern dimensions");
                    juce::PopupMenu::dismissAllActiveMenus();
                    if (index == 3) testArrow(4);
                    else testFillMenu(0);
                });
            });
            return;
        }
        auto& beats = laneProcessor->projectState().patterns[0].lanes[0].beats;
        for (auto& beat : beats)
        {
            beat = bsl::BeatState::withSubdivision(3, 0, 0.8f);
            for (std::size_t slice = 0; slice < 3; ++slice)
                beat.slices[slice].padIndex = static_cast<std::uint8_t>(slice);
        }
        sendKey("keydown", keys[static_cast<std::size_t>(index)]);
        afterInput([&, index] {
            check(juce::KeyPress::isKeyCurrentlyDown(keyCodes[static_cast<std::size_t>(index)]),
                  "isolated editor observes held arrow key " + juce::String(keys[static_cast<std::size_t>(index)]));
            const auto arrowView = laneEditor->createComponentSnapshot(laneEditor->getLocalBounds());
            check(similar(arrowView.getPixelAt(33, 306), juce::Colour(look.palette().accentBright)),
                  "held arrow displays the manual's lane-wide action arrow");
            if (index == 0) saveVisual(*laneEditor, output, "lane-arrow-held.png");
            const auto original = laneProcessor->projectState().patterns[0].lanes[0].beats;
            auto expected = original;
            for (auto& beat : expected)
                if (index < 2) beat.rotate(index == 0 ? -1 : 1); else beat.mirror();
            const auto event = visualEvent(*laneEditor, { 20.0f, 306.0f }, juce::ModifierKeys::leftButtonModifier);
            laneEditor->mouseDown(event);
            laneEditor->mouseUp(event);
            check(laneProcessor->projectState().patterns[0].lanes[0].beats == expected,
                  "held arrow plus lane click transforms every beat");
            check(laneProcessor->projectEditor().undo()
                      && laneProcessor->projectState().patterns[0].lanes[0].beats == original,
                  "lane transform is one undoable action");
            sendKey("keyup", keys[static_cast<std::size_t>(index)]);
            afterInput([&, index] { testArrow(index + 1); });
        });
    };
    // Let X11 map the peer and deliver focus before asserting keyboard state.
    juce::Timer::callAfterDelay(200, [&] {
      for (auto* control : std::array<juce::Component*, 3> { &button, &rotary, &linear })
      {
        parent.grabKeyboardFocus();
        const auto before = control->createComponentSnapshot(control->getLocalBounds());
        control->grabKeyboardFocus();
        check(control->hasKeyboardFocus(false), "keyboard can focus the control");
        const auto after = control->createComponentSnapshot(control->getLocalBounds());
        int different = 0;
        for (int y = 0; y < before.getHeight(); ++y)
            for (int x = 0; x < before.getWidth(); ++x)
                different += before.getPixelAt(x, y) != after.getPixelAt(x, y);
        check(different > 4, "keyboard focus has a visible indicator");
      }
      if (juce::SystemStats::getEnvironmentVariable("BSL_ISOLATED_GUI_TESTS", "") == "1")
      {
          // The focus-indicator controls are direct window children, not
          // content. Remove them before testing the isolated editor's keys.
          for (auto* control : std::array<juce::Component*, 3> { &button, &rotary, &linear })
              parent.removeChildComponent(control);
          parent.setContentNonOwned(laneEditor.get(), true);
          parent.toFront(true);
          focusEditor([&] { testArrow(0); });
      }
      else
      {
          std::cout << "Isolated keyboard injection skipped outside the Xvfb harness\n";
          juce::MessageManager::getInstance()->stopDispatchLoop();
      }
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    parent.clearContentComponent();
    parent.setLookAndFeel(nullptr);
}

juce::String bypassParameterId(int pad, const char* effect)
{
    return "pad" + juce::String(pad + 1) + effect + "Bypass";
}

bsl::ProjectState makeBypassTestProject(int padIndex)
{
    auto state = bsl::ProjectState::makeDefault();
    state.global.sequencerEnabled = false;
    juce::AudioBuffer<float> source(1, 4096);
    for (int frame = 0; frame < source.getNumSamples(); ++frame)
        source.setSample(0, frame, 0.3f + 0.2f * std::sin(float(frame) * 0.71f));
    juce::MemoryBlock wav;
    juce::WavAudioFormat format;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(
        new juce::MemoryOutputStream(wav, false), 48000, 1, 24, {}, 0));
    check(writer != nullptr && writer->writeFromAudioSampleBuffer(source, 0, source.getNumSamples()),
          "create bypass test sample");
    writer.reset();
    // Pad zero also anchors the custom kit so preparation never imports a
    // private factory kit into this deterministic processor test.
    for (const auto pad : { 0, padIndex })
    {
        auto& sound = state.kit.pads[static_cast<std::size_t>(pad)];
        sound.customSampleName = "automation-test.wav";
        sound.customSampleSha256 = juce::SHA256(wav).toHexString().toStdString();
        sound.customSampleBase64 = juce::Base64::toBase64(wav.getData(), wav.getSize()).toStdString();
    }
    return state;
}


void testModuleSwitchesReachEditorAndProcessor(const juce::File& output)
{
    std::array<float, 4> peaks {};
    std::size_t index = 0;
    for (const bool aux : { false, true })
        for (const bool master : { false, true })
        {
            auto processor = std::make_unique<BeatScholarLinuxProcessor>();
            auto state = makeBypassTestProject(0);
            state.global.auxEnabled = aux;
            state.global.masterEnabled = master;
            state.master.cutoff = 0.15f;
            state.master.gain = 0.25f;
            auto& pad = state.kit.pads[0];
            pad.filterEnabled = false;
            pad.hpfBypass = true; // Isolate module gain from the always-on 10 Hz HPF.
            pad.independentDistortion = true;
            pad.distortionMix = 0.7f;
            pad.distortionDrive = 0.8f;
            const auto bytes = bsl::statecodec::encode(state);
            processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
            processor->setRateAndBufferSizeDetails(48000, 64);
            processor->prepareToPlay(48000, 64);
            BeatScholarLinuxProcessorTestAccess::prepareDistortion(*processor);
            auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
            editor->setSize(1280, 720);
            for (int control = 0; control < 10; ++control)
            {
                auto* slider = childWithId<juce::Slider>(*editor, ("effect." + juce::String(control)).toRawUTF8());
                check(slider != nullptr && slider->isEnabled() == (control < 6 || aux),
                      "AUX switch enables its controls without disabling pad inserts");
            }
            for (int control = 0; control < 5; ++control)
            {
                auto* slider = childWithId<juce::Slider>(*editor, ("master." + juce::String(control)).toRawUTF8());
                check(slider != nullptr && slider->isEnabled() == (control == 4 || master),
                      "Master switch disables processing controls while keeping volume active");
            }
            auto* routing = childWithId<juce::TextButton>(*editor, "routing.mode");
            check(routing != nullptr && !routing->getToggleState()
                      && !processor->projectState().global.multiOutput,
                  "all AUX/Master switch combinations preserve Stereo routing");
            juce::AudioBuffer<float> audio(processor->getTotalNumOutputChannels(), 64);
            juce::MidiBuffer midi;
            for (int silentBlock = 0; silentBlock < 75; ++silentBlock)
                processor->processBlock(audio, midi);
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, juce::uint8(127)), 0);
            processor->processBlock(audio, midi);
            peaks[index++] = audio.getMagnitude(0, 0, 64);
            const auto screenshotName = "modules-aux-" + juce::String(aux ? 1 : 0)
                                            + "-master-" + juce::String(master ? 1 : 0) + ".png";
            saveVisual(*editor, output, screenshotName.toRawUTF8());
            editor.reset();
            processor->releaseResources();
        }
    // A 0.5-peak sample, native 0.3 pad trim, centre pan and 0.25
    // master volume produce approximately 0.0265 on either channel.
    std::cout << "Module-switch peaks: " << peaks[0] << ", " << peaks[1]
              << ", " << peaks[2] << ", " << peaks[3] << '\n';
    check(std::abs(peaks[0] - 0.5f * 0.3f * std::sqrt(0.5f) * 0.25f) < 0.0001f
              && peaks[1] < peaks[0] * 0.1f,
          "restored Master flag bypasses the real processor LPF and retains volume");
    check(peaks[2] > peaks[0] * 1.1f,
          "restored AUX flag independently reaches the real processor parallel path");
    check(peaks[3] < peaks[2] * 0.1f,
          "Master remains independent when AUX is active");
}

void testSamplerDisableClearsVoicesAndTails()
{
    for (const bool oneShot : { false, true })
        for (const bool wet : { false, true })
        {
            auto processor = std::make_unique<BeatScholarLinuxProcessor>();
            auto state = makeBypassTestProject(0);
            state.kit.pads[0].oneShot = oneShot;
            if (wet)
            {
                state.kit.pads[0].room = 1.0f;
                state.kit.pads[0].reverb = 1.0f;
            }
            const auto bytes = bsl::statecodec::encode(state);
            processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
            processor->setRateAndBufferSizeDetails(48000, 64);
            processor->prepareToPlay(48000, 64);
            if (wet)
            {
                BeatScholarLinuxProcessorTestAccess::prepareAmbience(*processor);
                check(processor->hasFactoryAmbience(), "synthetic Room and Plate are ready");
            }
            settleMainGain(*processor, 64);
            juce::AudioBuffer<float> audio(processor->getTotalNumOutputChannels(), 64);
            juce::MidiBuffer midi;
            const auto magnitude = [&] { return audio.getMagnitude(0, 0, 64); };
            const auto setSampler = [&](bool enabled) {
                processor->projectState().global.samplerEnabled = enabled;
                processor->markProjectDirty();
                processor->publishProjectStateIfNeeded();
            };
            const auto label = juce::String(oneShot ? "one-shot" : "gated")
                               + (wet ? " wet tail" : " active voice");
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, juce::uint8(127)), 0);
            processor->processBlock(audio, midi);
            check(magnitude() > 0.1f, "sampler starts the test note: " + label);
            // For the wet case let the source finish, leaving only effect history.
            for (int block = 1; wet && block < 64; ++block)
            {
                midi.clear();
                processor->processBlock(audio, midi);
            }
            if (wet)
            {
                // Cross the sample boundary and release a gated note before
                // disabling the sampler, so this case exercises effect history
                // independently of the active-voice case above.
                midi.clear();
                midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
                processor->processBlock(audio, midi);
                check(magnitude() > 0.001f, "effect tail remains after the source ends: " + label);
            }
            setSampler(false);
            float disabledPeak = 0.0f;
            for (int block = 0; block < 4; ++block)
            {
                midi.clear();
                if (block == 0)
                    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
                processor->processBlock(audio, midi);
                disabledPeak = std::max(disabledPeak, magnitude());
            }
            check(disabledPeak == 0.0f, "disabled sampler is silent: " + label);
            setSampler(true);
            float resumedPeak = 0.0f;
            for (int block = 0; block < (wet ? 512 : 128); ++block)
            {
                midi.clear();
                processor->processBlock(audio, midi);
                resumedPeak = std::max(resumedPeak, magnitude());
            }
            check(resumedPeak == 0.0f, "re-enabled sampler does not revive old audio: " + label);
            midi.clear();
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, juce::uint8(127)), 0);
            processor->processBlock(audio, midi);
            check(magnitude() > 0.1f, "re-enabled sampler responds to a fresh note: " + label);
            processor->releaseResources();
        }
}

void testSparseMultiOutputBusMapping()
{
    for (const int pad : {0, 7, 15})
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto state = makeBypassTestProject(pad);
        state.global.multiOutput = true;
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        auto layout = processor->getBusesLayout();
        layout.outputBuses.getReference(pad + 1) = juce::AudioChannelSet::stereo();
        check(processor->setBusesLayout(layout), "host can activate one isolated drum output");
        processor->setRateAndBufferSizeDetails(48000, 64);
        processor->prepareToPlay(48000, 64);
        check(processor->getTotalNumOutputChannels() == 4, "sparse multi layout packs active channels only");
        juce::AudioBuffer<float> audio(4, 64);
        juce::MidiBuffer midi;
        processor->triggerPad(static_cast<std::size_t>(pad), 1.0f);
        processor->processBlock(audio, midi);
        check(audio.getMagnitude(0, 0, 64) == 0.0f && audio.getMagnitude(1, 0, 64) == 0.0f,
              "multi mode leaves master bus silent");
        check(audio.getMagnitude(2, 0, 64) > 0.1f && audio.getMagnitude(3, 0, 64) > 0.1f,
              "isolated host bus receives its own pad with earlier buses disabled: " + juce::String(pad + 1));
        processor->releaseResources();
    }
}

std::vector<float> renderBypassTest(bsl::ProjectState state, int pad, const juce::String& parameter,
                                   bool bypass)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->setRateAndBufferSizeDetails(48000, 64);
    const auto bytes = bsl::statecodec::encode(state);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    processor->prepareToPlay(48000, 64);
    if (auto* toggle = processor->parameters().getParameter(parameter))
        toggle->setValueNotifyingHost(bypass ? 1.0f : 0.0f);
    std::vector<float> result;
    juce::AudioBuffer<float> buffer(processor->getTotalNumOutputChannels(), 64);
    juce::MidiBuffer midi;
    for (int block = 0; block < 4; ++block)
    {
        midi.clear();
        if (block == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 36 + pad, juce::uint8(127)), 0);
        processor->processBlock(buffer, midi);
        result.insert(result.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 64);
    }
    processor->releaseResources();
    return result;
}


void testMasterResonanceAltReset()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->projectState().master.resonance = 0.9f;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* resonance = childWithId<juce::Slider>(*editor, "master.1");
    check(resonance != nullptr && resonance->isVisible(), "master resonance reset control exists");
    if (resonance == nullptr) return;
    const auto click = visualEvent(*editor, resonance->getBounds().getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    editor->mouseDown(click);
    editor->mouseUp(click);
    check(std::abs(processor->projectState().master.resonance - 7.0f / 24.0f) < 1.0e-7f
              && std::abs(resonance->getValue() - 7.0 / 24.0) < 1.0e-7,
          "Alt-click restores native master Q 0.8 without slider quantization");
    check(processor->projectEditor().undo()
              && processor->projectState().master.resonance == 0.9f,
          "master resonance reset is undoable");
}

void testPadResonanceAltReset()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->projectState().kit.pads[0].resonance = 0.9f;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* resonance = childWithId<juce::Slider>(*editor, "effect.2");
    check(resonance != nullptr && resonance->isVisible(), "pad resonance reset control exists");
    if (resonance == nullptr) return;
    const auto click = visualEvent(*editor, resonance->getBounds().getCentre().toFloat(),
        juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    editor->mouseDown(click);
    editor->mouseUp(click);
    check(std::abs(processor->projectState().kit.pads[0].resonance
                       - bsl::defaultFilterResonance) < 1.0e-7f,
          "Alt-click restores the native pad Q 0.8 resonance default");
    check(processor->projectEditor().undo()
              && processor->projectState().kit.pads[0].resonance == 0.9f,
          "pad resonance reset is undoable");
}

void testDocumentedPadBypassAutomation(const juce::File& output)
{
    constexpr std::array effects { "Lpf", "Hpf", "Drive", "Crush" };
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    bool allPresent = true;
    for (int pad = 0; pad < 16; ++pad)
        for (const auto* effect : effects)
        {
            auto* parameter = processor->parameters().getParameter(bypassParameterId(pad, effect));
            check(parameter != nullptr && parameter->isAutomatable() && parameter->isBoolean(),
                  "documented host bypass exists: " + bypassParameterId(pad, effect));
            allPresent &= parameter != nullptr;
        }
    if (!allPresent) return;
    check(processor->parameters().getParameter("swing")->getParameterIndex() == 0
              && processor->parameters().getParameter("masterGain")->getParameterIndex() == 3,
          "new bypass parameters preserve existing host parameter ordering");

    for (const int pad : { 0, 15 })
    {
        const auto source = makeBypassTestProject(pad);
        const auto dry = renderBypassTest(source, pad, {}, false);
        for (int effect = 0; effect < 4; ++effect)
        {
            auto processed = source;
            auto& sound = processed.kit.pads[static_cast<std::size_t>(pad)];
            if (effect == 0) sound.cutoff = 0.08f;
            if (effect == 1) sound.highPass = 0.95f;
            if (effect == 2) { sound.driveEnabled = true; sound.independentDistortion = true; sound.driveGainDb = 18.0f; }
            if (effect == 3) sound.crush = 0.85f;
            const auto id = bypassParameterId(pad, effects[static_cast<std::size_t>(effect)]);
            check(renderBypassTest(processed, pad, id, false) != dry,
                  id + " has an audible effect when enabled");
            // The default LPF and HPF are active at 20 kHz and 10 Hz.
            // A bypass reference must bypass the corresponding default filter.
            const auto bypassReference = effect == 0 || effect == 1
                ? renderBypassTest(source, pad, id, true) : dry;
            check(renderBypassTest(processed, pad, id, true) == bypassReference,
                  id + " provides sample-exact bypass without changing effect values");
            check(renderBypassTest(processed, pad, bypassParameterId((pad + 1) % 16,
                      effects[static_cast<std::size_t>(effect)]), true) != dry,
                  id + " is not bypassed by another pad's automation");
        }
    }

    const auto source = makeBypassTestProject(0);
    const auto bytes = bsl::statecodec::encode(source);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    for (int pad = 0; pad < 16; ++pad)
        for (const auto* effect : effects)
            processor->parameters().getParameter(bypassParameterId(pad, effect))
                ->setValueNotifyingHost(pad % 2 == 0 ? 1.0f : 0.0f);
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);
    auto restored = std::make_unique<BeatScholarLinuxProcessor>();
    restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const auto checkRestored = [&effects](BeatScholarLinuxProcessor& candidate, const char* medium) {
        for (int pad = 0; pad < 16; ++pad)
            for (const auto* effect : effects)
                check(candidate.parameters().getRawParameterValue(bypassParameterId(pad, effect))->load()
                          == (pad % 2 == 0 ? 1.0f : 0.0f),
                      juce::String(medium) + " retains each automated bypass state");
    };
    checkRestored(*restored, "host state");
    const auto preset = output.getChildFile("bypass-roundtrip.bslpreset");
    const auto kit = output.getChildFile("bypass-roundtrip.bslkit");
    check(processor->savePresetFile(preset) && restored->loadPresetFile(preset), "bypass preset round trip");
    checkRestored(*restored, "preset");
    check(processor->saveKitFile(kit), "save automated bypass kit");
    restored->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    check(restored->loadKitFile(kit), "load automated bypass kit");
    checkRestored(*restored, "kit");
    restored->projectState().global.kitLocked = true;
    const auto neutralPreset = output.getChildFile("neutral-bypass.bslpreset");
    neutralPreset.replaceWithText(bsl::statecodec::encodePresetJson(source));
    check(restored->loadPresetFile(neutralPreset), "load preset while kit locked");
    checkRestored(*restored, "kit lock");
}



void testBeatDragPublishesEachChangeAndOwnsOneUndo()
{
    struct PlayHead final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };
    for (const bool velocityDrag : { false, true })
    for (const bool move : { false, true })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto state = makeBypassTestProject(0);
        state.global.sequencerEnabled = true;
        state.global.samplerEnabled = false;
        state.patterns[0].lanes = { bsl::LaneState::make(4) };
        for (auto& beat : state.patterns[0].lanes[0].beats)
            beat.slices[0].enabled = false;
        state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(1, 0, 0.5f);
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000.0, 24000);
        processor->prepareToPlay(48000.0, 24000);
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(1280, 720);
        const juce::ModifierKeys modifiers(juce::ModifierKeys::leftButtonModifier
            | (velocityDrag ? juce::ModifierKeys::altModifier : juce::ModifierKeys::commandModifier));
        const juce::Point<float> start { 196.0f, 306.0f };
        editor->mouseDown(visualEvent(*editor, start, modifiers));
        processor->publishProjectStateIfNeeded(); // A timer tick may precede the first movement.
        if (move)
            for (int step = 1; step <= 2; ++step)
            {
                editor->mouseDrag(visualEvent(*editor,
                    { start.x, start.y - float(step * (velocityDrag ? 14 : 12)) }, modifiers));
                processor->publishProjectStateIfNeeded(); // Each movement must reach the audio thread.
            }
        editor->mouseUp(visualEvent(*editor, start, modifiers));
        if (!move)
            check(!processor->projectEditor().canUndo(),
                  "modifier click without a changed beat creates no empty Undo step");
        else
        {
            PlayHead playHead;
            playHead.info.setIsPlaying(true);
            playHead.info.setTimeInSamples(0);
            playHead.info.setPpqPosition(0.0);
            playHead.info.setBpm(120.0);
            processor->setPlayHead(&playHead);
            juce::AudioBuffer<float> audio(2, 24000);
            juce::MidiBuffer midi;
            processor->processBlock(audio, midi);
            int noteCount = 0;
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                {
                    ++noteCount;
                    if (velocityDrag)
                        check(event.getMessage().getVelocity() == 89,
                              "playback uses the final velocity after multiple published drag updates");
                }
            check(noteCount == (velocityDrag ? 1 : 3),
                  "playback uses the final subdivision after multiple published drag updates");
            processor->setPlayHead(nullptr);
            check(processor->projectEditor().undo(), "beat drag is undoable");
            const auto& original = processor->projectState().patterns[0].lanes[0].beats[0];
            check(original.slices.size() == 1 && original.slices[0].velocity == 0.5f
                      && !processor->projectEditor().canUndo(),
                  "one Undo restores the complete beat drag gesture");
            check(processor->projectEditor().redo(), "beat drag is redoable");
            const auto& changed = processor->projectState().patterns[0].lanes[0].beats[0];
            check(velocityDrag ? std::abs(changed.slices[0].velocity - 0.7f) < 1e-6f
                               : changed.slices.size() == 3,
                  "one Redo restores the final dragged value");
        }
        processor->releaseResources();
    }
}

void testRandomizerPaintStroke(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns[0].lanes = { bsl::LaneState::make(4) };
    const auto original = state.patterns[0].lanes[0].beats;
    auto& settings = state.randomizer;
    settings.selectedPads = {7};
    settings.selectedSubdivisions = {3};
    settings.noteSelectionEnabled = settings.slicingEnabled = true;
    settings.triggeringEnabled = settings.velocityEnabled = true;
    settings.fillEmptyOnly = false;
    settings.triggerChance = 1.0f;
    settings.minimumVelocity = settings.maximumVelocity = 0.5f;
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);
    childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
    editor->mouseDown(visualEvent(*editor, {196, 306}));
    processor->publishProjectStateIfNeeded();
    editor->mouseDrag(visualEvent(*editor, {463, 306}));
    processor->publishProjectStateIfNeeded();
    editor->mouseDrag(visualEvent(*editor, {730, 306}));
    processor->publishProjectStateIfNeeded();
    // A revisited pie is not rerolled, even across a different newly made slice.
    settings.selectedPads = {8};
    editor->mouseDrag(visualEvent(*editor, {423, 306}));
    editor->mouseUp(visualEvent(*editor, {423, 306}));
    const auto painted = state.patterns[0].lanes[0].beats;
    check(std::all_of(painted.begin(), painted.begin() + 3, [](const auto& beat) {
        return beat.slices.size() == 3
            && std::all_of(beat.slices.begin(), beat.slices.end(), [](const auto& slice) {
                return slice.enabled && slice.padIndex == 7 && slice.velocity == 0.5f;
            });
    }) && painted[3] == original[3],
        "Randomizer drag paints each entered beat once and leaves unvisited beats unchanged");
    saveVisual(*editor, output, "tutorial-randomizer-drag.png");
    check(processor->projectEditor().undo()
              && state.patterns[0].lanes[0].beats == original
              && !processor->projectEditor().canUndo(),
          "one Undo restores the entire Randomizer stroke");
    check(processor->projectEditor().redo()
              && state.patterns[0].lanes[0].beats == painted,
          "one Redo restores the entire Randomizer stroke");
    editor->mouseDown(visualEvent(*editor, {196, 306}));
    editor->mouseUp(visualEvent(*editor, {196, 306}));
    check(state.patterns[0].lanes[0].beats[0].slices[0].padIndex == 8,
          "a fresh Randomizer stroke can visit a previously painted beat again");
    auto recalled = bsl::ProjectState::makeDefault();
    recalled.patterns[0].lanes = { bsl::LaneState::make(4) };
    editor->mouseDown(visualEvent(*editor, {196, 306}));
    const auto bytes = bsl::statecodec::encode(recalled);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    editor->mouseDrag(visualEvent(*editor, {463, 306}));
    editor->mouseUp(visualEvent(*editor, {463, 306}));
    check(processor->projectState().patterns[0].lanes[0].beats
              == recalled.patterns[0].lanes[0].beats
              && !processor->projectEditor().canUndo(),
          "host recall cancels an in-progress Randomizer stroke before its next drag");
}

void testHostRecallCancelsStalePatternInteractions()
{
    for (const bool velocityDrag : { false, true })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        processor->projectState().patterns[0].lanes = { bsl::LaneState::make(4) };
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        if (velocityDrag)
            childWithId<juce::TextButton>(*editor, "brush.velocity")->onClick();
        editor->mouseDown(visualEvent(*editor, {196.0f, 306.0f}));
        auto recalled = bsl::ProjectState::makeDefault();
        recalled.patterns[0].lanes = { bsl::LaneState::make(4) };
        recalled.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(3, 2, 0.35f);
        const auto bytes = bsl::statecodec::encode(recalled);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        if (velocityDrag)
            editor->mouseDrag(visualEvent(*editor, {196.0f, 220.0f}));
        else
            editor->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
        const auto& beat = processor->projectState().patterns[0].lanes[0].beats[0];
        check(!processor->projectEditor().canUndo(),
              "host recall cancels the old keyboard transform target before the timer tick");
        check(std::abs(beat.slices[0].velocity - 0.35f) < 1e-6f,
              "host recall cancels an old velocity drag before the timer tick");
    }
}

void testRandomizerPanelRecall()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    {
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
    }
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* random = childWithId<juce::Slider>(*editor, "random.2");
    check(random != nullptr && random->isVisible(), "randomizer panel survives editor reopen");
    juce::MemoryBlock bytes;
    processor->getStateInformation(bytes);
    auto restored = std::make_unique<BeatScholarLinuxProcessor>();
    auto restoredEditor = std::make_unique<BeatScholarLinuxEditor>(*restored);
    restored->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    // A keyboard event must synchronize the newly recalled panel immediately.
    restoredEditor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    random = childWithId<juce::Slider>(*restoredEditor, "random.2");
    check(random != nullptr && random->isVisible(), "host recall restores the saved randomizer panel");
}

void testRandomizerInitUndoAndHostRecall(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.randomizer.selectedPads = { 3, 5 };
    state.randomizer.selectedSubdivisions = { 7, 13 };
    state.randomizer.noteSelectionEnabled = false;
    state.randomizer.slicingEnabled = false;
    state.randomizer.triggeringEnabled = false;
    state.randomizer.velocityEnabled = false;
    state.randomizer.fillEmptyOnly = true;
    state.randomizer.triggerChance = 0.25f;
    state.randomizer.spacing = 0.75f;
    state.randomizer.minimumVelocity = 0.3f;
    state.randomizer.maximumVelocity = 0.6f;
    state.ui.randomizerFile = "Edited Randomizer.bslrandom";
    state.patterns[0].lanes[0].beats[0] = bsl::BeatState::withSubdivision(3, 5, 0.6f);
    state.kit.pads[0].name = "Keep this sound";
    const auto previous = bsl::statecodec::encodeRandomizerJson(state.randomizer);
    const auto expected = bsl::statecodec::encodeRandomizerJson(bsl::RandomizerSettings::makeInitPreset());
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
    processor->resetRandomizerToInit();
    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    const auto checkInit = [&](BeatScholarLinuxProcessor& p, BeatScholarLinuxEditor& e) {
        check(bsl::statecodec::encodeRandomizerJson(p.projectState().randomizer) == expected
                  && p.projectState().ui.randomizerFile.empty(),
              "Randomizer Init resets every setting and clears the old preset identity");
        auto* button = childWithId<juce::TextButton>(e, "random.preset");
        check(button != nullptr && button->getButtonText() == "Init Preset",
              "Randomizer Init displays its name after reset and host recall");
        const auto& beat = p.projectState().patterns[0].lanes[0].beats[0];
        check(beat.slices.size() == 3 && beat.slices[0].padIndex == 5
                  && p.projectState().kit.pads[0].name == "Keep this sound",
              "Randomizer Init preserves the pattern and kit");
    };
    checkInit(*processor, *editor);
    saveVisual(*editor, output, "randomizer-init.png");
    juce::MemoryBlock bytes;
    processor->getStateInformation(bytes);
    auto recalled = std::make_unique<BeatScholarLinuxProcessor>();
    recalled->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    auto recalledEditor = std::make_unique<BeatScholarLinuxEditor>(*recalled);
    checkInit(*recalled, *recalledEditor);
    childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
    check(bsl::statecodec::encodeRandomizerJson(processor->projectState().randomizer) == previous
              && processor->projectState().ui.randomizerFile == "Edited Randomizer.bslrandom"
              && childWithId<juce::TextButton>(*editor, "random.preset")->getButtonText() == "Edited Randomizer",
          "one Undo restores the pre-Init settings and preset identity");
    childWithId<juce::TextButton>(*editor, "toolbar.redo")->onClick();
    checkInit(*processor, *editor);
}

void testPresetNamesSurviveEditorAndHostRecall(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    const auto kitA = output.getChildFile("Named Kit A.bslkit");
    const auto kitB = output.getChildFile("Named Kit B.bslkit");
    const auto preset = output.getChildFile("Named Preset.bslpreset");
    const auto random = output.getChildFile("Named Randomizer.bslrandom");
    check(processor->saveKitFile(kitA) && processor->saveRandomizerFile(random)
          && processor->savePresetFile(preset), "save named content for editor recall");
    const auto blockingParent = output.getChildFile("preset-parent-is-a-file");
    check(blockingParent.replaceWithText("fixture")
          && !processor->savePresetFile(blockingParent.getChildFile("cannot-save.bslpreset")),
          "failed preset save does not replace content identity");
    const auto checkNames = [](BeatScholarLinuxEditor& editor) {
        auto* presetButton = childWithId<juce::TextButton>(editor, "toolbar.preset");
        auto* kitButton = childWithId<juce::TextButton>(editor, "kit.menu");
        check(presetButton != nullptr && presetButton->getButtonText() == "Named Preset",
              "preset identity survives creation of a fresh editor");
        check(kitButton != nullptr && kitButton->getButtonText() == "Named Kit A",
              "kit identity survives creation of a fresh editor");
        childWithId<juce::TextButton>(editor, "brush.dice")->onClick();
        auto* randomButton = childWithId<juce::TextButton>(editor, "random.preset");
        check(randomButton != nullptr && randomButton->getButtonText() == "Named Randomizer",
              "randomizer preset identity survives editor reopen and host recall");
    };
    {
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        checkNames(*editor);
    }
    juce::MemoryBlock bytes;
    processor->getStateInformation(bytes);
    auto restored = std::make_unique<BeatScholarLinuxProcessor>();
    restored->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*restored);
    checkNames(*editor);
    // A host state must retain a useful name even when its external preset file
    // is no longer present; all playable state is already embedded.
    check(preset.deleteFile(), "remove only the test-owned external preset fixture");
    editor.reset();
    editor = std::make_unique<BeatScholarLinuxEditor>(*restored);
    checkNames(*editor);
    check(restored->saveKitFile(kitB) && restored->loadKitFile(kitA)
          && restored->loadKitFile(kitB), "load second named kit for undo");
    auto* undo = childWithId<juce::TextButton>(*editor, "toolbar.undo");
    if (undo != nullptr) undo->onClick();
    checkNames(*editor);
}

void testLegacyPresetKitIdentity(const juce::File& output)
{
    const auto previous = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", "");
    const auto xdg = output.getChildFile("legacy-kit-identity-xdg");
    check(xdg.deleteRecursively(), "clear test-owned kit metadata from prior runs");
    setenv("XDG_DATA_HOME", xdg.getFullPathName().toRawUTF8(), 1);
    const auto kits = bsl::dataRoot().getChildFile("Factory/Kits/Classic");
    check(kits.createDirectory().wasOk(), "create isolated legacy kit metadata");
    const auto kit = kits.getChildFile("Tutorial Kit.json");
    // Tutorial preset and kit names can disagree internally; the id is stable.
    const juce::String metadata = R"json({"id":"tutorial-kit-id","name":"Internal Name",
      "Pads":{"Pads":[{"name":"Do not replace preset sounds"}]}})json";
    check(kit.replaceWithText(metadata), "write synthetic factory kit");
    const auto preset = output.getChildFile("Tutorial Identity.json");
    const juce::String json = R"json({"Sampler":{"id":"tutorial-kit-id","name":"Old Name",
      "Pads":{"Pads":[{"name":"Preset Kick","Params":{"Aux":{"Aux1":"41%","Aux2":"17%"}}}]}},
      "Seq":{"Patterns":{"Patterns":[{"name":"Tutorial","Events":{
        "LoopTime":{"Bars":[{"num":1,"denum":4}]},
        "Sequences":[{"Sequences":[{"Steps":[{"enabled":true,"Event":{"noteNum":36,"velocity":0.8}}]}]}]
      }}]}}})json";
    check(preset.replaceWithText(json), "write synthetic tutorial preset");
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    check(processor->loadPresetFile(preset), "load legacy tutorial identity");
    check(processor->projectState().ui.kitFile == kit.getFullPathName().toStdString(),
          "legacy preset resolves kit identity by id despite differing names");
    check(processor->projectState().kit.pads[0].name == "Preset Kick"
              && std::abs(processor->projectState().kit.pads[0].room - 0.41f) < 1e-6f
              && std::abs(processor->projectState().kit.pads[0].reverb - 0.17f) < 1e-6f,
          "kit identity lookup preserves preset sounds and tutorial Room/Reverb values");
    juce::MemoryBlock bytes;
    processor->getStateInformation(bytes);
    auto recalled = std::make_unique<BeatScholarLinuxProcessor>();
    recalled->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*recalled);
    auto* menu = childWithId<juce::TextButton>(*editor, "kit.menu");
    check(menu != nullptr && menu->getButtonText() == "Tutorial Kit",
          "resolved legacy kit header survives host recall");
    processor->projectState().global.kitLocked = true;
    processor->projectState().ui.kitFile = "Locked Kit.bslkit";
    processor->projectState().kit.pads[0].name = "Locked Kick";
    check(processor->loadPresetFile(preset)
              && processor->projectState().ui.kitFile == "Locked Kit.bslkit"
              && processor->projectState().kit.pads[0].name == "Locked Kick",
          "kit lock keeps current identity and sounds on legacy preset load");
    processor->projectState().global.kitLocked = false;
    const auto duplicate = kits.getChildFile("Duplicate.json");
    check(duplicate.replaceWithText(metadata) && processor->loadPresetFile(preset)
              && processor->projectState().ui.kitFile.empty(),
          "ambiguous legacy kit id does not invent a kit identity");
    check(duplicate.deleteFile() && kit.deleteFile() && processor->loadPresetFile(preset)
              && processor->projectState().ui.kitFile.empty(),
          "missing legacy kit keeps a playable preset without a false identity");
    const auto fallback = kits.getChildFile("Old Name.json");
    check(fallback.replaceWithText(metadata.replace("tutorial-kit-id", "different-id"))
              && processor->loadPresetFile(preset)
              && processor->projectState().ui.kitFile == fallback.getFullPathName().toStdString(),
          "unique legacy sampler name falls back to an existing kit basename");
    if (previous.isEmpty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", previous.toRawUTF8(), 1);
}

void testContentNavigationSkipsMalformedFiles(const juce::File& output)
{
    const auto previous = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", "");
    const auto xdg = output.getChildFile("navigation-xdg");
    setenv("XDG_DATA_HOME", xdg.getFullPathName().toRawUTF8(), 1);
    using FileOperation = bool (BeatScholarLinuxProcessor::*)(const juce::File&);
    struct Content
    {
        const char* factory;
        const char* user;
        const char* extension;
        const char* title;
        FileOperation save;
        FileOperation load;
        std::string bsl::UiState::* identity;
    };
    const std::array contents {
        Content {"Factory/Presets", "User/Presets", ".bslpreset", "preset",
                 &BeatScholarLinuxProcessor::savePresetFile, &BeatScholarLinuxProcessor::loadPresetFile,
                 &bsl::UiState::presetFile},
        Content {"Factory/Kits", "User/Kits", ".bslkit", "kit",
                 &BeatScholarLinuxProcessor::saveKitFile, &BeatScholarLinuxProcessor::loadKitFile,
                 &bsl::UiState::kitFile},
        Content {"Factory/Step Randomizer", "User/Randomizers", ".bslrandom", "randomizer preset",
                 &BeatScholarLinuxProcessor::saveRandomizerFile, &BeatScholarLinuxProcessor::loadRandomizerFile,
                 &bsl::UiState::randomizerFile}
    };
    for (const auto& content : contents)
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        const auto factory = bsl::dataRoot().getChildFile(content.factory).getChildFile("Nested category");
        const auto user = bsl::dataRoot().getChildFile(content.user);
        check(factory.createDirectory().wasOk() && user.createDirectory().wasOk(), "create navigation fixtures");
        const auto first = factory.getChildFile(juce::String("A valid") + content.extension);
        const auto middle = factory.getChildFile(juce::String("B malformed") + content.extension);
        const auto last = user.getChildFile(juce::String("C valid") + content.extension);
        const auto tail = user.getChildFile(juce::String("D malformed") + content.extension);
        check((processor.get()->*content.save)(first) && (processor.get()->*content.save)(last)
                  && middle.replaceWithText("not valid JSON") && tail.replaceWithText("not valid JSON"),
              "write valid and malformed content navigation fixtures");
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        const auto navigate = [&](bool forward) {
            const auto title = juce::String(forward ? "Next " : "Previous ") + content.title;
            juce::TextButton* arrow = nullptr;
            for (auto* child : editor->getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*>(child);
                    button != nullptr && button->getTitle() == title)
                    arrow = button;
            check(arrow != nullptr, "find navigation arrow: " + title);
            if (arrow != nullptr) arrow->onClick();
        };
        const auto expectFile = [&](const juce::File& expected, const char* message) {
            check(juce::File(processor->projectState().ui.*content.identity) == expected,
                  juce::String(content.title) + ": " + message);
        };
        check((processor.get()->*content.load)(first), "load first navigation fixture");
        navigate(true);
        expectFile(last, "Next skips malformed file and reaches the user category");
        check((processor.get()->*content.load)(last), "load last navigation fixture");
        navigate(false);
        expectFile(first, "Previous skips malformed file and reaches a nested factory category");
        check((processor.get()->*content.load)(last), "reload last navigation fixture");
        navigate(true);
        expectFile(first, "Next skips malformed tail and wraps to first usable file");
        check((processor.get()->*content.load)(first), "reload first navigation fixture");
        navigate(false);
        expectFile(last, "Previous wraps past malformed tail to last usable file");
        check(first.replaceWithText("not valid JSON") && last.replaceWithText("not valid JSON"),
              "make every navigation fixture malformed");
        const auto before = bsl::statecodec::encode(processor->projectState());
        navigate(true);
        navigate(false);
        check(bsl::statecodec::encode(processor->projectState()) == before,
              "an all-invalid content category leaves the complete project unchanged");
        processor->projectState().ui.*content.identity = {};
        check((processor.get()->*content.save)(last), "restore last usable navigation fixture");
        processor->projectState().ui.*content.identity = {};
        navigate(true);
        expectFile(last, "unset identity skips initial malformed files");
    }
    if (previous.isEmpty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", previous.toRawUTF8(), 1);
}

void testRandomizerVelocityEndpointsStayVisibleAndOrdered()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
    auto* minimum = childWithId<juce::Slider>(*editor, "random.2");
    auto* maximum = childWithId<juce::Slider>(*editor, "random.3");
    check(minimum != nullptr && maximum != nullptr, "randomizer velocity endpoints exist");
    if (minimum == nullptr || maximum == nullptr) return;
    maximum->setValue(0.2, juce::sendNotificationSync);
    check(std::abs(processor->projectState().randomizer.minimumVelocity - 0.2f) < 1e-6f
          && std::abs(processor->projectState().randomizer.maximumVelocity - 0.2f) < 1e-6f
          && std::abs(minimum->getValue() - 0.2) < 1e-6,
          "lowering Max below Min moves Min and keeps both displayed values truthful");
    minimum->setValue(0.9, juce::sendNotificationSync);
    check(std::abs(processor->projectState().randomizer.maximumVelocity - 0.9f) < 1e-6f
          && std::abs(maximum->getValue() - 0.9) < 1e-6,
          "raising Min above Max updates the displayed Max as well as the model");
    childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
    check(std::abs(minimum->getValue() - 0.2) < 1e-6 && std::abs(maximum->getValue() - 0.2) < 1e-6,
          "one undo restores both velocity endpoints");
}

void testTutorialRandomizerNumericUnits(const juce::File& output)
{
    for (const int width : { 960, 1280 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto& state = processor->projectState();
        state.ui.darkMode = width == 960;
        state.randomizer.triggerChance = 0.91f;
        state.randomizer.minimumVelocity = 70.0f / 127.0f;
        state.randomizer.maximumVelocity = 120.0f / 127.0f;
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(width, width * 9 / 16);
        editor->setVisible(true);
        childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
        auto* chance = childWithId<juce::Slider>(*editor, "random.0");
        auto* minimum = childWithId<juce::Slider>(*editor, "random.2");
        auto* maximum = childWithId<juce::Slider>(*editor, "random.3");
        auto* velocity = childWithId<juce::Slider>(*editor, "toolbar.velocity");
        check(chance->getTextFromValue(chance->getValue()) == "91%",
              "Trigger Chance displays tutorial percentage units");
        check(minimum->getTextFromValue(minimum->getValue()) == "70"
              && maximum->getTextFromValue(maximum->getValue()) == "120"
              && maximum->getTextFromValue(1.0) == "127",
              "Randomizer Min and Max display MIDI velocity units through 127");
        auto* entry = childWithId<juce::TextEditor>(*editor, "control.value");
        const auto typeValue = [&](juce::Slider& control, const juce::String& text,
                                   const juce::String& expectedInitial) {
            editor->mouseDoubleClick(visualEvent(*editor, control.getBounds().getCentre().toFloat()));
            check(entry->isVisible() && entry->getText() == expectedInitial,
                  "numeric entry starts in the displayed units");
            entry->setText(text);
            entry->onReturnKey();
        };
        typeValue(*minimum, "97", "70");
        check(std::abs(state.randomizer.minimumVelocity - 97.0f / 127.0f) < 1e-6f,
              "typed MIDI velocity reaches the normalized model without percent rounding");
        childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
        check(std::abs(state.randomizer.minimumVelocity - 70.0f / 127.0f) < 1e-6f,
              "one undo restores the original fractional MIDI velocity");
        typeValue(*chance, "40", "91");
        check(std::abs(state.randomizer.triggerChance - 0.4f) < 1e-6f,
              "typed Trigger Chance percentage reaches the normalized model");
        childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
        check(velocity->getMaximum() == 127 && velocity->getTextValueSuffix().isEmpty(),
              "toolbar brush velocity uses MIDI units without a percent suffix");
        typeValue(*velocity, "100", "102");
        check(std::abs(state.global.defaultVelocity - 100.0f / 127.0f) < 1e-6f,
              "typed toolbar velocity uses the same MIDI units as Randomizer");
        saveVisual(*editor, output, width == 960 ? "randomizer-tutorial-units-960.png"
                                               : "randomizer-tutorial-units-1280.png");
        typeValue(*velocity, "102", "100");
        const auto reset = visualEvent(*editor, velocity->getBounds().getCentre().toFloat(),
            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
        editor->mouseDown(reset);
        editor->mouseUp(reset);
        check(state.global.defaultVelocity == bsl::GlobalState{}.defaultVelocity
              && velocity->getValue() == 102.0,
              "Alt-reset preserves the exact velocity default even when the display is already 102");
        editor->mouseDown(reset);
        editor->mouseUp(reset);
        childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
        check(std::abs(state.global.defaultVelocity - 102.0f / 127.0f) < 1e-6f,
              "one undo restores velocity before reset and a repeated reset adds no empty edit");
        editor->keyPressed(juce::KeyPress(juce::KeyPress::tabKey));
        auto* attack = childWithId<juce::Slider>(*editor, "sound.0");
        typeValue(*attack, "0.625", "0.000");
        check(std::abs(state.kit.pads[0].attackSeconds - 0.625f) < 1e-6f,
              "leaving Randomizer restores the sound control's ordinary units");
    }
}

void testMasterGainReportsHostGestures()
{
    struct Listener final : juce::AudioProcessorParameter::Listener
    {
        std::vector<bool> gestures;
        int values = 0;
        void parameterValueChanged(int, float) override { ++values; }
        void parameterGestureChanged(int, bool starting) override { gestures.push_back(starting); }
    } listener;
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* parameter = processor->parameters().getParameter("masterGain");
    auto* control = childWithId<juce::Slider>(*editor, "master.4");
    parameter->addListener(&listener);
    control->onDragStart();
    control->setValue(0.8, juce::sendNotificationSync);
    control->setValue(0.6, juce::sendNotificationSync);
    control->onDragEnd();
    check(listener.gestures == std::vector<bool>({true, false}) && listener.values >= 2,
          "master drag reports one begin/end touch gesture to the host");
    listener.gestures.clear();
    control->setValue(0.9, juce::sendNotificationSync);
    check(listener.gestures == std::vector<bool>({true, false}),
          "numeric or reset master edit reports a complete host gesture");
    listener.gestures.clear();
    control->onDragStart();
    editor.reset();
    check(listener.gestures == std::vector<bool>({true, false}),
          "closing the editor during a master drag releases host touch automation");
    parameter->removeListener(&listener);
}

void testLegacyAttackFallbackRemainsAudibleAfterEditing()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto state = makeBypassTestProject(0);
    state.kit.pads[0].attackSeconds = 0.0f;
    state.kit.pads[0].attackNormalized = 0.35f;
    const auto bytes = bsl::statecodec::encode(state);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* attack = childWithId<juce::Slider>(*editor, "sound.0");
    auto* pan = childWithId<juce::Slider>(*editor, "sound.6");
    check(attack != nullptr && pan != nullptr, "legacy attack and pan controls exist");
    if (attack == nullptr || pan == nullptr) return;
    check(std::abs(attack->getValue() - 0.35) < 1e-5,
          "attack displays the same effective fallback value that the audio engine uses");
    pan->setValue(0.2, juce::sendNotificationSync);
    const auto& pad = processor->projectState().kit.pads[0];
    const float effectiveAttack = pad.attackSeconds > 0.0f ? pad.attackSeconds : pad.attackNormalized;
    check(std::abs(effectiveAttack - 0.35f) < 1e-6f,
          "editing an unrelated sound control preserves an older normalized attack");
}

void testDefaultVelocityDragUndo()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* velocity = childWithId<juce::Slider>(*editor, "toolbar.velocity");
    auto* undo = childWithId<juce::TextButton>(*editor, "toolbar.undo");
    check(velocity != nullptr && undo != nullptr, "default velocity and undo controls exist");
    if (velocity == nullptr || undo == nullptr) return;
    const auto before = processor->projectState().global.defaultVelocity;
    if (velocity->onDragStart) velocity->onDragStart();
    velocity->setValue(60, juce::sendNotificationSync);
    velocity->setValue(40, juce::sendNotificationSync);
    velocity->setValue(20, juce::sendNotificationSync);
    if (velocity->onDragEnd) velocity->onDragEnd();
    undo->onClick();
    check(std::abs(processor->projectState().global.defaultVelocity - before) < 1e-6f
          && std::abs(velocity->getValue() - std::round(before * 127.0)) < 1e-5,
          "one undo restores the complete default-velocity drag and its displayed value");
    check(!processor->projectEditor().canUndo(), "default-velocity drag owns exactly one undo step");
}

void testAutonomousManualRegressions()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* gain = childWithId<juce::Slider>(*editor, "master.4");
    check(gain != nullptr && gain->getMaximum() == 1.5,
          "master fader matches the existing host automation range");
    auto* attack = childWithId<juce::Slider>(*editor, "sound.0");
    check(attack != nullptr, "attack control exists for undo test");
    if (attack != nullptr)
    {
        if (attack->onDragStart) attack->onDragStart();
        if (attack->onDragEnd) attack->onDragEnd();
        check(!processor->projectEditor().canUndo(), "unchanged knob gesture does not allocate an undo entry");
        processor->projectEditor().addLane(0);
        const auto lanes = processor->projectState().patterns[0].lanes.size();
        const auto previous = attack->getValue();
        if (attack->onDragStart) attack->onDragStart();
        attack->setValue(0.7, juce::sendNotificationSync);
        attack->setValue(0.8, juce::sendNotificationSync);
        if (attack->onDragEnd) attack->onDragEnd();
        check(processor->projectEditor().undo(), "slider drag creates undo step");
        check(processor->projectState().patterns[0].lanes.size() == lanes,
              "undoing slider retains prior unrelated lane edit");
        check(std::abs(processor->projectState().kit.pads[0].attackSeconds - previous) < 1e-5,
              "undoing drag restores pre-gesture value");
    }
    editor->keyPressed(juce::KeyPress('/', {}, '/'));
    check(processor->projectState().ui.randomizerVisible,
          "momentary randomizer brush reveals its controls");
    editor->keyStateChanged(false);
    check(!processor->projectState().ui.randomizerVisible,
          "releasing momentary randomizer restores sound controls");
}






void testMainAndKitInitContract()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.ui.selectedPad = 2;
    state.ui.presetFile = "Edited.bslpreset";
    state.ui.kitFile = "Locked.bslkit";
    state.kit.pads[0].name = "Locked drum";
    state.global.kitLocked = true;
    state.master.room = 0.8f;
    state.patterns[0].lanes[0].beats[0].slices[0].enabled = true;
    auto* bypass = processor->parameters().getParameter("pad1LpfBypass");
    auto* master = processor->parameters().getParameter("masterGain");
    auto* swing = processor->parameters().getParameter("swing");
    bypass->setValueNotifyingHost(1.0f);
    master->setValueNotifyingHost(master->convertTo0to1(0.6f));
    swing->setValueNotifyingHost(swing->convertTo0to1(42.0f));
    processor->resetPresetToInit();
    const auto& initialized = processor->projectState();
    check(initialized.isValid() && initialized.patterns.size() == 1
              && initialized.patterns[0].bars.size() == 1
              && initialized.patterns[0].lanes.size() == 3
              && initialized.patterns[0].name == "A" && initialized.patterns[0].triggerNote == 60,
          "Main Init restores the observed one-pattern three-lane layout");
    bool empty = true;
    for (const auto& lane : initialized.patterns[0].lanes)
        for (const auto& beat : lane.beats)
            for (const auto& slice : beat.slices) empty = empty && !slice.enabled;
    check(empty && initialized.ui.presetFile.empty() && initialized.ui.selectedPad == 2,
          "Main Init clears notes and identity while retaining selected drum");
    check(initialized.global.kitLocked && initialized.ui.kitFile == "Locked.bslkit"
              && initialized.kit.pads[0].name == "Locked drum" && initialized.kit.pads[0].lpfBypass,
          "Main Init respects Kit Lock including automated bypass");
    check(initialized.master.room == 0.0f && initialized.master.reverb == 0.0f
              && initialized.master.distortion == 0.0f
              && processor->parameters().getRawParameterValue("swing")->load() == 0.0f,
          "Main Init resets effects and synchronizes host parameters");
    processor->projectState().global.kitLocked = false;
    processor->resetPresetToInit();
    check(processor->projectState().ui.kitFile.empty()
              && processor->projectState().kit.pads[0].name != "Locked drum"
              && processor->parameters().getRawParameterValue("pad1LpfBypass")->load() == 0.0f,
          "Unlocked Main Init resets kit identity and parameters");

    processor->projectState().patterns[0].name = "Keep sequence";
    processor->projectState().patterns[0].lanes[0].beats[0].slices[0].enabled = true;
    processor->projectState().ui.presetFile = "Keep main.bslpreset";
    processor->projectState().ui.kitFile = "Edited kit.bslkit";
    processor->projectState().kit.pads[0].name = "Edited drum";
    bypass->setValueNotifyingHost(1.0f);
    master->setValueNotifyingHost(master->convertTo0to1(0.6f));
    swing->setValueNotifyingHost(swing->convertTo0to1(42.0f));
    processor->resetKitToInit();
    const auto after = bsl::statecodec::encode(processor->projectState());
    check(processor->projectState().patterns[0].name == "Keep sequence"
              && processor->projectState().patterns[0].lanes[0].beats[0].slices[0].enabled
              && processor->projectState().ui.presetFile == "Keep main.bslpreset"
              && processor->projectState().ui.kitFile.empty()
              && std::abs(processor->parameters().getRawParameterValue("swing")->load() - 42.0f) < 1e-5f,
          "Kit Init retains sequence, Main identity and global automation");
    check(processor->restoreProjectHistory(false)
              && processor->projectState().kit.pads[0].name == "Edited drum"
              && processor->projectState().ui.kitFile == "Edited kit.bslkit"
              && processor->parameters().getRawParameterValue("pad1LpfBypass")->load() == 1.0f
              && std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 0.6f) < 1e-5f,
          "Kit Init undo restores prior kit identity and audible automation");
    check(processor->restoreProjectHistory(true)
              && bsl::statecodec::encode(processor->projectState()) == after,
          "Kit Init redo restores complete initialized state");
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);
    auto recalled = std::make_unique<BeatScholarLinuxProcessor>();
    recalled->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    check(bsl::statecodec::encode(recalled->projectState()) == after,
          "Initialized kit and retained sequence survive host recall");
}

void testKitHistoryRestoresBypassesWithoutResettingGlobalAutomation(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto source = makeBypassTestProject(0);
    const auto bytes = bsl::statecodec::encode(source);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    auto* bypass = processor->parameters().getParameter("pad1LpfBypass");
    auto* master = processor->parameters().getParameter("masterGain");
    auto* swing = processor->parameters().getParameter("swing");
    bypass->setValueNotifyingHost(1.0f);
    master->setValueNotifyingHost(master->convertTo0to1(0.6f));
    swing->setValueNotifyingHost(swing->convertTo0to1(42.0f));
    const auto kit = output.getChildFile("neutral-history.bslkit");
    kit.replaceWithText(bsl::statecodec::encodeKitJson(source.kit, source.master));
    check(processor->loadKitFile(kit), "load kit for audible-history test");
    check(processor->parameters().getRawParameterValue("pad1LpfBypass")->load() == 0.0f,
          "kit load applies kit bypass value");
    check(std::abs(processor->parameters().getRawParameterValue("swing")->load() - 42.0f) < 1e-5f,
          "kit load retains host-automated global swing");
    check(processor->restoreProjectHistory(false), "kit load can be undone");
    check(processor->parameters().getRawParameterValue("pad1LpfBypass")->load() == 1.0f
              && std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 0.6f) < 1e-5f,
          "kit undo restores audible pre-load bypass and master values");
    check(processor->restoreProjectHistory(true), "kit load can be redone");
    check(processor->parameters().getRawParameterValue("pad1LpfBypass")->load() == 0.0f
              && std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 1.0f) < 1e-5f,
          "kit redo reapplies saved kit bypass and master values");
    check(std::abs(processor->parameters().getRawParameterValue("swing")->load() - 42.0f) < 1e-5f,
          "kit history leaves unrelated global automation intact");
}

void testSampleUndoRestoresAudibleData(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto source = makeBypassTestProject(0);
    const auto state = bsl::statecodec::encode(source);
    processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    processor->setRateAndBufferSizeDetails(48000, 257);
    processor->prepareToPlay(48000, 257);
    settleMainGain(*processor, 257);
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* undo = childWithId<juce::TextButton>(*editor, "toolbar.undo");
    auto* redo = childWithId<juce::TextButton>(*editor, "toolbar.redo");
    check(undo && redo, "sample undo controls exist");
    if (!(undo && redo)) return;
    const auto render = [&] {
        processor->triggerPad(0);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 257);
        processor->processBlock(audio, midi);
        return std::vector<float>(audio.getReadPointer(0), audio.getReadPointer(0) + 257);
    };
    const auto original = render();
    const auto replacement = output.getChildFile("replacement.wav");
    juce::AudioBuffer<float> waveform(1, 4096);
    for (int frame = 0; frame < 4096; ++frame)
        waveform.setSample(0, frame, 0.7f * std::sin(float(frame) * 0.03f));
    juce::WavAudioFormat format;
    auto stream = replacement.createOutputStream();
    check(stream != nullptr, "create replacement sample stream");
    if (!stream) return;
    stream->setPosition(0); stream->truncate();
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.release(), 48000, 1, 24, {}, 0));
    check(writer && writer->writeFromAudioSampleBuffer(waveform, 0, 4096), "write replacement sample");
    writer.reset();
    check(processor->loadCustomSample(0, replacement).wasOk(), "load replacement custom sample");
    const auto replaced = render();
    check(replaced != original, "replacement changes audible sample");
    undo->onClick();
    processor->publishProjectStateIfNeeded();
    check(render() == original, "sample undo restores exact audible data, not only metadata");
    redo->onClick();
    processor->publishProjectStateIfNeeded();
    check(render() == replaced, "sample redo restores exact replacement audio");
    processor->releaseResources();
}



void testSampleDropTargets(const juce::File& output)
{
    const auto source = makeBypassTestProject(0);
    juce::MemoryOutputStream wav;
    check(juce::Base64::convertFromBase64(wav, juce::String(source.kit.pads[0].customSampleBase64.value())),
          "decode sample-drop fixture");
    const auto file = output.getChildFile("drop-target.wav");
    check(file.replaceWithData(wav.getData(), wav.getDataSize()), "write sample-drop fixture");
    const juce::StringArray files { file.getFullPathName() };
    const auto mixedFile = output.getChildFile("drop-mixed.wav");
    check(mixedFile.replaceWithData(wav.getData(), wav.getDataSize()), "write mixed sample-drop fixture");
    const juce::StringArray mixedFiles { output.getChildFile("unsupported.txt").getFullPathName(),
                                         mixedFile.getFullPathName() };
    for (const int width : { 960, 1280 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto& state = processor->projectState();
        state.ui.selectedPad = 3;
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(width, width * 720 / 1280);
        editor->setVisible(true);
        const auto drop = [&](int x, int y) { editor->filesDropped(files, x * width / 1280, y * width / 1280); };
        const auto original = bsl::statecodec::encodeKitJson(state.kit, state.master);
        for (const auto point : { juce::Point<int>{600, 30}, juce::Point<int>{200, 306},
                                  juce::Point<int>{650, 605} })
        {
            drop(point.x, point.y);
            check(bsl::statecodec::encodeKitJson(state.kit, state.master) == original && !processor->projectEditor().canUndo(),
                  "drop outside a pad or sample area leaves the kit and history unchanged");
        }
        auto* random = childWithId<juce::TextButton>(*editor, "brush.dice");
        auto* drum = childWithId<juce::TextButton>(*editor, "brush.drum");
        auto* pad = childWithId<juce::TextButton>(*editor, "pad.8");
        check(random && drum && pad, "sample-drop targets exist");
        if (!(random && drum && pad)) continue;
        random->onClick();
        drop(475, 650);
        check(bsl::statecodec::encodeKitJson(state.kit, state.master) == original, "hidden Sounds sample area does not accept drops in Randomizer");
        const auto target = pad->getBounds().getCentre();
        editor->filesDropped(files, target.x, target.y);
        check(state.kit.pads[8].customSampleName == "drop-target.wav"
                  && state.kit.pads[3].customSampleBase64.empty() && state.ui.selectedPad == 8,
              "drop on a drum pad loads that pad while Randomizer is shown");
        check(editor->isInterestedInFileDrag(mixedFiles), "mixed drag containing supported audio is accepted");
        editor->filesDropped(mixedFiles, target.x, target.y);
        check(state.kit.pads[8].customSampleName == "drop-mixed.wav",
              "pad drop loads the first supported audio file in a mixed selection");
        drum->onClick();
        state.ui.selectedPad = 3;
        const auto otherPad = state.kit.pads[8];
        drop(475, 650);
        check(state.kit.pads[3].customSampleName == "drop-target.wav"
                  && state.kit.pads[8].customSampleName == otherPad.customSampleName
                  && state.kit.pads[8].customSampleBase64.value() == otherPad.customSampleBase64.value(),
              "drop on visible Sounds sample area loads only the selected pad");
        editor->filesDropped(mixedFiles, 475 * width / 1280, 650 * width / 1280);
        check(state.kit.pads[3].customSampleName == "drop-mixed.wav"
                  && state.kit.pads[8].customSampleName == otherPad.customSampleName,
              "Sounds drop loads supported audio following an unsupported file");
        if (width == 1280) saveVisual(*editor, output, "sample-drop-target.png");
    }
}

void captureTutorialFactorySoundSelection(const juce::File& output)
{
    BeatScholarLinuxProcessor processor;
    BeatScholarLinuxEditor editor(processor);
    editor.setSize(1280, 720);
    for (const auto& selection : std::array<std::pair<char, const char*>, 3> {{
             { 'a', "tutorial-factory-kick.png" },
             { 's', "tutorial-factory-snare.png" },
             { 'h', "tutorial-factory-open-hat.png" }
         }})
    {
        editor.keyPressed(juce::KeyPress(selection.first, {}, selection.first));
        saveVisual(editor, output, selection.second);
    }
}

void testDocumentedKeyboardRows()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    BeatScholarLinuxEditor editor(*processor);
    const std::string rows = "asdfghjkqwertyui";
    for (std::size_t pad = 0; pad < rows.size(); ++pad)
    {
        check(editor.keyPressed(juce::KeyPress(rows[pad], {}, rows[pad])), "pad shortcut handled");
        check(processor->projectState().ui.selectedPad == pad,
              "manual keyboard row selects matching visible pad: " + juce::String(rows[pad]));
    }
    // Manual p11: pad keys prime a drum for placement and editing in Sounds.
    // Test both keyboard rows after every other persistent brush.
    for (const auto* previous : { "brush.slicer", "brush.velocity", "brush.dice" })
        for (const char key : { 'a', 'q' })
        {
            auto current = std::make_unique<BeatScholarLinuxProcessor>();
            auto& state = current->projectState();
            state.patterns[0].lanes = { bsl::LaneState::make(4) };
            auto& beat = state.patterns[0].lanes[0].beats[0];
            beat = bsl::BeatState::withSubdivision(3, 7, 0.3f);
            for (auto& slice : beat.slices) slice.enabled = false;
            const auto before = beat;
            BeatScholarLinuxEditor view(*current);
            view.setVisible(true);
            auto* oldBrush = childWithId<juce::TextButton>(view, previous);
            auto* drum = childWithId<juce::TextButton>(view, "brush.drum");
            auto* sample = childWithId<juce::TextButton>(view, "sample.oneshot");
            check(oldBrush && drum && sample, "pad-key test locates brush and module controls");
            if (!(oldBrush && drum && sample)) continue;
            oldBrush->onClick();
            view.keyPressed(juce::KeyPress(key, {}, key));
            auto* random = childWithId<juce::Slider>(view, "random.0");
            check(drum->getToggleState() && (random == nullptr || !random->isVisible()) && sample->isVisible()
                      && !state.ui.randomizerVisible,
                  "pad key selects Drum and opens Sounds after " + juce::String(previous));
            // Slice zero is the lower-left sector in the tutorial orientation.
            const auto click = visualEvent(view, {150.0f, 335.0f});
            view.mouseDown(click); view.mouseUp(click);
            auto expected = before;
            expected.slices[0].enabled = true;
            expected.slices[0].padIndex = key == 'a' ? 0 : 8;
            expected.slices[0].velocity = state.global.defaultVelocity;
            expected.slices[0].velocityAssigned = true;
            check(beat == expected, "pad key primes only the clicked slice without reslicing or randomizing");
            check(current->projectEditor().undo() && state.patterns[0].lanes[0].beats[0] == before
                      && !current->projectEditor().canUndo(),
                  "pad-key selection adds no history before the one placement edit");
        }
    for (const char held : { '3', '/', '\0' })
    {
        auto current = std::make_unique<BeatScholarLinuxProcessor>();
        BeatScholarLinuxEditor view(*current);
        view.setVisible(true);
        auto* oldBrush = childWithId<juce::TextButton>(view, "brush.dice");
        check(oldBrush != nullptr, "pad-key held-shortcut test locates previous brush");
        if (oldBrush == nullptr) continue;
        oldBrush->onClick();
        if (held == 0) view.modifierKeysChanged(juce::ModifierKeys::altModifier);
        else view.keyPressed(juce::KeyPress(held, {}, held));
        view.keyPressed(juce::KeyPress('a', {}, 'a'));
        const auto* active = childWithId<juce::TextButton>(view,
            held == '/' ? "brush.dice" : held == '3' ? "brush.slicer" : "brush.velocity");
        check(active && active->getToggleState() && current->projectState().ui.selectedPad == 0,
              "pad key preserves the currently held momentary editing tool");
        if (held == 0) view.modifierKeysChanged({});
        else view.keyStateChanged(false);
        const auto* drum = childWithId<juce::TextButton>(view, "brush.drum");
        const auto* sample = childWithId<juce::TextButton>(view, "sample.oneshot");
        check(drum && drum->getToggleState() && sample && sample->isVisible()
                  && !current->projectState().ui.randomizerVisible,
              "releasing held shortcut after a pad key restores Drum and Sounds");
        check(!current->projectEditor().canUndo(), "tool and pad selection add no edit history");
    }

}

void testTutorialPatternAddTile(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    BeatScholarLinuxEditor editor(*processor);
    editor.setVisible(true);
    auto* toggle = childWithId<juce::TextButton>(editor, "arrangement.toggle");
    auto* add = childWithId<juce::TextButton>(editor, "arrangement.add");
    check(toggle && add, "locate arrangement view and add controls");
    if (!(toggle && add)) return;
    const auto click = [&](juce::TextButton* button) {
        const auto event = visualEvent(editor, button->getBounds().getCentre().toFloat())
                               .getEventRelativeTo(button);
        static_cast<juce::Component*>(button)->mouseDown(event);
        static_cast<juce::Component*>(button)->mouseUp(event);
    };
    for (const int width : { 960, 1280, 1920 })
    {
        editor.setSize(width, width * 720 / 1280);
        const float scale = float(width) / 1280.0f;
        check(!state.ui.patternMode && toggle->getButtonText() == "Patterns",
              "bar view names its destination");
        const auto bottom = add->getY();
        click(toggle);
        check(state.ui.patternMode && toggle->getButtonText() == "Bars"
                  && add->getY() < 250 * scale && add->getY() < bottom,
              "view switch immediately places plus below the single pattern");
        check(std::abs(add->getY() - 209 * scale) <= 1
                  && std::abs(add->getHeight() - 62 * scale) <= 1,
              "single-pattern add tile matches tutorial position and height");
        check(editor.getComponentAt(add->getBounds().getCentre()) == add,
              "moved plus tile owns its visible click target");
        saveVisual(editor, output, ("pattern-add-one-" + std::to_string(width) + ".png").c_str());
        click(toggle);
        check(!state.ui.patternMode && add->getY() == bottom,
              "returning to Bars restores the bottom add control");
    }
    editor.setSize(1280, 720);
    click(toggle);
    const auto firstY = add->getY();
    click(add);
    check(state.patterns.size() == 2 && add->getY() > firstY
              && add->getY() < 350,
          "visible plus adds a pattern and follows the new final card");
    for (const int width : { 960, 1280, 1920 })
    {
        editor.setSize(width, width * 720 / 1280);
        const float scale = float(width) / 1280.0f;
        check(std::abs(add->getY() - 291 * scale) <= 2
                  && std::abs(add->getHeight() - 62 * scale) <= 1,
              "two-pattern add tile matches tutorial position and height");
        const auto clickCard = [&](float y) {
            const auto event = visualEvent(editor, juce::Point<float>(1210, y) * scale);
            editor.mouseDown(event); editor.mouseUp(event);
        };
        clickCard(279);
        check(state.activePattern == 1, "second card lower edge selects second pattern");
        clickCard(204);
        check(state.activePattern == 1, "blank card gap does not select the preceding pattern");
        clickCard(197);
        check(state.activePattern == 0, "first card lower edge selects first pattern");
        clickCard(279);
        saveVisual(editor, output, ("pattern-add-two-" + std::to_string(width) + ".png").c_str());
    }
    // Match the retained Samples & Sounds frame for direct typography inspection.
    const auto beforeTypographyCapture = state;
    state.patterns[0].name = "Beat";
    state.patterns[1].name = "Fill";
    state.patterns[0].triggerNote = 60;
    state.patterns[1].triggerNote = 61;
    state.ui.midiFlats = true;
    state.activePattern = 0;
    for (const int width : { 960, 1280, 1920 })
    {
        editor.setSize(width, width * 720 / 1280);
        saveVisual(editor, output, ("tutorial-pattern-text-" + std::to_string(width) + ".png").c_str());
    }
    state = beforeTypographyCapture;
    editor.setSize(1280, 720);
    childWithId<juce::TextButton>(editor, "toolbar.undo")->onClick();
    check(state.patterns.size() == 1 && add->getY() == firstY,
          "undo restores both the pattern list and plus position");
    for (int i = 1; i < 12; ++i)
    {
        click(add);
        if (state.patterns.size() == 3)
            check(add->getY() == 373 && add->getHeight() == 62,
                  "third pattern and add tile fit before compact overflow layout");
    }
    check(state.patterns.size() == 12 && add->getY() == 456,
          "overflow keeps plus accessible below the clipped cards");
    juce::MouseWheelDetails wheel; wheel.deltaY = -100.0f;
    editor.mouseWheelMove(visualEvent(editor, {1205, 300}), wheel);
    click(add);
    check(state.patterns.size() == 13 && add->getY() == 456,
          "scrolled list still adds through the fixed overflow tile");
}

void testArrangementOverflowAndContextActions(const juce::File& output)
{
    for (const bool patterns : { false, true })
        for (const int width : { 960, 1280, 1920 })
        {
            auto processor = std::make_unique<BeatScholarLinuxProcessor>();
            auto& state = processor->projectState();
            state.ui.patternMode = patterns;
            if (patterns)
            {
                while (state.patterns.size() < 12) processor->projectEditor().addPattern();
                state.activePattern = 0;
            }
            else
                while (state.patterns[0].bars.size() < 12)
                    processor->projectEditor().addBar(0, bsl::BarState::make(4, 4));
            BeatScholarLinuxEditor editor(*processor);
            editor.setSize(width, width * 720 / 1280);
            const float scale = float(width) / 1280.0f;
            const auto count = [&] { return patterns ? state.patterns.size() : state.patterns[0].bars.size(); };
            editor.mouseDown(visualEvent(editor, juce::Point<float>(1252, 120) * scale,
                                        juce::ModifierKeys::rightButtonModifier));
            check(count() == 12, "right click over arrangement delete opens menu without deletion");
            juce::PopupMenu::dismissAllActiveMenus();
            if (count() != 12) continue; // Keep the remaining checks independent of destructive RED behavior.
            juce::MouseWheelDetails wheel;
            wheel.deltaY = -100.0f;
            editor.mouseWheelMove(visualEvent(editor, juce::Point<float>(1205, 300) * scale), wheel);
            editor.mouseDown(visualEvent(editor, juce::Point<float>(1205, 423) * scale));
            check((patterns ? state.activePattern : state.ui.selectedBar) == 11,
                  "last arrangement item remains reachable at every editor size");
            const auto name = std::string(patterns ? "patterns-" : "bars-") + std::to_string(width) + ".png";
            saveVisual(editor, output, name.c_str());
            editor.mouseDown(visualEvent(editor, juce::Point<float>(1170, 436) * scale));
            check(count() == 13, "scrolled duplicate glyph edits the visible final item");
        }
}

void testTutorialContinuousZoomAndProportionalScroll(const juce::File& output)
{
    for (const int width : { 960, 1280, 1600 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto& state = processor->projectState();
        state.patterns[0].bars.assign(4, bsl::BarState::make(4, 4));
        state.patterns[0].lanes.assign(3, bsl::LaneState::make(16));
        state.ui.selectedPad = 15;
        BeatScholarLinuxEditor editor(*processor);
        editor.setSize(width, width * 720 / 1280);
        const float scale = static_cast<float>(width) / 1280.0f;
        const auto before = bsl::statecodec::encode(state);
        const auto click = [&](float x, float y) {
            const auto event = visualEvent(editor, juce::Point<float>(x, y) * scale);
            editor.mouseDown(event); editor.mouseUp(event);
        };
        const auto drag = [&](juce::Point<float> from, juce::Point<float> to) {
            editor.mouseDown(visualEvent(editor, from * scale));
            editor.mouseDrag(visualEvent(editor, to * scale));
            editor.mouseUp(visualEvent(editor, to * scale));
        };
        // Basic tutorial 5:28-5:55: independent continuous zoom and rectangular scroll thumbs.
        drag({1014.0f, 512.0f}, {1045.43f, 512.0f}); // approximately 3x horizontal
        drag({1141.0f, 465.0f}, {1141.0f, 451.29f}); // approximately 2x vertical
        check(bsl::statecodec::encode(state) == before && !processor->projectEditor().canUndo(),
              "continuous zoom changes only the viewport");
        click(450, 300);
        check(state.patterns[0].lanes[0].beats[1].slices[0].padIndex == 15,
              "continuous zoom maps visible beat and lane targets at every editor size");
        check(processor->projectEditor().undo() && bsl::statecodec::encode(state) == before,
              "zoomed beat edit is the only Undo entry");
        click(100, 512); // Inside the proportional horizontal thumb: no initial jump.
        click(450, 300);
        check(state.patterns[0].lanes[0].beats[1].slices[0].padIndex == 15,
              "grabbing a scrollbar thumb preserves the clicked offset");
        processor->projectEditor().undo();
        drag({100.0f, 512.0f}, {339.5f, 512.0f}); // four quarter notes
        drag({1141.0f, 85.0f}, {1141.0f, 129.5f}); // half a lane
        check(bsl::statecodec::encode(state) == before && !processor->projectEditor().canUndo(),
              "scrollbar dragging changes neither project state nor edit history");
        click(450, 300);
        check(state.patterns[0].lanes[1].beats[5].slices[0].padIndex == 15,
              "panned targets follow the proportional scroll thumb on both axes");
        const auto name = "tutorial-continuous-zoom-" + std::to_string(width) + ".png";
        saveVisual(editor, output, name.c_str());
    }
}

void testZoomButtonsAndScrollRails(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns[0].lanes = { bsl::LaneState::make(4), bsl::LaneState::make(4) };
    state.ui.selectedPad = 15;
    BeatScholarLinuxEditor editor(*processor);
    const auto beforeScroll = editor.createComponentSnapshot(editor.getLocalBounds());
    const auto click = [&](float x, float y) {
        auto event = visualEvent(editor, { x, y });
        editor.mouseDown(event); editor.mouseUp(event);
    };
    click(1141, 512); // Horizontal + doubles the visible beat width.
    click(800, 200);
    check(state.patterns[0].lanes[0].beats[1].slices[0].padIndex == 15,
          "horizontal plus zooms beat hit targets");
    state.ui.selectedPad = 14;
    click(996, 512);
    click(800, 200);
    check(state.patterns[0].lanes[0].beats[2].slices[0].padIndex == 14,
          "horizontal minus restores beat hit targets");
    click(1141, 512);
    click(965, 512); // Right end of the scroll rail, before the zoom buttons.
    state.ui.selectedPad = 13;
    click(300, 200);
    check(state.patterns[0].lanes[0].beats[2].slices[0].padIndex == 13,
          "horizontal rail scrolls to the final visible beats");
    click(1141, 353); // Vertical +.
    state.ui.selectedPad = 12;
    click(300, 400);
    check(state.patterns[0].lanes[0].beats[2].slices[0].padIndex == 12,
          "vertical plus zooms lane hit targets");
    click(1141, 487);
    state.ui.selectedPad = 11;
    click(300, 400);
    check(state.patterns[0].lanes[1].beats[2].slices[0].padIndex == 11,
          "vertical minus restores lane hit targets");
    click(1141, 353);
    click(1141, 330);
    state.ui.selectedPad = 10;
    click(300, 200);
    check(state.patterns[0].lanes[1].beats[2].slices[0].padIndex == 10,
          "vertical rail scrolls to the final lane");
    const auto afterScroll = editor.createComponentSnapshot(editor.getLocalBounds());
    int changedHeaderPixels = 0;
    for (int y = 0; y < 102; ++y)
        for (int x = 8; x < 58; ++x)
            changedHeaderPixels += beforeScroll.getPixelAt(x, y) != afterScroll.getPixelAt(x, y);
    check(changedHeaderPixels == 0, "scrolled lane controls stay below the toolbar and add-lane header");
    saveVisual(editor, output, "zoomed-and-scrolled.png");
}

void testDenseLaneControlsMatchTheirVisibleTargets(const juce::File& output)
{
    // Basic Beat Making & Editing at 1:25 and 5:38 shows three lanes.
    // Retain the matching lane count for visual comparison of the control labels.
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        processor->projectState().patterns[0].lanes.assign(3, bsl::LaneState::make(4));
        BeatScholarLinuxEditor editor(*processor);
        saveVisual(editor, output, "tutorial-three-lane-controls.png");
    }
    // These are the four displayed first-lane button centres in the 1280x720
    // layout with 15 lanes. They fall in different raw lane quartiles.
    constexpr std::array<float, 4> centres { 110.99f, 114.98f, 118.96f, 122.94f };
    for (const int width : { 960, 1280, 1600 })
        for (int action = 0; action < 4; ++action)
        {
            auto processor = std::make_unique<BeatScholarLinuxProcessor>();
            auto& state = processor->projectState();
            state.patterns[0].lanes.assign(15, bsl::LaneState::make(4));
            BeatScholarLinuxEditor editor(*processor);
            editor.setSize(width, width * 720 / 1280);
            const float scale = static_cast<float>(width) / 1280.0f;
            if (width == 1280 && action == 0)
                saveVisual(editor, output, "dense-lane-targets.png");
            const auto click = [&](float x, float y) {
                auto event = visualEvent(editor, juce::Point<float>(x, y) * scale);
                editor.mouseDown(event); editor.mouseUp(event);
            };
            click(33, centres[static_cast<std::size_t>(action)]);
            const auto& lanes = state.patterns[0].lanes;
            if (action == 0)
                check(lanes.size() == 15 && lanes[0].solo && !lanes[0].muted,
                      "dense lane visible S toggles only solo");
            else if (action == 1)
                check(lanes.size() == 15 && lanes[0].muted && !lanes[0].solo,
                      "dense lane visible M toggles only mute");
            else
                check(lanes.size() == (action == 2 ? 14 : 16),
                      action == 2 ? "dense lane visible minus removes a lane"
                                  : "dense lane visible plus adds a lane");
            check(processor->projectEditor().undo(), "lane control action has one Undo");
            check(state.patterns[0].lanes.size() == 15
                  && !state.patterns[0].lanes[0].solo && !state.patterns[0].lanes[0].muted,
                  "Undo restores the lane control action");
            click(33, 106); // Visible padding above the first control.
            check(!processor->projectEditor().canUndo()
                  && !state.patterns[0].lanes[0].solo && !state.patterns[0].lanes[0].muted,
                  "lane-control padding is inert");
        }
}

void testCommandDragPansBlankGrid(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto& state = processor->projectState();
    state.patterns[0].lanes = { bsl::LaneState::make(4), bsl::LaneState::make(4) };
    state.patterns[0].lanes[0].beats[0].setSubdivision(3);
    state.ui.selectedPad = 15;
    BeatScholarLinuxEditor editor(*processor);
    const auto click = [&](float x, float y) {
        auto event = visualEvent(editor, { x, y });
        editor.mouseDown(event); editor.mouseUp(event);
    };
    click(1141, 512); // 2x horizontal zoom.
    click(1141, 353); // 2x vertical zoom.
    const auto before = bsl::statecodec::encode(state);
    const juce::ModifierKeys modifiers(juce::ModifierKeys::leftButtonModifier
                                      | juce::ModifierKeys::commandModifier);
    // This corner belongs to beat zero's rectangular cell, but lies outside its pie.
    editor.mouseDown(visualEvent(editor, { 70, 110 }, modifiers));
    editor.mouseDrag(visualEvent(editor, { -1100, -450 }, modifiers));
    editor.mouseUp(visualEvent(editor, { -1100, -450 }, modifiers));
    check(bsl::statecodec::encode(state) == before && !processor->projectEditor().canUndo(),
          "Command drag on blank grid pans without changing slices or adding Undo");
    click(300, 200);
    check(state.patterns[0].lanes[1].beats[2].slices[0].padIndex == 15,
          "blank-grid pan moves both axes and clamps at the final beats and lanes");
    saveVisual(editor, output, "command-grid-pan.png");
}

void testInvalidEmbeddedAudioDoesNotReplaceStateOrSound(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto source = makeBypassTestProject(0);
    source.patterns[0].name = "Keep this project";
    auto encoded = bsl::statecodec::encode(source);
    processor->setStateInformation(encoded.getData(), int(encoded.getSize()));
    const auto sample = juce::File(__FILE__).getParentDirectory().getChildFile("fixtures/synthetic-tone.mp3");
    check(processor->loadCustomSample(0, sample).wasOk(), "valid audio loads before malformed restore");
    const auto render = [&] {
        processor->setRateAndBufferSizeDetails(48000, 257);
        processor->prepareToPlay(48000, 257);
        settleMainGain(*processor, 257);
        processor->triggerPad(0);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 257);
        processor->processBlock(audio, midi);
        auto result = std::vector<float>(audio.getReadPointer(0), audio.getReadPointer(0) + 257);
        processor->releaseResources();
        return result;
    };
    const auto before = render();
    auto invalid = processor->projectState();
    invalid.patterns[0].name = "Invalid media";
    invalid.kit.pads[0].customSampleName = "invalid.flac";
    invalid.kit.pads[0].customSampleBase64 = juce::Base64::toBase64("FLAC", 4).toStdString();
    invalid.kit.pads[0].customSampleSha256 = juce::SHA256("FLAC", 4).toHexString().toStdString();
    encoded = bsl::statecodec::encode(invalid);
    check(!encoded.isEmpty(), "malformed audio fixture passes structural codec checks");
    processor->setStateInformation(encoded.getData(), int(encoded.getSize()));
    check(processor->projectState().patterns[0].name == "Keep this project",
          "host recall rejects undecodable embedded audio atomically");
    check(render() == before, "rejected host recall preserves exact audible sample");
    const auto preset = output.getChildFile("invalid-audio.bslpreset");
    check(preset.replaceWithText(bsl::statecodec::encodePresetJson(invalid)), "write malformed audio preset");
    check(!processor->loadPresetFile(preset), "preset import rejects undecodable embedded audio");
    check(render() == before, "rejected preset preserves exact audible sample");
    const auto kit = output.getChildFile("invalid-audio.bslkit");
    check(kit.replaceWithText(bsl::statecodec::encodeKitJson(invalid.kit, invalid.master)), "write malformed audio kit");
    check(!processor->loadKitFile(kit), "kit import rejects undecodable embedded audio");
    check(render() == before, "rejected kit preserves exact audible sample");
}

void testDecodedSampleLimitsBeforeAllocation()
{
    using Processor = BeatScholarLinuxProcessor;
    const auto stereoFrames = static_cast<std::int64_t>(Processor::maximumDecodedSampleBytes / (2 * sizeof(float)));
    check(Processor::supportsDecodedSample(stereoFrames, 2), "decoded stereo sample may reach its memory boundary");
    check(!Processor::supportsDecodedSample(stereoFrames + 1, 2), "decoded stereo size is bounded before allocation");
    check(Processor::supportsDecodedSample(stereoFrames * 2, 1), "mono uses its single-channel budget");
    check(!Processor::supportsDecodedSample(std::numeric_limits<std::int64_t>::max(), 1),
          "malformed or huge decoder length cannot narrow to int");
    check(!Processor::supportsDecodedSample(100, 0) && !Processor::supportsDecodedSample(0, 2),
          "empty decoder shape rejected");
}

void testDocumentedMp3CustomSampleSupport()
{
    const auto file = juce::File(__FILE__).getParentDirectory().getChildFile("fixtures/synthetic-tone.mp3");
    check(file.existsAsFile(), "synthetic MP3 fixture exists");
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    const bool loaded = processor->loadCustomSample(0, file).wasOk();
    check(loaded, "documented MP3 custom sample decodes successfully");
    if (!loaded) return;
    processor->projectState().global.sequencerEnabled = false;
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);
    auto restored = std::make_unique<BeatScholarLinuxProcessor>();
    restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    restored->setRateAndBufferSizeDetails(48000, 257);
    restored->prepareToPlay(48000, 257);
    restored->triggerPad(0);
    juce::AudioBuffer<float> audio(2, 257);
    float peak = 0.0f;
    for (int block = 0; block < 48; ++block)
    {
        juce::MidiBuffer midi;
        restored->processBlock(audio, midi);
        peak = std::max(peak, audio.getMagnitude(0, audio.getNumSamples()));
    }
    check(peak > 0.01f, "embedded MP3 survives state restore and renders at another sample rate");
    restored->releaseResources();
}

void testDocumentedNumericEntry(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    auto* control = childWithId<juce::Slider>(*editor, "sound.0");
    check(control != nullptr, "numeric-entry control exists");
    if (control == nullptr) return;
    control->setValue(0.25, juce::sendNotificationSync);
    const auto position = control->getBounds().getCentre().toFloat();
    const auto event = visualEvent(*editor, position, juce::ModifierKeys::leftButtonModifier);
    // Component dispatch sends the double click to both slider and its listener.
    control->mouseDoubleClick(event.getEventRelativeTo(control));
    editor->mouseDoubleClick(event);
    check(std::abs(control->getValue() - 0.25) < 1e-6,
          "double click opens entry without resetting the value");
    auto* entry = childWithId<juce::TextEditor>(*editor, "control.value");
    check(entry != nullptr && entry->isVisible(), "double click opens numeric entry");
    if (entry == nullptr || !entry->isVisible()) return;
    const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
    auto stream = output.getChildFile("numeric-entry.png").createOutputStream();
    if (stream) { stream->setPosition(0); stream->truncate(); }
    check(stream && juce::PNGImageFormat().writeImageToStream(snapshot, *stream), "capture numeric entry");
    entry->setText("0.625");
    entry->onReturnKey();
    check(std::abs(processor->projectState().kit.pads[0].attackSeconds - 0.625f) < 1e-6f,
          "numeric entry applies typed value");
    editor->mouseDoubleClick(event);
    entry->setText("0.9");
    entry->onEscapeKey();
    check(std::abs(control->getValue() - 0.625) < 1e-6,
          "Escape cancels numeric entry without changing the control");
}

void testBarMeterNumericEntry(const juce::File& output)
{
    struct Target { juce::Point<float> position; bool denominator; };
    const std::array<Target, 4> targets {{{{580.0f, 80.0f}, false}, {{700.0f, 80.0f}, true},
                                         {{1190.0f, 146.0f}, false}, {{1220.0f, 146.0f}, true}}};
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(1280, 720);
        editor->setVisible(true);
        const auto target = targets[index];
        const auto event = visualEvent(*editor, target.position);
        const auto open = [&] {
            editor->mouseDown(event);
            editor->mouseDoubleClick(event);
            editor->mouseUp(event);
        };
        open();
        auto* entry = childWithId<juce::TextEditor>(*editor, "control.value");
        check(entry != nullptr && entry->isVisible(), "bar meter double click opens numeric entry");
        if (entry == nullptr || !entry->isVisible()) continue;
        check(entry->getText() == "4", "bar numeric entry starts at its current value");
        check(!processor->projectEditor().canUndo(), "opening bar numeric entry does not edit history");
        if (index == 0) saveVisual(*editor, output, "bar-numerator-entry.png");
        if (index == 3) saveVisual(*editor, output, "arrangement-denominator-entry.png");
        for (const auto* invalid : {"0", "65", "3.5", "1e300", "-2"})
        {
            entry->setText(invalid);
            entry->onReturnKey();
            check(entry->isVisible() && !processor->projectEditor().canUndo(),
                  "invalid bar value keeps entry open and leaves project unchanged");
        }
        if (target.denominator)
        {
            entry->setText("3");
            entry->onReturnKey();
            check(entry->isVisible() && !processor->projectEditor().canUndo(),
                  "unsupported bar denominator is rejected");
        }
        entry->setText(target.denominator ? "8" : "7");
        entry->onReturnKey();
        const auto& edited = processor->projectState().patterns[0];
        check(!entry->isVisible() && edited.bars[0].numerator == (target.denominator ? 4 : 7)
                  && edited.bars[0].denominator == (target.denominator ? 8 : 4)
                  && edited.lanes[0].beats.size() == (target.denominator ? 4U : 7U),
              "typed meter updates the selected half and resizes its lane beats");
        check(processor->projectEditor().undo() && !processor->projectEditor().canUndo()
                  && processor->projectState().patterns[0].bars[0].numerator == 4
                  && processor->projectState().patterns[0].bars[0].denominator == 4
                  && processor->projectState().patterns[0].lanes[0].beats.size() == 4,
              "one undo restores the typed meter and lane length");
        open();
        entry->setText("16");
        entry->onEscapeKey();
        check(!entry->isVisible() && !processor->projectEditor().canUndo(),
              "Escape cancels bar numeric entry without creating history");
        open();
        entry->setText("16");
        entry->onFocusLost();
        check(!entry->isVisible() && !processor->projectEditor().canUndo(),
              "focus loss cancels bar numeric entry without changing state");
        open();
        entry->setText("4");
        entry->onReturnKey();
        check(!entry->isVisible() && !processor->projectEditor().canUndo(),
              "unchanged bar value closes entry without adding undo history");
        open();
        entry->setText("16");
        juce::MemoryBlock saved;
        processor->getStateInformation(saved);
        processor->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        entry->onReturnKey();
        check(!entry->isVisible() && processor->projectState().patterns[0].bars[0].numerator == 4
                  && processor->projectState().patterns[0].bars[0].denominator == 4,
              "host state restore cancels pending bar numeric entry before it can edit loaded state");
    }
}

void testLaneContextMenuDoesNotEdit()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->projectState().patterns[0].lanes = { bsl::LaneState::make(4) };
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    const auto event = visualEvent(*editor, { 20.0f, 120.0f }, juce::ModifierKeys::rightButtonModifier);
    editor->mouseDown(event);
    check(!processor->projectState().patterns[0].lanes[0].solo,
          "right-clicking lane opens menu without toggling solo");
    check(juce::PopupMenu::dismissAllActiveMenus(), "lane right-click exposes a context menu");
}

void testMasterUndoPreservesHostAutomation()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    auto* gain = childWithId<juce::Slider>(*editor, "master.4");
    auto* undo = childWithId<juce::TextButton>(*editor, "toolbar.undo");
    auto* redo = childWithId<juce::TextButton>(*editor, "toolbar.redo");
    auto* attack = childWithId<juce::Slider>(*editor, "sound.0");
    check(gain && undo && redo && attack, "master undo controls exist");
    if (!(gain && undo && redo && attack)) return;
    auto* master = processor->parameters().getParameter("masterGain");
    auto* swing = processor->parameters().getParameter("swing");
    master->setValueNotifyingHost(master->convertTo0to1(0.6f));
    attack->setValue(0.3, juce::sendNotificationSync);
    check(std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 0.6f) < 1e-5f,
          "sound edits preserve host-automated master gain");
    if (gain->onDragStart) gain->onDragStart();
    gain->setValue(1.25, juce::sendNotificationSync);
    if (gain->onDragEnd) gain->onDragEnd();
    swing->setValueNotifyingHost(swing->convertTo0to1(42.0f));
    undo->onClick();
    check(std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 0.6f) < 1e-5f,
          "master undo restores the audible pre-drag gain");
    check(std::abs(processor->parameters().getRawParameterValue("swing")->load() - 42.0f) < 1e-5f,
          "master undo preserves unrelated host automation");
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);
    auto restored = bsl::ProjectState::makeDefault();
    check(bsl::statecodec::decode(saved.getData(), static_cast<int>(saved.getSize()), restored)
              && std::abs(restored.master.gain - 0.6f) < 1e-5f,
          "master undo persists audible gain in saved state");
    redo->onClick();
    check(std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 1.25f) < 1e-5f,
          "master redo restores the changed audible gain");
    undo->onClick(); // Master gesture back to the automated pre-gesture value.
    undo->onClick(); // Older sound-only edit must not own the master parameter.
    check(std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 0.6f) < 1e-5f,
          "multi-step undo does not replay stale master values from sound-only snapshots");
    master->setValueNotifyingHost(master->convertTo0to1(0.9f));
    attack->setValue(0.4, juce::sendNotificationSync);
    undo->onClick();
    check(std::abs(processor->parameters().getRawParameterValue("masterGain")->load() - 0.9f) < 1e-5f,
          "undo of sound-only edit leaves subsequent master automation intact");
}


std::unique_ptr<BeatScholarLinuxProcessor> makeProcessorForWrapper(juce::AudioProcessor::WrapperType wrapper)
{
    juce::AudioProcessor::setTypeOfNextNewPlugin(wrapper);
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    return processor;
}

void testStandaloneTransportIgnoresDeviceClock(const juce::File& output)
{
    struct DeviceClock final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    } clock;
    // AudioProcessorPlayer reports device time, but supplies no musical
    // transport or tempo. Its default isPlaying is false, even after Play.
    clock.info.setTimeInSamples(96000);
    auto processor = makeProcessorForWrapper(juce::AudioProcessor::wrapperType_Standalone);
    auto state = makeBypassTestProject(0);
    state.global.sequencerEnabled = true;
    state.global.beatsPerMinute = 120.0;
    state.patterns[0].lanes = { bsl::LaneState::make(4) };
    for (auto& beat : state.patterns[0].lanes[0].beats)
        beat.slices[0].enabled = true;
    const auto bytes = bsl::statecodec::encode(state);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    processor->disableNonMainBuses();
    processor->setRateAndBufferSizeDetails(48000, 480);
    processor->prepareToPlay(48000, 480);
    processor->setPlayHead(&clock);
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    auto* play = childWithId<juce::TextButton>(*editor, "toolbar.play");
    auto* stop = childWithId<juce::TextButton>(*editor, "toolbar.stop");
    check(play != nullptr && play->isVisible() && !play->getBounds().isEmpty() && stop != nullptr && stop->isVisible() && !stop->getBounds().isEmpty(),
          "standalone exposes the manual's shared Play/Pause control and Stop");
    if (play == nullptr || stop == nullptr) return;
    check(play->isEnabled() && stop->isEnabled(), "standalone Play and Stop remain active");
    auto* tempo = childWithId<juce::Slider>(*editor, "toolbar.bpm");
    check(tempo && tempo->isVisible() && tempo->isEnabled(),
          "standalone retains an editable BPM control");
    const auto refreshTransport = [&](bool playing) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
        do
        {
            juce::Thread::sleep(10);
            juce::Timer::callPendingTimersSynchronously();
            if (play->getToggleState() == playing
                && play->getTitle() == (playing ? "Pause" : "Play")) return;
        }
        while (juce::Time::getMillisecondCounterHiRes() < deadline);
    };
    juce::AudioBuffer<float> audio(2, 480);
    juce::MidiBuffer midi;
    const auto render = [&] {
        midi.clear();
        processor->processBlock(audio, midi);
        int notes = 0;
        for (const auto event : midi) notes += event.getMessage().isNoteOn();
        clock.info.setTimeInSamples(*clock.info.getTimeInSamples() + 480);
        return notes;
    };
    processor->stopStandalone();
    settleMainGain(*processor, 480);
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey,
              juce::ModifierKeys::ctrlModifier, ' ')) && !processor->isStandalonePlaying(),
          "modified Space is not the standalone transport shortcut");
    check(editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
          "Presets & Kits tutorial: standalone Space starts playback");
    check(render() == 1 && processor->isTransportPlaying(),
          "standalone Play starts the sequencer despite a stopped device playhead");
    check(audio.getMagnitude(0, audio.getNumSamples()) > 0.01f,
          "standalone sequencer reaches the main audio output");
    refreshTransport(true);
    check(play->getToggleState() && play->getTitle() == "Pause",
          "playing transport identifies its shared button as Pause");
    saveVisual(*editor, output, "standalone-transport-playing.png");
    check(editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
          "standalone Space pauses playback");
    refreshTransport(false);
    check(!play->getToggleState() && play->getTitle() == "Play",
          "paused transport identifies its shared button as Play");
    saveVisual(*editor, output, "standalone-transport-paused.png");
    for (int block = 0; block < 60; ++block)
        check(render() == 0 && !processor->isTransportPlaying(),
              "standalone Pause stops sequencing while the device clock advances");
    check(editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)),
          "standalone Space resumes playback");
    for (int block = 0; block < 49; ++block)
        check(render() == 0, "standalone Resume preserves the paused musical position");
    check(render() == 1, "standalone tempo uses the local 120 BPM quarter-note interval");
    stop->onClick();
    check(render() == 0 && !processor->isTransportPlaying(), "standalone Stop halts transport");
    play->onClick();
    check(render() == 1, "standalone Play after Stop restarts at beat one");
    auto* sound = childWithId<juce::Slider>(*editor, "sound.0");
    check(sound != nullptr, "locate numeric-entry control");
    if (sound != nullptr)
    {
        editor->mouseDoubleClick(visualEvent(*editor, sound->getBounds().getCentre().toFloat()));
        auto* entry = childWithId<juce::TextEditor>(*editor, "control.value");
        check(entry != nullptr && entry->isVisible(), "numeric entry opens during playback");
        check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))
                  && processor->isStandalonePlaying(),
              "Space in numeric entry does not toggle transport");
        if (entry != nullptr) entry->onEscapeKey();
    }
    processor->setPlayHead(nullptr);
    processor->releaseResources();
}

void testHostedTransportRemainsHostControlled(const juce::File& output)
{
    struct HostClock final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    } clock;
    clock.info.setTimeInSamples(0);
    clock.info.setPpqPosition(0.0);
    clock.info.setBpm(120.0);
    clock.info.setIsPlaying(false);
    auto processor = makeProcessorForWrapper(juce::AudioProcessor::wrapperType_VST3);
    auto state = makeBypassTestProject(0);
    state.global.sequencerEnabled = true;
    state.patterns[0].lanes = { bsl::LaneState::make(4) };
    for (auto& beat : state.patterns[0].lanes[0].beats)
        beat.slices[0].enabled = true;
    const auto bytes = bsl::statecodec::encode(state);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    processor->disableNonMainBuses();
    processor->setRateAndBufferSizeDetails(48000, 480);
    processor->prepareToPlay(48000, 480);
    processor->setPlayHead(&clock);
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    auto* play = childWithId<juce::TextButton>(*editor, "toolbar.play");
    auto* pause = childWithId<juce::TextButton>(*editor, "toolbar.pause");
    auto* stop = childWithId<juce::TextButton>(*editor, "toolbar.stop");
    check(play != nullptr && stop != nullptr && pause != nullptr,
          "hosted editor retains its transport controls");
    if (play == nullptr || pause == nullptr || stop == nullptr) return;
    check(!play->isEnabled() && !pause->isEnabled() && !stop->isEnabled(),
          "manual p9 transport controls are inactive in a VST host");
    processor->stopStandalone();
    play->onClick();
    check(!processor->isStandalonePlaying(),
          "hosted Play callback cannot create a fictitious standalone playing state");
    check(!editor->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey))
              && !processor->isStandalonePlaying(),
          "hosted Space remains available to the DAW transport");
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 100.0;
    do
    {
        juce::Thread::sleep(10);
        juce::Timer::callPendingTimersSynchronously();
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    check(!play->getToggleState() && play->getTitle() == "Play",
          "hosted transport does not show a false standalone Pause state");
    juce::AudioBuffer<float> audio(2, 480);
    juce::MidiBuffer midi;
    const auto render = [&] {
        midi.clear();
        processor->processBlock(audio, midi);
        int notes = 0;
        for (const auto event : midi) notes += event.getMessage().isNoteOn();
        return notes;
    };
    check(render() == 0 && !processor->isTransportPlaying(),
          "stopped host keeps hosted sequencing stopped");
    clock.info.setIsPlaying(true);
    check(render() == 1 && processor->isTransportPlaying(),
          "host Play starts sequencing while plugin transport controls are inactive");
    // Even direct callback delivery must not change the processor's local
    // transport state; only the host clock controls VST sequencing.
    processor->playStandalone();
    pause->onClick();
    check(processor->isStandalonePlaying(), "hosted Pause callback cannot alter local transport state");
    stop->onClick();
    check(processor->isStandalonePlaying(), "hosted Stop callback cannot alter local transport state");
    processor->stopStandalone();
    clock.info.setIsPlaying(false);
    check(render() == 0 && !processor->isTransportPlaying(),
          "host Stop halts hosted sequencing");
    auto* tempo = childWithId<juce::Slider>(*editor, "toolbar.bpm");
    check(tempo && tempo->isVisible() && !tempo->isEnabled(),
          "hosted BPM stays visible as a read-only host-tempo display");
    if (tempo)
    {
        const auto storedBpm = processor->parameters().getRawParameterValue("bpm")->load();
        const auto projectBpm = processor->projectState().global.beatsPerMinute;
        for (const double hostBpm : { 120.0, 137.25, 350.0, 15.0 })
        {
            clock.info.setBpm(hostBpm);
            render();
            const auto until = juce::Time::getMillisecondCounterHiRes() + 100.0;
            do {
                juce::Thread::sleep(10);
                juce::Timer::callPendingTimersSynchronously();
            } while (juce::Time::getMillisecondCounterHiRes() < until);
            check(std::abs(tempo->getValue() - hostBpm) < 0.01,
                  "hosted BPM follows fractional and out-of-standalone-range host tempos");
        }
        const auto centre = tempo->getBounds().getCentre().toFloat();
        editor->mouseDown(visualEvent(*editor, centre, juce::ModifierKeys::altModifier));
        editor->mouseUp(visualEvent(*editor, centre));
        editor->mouseDoubleClick(visualEvent(*editor, centre).getEventRelativeTo(tempo));
        auto* input = childWithId<juce::TextEditor>(*editor, "control.value");
        check(!input || !input->isVisible(), "disabled host BPM cannot open numeric editing");
        check(std::abs(tempo->getValue() - 15.0) < 0.01
                  && processor->parameters().getRawParameterValue("bpm")->load() == storedBpm
                  && processor->projectState().global.beatsPerMinute == projectBpm
                  && !processor->projectEditor().canUndo(),
              "host BPM display and attempted edits never write the standalone tempo or history");
    }
    saveVisual(*editor, output, "hosted-transport.png");
    processor->setPlayHead(nullptr);
    processor->releaseResources();
}

void testMidiExportUsesTheActiveTempo(const juce::File& output)
{
    struct HostClock final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    } clock;
    clock.info.setBpm(140.0);
    clock.info.setIsPlaying(false);
    for (const auto wrapper : { juce::AudioProcessor::wrapperType_VST3,
                                juce::AudioProcessor::wrapperType_Standalone })
    {
        auto processor = makeProcessorForWrapper(wrapper);
        processor->projectState().global.beatsPerMinute = 120.0;
        processor->disableNonMainBuses();
        processor->setRateAndBufferSizeDetails(48000, 480);
        processor->prepareToPlay(48000, 480);
        processor->setPlayHead(&clock);
        juce::AudioBuffer<float> audio(2, 480);
        juce::MidiBuffer messages;
        processor->processBlock(audio, messages);
        BeatScholarLinuxEditor editor(*processor);
        const auto directory = output.getChildFile(wrapper == juce::AudioProcessor::wrapperType_VST3
            ? "host-tempo-export" : "standalone-tempo-export");
        const auto file = editor.writePatternMidi(0, directory);
        juce::FileInputStream input(file);
        juce::MidiFile midi;
        check(input.openedOk() && midi.readFrom(input) && midi.getNumTracks() == 1,
              "tempo export produces a readable MIDI track");
        double exportedBpm = 0.0;
        if (midi.getNumTracks() == 1)
            for (int index = 0; index < midi.getTrack(0)->getNumEvents(); ++index)
            {
                const auto& event = midi.getTrack(0)->getEventPointer(index)->message;
                if (event.isTempoMetaEvent()) exportedBpm = 60.0 / event.getTempoSecondsPerQuarterNote();
            }
        const double expected = wrapper == juce::AudioProcessor::wrapperType_VST3 ? 140.0 : 120.0;
        check(std::abs(exportedBpm - expected) < 0.001,
              "MIDI tempo matches the host in a plugin and the saved tempo in standalone");
        check(processor->projectState().global.beatsPerMinute == 120.0
                  && processor->parameters().getRawParameterValue("bpm")->load() == 120.0f
                  && !processor->projectEditor().canUndo(),
              "exporting at host tempo leaves the saved tempo and edit history untouched");
        processor->setPlayHead(nullptr);
        processor->releaseResources();
    }
}

void testStandaloneRestoresMultiOutputAsStereo()
{
    for (const auto wrapper : { juce::AudioProcessor::wrapperType_Standalone,
                                juce::AudioProcessor::wrapperType_VST3 })
    {
        const bool standalone = wrapper == juce::AudioProcessor::wrapperType_Standalone;
        auto processor = makeProcessorForWrapper(wrapper);
        auto state = makeBypassTestProject(3);
        state.global.multiOutput = true;
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        check(processor->projectState().global.multiOutput == !standalone
                  && (processor->parameters().getRawParameterValue("routing")->load() >= 0.5f) == !standalone,
              "standalone restores stereo routing; VST3 retains saved multi-output routing");
        processor->disableNonMainBuses();
        processor->setRateAndBufferSizeDetails(48000, 480);
        processor->prepareToPlay(48000, 480);
        settleMainGain(*processor, 480);
        juce::AudioBuffer<float> audio(2, 480);
        juce::MidiBuffer midi;
        processor->triggerPad(3, 1.0f);
        processor->processBlock(audio, midi);
        check((audio.getMagnitude(0, audio.getNumSamples()) > 0.01f) == standalone,
              "restored standalone preset remains audible on stereo; VST3 keeps its separate pad outputs");
        // The standalone has no routing selector. Even an external parameter
        // write must not strand audio on output buses that its wrapper disables.
        processor->parameters().getParameter("routing")->setValueNotifyingHost(1.0f);
        processor->triggerPad(3, 1.0f);
        midi.clear();
        processor->processBlock(audio, midi);
        check((audio.getMagnitude(0, audio.getNumSamples()) > 0.01f) == standalone,
              "effective standalone routing stays stereo after a multi-output parameter write");
        juce::MemoryBlock saved;
        processor->getStateInformation(saved);
        auto restored = bsl::ProjectState::makeDefault();
        check(bsl::statecodec::decode(saved.getData(), saved.getSize(), restored)
                  && restored.global.multiOutput == !standalone,
              "saved routing reflects standalone stereo or VST3 multi-output");
        processor->releaseResources();
    }
}


void testQueuedPatternsSurviveStructuralEdits()
{
    struct PlayHead final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };
    // Each pattern plays a distinct pad every quarter note. The incoming C
    // trigger is queued at sample 100 for the boundary at sample 24000.
    for (int edit = 0; edit < 5; ++edit)
    {
        PlayHead clock;
        clock.info.setIsPlaying(true);
        clock.info.setBpm(120.0);
        auto processor = makeProcessorForWrapper(juce::AudioProcessor::wrapperType_VST3);
        auto state = bsl::ProjectState::makeDefault();
        state.global.samplerEnabled = false;
        state.patterns.resize(3, state.patterns.front());
        for (std::size_t i = 0; i < state.patterns.size(); ++i)
        {
            auto& pattern = state.patterns[i];
            pattern.name = std::string(1, static_cast<char>('A' + i));
            pattern.triggerNote = static_cast<std::uint8_t>(12 + i);
            pattern.quantization = bsl::PatternQuantization::quarter;
            pattern.lanes = { bsl::LaneState::make(4) };
            for (auto& beat : pattern.lanes[0].beats)
                beat = bsl::BeatState::withSubdivision(1, static_cast<std::uint8_t>(i), 0.8f);
        }
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, 1024);
        processor->prepareToPlay(48000, 1024);
        processor->setPlayHead(&clock);
        std::vector<std::pair<int, int>> notes;
        for (int block = 0; block < 25; ++block)
        {
            if (block == 1)
            {
                auto& editor = processor->projectEditor();
                if (edit == 0)
                    check(editor.removePattern(1), "remove B before queued C");
                else if (edit == 1)
                {
                    check(editor.duplicatePattern(1), "insert a copy before queued C");
                    // Keep A selected: this tests a structural shift rather
                    // than an intentional immediate UI pattern change.
                    editor.state().activePattern = 0;
                }
                else
                {
                    check(editor.removePattern(2), "remove queued C");
                    if (edit == 3)
                    {
                        check(editor.addPattern(), "reuse removed pattern's index");
                        editor.state().patterns.back().name = "Replacement";
                        for (auto& lane : editor.state().patterns.back().lanes)
                            for (auto& beat : lane.beats)
                                beat = bsl::BeatState::withSubdivision(1, 3, 0.8f);
                        editor.state().activePattern = 0;
                    }
                    else if (edit == 4)
                        check(editor.undo(), "undo target removal before next audio callback");
                }
                processor->markProjectDirty();
                processor->publishProjectStateIfNeeded();
            }
            clock.info.setTimeInSamples(block * 1024);
            clock.info.setPpqPosition(block * 1024.0 / 24000.0);
            juce::AudioBuffer<float> audio(2, 1024);
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 14, 0.8f), 100);
            processor->processBlock(audio, midi);
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    notes.emplace_back(block * 1024 + event.samplePosition,
                                       event.getMessage().getNoteNumber());
        }
        const bool targetExists = edit < 2 || edit == 4;
        const std::vector<std::pair<int, int>> expected { { 0, 36 },
                                                        { 24000, targetExists ? 38 : 36 } };
        check(notes == expected, "queued pattern retains identity or cancels after edit "
                                 + juce::String(edit));
        const int requested = processor->takeRequestedPatternForUi();
        if (targetExists)
            check(requested >= 0 && requested < static_cast<int>(processor->projectState().patterns.size())
                      && processor->projectState().patterns[static_cast<std::size_t>(requested)].name == "C",
                  "queued pattern UI feedback resolves its current index after edit " + juce::String(edit));
        else
            check(requested == -1, "cancelled target does not select a substitute pattern");
        processor->setPlayHead(nullptr);
        processor->releaseResources();
    }
}


void testMidiExportGatesFollowPlayback(const juce::File& output)
{
    // Read the serialized events without JUCE inventing missing note-offs.
    // Dense slices, swing, shared notes and mixed meters must each retain one
    // release per hit, at the same gate boundary used by the live sequencer.
    for (int mode = 0; mode < 4; ++mode)
    {
        BeatScholarLinuxProcessor processor;
        auto& state = processor.projectState();
        auto& pattern = state.patterns.front();
        pattern.name = "MIDI gate case " + std::to_string(mode);
        pattern.bars = mode == 3 ? std::vector<bsl::BarState> { bsl::BarState::make(3, 8), bsl::BarState::make(1, 16) }
                                : std::vector<bsl::BarState> { bsl::BarState::make(1, 4) };
        pattern.lanes = { bsl::LaneState::make(mode == 3 ? 4 : 1) };
        for (auto& beat : pattern.lanes.front().beats)
            beat = bsl::BeatState::withSubdivision(mode == 0 ? 45 : 7, 0, 0.8f);
        state.global.swingPercent = mode == 1 ? 200.0 : mode == 3 ? -200.0 : 0.0;
        if (mode == 2)
        {
            pattern.lanes.push_back(bsl::LaneState::make(1));
            pattern.lanes.back().beats.front() = bsl::BeatState::withSubdivision(3, 0, 0.5f);
        }
        bsl::TransportFrame frame;
        frame.sampleRate = 1920.0; // 120 BPM: one sample per MIDI tick.
        frame.bpm = 120.0;
        frame.playing = true;
        frame.blockSize = static_cast<int>(std::llround(pattern.durationInQuarterNotes() * 960));
        const auto played = bsl::Sequencer::render(state, frame);
        BeatScholarLinuxEditor editor(processor);
        const auto file = editor.writePatternMidi(0, output.getChildFile("midi-export"));
        juce::FileInputStream input(file);
        juce::MidiFile midi;
        check(input.openedOk() && midi.readFrom(input, false) && midi.getNumTracks() == 1,
              "gate regression reads the raw exported MIDI track");
        if (midi.getNumTracks() != 1) continue;
        std::vector<std::pair<int, int>> exportedOns, exportedOffs, expectedOffs;
        for (int i = 0; i < midi.getTrack(0)->getNumEvents(); ++i)
        {
            const auto& m = midi.getTrack(0)->getEventPointer(i)->message;
            if (m.isNoteOn()) exportedOns.emplace_back(static_cast<int>(m.getTimeStamp()), m.getNoteNumber());
            if (m.isNoteOff()) exportedOffs.emplace_back(static_cast<int>(m.getTimeStamp()), m.getNoteNumber());
        }
        std::vector<std::pair<int, int>> expectedOns;
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            const auto& hit = played[i];
            expectedOns.emplace_back(hit.sampleOffset, hit.note);
            int end = std::min(frame.blockSize, hit.sampleOffset + static_cast<int>(hit.gateFrames));
            for (std::size_t next = i + 1; next < played.size(); ++next)
                if (played[next].note == hit.note)
                {
                    end = std::min(end, played[next].sampleOffset);
                    break;
                }
            expectedOffs.emplace_back(end, hit.note);
        }
        std::sort(expectedOffs.begin(), expectedOffs.end());
        check(exportedOns == expectedOns, "MIDI export keeps every live onset in case " + juce::String(mode));
        check(exportedOffs == expectedOffs,
              "MIDI export releases each hit once at its playback gate or retrigger in case " + juce::String(mode));
    }
}

void testShiftExportPreservesPlayingAndQueuedPatterns()
{
    struct PlayHead final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };
    for (const bool queued : { false, true })
    {
        PlayHead clock;
        clock.info.setIsPlaying(true);
        clock.info.setBpm(120.0);
        auto processor = makeProcessorForWrapper(juce::AudioProcessor::wrapperType_VST3);
        auto state = bsl::ProjectState::makeDefault();
        state.ui.patternMode = true;
        state.global.samplerEnabled = false;
        state.patterns.resize(3, state.patterns.front());
        for (std::size_t i = 0; i < state.patterns.size(); ++i)
        {
            auto& pattern = state.patterns[i];
            pattern.triggerNote = static_cast<std::uint8_t>(12 + i);
            pattern.quantization = bsl::PatternQuantization::quarter;
            pattern.lanes = { bsl::LaneState::make(4) };
            for (auto& beat : pattern.lanes.front().beats)
                beat = bsl::BeatState::withSubdivision(1, static_cast<std::uint8_t>(i), 0.8f);
        }
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, 1024);
        processor->prepareToPlay(48000, 1024);
        processor->setPlayHead(&clock);
        BeatScholarLinuxEditor editor(*processor);
        std::vector<std::pair<int, int>> afterExport;
        for (int block = 0; block < 25; ++block)
        {
            if (block == 1)
            {
                const auto event = visualEvent(editor, {1205.0f, 218.0f},
                    juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
                editor.mouseDown(event);
                check(processor->projectState().activePattern == 0,
                      "Shift export does not select the dragged inactive pattern");
                editor.mouseUp(event);
            }
            clock.info.setTimeInSamples(block * 1024);
            clock.info.setPpqPosition(block * 1024.0 / 24000.0);
            juce::AudioBuffer<float> audio(2, 1024);
            juce::MidiBuffer midi;
            if (queued && block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 14, 0.8f), 100);
            processor->processBlock(audio, midi);
            if (block > 0)
                for (const auto event : midi)
                    if (event.getMessage().isNoteOn())
                        afterExport.emplace_back(block * 1024 + event.samplePosition,
                                                 event.getMessage().getNoteNumber());
        }
        check(afterExport == std::vector<std::pair<int, int>> {{24000, queued ? 38 : 36}},
              queued ? "Shift export preserves the MIDI-queued pattern switch"
                     : "Shift export preserves the playing pattern and its phase");
        processor->setPlayHead(nullptr);
        processor->releaseResources();
    }
}

void testDirectPatternClickOverridesMidiSwitch()
{
    struct PlayHead final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };
    for (const bool queued : { false, true })
    {
        PlayHead clock;
        clock.info.setIsPlaying(true);
        clock.info.setBpm(120.0);
        auto processor = makeProcessorForWrapper(juce::AudioProcessor::wrapperType_VST3);
        auto state = bsl::ProjectState::makeDefault();
        state.ui.patternMode = true;
        state.global.samplerEnabled = false;
        state.patterns.resize(2, state.patterns.front());
        for (std::size_t i = 0; i < state.patterns.size(); ++i)
        {
            state.patterns[i].triggerNote = static_cast<std::uint8_t>(12 + i);
            state.patterns[i].quantization = queued ? bsl::PatternQuantization::quarter : bsl::PatternQuantization::off;
            state.patterns[i].lanes = { bsl::LaneState::make(4) };
            for (auto& beat : state.patterns[i].lanes[0].beats)
                beat = bsl::BeatState::withSubdivision(1, static_cast<std::uint8_t>(i), 0.8f);
        }
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, 1024);
        processor->prepareToPlay(48000, 1024);
        processor->setPlayHead(&clock);
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        std::vector<std::pair<int, int>> notesAfterClick;
        for (int block = 0; block < 25; ++block)
        {
            if (block == 1)
            {
                const auto event = visualEvent(*editor, { 1205.0f, 146.0f });
                editor->mouseDown(event);
                editor->mouseUp(event);
                check(processor->takeRequestedPatternForUi() == -1,
                      "explicit click discards MIDI feedback not yet seen by the UI timer");
            }
            clock.info.setTimeInSamples(block * 1024);
            clock.info.setPpqPosition(block * 1024.0 / 24000.0);
            juce::AudioBuffer<float> audio(2, 1024);
            juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 13, 0.8f), 100);
            processor->processBlock(audio, midi);
            if (block > 0)
                for (const auto event : midi)
                    if (event.getMessage().isNoteOn())
                        notesAfterClick.emplace_back(block * 1024 + event.samplePosition,
                                                     event.getMessage().getNoteNumber());
        }
        check(notesAfterClick == std::vector<std::pair<int, int>> { {24000, 36} },
              queued ? "clicking active pattern cancels a queued MIDI switch"
                     : "clicking displayed pattern overrides a MIDI switch before timer feedback");
        check(processor->takeRequestedPatternForUi() == -1 && processor->projectState().activePattern == 0,
              "late MIDI feedback cannot restore the overridden selection");
        processor->selectPatternFromUi(1);
        check(processor->projectEditor().removePattern(1)
                  && processor->projectEditor().addPattern(),
              "replace clicked pattern before audio consumes selection");
        processor->projectState().activePattern = 0;
        processor->projectState().patterns[1].triggerNote = 13;
        processor->projectState().patterns[1].quantization = bsl::PatternQuantization::off;
        processor->markProjectDirty();
        processor->publishProjectStateIfNeeded();
        clock.info.setTimeInSamples(25600);
        clock.info.setPpqPosition(25600.0 / 24000.0);
        juce::AudioBuffer<float> audio(2, 1024);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 13, 0.8f), 100);
        processor->processBlock(audio, midi);
        check(processor->takeRequestedPatternForUi() == 1,
              "removing an unconsumed UI selection does not suppress future MIDI feedback");
        processor->setPlayHead(nullptr);
        processor->releaseResources();
    }
}

void testOutgoingMidiReleasesOnTransportChanges()
{
    struct PlayHead final : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };
    for (int mode = 0; mode < 3; ++mode)
    {
        PlayHead playHead;
        playHead.info.setIsPlaying(true);
        playHead.info.setTimeInSamples(0);
        playHead.info.setPpqPosition(0.0);
        playHead.info.setBpm(120.0);
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto state = makeBypassTestProject(0);
        state.global.sequencerEnabled = true;
        state.global.samplerEnabled = false;
        state.patterns[0].lanes = { bsl::LaneState::make(4) };
        for (auto& beat : state.patterns[0].lanes[0].beats)
            beat.slices[0].enabled = false;
        state.patterns[0].lanes[0].beats[0].slices[0].enabled = true;
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, 64);
        processor->prepareToPlay(48000, 64);
        processor->setPlayHead(&playHead);
        juce::AudioBuffer<float> audio(2, 64);
        juce::MidiBuffer midi;
        processor->processBlock(audio, midi);
        int ons = 0;
        for (const auto event : midi) ons += event.getMessage().isNoteOn();
        check(ons == 1, "transport-change test starts one held outgoing note");
        playHead.info.setTimeInSamples(64);
        playHead.info.setPpqPosition(64.0 / 24000.0);
        if (mode == 0)
            playHead.info.setIsPlaying(false);
        else if (mode == 1)
        {
            processor->projectState().global.sequencerEnabled = false;
            processor->markProjectDirty();
            processor->publishProjectStateIfNeeded();
        }
        else
        {
            playHead.info.setTimeInSamples(50000);
            playHead.info.setPpqPosition(50000.0 / 24000.0);
        }
        midi.clear();
        processor->processBlock(audio, midi);
        int offs = 0;
        for (const auto event : midi)
            if (event.getMessage().isNoteOff())
            {
                ++offs;
                check(event.samplePosition == 0, "transport change releases outgoing note at block start");
            }
        check(offs == 1, "stop, sequencer disable, and host seek each release held outgoing MIDI");
        processor->setPlayHead(nullptr);
        processor->releaseResources();
    }
}

void testPublishedDisplayOptions(const juce::File& output)
{
    auto base = makeBypassTestProject(0);
    base.kit.pads[0].midiNote = 61;
    auto json = juce::JSON::parse(bsl::statecodec::encodePresetJson(base));
    auto* ui = json.getProperty("ui", {}).getDynamicObject();
    check(ui != nullptr, "display fixture contains UI settings");
    if (ui == nullptr) return;
    ui->setProperty("darkMode", true);
    ui->setProperty("middleCOctave", 4);
    ui->setProperty("midiFlats", true);
    ui->setProperty("midiNoteNames", true);
    auto restored = base;
    check(bsl::statecodec::decodePresetJson(juce::JSON::toString(json), restored), "display preferences load");
    const auto saved = juce::JSON::parse(bsl::statecodec::encodePresetJson(restored)).getProperty("ui", {});
    check(bool(saved.getProperty("darkMode", false)) && int(saved.getProperty("middleCOctave", 3)) == 4
          && bool(saved.getProperty("midiFlats", false)), "host state retains published display preferences");
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    const auto bytes = bsl::statecodec::encode(restored);
    processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setSize(1280, 720);
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    int neutral = 0;
    for (int y = 110; y < 500; y += 10)
        for (int x = 70; x < 1120; x += 10)
        {
            const auto pixel = image.getPixelAt(x, y);
            neutral += pixel.getRed() == pixel.getGreen() && pixel.getGreen() == pixel.getBlue()
                       && pixel.getRed() < 50;
        }
    check(neutral > 2500, "dark theme uses reference neutral black/gray sequencer surfaces");
    saveVisual(*editor, output, "reference-dark.png");
    auto* pad = dynamic_cast<juce::TextButton*>(editor->findChildWithID("pad.0"));
    check(pad && pad->getTooltip().contains("Db4"), "MIDI display applies flats and middle-C convention");

    ui->setProperty("midiNoteNames", false);
    check(bsl::statecodec::decodePresetJson(juce::JSON::toString(json), restored), "numeric MIDI display loads");
    const auto numeric = bsl::statecodec::encode(restored);
    processor->setStateInformation(numeric.getData(), static_cast<int>(numeric.getSize()));
    for (int n = 0; n < 100 && pad && !pad->getTooltip().contains("MIDI 61"); ++n)
    {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep(10);
    }
    check(pad && pad->getTooltip().contains("MIDI 61"), "numeric MIDI display refreshes after host recall");
}

void testHostMidiMapReportsCurrentPadNames()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    const auto state = processor->projectState();
    for (const auto& pad : state.kit.pads)
    {
        const auto name = processor->getNameForMidiNoteNumber(pad.midiNote, 1);
        check(name && *name == juce::String(pad.name), "host receives mapped drum pad name");
    }
    check(!processor->getNameForMidiNoteNumber(-1, 1) && !processor->getNameForMidiNoteNumber(128, 1),
          "host map queries reject out of range notes");
    processor->projectState().kit.pads[0].midiNote = 10;
    processor->projectState().kit.pads[0].name = "Mapped kick";
    processor->markProjectDirty();
    processor->publishProjectStateIfNeeded();
    const auto remapped = processor->getNameForMidiNoteNumber(10, 16);
    check(remapped && *remapped == "Mapped kick" && !processor->getNameForMidiNoteNumber(36, 1),
          "host map follows remapping and pad name changes on any MIDI input channel");
}

void testDenseOfflineBlockKeepsEveryHit()
{
    constexpr int frames = 16384;
    auto state = makeBypassTestProject(0);
    state.global.sequencerEnabled = true;
    state.global.beatsPerMinute = 300.0;
    auto& pattern = state.patterns[0];
    pattern.bars = { bsl::BarState::make(1, 16) };
    pattern.lanes.assign(16, bsl::LaneState::make(1));
    for (std::size_t lane = 0; lane < pattern.lanes.size(); ++lane)
        pattern.lanes[lane].beats[0] = bsl::BeatState::withSubdivision(45, static_cast<std::uint8_t>(lane), 0.5f);
    const auto bytes = bsl::statecodec::encode(state);
    auto render = [&](int blockSize) {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, blockSize);
        processor->prepareToPlay(48000, blockSize);
        std::vector<std::pair<int, int>> notes;
        std::vector<float> audio;
        for (int offset = 0; offset < frames; offset += blockSize)
        {
            juce::AudioBuffer<float> buffer(2, std::min(blockSize, frames - offset));
            juce::MidiBuffer midi;
            processor->processBlock(buffer, midi);
            for (const auto event : midi)
                if (event.getMessage().isNoteOn())
                    notes.emplace_back(offset + event.samplePosition, event.getMessage().getNoteNumber());
            audio.insert(audio.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
        }
        std::sort(notes.begin(), notes.end());
        return std::pair { notes, audio };
    };
    const auto small = render(128), large = render(frames);
    check(small.first.size() == 4928, "dense reference contains all 308 times and 16 lanes");
    check(large.first == small.first, "large offline block preserves every dense pattern hit");
    float error = 0.0f;
    for (std::size_t n = 0; n < small.second.size(); ++n)
        error = std::max(error, std::abs(small.second[n] - large.second[n]));
    check(error < 1.0e-5f, "dense offline audio is independent of host block size");
}

void testIncomingMidiBeyondFormerCapacity()
{
    const auto state = makeBypassTestProject(0);
    const auto bytes = bsl::statecodec::encode(state);
    auto render = [&](bool dense) {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, 512);
        processor->prepareToPlay(48000, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        if (dense)
            for (int i = 0; i < 8192; ++i)
                midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.7f), 64);
        processor->processBlock(buffer, midi);
        return std::vector<float>(buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
    };
    check(render(true) == render(false), "incoming MIDI after 8192 events still reaches sampler");
}

void testFactorySamplesRespectXdgDataHome(const juce::File& output)
{
    const auto previous = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", "");
    const auto xdg = output.getChildFile("xdg-root");
    const auto samples = xdg.getChildFile("BeatScholarLinux/Factory/Samples");
    check(samples.createDirectory().wasOk(), "create isolated XDG sample directory");
    const auto fixture = makeBypassTestProject(0);
    juce::MemoryOutputStream wav;
    check(juce::Base64::convertFromBase64(wav, juce::String(fixture.kit.pads[0].customSampleBase64.value())), "decode synthetic XDG sample");
    const auto file = samples.getChildFile("xdg-synthetic.wav");
    check(file.replaceWithData(wav.getData(), wav.getDataSize()), "write synthetic XDG sample");
    setenv("XDG_DATA_HOME", xdg.getFullPathName().toRawUTF8(), 1);
    check(bsl::dataRoot() == xdg.getChildFile("BeatScholarLinux"), "editor and loader share configured data root");
    const auto manual = bsl::dataRoot().getChildFile("Manual.pdf");
    manual.deleteFile();
    check(bsl::manualUrl().toString(false).startsWith("https://downloads.modalics.com/"), "missing local manual falls back to official manual");
    manual.replaceWithText("synthetic local manual placeholder");
    check(bsl::manualUrl().isLocalFile() && bsl::manualUrl().getLocalFile() == manual, "F1 prefers local manual in configured data root");
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->setRateAndBufferSizeDetails(48000, 64);
    processor->prepareToPlay(48000, 64);
    check(processor->setFactorySample(0, 0, "xdg-synthetic.wav"), "factory sample chooser loads from XDG_DATA_HOME");
    if (previous.isEmpty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", previous.toRawUTF8(), 1);
}

void testSampleReplacementPreservesOtherPads(const juce::File& output)
{
    const auto previous = juce::SystemStats::getEnvironmentVariable("XDG_DATA_HOME", "");
    const auto xdg = output.getChildFile("sample-isolation-xdg");
    setenv("XDG_DATA_HOME", xdg.getFullPathName().toRawUTF8(), 1);
    const auto samples = bsl::dataRoot().getChildFile("Factory/Samples");
    check(samples.createDirectory().wasOk(), "create sample isolation fixtures");
    auto state = makeBypassTestProject(1);
    state.kit.pads[1].oneShot = true;
    juce::MemoryOutputStream wav;
    check(juce::Base64::convertFromBase64(wav, juce::String(state.kit.pads[0].customSampleBase64.value())),
          "decode sample isolation fixture");
    const auto file = samples.getChildFile("replacement.wav");
    check(file.replaceWithData(wav.getData(), wav.getDataSize()), "write sample isolation fixture");
    const auto bytes = bsl::statecodec::encode(state);
    const auto render = [&](int operation) {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, 64);
        processor->prepareToPlay(48000, 64);
        settleMainGain(*processor, 64);
        juce::AudioBuffer<float> buffer(2, 64);
        juce::MidiBuffer midi;
        processor->triggerPad(1, 0.7f);
        processor->processBlock(buffer, midi);
        check(buffer.getMagnitude(0, 64) > 0.01f, "unrelated pad is sounding before sample replacement");
        if (operation == 0)
            processor->clearCustomSample(0);
        else if (operation > 0)
            check(processor->setFactorySample(0, static_cast<std::size_t>(operation - 1), "replacement.wav"),
                  "replace the selected factory sample layer");
        if (operation >= 0)
            check(processor->projectState().kit.pads[0].customSampleBase64.empty(),
                  "sample replacement clears the selected pad's custom payload");
        midi.clear();
        processor->processBlock(buffer, midi);
        return std::vector<float>(buffer.getReadPointer(0), buffer.getReadPointer(0) + 64);
    };
    const auto reference = render(-1);
    check(std::any_of(reference.begin(), reference.end(), [](float value) { return std::abs(value) > 0.01f; }),
          "unrelated sample continues beyond the replacement point");
    for (const int operation : { 0, 1, 2 })
        check(render(operation) == reference,
              juce::String(operation == 0 ? "custom sample reset" : "factory sample replacement")
                  + " preserves an unrelated pad's uninterrupted audio");
    if (previous.isEmpty()) unsetenv("XDG_DATA_HOME");
    else setenv("XDG_DATA_HOME", previous.toRawUTF8(), 1);
}

void testSequencerMidiNotesHaveFiniteLifetimes()
{
    for (const int blockSize : { 64, 257 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto state = makeBypassTestProject(0);
        state.global.sequencerEnabled = true;
        state.global.samplerEnabled = false;
        state.patterns[0].lanes = { bsl::LaneState::make(4) };
        for (auto& beat : state.patterns[0].lanes[0].beats)
            for (auto& slice : beat.slices) slice.enabled = false;
        auto& beat = state.patterns[0].lanes[0].beats[0];
        beat = bsl::BeatState::withSubdivision(3, 0, 0.8f);
        const auto bytes = bsl::statecodec::encode(state);
        processor->setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        processor->setRateAndBufferSizeDetails(48000, blockSize);
        processor->prepareToPlay(48000, blockSize);
        juce::AudioBuffer<float> audio(2, blockSize);
        int held = 0, ons = 0, offs = 0;
        for (int frame = 0; frame < 30000; frame += blockSize)
        {
            juce::MidiBuffer midi;
            // Incoming performance notes are consumed by the instrument;
            // they must not share generated outgoing note lifetimes.
            if (frame == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 1);
            if (frame == blockSize) midi.addEvent(juce::MidiMessage::noteOff(1, 36), 3);
            processor->processBlock(audio, midi);
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();
                if (message.isNoteOn()) { ++held; ++ons; }
                if (message.isNoteOff()) { --held; ++offs; }
                check(held >= 0 && held <= 1, "sequencer balances same-note retriggers");
            }
        }
        check(ons == 3 && offs == 3 && held == 0,
              "outgoing sequencer MIDI releases every note across callback boundaries");
        processor->releaseResources();
    }
}

void testRenderedLevelMetersAndLayout(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->projectState().global.sequencerEnabled = false;
    processor->markProjectDirty();
    processor->publishProjectStateIfNeeded();
    processor->setRateAndBufferSizeDetails(48000, 64);
    processor->prepareToPlay(48000, 64);
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    auto* volume = childWithId<juce::Slider>(*editor, "sound.5");
    auto* pan = childWithId<juce::Slider>(*editor, "sound.6");
    check(volume != nullptr && volume->getSliderStyle() == juce::Slider::LinearVertical
          && pan != nullptr && pan->getSliderStyle() == juce::Slider::LinearHorizontal,
          "manual Level section has a vertical fader and horizontal pan slider");
    juce::AudioBuffer<float> audio(2, 64);
    juce::MidiBuffer midi;
    processor->processBlock(audio, midi);
    check(processor->takeOutputPeak(0) == 0.0f && processor->takePadPeak(0) == 0.0f,
          "meters remain silent with raised faders but no audio");
    processor->triggerPad(0, 1.0f);
    float expectedLeft = 0.0f, expectedRight = 0.0f;
    for (int block = 0; block < 20; ++block)
    {
        midi.clear();
        processor->processBlock(audio, midi);
        expectedLeft = std::max(expectedLeft, audio.getMagnitude(0, 0, 64));
        expectedRight = std::max(expectedRight, audio.getMagnitude(1, 0, 64));
    }
    const auto left = processor->takeOutputPeak(0);
    const auto right = processor->takeOutputPeak(1);
    check(left > 0.0f && std::abs(left - expectedLeft) < 1e-6f
          && std::abs(right - expectedRight) < 1e-6f,
          "master meters accumulate actual output peaks until the UI reads them");
    check(processor->takeOutputPeak(0) == 0.0f && processor->takePadPeak(0) > 0.0f
          && processor->takePadPeak(1) == 0.0f,
          "reading meter clears its peak without cross-pad contamination");
    processor->triggerPad(0, 1.0f);
    midi.clear();
    processor->processBlock(audio, midi);
    int illuminatedPixels = 0;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
    do
    {
        juce::Timer::callPendingTimersSynchronously();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        illuminatedPixels = 0;
        for (int y = 612; y < 704; ++y)
            for (int x = 1233; x < 1249; ++x)
            {
                const auto colour = image.getPixelAt(x, y);
                illuminatedPixels += colour.getGreen() > 180 && colour.getRed() > 80 && colour.getBlue() < 100;
            }
        if (illuminatedPixels > 20) break;
        juce::Thread::sleep(10);
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
    check(illuminatedPixels > 20, "rendered audio illuminates the real master meter in the editor");
    saveVisual(*editor, output, "live-level-meters.png");
    processor->releaseResources();
}

void testTutorialEffectDragValue(const juce::File& output)
{
    for (const int width : { 960, 1280 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setSize(width, width * 9 / 16);
        editor->setVisible(true);
        auto* readout = childWithId<juce::Label>(*editor, "effect.drag-value");
        check(readout != nullptr, "tutorial effect drag readout exists");
        if (readout == nullptr) continue;
        check(!readout->isVisible(), "drag readout is hidden at rest");
        bool interceptsSelf = true, interceptsChildren = true;
        readout->getInterceptsMouseClicks(interceptsSelf, interceptsChildren);
        check(!interceptsSelf && !interceptsChildren, "drag readout cannot steal mouse input");
        for (const int index : { 8, 9 })
        {
            auto* control = childWithId<juce::Slider>(*editor, ("effect." + juce::String(index)).toRawUTF8());
            check(control != nullptr, "tutorial Dist/Gain control exists");
            if (control == nullptr) continue;
            const double initial = index == 8 ? 0.72 : 0.47;
            control->setValue(initial, juce::sendNotificationSync);
            // Wheel/keyboard gestures do not reuse a previous mouse anchor.
            control->onDragStart();
            check(!readout->isVisible(), "non-mouse gesture leaves the drag readout hidden");
            control->onDragEnd();
            const float scale = static_cast<float>(width) / 1280.0f;
            const auto local = juce::Point<float>(static_cast<float>(control->getWidth()) * 0.8f,
                index == 9 ? control->getPositionOfValue(initial)
                           : static_cast<float>(control->getHeight()) * 0.5f);
            const auto down = visualEvent(*editor, control->getPosition().toFloat() + local)
                .getEventRelativeTo(control);
            control->mouseDown(down);
            check(readout->isVisible(), "mouse down shows tutorial numeric readout");
            const auto bounds = readout->getBounds();
            check(bounds.getWidth() == juce::roundToInt(80.0f * scale)
                      && bounds.getHeight() == juce::roundToInt(50.0f * scale),
                  "readout uses the tutorial box dimensions at both editor scales");
            check(std::abs(bounds.getCentreX() - (control->getX() + local.x)) <= 1.0f
                      && bounds.getBottom() == control->getY() - juce::roundToInt(12.0f * scale),
                  "readout anchors above the initial mouse x and the control label");
            if (index == 8)
                check(readout->getText() == "72%", "Dist readout displays a whole percentage");
            else
                check(readout->getText() == juce::String(control->getValue(), 2),
                      "Gain readout displays two decimals");
            const auto screenshot = juce::String(index == 8 ? "dist" : "gain")
                + "-drag-value-" + juce::String(width) + ".png";
            saveVisual(*editor, output, screenshot.toRawUTF8());
            const auto startValue = control->getValue();
            for (const float delta : { -8.0f, -16.0f })
            {
                control->mouseDrag(down.withNewPosition(local + juce::Point<float>(0.0f, delta * scale)));
                check(readout->getBounds() == bounds, "readout remains fixed while mouse moves");
                check(readout->getText() == (index == 8
                    ? juce::String(juce::roundToInt(control->getValue() * 100.0)) + "%"
                    : juce::String(control->getValue(), 2)), "readout follows live drag value");
            }
            check(std::abs(control->getValue() - startValue) > 0.01,
                  "real mouse drag changes the effect value");
            const auto& pads = processor->projectState().kit.pads;
            check(index == 8 ? std::abs(pads[0].distortionMix - control->getValue()) < 1e-6
                             : std::all_of(pads.begin(), pads.end(), [control](const auto& pad) {
                                   return std::abs(pad.distortionDrive - control->getValue()) < 1e-6;
                               }), "drag publishes the selected send or shared Gain to the model");
            control->mouseUp(down.withNewPosition(local + juce::Point<float>(0.0f, -16.0f * scale)));
            check(!readout->isVisible(), "mouse release removes the tutorial readout");
            childWithId<juce::TextButton>(*editor, "toolbar.undo")->onClick();
            check(std::abs(control->getValue() - initial) < 1e-6,
                  "one undo restores the entire Dist/Gain drag");
            control->mouseDown(down);
            childWithId<juce::TextButton>(*editor, "brush.dice")->onClick();
            check(!readout->isVisible(), "changing panel cancels the drag readout");
            control->mouseUp(down);
            childWithId<juce::TextButton>(*editor, "brush.drum")->onClick();
        }
        saveVisual(*editor, output, width == 1280 ? "effect-drag-released.png" : "effect-drag-released-small.png");
    }
}

void testCloseEditorDuringEffectDrag()
{
    for (const int index : { 8, 9 })
    {
        auto processor = std::make_unique<BeatScholarLinuxProcessor>();
        auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        editor->setVisible(true);
        auto* control = childWithId<juce::Slider>(*editor,
            ("effect." + juce::String(index)).toRawUTF8());
        check(control != nullptr, "close-during-drag control exists");
        if (control == nullptr) continue;
        const auto point = control->getBounds().getCentre().toFloat();
        control->mouseDown(visualEvent(*editor, point).getEventRelativeTo(control));
        check(childWithId<juce::Label>(*editor, "effect.drag-value")->isVisible(),
              "close-during-drag begins with a live readout");
        editor.reset();
        editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
        check(!childWithId<juce::Label>(*editor, "effect.drag-value")->isVisible(),
              "reopened editor has no stale drag readout");
    }
}

void testSeparateDriveAndDistControls(const juce::File& output)
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    auto& pad = processor->projectState().kit.pads[0];
    auto* drive = dynamic_cast<juce::Slider*>(editor->getComponentAt(699, 680));
    auto* dist = dynamic_cast<juce::Slider*>(editor->getComponentAt(885, 680));
    check(drive != nullptr && dist != nullptr, "documented Drive and Dist controls exist");
    if (drive == nullptr || dist == nullptr) return;
    check(drive->getMinimum() == 0.0 && drive->getMaximum() == 24.0,
          "Drive exposes the native 0..24 dB control range");
    const std::array<std::pair<double, double>, 5> nativeDrivePositions {{{0.0, 0.0},
        {6.0, 0.311383776}, {12.0, 0.5}, {18.0, 0.720252418}, {24.0, 1.0}}};
    for (const auto& [decibels, position] : nativeDrivePositions)
    {
        check(std::abs(drive->valueToProportionOfLength(decibels) - position) < 1e-8,
              "Drive marker follows the native gain-domain range");
        check(std::abs(drive->proportionOfLengthToValue(position) - decibels) < 1e-6,
              "Drive mouse travel converts back to the native dB value");
    }
    drive->setValue(6.0, juce::dontSendNotification);
    saveVisual(*editor, output, "drive-native-6db.png");
    drive->setValue(18.0, juce::dontSendNotification);
    saveVisual(*editor, output, "drive-native-18db.png");
    drive->setValue(0.0, juce::dontSendNotification);
    pad.nativeDriveInsert = false;
    drive->setValue(12.0, juce::sendNotificationSync);
    check(pad.nativeDriveInsert && pad.driveEnabled && pad.driveGainDb == 12.0f && pad.distortionMix == 0.0f,
          "Drive sets insert gain without changing Dist send");
    auto* gain = childWithId<juce::Slider>(*editor, "effect.9");
    check(gain != nullptr && gain->getSliderStyle() == juce::Slider::LinearVertical,
          "manual parallel distortion Gain fader is available");
    if (gain != nullptr)
    {
        gain->setValue(0.82, juce::sendNotificationSync);
        check(std::all_of(processor->projectState().kit.pads.begin(), processor->projectState().kit.pads.end(),
                  [](const auto& sound) { return std::abs(sound.distortionDrive - 0.82f) < 1e-6f; }),
              "parallel Gain affects all drum sends");
        check(pad.driveGainDb == 12.0f && pad.distortionMix == 0.0f && pad.gain == 1.0f,
              "parallel Gain preserves insert Drive, send level, and sample volume");
        check(processor->restoreProjectHistory(false), "shared parallel Gain can be undone");
        check(std::all_of(processor->projectState().kit.pads.begin(), processor->projectState().kit.pads.end(),
                  [](const auto& sound) { return sound.distortionDrive == 0.5f; }),
              "one undo restores every pad's parallel Gain");
    }
    dist->setValue(0.6, juce::sendNotificationSync);
    check(std::abs(pad.distortionMix - 0.6f) < 1e-6f && pad.driveGainDb == 12.0f && pad.delay == 0.0f,
          "Dist changes parallel distortion independently of Drive and delay");
    auto* reverb = childWithId<juce::Slider>(*editor, "effect.6");
    auto* room = childWithId<juce::Slider>(*editor, "effect.7");
    if (reverb != nullptr && room != nullptr)
    {
        reverb->setValue(0.26, juce::sendNotificationSync);
        room->setValue(0.45, juce::sendNotificationSync);
        dist->setValue(0.29, juce::sendNotificationSync);
        saveVisual(*editor, output, "sampler-sends-manual.png");
        reverb->setValue(0.0, juce::sendNotificationSync);
        room->setValue(0.5, juce::sendNotificationSync);
        dist->setValue(1.0, juce::sendNotificationSync);
        saveVisual(*editor, output, "sampler-sends-range.png");
    }
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    const juce::File output(argc > 1 ? argv[1] : "/tmp/beat-scholar-editor-tests");
    check(output.createDirectory().wasOk(), "create screenshot directory");
    testPublishedDisplayOptions(output);
    testHostMidiMapReportsCurrentPadNames();
    testDenseOfflineBlockKeepsEveryHit();
    testIncomingMidiBeyondFormerCapacity();
    testFactorySamplesRespectXdgDataHome(output);
    testSampleReplacementPreservesOtherPads(output);
    testAutonomousManualRegressions();
    testDefaultVelocityDragUndo();
    testHostRecallCancelsStalePatternInteractions();
    testBeatDragPublishesEachChangeAndOwnsOneUndo();
    testRandomizerPaintStroke(output);
    testRandomizerPanelRecall();
    testRandomizerInitUndoAndHostRecall(output);
    testLegacyAttackFallbackRemainsAudibleAfterEditing();
    testMasterGainReportsHostGestures();
    testMonoButtonEditsOnlySelectedPadAndSupportsUndo(output);
    testRoutingLabelTogglesAndTracksHost(output);
    testRandomizerVelocityEndpointsStayVisibleAndOrdered();
    testTutorialRandomizerNumericUnits(output);
    testPresetNamesSurviveEditorAndHostRecall(output);
    testLegacyPresetKitIdentity(output);
    testContentNavigationSkipsMalformedFiles(output);
    testMasterUndoPreservesHostAutomation();
    testMainAndKitInitContract();
    testKitHistoryRestoresBypassesWithoutResettingGlobalAutomation(output);
    testSampleUndoRestoresAudibleData(output);
    testSampleDropTargets(output);
    testDocumentedKeyboardRows();
    captureTutorialFactorySoundSelection(output);
    testTutorialPatternAddTile(output);
    testArrangementOverflowAndContextActions(output);
    testTutorialContinuousZoomAndProportionalScroll(output);
    testZoomButtonsAndScrollRails(output);
    testDenseLaneControlsMatchTheirVisibleTargets(output);
    testCommandDragPansBlankGrid(output);
    testInvalidEmbeddedAudioDoesNotReplaceStateOrSound(output);
    testDecodedSampleLimitsBeforeAllocation();
    testDocumentedMp3CustomSampleSupport();
    testDocumentedNumericEntry(output);
    testBarMeterNumericEntry(output);
    testLaneContextMenuDoesNotEdit();
    testSequencerMidiNotesHaveFiniteLifetimes();
    testStandaloneTransportIgnoresDeviceClock(output);
    testHostedTransportRemainsHostControlled(output);
    testMidiExportUsesTheActiveTempo(output);
    testStandaloneRestoresMultiOutputAsStereo();
    testQueuedPatternsSurviveStructuralEdits();
    testDirectPatternClickOverridesMidiSwitch();
    testOutgoingMidiReleasesOnTransportChanges();
    testPadResonanceAltReset();
    testMasterResonanceAltReset();
    testDocumentedPadBypassAutomation(output);
    testModuleSwitchesReachEditorAndProcessor(output);
    testSamplerDisableClearsVoicesAndTails();
    testSparseMultiOutputBusMapping();
    testSectors(output);
    testSeparateDriveAndDistControls(output);
    testTutorialEffectDragValue(output);
    testCloseEditorDuringEffectDrag();
    testRenderedLevelMetersAndLayout(output);
    testStateRefreshPreservesSoundControls(output);
    testSoundControlEditsPreserveOtherValues();
    testSequencedOneShotGateUsesBeatDurationAcrossRatesAndBlocks();
    testPadAuditionsPreserveRepeatedHits();
    testHostNoteOffKeepsMidiChannelsIndependent();
    testMixedMeterGeometry(output);
    testBarHeaderActions(output);
    testCustomSampleAndMasterVisualStates(output);
    testRandomizerTabRestoresSoundPanel();
    testRandomizerTriggerModuleToggleIsIndependent(output);
    testRandomizerFillEmptyIsClickToggle(output);
    testRandomizerPadSelectionIsUndoable();
    testPatternMidiExportPreservesNameAndMeterDuration(output);
    testMidiExportGatesFollowPlayback(output);
    testShiftExportPreservesPlayingAndQueuedPatterns();
    testDrumBrushDoesNotInheritSubdivision();
    testTogglingNoteRemembersVelocity();
    testFreshNoteUsesDefaultAndKeepsItAfterRecall();
    testShiftBeatFillOverridesBrushAndRemembersVelocities();
    testShiftLaneResetOverridesActiveBrush();
    testShiftLaneResetOverridesRandomizer();
    testBarMeterDragIsOneUndoStep(output);
    testArrowTransformRequiresClick();
    testKeyboardFocus(output);
    std::cout << "Editor regression failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
