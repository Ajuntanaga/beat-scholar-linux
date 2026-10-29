#include "plugin/PluginEditor.h"
#include "plugin/StateCodec.h"
#include "plugin/DataPaths.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool condition, const juce::String& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
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
    // identities so a rotated painter cannot agree with the wrong hit target.
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
                const auto point = centre + juce::Point<float>(std::sin(angle), -std::cos(angle))
                                              * (radius * distance);
                check(similar(snapshot.getPixelAt(juce::roundToInt(point.x), juce::roundToInt(point.y)), expected),
                      "radial fill/clockwise identity: " + juce::String(int(count)) + "/" + juce::String(int(i)));
            }
        }
        // A click in each visual sector must change that sector exactly once.
        for (std::size_t i = 0; i < count; ++i)
        {
            const float angle = juce::MathConstants<float>::twoPi * (float(i) + 0.5f) / float(count);
            const auto point = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * 0.8f);
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
    check(std::abs(pitch->getValue() - 5.25) < 1.0e-6,
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

void saveVisual(BeatScholarLinuxEditor& editor, const juce::File& output, const char* name)
{
    const auto snapshot = editor.createComponentSnapshot(editor.getLocalBounds());
    auto stream = output.getChildFile(name).createOutputStream();
    if (stream != nullptr) { stream->setPosition(0); stream->truncate(); }
    check(stream != nullptr && juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
          "write public-feature screenshot");
}

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

void testBarHeaderActions()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    processor->projectEditor().addBar(0, bsl::BarState::make(4, 4));
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    const auto duplicate = visualEvent(*editor, { 517, 80 });
    editor->mouseDown(duplicate);
    editor->mouseUp(duplicate);
    check(processor->projectState().patterns.front().bars.size() == 3,
          "visible bar-header duplicate icon duplicates a bar");
    const auto remove = visualEvent(*editor, { 402, 80 });
    editor->mouseDown(remove);
    editor->mouseUp(remove);
    check(processor->projectState().patterns.front().bars.size() == 2,
          "visible bar-header remove icon removes a bar");
    const auto add = visualEvent(*editor, { 549, 80 });
    editor->mouseDown(add);
    editor->mouseUp(add);
    check(processor->projectState().patterns.front().bars.size() == 3,
          "visible bar-header add icon adds a bar");
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
    for (int y = 585; y < 697; ++y)
        for (int x = 1214; x < 1236; ++x)
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
    };
    constexpr std::array cases {
        Case { 3, 8, "Sparse 3-8", false, false },
        Case { 7, 16, "Late 7-16", false, true },
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
            if (message.isNoteOff())
                latestNoteOff = std::max(latestNoteOff, message.getTimeStamp());
        }
        check(hasName, "exported MIDI carries the pattern name as track metadata");
        check(hasEnd && std::abs(endTick - expectedEnd) < 1.0e-9,
              "exported MIDI ends at the complete meter-defined pattern duration");
        check(latestNoteOff <= expectedEnd,
              "exported final note-off is clamped to the end-of-track tick");
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

void testBarMeterDragIsOneUndoStep()
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
}

void testKeyboardFocus()
{
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
    const auto sendKey = [](const char* action, const char* key) {
        juce::ChildProcess command;
        const bool started = command.start(juce::StringArray { "xdotool", action, key });
        check(started && command.waitForProcessToFinish(2000) && command.getExitCode() == 0,
              "send key only to isolated Xvfb display");
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
                    else juce::MessageManager::getInstance()->stopDispatchLoop();
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
        juce::Timer::callAfterDelay(50, [&, index] {
            check(juce::KeyPress::isKeyCurrentlyDown(keyCodes[static_cast<std::size_t>(index)]),
                  "isolated editor observes held arrow key");
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
            juce::Timer::callAfterDelay(50, [&, index] { testArrow(index + 1); });
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
          juce::Timer::callAfterDelay(150, [&] { testArrow(0); });
      else
      {
          std::cout << "Isolated keyboard injection skipped outside the Xvfb harness\n";
          juce::MessageManager::getInstance()->stopDispatchLoop();
      }
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
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
            check(renderBypassTest(processed, pad, id, true) == dry,
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
          && std::abs(velocity->getValue() - before * 100.0) < 1e-5,
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
        for (int y = 602; y < 690; ++y)
            for (int x = 1205; x < 1233; ++x)
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

void testSeparateDriveAndDistControls()
{
    auto processor = std::make_unique<BeatScholarLinuxProcessor>();
    auto editor = std::make_unique<BeatScholarLinuxEditor>(*processor);
    editor->setVisible(true);
    auto& pad = processor->projectState().kit.pads[0];
    auto* drive = dynamic_cast<juce::Slider*>(editor->getComponentAt(699, 680));
    auto* dist = dynamic_cast<juce::Slider*>(editor->getComponentAt(885, 680));
    check(drive != nullptr && dist != nullptr, "documented Drive and Dist controls exist");
    if (drive == nullptr || dist == nullptr) return;
    drive->setValue(12.0, juce::sendNotificationSync);
    check(pad.driveEnabled && pad.driveGainDb == 12.0f && pad.distortionMix == 0.0f,
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
    testAutonomousManualRegressions();
    testDefaultVelocityDragUndo();
    testHostRecallCancelsStalePatternInteractions();
    testBeatDragPublishesEachChangeAndOwnsOneUndo();
    testRandomizerPanelRecall();
    testLegacyAttackFallbackRemainsAudibleAfterEditing();
    testMasterGainReportsHostGestures();
    testRandomizerVelocityEndpointsStayVisibleAndOrdered();
    testPresetNamesSurviveEditorAndHostRecall(output);
    testMasterUndoPreservesHostAutomation();
    testKitHistoryRestoresBypassesWithoutResettingGlobalAutomation(output);
    testSampleUndoRestoresAudibleData(output);
    testDecodedSampleLimitsBeforeAllocation();
    testDocumentedMp3CustomSampleSupport();
    testDocumentedNumericEntry(output);
    testLaneContextMenuDoesNotEdit();
    testSequencerMidiNotesHaveFiniteLifetimes();
    testOutgoingMidiReleasesOnTransportChanges();
    testDocumentedPadBypassAutomation(output);
    testSparseMultiOutputBusMapping();
    testSectors(output);
    testSeparateDriveAndDistControls();
    testRenderedLevelMetersAndLayout(output);
    testStateRefreshPreservesSoundControls(output);
    testSequencedOneShotGateUsesBeatDurationAcrossRatesAndBlocks();
    testMixedMeterGeometry(output);
    testBarHeaderActions();
    testCustomSampleAndMasterVisualStates(output);
    testRandomizerTabRestoresSoundPanel();
    testRandomizerTriggerModuleToggleIsIndependent(output);
    testRandomizerPadSelectionIsUndoable();
    testPatternMidiExportPreservesNameAndMeterDuration(output);
    testDrumBrushDoesNotInheritSubdivision();
    testShiftLaneResetOverridesActiveBrush();
    testShiftLaneResetOverridesRandomizer();
    testBarMeterDragIsOneUndoStep();
    testKeyboardFocus();
    std::cout << "Editor regression failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
