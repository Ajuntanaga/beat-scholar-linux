#include "plugin/PluginEditor.h"
#include "plugin/DataPaths.h"

#include "core/Randomizer.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace {

constexpr std::array<const char*, bsl::padCount> padNames {
    "KICK", "SNARE", "RIM", "CLAP", "HAT C", "HAT O", "CRASH 1", "RIDE",
    "CRASH 2", "TOM HI", "TOM MID", "TOM LOW", "SHAKER", "TRIANGLE", "FX 1", "FX 2"
};

void addShortcutMenuItem(juce::PopupMenu& menu, int id, const juce::String& text,
                         const juce::String& shortcut, bool enabled = true)
{
    juce::PopupMenu::Item item(text);
    item.itemID = id;
    item.shortcutKeyDescription = shortcut;
    item.isEnabled = enabled;
    menu.addItem(std::move(item));
}

// Basic Beat Making & Editing: duplicate sits 70% along the bar, with
// fixed spacing for add/remove. Share the rectangle between paint and input.
juce::Rectangle<float> barHeaderActions(juce::Rectangle<float> bounds, float scale)
{
    if (bounds.getWidth() < 300.0f * scale)
        return {};
    constexpr float spacing = 33.0f;
    return { bounds.getX() + bounds.getWidth() * 0.70f - spacing * scale * 0.5f,
             bounds.getY(), 3.0f * spacing * scale, bounds.getHeight() };
}

void enableSlice(bsl::SliceState& slice, std::uint8_t pad, float defaultVelocity)
{
    if (!slice.velocityAssigned || slice.padIndex != pad)
        slice.velocity = defaultVelocity;
    slice.padIndex = pad;
    slice.velocityAssigned = true;
    slice.enabled = true;
}

bool isAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".flac" || extension == ".aif"
           || extension == ".aiff" || extension == ".mp3" || extension == ".ogg";
}

enum class MenuContent { preset, kit, randomizer };

struct MenuFile
{
    int id = 0;
    MenuContent content = MenuContent::preset;
    juce::File file;
};

using bsl::dataRoot;

bool hasContentExtension(const juce::File& file, MenuContent content)
{
    if (file.getFileNameWithoutExtension().equalsIgnoreCase("Version"))
        return false;
    if (file.hasFileExtension("json"))
        return true;
    if (content == MenuContent::preset)
        return file.hasFileExtension("bslpreset");
    if (content == MenuContent::kit)
        return file.hasFileExtension("bslkit");
    return file.hasFileExtension("bslrandom");
}

void addDirectoryMenu(juce::PopupMenu& parent, const juce::File& directory,
                      MenuContent content, std::vector<MenuFile>& entries, int& nextId)
{
    if (!directory.isDirectory())
        return;
    auto directories = directory.findChildFiles(juce::File::findDirectories, false);
    auto files = directory.findChildFiles(juce::File::findFiles, false);
    const auto byName = [](const juce::File& left, const juce::File& right) {
        return left.getFileName().compareIgnoreCase(right.getFileName()) < 0;
    };
    std::sort(directories.begin(), directories.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    for (const auto& child : directories)
    {
        juce::PopupMenu submenu;
        addDirectoryMenu(submenu, child, content, entries, nextId);
        if (submenu.getNumItems() > 0)
            parent.addSubMenu(child.getFileName(), submenu);
    }
    for (const auto& file : files)
    {
        if (!hasContentExtension(file, content))
            continue;
        const int id = nextId++;
        entries.push_back({ id, content, file });
        parent.addItem(id, file.getFileNameWithoutExtension());
    }
}

std::vector<juce::File> contentFiles(const juce::File& directory, MenuContent content)
{
    std::vector<juce::File> result;
    if (!directory.isDirectory())
        return result;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, true))
        if (hasContentExtension(file, content))
            result.push_back(file);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.getFullPathName().compareIgnoreCase(right.getFullPathName()) < 0;
    });
    return result;
}

template <typename Load>
bool loadAdjacentContent(const std::vector<juce::File>& files, const juce::File& current,
                         int delta, Load&& load)
{
    const int count = static_cast<int>(files.size());
    if (count == 0 || delta == 0)
        return false;
    const int step = delta < 0 ? -1 : 1;
    const auto found = std::find(files.begin(), files.end(), current);
    int index = found == files.end() ? (step < 0 ? count : -1)
                                    : static_cast<int>(std::distance(files.begin(), found));
    for (int attempt = 0; attempt < count; ++attempt)
    {
        index = (index + step + count) % count;
        const auto& file = files[static_cast<std::size_t>(index)];
        // Do not reload the current document if every other candidate is broken.
        if (file != current && load(file))
            return true;
    }
    return false;
}

juce::String factorySampleToken(std::size_t pad)
{
    static constexpr std::array<const char*, bsl::padCount> tokens {
        "_INST-BD_", "_INST-Snare_", "_INST-Rim_", "_INST-Clap_",
        "_INST-Hat_TYPE-Closed_", "_INST-Hat_TYPE-Open_",
        "_INST-Cymbal_TYPE-Crash_", "_INST-Cymbal_TYPE-Ride_",
        "_INST-Cymbal_TYPE-Crash_", "_INST-Tom_TYPE-Hi_",
        "_INST-Tom_TYPE-Mid_", "_INST-Tom_TYPE-Lo_", "_INST-Shaker_",
        "_INST-Triangle_", "_INST-FX", "_INST-FX"
    };
    return pad < tokens.size() ? juce::String(tokens[pad]) : juce::String();
}

juce::String factorySampleDisplayName(const juce::File& file)
{
    auto name = file.getFileNameWithoutExtension();
    const int marker = name.indexOf("_NAME-");
    return marker >= 0 ? name.substring(marker + 6) : name;
}

juce::String quantizationLabel(bsl::PatternQuantization value)
{
    switch (value)
    {
        case bsl::PatternQuantization::off: return "Off";
        case bsl::PatternQuantization::eighth: return "1/8";
        case bsl::PatternQuantization::quarter: return "1/4";
        case bsl::PatternQuantization::half: return "2/4";
        case bsl::PatternQuantization::whole: return "4/4";
        case bsl::PatternQuantization::sixteenth: return "1/16";
        case bsl::PatternQuantization::thirtySecond: return "1/32";
    }
    return "4/4";
}

juce::PopupMenu quantizationMenu(bsl::PatternQuantization current)
{
    juce::PopupMenu menu;
    for (const auto value : { bsl::PatternQuantization::whole, bsl::PatternQuantization::half,
                             bsl::PatternQuantization::quarter, bsl::PatternQuantization::eighth,
                             bsl::PatternQuantization::sixteenth, bsl::PatternQuantization::thirtySecond })
        menu.addItem(10 + static_cast<int>(value), quantizationLabel(value), true, value == current);
    return menu;
}

juce::Rectangle<int> designRect(int width, int height, float x, float y, float w, float h)
{
    constexpr float designWidth = 1280.0f;
    constexpr float designHeight = 720.0f;
    const float scale = std::min(static_cast<float>(width) / designWidth,
                                 static_cast<float>(height) / designHeight);
    const float offsetX = (static_cast<float>(width) - designWidth * scale) * 0.5f;
    const float offsetY = (static_cast<float>(height) - designHeight * scale) * 0.5f;
    return juce::Rectangle<float>(offsetX + x * scale, offsetY + y * scale,
                                  w * scale, h * scale).toNearestInt();
}

float designScale(int width, int height)
{
    return std::min(static_cast<float>(width) / 1280.0f,
                    static_cast<float>(height) / 720.0f);
}

juce::String midiNoteLabel(int note, const bsl::UiState& ui)
{
    return ui.midiNoteNames
        ? juce::MidiMessage::getMidiNoteName(note, !ui.midiFlats, true, ui.middleCOctave)
        : "MIDI " + juce::String(note);
}

struct BeatSpan { float start; float length; };

std::vector<BeatSpan> beatSpans(const bsl::PatternState& pattern)
{
    std::vector<BeatSpan> result;
    result.reserve(pattern.lanes.front().beats.size());
    float start = 0.0f;
    for (const auto& bar : pattern.bars)
    {
        const float length = 4.0f / static_cast<float>(bar.denominator);
        for (int beat = 0; beat < bar.numerator; ++beat)
        {
            result.push_back({ start, length });
            start += length;
        }
    }
    return result;
}

} // namespace

BeatScholarLinuxEditor::BeatScholarLinuxEditor(BeatScholarLinuxProcessor& owner)
    : AudioProcessorEditor(&owner), processor_(owner)
{
    lookAndFeel_.setDarkMode(processor_.projectState().ui.darkMode);
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(960, 540, 1920, 1080);
    if (auto* editorConstrainer = getConstrainer())
        editorConstrainer->setFixedAspectRatio(16.0 / 9.0);
    setSize(1280, 720);

    padColours_ = {
        juce::Colour(0xffbf316b), juce::Colour(0xff4058bd), juce::Colour(0xff2eb574), juce::Colour(0xffd51e42),
        juce::Colour(0xffb9983a), juce::Colour(0xffa95c35), juce::Colour(0xff55349d), juce::Colour(0xff4e8d2d),
        juce::Colour(0xff8a2b2d), juce::Colour(0xffc152ad), juce::Colour(0xffab247d), juce::Colour(0xff6d317d),
        juce::Colour(0xff17aaa5), juce::Colour(0xff4259bd), juce::Colour(0xff2435d9), juce::Colour(0xff3f8026)
    };

    title_.setText("BEAT SCHOLAR", juce::dontSendNotification);
    title_.setColour(juce::Label::textColourId, juce::Colour(lookAndFeel_.palette().text));
    addAndMakeVisible(title_);

    status_.setFont(bsl::ui::font(10.0f));
    status_.setColour(juce::Label::textColourId, juce::Colour(lookAndFeel_.palette().textMuted));
    status_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status_);

    swing_.setSliderStyle(juce::Slider::LinearHorizontal);
    swing_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    swing_.setTooltip("Swing. Right-click to select the 1/8, 1/4, or 1/2 reference.");
    swing_.addMouseListener(this, false);
    addAndMakeVisible(swing_);
    swingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.parameters(), "swing", swing_);

    bpm_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpm_.setComponentID("toolbar.bpm");
    bpm_.setTooltip("Tempo. In a plugin, this read-only value follows the host.");
    bpm_.setColour(juce::Slider::trackColourId, juce::Colour(lookAndFeel_.palette().accent));
    bpm_.addMouseListener(this, false);
    addAndMakeVisible(bpm_);
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        bpmAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.parameters(), "bpm", bpm_);
    else
    {
        // Host tempo is a display, independent of the standalone parameter range.
        bpm_.setRange(0.0, 1.0e9, 0.01);
        bpm_.setValue(processor_.effectiveBpm(), juce::dontSendNotification);
        bpm_.setEnabled(false);
    }

    defaultVelocity_.setSliderStyle(juce::Slider::LinearHorizontal);
    defaultVelocity_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    defaultVelocity_.setComponentID("toolbar.velocity");
    defaultVelocity_.setRange(0.0, 127.0, 1.0);
    defaultVelocity_.setTextValueSuffix("");
    defaultVelocity_.setTooltip("Default velocity used when a note slice is enabled.");
    defaultVelocity_.setDoubleClickReturnValue(false, 102.0);
    defaultVelocity_.addMouseListener(this, false);
    defaultVelocity_.setValue(processor_.projectState().global.defaultVelocity * 127.0,
                              juce::dontSendNotification);
    defaultVelocity_.onDragStart = [this] {
        refreshRestoredState(&defaultVelocity_);
        moduleControlGesture_ = true;
        moduleControlCheckpointed_ = false;
    };
    defaultVelocity_.onDragEnd = [this] {
        moduleControlGesture_ = false;
        moduleControlCheckpointed_ = false;
    };
    defaultVelocity_.onValueChange = [this] {
        if (updatingControls_)
            return;
        refreshRestoredState(&defaultVelocity_);
        checkpointModuleControl();
        processor_.projectState().global.defaultVelocity =
            static_cast<float>(defaultVelocity_.getValue() / 127.0);
    };
    addAndMakeVisible(defaultVelocity_);

    routing_.setComponentID("routing.mode");
    routing_.setButtonText("Stereo");
    routing_.setClickingTogglesState(true);
    routing_.onStateChange = [this] {
        routing_.setButtonText(routing_.getToggleState() ? "Multi" : "Stereo");
    };
    routing_.setTooltip("Stereo uses master processing; Multi routes pads to sixteen stereo outputs.");
    addAndMakeVisible(routing_);
    routingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.parameters(), "routing", routing_);

    configureButton(presetPrevious_, "", [this] { loadAdjacentPreset(-1); });
    presetPrevious_.setComponentID("icon.prev");
    presetPrevious_.setTitle("Previous preset");
    presetPrevious_.setTooltip("Load the previous preset");
    configureButton(preset_, "Init", [this] { showPresetMenu(); });
    preset_.setComponentID("toolbar.preset");
    configureButton(presetNext_, "", [this] { loadAdjacentPreset(1); });
    presetNext_.setComponentID("icon.next");
    presetNext_.setTitle("Next preset");
    presetNext_.setTooltip("Load the next preset");
    configureButton(presetSave_, "", [this] { savePresetAs(); });
    presetSave_.setComponentID("icon.save");
    presetSave_.setTitle("Save preset");
    presetSave_.setTooltip("Save the current preset");
    configureButton(sequencer_, "SEQ", [this] {
        auto& enabled = processor_.projectState().global.sequencerEnabled;
        enabled = !enabled;
        processor_.markProjectDirty();
    });
    configureButton(sampler_, "SAMPLE", [this] {
        auto& enabled = processor_.projectState().global.samplerEnabled;
        enabled = !enabled;
        processor_.markProjectDirty();
    });
    configureButton(play_, "", [this] {
        if (processor_.wrapperType != juce::AudioProcessor::wrapperType_Standalone) return;
        if (processor_.isStandalonePlaying()) processor_.pauseStandalone();
        else processor_.playStandalone();
    });
    play_.setComponentID("toolbar.play");
    play_.setTitle("Play");
    configureButton(pause_, "", [this] {
        if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
            processor_.pauseStandalone();
    });
    pause_.setComponentID("toolbar.pause");
    pause_.setTitle("Pause");
    configureButton(stop_, "", [this] {
        if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
            processor_.stopStandalone();
    });
    stop_.setComponentID("toolbar.stop");
    stop_.setTitle("Stop");
    const bool standaloneTransport = processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    play_.setEnabled(standaloneTransport);
    pause_.setEnabled(standaloneTransport);
    stop_.setEnabled(standaloneTransport);
    pause_.setTooltip(standaloneTransport ? "Pause standalone playback" : "Playback follows the host timeline");
    stop_.setTooltip(standaloneTransport ? "Stop standalone playback" : "Playback follows the host timeline");
    configureButton(drumBrush_, "", [this] {
        if (brush_ != Brush::drum)
            previousBrush_ = brush_;
        brush_ = Brush::drum;
        module_ = Module::soundCharacter;
        processor_.projectState().ui.randomizerVisible = false;
        configureModuleControls();
    });
    drumBrush_.setComponentID("brush.drum");
    drumBrush_.setTitle("Drum brush");
    drumBrush_.setTooltip("Place or replace drum slices");
    configureButton(slicerBrush_, "", [this] {
        previousBrush_ = brush_;
        brush_ = Brush::slicer;
        module_ = Module::soundCharacter;
        processor_.projectState().ui.randomizerVisible = false;
        configureModuleControls();
    });
    slicerBrush_.setComponentID("brush.slicer");
    slicerBrush_.setTitle("Slicer brush");
    slicerBrush_.setTooltip("Change beat subdivisions");
    configureButton(velocityBrush_, "", [this] {
        previousBrush_ = brush_;
        brush_ = Brush::velocity;
        module_ = Module::soundCharacter;
        processor_.projectState().ui.randomizerVisible = false;
        configureModuleControls();
    });
    velocityBrush_.setComponentID("brush.velocity");
    velocityBrush_.setTitle("Velocity brush");
    velocityBrush_.setTooltip("Adjust note velocity");
    configureButton(randomizerBrush_, "", [this] {
        previousBrush_ = brush_;
        brush_ = Brush::randomizer;
        processor_.projectState().ui.randomizerVisible = true;
        module_ = Module::randomizer;
        configureModuleControls();
    });
    randomizerBrush_.setComponentID("brush.dice");
    randomizerBrush_.setTitle("Randomizer brush");
    randomizerBrush_.setTooltip("Apply the configured randomizer");
    configureButton(undo_, "", [this] { restoreHistory(false); });
    undo_.setComponentID("toolbar.undo");
    undo_.setTitle("Undo");
    configureButton(redo_, "", [this] { restoreHistory(true); });
    redo_.setComponentID("toolbar.redo");
    redo_.setTitle("Redo");
    configureButton(about_, "MODALICS", [this] { showAbout(); });
    about_.setComponentID("toolbar.about");
    configureButton(barsPatterns_, "Patterns", [this] {
        auto& mode = processor_.projectState().ui.patternMode;
        mode = !mode;
        arrangementScroll_ = 0.0f;
        repaint();
        barsPatterns_.setButtonText(mode ? "Bars" : "Patterns");
        resized();
    });
    barsPatterns_.setComponentID("arrangement.toggle");
    configureButton(addItem_, "+", [this] {
        auto& state = processor_.projectState();
        if (state.ui.patternMode)
            processor_.projectEditor().addPattern();
        else
            processor_.projectEditor().addBar(state.activePattern, bsl::BarState::make(4, 4));
        updateControlsFromState();
    });
    addItem_.setComponentID("arrangement.add");
    configureButton(duplicateItem_, "DUP", [this] {
        auto& state = processor_.projectState();
        if (state.ui.patternMode)
            processor_.projectEditor().duplicatePattern(state.activePattern);
        else
            processor_.projectEditor().duplicateBar(state.activePattern,
                                                     std::min(state.ui.selectedBar,
                                                              state.patterns[state.activePattern].bars.size() - 1));
        updateControlsFromState();
    });
    configureButton(removeItem_, "-", [this] {
        auto& state = processor_.projectState();
        if (state.ui.patternMode)
            processor_.projectEditor().removePattern(state.activePattern);
        else
            processor_.projectEditor().removeBar(state.activePattern,
                                                  std::min(state.ui.selectedBar,
                                                           state.patterns[state.activePattern].bars.size() - 1));
        updateControlsFromState();
    });
    configureButton(soundTab_, "Kick", [this] {
        module_ = Module::soundCharacter;
        processor_.projectState().ui.randomizerVisible = false;
        configureModuleControls();
    });
    configureButton(randomizerTab_, "RANDOM", [this] {
        if (module_ == Module::randomizer)
            showRandomizerPresetMenu();
        else
        {
            module_ = Module::randomizer;
            processor_.projectState().ui.randomizerVisible = true;
            configureModuleControls();
        }
    });
    randomizerTab_.setComponentID("random.preset");
    configureButton(masterTab_, "SET", [this] {
        if (processor_.wrapperType != juce::AudioProcessor::wrapperType_Standalone) return;
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->showAudioSettingsDialog();
        else
            status_.setText("Standalone audio/MIDI settings are unavailable", juce::dontSendNotification);
    });
    masterTab_.setComponentID("master.settings");
    configureButton(auditionDuringPlayback_, "AUD", [this] {
        auto& enabled = processor_.projectState().global.auditionDuringPlayback;
        resetOrToggle(enabled, true, juce::ModifierKeys::getCurrentModifiers());
    });
    auditionDuringPlayback_.setTooltip("Allow drum-pad audition while transport is playing");
    auditionDuringPlayback_.setComponentID("kit.audition");
    configureButton(kitLocked_, "LOCK", [this] {
        auto& locked = processor_.projectState().global.kitLocked;
        resetOrToggle(locked, false, juce::ModifierKeys::getCurrentModifiers());
    });
    kitLocked_.setTooltip("Keep the current kit when a full preset is loaded");
    kitLocked_.setComponentID("kit.lock");
    configureButton(kitMenu_, "Init", [this] { showKitPresetMenu(); });
    kitMenu_.setComponentID("kit.menu");
    kitMenu_.setTooltip("Open factory and user kit presets, save a kit, or show its directory");
    configureButton(kitSave_, "", [this] { saveKitAs(); });
    kitSave_.setComponentID("icon.save");
    kitSave_.setTitle("Save kit");
    kitSave_.setTooltip("Save the current kit");
    configureButton(midiMap_, "MAP", [this] {
        showPadMenu(processor_.projectState().ui.selectedPad);
    });
    midiMap_.setTooltip("Edit the selected pad's MIDI output note");
    midiMap_.setComponentID("kit.map");
    configureButton(kitPrevious_, "", [this] { loadAdjacentKit(-1); });
    kitPrevious_.setComponentID("icon.prev");
    kitPrevious_.setTitle("Previous kit");
    configureButton(kitNext_, "", [this] { loadAdjacentKit(1); });
    kitNext_.setComponentID("icon.next");
    kitNext_.setTitle("Next kit");
    configureButton(randomizerPrevious_, "", [this] { loadAdjacentRandomizer(-1); });
    randomizerPrevious_.setComponentID("icon.prev");
    randomizerPrevious_.setTitle("Previous randomizer preset");
    configureButton(randomizerNext_, "", [this] { loadAdjacentRandomizer(1); });
    randomizerNext_.setComponentID("icon.next");
    randomizerNext_.setTitle("Next randomizer preset");

    for (std::size_t index = 0; index < pads_.size(); ++index)
    {
        auto& pad = pads_[index];
        configureButton(pad, padNames[index], [this, index] {
            if (module_ == Module::randomizer)
            {
                auto& selected = processor_.projectState().randomizer.selectedPads;
                processor_.projectEditor().checkpoint();
                const auto value = static_cast<std::uint8_t>(index);
                if (const auto found = std::find(selected.begin(), selected.end(), value);
                    found != selected.end())
                    selected.erase(found);
                else
                    selected.push_back(value);
                processor_.markProjectDirty();
                configureModuleControls();
            }
            else if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
                showPadMenu(index);
            else
                selectPad(index, true);
        });
        pad.setColour(juce::TextButton::buttonColourId, padColours_[index].darker(0.45f));
        pad.setColour(juce::TextButton::buttonOnColourId, padColours_[index]);
        pad.setComponentID("pad." + juce::String(static_cast<int>(index)));
        pad.setClickingTogglesState(false);
    }

    configureButton(loadSample_, "", [this] {
        chooseCustomSample(processor_.projectState().ui.selectedPad);
    });
    loadSample_.setComponentID("sample.folder");
    loadSample_.setTitle("Load custom sample");
    loadSample_.setTooltip("Load a custom sample for the selected pad");
    configureButton(clearSample_, "X", [this] {
        processor_.clearCustomSample(processor_.projectState().ui.selectedPad);
        configureModuleControls();
    });
    clearSample_.setComponentID("sample.clear");
    configureButton(layerOne_, "Layer 1", [this] {
        showFactorySampleMenu(processor_.projectState().ui.selectedPad, 0);
    });
    layerOne_.setComponentID("sample.layer");
    configureButton(layerTwo_, "Layer 2", [this] {
        showFactorySampleMenu(processor_.projectState().ui.selectedPad, 1);
    });
    layerTwo_.setComponentID("sample.layer");
    configureButton(padSolo_, "S", [this] {
        auto& pad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
        resetOrToggle(pad.solo, false, juce::ModifierKeys::getCurrentModifiers());
        configureModuleControls();
    });
    configureButton(padMute_, "M", [this] {
        auto& pad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
        resetOrToggle(pad.muted, false, juce::ModifierKeys::getCurrentModifiers());
        configureModuleControls();
    });
    configureButton(oneShot_, "One Shot", [this] {
        auto& pad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
        resetOrToggle(pad.oneShot, true, juce::ModifierKeys::getCurrentModifiers());
        configureModuleControls();
    });
    oneShot_.setComponentID("sample.oneshot");
    configureButton(mono_, "Mono", [this] {
        auto& pad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
        resetOrToggle(pad.mono, false, juce::ModifierKeys::getCurrentModifiers());
        configureModuleControls();
    });
    mono_.setComponentID("sample.mono");
    mono_.setTitle("Mono");
    mono_.setTooltip("Mono: replace the previous sample voice when this pad is triggered again.");

    configureButton(randomClearPads_, "Clear", [this] {
        processor_.projectEditor().checkpoint();
        processor_.projectState().randomizer.selectedPads.clear();
        processor_.markProjectDirty();
        configureModuleControls();
    });
    configureButton(randomAllPads_, "All", [this] {
        processor_.projectEditor().checkpoint();
        auto& values = processor_.projectState().randomizer.selectedPads;
        values.clear();
        for (std::size_t value = 0; value < bsl::padCount; ++value)
            values.push_back(static_cast<std::uint8_t>(value));
        processor_.markProjectDirty();
        configureModuleControls();
    });
    configureButton(randomClearSlices_, "Clear", [this] {
        processor_.projectEditor().checkpoint();
        processor_.projectState().randomizer.selectedSubdivisions.clear();
        processor_.markProjectDirty();
        configureModuleControls();
    });
    configureButton(randomAllSlices_, "All", [this] {
        processor_.projectEditor().checkpoint();
        auto& values = processor_.projectState().randomizer.selectedSubdivisions;
        for (int value = randomSlicePage_ * 9 + 1; value <= std::min(45, randomSlicePage_ * 9 + 9); ++value)
            if (std::find(values.begin(), values.end(), static_cast<std::size_t>(value)) == values.end())
                values.push_back(static_cast<std::size_t>(value));
        processor_.markProjectDirty();
        configureModuleControls();
    });
    configureButton(randomSlicePrevious_, "", [this] {
        randomSlicePage_ = (randomSlicePage_ + 4) % 5;
        configureModuleControls();
    });
    randomSlicePrevious_.setComponentID("icon.prev");
    randomSlicePrevious_.setTitle("Previous subdivision page");
    configureButton(randomSliceNext_, "", [this] {
        randomSlicePage_ = (randomSlicePage_ + 1) % 5;
        configureModuleControls();
    });
    randomSliceNext_.setComponentID("icon.next");
    randomSliceNext_.setTitle("Next subdivision page");
    for (std::size_t index = 0; index < randomSliceButtons_.size(); ++index)
    {
        configureButton(randomSliceButtons_[index], juce::String(static_cast<int>(index + 1)),
                        [this, index] {
            const auto value = static_cast<std::size_t>(randomSlicePage_ * 9 + static_cast<int>(index) + 1);
            auto& values = processor_.projectState().randomizer.selectedSubdivisions;
            processor_.projectEditor().checkpoint();
            if (const auto found = std::find(values.begin(), values.end(), value); found != values.end())
                values.erase(found);
            else
                values.push_back(value);
            processor_.markProjectDirty();
            configureModuleControls();
        });
    }

    const std::array<const char*, 4> moduleNames {
        "Drum Selection", "Beat Slicing", "Step Triggering", "Velocity Range"
    };
    for (std::size_t index = 0; index < randomModuleToggles_.size(); ++index)
    {
        configureButton(randomModuleToggles_[index], "", [this, index] {
            auto& random = processor_.projectState().randomizer;
            const std::array<bool*, 4> enabled { &random.noteSelectionEnabled,
                &random.slicingEnabled, &random.triggeringEnabled, &random.velocityEnabled };
            resetOrToggle(*enabled[index], true, juce::ModifierKeys::getCurrentModifiers());
            configureModuleControls();
        });
        randomModuleToggles_[index].setComponentID("random.module." + juce::String(static_cast<int>(index)));
        randomModuleToggles_[index].setTitle(moduleNames[index]);
        randomModuleToggles_[index].setTooltip(juce::String("Enable ") + moduleNames[index]);
    }

    configureButton(randomFillEmpty_, "", [this] {
        resetOrToggle(processor_.projectState().randomizer.fillEmptyOnly, false,
                      juce::ModifierKeys::getCurrentModifiers());
        configureModuleControls();
    });
    randomFillEmpty_.setComponentID("random.fillEmpty");
    randomFillEmpty_.setTitle("Fill Empty");
    randomFillEmpty_.setTooltip("Randomize only empty slices");

    configureSlider(soundControls_[0], 0.0, 2.0, 0.001, " s");
    configureSlider(soundControls_[1], 0.0, 1.0, 0.001, "");
    configureSlider(soundControls_[2], -12.0, 12.0, 1.0, " st");
    configureSlider(soundControls_[3], -100.0, 100.0, 0.1, " ct");
    configureSlider(soundControls_[4], 0.0, 1.0, 0.001, "");
    configureSlider(soundControls_[5], 0.0, 1.0, 0.001, "");
    configureSlider(soundControls_[6], 0.0, 2.0, 0.001, "");
    configureSlider(soundControls_[7], -1.0, 1.0, 0.001, "");
    configureSlider(soundControls_[8], 0.0, 1.0, 0.001, "");
    for (std::size_t index = 0; index < soundControls_.size(); ++index)
    {
        soundLabels_[index].setInterceptsMouseClicks(false, false);
        soundLabels_[index].setText("", juce::dontSendNotification);
        soundLabels_[index].setJustificationType(juce::Justification::centred);
        soundLabels_[index].setFont(bsl::ui::font(14.0f, true));
        soundLabels_[index].setColour(juce::Label::textColourId, juce::Colour(0xffbfc3c8));
        addAndMakeVisible(soundLabels_[index]);
        soundControls_[index].addMouseListener(this, false);
        soundControls_[index].onDragStart = [this, index] {
            refreshRestoredState(&soundControls_[index]);
            moduleControlGesture_ = true;
            moduleControlCheckpointed_ = false;
        };
        soundControls_[index].onDragEnd = [this] {
            moduleControlGesture_ = false;
            moduleControlCheckpointed_ = false;
        };
        soundControls_[index].onValueChange = [this, index] {
            if (updatingControls_) return;
            refreshRestoredState(&soundControls_[index]);
            checkpointModuleControl();
            if (module_ == Module::randomizer)
            {
                if (index == 2 && soundControls_[2].getValue() > soundControls_[3].getValue())
                    soundControls_[3].setValue(soundControls_[2].getValue(), juce::dontSendNotification);
                if (index == 3 && soundControls_[3].getValue() < soundControls_[2].getValue())
                    soundControls_[2].setValue(soundControls_[3].getValue(), juce::dontSendNotification);
            }
            applyModuleControls(soundControls_[index]);
        };
    }

    effectDragValue_.setComponentID("effect.drag-value");
    effectDragValue_.setJustificationType(juce::Justification::centred);
    effectDragValue_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2c295d));
    effectDragValue_.setColour(juce::Label::textColourId, juce::Colour(0xffe7e7f7));
    effectDragValue_.setInterceptsMouseClicks(false, false);
    addChildComponent(effectDragValue_);
    for (std::size_t index = 0; index < effectControls_.size(); ++index)
    {
        if (index == 3)
        {
            configureSlider(effectControls_[index], 0.0, 24.0, 0.01, " dB");
            // Native Drive uses a skewed linear-gain range, displayed in dB.
            juce::NormalisableRange<double> gainRange(1.0, juce::Decibels::decibelsToGain(24.0));
            gainRange.setSkewForCentre(juce::Decibels::decibelsToGain(12.0));
            juce::NormalisableRange<double> driveRange(0.0, 24.0,
                [gainRange](double, double, double position) {
                    return juce::Decibels::gainToDecibels(gainRange.convertFrom0to1(position));
                },
                [gainRange](double, double, double decibels) {
                    return gainRange.convertTo0to1(juce::Decibels::decibelsToGain(decibels));
                });
            driveRange.interval = 0.01;
            effectControls_[index].setNormalisableRange(driveRange);
        }
        else
            // Q = 0.1 + 2.4 * value: its native 0.8 default is 7/24,
            // so keep resonance continuous instead of rounding it to 0.292.
            configureSlider(effectControls_[index], 0.0, 1.0, index == 2 ? 0.0 : 0.001, "");
        effectLabels_[index].setJustificationType(juce::Justification::centred);
        effectLabels_[index].setFont(bsl::ui::font(14.0f, true));
        effectLabels_[index].setColour(juce::Label::textColourId,
                                      juce::Colour(lookAndFeel_.palette().text));
        addAndMakeVisible(effectLabels_[index]);
        effectControls_[index].addMouseListener(this, false);
        effectControls_[index].onDragStart = [this, index] {
            refreshRestoredState(&effectControls_[index]);
            moduleControlGesture_ = true;
            moduleControlCheckpointed_ = false;
            if ((index == 8 || index == 9) && effectControls_[index].mouseGesture)
            {
                effectDragIndex_ = static_cast<int>(index);
                updateEffectDragValue();
                effectDragValue_.toFront(false);
            }
        };
        effectControls_[index].onDragEnd = [this] {
            effectDragIndex_ = -1;
            effectDragValue_.setVisible(false);
            moduleControlGesture_ = false;
            moduleControlCheckpointed_ = false;
        };
        effectControls_[index].onValueChange = [this, index] {
            if (updatingControls_) return;
            refreshRestoredState(&effectControls_[index]);
            checkpointModuleControl();
            auto& state = processor_.projectState();
            auto& pad = state.kit.pads[state.ui.selectedPad];
            if (index == 3)
            {
                pad.driveEnabled = effectControls_[3].getValue() != 0.0;
                pad.nativeDriveInsert = true;
            }
            if (index == 3 || index == 8)
                pad.independentDistortion = true;
            // The Gain fader controls the shared parallel distortion chain,
            // independently of each pad's Dist send and insert Drive.
            if (index == 9)
                for (auto& sound : state.kit.pads)
                    sound.distortionDrive = static_cast<float>(effectControls_[9].getValue());
            applyModuleControls(effectControls_[index]);
            updateEffectDragValue();
        };
    }
    for (std::size_t index = 0; index < masterControls_.size(); ++index)
    {
        configureSlider(masterControls_[index], 0.0, index == 4 ? 1.5 : 1.0,
                        index == 1 ? 0.0 : 0.001, "");
        masterLabels_[index].setJustificationType(juce::Justification::centred);
        masterLabels_[index].setFont(bsl::ui::font(14.0f, true));
        masterLabels_[index].setColour(juce::Label::textColourId,
                                      juce::Colour(lookAndFeel_.palette().text));
        addAndMakeVisible(masterLabels_[index]);
        masterControls_[index].addMouseListener(this, false);
        masterControls_[index].onDragStart = [this, index] {
            refreshRestoredState(&masterControls_[index]);
            moduleControlGesture_ = true;
            moduleControlCheckpointed_ = false;
            if (index == 4 && !masterGainGesture_)
            {
                masterGainGesture_ = true;
                processor_.parameters().getParameter("masterGain")->beginChangeGesture();
            }
        };
        masterControls_[index].onDragEnd = [this, index] {
            if (index == 4 && masterGainGesture_)
            {
                masterGainGesture_ = false;
                processor_.parameters().getParameter("masterGain")->endChangeGesture();
            }
            moduleControlGesture_ = false;
            moduleControlCheckpointed_ = false;
        };
        masterControls_[index].onValueChange = [this, index] {
            if (updatingControls_) return;
            refreshRestoredState(&masterControls_[index]);
            checkpointModuleControl(index == 4);
            applyModuleControls(masterControls_[index]);
        };
    }

    numericEntry_.setComponentID("control.value");
    numericEntry_.setTitle("Numeric value");
    numericEntry_.setTooltip("Enter a value, then press Return. Escape cancels.");
    numericEntry_.setInputRestrictions(32, "0123456789.+-eE");
    numericEntry_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(lookAndFeel_.palette().panelRaised));
    numericEntry_.setColour(juce::TextEditor::textColourId, juce::Colour(lookAndFeel_.palette().text));
    numericEntry_.onReturnKey = [this] {
        const auto text = numericEntry_.getText().trim();
        const auto utf8 = text.toRawUTF8();
        char* end = nullptr;
        const double value = std::strtod(utf8, &end);
        if (end == utf8 || *end != '\0' || !std::isfinite(value))
        {
            numericEntry_.selectAll();
            return;
        }
        if (numericEntryBar_)
        {
            const auto target = *numericEntryBar_;
            auto& state = processor_.projectState();
            if (target.stateRevision != processor_.restoredStateRevision()
                || target.pattern != state.activePattern || target.pattern >= state.patterns.size()
                || target.bar >= state.patterns[target.pattern].bars.size())
            {
                cancelNumericEntry();
                return;
            }
            const auto previous = state.patterns[target.pattern].bars[target.bar];
            auto meter = previous;
            if (value < 1.0 || value > 64.0 || value != std::floor(value))
            {
                numericEntry_.selectAll();
                return;
            }
            (target.denominator ? meter.denominator : meter.numerator) = static_cast<int>(value);
            if (!meter.isValid())
            {
                numericEntry_.selectAll();
                return;
            }
            if (meter.numerator != previous.numerator || meter.denominator != previous.denominator)
                processor_.projectEditor().setBarMeter(target.pattern, target.bar,
                                                       meter.numerator, meter.denominator);
            cancelNumericEntry();
            updateControlsFromState();
            return;
        }
        if (numericEntryControl_ != nullptr)
        {
            const auto sliderValue = numericEntryControl_->valueFromTextFunction
                                         ? numericEntryControl_->valueFromTextFunction(text) : value;
            numericEntryControl_->setValue(sliderValue, juce::sendNotificationSync);
        }
        cancelNumericEntry();
    };
    const auto cancelEntry = [this] { cancelNumericEntry(); };
    numericEntry_.onEscapeKey = cancelEntry;
    numericEntry_.onFocusLost = cancelEntry;
    addChildComponent(numericEntry_);

    if (processor_.projectState().ui.randomizerVisible)
    {
        module_ = Module::randomizer;
        brush_ = Brush::randomizer;
    }
    configureModuleControls();
    observedStateRevision_ = processor_.restoredStateRevision();
    startTimerHz(20);
}

BeatScholarLinuxEditor::~BeatScholarLinuxEditor()
{
    if (masterGainGesture_)
    {
        masterGainGesture_ = false;
        processor_.parameters().getParameter("masterGain")->endChangeGesture();
    }
    masterControls_[4].onDragStart = nullptr;
    masterControls_[4].onDragEnd = nullptr;
    // Sliders can end a live drag while their members are being destroyed.
    // Detach callbacks before the drag readout and editor state are destroyed.
    for (auto& control : effectControls_)
    {
        control.onDragStart = nullptr;
        control.onDragEnd = nullptr;
    }
    setLookAndFeel(nullptr);
}

void BeatScholarLinuxEditor::configureButton(juce::TextButton& button, const juce::String& text,
                                              std::function<void()> action)
{
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId,
                     juce::Colour(lookAndFeel_.palette().panelRaised));
    button.setColour(juce::TextButton::buttonOnColourId,
                     juce::Colour(lookAndFeel_.palette().accent));
    button.setColour(juce::TextButton::textColourOffId,
                     juce::Colour(lookAndFeel_.palette().text));
    button.setColour(juce::TextButton::textColourOnId,
                     juce::Colour(lookAndFeel_.palette().text));
    button.onClick = std::move(action);
    addAndMakeVisible(button);
}

void BeatScholarLinuxEditor::configureSlider(juce::Slider& slider, double minimum, double maximum,
                                              double interval, const juce::String& suffix)
{
    slider.setRange(minimum, maximum, interval);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 18);
    slider.setTextValueSuffix(suffix);
    slider.setDoubleClickReturnValue(false, minimum <= 0.0 && maximum >= 0.0 ? 0.0 : minimum);
    slider.setColour(juce::Slider::rotarySliderFillColourId,
                     juce::Colour(lookAndFeel_.palette().accentBright));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                     juce::Colour(lookAndFeel_.palette().border));
    addAndMakeVisible(slider);
}

juce::Rectangle<int> BeatScholarLinuxEditor::patternBounds() const
{
    return designRect(getWidth(), getHeight(), 62.0f, 104.0f, 1070.0f, 404.0f);
}

juce::Rectangle<float> BeatScholarLinuxEditor::laneCellBounds(std::size_t lane) const
{
    const auto pattern = patternBounds();
    const auto rail = designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 448.0f);
    const auto& state = processor_.projectState();
    const float height = static_cast<float>(pattern.getHeight())
                         / static_cast<float>(state.patterns[state.activePattern].lanes.size())
                         * verticalZoom_;
    return { static_cast<float>(rail.getX()), static_cast<float>(pattern.getY())
                + (static_cast<float>(lane) - verticalScroll_) * height,
             static_cast<float>(rail.getWidth()), height - designScale(getWidth(), getHeight()) };
}

juce::Rectangle<float> BeatScholarLinuxEditor::laneControlBounds(std::size_t lane, int control) const
{
    const auto scale = designScale(getWidth(), getHeight());
    const auto controls = laneCellBounds(lane).reduced(4.0f * scale, 5.0f * scale);
    const auto compact = controls.withSizeKeepingCentre(
        std::min(controls.getWidth(), 26.0f * scale), controls.getHeight());
    if (controls.getHeight() >= 76.0f * scale)
        return { compact.getX(), compact.getCentreY() - 38.0f * scale
                                    + static_cast<float>(control) * 20.0f * scale,
                 compact.getWidth(), 16.0f * scale };
    const float height = controls.getHeight() / 4.0f;
    return compact.withY(controls.getY() + static_cast<float>(control) * height)
                  .withHeight(height).reduced(0.0f, scale);
}

juce::Rectangle<int> BeatScholarLinuxEditor::kitBounds() const
{
    return designRect(getWidth(), getHeight(), 8.0f, 558.0f, 388.0f, 154.0f);
}

juce::Rectangle<int> BeatScholarLinuxEditor::barHeaderBounds() const
{
    return designRect(getWidth(), getHeight(), 62.0f, 60.0f, 1070.0f, 42.0f);
}

juce::Rectangle<int> BeatScholarLinuxEditor::arrangementBounds() const
{
    return designRect(getWidth(), getHeight(), 1138.0f, 60.0f, 134.0f, 448.0f);
}

bool BeatScholarLinuxEditor::shortPatternList() const
{
    const auto& state = processor_.projectState();
    return state.ui.patternMode && state.patterns.size() <= 3;
}

juce::Rectangle<int> BeatScholarLinuxEditor::arrangementPadBounds() const
{
    return shortPatternList()
        ? designRect(getWidth(), getHeight(), 1161.0f, 127.0f, 99.0f, 323.0f)
        : designRect(getWidth(), getHeight(), 1161.0f, 112.0f, 99.0f, 338.0f);
}

int BeatScholarLinuxEditor::arrangementItemHeight() const
{
    if (shortPatternList())
        return juce::roundToInt(82.0f * designScale(getWidth(), getHeight()));
    const auto& state = processor_.projectState();
    const auto count = state.ui.patternMode ? state.patterns.size()
                                            : state.patterns[state.activePattern].bars.size();
    const auto scale = designScale(getWidth(), getHeight());
    return std::max(1, std::max(static_cast<int>(54.0f * scale),
        std::min(static_cast<int>(72.0f * scale),
                 arrangementPadBounds().getHeight() / static_cast<int>(std::max(std::size_t{1}, count)))));
}

juce::Rectangle<int> BeatScholarLinuxEditor::arrangementItemBounds(std::size_t index) const
{
    const auto area = arrangementPadBounds();
    const int height = arrangementItemHeight();
    const int gap = shortPatternList()
        ? juce::roundToInt(8.0f * designScale(getWidth(), getHeight())) : 4;
    return { area.getX(), area.getY() + juce::roundToInt((static_cast<float>(index)
                - arrangementScroll_) * height), area.getWidth(), height - gap };
}

float BeatScholarLinuxEditor::maximumArrangementScroll() const
{
    const auto& state = processor_.projectState();
    const auto count = state.ui.patternMode ? state.patterns.size()
                                            : state.patterns[state.activePattern].bars.size();
    return std::max(0.0f, static_cast<float>(count)
        - static_cast<float>(arrangementPadBounds().getHeight()) / arrangementItemHeight());
}

int BeatScholarLinuxEditor::arrangementActionAt(juce::Point<int> point, std::size_t index) const
{
    const auto bounds = arrangementItemBounds(index).toFloat();
    const float size = 18.0f * designScale(getWidth(), getHeight());
    if (bounds.withSize(size, size).contains(point.toFloat())) return 1;
    if (bounds.withSize(size, size).withX(bounds.getRight() - size).contains(point.toFloat())) return 2;
    if (bounds.withSize(size, size).withY(bounds.getBottom() - size).contains(point.toFloat())) return 0;
    return -1;
}

void BeatScholarLinuxEditor::clampView()
{
    const auto& pattern = processor_.projectState().patterns[processor_.projectState().activePattern];
    const float quarters = static_cast<float>(pattern.durationInQuarterNotes());
    const float lanes = static_cast<float>(pattern.lanes.size());
    horizontalScroll_ = std::clamp(horizontalScroll_, 0.0f, quarters - quarters / horizontalZoom_);
    verticalScroll_ = std::clamp(verticalScroll_, 0.0f, lanes - lanes / verticalZoom_);
    arrangementScroll_ = std::clamp(arrangementScroll_, 0.0f, maximumArrangementScroll());
}

juce::Rectangle<float> BeatScholarLinuxEditor::scrollRailBounds(int axis) const
{
    return (axis == 1 ? designRect(getWidth(), getHeight(), 12.0f, 508.0f, 958.0f, 8.0f)
                      : designRect(getWidth(), getHeight(), 1139.0f, 66.0f, 5.0f, 267.0f)).toFloat();
}

juce::Rectangle<float> BeatScholarLinuxEditor::scrollThumbBounds(int axis) const
{
    const auto rail = scrollRailBounds(axis);
    const auto& state = processor_.projectState();
    const auto& pattern = state.patterns[state.activePattern];
    if (axis == 1)
        return { rail.getX() + horizontalScroll_ / static_cast<float>(pattern.durationInQuarterNotes())
                                  * rail.getWidth(),
                 rail.getY(), rail.getWidth() / horizontalZoom_, rail.getHeight() };
    return { rail.getX(), rail.getY() + verticalScroll_ / static_cast<float>(pattern.lanes.size())
                                          * rail.getHeight(),
             rail.getWidth(), rail.getHeight() / verticalZoom_ };
}

juce::Rectangle<float> BeatScholarLinuxEditor::zoomRailBounds(int axis) const
{
    return (axis == 1 ? designRect(getWidth(), getHeight(), 1014.0f, 510.0f, 110.0f, 4.0f)
                      : designRect(getWidth(), getHeight(), 1140.0f, 369.0f, 3.0f, 96.0f)).toFloat();
}

juce::Rectangle<int> BeatScholarLinuxEditor::zoomButtonBounds(int axis, bool increase) const
{
    return axis == 1 ? designRect(getWidth(), getHeight(), increase ? 1132.0f : 987.0f,
                                  503.0f, 18.0f, 18.0f)
                     : designRect(getWidth(), getHeight(), 1132.0f,
                                  increase ? 344.0f : 478.0f, 18.0f, 18.0f);
}

void BeatScholarLinuxEditor::moveScrollRail(juce::Point<int> point)
{
    const auto& pattern = processor_.projectState().patterns[processor_.projectState().activePattern];
    const auto rail = scrollRailBounds(draggingScrollRail_);
    if (draggingScrollRail_ == 1)
        horizontalScroll_ = (point.x - rail.getX() - scrollRailGrabOffset_) / rail.getWidth()
            * static_cast<float>(pattern.durationInQuarterNotes());
    else if (draggingScrollRail_ == 2)
        verticalScroll_ = (point.y - rail.getY() - scrollRailGrabOffset_) / rail.getHeight()
            * static_cast<float>(pattern.lanes.size());
    clampView();
    repaint();
}

void BeatScholarLinuxEditor::moveZoomRail(juce::Point<int> point)
{
    const auto rail = zoomRailBounds(draggingZoomRail_);
    const float fraction = draggingZoomRail_ == 1
        ? (point.x - rail.getX()) / rail.getWidth()
        : (rail.getBottom() - point.y) / rail.getHeight();
    auto& zoom = draggingZoomRail_ == 1 ? horizontalZoom_ : verticalZoom_;
    zoom = 1.0f + 7.0f * std::clamp(fraction, 0.0f, 1.0f);
    clampView();
    repaint();
}

juce::Rectangle<int> BeatScholarLinuxEditor::lowerBounds() const
{
    return designRect(getWidth(), getHeight(), 8.0f, 522.0f, 1264.0f, 192.0f);
}

juce::Rectangle<int> BeatScholarLinuxEditor::soundBounds() const
{
    if (module_ == Module::randomizer)
        return designRect(getWidth(), getHeight(), 408.0f, 524.0f, 652.0f, 190.0f);
    return designRect(getWidth(), getHeight(), 400.0f, 522.0f, 648.0f, 190.0f);
}

juce::Rectangle<int> BeatScholarLinuxEditor::masterBounds() const
{
    return designRect(getWidth(), getHeight(), 1066.0f, 524.0f, 206.0f, 190.0f);
}

void BeatScholarLinuxEditor::paint(juce::Graphics& graphics)
{
    clampView();
    const float scale = designScale(getWidth(), getHeight());
    const auto drawActionGlyph = [this, &graphics, scale](juce::Rectangle<float> area, int glyph) {
        const float stroke = std::max(1.0f, scale);
        graphics.setColour(juce::Colour(lookAndFeel_.palette().textMuted));
        area = area.reduced(4.0f * scale);
        if (glyph == 0)
        {
            const auto back = area.withSizeKeepingCentre(area.getWidth() * 0.48f,
                                                         area.getHeight() * 0.48f)
                                  .translated(-2.0f * scale, 2.0f * scale);
            graphics.drawRect(back, stroke);
            graphics.drawRect(back.translated(3.0f * scale, -3.0f * scale), stroke);
        }
        else if (glyph == 1)
        {
            graphics.drawLine(area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom(), stroke);
            graphics.drawLine(area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), stroke);
        }
        else
        {
            graphics.drawLine(juce::Line<float>(area.getTopLeft(), area.getBottomRight()), stroke);
            graphics.drawLine(juce::Line<float>(area.getTopRight(), area.getBottomLeft()), stroke);
        }
    };
    graphics.fillAll(juce::Colour(lookAndFeel_.palette().background));
    const auto canvas = designRect(getWidth(), getHeight(), 4.0f, 4.0f, 1272.0f, 712.0f);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().borderBright));
    graphics.drawRect(canvas.toFloat(), std::max(1.0f, scale));

    const auto toolbar = designRect(getWidth(), getHeight(), 8.0f, 8.0f, 1264.0f, 46.0f);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(toolbar);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(toolbar.toFloat(), std::max(1.0f, scale));

    // The manual's wordmark is text crossed by a thin geometric star.
    const auto mark = designRect(getWidth(), getHeight(), 83.0f, 14.0f, 38.0f, 34.0f).toFloat();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text).withAlpha(0.62f));
    for (int index = 0; index < 8; ++index)
    {
        const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(index) / 8.0f;
        graphics.drawLine(mark.getCentreX(), mark.getCentreY(),
                          mark.getCentreX() + std::cos(angle) * mark.getWidth() * 0.55f,
                          mark.getCentreY() + std::sin(angle) * mark.getHeight() * 0.55f,
                          std::max(0.7f, scale * 0.75f));
    }
    for (const float x : { 354.0f, 670.0f, 1048.0f })
    {
        const auto divider = designRect(getWidth(), getHeight(), x, 14.0f, 1.5f, 34.0f);
        graphics.fillRect(divider);
    }
    graphics.setFont(bsl::ui::font(10.0f * scale, true));
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
    graphics.drawText("Swing", designRect(getWidth(), getHeight(), 365.0f, 12.0f, 70.0f, 15.0f),
                      juce::Justification::centred);
    graphics.drawText(juce::String(static_cast<int>(requestedSubdivision_)),
                      designRect(getWidth(), getHeight(), 775.0f, 15.0f, 17.0f, 30.0f),
                      juce::Justification::centred);
    {
        const auto arrowArea = designRect(getWidth(), getHeight(), 791.0f, 20.0f, 13.0f, 20.0f).toFloat();
        juce::Path arrow;
        arrow.startNewSubPath(arrowArea.getX(), arrowArea.getY() + arrowArea.getHeight() * 0.34f);
        arrow.lineTo(arrowArea.getRight(), arrowArea.getY() + arrowArea.getHeight() * 0.34f);
        arrow.lineTo(arrowArea.getCentreX(), arrowArea.getBottom() - arrowArea.getHeight() * 0.20f);
        arrow.closeSubPath();
        graphics.setColour(juce::Colour(lookAndFeel_.palette().textMuted));
        graphics.fillPath(arrow);
    }

    const auto pattern = patternBounds();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(pattern);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(pattern.toFloat(), std::max(1.0f, scale));
    paintPattern(graphics, pattern.toFloat());

    const auto& state = processor_.projectState();
    const auto& current = state.patterns[state.activePattern];
    const auto header = barHeaderBounds().toFloat();
    const float quarterWidth = header.getWidth()
                               / static_cast<float>(current.durationInQuarterNotes()) * horizontalZoom_;
    float firstQuarter = 0.0f;
    graphics.saveState();
    graphics.reduceClipRegion(header.toNearestInt());
    graphics.setFont(bsl::ui::font(20.0f * scale, true));
    for (std::size_t bar = 0; bar < current.bars.size(); ++bar)
    {
        const auto& meter = current.bars[bar];
        const float duration = static_cast<float>(meter.numerator) * 4.0f / meter.denominator;
        const float x = header.getX() + (firstQuarter - horizontalScroll_) * quarterWidth;
        const float width = duration * quarterWidth;
        juce::Rectangle<float> bounds(x, header.getY(), width - scale, header.getHeight());
        graphics.setColour(bar == state.ui.selectedBar
                               ? juce::Colour(lookAndFeel_.palette().panelRaised)
                               : juce::Colour(lookAndFeel_.palette().panel));
        graphics.fillRect(bounds);
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.drawRect(bounds, std::max(1.0f, scale));
        graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
        const auto meterArea = bounds.withSizeKeepingCentre(std::min(bounds.getWidth() * 0.45f, 120.0f * scale),
                                                            bounds.getHeight());
        if (bounds.getWidth() < 110.0f * scale)
        {
            graphics.drawText(juce::String(meter.numerator), meterArea.withHeight(meterArea.getHeight() * 0.5f),
                              juce::Justification::centred);
            graphics.drawText(juce::String(meter.denominator), meterArea.withTrimmedTop(meterArea.getHeight() * 0.5f),
                              juce::Justification::centred);
        }
        else
        {
            graphics.drawText(juce::String(meter.numerator), meterArea.withTrimmedRight(meterArea.getWidth() * 0.54f),
                              juce::Justification::centredRight);
            graphics.drawLine(meterArea.getCentreX(), meterArea.getY() + meterArea.getHeight() * 0.23f,
                              meterArea.getCentreX(), meterArea.getBottom() - meterArea.getHeight() * 0.23f,
                              std::max(1.0f, scale));
            graphics.drawText(juce::String(meter.denominator),
                              meterArea.withTrimmedLeft(meterArea.getWidth() * 0.54f),
                              juce::Justification::centredLeft);
        }
        if (auto actions = barHeaderActions(bounds, scale); !actions.isEmpty())
        {
            const float actionWidth = actions.getWidth() / 3.0f;
            for (int action = 0; action < 3; ++action)
                drawActionGlyph(actions.removeFromLeft(actionWidth)
                                    .withSizeKeepingCentre(18.0f * scale, 18.0f * scale), action);
        }
        firstQuarter += duration;
    }
    graphics.restoreState();

    // Visible lane column, including the manual's lane-wide brush arrows.
    const auto laneModifiers = juce::ModifierKeys::getCurrentModifiers();
    const bool wholeLaneAction = laneModifiers.isShiftDown()
        || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::leftKey)
        || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::rightKey)
        || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::downKey)
        || (!laneModifiers.isAltDown()
            && (brush_ == Brush::slicer || brush_ == Brush::randomizer
                || juce::KeyPress::isKeyCurrentlyDown('/')));
    const auto laneRail = designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 448.0f);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(laneRail);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(laneRail.toFloat(), std::max(1.0f, scale));
    const auto addLane = designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 42.0f);
    graphics.setFont(bsl::ui::font(22.0f * scale, true));
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
    graphics.drawText("+", addLane, juce::Justification::centred);
    graphics.setFont(bsl::ui::font(9.0f * scale, true));
    graphics.saveState();
    graphics.reduceClipRegion(laneRail.withY(pattern.getY()).withHeight(pattern.getHeight()));
    for (std::size_t lane = 0; lane < current.lanes.size(); ++lane)
    {
        const auto cell = laneCellBounds(lane);
        if (cell.getBottom() < pattern.getY() || cell.getY() > pattern.getBottom())
            continue;
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.drawRect(cell, std::max(1.0f, scale));
        graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
        if (wholeLaneAction)
        {
            juce::Path arrow;
            const auto centre = cell.getCentre();
            arrow.startNewSubPath(centre.x - 7.0f * scale, centre.y - 14.0f * scale);
            arrow.lineTo(centre.x + 8.0f * scale, centre.y);
            arrow.lineTo(centre.x - 7.0f * scale, centre.y + 14.0f * scale);
            arrow.closeSubPath();
            graphics.setColour(juce::Colour(lookAndFeel_.palette().accentBright));
            graphics.fillPath(arrow);
        }
        else
        {
            const auto& laneState = current.lanes[lane];
            for (int control = 0; control < 4; ++control)
            {
                const auto slot = laneControlBounds(lane, control);
                graphics.setColour(juce::Colour(control == 0 && laneState.solo
                                                     ? lookAndFeel_.palette().accent
                                                 : control == 1 && laneState.muted
                                                     ? lookAndFeel_.palette().magenta
                                                     : lookAndFeel_.palette().panelRaised));
                graphics.fillRect(slot);
                graphics.setColour(juce::Colour(lookAndFeel_.palette().borderBright));
                graphics.drawRect(slot, std::max(0.8f, scale));
                graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
                // Tutorial lane labels fill the button vertically; shrink only for dense lanes.
                graphics.setFont(bsl::ui::font(std::min(16.0f * scale, slot.getHeight()), true));
                graphics.drawText(control == 0 ? "S" : control == 1 ? "M" : control == 2 ? "-" : "+",
                                  slot.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    graphics.restoreState();

    // Independent zoom sliders and viewport-sized scroll thumbs (Basic tutorial 5:28).
    const auto paintViewControls = [&](int axis) {
        const auto scrollRail = scrollRailBounds(axis);
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.fillRect(scrollRail);
        graphics.setColour(juce::Colour(lookAndFeel_.palette().textMuted));
        graphics.fillRect(scrollThumbBounds(axis));
        const auto zoomRail = zoomRailBounds(axis);
        graphics.setColour(juce::Colour(lookAndFeel_.palette().borderBright));
        graphics.fillRect(zoomRail);
        const float fraction = ((axis == 1 ? horizontalZoom_ : verticalZoom_) - 1.0f) / 7.0f;
        const auto centre = axis == 1
            ? juce::Point<float>(zoomRail.getX() + fraction * zoomRail.getWidth(), zoomRail.getCentreY())
            : juce::Point<float>(zoomRail.getCentreX(), zoomRail.getBottom() - fraction * zoomRail.getHeight());
        graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
        graphics.fillEllipse(centre.x - 4.0f * scale, centre.y - 4.0f * scale,
                             8.0f * scale, 8.0f * scale);
        graphics.setFont(bsl::ui::font(13.0f * scale, true));
        graphics.drawText("-", zoomButtonBounds(axis, false), juce::Justification::centred);
        graphics.drawText("+", zoomButtonBounds(axis, true), juce::Justification::centred);
    };
    paintViewControls(1);

    // Right-side arrangement rail and vertical pads.
    const auto arrangement = arrangementBounds();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(arrangement);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(arrangement.toFloat(), std::max(1.0f, scale));
    paintViewControls(2);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().textMuted));
    graphics.setFont(bsl::ui::font(10.0f * scale, true));
    if (state.ui.patternMode)
    {
        graphics.drawText("Q", designRect(getWidth(), getHeight(), 1198.0f, 88.0f, 12.0f, 18.0f),
                          juce::Justification::centred);
        const auto arrow = designRect(getWidth(), getHeight(), 1213.0f, 94.0f, 8.0f, 5.0f).toFloat();
        juce::Path triangle;
        triangle.addTriangle(arrow.getTopLeft(), arrow.getTopRight(),
                             {arrow.getCentreX(), arrow.getBottom()});
        graphics.fillPath(triangle);
    }
    else
        graphics.drawText(juce::String(current.name),
                          designRect(getWidth(), getHeight(), 1161.0f, 88.0f, 99.0f, 18.0f),
                          juce::Justification::centred);
    {
        const auto padArea = arrangementPadBounds();
        const juce::Graphics::ScopedSaveState savedGraphics(graphics);
        graphics.reduceClipRegion(padArea);
        if (state.ui.patternMode)
        {
            for (std::size_t index = 0; index < state.patterns.size(); ++index)
            {
                const auto bounds = arrangementItemBounds(index);
                if (bounds.getY() >= padArea.getBottom()) break;
                if (bounds.getBottom() <= padArea.getY()) continue;
                graphics.setColour(index == state.activePattern
                                       ? juce::Colour(lookAndFeel_.palette().panelRaised)
                                       : juce::Colour(lookAndFeel_.palette().panel));
                graphics.fillRect(bounds);
                graphics.setColour(index == state.activePattern
                                       ? juce::Colour(lookAndFeel_.palette().borderBright)
                                       : juce::Colour(lookAndFeel_.palette().border));
                graphics.drawRect(bounds, index == state.activePattern ? 2 : 1);
                graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
                graphics.setFont(bsl::ui::font(14.0f * scale, true));
                graphics.drawFittedText(juce::String(state.patterns[index].name),
                                        bounds.reduced(juce::roundToInt(5.0f * scale),
                                                       juce::roundToInt(3.0f * scale)),
                                        juce::Justification::centred, 1);
                drawActionGlyph(bounds.toFloat().removeFromTop(18.0f * scale)
                                    .removeFromLeft(18.0f * scale), 1);
                drawActionGlyph(bounds.toFloat().removeFromTop(18.0f * scale)
                                    .removeFromRight(18.0f * scale), 2);
                drawActionGlyph(bounds.toFloat().withSize(18.0f * scale, 18.0f * scale)
                                    .withY(bounds.getBottom() - 18.0f * scale), 0);
                // Action glyphs use the muted tint; note labels retain the text tint.
                graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
                graphics.setFont(bsl::ui::font(9.0f * scale, true));
                graphics.drawText(midiNoteLabel(state.patterns[index].triggerNote, state.ui),
                                  bounds.toFloat().reduced(7.0f * scale, 5.0f * scale)
                                      .removeFromBottom(12.0f * scale),
                                  juce::Justification::centredRight);
            }
        }
        else
        {
            for (std::size_t index = 0; index < current.bars.size(); ++index)
            {
                const auto bounds = arrangementItemBounds(index);
                if (bounds.getY() >= padArea.getBottom()) break;
                if (bounds.getBottom() <= padArea.getY()) continue;
                graphics.setColour(index == state.ui.selectedBar
                                       ? juce::Colour(lookAndFeel_.palette().panelRaised)
                                       : juce::Colour(lookAndFeel_.palette().panel));
                graphics.fillRect(bounds);
                graphics.setColour(index == state.ui.selectedBar
                                       ? juce::Colour(lookAndFeel_.palette().borderBright)
                                       : juce::Colour(lookAndFeel_.palette().border));
                graphics.drawRect(bounds, index == state.ui.selectedBar ? 2 : 1);
                graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
                graphics.setFont(bsl::ui::font(11.0f * scale, true));
                graphics.drawText(juce::String(current.bars[index].numerator) + "  |  "
                                      + juce::String(current.bars[index].denominator),
                                  bounds, juce::Justification::centred);
                const auto padBounds = bounds.toFloat();
                drawActionGlyph(padBounds.withSizeKeepingCentre(18.0f * scale, 18.0f * scale)
                                     .withPosition(padBounds.getX(), padBounds.getY()), 1);
                drawActionGlyph(padBounds.withSizeKeepingCentre(18.0f * scale, 18.0f * scale)
                                     .withPosition(padBounds.getRight() - 18.0f * scale, padBounds.getY()), 2);
                drawActionGlyph(padBounds.withSizeKeepingCentre(18.0f * scale, 18.0f * scale)
                                     .withPosition(padBounds.getX(), padBounds.getBottom() - 18.0f * scale), 0);
            }
        }

    }

    // Persistent lower kit, sound/randomizer, and master modules.
    const auto lower = lowerBounds();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(lower);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(lower.toFloat(), std::max(1.0f, scale));

    const auto kit = designRect(getWidth(), getHeight(), 8.0f, 522.0f, 388.0f, 190.0f);
    const auto sound = soundBounds();
    const auto master = masterBounds();
    for (auto section : { kit, sound, master })
    {
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.drawRect(section.toFloat(), std::max(1.0f, scale));
        graphics.setColour(juce::Colour(lookAndFeel_.palette().header));
        graphics.fillRect(section.removeFromTop(static_cast<int>(34.0f * scale)));
    }
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
    graphics.setFont(bsl::ui::font(14.0f * scale, true));
    graphics.drawText(module_ == Module::randomizer ? "Randomizer" : juce::String(padNames[state.ui.selectedPad]),
                      designRect(getWidth(), getHeight(), 546.0f, 524.0f, 384.0f, 30.0f),
                      juce::Justification::centred);
    graphics.setFont(bsl::ui::font(17.0f * scale, true));
    graphics.drawText("Master", designRect(getWidth(), getHeight(), 1066.0f, 526.0f, 206.0f, 30.0f),
                      juce::Justification::centred);

    const auto drawMeter = [&graphics, this](float level, float x, float y, float width, float height) {
        auto bounds = designRect(getWidth(), getHeight(), x, y, width, height).toFloat();
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.fillRect(bounds);
        const float fraction = level > 0.001f ? juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(level) + 60.0f) / 60.0f) : 0.0f;
        graphics.setColour(level > 1.0f ? juce::Colour(0xffef4665) : juce::Colour(lookAndFeel_.palette().meter));
        graphics.fillRect(bounds.removeFromBottom(bounds.getHeight() * fraction));
    };
    // These are the fader's two rails. The slider paints only its thumb so
    // its opaque background cannot cover the actual output levels.
    drawMeter(outputMeterLevels_[0], 1233.5f, 612.0f, 4.0f, 92.0f);
    drawMeter(outputMeterLevels_[1], 1244.5f, 612.0f, 4.0f, 92.0f);
    if (module_ != Module::randomizer)
        drawMeter(padMeterLevels_[state.ui.selectedPad], 1031.0f, 616.0f, 7.0f, 76.0f);

    if (module_ == Module::randomizer)
    {
        // Positions measured from the official Randomizer tutorial at 5:11.
        // Each section has its own outline, with space between adjacent sections.
        struct RandomizerPanel { const char* title; float x; float width; float titleCentre; };
        graphics.setFont(bsl::ui::font(11.0f * scale, true));
        for (const auto& item : std::array<RandomizerPanel, 4> {{
                 { "Drum Selection", 408.0f, 121.0f, 459.0f },
                 { "Beat Slicing", 542.0f, 171.0f, 626.0f },
                 { "Step Triggering", 727.0f, 150.0f, 796.0f },
                 { "Velocity Range", 890.0f, 166.0f, 970.0f }
             }})
        {
            graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
            graphics.drawRect(designRect(getWidth(), getHeight(), item.x, 556.0f, item.width, 158.0f).toFloat(),
                              std::max(1.0f, scale));
            graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
            graphics.drawText(item.title,
                              designRect(getWidth(), getHeight(), item.titleCentre - 48.0f, 560.0f, 96.0f, 22.0f),
                              juce::Justification::centred);
        }
    }
    else
    {
        auto samplePanel = designRect(getWidth(), getHeight(), 402.0f, 558.0f, 146.0f, 152.0f);
        const auto& selectedPad = state.kit.pads[state.ui.selectedPad];
        const auto sampleColour = padColours_[state.ui.selectedPad].withSaturation(0.72f)
                                                              .withBrightness(0.70f);
        const bool custom = !selectedPad.customSampleLayered
                            && !selectedPad.customSamples[0].name.empty();
        graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
        graphics.fillRect(samplePanel);
        graphics.setColour(sampleColour);
        graphics.fillRect(samplePanel.removeFromTop(static_cast<int>((custom ? 7.0f : 10.0f) * scale)));
        graphics.setColour(juce::Colour(lookAndFeel_.palette().text).withAlpha(0.90f));
        const auto sampleGlyph = designRect(getWidth(), getHeight(), custom ? 451.0f : 448.0f, custom ? 627.0f : 607.0f,
                                             52.0f, custom ? 36.0f : 42.0f).toFloat();
        if (custom)
        {
            juce::Path waveform;
            // The tutorial's custom-sample symbol has flat ends and three sharp peaks.
            constexpr std::array<juce::Point<float>, 9> waveformPoints {{
                { 0.000f, 0.590f }, { 0.171f, 0.590f }, { 0.239f, 0.415f },
                { 0.316f, 0.771f }, { 0.382f, 0.339f }, { 0.454f, 0.920f },
                { 0.533f, 0.000f }, { 0.632f, 0.590f }, { 1.000f, 0.590f }
            }};
            waveform.startNewSubPath(sampleGlyph.getX(),
                                     sampleGlyph.getY() + waveformPoints.front().y * sampleGlyph.getHeight());
            for (std::size_t index = 1; index < waveformPoints.size(); ++index)
                waveform.lineTo(sampleGlyph.getX() + waveformPoints[index].x * sampleGlyph.getWidth(),
                                sampleGlyph.getY() + waveformPoints[index].y * sampleGlyph.getHeight());
            graphics.strokePath(waveform, juce::PathStrokeType(std::max(1.5f, 2.75f * scale),
                                                               juce::PathStrokeType::curved));
        }
        else
        {
            bsl::ui::drawInstrumentGlyph(graphics, sampleGlyph, state.ui.selectedPad,
                juce::Colour(lookAndFeel_.palette().text).withAlpha(0.90f),
                std::max(1.4f, 1.7f * scale));
        }
        // Vertical separators reproduce the manual's sound design groupings.
        for (const float x : { 566.0f, 626.0f, 687.0f, 850.0f, 976.0f })
        {
            graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
            const auto line = designRect(getWidth(), getHeight(), x, 558.0f, 1.0f, 152.0f);
            graphics.fillRect(line);
        }
    }

}

void BeatScholarLinuxEditor::paintPattern(juce::Graphics& graphics, juce::Rectangle<float> area)
{
    const auto& state = processor_.projectState();
    const auto& pattern = state.patterns[state.activePattern];
    const auto laneCount = pattern.lanes.size();
    const auto beatCount = pattern.lanes.front().beats.size();
    if (laneCount == 0 || beatCount == 0)
        return;
    const auto spans = beatSpans(pattern);
    const float quarterWidth = area.getWidth() / static_cast<float>(pattern.durationInQuarterNotes()) * horizontalZoom_;
    const float cellHeight = area.getHeight() / static_cast<float>(laneCount) * verticalZoom_;
    const float scale = designScale(getWidth(), getHeight());

    const auto preview = beatClipboard_ && beatHoverPosition_
                             && juce::KeyPress::isKeyCurrentlyDown('v')
                             && !juce::KeyPress::isKeyCurrentlyDown('c')
                         ? beatAt(*beatHoverPosition_) : std::nullopt;

    graphics.saveState();
    graphics.reduceClipRegion(area.toNearestInt());
    for (std::size_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const float y = area.getY()
                        + (static_cast<float>(laneIndex) - verticalScroll_) * cellHeight;
        for (std::size_t beatIndex = 0; beatIndex < beatCount; ++beatIndex)
        {
            const float cellWidth = spans[beatIndex].length * quarterWidth;
            const float x = area.getX() + (spans[beatIndex].start - horizontalScroll_) * quarterWidth;
            const juce::Rectangle<float> cell(x, y, cellWidth, cellHeight);
            graphics.setColour(juce::Colour((beatIndex + laneIndex) % 2 == 0
                                                ? lookAndFeel_.palette().panel
                                                : lookAndFeel_.palette().background));
            graphics.fillRect(cell);
            graphics.setColour(juce::Colour(lookAndFeel_.palette().grid));
            graphics.drawRect(cell, std::max(1.0f, scale));
        }
    }

    graphics.setColour(juce::Colour(lookAndFeel_.palette().header));
    for (std::size_t laneIndex = 1; laneIndex < laneCount; ++laneIndex)
    {
        const float y = area.getY()
                        + (static_cast<float>(laneIndex) - verticalScroll_) * cellHeight;
        graphics.fillRect(area.getX(), y - 2.0f * scale,
                          area.getWidth(), std::max(2.0f, 4.0f * scale));
    }
    float boundaryQuarter = 0.0f;
    for (std::size_t bar = 0; bar + 1 < pattern.bars.size(); ++bar)
    {
        boundaryQuarter += static_cast<float>(pattern.bars[bar].numerator) * 4.0f / pattern.bars[bar].denominator;
        const float x = area.getX()
                        + (boundaryQuarter - horizontalScroll_) * quarterWidth;
        graphics.fillRect(x - 2.0f * scale, area.getY(),
                          std::max(2.0f, 4.0f * scale), area.getHeight());
    }

    for (std::size_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const auto& lane = pattern.lanes[laneIndex];
        const float y = area.getY()
                        + (static_cast<float>(laneIndex) - verticalScroll_ + 0.5f) * cellHeight;
        if (y + cellHeight * 0.36f < area.getY() || y - cellHeight * 0.36f > area.getBottom())
            continue;
        for (std::size_t beatIndex = 0; beatIndex < beatCount; ++beatIndex)
        {
            const bool previewing = preview && preview->lane == laneIndex && preview->beat == beatIndex;
            const auto& beat = previewing ? *beatClipboard_ : lane.beats[beatIndex];
            const float radius = std::max(3.0f, std::min(std::min(1.0f, spans[beatIndex].length) * quarterWidth,
                                                       cellHeight) * 0.36f);
            const float x = area.getX()
                            + (spans[beatIndex].start + spans[beatIndex].length * 0.5f - horizontalScroll_) * quarterWidth;
            if (x + radius < area.getX() || x - radius > area.getRight())
                continue;
            if (previewing)
                graphics.beginTransparencyLayer(0.5f);
            const auto slices = beat.slices.size();
            // A populated beat outlines every slice, including empty sectors
            // (Samples & Sounds tutorial, 2:35). Keep an empty beat dim.
            const bool populated = std::any_of(beat.slices.begin(), beat.slices.end(),
                                              [](const auto& slice) { return slice.enabled; });
            for (std::size_t sliceIndex = 0; sliceIndex < slices; ++sliceIndex)
            {
                const float start = juce::MathConstants<float>::pi + juce::MathConstants<float>::twoPi
                                          * static_cast<float>(sliceIndex) / static_cast<float>(slices);
                const float end = juce::MathConstants<float>::pi + juce::MathConstants<float>::twoPi
                                        * static_cast<float>(sliceIndex + 1) / static_cast<float>(slices);
                juce::Path wedge;
                // Slice zero starts at six o'clock and proceeds clockwise,
                // matching beatAt(). A pie closes through the centre, not a chord.
                wedge.addPieSegment(x - radius, y - radius, radius * 2.0f,
                                    radius * 2.0f, start, end, 0.0f);
                const auto& slice = beat.slices[sliceIndex];
                const std::size_t pad = std::min<std::size_t>(slice.padIndex, bsl::padCount - 1);
                graphics.setColour(slice.enabled
                                       ? padColours_[pad].withBrightness(0.72f)
                                             .withAlpha(std::clamp(0.55f + slice.velocity * 0.45f,
                                                                   0.0f, 1.0f))
                                       : juce::Colour(lookAndFeel_.palette().panel));
                graphics.fillPath(wedge);
                graphics.setColour(populated ? juce::Colour(lookAndFeel_.palette().text)
                                             : juce::Colour(lookAndFeel_.palette().border));
                graphics.strokePath(wedge, juce::PathStrokeType(std::max(1.0f, scale * 1.15f)));
            }
            if (previewing)
                graphics.endTransparencyLayer();
            if (lastHit_ && lastHit_->lane == laneIndex && lastHit_->beat == beatIndex)
            {
                graphics.setColour(juce::Colour(lookAndFeel_.palette().accentBright));
                graphics.drawEllipse(x - radius - 2.0f, y - radius - 2.0f,
                                     radius * 2.0f + 4.0f, radius * 2.0f + 4.0f, 2.0f);
            }
        }
    }
    graphics.restoreState();
}

void BeatScholarLinuxEditor::resized()
{
    for (auto& label : effectLabels_)
        label.setFont(bsl::ui::font(14.0f * designScale(getWidth(), getHeight()), true));
    for (auto& label : soundLabels_)
        label.setFont(bsl::ui::font(14.0f * designScale(getWidth(), getHeight()), true));
    title_.setFont(juce::Font(juce::FontOptions("Nimbus Sans Narrow", "Bold",
+                                                24.0f * designScale(getWidth(), getHeight()))));
    title_.setBounds(designRect(getWidth(), getHeight(), 16.0f, 13.0f, 132.0f, 34.0f));
    presetPrevious_.setBounds(designRect(getWidth(), getHeight(), 150.0f, 16.0f, 24.0f, 28.0f));
    preset_.setBounds(designRect(getWidth(), getHeight(), 176.0f, 14.0f, 116.0f, 32.0f));
    presetNext_.setBounds(designRect(getWidth(), getHeight(), 294.0f, 16.0f, 24.0f, 28.0f));
    presetSave_.setBounds(designRect(getWidth(), getHeight(), 322.0f, 16.0f, 24.0f, 28.0f));
    sequencer_.setVisible(false);
    sampler_.setVisible(false);
    swing_.setBounds(designRect(getWidth(), getHeight(), 365.0f, 27.0f, 112.0f, 20.0f));
    stop_.setBounds(designRect(getWidth(), getHeight(), 493.0f, 14.0f, 34.0f, 34.0f));
    play_.setBounds(designRect(getWidth(), getHeight(), 535.0f, 12.0f, 38.0f, 38.0f));
    pause_.setVisible(false);
    bpm_.setBounds(designRect(getWidth(), getHeight(), 580.0f, 15.0f, 90.0f, 32.0f));
    bpm_.setVisible(true);
    drumBrush_.setBounds(designRect(getWidth(), getHeight(), 688.0f, 14.0f, 34.0f, 34.0f));
    slicerBrush_.setBounds(designRect(getWidth(), getHeight(), 742.0f, 14.0f, 34.0f, 34.0f));
    velocityBrush_.setBounds(designRect(getWidth(), getHeight(), 806.0f, 14.0f, 34.0f, 34.0f));
    randomizerBrush_.setBounds(designRect(getWidth(), getHeight(), 920.0f, 14.0f, 34.0f, 34.0f));
    defaultVelocity_.setBounds(designRect(getWidth(), getHeight(), 846.0f, 16.0f, 66.0f, 28.0f));
    undo_.setBounds(designRect(getWidth(), getHeight(), 1060.0f, 13.0f, 38.0f, 36.0f));
    redo_.setBounds(designRect(getWidth(), getHeight(), 1100.0f, 13.0f, 38.0f, 36.0f));
    about_.setBounds(designRect(getWidth(), getHeight(), 1144.0f, 13.0f, 120.0f, 36.0f));
    status_.setBounds(designRect(getWidth(), getHeight(), 958.0f, 15.0f, 94.0f, 30.0f));

    const auto kit = kitBounds();
    const int gap = std::max(2, static_cast<int>(3.0f * designScale(getWidth(), getHeight())));
    const int padWidth = (kit.getWidth() - gap * 7) / 8;
    const int padHeight = (kit.getHeight() - gap) / 2;
    for (std::size_t index = 0; index < pads_.size(); ++index)
    {
        const int column = static_cast<int>(index % 8);
        const int row = 1 - static_cast<int>(index / 8);
        pads_[index].setBounds(kit.getX() + column * (padWidth + gap),
                               kit.getY() + row * (padHeight + gap), padWidth, padHeight);
    }

    auditionDuringPlayback_.setBounds(designRect(getWidth(), getHeight(), 12.0f, 526.0f, 34.0f, 26.0f));
    midiMap_.setBounds(designRect(getWidth(), getHeight(), 48.0f, 526.0f, 42.0f, 26.0f));
    kitMenu_.setBounds(designRect(getWidth(), getHeight(), 92.0f, 524.0f, 194.0f, 30.0f));
    kitLocked_.setBounds(designRect(getWidth(), getHeight(), 290.0f, 526.0f, 28.0f, 26.0f));
    kitSave_.setBounds(designRect(getWidth(), getHeight(), 322.0f, 526.0f, 28.0f, 26.0f));
    kitPrevious_.setBounds(designRect(getWidth(), getHeight(), 354.0f, 526.0f, 18.0f, 26.0f));
    kitNext_.setBounds(designRect(getWidth(), getHeight(), 374.0f, 526.0f, 18.0f, 26.0f));
    soundTab_.setBounds(designRect(getWidth(), getHeight(), 546.0f, 524.0f, 360.0f, 30.0f));
    randomizerTab_.setBounds(designRect(getWidth(), getHeight(), 546.0f, 524.0f, 360.0f, 30.0f));
    randomizerPrevious_.setBounds(designRect(getWidth(), getHeight(), 1004.0f, 528.0f, 24.0f, 26.0f));
    randomizerNext_.setBounds(designRect(getWidth(), getHeight(), 1032.0f, 528.0f, 24.0f, 26.0f));
    routing_.setBounds(designRect(getWidth(), getHeight(), 1210.0f, 527.0f, 54.0f, 25.0f));
    masterTab_.setBounds(designRect(getWidth(), getHeight(), 1243.0f, 527.0f, 28.0f, 27.0f));
    const bool standalone = processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    routing_.setVisible(!standalone);
    masterTab_.setVisible(standalone);
    kitPrevious_.setVisible(module_ != Module::randomizer);
    kitNext_.setVisible(module_ != Module::randomizer);
    randomizerPrevious_.setVisible(module_ == Module::randomizer);
    randomizerNext_.setVisible(module_ == Module::randomizer);

    const auto& selectedPad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
    const bool custom = !selectedPad.customSampleLayered
                        && !selectedPad.customSamples[0].name.empty();
    loadSample_.setBounds(designRect(getWidth(), getHeight(), 402.0f, 622.0f, 28.0f, 26.0f));
    clearSample_.setBounds(designRect(getWidth(), getHeight(), 512.0f, 563.0f, 27.0f, 24.0f));
    layerOne_.setBounds(designRect(getWidth(), getHeight(), custom ? 408.0f : 415.0f,
                                   custom ? 587.0f : 578.0f, custom ? 131.0f : 118.0f,
                                   custom ? 26.0f : 19.0f));
    layerTwo_.setBounds(designRect(getWidth(), getHeight(), 415.0f, 683.0f, 118.0f, 19.0f));
    padMute_.setBounds(designRect(getWidth(), getHeight(), 404.0f, 526.0f, 30.0f, 26.0f));
    padSolo_.setBounds(designRect(getWidth(), getHeight(), 436.0f, 526.0f, 30.0f, 26.0f));
    mono_.setBounds(designRect(getWidth(), getHeight(), 468.0f, 526.0f, 76.0f, 26.0f));
    oneShot_.setBounds(designRect(getWidth(), getHeight(), 440.0f, custom ? 672.0f : 650.0f, 80.0f, 24.0f));

    if (module_ == Module::randomizer)
    {
        const std::array<float, 4> toggleX { 503.0f, 687.0f, 851.0f, 1033.0f };
        for (std::size_t index = 0; index < randomModuleToggles_.size(); ++index)
            randomModuleToggles_[index].setBounds(designRect(getWidth(), getHeight(), toggleX[index], 559.0f, 24.0f, 24.0f));
        randomClearPads_.setBounds(designRect(getWidth(), getHeight(), 445.0f, 607.0f, 45.0f, 32.0f));
        randomAllPads_.setBounds(designRect(getWidth(), getHeight(), 445.0f, 663.0f, 45.0f, 31.0f));
        randomClearSlices_.setBounds(designRect(getWidth(), getHeight(), 585.0f, 592.0f, 35.0f, 20.0f));
        randomAllSlices_.setBounds(designRect(getWidth(), getHeight(), 636.0f, 592.0f, 35.0f, 20.0f));
        randomSlicePrevious_.setBounds(designRect(getWidth(), getHeight(), 553.0f, 651.0f, 23.0f, 25.0f));
        randomSliceNext_.setBounds(designRect(getWidth(), getHeight(), 681.0f, 651.0f, 23.0f, 25.0f));
        for (std::size_t index = 0; index < randomSliceButtons_.size(); ++index)
        {
            const int column = static_cast<int>(index % 3);
            const int row = static_cast<int>(index / 3);
            randomSliceButtons_[index].setBounds(designRect(getWidth(), getHeight(),
                582.0f + static_cast<float>(column) * 30.5f,
                620.0f + static_cast<float>(row) * 30.5f, 27.0f, 27.0f));
        }
        const std::array<juce::Rectangle<int>, 8> controlBounds {
            designRect(getWidth(), getHeight(), 737.0f, 606.0f, 72.0f, 72.0f),
            designRect(getWidth(), getHeight(), 750.0f, 689.0f, 104.0f, 20.0f),
            designRect(getWidth(), getHeight(), 901.0f, 605.0f, 72.0f, 72.0f),
            designRect(getWidth(), getHeight(), 977.0f, 605.0f, 72.0f, 72.0f),
            designRect(getWidth(), getHeight(), 668.0f, 564.0f, 24.0f, 24.0f),
            designRect(getWidth(), getHeight(), 500.0f, 564.0f, 24.0f, 24.0f),
            designRect(getWidth(), getHeight(), 1018.0f, 564.0f, 24.0f, 24.0f),
            designRect(getWidth(), getHeight(), 831.0f, 627.0f, 28.0f, 28.0f)
        };
        const std::array<juce::Rectangle<int>, 8> labelBounds {
            designRect(getWidth(), getHeight(), 732.0f, 590.0f, 82.0f, 14.0f),
            designRect(getWidth(), getHeight(), 764.0f, 675.0f, 76.0f, 14.0f),
            designRect(getWidth(), getHeight(), 901.0f, 592.0f, 72.0f, 14.0f),
            designRect(getWidth(), getHeight(), 977.0f, 592.0f, 72.0f, 14.0f),
            designRect(getWidth(), getHeight(), 668.0f, 564.0f, 24.0f, 14.0f),
            designRect(getWidth(), getHeight(), 500.0f, 564.0f, 24.0f, 14.0f),
            designRect(getWidth(), getHeight(), 1018.0f, 564.0f, 24.0f, 14.0f),
            designRect(getWidth(), getHeight(), 814.0f, 590.0f, 62.0f, 14.0f)
        };
        for (std::size_t index = 0; index < 8; ++index)
        {
            soundLabels_[index].setBounds(labelBounds[index]);
            soundControls_[index].setBounds(controlBounds[index]);
        }
        randomFillEmpty_.setBounds(controlBounds[7]);
    }
    else
    {
        const std::array<juce::Point<float>, 8> characterPositions {{
            { 573.0f, 573.0f }, { 573.0f, 649.0f }, { 635.0f, 573.0f }, { 635.0f, 649.0f },
            { 508.0f, 605.0f }, { 984.0f, 566.0f }, { 984.0f, 642.0f }, { 468.0f, 638.0f }
        }};
        for (std::size_t index = 0; index < characterPositions.size(); ++index)
        {
            if (index == 4)
            {
                soundLabels_[index].setBounds(designRect(getWidth(), getHeight(),
                    510.0f, 568.0f, 34.0f, 14.0f));
                soundControls_[index].setBounds(designRect(getWidth(), getHeight(),
                    524.0f, 578.0f, 24.0f, 85.0f));
            }
            else if (index == 5)
            {
                soundLabels_[index].setBounds(designRect(getWidth(), getHeight(), 984.0f, 566.0f, 54.0f, 14.0f));
                soundControls_[index].setBounds(designRect(getWidth(), getHeight(), 990.0f, 607.0f, 42.0f, 95.0f));
            }
            else if (index == 6)
            {
                soundLabels_[index].setBounds(designRect(getWidth(), getHeight(), 983.0f, 582.0f, 56.0f, 18.0f));
                soundControls_[index].setBounds(designRect(getWidth(), getHeight(), 992.0f, 582.0f, 38.0f, 18.0f));
            }
            else
            {
                soundLabels_[index].setBounds(designRect(getWidth(), getHeight(),
                    characterPositions[index].x, characterPositions[index].y, 54.0f, 14.0f));
                // Character dials use the smaller insert-control rings in the
                // Samples & Sounds tutorial; send controls are deliberately larger.
                soundControls_[index].setBounds(index < 4
                    ? designRect(getWidth(), getHeight(), characterPositions[index].x + 6.0f,
                                 index % 2 == 0 ? 589.0f : 662.0f, 42.0f, 42.0f)
                    : designRect(getWidth(), getHeight(), characterPositions[index].x,
                                 characterPositions[index].y + 12.0f, 54.0f, 55.0f));
            }
        }
        const std::array<juce::Point<float>, 10> effectPositions {{
            { 698.0f, 573.0f }, { 754.0f, 573.0f }, { 809.0f, 573.0f },
            { 698.0f, 649.0f }, { 754.0f, 649.0f }, { 809.0f, 649.0f },
            { 928.0f, 573.0f }, { 870.0f, 573.0f }, { 870.0f, 649.0f }, { 928.0f, 649.0f }
        }};
        for (std::size_t index = 0; index < effectPositions.size(); ++index)
        {
            effectLabels_[index].setBounds(designRect(getWidth(), getHeight(),
                effectPositions[index].x, effectPositions[index].y, 54.0f, 14.0f));
            if (index < 6)
                effectControls_[index].setBounds(designRect(getWidth(), getHeight(),
                    effectPositions[index].x + 6.0f,
                    index < 3 ? 589.0f : 662.0f, 42.0f, 42.0f));
            else if (index < 9)
                // Including the centered outline, 50px bounds paint the tutorial's
                // 40px send rings. The lower ring aligns with the insert row.
                effectControls_[index].setBounds(designRect(getWidth(), getHeight(),
                    effectPositions[index].x + 2.0f,
                    index < 8 ? 586.0f : 658.0f, 50.0f, 50.0f));
            else
                // Gain is a vertical fader, with its own track and thumb geometry.
                effectControls_[index].setBounds(designRect(getWidth(), getHeight(),
                    effectPositions[index].x, 654.0f, 54.0f, 55.0f));
        }
    }

    // Painted centers and labels measured from the official Randomizer
    // tutorial at 5:11, normalized to the 1280 x 720 design surface.
    const std::array<juce::Point<float>, 5> masterPositions {{
        { 1108.0f, 573.0f }, { 1179.0f, 573.0f }, { 1108.0f, 649.0f },
        { 1179.0f, 649.0f }, { 1241.0f, 571.0f }
    }};
    for (std::size_t index = 0; index < masterPositions.size(); ++index)
    {
        masterLabels_[index].setFont(bsl::ui::font(14.0f * designScale(getWidth(), getHeight()), true));
        masterLabels_[index].setBounds(designRect(getWidth(), getHeight(),
            masterPositions[index].x - 26.0f, masterPositions[index].y, 52.0f, 14.0f));
        masterControls_[index].setBounds(index == 4
            ? designRect(getWidth(), getHeight(), 1216.0f, 600.0f, 50.0f, 116.0f)
            : designRect(getWidth(), getHeight(), masterPositions[index].x - 21.0f,
                         masterPositions[index].y + 19.0f, 42.0f, 42.0f));
    }

    barsPatterns_.setBounds(designRect(getWidth(), getHeight(), 1185.0f, 64.0f, 60.0f, 22.0f));
    auto addBounds = designRect(getWidth(), getHeight(), 1161.0f, 456.0f, 99.0f,
                                shortPatternList() ? 62.0f : 46.0f);
    const auto& arrangementState = processor_.projectState();
    if (arrangementState.ui.patternMode && maximumArrangementScroll() == 0.0f)
    {
        // The tutorial's short Pattern lists place + immediately after the cards.
        // Keep it below the viewport when the complete list and tile do not fit.
        const auto nextY = arrangementPadBounds().getY()
            + static_cast<int>(arrangementState.patterns.size()) * arrangementItemHeight();
        if (nextY + addBounds.getHeight() <= arrangementPadBounds().getBottom())
            addBounds.setY(nextY);
    }
    addItem_.setBounds(addBounds);
    duplicateItem_.setVisible(false);
    removeItem_.setVisible(false);
    updateEffectDragValue();
}

std::optional<BeatScholarLinuxEditor::BeatHit> BeatScholarLinuxEditor::beatAt(juce::Point<int> point,
                                                                         bool insidePieOnly) const
{
    const auto area = patternBounds();
    if (!area.contains(point))
        return std::nullopt;
    const auto& state = processor_.projectState();
    const auto& pattern = state.patterns[state.activePattern];
    const auto lanes = pattern.lanes.size();
    const auto spans = beatSpans(pattern);
    const float quarterWidth = static_cast<float>(area.getWidth())
                               / static_cast<float>(pattern.durationInQuarterNotes()) * horizontalZoom_;
    const float cellHeight = static_cast<float>(area.getHeight()) / static_cast<float>(lanes)
                             * verticalZoom_;
    const auto quarterPosition = horizontalScroll_
                                 + static_cast<float>(point.x - area.getX()) / quarterWidth;
    const auto lanePosition = verticalScroll_
                              + static_cast<float>(point.y - area.getY()) / cellHeight;
    if (quarterPosition < 0.0f || lanePosition < 0.0f
        || quarterPosition >= static_cast<float>(pattern.durationInQuarterNotes())
        || lanePosition >= static_cast<float>(lanes))
        return std::nullopt;
    const auto found = std::find_if(spans.begin(), spans.end(), [quarterPosition](const auto& span) {
        return quarterPosition >= span.start && quarterPosition < span.start + span.length;
    });
    if (found == spans.end()) return std::nullopt;
    const auto beat = static_cast<std::size_t>(std::distance(spans.begin(), found));
    const auto lane = static_cast<std::size_t>(lanePosition);
    const float centreX = area.getX()
                          + (found->start + found->length * 0.5f - horizontalScroll_) * quarterWidth;
    const float centreY = area.getY()
                          + (static_cast<float>(lane) - verticalScroll_ + 0.5f) * cellHeight;
    if (insidePieOnly)
    {
        const float radius = std::max(3.0f,
            std::min(std::min(1.0f, found->length) * quarterWidth, cellHeight) * 0.36f);
        if (point.toFloat().getDistanceFrom({ centreX, centreY }) > radius)
            return std::nullopt;
    }
    float angle = std::atan2(static_cast<float>(point.y) - centreY,
                             static_cast<float>(point.x) - centreX)
                  - juce::MathConstants<float>::halfPi;
    if (angle < 0.0f)
        angle += juce::MathConstants<float>::twoPi;
    const auto slices = pattern.lanes[lane].beats[beat].slices.size();
    const auto slice = std::min(slices - 1, static_cast<std::size_t>(
        angle / juce::MathConstants<float>::twoPi * static_cast<float>(slices)));
    return BeatHit { lane, beat, slice };
}

std::optional<std::size_t> BeatScholarLinuxEditor::padAt(juce::Point<int> point) const
{
    for (std::size_t index = 0; index < pads_.size(); ++index)
        if (pads_[index].getBounds().contains(point))
            return index;
    return std::nullopt;
}

std::optional<std::size_t> BeatScholarLinuxEditor::laneAt(juce::Point<int> point) const
{
    const auto area = patternBounds();
    if (point.x < 4 || point.x >= area.getX() || point.y < area.getY() || point.y >= area.getBottom())
        return std::nullopt;
    const auto& lanes = processor_.projectState().patterns[processor_.projectState().activePattern].lanes;
    if (lanes.empty())
        return std::nullopt;
    const float cellHeight = static_cast<float>(area.getHeight())
                             / static_cast<float>(lanes.size()) * verticalZoom_;
    const auto position = verticalScroll_
                          + static_cast<float>(point.y - area.getY()) / cellHeight;
    if (position < 0.0f || position >= static_cast<float>(lanes.size()))
        return std::nullopt;
    return static_cast<std::size_t>(position);
}

std::optional<std::size_t> BeatScholarLinuxEditor::barAt(juce::Point<int> point,
                                                         bool arrangement) const
{
    const auto& state = processor_.projectState();
    const auto& pattern = state.patterns[state.activePattern];
    if (arrangement)
    {
        const auto area = arrangementPadBounds();
        if (!area.contains(point) || state.ui.patternMode || pattern.bars.empty())
            return std::nullopt;
        const auto index = static_cast<std::size_t>(arrangementScroll_
            + static_cast<float>(point.y - area.getY()) / arrangementItemHeight());
        return index < pattern.bars.size() ? std::optional<std::size_t>(index) : std::nullopt;
    }
    const auto area = barHeaderBounds();
    if (!area.contains(point))
        return std::nullopt;
    const float quarterWidth = static_cast<float>(area.getWidth())
                               / static_cast<float>(pattern.durationInQuarterNotes()) * horizontalZoom_;
    const auto position = horizontalScroll_
                          + static_cast<float>(point.x - area.getX()) / quarterWidth;
    if (position < 0.0f || position >= static_cast<float>(pattern.durationInQuarterNotes()))
        return std::nullopt;
    float end = 0.0f;
    for (std::size_t index = 0; index < pattern.bars.size(); ++index)
    {
        end += static_cast<float>(pattern.bars[index].numerator) * 4.0f / pattern.bars[index].denominator;
        if (position < end)
            return index;
    }
    return std::nullopt;
}

std::optional<bool> BeatScholarLinuxEditor::barMeterDenominatorAt(juce::Point<int> point,
                                                                 std::size_t bar) const
{
    if (arrangementPadBounds().contains(point))
    {
        if (arrangementActionAt(point, bar) >= 0) return std::nullopt;
        return point.x >= arrangementItemBounds(bar).getCentreX();
    }
    const auto& state = processor_.projectState();
    const auto& pattern = state.patterns[state.activePattern];
    const auto header = barHeaderBounds();
    const float quarterWidth = static_cast<float>(header.getWidth())
        / static_cast<float>(pattern.durationInQuarterNotes()) * horizontalZoom_;
    float barStart = 0.0f;
    for (std::size_t index = 0; index < bar; ++index)
        barStart += static_cast<float>(pattern.bars[index].durationInQuarterNotes());
    const float barLeft = header.getX() + (barStart - horizontalScroll_) * quarterWidth;
    const float barWidth = static_cast<float>(pattern.bars[bar].durationInQuarterNotes()) * quarterWidth;
    const float scale = designScale(getWidth(), getHeight());
    const float drawnWidth = barWidth - scale;
    const auto actions = barHeaderActions({ barLeft, static_cast<float>(header.getY()),
                                           drawnWidth, static_cast<float>(header.getHeight()) }, scale);
    if (actions.contains(point.toFloat()))
        return std::nullopt;
    return drawnWidth < 110.0f * scale ? point.y >= header.getCentreY()
                                     : point.x >= barLeft + barWidth * 0.5f;
}

std::optional<std::size_t> BeatScholarLinuxEditor::patternAt(juce::Point<int> point) const
{
    const auto area = arrangementPadBounds();
    const auto& state = processor_.projectState();
    if (!state.ui.patternMode || !area.contains(point) || state.patterns.empty())
        return std::nullopt;
    const auto index = static_cast<std::size_t>(arrangementScroll_
        + static_cast<float>(point.y - area.getY()) / arrangementItemHeight());
    return index < state.patterns.size() && arrangementItemBounds(index).contains(point)
        ? std::optional<std::size_t>(index) : std::nullopt;
}

juce::File BeatScholarLinuxEditor::writePatternMidi(std::size_t patternIndex,
                                                   juce::File directory) const
{
    constexpr int ticksPerQuarter = 960;
    const auto& state = processor_.projectState();
    if (patternIndex >= state.patterns.size())
        return {};
    const auto events = bsl::Sequencer::exportPattern(state, patternIndex, ticksPerQuarter);
    const auto& pattern = state.patterns[patternIndex];
    const double endTick = std::round(pattern.durationInQuarterNotes() * ticksPerQuarter);
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::textMetaEvent(3, juce::String(pattern.name)));
    const double exportBpm = processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone
        ? state.global.beatsPerMinute : processor_.effectiveBpm();
    auto tempo = juce::MidiMessage::tempoMetaEvent(static_cast<int>(
        std::llround(60000000.0 / std::max(1.0, exportBpm))));
    tempo.setTimeStamp(0.0);
    track.addEvent(tempo);
    double barTick = 0.0;
    for (const auto& bar : pattern.bars)
    {
        auto meter = juce::MidiMessage::timeSignatureMetaEvent(bar.numerator, bar.denominator);
        meter.setTimeStamp(barTick);
        track.addEvent(meter);
        barTick += static_cast<double>(bar.numerator) * 4.0 / bar.denominator * ticksPerQuarter;
    }
    // Match live note lifetimes: a retrigger releases the previous same-note hit.
    // Explicitly bound each release so JUCE cannot leave stale note-offs behind.
    std::array<std::int64_t, 128> nextOn;
    nextOn.fill(static_cast<std::int64_t>(endTick));
    std::vector<std::int64_t> noteOffTicks(events.size());
    for (std::size_t index = events.size(); index-- > 0;)
    {
        const auto& event = events[index];
        noteOffTicks[index] = std::min(event.tick + event.gateTicks, nextOn[event.note]);
        nextOn[event.note] = event.tick;
    }
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const auto& event = events[index];
        auto on = juce::MidiMessage::noteOn(1, event.note, event.velocity);
        on.setTimeStamp(static_cast<double>(event.tick));
        track.addEvent(on);
        auto off = juce::MidiMessage::noteOff(1, event.note);
        off.setTimeStamp(static_cast<double>(noteOffTicks[index]));
        track.addEvent(off);
    }
    track.updateMatchedPairs();
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(endTick);
    track.addEvent(end);
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ticksPerQuarter);
    midi.addTrack(track);
    if (directory == juce::File{})
        directory = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                        .getChildFile(".cache/BeatScholarLinux/MIDI");
    if (!directory.createDirectory().wasOk())
        return {};
    auto name = juce::String(state.patterns[patternIndex].name)
                    .retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ")
                    .trim();
    if (name.isEmpty())
        name = "Pattern";
    const auto file = directory.getNonexistentChildFile(name, ".mid", false);
    juce::FileOutputStream output(file);
    if (!output.openedOk() || !midi.writeTo(output))
        return {};
    output.flush();
    return file;
}

void BeatScholarLinuxEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (event.mods.isAltDown() || event.mods.isPopupMenu()) return;
    const auto position = event.getEventRelativeTo(this).getPosition();
    auto* slider = dynamic_cast<juce::Slider*>(event.eventComponent);
    if (slider == nullptr)
        slider = dynamic_cast<juce::Slider*>(getComponentAt(position));
    refreshRestoredState();
    auto centre = position;
    if (slider != nullptr && !slider->isEnabled()) return;
    if (slider != nullptr && slider->isVisible())
    {
        numericEntryBar_.reset();
        numericEntryControl_ = slider;
        numericEntry_.setTitle("Numeric value");
        numericEntry_.setText(slider->textFromValueFunction
                                  ? slider->textFromValueFunction(slider->getValue())
                                  : juce::String(slider->getValue(), slider->getNumDecimalPlacesToDisplay()), false);
        centre = slider->getBounds().getCentre();
    }
    else
    {
        const auto bar = barAt(position, false).has_value() ? barAt(position, false) : barAt(position, true);
        if (!bar) return;
        const auto denominator = barMeterDenominatorAt(position, *bar);
        if (!denominator) return;
        cancelPatternInteraction();
        const auto& state = processor_.projectState();
        const auto& meter = state.patterns[state.activePattern].bars[*bar];
        numericEntryControl_ = nullptr;
        numericEntryBar_ = BarMeterEntry { state.activePattern, *bar, *denominator,
                                          processor_.restoredStateRevision() };
        numericEntry_.setTitle(*denominator ? "Bar note value" : "Bar beat count");
        numericEntry_.setText(juce::String(*denominator ? meter.denominator : meter.numerator), false);
    }
    numericEntry_.setBounds(std::clamp(centre.x - 48, 0, std::max(0, getWidth() - 96)),
                            std::clamp(centre.y - 13, 0, std::max(0, getHeight() - 26)), 96, 26);
    numericEntry_.setVisible(true);
    numericEntry_.toFront(true);
    numericEntry_.grabKeyboardFocus();
    numericEntry_.selectAll();
}

void BeatScholarLinuxEditor::mouseEnter(const juce::MouseEvent& event)
{
    mouseMove(event);
}

void BeatScholarLinuxEditor::mouseMove(const juce::MouseEvent& event)
{
    beatHoverPosition_ = event.getEventRelativeTo(this).getPosition();
    if (beatClipboard_ && juce::KeyPress::isKeyCurrentlyDown('v'))
        repaint();
}

void BeatScholarLinuxEditor::mouseExit(const juce::MouseEvent&)
{
    beatHoverPosition_.reset();
    if (beatClipboard_)
        repaint();
}

void BeatScholarLinuxEditor::mouseDown(const juce::MouseEvent& event)
{
    refreshRestoredState();
    cancelPatternInteraction();
    const auto position = event.getEventRelativeTo(this).getPosition();
    beatHoverPosition_ = position;
    clampView();
    if (!event.mods.isPopupMenu())
    {
        if (processor_.projectState().ui.patternMode
            && designRect(getWidth(), getHeight(), 1161.0f, 88.0f, 99.0f, 18.0f).contains(position))
        {
            showPatternQuantizationMenu();
            return;
        }
        const float scale = designScale(getWidth(), getHeight());
        for (const int axis : { 1, 2 })
        {
            const bool increase = zoomButtonBounds(axis, true).contains(position);
            if (increase || zoomButtonBounds(axis, false).contains(position))
            {
                auto& zoom = axis == 1 ? horizontalZoom_ : verticalZoom_;
                zoom = std::clamp(zoom * (increase ? 2.0f : 0.5f), 1.0f, 8.0f);
                clampView();
                repaint();
                return;
            }
            if (zoomRailBounds(axis).expanded(5.0f * scale).contains(position.toFloat()))
            {
                draggingZoomRail_ = axis;
                moveZoomRail(position);
                return;
            }
            if (scrollRailBounds(axis).expanded(4.0f * scale).contains(position.toFloat()))
            {
                draggingScrollRail_ = axis;
                const auto thumb = scrollThumbBounds(axis);
                const float coordinate = axis == 1 ? position.x : position.y;
                const float start = axis == 1 ? thumb.getX() : thumb.getY();
                const float length = axis == 1 ? thumb.getWidth() : thumb.getHeight();
                // A grab inside the thumb keeps its offset; track clicks centre it.
                scrollRailGrabOffset_ = coordinate >= start && coordinate <= start + length
                    ? coordinate - start : length * 0.5f;
                moveScrollRail(position);
                return;
            }
        }
    }
    if (designRect(getWidth(), getHeight(), 775.0f, 14.0f, 31.0f, 34.0f).contains(position))
    {
        showSubdivisionMenu();
        return;
    }
    if (designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 42.0f).contains(position))
    {
        if (event.mods.isPopupMenu())
            return;
        processor_.projectEditor().addLane(processor_.projectState().activePattern);
        updateControlsFromState();
        return;
    }
    if (event.mods.isPopupMenu() && swing_.getBounds().contains(position))
    {
        showSwingReferenceMenu();
        return;
    }
    if (event.mods.isAltDown())
    {
        if (swing_.getBounds().contains(position))
        {
            swing_.setValue(0.0);
            return;
        }
        if (bpm_.isEnabled() && bpm_.getBounds().contains(position))
        {
            bpm_.setValue(120.0);
            return;
        }
        if (defaultVelocity_.getBounds().contains(position))
        {
            auto& velocity = processor_.projectState().global.defaultVelocity;
            const auto defaultVelocity = bsl::GlobalState{}.defaultVelocity;
            if (velocity != defaultVelocity)
            {
                checkpointModuleControl();
                velocity = defaultVelocity;
            }
            // The MIDI display rounds to 102; reset retains the exact model default.
            defaultVelocity_.setValue(defaultVelocity * 127.0, juce::dontSendNotification);
            return;
        }
        std::array<double, 9> defaults {};
        if (module_ == Module::soundCharacter)
            defaults = { 0, 0, 0, 0, 0, 1, 0, 1, 0 };
        else if (module_ == Module::soundEffects)
            defaults = { 0, 1, bsl::defaultFilterResonance, 0, 0, 0, 0, 0, 0 };
        else if (module_ == Module::randomizer)
            defaults = { 0.5, 0.5, 0.4, 1, 1, 1, 1, 0, 0 };
        else
            defaults = { 1, bsl::defaultFilterResonance, 0, 0, 1, 0, 0, 0, 0 };
        for (std::size_t index = 0; index < soundControls_.size(); ++index)
            if (soundControls_[index].isVisible()
                && soundControls_[index].getBounds().contains(position))
            {
                soundControls_[index].setValue(defaults[index], juce::sendNotificationSync);
                return;
            }
        static constexpr std::array<double, 10> effectDefaults {
            0, 1, bsl::defaultFilterResonance, 0, 0, 0, 0, 0, 0, 0.5 };
        for (std::size_t index = 0; index < effectControls_.size(); ++index)
            if (effectControls_[index].isVisible()
                && effectControls_[index].getBounds().contains(position))
            {
                effectControls_[index].setValue(effectDefaults[index], juce::sendNotificationSync);
                return;
            }
        static constexpr std::array<double, 5> masterDefaults { 1, bsl::defaultFilterResonance, 0, 0, 1 };
        for (std::size_t index = 0; index < masterControls_.size(); ++index)
            if (masterControls_[index].getBounds().contains(position))
            {
                masterControls_[index].setValue(masterDefaults[index], juce::sendNotificationSync);
                return;
            }
    }
    if (event.mods.isPopupMenu())
        if (const auto pad = padAt(position))
        {
            processor_.projectState().ui.selectedPad = *pad;
            showPadMenu(*pad);
            return;
        }
    if (const auto lane = laneAt(position))
    {
        auto& state = processor_.projectState();
        state.ui.selectedLane = *lane;
        if (event.mods.isPopupMenu())
        {
            showLaneMenu(*lane);
            return;
        }
        if (event.mods.isShiftDown())
            processor_.projectEditor().resetLane(state.activePattern, *lane);
        else if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::leftKey)
                 || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::rightKey)
                 || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::downKey))
        {
            processor_.projectEditor().checkpoint();
            const bool left = juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::leftKey);
            const bool right = juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::rightKey);
            for (auto& beat : state.patterns[state.activePattern].lanes[*lane].beats)
                if (left || right) beat.rotate(left ? -1 : 1); else beat.mirror();
        }
        else if (!event.mods.isAltDown()
                 && (brush_ == Brush::randomizer || juce::KeyPress::isKeyCurrentlyDown('/')))
            processor_.projectEditor().randomizeLane(
                state.activePattern, *lane,
                static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()));
        else if (brush_ == Brush::slicer && !event.mods.isAltDown())
        {
            processor_.projectEditor().checkpoint();
            auto& beats = state.patterns[state.activePattern].lanes[*lane].beats;
            for (auto& beat : beats)
                beat.setSubdivision(requestedSubdivision_);
        }
        else
        {
            int action = -1;
            for (int control = 0; control < 4; ++control)
                if (laneControlBounds(*lane, control).contains(event.getEventRelativeTo(this).position))
                    action = control;
            if (action < 0)
                return;
            if (action <= 1)
            {
                auto& laneState = state.patterns[state.activePattern].lanes[*lane];
                resetOrToggle(action == 0 ? laneState.solo : laneState.muted, false, event.mods);
            }
            else if (action == 2)
                processor_.projectEditor().removeLane(state.activePattern, *lane);
            else
                processor_.projectEditor().addLane(state.activePattern);
        }
        repaint();
        return;
    }
    if (const auto bar = barAt(position, false).has_value()
                             ? barAt(position, false)
                             : barAt(position, true))
    {
        auto& state = processor_.projectState();
        auto& pattern = state.patterns[state.activePattern];
        state.ui.selectedBar = *bar;
        if (arrangementPadBounds().contains(position) && !event.mods.isPopupMenu() && !event.mods.isShiftDown()
            && !juce::KeyPress::isKeyCurrentlyDown('c') && !juce::KeyPress::isKeyCurrentlyDown('v'))
        {
            const int action = arrangementActionAt(position, *bar);
            if (action >= 0)
            {
                if (action == 1) processor_.projectEditor().addBar(state.activePattern, bsl::BarState::make(4, 4));
                else if (action == 2) processor_.projectEditor().removeBar(state.activePattern, *bar);
                else processor_.projectEditor().duplicateBar(state.activePattern, *bar);
                updateControlsFromState();
                return;
            }
        }
        std::size_t start = 0;
        for (std::size_t index = 0; index < *bar; ++index)
            start += static_cast<std::size_t>(pattern.bars[index].numerator);
        const auto count = static_cast<std::size_t>(pattern.bars[*bar].numerator);
        if (event.mods.isPopupMenu())
            showBarMenu(*bar);
        else if (juce::KeyPress::isKeyCurrentlyDown('c'))
            copyBar(*bar);
        else if (juce::KeyPress::isKeyCurrentlyDown('v') && barClipboard_)
            pasteBar(*bar);
        else if (event.mods.isShiftDown())
        {
            processor_.projectEditor().checkpoint();
            for (auto& lane : pattern.lanes)
                for (std::size_t beat = start; beat < start + count; ++beat)
                    lane.beats[beat].reset();
        }
        else
        {
            draggingBarMeter_ = true;
            barMeterDragChanged_ = false;
            draggingBarIndex_ = *bar;
            dragStartY_ = position.y;
            dragStartBarNumerator_ = pattern.bars[*bar].numerator;
            dragStartBarDenominator_ = pattern.bars[*bar].denominator;
            if (arrangementPadBounds().contains(position))
                draggingBarDenominator_ = position.x >= arrangementItemBounds(*bar).getCentreX();
            else
            {
                const auto header = barHeaderBounds();
                const float quarterWidth = static_cast<float>(header.getWidth())
                                           / static_cast<float>(pattern.durationInQuarterNotes()) * horizontalZoom_;
                float barStart = 0.0f;
                for (std::size_t index = 0; index < *bar; ++index)
                    barStart += static_cast<float>(pattern.bars[index].numerator) * 4.0f / pattern.bars[index].denominator;
                const float barLeft = static_cast<float>(header.getX())
                                      + (barStart - horizontalScroll_) * quarterWidth;
                const float barWidth = static_cast<float>(count) * 4.0f / pattern.bars[*bar].denominator * quarterWidth;
                const float scale = designScale(getWidth(), getHeight());
                const float drawnWidth = barWidth - scale;
                const auto actions = barHeaderActions({ barLeft, static_cast<float>(header.getY()),
                                                       drawnWidth, static_cast<float>(header.getHeight()) }, scale);
                if (actions.contains(position.toFloat()))
                {
                    draggingBarMeter_ = false;
                    lastHit_.reset();
                    const int action = std::clamp(static_cast<int>((position.x - actions.getX()) / (actions.getWidth() / 3.0f)), 0, 2);
                    if (action == 0) processor_.projectEditor().duplicateBar(state.activePattern, *bar);
                    else if (action == 1) processor_.projectEditor().addBar(state.activePattern, bsl::BarState::make(4, 4));
                    else processor_.projectEditor().removeBar(state.activePattern, *bar);
                    updateControlsFromState();
                    return;
                }
                draggingBarDenominator_ = drawnWidth < 110.0f * scale
                    ? position.y >= header.getCentreY()
                    : static_cast<float>(position.x) >= barLeft + barWidth * 0.5f;
            }
        }
        repaint();
        return;
    }
    if (const auto pattern = patternAt(position))
    {
        auto& state = processor_.projectState();
        if (!event.mods.isPopupMenu() && !event.mods.isShiftDown()
            && !juce::KeyPress::isKeyCurrentlyDown('c') && !juce::KeyPress::isKeyCurrentlyDown('v'))
        {
            const int action = arrangementActionAt(position, *pattern);
            if (action >= 0)
            {
                if (action == 1) processor_.projectEditor().addPattern();
                else if (action == 2) processor_.projectEditor().removePattern(*pattern);
                else processor_.projectEditor().duplicatePattern(*pattern);
                updateControlsFromState();
                return;
            }
        }
        if (event.mods.isPopupMenu())
        {
            processor_.selectPatternFromUi(*pattern);
            showPatternMenu(*pattern);
        }
        else if (juce::KeyPress::isKeyCurrentlyDown('c'))
            copyPattern(*pattern);
        else if (juce::KeyPress::isKeyCurrentlyDown('v') && patternClipboard_)
            pastePattern(*pattern);
        else if (event.mods.isShiftDown())
        {
            midiDragPending_ = true;
            midiDragPattern_ = *pattern;
        }
        else
            processor_.selectPatternFromUi(*pattern);
        repaint();
        return;
    }
    // Command on a pie edits its subdivision; the blank grid is a grab surface.
    if (event.mods.isCommandDown() && !event.mods.isPopupMenu()
        && patternBounds().contains(position) && !beatAt(position, true))
    {
        draggingGrid_ = true;
        gridDragStart_ = position;
        gridDragStartHorizontal_ = horizontalScroll_;
        gridDragStartVertical_ = verticalScroll_;
        return;
    }
    if (const auto hit = beatAt(position))
    {
        lastHit_ = hit;
        if (event.mods.isPopupMenu())
            showBeatMenu(*hit);
        else
        {
            auto& beat = processor_.projectState().patterns[processor_.projectState().activePattern]
                             .lanes[hit->lane].beats[hit->beat];
            if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::leftKey)
                || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::rightKey)
                || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::downKey))
            {
                processor_.projectEditor().checkpoint();
                if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::leftKey))
                    beat.rotate(-1);
                else if (juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::rightKey))
                    beat.rotate(1);
                else
                    beat.mirror();
            }
            else if (juce::KeyPress::isKeyCurrentlyDown('c'))
                beatClipboard_ = beat;
            else if (juce::KeyPress::isKeyCurrentlyDown('v'))
            {
                draggingBeatPaste_ = true;
                pasteBeat(*hit);
            }
            else if (event.mods.isCommandDown())
            {
                beatDragCheckpointed_ = false;
                draggingSubdivision_ = true;
                dragStartY_ = position.y;
                dragStartSubdivision_ = beat.slices.size();
            }
            else if (event.mods.isShiftDown())
                applyBrush(*hit, event.mods);
            else if (brush_ == Brush::velocity || event.mods.isAltDown())
            {
                auto& slice = beat.slices[hit->slice];
                beatDragCheckpointed_ = false;
                draggingVelocity_ = true;
                dragStartY_ = position.y;
                dragStartVelocity_ = slice.velocity;
            }
            else if (brush_ == Brush::randomizer || juce::KeyPress::isKeyCurrentlyDown('/'))
            {
                draggingRandomizer_ = true;
                randomizeBeatInStroke(*hit);
            }
            else
                applyBrush(*hit, event.mods);
        }
        repaint();
    }
}

void BeatScholarLinuxEditor::mouseDrag(const juce::MouseEvent& event)
{
    refreshRestoredState();
    const auto position = event.getEventRelativeTo(this).getPosition();
    beatHoverPosition_ = position;
    if (draggingBeatPaste_)
    {
        if (!juce::KeyPress::isKeyCurrentlyDown('v') || juce::KeyPress::isKeyCurrentlyDown('c'))
            draggingBeatPaste_ = false;
        else if (const auto hit = beatAt(position))
        {
            lastHit_ = hit;
            pasteBeat(*hit);
        }
        repaint();
        return;
    }
    if (draggingRandomizer_)
    {
        if (event.mods.isShiftDown() || event.mods.isAltDown() || event.mods.isCommandDown()
            || !(brush_ == Brush::randomizer || juce::KeyPress::isKeyCurrentlyDown('/')))
            draggingRandomizer_ = false;
        else if (const auto hit = beatAt(position))
        {
            lastHit_ = hit;
            randomizeBeatInStroke(*hit);
        }
        repaint();
        return;
    }
    if (draggingZoomRail_ != 0)
    {
        moveZoomRail(position);
        return;
    }
    if (draggingScrollRail_ != 0)
    {
        moveScrollRail(position);
        return;
    }
    if (draggingGrid_)
    {
        const auto area = patternBounds();
        const auto& state = processor_.projectState();
        const auto& pattern = state.patterns[state.activePattern];
        const float quarterWidth = static_cast<float>(area.getWidth())
            / static_cast<float>(pattern.durationInQuarterNotes()) * horizontalZoom_;
        const float cellHeight = static_cast<float>(area.getHeight())
            / static_cast<float>(pattern.lanes.size()) * verticalZoom_;
        horizontalScroll_ = gridDragStartHorizontal_
            - static_cast<float>(position.x - gridDragStart_.x) / quarterWidth;
        verticalScroll_ = gridDragStartVertical_
            - static_cast<float>(position.y - gridDragStart_.y) / cellHeight;
        clampView();
        repaint();
        return;
    }
    if (draggingBarMeter_)
    {
        auto& state = processor_.projectState();
        auto& pattern = state.patterns[state.activePattern];
        if (draggingBarIndex_ >= pattern.bars.size())
            return;
        const int delta = (dragStartY_ - position.y) / 12;
        if (draggingBarDenominator_)
        {
            static constexpr std::array<int, 4> denominators { 2, 4, 8, 16 };
            const auto found = std::find(denominators.begin(), denominators.end(),
                                         dragStartBarDenominator_);
            const int start = found == denominators.end()
                                  ? 1 : static_cast<int>(std::distance(denominators.begin(), found));
            const int denominator = denominators[static_cast<std::size_t>(
                std::clamp(start + delta, 0, static_cast<int>(denominators.size() - 1)))];
            if (pattern.bars[draggingBarIndex_].denominator != denominator)
            {
                processor_.projectEditor().setBarMeter(state.activePattern, draggingBarIndex_,
                                                       pattern.bars[draggingBarIndex_].numerator,
                                                       denominator, !barMeterDragChanged_);
                barMeterDragChanged_ = true;
            }
        }
        else
        {
            const int numerator = std::clamp(dragStartBarNumerator_ + delta, 1, 64);
            if (pattern.bars[draggingBarIndex_].numerator != numerator)
            {
                processor_.projectEditor().setBarMeter(state.activePattern, draggingBarIndex_,
                                                       numerator,
                                                       pattern.bars[draggingBarIndex_].denominator,
                                                       !barMeterDragChanged_);
                barMeterDragChanged_ = true;
            }
        }
        repaint();
        return;
    }
    if (midiDragPending_ && event.getDistanceFromDragStart() > 5)
    {
        midiDragPending_ = false;
        const auto file = writePatternMidi(midiDragPattern_);
        if (file.existsAsFile())
        {
            juce::StringArray files;
            files.add(file.getFullPathName());
            if (juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this))
                status_.setText("Dragging " + file.getFileName(), juce::dontSendNotification);
            else
                status_.setText("External MIDI drag could not start", juce::dontSendNotification);
        }
        else
            status_.setText("Pattern has no MIDI notes", juce::dontSendNotification);
        return;
    }
    if (draggingSubdivision_ && lastHit_)
    {
        auto& state = processor_.projectState();
        auto& beat = state.patterns[state.activePattern].lanes[lastHit_->lane].beats[lastHit_->beat];
        const int delta = (dragStartY_ - position.y) / 12;
        const auto subdivision = static_cast<std::size_t>(std::clamp(
            static_cast<int>(dragStartSubdivision_) + delta,
            1, static_cast<int>(bsl::maximumSubdivision)));
        if (beat.slices.size() != subdivision)
        {
            if (!beatDragCheckpointed_)
            {
                processor_.projectEditor().checkpoint();
                beatDragCheckpointed_ = true;
            }
            beat.setSubdivision(subdivision);
            processor_.markProjectDirty();
        }
        repaint();
        return;
    }
    if (!draggingVelocity_ || !lastHit_)
        return;
    auto& state = processor_.projectState();
    auto& slice = state.patterns[state.activePattern].lanes[lastHit_->lane]
                      .beats[lastHit_->beat].slices[lastHit_->slice];
    const auto velocity = std::clamp(
        dragStartVelocity_ + static_cast<float>(dragStartY_ - position.y) / 140.0f, 0.0f, 1.0f);
    if (slice.velocity != velocity)
    {
        if (!beatDragCheckpointed_)
        {
            processor_.projectEditor().checkpoint();
            beatDragCheckpointed_ = true;
        }
        slice.velocity = velocity;
        slice.velocityAssigned = true;
        processor_.markProjectDirty();
    }
    repaint();
}

void BeatScholarLinuxEditor::mouseUp(const juce::MouseEvent&)
{
    draggingScrollRail_ = 0;
    draggingZoomRail_ = 0;
    draggingGrid_ = false;
    draggingBeatPaste_ = false;
    draggingRandomizer_ = false;
    randomizedBeats_.clear();
    draggingVelocity_ = false;
    draggingSubdivision_ = false;
    beatDragCheckpointed_ = false;
    draggingBarMeter_ = false;
    midiDragPending_ = false;
}

void BeatScholarLinuxEditor::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel)
{
    const auto position = event.getEventRelativeTo(this).getPosition();
    clampView();
    if (arrangementPadBounds().contains(position))
    {
        arrangementScroll_ = std::clamp(arrangementScroll_ - wheel.deltaY * 3.0f,
                                         0.0f, maximumArrangementScroll());
        repaint();
        return;
    }
    if (!patternBounds().contains(position))
        return AudioProcessorEditor::mouseWheelMove(event, wheel);

    const auto& pattern = processor_.projectState().patterns[processor_.projectState().activePattern];
    const float beats = static_cast<float>(pattern.durationInQuarterNotes());
    const float lanes = static_cast<float>(pattern.lanes.size());
    if (event.mods.isCommandDown())
    {
        if (std::abs(wheel.deltaX) > 0.001f)
            horizontalZoom_ = std::clamp(horizontalZoom_ * std::exp(wheel.deltaX * 1.8f), 1.0f, 8.0f);
        else
            verticalZoom_ = std::clamp(verticalZoom_ * std::exp(wheel.deltaY * 1.8f), 1.0f, 8.0f);
    }
    else if (event.mods.isAltDown())
        horizontalZoom_ = std::clamp(horizontalZoom_ * std::exp(wheel.deltaY * 1.8f), 1.0f, 8.0f);
    else
    {
        horizontalScroll_ -= wheel.deltaX * std::max(1.0f, beats / horizontalZoom_);
        verticalScroll_ -= wheel.deltaY * std::max(1.0f, lanes / verticalZoom_);
    }
    horizontalScroll_ = std::clamp(horizontalScroll_, 0.0f,
                                   std::max(0.0f, beats - beats / horizontalZoom_));
    verticalScroll_ = std::clamp(verticalScroll_, 0.0f,
                                 std::max(0.0f, lanes - lanes / verticalZoom_));
    status_.setText("Pattern zoom " + juce::String(horizontalZoom_, 1) + "x / "
                        + juce::String(verticalZoom_, 1) + "x",
                    juce::dontSendNotification);
    repaint();
}

void BeatScholarLinuxEditor::showSwingReferenceMenu()
{
    const auto current = processor_.projectState().global.swingReference;
    juce::PopupMenu menu;
    menu.addItem(1, "1/8 swing reference", true, current == bsl::SwingReference::eighth);
    menu.addItem(2, "1/4 swing reference", true, current == bsl::SwingReference::quarter);
    menu.addItem(3, "1/2 swing reference", true, current == bsl::SwingReference::half);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision()](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision)
            return;
        safe->processor_.projectEditor().checkpoint();
        safe->processor_.projectState().global.swingReference = result == 1
            ? bsl::SwingReference::eighth
            : result == 2 ? bsl::SwingReference::quarter : bsl::SwingReference::half;
        safe->processor_.markProjectDirty();
        safe->status_.setText(result == 1 ? "Swing reference 1/8"
                                         : result == 2 ? "Swing reference 1/4"
                                                       : "Swing reference 1/2",
                              juce::dontSendNotification);
    });
}

void BeatScholarLinuxEditor::showSubdivisionMenu()
{
    juce::PopupMenu menu;
    for (int subdivision = 1; subdivision <= 9; ++subdivision)
        menu.addItem(subdivision, juce::String(subdivision), true,
                     requestedSubdivision_ == static_cast<std::size_t>(subdivision));
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(slicerBrush_),
                       [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this)](int result) {
        if (safe == nullptr || result <= 0)
            return;
        safe->requestedSubdivision_ = static_cast<std::size_t>(result);
        safe->status_.setText("Subdivision " + juce::String(result), juce::dontSendNotification);
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::pasteBeat(const BeatHit& hit)
{
    if (!beatClipboard_)
        return;
    auto& state = processor_.projectState();
    auto& beat = state.patterns[state.activePattern].lanes[hit.lane].beats[hit.beat];
    if (beat == *beatClipboard_)
        return;
    if (!beatDragCheckpointed_)
    {
        processor_.projectEditor().checkpoint();
        beatDragCheckpointed_ = true;
    }
    beat = *beatClipboard_;
    processor_.markProjectDirty();
}

void BeatScholarLinuxEditor::randomizeBeatInStroke(const BeatHit& hit)
{
    // Subdivision may change while painting; membership belongs to the whole pie.
    if (std::any_of(randomizedBeats_.begin(), randomizedBeats_.end(), [&](const auto& previous) {
            return previous.lane == hit.lane && previous.beat == hit.beat;
        }))
        return;
    randomizedBeats_.push_back(hit);
    auto& state = processor_.projectState();
    auto& beat = state.patterns[state.activePattern].lanes[hit.lane].beats[hit.beat];
    auto changed = beat;
    bsl::Randomizer::apply(changed, state.randomizer,
                          static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()),
                          state.global.defaultVelocity);
    if (changed == beat)
        return;
    if (!beatDragCheckpointed_)
    {
        processor_.projectEditor().checkpoint();
        beatDragCheckpointed_ = true;
    }
    beat = std::move(changed);
    processor_.markProjectDirty();
}

void BeatScholarLinuxEditor::applyBrush(const BeatHit& hit, const juce::ModifierKeys& modifiers)
{
    auto& state = processor_.projectState();
    auto& beat = state.patterns[state.activePattern].lanes[hit.lane].beats[hit.beat];
    processor_.projectEditor().checkpoint();
    if (modifiers.isShiftDown())
    {
        const bool enable = std::any_of(beat.slices.begin(), beat.slices.end(), [](const auto& slice) {
            return !slice.enabled;
        });
        for (auto& slice : beat.slices)
        {
            slice.enabled = enable;
            if (enable)
                enableSlice(slice, static_cast<std::uint8_t>(state.ui.selectedPad),
                            state.global.defaultVelocity);
        }
        return;
    }
    if (brush_ == Brush::slicer)
        beat.setSubdivision(requestedSubdivision_);
    const auto selectedSlice = std::min(hit.slice, beat.slices.size() - 1);
    auto& slice = beat.slices[selectedSlice];
    const auto padIndex = static_cast<std::uint8_t>(state.ui.selectedPad);
    if (slice.enabled && slice.padIndex == padIndex)
        slice.enabled = false;
    else
        enableSlice(slice, padIndex, state.global.defaultVelocity);
}

void BeatScholarLinuxEditor::showBeatMenu(const BeatHit& hit)
{
    juce::PopupMenu menu;
    addShortcutMenuItem(menu, 2, "Copy", "C");
    addShortcutMenuItem(menu, 3, "Paste", "V", beatClipboard_.has_value());
    addShortcutMenuItem(menu, 4, "Clear", "Shift");
    addShortcutMenuItem(menu, 1, "Fill", "Shift");
    addShortcutMenuItem(menu, 5, "Mirror", "Down Arrow");
    addShortcutMenuItem(menu, 6, "Shift Right", "Right Arrow");
    addShortcutMenuItem(menu, 7, "Shift Left", "Left Arrow");
    menu.addSeparator();
    for (const auto range : {std::pair{1, 9}, std::pair{10, 19},
                             std::pair{20, 29}, std::pair{30, 45}})
    {
        juce::PopupMenu subdivisions;
        for (int slices = range.first; slices <= range.second; ++slices)
            subdivisions.addItem(100 + slices, juce::String(slices));
        menu.addSubMenu("Slice " + juce::String(range.first) + "-" + juce::String(range.second), subdivisions);
    }
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision(), openedPattern = processor_.projectState().activePattern, hit](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision
            || safe->processor_.projectState().activePattern != openedPattern)
            return;
        auto& state = safe->processor_.projectState();
        // Host recall can replace a pattern while this asynchronous menu is open.
        if (state.activePattern >= state.patterns.size()) return;
        auto& pattern = state.patterns[state.activePattern];
        if (hit.lane >= pattern.lanes.size() || hit.beat >= pattern.lanes[hit.lane].beats.size()) return;
        auto& beat = pattern.lanes[hit.lane].beats[hit.beat];
        if (result == 2)
        {
            safe->beatClipboard_ = beat;
            return;
        }
        safe->processor_.projectEditor().checkpoint();
        if (result == 1 || result == 4)
        {
            for (auto& slice : beat.slices)
            {
                if (result == 1)
                    enableSlice(slice, slice.padIndex, state.global.defaultVelocity);
                else
                    slice.enabled = false;
            }
        }
        else if (result == 3 && safe->beatClipboard_)
            beat = *safe->beatClipboard_;
        else if (result == 5)
            beat.mirror();
        else if (result == 6 || result == 7)
            beat.rotate(result == 6 ? 1 : -1);
        else if (result >= 101 && result <= 145)
            beat.setSubdivision(static_cast<std::size_t>(result - 100));
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::selectPad(std::size_t pad, bool audition)
{
    if (pad >= bsl::padCount)
        return;
    // Pad selection primes Drum/Sounds. A held shortcut still overrides the tool
    // until release, then returns to this newly selected placement mode.
    const auto persistentBrush = momentaryBrushActive_ ? momentaryBrushPrevious_ : brush_;
    if (persistentBrush != Brush::drum)
        previousBrush_ = persistentBrush;
    if (momentaryBrushActive_)
    {
        momentaryBrushPrevious_ = Brush::drum;
        momentaryModulePrevious_ = Module::soundCharacter;
        updateMomentaryBrush();
    }
    else
    {
        brush_ = Brush::drum;
        module_ = Module::soundCharacter;
        processor_.projectState().ui.randomizerVisible = false;
    }
    processor_.projectState().ui.selectedPad = pad;
    if (audition && (processor_.projectState().global.auditionDuringPlayback
                     || !processor_.isTransportPlaying()))
        processor_.triggerPad(pad);
    updateControlsFromState();
}

void BeatScholarLinuxEditor::updateControlsFromState()
{
    cancelPatternInteraction();
    clampView();
    configureModuleControls();
}

void BeatScholarLinuxEditor::cancelNumericEntry()
{
    numericEntryControl_ = nullptr;
    numericEntryBar_.reset();
    numericEntry_.setVisible(false);
}

void BeatScholarLinuxEditor::cancelPatternInteraction()
{
    if (numericEntryBar_) cancelNumericEntry();
    draggingScrollRail_ = 0;
    draggingZoomRail_ = 0;
    draggingGrid_ = false;
    lastHit_.reset();
    beatHoverPosition_.reset();
    draggingBeatPaste_ = false;
    draggingRandomizer_ = false;
    randomizedBeats_.clear();
    draggingVelocity_ = false;
    draggingSubdivision_ = false;
    beatDragCheckpointed_ = false;
    draggingBarMeter_ = false;
    midiDragPending_ = false;
}

void BeatScholarLinuxEditor::refreshRestoredState(juce::Slider* editedControl)
{
    const auto revision = processor_.restoredStateRevision();
    if (revision == observedStateRevision_) return;
    // A control edit can precede the next timer tick. Refresh the other values
    // first, then retain this edit so it cannot overwrite the newly loaded kit.
    const double editedValue = editedControl != nullptr ? editedControl->getValue() : 0.0;
    observedStateRevision_ = revision;
    cancelPatternInteraction();
    if (momentaryBrushActive_)
    {
        brush_ = momentaryBrushPrevious_;
        module_ = momentaryModulePrevious_;
        momentaryBrushActive_ = false;
    }
    if (processor_.projectState().ui.randomizerVisible)
    {
        module_ = Module::randomizer;
        brush_ = Brush::randomizer;
    }
    else
    {
        if (module_ == Module::randomizer) module_ = Module::soundCharacter;
        if (brush_ == Brush::randomizer) brush_ = Brush::drum;
    }
    // The loaded panel becomes the base; physically held shortcuts still apply.
    keyStateChanged(false);
    configureModuleControls();
    if (editedControl != nullptr)
        editedControl->setValue(editedValue, juce::dontSendNotification);
}

void BeatScholarLinuxEditor::syncBrushButtons()
{
    const auto selectedPad = std::min(processor_.projectState().ui.selectedPad, bsl::padCount - 1);
    drumBrush_.setColour(juce::TextButton::buttonOnColourId, padColours_[selectedPad]);
    drumBrush_.setToggleState(brush_ == Brush::drum, juce::dontSendNotification);
    slicerBrush_.setToggleState(brush_ == Brush::slicer, juce::dontSendNotification);
    velocityBrush_.setToggleState(brush_ == Brush::velocity, juce::dontSendNotification);
    randomizerBrush_.setToggleState(brush_ == Brush::randomizer, juce::dontSendNotification);
}

void BeatScholarLinuxEditor::updateEffectDragValue()
{
    if (effectDragIndex_ < 0) return;
    const auto& control = effectControls_[static_cast<std::size_t>(effectDragIndex_)];
    const float scale = designScale(getWidth(), getHeight());
    // Samples & Sounds 05:22-05:27: fixed box above the initial drag point.
    const auto centreX = static_cast<float>(control.getX())
                       + control.dragAnchorX * static_cast<float>(control.getWidth());
    effectDragValue_.setBounds(juce::roundToInt(centreX - 40.0f * scale),
        control.getY() - juce::roundToInt(12.0f * scale) - juce::roundToInt(50.0f * scale),
        juce::roundToInt(80.0f * scale), juce::roundToInt(50.0f * scale));
    effectDragValue_.setFont(bsl::ui::font(14.0f * scale, true));
    effectDragValue_.setText(effectDragIndex_ == 8
        ? juce::String(juce::roundToInt(control.getValue() * 100.0)) + "%"
        : juce::String(control.getValue(), 2), juce::dontSendNotification);
    effectDragValue_.setVisible(true);
}

void BeatScholarLinuxEditor::configureModuleControls()
{
    effectDragIndex_ = -1;
    effectDragValue_.setVisible(false);
    syncBrushButtons();
    updatingControls_ = true;
    auto& state = processor_.projectState();
    lookAndFeel_.setDarkMode(state.ui.darkMode);
    numericEntry_.setColour(juce::TextEditor::backgroundColourId,
                            juce::Colour(lookAndFeel_.palette().panelRaised));
    state.ui.selectedPad = std::min(state.ui.selectedPad, bsl::padCount - 1);
    const auto& pad = state.kit.pads[state.ui.selectedPad];
    const bool randomizerVisible = module_ == Module::randomizer;
    const bool custom = !pad.customSampleLayered && !pad.customSamples[0].name.empty();
    const std::array<bool, 4> moduleEnabled { state.randomizer.noteSelectionEnabled,
        state.randomizer.slicingEnabled, state.randomizer.triggeringEnabled, state.randomizer.velocityEnabled };
    for (std::size_t index = 0; index < randomModuleToggles_.size(); ++index)
    {
        randomModuleToggles_[index].setVisible(randomizerVisible);
        randomModuleToggles_[index].setToggleState(moduleEnabled[index], juce::dontSendNotification);
    }
    randomFillEmpty_.setVisible(randomizerVisible);
    randomFillEmpty_.setToggleState(state.randomizer.fillEmptyOnly, juce::dontSendNotification);
    std::array<double, 9> values {};
    std::array<double, 9> defaults {};
    std::array<const char*, 9> labels {};
    std::array<const char*, 9> suffixes {};
    std::array<std::array<double, 3>, 9> ranges {};
    std::size_t visible = 8;
    if (!randomizerVisible)
    {
        values = { pad.attackSeconds > 0.0f ? pad.attackSeconds : pad.attackNormalized,
                   pad.damp, pad.pitchSemitones, pad.fineCents,
                   pad.layerMix, pad.gain, pad.pan, pad.oneShot ? 1.0 : 0.0, 0 };
        defaults = { 0, 0, 0, 0, 0, 1, 0, 1, 0 };
        labels = { "Attack", "Damp", "Pitch", "Fine", "Mix", "Level", "L             R", "One Shot", "" };
        suffixes = { " s", "", " st", " ct", "", "", "", "", "" };
        ranges = {{{ 0, 2, 0.001 }, { 0, 1, 0.001 }, { -12, 12, 1.0 }, { -100, 100, 0.1 },
                   { 0, 1, 0.001 }, { 0, 2, 0.001 }, { -1, 1, 0.001 }, { 0, 1, 1 },
                   { 0, 1, 0.001 }}};
        visible = 7;
        soundTab_.setButtonText(juce::String(pad.name));
    }
    else
    {
        const auto& random = state.randomizer;
        values = { random.triggerChance, random.spacing, random.minimumVelocity, random.maximumVelocity,
                   random.slicingEnabled ? 1.0 : 0.0, random.noteSelectionEnabled ? 1.0 : 0.0,
                   random.velocityEnabled ? 1.0 : 0.0, random.fillEmptyOnly ? 1.0 : 0.0, 0 };
        defaults = { 0.5, 0.5, 0.4, 1, 1, 1, 1, 0, 0 };
        labels = { "Trigger Chance", "Spacing", "Min", "Max", "", "", "", "Fill Empty", "" };
        for (std::size_t index = 0; index < 4; ++index)
            ranges[index] = { 0, 1, index >= 2 ? 0.0 : 0.001 };
        for (std::size_t index = 4; index < 8; ++index)
            ranges[index] = { 0, 1, 1 };
        ranges[8] = { 0, 1, 0.001 };
        visible = 8;
        randomizerTab_.setButtonText(currentRandomizerFile() != juce::File{}
                                         ? currentRandomizerFile().getFileNameWithoutExtension()
                                         : "Init Preset");
    }
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const bool visibleControl = index < visible && !(randomizerVisible && index >= 4 && index <= 6)
                                    && !(!randomizerVisible && custom && index == 4);
        soundControls_[index].setVisible(visibleControl && !(randomizerVisible && index == 7));
        soundLabels_[index].setVisible(visibleControl && (randomizerVisible || index != 4));
        soundControls_[index].setRange(ranges[index][0], ranges[index][1], ranges[index][2]);
        soundControls_[index].setTextValueSuffix(suffixes[index]);
        soundControls_[index].textFromValueFunction = nullptr;
        soundControls_[index].valueFromTextFunction = nullptr;
        if (randomizerVisible && (index == 0 || index == 2 || index == 3))
        {
            const double scale = index == 0 ? 100.0 : 127.0;
            soundControls_[index].textFromValueFunction = [scale](double value) {
                return juce::String(static_cast<int>(std::lround(value * scale)));
            };
            soundControls_[index].valueFromTextFunction = [scale](const juce::String& text) {
                return text.getDoubleValue() / scale;
            };
            soundControls_[index].setTextValueSuffix(index == 0 ? "%" : "");
        }
        soundControls_[index].setDoubleClickReturnValue(false, defaults[index]);
        soundLabels_[index].setText(labels[index], juce::dontSendNotification);
        soundControls_[index].setValue(values[index], juce::dontSendNotification);
        soundControls_[index].setComponentID(randomizerVisible
                                                 ? "random." + juce::String(static_cast<int>(index))
                                                 : "sound." + juce::String(static_cast<int>(index)));
        soundControls_[index].setSliderStyle(!randomizerVisible && (index == 4 || index == 5)
                                                 ? juce::Slider::LinearVertical
                                             : (!randomizerVisible && index == 6) || (randomizerVisible && index == 1)
                                                 ? juce::Slider::LinearHorizontal
                                                 : juce::Slider::RotaryHorizontalVerticalDrag);
        soundControls_[index].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    }
    if (!randomizerVisible)
    {
        const bool hasTwoFactoryLayers = state.ui.selectedPad <= 1;
        soundControls_[4].setEnabled(hasTwoFactoryLayers);
        soundLabels_[4].setEnabled(hasTwoFactoryLayers);
        soundControls_[4].setTooltip(hasTwoFactoryLayers
                                         ? "Blend the two Kick/Snare factory layers"
                                         : "Layer mixing is available only for Kick and Snare");
        layerTwo_.setEnabled(hasTwoFactoryLayers);
    }
    else
        soundControls_[4].setEnabled(true);

    const std::array<const char*, 10> effectNames {
        "HPF", "Cutoff", "Reso", "Drive", "Comp", "Crush", "Reverb", "Room", "Dist", "Gain"
    };
    const std::array<double, 10> effectValues {
        pad.highPass, pad.cutoff, pad.resonance, pad.driveGainDb, pad.compression,
        pad.crush, pad.reverb, pad.room, pad.distortionMix, pad.distortionDrive
    };
    const std::array<double, 10> effectDefaults {
        0, 1, bsl::defaultFilterResonance, 0, 0, 0, 0, 0, 0, 0.5
    };
    for (std::size_t index = 0; index < effectControls_.size(); ++index)
    {
        effectControls_[index].setEnabled(index < 6 || state.global.auxEnabled);
        effectLabels_[index].setEnabled(index < 6 || state.global.auxEnabled);
        effectControls_[index].setVisible(!randomizerVisible);
        effectLabels_[index].setVisible(!randomizerVisible);
        effectLabels_[index].setText(effectNames[index], juce::dontSendNotification);
        effectControls_[index].setTitle(effectNames[index]);
        effectControls_[index].setComponentID("effect." + juce::String(static_cast<int>(index)));
        effectControls_[index].setDoubleClickReturnValue(false, effectDefaults[index]);
        effectControls_[index].setValue(effectValues[index], juce::dontSendNotification);
        effectControls_[index].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        effectControls_[index].setSliderStyle(index == 9 ? juce::Slider::LinearVertical
                                                        : juce::Slider::RotaryHorizontalVerticalDrag);
        if (index == 9)
            effectControls_[index].setTooltip("Parallel distortion gain for all drum sends");
    }

    const std::array<const char*, 5> masterNames { "Cutoff", "Reso", "Crush", "Comp", "Master" };
    const std::array<double, 5> masterValues { state.master.cutoff, state.master.resonance,
                                               state.master.crush, state.master.compression,
                                               state.master.gain };
    const std::array<double, 5> masterDefaults { 1, bsl::defaultFilterResonance, 0, 0, 1 };
    for (std::size_t index = 0; index < masterControls_.size(); ++index)
    {
        masterControls_[index].setEnabled(index == 4 || state.global.masterEnabled);
        masterLabels_[index].setEnabled(index == 4 || state.global.masterEnabled);
        masterLabels_[index].setText(masterNames[index], juce::dontSendNotification);
        masterControls_[index].setComponentID("master." + juce::String(static_cast<int>(index)));
        masterControls_[index].setDoubleClickReturnValue(false, masterDefaults[index]);
        masterControls_[index].setValue(masterValues[index], juce::dontSendNotification);
        masterControls_[index].setSliderStyle(index == 4 ? juce::Slider::LinearVertical
                                                         : juce::Slider::RotaryHorizontalVerticalDrag);
        masterControls_[index].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    }

    const auto layerName = [](const auto& id, const char* fallback) {
        return id.empty() ? juce::String(fallback)
                          : factorySampleDisplayName(juce::File(juce::String(id)));
    };
    layerOne_.setButtonText(!pad.customSamples[0].name.empty()
                                ? juce::String(pad.customSamples[0].name)
                                : layerName(pad.factorySampleIds[0], "Layer 1"));
    layerOne_.setComponentID(custom ? "sample.custom" : "sample.layer");
    layerTwo_.setButtonText(!pad.customSamples[1].name.empty()
                                ? juce::String(pad.customSamples[1].name)
                                : layerName(pad.factorySampleIds[1], "Layer 2"));
    padSolo_.setToggleState(pad.solo, juce::dontSendNotification);
    padMute_.setToggleState(pad.muted, juce::dontSendNotification);
    oneShot_.setToggleState(pad.oneShot, juce::dontSendNotification);
    mono_.setToggleState(pad.mono, juce::dontSendNotification);
    loadSample_.setVisible(!randomizerVisible);
    clearSample_.setVisible(!randomizerVisible
                            && (!pad.customSamples[0].empty() || !pad.customSamples[1].empty()));
    layerOne_.setVisible(!randomizerVisible);
    layerTwo_.setVisible(!randomizerVisible && !custom && state.ui.selectedPad <= 1);
    padSolo_.setVisible(!randomizerVisible);
    padMute_.setVisible(!randomizerVisible);
    oneShot_.setVisible(!randomizerVisible);
    mono_.setVisible(!randomizerVisible);
    soundTab_.setVisible(!randomizerVisible);
    randomizerTab_.setVisible(randomizerVisible);
    randomClearPads_.setVisible(randomizerVisible);
    randomAllPads_.setVisible(randomizerVisible);
    randomClearSlices_.setVisible(randomizerVisible);
    randomAllSlices_.setVisible(randomizerVisible);
    randomSlicePrevious_.setVisible(randomizerVisible);
    randomSliceNext_.setVisible(randomizerVisible);
    for (std::size_t index = 0; index < randomSliceButtons_.size(); ++index)
    {
        const auto value = static_cast<std::size_t>(randomSlicePage_ * 9 + static_cast<int>(index) + 1);
        randomSliceButtons_[index].setVisible(randomizerVisible);
        randomSliceButtons_[index].setButtonText(juce::String(static_cast<int>(value)));
        randomSliceButtons_[index].setToggleState(
            std::find(state.randomizer.selectedSubdivisions.begin(),
                      state.randomizer.selectedSubdivisions.end(), value)
                != state.randomizer.selectedSubdivisions.end(),
            juce::dontSendNotification);
    }
    for (std::size_t index = 0; index < pads_.size(); ++index)
    {
        const bool selected = module_ == Module::randomizer
            ? std::find(state.randomizer.selectedPads.begin(), state.randomizer.selectedPads.end(),
                        static_cast<std::uint8_t>(index)) != state.randomizer.selectedPads.end()
            : index == state.ui.selectedPad;
        pads_[index].setToggleState(selected, juce::dontSendNotification);
        const auto& padState = state.kit.pads[index];
        juce::String tooltip = juce::String(padState.name) + " — " + midiNoteLabel(padState.midiNote, state.ui);
        if (!padState.customSamples[0].name.empty() || !padState.customSamples[1].name.empty())
        {
            tooltip << " — custom: ";
            if (!padState.customSamples[0].name.empty())
                tooltip << padState.customSamples[0].name;
            if (!padState.customSamples[1].name.empty())
                tooltip << (padState.customSamples[0].name.empty() ? "" : " + ")
                        << padState.customSamples[1].name;
        }
        else if (!padState.factorySampleIds[0].empty())
            tooltip << " — " << factorySampleDisplayName(
                juce::File(juce::String(padState.factorySampleIds[0])));
        pads_[index].setTooltip(tooltip);
    }
    barsPatterns_.setButtonText(state.ui.patternMode ? "Bars" : "Patterns");
    defaultVelocity_.setValue(state.global.defaultVelocity * 127.0,
                              juce::dontSendNotification);
    auditionDuringPlayback_.setToggleState(state.global.auditionDuringPlayback,
                                           juce::dontSendNotification);
    kitLocked_.setToggleState(state.global.kitLocked, juce::dontSendNotification);
    kitMenu_.setButtonText(currentKitFile() != juce::File{}
                               ? currentKitFile().getFileNameWithoutExtension() : "Init");
    preset_.setButtonText(currentPresetFile() != juce::File{}
                              ? currentPresetFile().getFileNameWithoutExtension() : "Init");
    kitPrevious_.setVisible(module_ != Module::randomizer);
    kitNext_.setVisible(module_ != Module::randomizer);
    randomizerPrevious_.setVisible(module_ == Module::randomizer);
    randomizerNext_.setVisible(module_ == Module::randomizer);
    resized();
    updatingControls_ = false;
}

void BeatScholarLinuxEditor::checkpointModuleControl(bool masterGain)
{
    if (moduleControlGesture_ && moduleControlCheckpointed_) return;
    if (masterGain)
        processor_.projectState().master.gain =
            processor_.parameters().getRawParameterValue("masterGain")->load();
    processor_.projectEditor().checkpoint(masterGain ? bsl::HistoryParameters::masterGain
                                                   : bsl::HistoryParameters::none);
    moduleControlCheckpointed_ = true;
}

void BeatScholarLinuxEditor::restoreHistory(bool redo)
{
    if (processor_.restoreProjectHistory(redo))
        updateControlsFromState();
}


void BeatScholarLinuxEditor::applyModuleControls(juce::Slider& changedControl)
{
    if (updatingControls_)
        return;
    auto& state = processor_.projectState();
    auto& pad = state.kit.pads[state.ui.selectedPad];
    // Sliders may round their displayed value. Only the edited control may
    // write back, so another gesture cannot alter restored parameter precision.
    const auto applyValue = [&changedControl](juce::Slider& control, float& value) {
        if (&changedControl == &control)
            value = static_cast<float>(control.getValue());
    };
    if (module_ != Module::randomizer)
    {
        if (&changedControl == &soundControls_[0])
        {
            pad.attackSeconds = static_cast<float>(soundControls_[0].getValue());
            pad.attackNormalized = std::clamp(pad.attackSeconds, 0.0f, 1.0f);
        }
        applyValue(soundControls_[1], pad.damp);
        applyValue(soundControls_[2], pad.pitchSemitones);
        applyValue(soundControls_[3], pad.fineCents);
        applyValue(soundControls_[4], pad.layerMix);
        applyValue(soundControls_[5], pad.gain);
        applyValue(soundControls_[6], pad.pan);
        if (&changedControl == &soundControls_[7])
            pad.oneShot = soundControls_[7].getValue() >= 0.5;
        applyValue(effectControls_[0], pad.highPass);
        applyValue(effectControls_[1], pad.cutoff);
        applyValue(effectControls_[2], pad.resonance);
        applyValue(effectControls_[3], pad.driveGainDb);
        applyValue(effectControls_[4], pad.compression);
        applyValue(effectControls_[5], pad.crush);
        applyValue(effectControls_[6], pad.reverb);
        applyValue(effectControls_[7], pad.room);
        applyValue(effectControls_[8], pad.distortionMix);
    }
    else
    {
        auto& random = state.randomizer;
        applyValue(soundControls_[0], random.triggerChance);
        applyValue(soundControls_[1], random.spacing);
        if (&changedControl == &soundControls_[2] || &changedControl == &soundControls_[3])
        {
            // The callback keeps these paired sliders ordered during a gesture.
            random.minimumVelocity = static_cast<float>(soundControls_[2].getValue());
            random.maximumVelocity = static_cast<float>(soundControls_[3].getValue());
        }
        if (&changedControl == &soundControls_[4])
            random.slicingEnabled = soundControls_[4].getValue() >= 0.5;
        if (&changedControl == &soundControls_[5])
            random.noteSelectionEnabled = soundControls_[5].getValue() >= 0.5;
        if (&changedControl == &soundControls_[6])
            random.velocityEnabled = soundControls_[6].getValue() >= 0.5;
        if (&changedControl == &soundControls_[7])
            random.fillEmptyOnly = soundControls_[7].getValue() >= 0.5;
    }
    applyValue(masterControls_[0], state.master.cutoff);
    applyValue(masterControls_[1], state.master.resonance);
    applyValue(masterControls_[2], state.master.crush);
    applyValue(masterControls_[3], state.master.compression);
    if (&changedControl == &masterControls_[4])
    {
        state.master.gain = static_cast<float>(masterControls_[4].getValue());
        if (auto* master = processor_.parameters().getParameter("masterGain"))
        {
            if (!masterGainGesture_) master->beginChangeGesture();
            master->setValueNotifyingHost(master->convertTo0to1(state.master.gain));
            if (!masterGainGesture_) master->endChangeGesture();
        }
    }
    processor_.markProjectDirty();
}

void BeatScholarLinuxEditor::showLaneMenu(std::size_t laneIndex)
{
    auto& state = processor_.projectState();
    auto& pattern = state.patterns[state.activePattern];
    if (laneIndex >= pattern.lanes.size())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, "Solo", true, pattern.lanes[laneIndex].solo);
    menu.addItem(2, "Mute", true, pattern.lanes[laneIndex].muted);
    menu.addItem(3, "Reset lane");
    menu.addItem(6, "Randomize lane");
    menu.addSeparator();
    menu.addItem(4, "Add lane", pattern.lanes.size() < bsl::maximumLaneCount);
    menu.addItem(5, "Remove lane", pattern.lanes.size() > 1);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision(), openedPattern = processor_.projectState().activePattern, laneIndex](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision
            || safe->processor_.projectState().activePattern != openedPattern)
            return;
        auto& currentState = safe->processor_.projectState();
        auto& currentPattern = currentState.patterns[currentState.activePattern];
        if (laneIndex >= currentPattern.lanes.size())
            return;
        if (result == 1 || result == 2)
        {
            safe->processor_.projectEditor().checkpoint();
            if (result == 1)
                currentPattern.lanes[laneIndex].solo = !currentPattern.lanes[laneIndex].solo;
            else
                currentPattern.lanes[laneIndex].muted = !currentPattern.lanes[laneIndex].muted;
        }
        else if (result == 3)
            safe->processor_.projectEditor().resetLane(currentState.activePattern, laneIndex);
        else if (result == 4)
            safe->processor_.projectEditor().addLane(currentState.activePattern);
        else if (result == 5)
            safe->processor_.projectEditor().removeLane(currentState.activePattern, laneIndex);
        else if (result == 6)
            safe->processor_.projectEditor().randomizeLane(
                currentState.activePattern, laneIndex,
                static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()));
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::copyBar(std::size_t barIndex)
{
    const auto& state = processor_.projectState();
    const auto& pattern = state.patterns[state.activePattern];
    if (barIndex >= pattern.bars.size())
        return;
    std::size_t start = 0;
    for (std::size_t index = 0; index < barIndex; ++index)
        start += static_cast<std::size_t>(pattern.bars[index].numerator);
    const auto count = static_cast<std::size_t>(pattern.bars[barIndex].numerator);
    BarClipboard copy;
    copy.meter = pattern.bars[barIndex];
    for (const auto& lane : pattern.lanes)
        copy.lanes.emplace_back(lane.beats.begin() + static_cast<std::ptrdiff_t>(start),
                                lane.beats.begin() + static_cast<std::ptrdiff_t>(start + count));
    barClipboard_ = std::move(copy);
}

void BeatScholarLinuxEditor::pasteBar(std::size_t barIndex)
{
    auto& state = processor_.projectState();
    if (!barClipboard_ || !processor_.projectEditor().setBarMeter(state.activePattern, barIndex,
            barClipboard_->meter.numerator, barClipboard_->meter.denominator))
        return;
    auto& pattern = state.patterns[state.activePattern];
    std::size_t start = 0;
    for (std::size_t index = 0; index < barIndex; ++index)
        start += static_cast<std::size_t>(pattern.bars[index].numerator);
    const auto copiedLanes = std::min(pattern.lanes.size(), barClipboard_->lanes.size());
    for (std::size_t lane = 0; lane < copiedLanes; ++lane)
        std::copy(barClipboard_->lanes[lane].begin(), barClipboard_->lanes[lane].end(),
                  pattern.lanes[lane].beats.begin() + static_cast<std::ptrdiff_t>(start));
    const auto count = static_cast<std::size_t>(pattern.bars[barIndex].numerator);
    for (std::size_t lane = copiedLanes; lane < pattern.lanes.size(); ++lane)
        for (std::size_t beat = start; beat < start + count; ++beat)
            pattern.lanes[lane].beats[beat].reset();
}

void BeatScholarLinuxEditor::copyPattern(std::size_t patternIndex)
{
    const auto& state = processor_.projectState();
    if (patternIndex < state.patterns.size())
        patternClipboard_ = state.patterns[patternIndex];
}

void BeatScholarLinuxEditor::pastePattern(std::size_t patternIndex)
{
    auto& state = processor_.projectState();
    if (!patternClipboard_ || patternIndex >= state.patterns.size())
        return;
    processor_.projectEditor().checkpoint();
    const auto name = state.patterns[patternIndex].name;
    const auto note = state.patterns[patternIndex].triggerNote;
    const auto identity = state.patterns[patternIndex].runtimeId;
    state.patterns[patternIndex] = *patternClipboard_;
    state.patterns[patternIndex].runtimeId = identity;
    state.patterns[patternIndex].name = name;
    state.patterns[patternIndex].triggerNote = note;
}

void BeatScholarLinuxEditor::showBarMenu(std::size_t barIndex)
{
    static constexpr std::array<std::pair<int, int>, 8> meters {{
        { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 }
    }};
    juce::PopupMenu meterMenu;
    for (std::size_t index = 0; index < meters.size(); ++index)
        meterMenu.addItem(100 + static_cast<int>(index), juce::String(meters[index].first)
                                                        + "/" + juce::String(meters[index].second));
    const auto& state = processor_.projectState();
    juce::PopupMenu menu;
    addShortcutMenuItem(menu, 4, "Copy", "C");
    addShortcutMenuItem(menu, 5, "Paste", "V", barClipboard_.has_value());
    addShortcutMenuItem(menu, 1, "Clear", "Shift");
    menu.addItem(2, "Duplicate");
    menu.addItem(3, "Delete", state.patterns[state.activePattern].bars.size() > 1);
    menu.addItem(6, "Insert Here");
    menu.addItem(7, "Append");
    menu.addSeparator();
    menu.addSubMenu("Meter", meterMenu);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision(), openedPattern = processor_.projectState().activePattern,
                            barIndex](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision
            || safe->processor_.projectState().activePattern != openedPattern)
            return;
        auto& state = safe->processor_.projectState();
        if (barIndex >= state.patterns[state.activePattern].bars.size())
            return;
        if (result >= 100 && result < 108)
        {
            const auto meter = meters[static_cast<std::size_t>(result - 100)];
            safe->processor_.projectEditor().setBarMeter(state.activePattern, barIndex,
                                                         meter.first, meter.second);
        }
        else if (result == 1)
        {
            auto& pattern = state.patterns[state.activePattern];
            std::size_t start = 0;
            for (std::size_t index = 0; index < barIndex; ++index)
                start += static_cast<std::size_t>(pattern.bars[index].numerator);
            const auto count = static_cast<std::size_t>(pattern.bars[barIndex].numerator);
            safe->processor_.projectEditor().checkpoint();
            for (auto& lane : pattern.lanes)
                for (std::size_t beat = start; beat < start + count; ++beat)
                    lane.beats[beat].reset();
        }
        else if (result == 2)
            safe->processor_.projectEditor().duplicateBar(state.activePattern, barIndex);
        else if (result == 3)
            safe->processor_.projectEditor().removeBar(state.activePattern, barIndex);
        else if (result == 4)
            safe->copyBar(barIndex);
        else if (result == 5)
            safe->pasteBar(barIndex);
        else if (result == 6)
            safe->processor_.projectEditor().insertBar(state.activePattern, barIndex, bsl::BarState::make(4, 4));
        else if (result == 7)
            safe->processor_.projectEditor().addBar(state.activePattern, bsl::BarState::make(4, 4));
        safe->updateControlsFromState();
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::showPatternQuantizationMenu()
{
    const auto& state = processor_.projectState();
    const auto index = state.activePattern;
    if (index >= state.patterns.size()) return;
    auto menu = quantizationMenu(state.patterns[index].quantization);
    menu.setLookAndFeel(&lookAndFeel_);
    const auto bounds = designRect(getWidth(), getHeight(), 1161.0f, 88.0f, 99.0f, 18.0f);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(bounds)),
        [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
         revision = processor_.restoredStateRevision(), index,
         identity = state.patterns[index].runtimeId](int result) {
            if (safe == nullptr || result < 10 || result > 16
                || safe->processor_.restoredStateRevision() != revision) return;
            auto& patterns = safe->processor_.projectState().patterns;
            if (index >= patterns.size() || patterns[index].runtimeId != identity) return;
            const auto value = static_cast<bsl::PatternQuantization>(result - 10);
            if (patterns[index].quantization == value) return;
            safe->processor_.projectEditor().checkpoint();
            patterns[index].quantization = value;
            safe->updateControlsFromState();
            safe->repaint();
        });
}

void BeatScholarLinuxEditor::showPatternMenu(std::size_t patternIndex)
{
    if (patternIndex >= processor_.projectState().patterns.size())
        return;
    const auto patternCount = processor_.projectState().patterns.size();
    juce::PopupMenu menu;
    addShortcutMenuItem(menu, 4, "Export MIDI", "Shift + DRAG");
    menu.addSeparator();
    menu.addItem(1, "Rename");
    addShortcutMenuItem(menu, 5, "Copy", "C");
    addShortcutMenuItem(menu, 6, "Paste", "V", patternClipboard_.has_value());
    menu.addItem(7, "Insert", patternCount < 116);
    menu.addItem(8, "Delete", patternCount > 1);
    menu.addItem(9, "Duplicate", patternCount < 116);
    menu.addSeparator();
    menu.addItem(2, "Trigger note down");
    menu.addItem(3, "Trigger note up");
    menu.addSubMenu("Trigger quantization",
                    quantizationMenu(processor_.projectState().patterns[patternIndex].quantization));
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision(),
                            patternIndex](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision)
            return;
        auto& state = safe->processor_.projectState();
        if (patternIndex >= state.patterns.size())
            return;
        if (result == 1)
        {
            auto* dialog = new juce::AlertWindow("Rename pattern", "Enter a new pattern name",
                                                 juce::MessageBoxIconType::NoIcon);
            dialog->addTextEditor("name", juce::String(state.patterns[patternIndex].name));
            dialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [safe, patternIndex, dialog, revision](int accepted) {
                    if (safe != nullptr && accepted == 1
                        && safe->processor_.restoredStateRevision() == revision)
                    {
                        auto& dialogState = safe->processor_.projectState();
                        if (patternIndex < dialogState.patterns.size())
                        {
                            safe->processor_.projectEditor().checkpoint();
                            dialogState.patterns[patternIndex].name =
                                dialog->getTextEditorContents("name").trim().toStdString();
                            if (dialogState.patterns[patternIndex].name.empty())
                                dialogState.patterns[patternIndex].name = "Pattern";
                        }
                    }
                }), true);
        }
        else if (result == 2 || result == 3)
        {
            safe->processor_.projectEditor().shiftPatternTriggerNote(patternIndex, result == 3 ? 1 : -1);
        }
        else if (result == 4)
        {
            const auto file = safe->writePatternMidi(patternIndex);
            safe->status_.setText(file.existsAsFile() ? "MIDI exported to " + file.getFullPathName()
                                                      : "Pattern has no MIDI notes",
                                  juce::dontSendNotification);
        }
        else if (result == 5)
            safe->copyPattern(patternIndex);
        else if (result == 6)
            safe->pastePattern(patternIndex);
        else if (result == 7)
            safe->processor_.projectEditor().insertPattern(patternIndex);
        else if (result == 8)
            safe->processor_.projectEditor().removePattern(patternIndex);
        else if (result == 9)
            safe->processor_.projectEditor().duplicatePattern(patternIndex);
        else if (result >= 10 && result <= 16)
        {
            if (state.patterns[patternIndex].quantization
                == static_cast<bsl::PatternQuantization>(result - 10)) return;
            safe->processor_.projectEditor().checkpoint();
            state.patterns[patternIndex].quantization =
                static_cast<bsl::PatternQuantization>(result - 10);
        }
        safe->updateControlsFromState();
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::showPadMenu(std::size_t padIndex)
{
    if (padIndex >= bsl::padCount)
        return;
    const auto& pad = processor_.projectState().kit.pads[padIndex];
    juce::PopupMenu menu;
    menu.addItem(1, "Load custom sample...");
    menu.addItem(2, "Remove custom sample",
                 !pad.customSamples[0].base64.empty() || !pad.customSamples[1].base64.empty());
    menu.addItem(8, "Select factory sample" + juce::String(padIndex <= 1 ? " layer 1..." : "..."));
    menu.addItem(9, "Select factory sample layer 2...", padIndex <= 1);
    menu.addSeparator();
    menu.addItem(3, "MIDI note down");
    menu.addItem(4, "MIDI note up");
    menu.addItem(5, "Reset MIDI note");
    menu.addSeparator();
    menu.addItem(6, "Solo", true, pad.solo);
    menu.addItem(7, "Mute", true, pad.muted);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision(), padIndex](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision)
            return;
        if (result == 8 || result == 9)
        {
            safe->showFactorySampleMenu(padIndex, result == 9 ? 1u : 0u);
            return;
        }
        if (result == 1)
        {
            safe->chooseCustomSample(padIndex);
            return;
        }
        if (result == 2)
            safe->processor_.clearCustomSample(padIndex);
        else
        {
            safe->processor_.projectEditor().checkpoint();
            auto& mutablePad = safe->processor_.projectState().kit.pads[padIndex];
            auto& note = mutablePad.midiNote;
            if (result == 3)
                note = static_cast<std::uint8_t>(std::max(0, static_cast<int>(note) - 1));
            else if (result == 4)
                note = static_cast<std::uint8_t>(std::min(127, static_cast<int>(note) + 1));
            else if (result == 5)
                note = static_cast<std::uint8_t>(36 + padIndex);
            else if (result == 6)
                mutablePad.solo = !mutablePad.solo;
            else if (result == 7)
                mutablePad.muted = !mutablePad.muted;
        }
        safe->updateControlsFromState();
    });
}

void BeatScholarLinuxEditor::chooseCustomSample(std::size_t padIndex)
{
    if (padIndex >= bsl::padCount)
        return;
    chooser_ = std::make_unique<juce::FileChooser>("Load Custom Sample", juce::File {},
                                                 "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                           revision = processor_.restoredStateRevision(), padIndex](const juce::FileChooser& chooser) {
        if (safe == nullptr || chooser.getResult() == juce::File {}
            || safe->processor_.restoredStateRevision() != revision)
            return;
        const auto result = safe->processor_.loadCustomSample(padIndex, chooser.getResult());
        if (result.wasOk())
        {
            safe->updateControlsFromState();
            safe->repaint();
            safe->status_.setText("Loaded " + chooser.getResult().getFileName(), juce::dontSendNotification);
        }
        else
            safe->status_.setText(result.getErrorMessage(), juce::dontSendNotification);
    });
}

void BeatScholarLinuxEditor::showFactorySampleMenu(std::size_t padIndex, std::size_t layer)
{
    if (padIndex >= bsl::padCount || layer >= 2 || (layer == 1 && padIndex > 1))
        return;
    const auto directory = dataRoot().getChildFile("Factory/Samples");
    const auto token = factorySampleToken(padIndex);
    std::vector<juce::File> files;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false))
    {
        if (!isAudioFile(file)) continue;
        const auto name = file.getFileName();
        bool matches = name.containsIgnoreCase(token);
        if (padIndex == 4)
            matches = matches || name.containsIgnoreCase("_INST-HH_TYPE-Closed_");
        if (padIndex <= 1)
            matches = matches && name.containsIgnoreCase(layer == 0 ? "_TYPE-Body_" : "_TYPE-Hit_");
        if (matches)
            files.push_back(file);
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return factorySampleDisplayName(left).compareIgnoreCase(factorySampleDisplayName(right)) < 0;
    });
    const auto current = juce::String(
        processor_.projectState().kit.pads[padIndex].factorySampleIds[layer]);
    juce::PopupMenu menu;
    for (std::size_t index = 0; index < files.size(); ++index)
        menu.addItem(1000 + static_cast<int>(index), factorySampleDisplayName(files[index]), true,
                     files[index].getFileName() == current);
    if (files.empty())
        menu.addItem(1, "No matching factory samples installed", false);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision(),
                            files = std::move(files), padIndex, layer](int result) {
        if (safe == nullptr || result < 1000
            || safe->processor_.restoredStateRevision() != revision)
            return;
        const auto index = static_cast<std::size_t>(result - 1000);
        if (index < files.size()
            && safe->processor_.setFactorySample(padIndex, layer, files[index].getFileName()))
        {
            safe->selectPad(padIndex, true);
            safe->status_.setText("Factory sample: " + factorySampleDisplayName(files[index]),
                                  juce::dontSendNotification);
        }
    });
}

void BeatScholarLinuxEditor::showRandomizerSelectionMenu()
{
    auto& settings = processor_.projectState().randomizer;
    juce::PopupMenu slices;
    slices.addItem(3, "Clear slices");
    slices.addItem(4, "All 1-45");
    slices.addSeparator();
    for (int value = 1; value <= static_cast<int>(bsl::maximumSubdivision); ++value)
        slices.addItem(100 + value, juce::String(value), true,
                       std::find(settings.selectedSubdivisions.begin(),
                                 settings.selectedSubdivisions.end(),
                                 static_cast<std::size_t>(value)) != settings.selectedSubdivisions.end());
    juce::PopupMenu menu;
    menu.addItem(1, "Clear drum selection");
    menu.addItem(2, "Select all drums");
    menu.addSubMenu("Slice values", slices);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync({}, [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this), revision = processor_.restoredStateRevision()](int result) {
        if (safe == nullptr || result == 0
            || safe->processor_.restoredStateRevision() != revision)
            return;
        auto& randomizerSettings = safe->processor_.projectState().randomizer;
        safe->processor_.projectEditor().checkpoint();
        if (result == 1)
            randomizerSettings.selectedPads.clear();
        else if (result == 2)
        {
            randomizerSettings.selectedPads.clear();
            for (std::size_t value = 0; value < bsl::padCount; ++value)
                randomizerSettings.selectedPads.push_back(static_cast<std::uint8_t>(value));
        }
        else if (result == 3)
            randomizerSettings.selectedSubdivisions.clear();
        else if (result == 4)
        {
            randomizerSettings.selectedSubdivisions.clear();
            for (std::size_t value = 1; value <= bsl::maximumSubdivision; ++value)
                randomizerSettings.selectedSubdivisions.push_back(value);
        }
        else if (result >= 101 && result <= 145)
        {
            const auto value = static_cast<std::size_t>(result - 100);
            if (const auto found = std::find(randomizerSettings.selectedSubdivisions.begin(),
                                             randomizerSettings.selectedSubdivisions.end(), value);
                found != randomizerSettings.selectedSubdivisions.end())
                randomizerSettings.selectedSubdivisions.erase(found);
            else
                randomizerSettings.selectedSubdivisions.push_back(value);
        }
        safe->processor_.markProjectDirty();
        safe->configureModuleControls();
    });
}

void BeatScholarLinuxEditor::savePresetAs()
{
    const auto directory = dataRoot().getChildFile("User/Presets");
    directory.createDirectory();
    const juce::String extension = ".bslpreset";
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save Beat Scholar Linux preset",
        directory.getChildFile(juce::String("My Preset") + extension), "*.bslpreset");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                           extension](const juce::FileChooser& chooser) {
        if (safe == nullptr)
            return;
        auto file = chooser.getResult();
        if (file == juce::File {}) return;
        if (!file.hasFileExtension(extension.substring(1)))
            file = file.withFileExtension(extension);
        if (safe->processor_.savePresetFile(file))
        {
            safe->status_.setText("Preset saved", juce::dontSendNotification);
            safe->configureModuleControls();
        }
        else
            safe->status_.setText("Could not save preset", juce::dontSendNotification);
    });
}

void BeatScholarLinuxEditor::saveKitAs()
{
    const auto directory = dataRoot().getChildFile("User/Kits");
    directory.createDirectory();
    const juce::String extension = ".bslkit";
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save Beat Scholar Linux kit",
        directory.getChildFile(juce::String("My Kit") + extension), "*.bslkit");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                           extension](const juce::FileChooser& chooser) {
        if (safe == nullptr)
            return;
        auto file = chooser.getResult();
        if (file == juce::File {}) return;
        if (!file.hasFileExtension(extension.substring(1)))
            file = file.withFileExtension(extension);
        if (safe->processor_.saveKitFile(file))
        {
            safe->status_.setText("Kit saved", juce::dontSendNotification);
            safe->configureModuleControls();
        }
        else
            safe->status_.setText("Could not save kit", juce::dontSendNotification);
    });
}

void BeatScholarLinuxEditor::showKitPresetMenu()
{
    const auto root = dataRoot();
    const auto factoryDirectory = root.getChildFile("Factory/Kits");
    const auto userDirectory = root.getChildFile("User/Kits");
    userDirectory.createDirectory();
    std::vector<MenuFile> entries;
    int nextId = 1000;
    juce::PopupMenu menu;
    menu.addItem(3, "Init Preset");
    menu.addSeparator();
    menu.addSectionHeader("Factory:");
    addDirectoryMenu(menu, factoryDirectory, MenuContent::kit, entries, nextId);
    menu.addSeparator();
    menu.addSectionHeader("User:");
    addDirectoryMenu(menu, userDirectory, MenuContent::kit, entries, nextId);
    menu.addSeparator();
    menu.addItem(1, "Save Preset");
    menu.addItem(2, "Show Preset Dir");
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(kitMenu_),
                       [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                        entries = std::move(entries), userDirectory](int result) {
        if (safe == nullptr || result == 0)
            return;
        if (result == 3)
        {
            safe->processor_.resetKitToInit();
            safe->status_.setText("Kit initialized", juce::dontSendNotification);
            safe->updateControlsFromState();
            return;
        }
        if (result == 1)
        {
            safe->saveKitAs();
            return;
        }
        if (result == 2)
        {
            userDirectory.startAsProcess();
            return;
        }
        const auto found = std::find_if(entries.begin(), entries.end(), [result](const auto& entry) {
            return entry.id == result;
        });
        if (found != entries.end() && safe->processor_.loadKitFile(found->file))
        {
            safe->status_.setText("Loaded " + found->file.getFileNameWithoutExtension(),
                                  juce::dontSendNotification);
            safe->configureModuleControls();
        }
    });
}

void BeatScholarLinuxEditor::showRandomizerPresetMenu()
{
    const auto root = dataRoot();
    const auto factoryDirectory = root.getChildFile("Factory/Step Randomizer");
    const auto userDirectory = root.getChildFile("User/Randomizers");
    userDirectory.createDirectory();
    std::vector<MenuFile> entries;
    int nextId = 1000;
    juce::PopupMenu menu;
    menu.addItem(3, "Init Preset");
    menu.addSeparator();
    menu.addSectionHeader("Factory:");
    addDirectoryMenu(menu, factoryDirectory, MenuContent::randomizer, entries, nextId);
    menu.addSeparator();
    menu.addSectionHeader("User:");
    addDirectoryMenu(menu, userDirectory, MenuContent::randomizer, entries, nextId);
    menu.addSeparator();
    menu.addItem(1, "Save Preset");
    menu.addItem(2, "Show Preset Dir");
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(randomizerTab_),
                       [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                        entries = std::move(entries), userDirectory](int result) {
        if (safe == nullptr || result == 0)
            return;
        if (result == 3)
        {
            safe->processor_.resetRandomizerToInit();
            safe->randomSlicePage_ = 0;
            safe->status_.setText("Randomizer initialized", juce::dontSendNotification);
            safe->configureModuleControls();
            return;
        }
        if (result == 2)
        {
            userDirectory.startAsProcess();
            return;
        }
        if (result == 1)
        {
            const juce::String extension = ".bslrandom";
            safe->chooser_ = std::make_unique<juce::FileChooser>(
                "Save Beat Scholar Linux randomizer",
                userDirectory.getChildFile(juce::String("My Randomizer") + extension),
                "*.bslrandom");
            safe->chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                                            | juce::FileBrowserComponent::canSelectFiles
                                            | juce::FileBrowserComponent::warnAboutOverwriting,
                                        [safe, extension](const juce::FileChooser& chooser) {
                if (safe == nullptr)
                    return;
                auto file = chooser.getResult();
                if (file == juce::File {}) return;
                if (!file.hasFileExtension(extension.substring(1)))
                    file = file.withFileExtension(extension);
                if (safe->processor_.saveRandomizerFile(file))
                {
                    safe->status_.setText("Randomizer saved", juce::dontSendNotification);
                    safe->configureModuleControls();
                }
                else
                    safe->status_.setText("Could not save randomizer", juce::dontSendNotification);
            });
            return;
        }
        const auto found = std::find_if(entries.begin(), entries.end(), [result](const auto& entry) {
            return entry.id == result;
        });
        if (found != entries.end() && safe->processor_.loadRandomizerFile(found->file))
        {
            safe->status_.setText("Loaded " + found->file.getFileNameWithoutExtension(),
                                  juce::dontSendNotification);
            safe->configureModuleControls();
        }
    });
}

void BeatScholarLinuxEditor::showPresetMenu()
{
    const auto root = dataRoot();
    const auto factoryPresets = root.getChildFile("Factory/Presets");
    const auto userPresets = root.getChildFile("User/Presets");
    userPresets.createDirectory();

    std::vector<MenuFile> entries;
    int nextId = 1000;
    juce::PopupMenu menu;
    menu.addItem(4, "Init Preset");
    menu.addSeparator();
    menu.addSectionHeader("Factory:");
    addDirectoryMenu(menu, factoryPresets, MenuContent::preset, entries, nextId);
    menu.addSeparator();
    menu.addSectionHeader("User:");
    addDirectoryMenu(menu, userPresets, MenuContent::preset, entries, nextId);
    menu.addItem(1, "Load preset file...");
    menu.addSeparator();
    menu.addItem(2, "Save Preset");
    menu.addItem(3, "Show Preset Dir");
    menu.addSeparator();
    const auto& global = processor_.projectState().global;
    menu.addItem(10, "Sequencer", true, global.sequencerEnabled);
    menu.addItem(11, "Sampler", true, global.samplerEnabled);
    menu.addItem(12, "AUX", true, global.auxEnabled);
    menu.addItem(13, "Master", true, global.masterEnabled);
    const auto& ui = processor_.projectState().ui;
    menu.addSeparator();
    menu.addItem(20, "Dark Mode", true, ui.darkMode);
    juce::PopupMenu midiDisplay, middleC, noteFormat, accidentals;
    middleC.addItem(21, "C3", true, ui.middleCOctave == 3);
    middleC.addItem(22, "C4", true, ui.middleCOctave == 4);
    noteFormat.addItem(23, "Note Names", true, ui.midiNoteNames);
    noteFormat.addItem(24, "Note Numbers", true, !ui.midiNoteNames);
    accidentals.addItem(25, "Sharps", true, !ui.midiFlats);
    accidentals.addItem(26, "Flats", true, ui.midiFlats);
    midiDisplay.addSubMenu("Middle C", middleC);
    midiDisplay.addSubMenu("Format", noteFormat);
    midiDisplay.addSubMenu("Accidentals", accidentals);
    menu.addSubMenu("MIDI Display", midiDisplay);
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(preset_),
                       [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                        entries = std::move(entries), userPresets, factoryPresets](int result) {
        if (safe == nullptr || result == 0)
            return;
        if (result == 4)
        {
            safe->processor_.resetPresetToInit();
            safe->status_.setText("Preset initialized", juce::dontSendNotification);
            safe->updateControlsFromState();
            return;
        }
        if (result >= 1000)
        {
            const auto found = std::find_if(entries.begin(), entries.end(), [result](const auto& entry) {
                return entry.id == result;
            });
            if (found == entries.end())
                return;
            if (safe->processor_.loadPresetFile(found->file))
            {
                safe->status_.setText("Loaded " + found->file.getFileNameWithoutExtension(),
                                      juce::dontSendNotification);
                safe->updateControlsFromState();
            }
            else
                safe->status_.setText("Could not load " + found->file.getFileName(),
                                      juce::dontSendNotification);
            return;
        }
        if (result == 3)
        {
            userPresets.startAsProcess();
            return;
        }
        if (result == 2)
        {
            safe->savePresetAs();
            return;
        }
        if (result >= 20 && result <= 26)
        {
            safe->processor_.projectEditor().checkpoint();
            auto& ui = safe->processor_.projectState().ui;
            if (result == 20) ui.darkMode = !ui.darkMode;
            if (result == 21 || result == 22) ui.middleCOctave = result == 21 ? 3 : 4;
            if (result == 23 || result == 24) ui.midiNoteNames = result == 23;
            if (result == 25 || result == 26) ui.midiFlats = result == 26;
            safe->processor_.markProjectDirty();
            safe->configureModuleControls();
            safe->repaint();
            return;
        }
        if (result >= 10 && result <= 13)
        {
            safe->processor_.projectEditor().checkpoint();
            auto& mutableGlobal = safe->processor_.projectState().global;
            if (result == 10)
                mutableGlobal.sequencerEnabled = !mutableGlobal.sequencerEnabled;
            else if (result == 11)
                mutableGlobal.samplerEnabled = !mutableGlobal.samplerEnabled;
            else if (result == 12)
                mutableGlobal.auxEnabled = !mutableGlobal.auxEnabled;
            else
                mutableGlobal.masterEnabled = !mutableGlobal.masterEnabled;
            safe->processor_.markProjectDirty();
            safe->configureModuleControls();
            return;
        }
        if (result == 1)
        {
            safe->chooser_ = std::make_unique<juce::FileChooser>("Load Beat Scholar preset",
                                                                 factoryPresets, "*.json;*.bslpreset");
            safe->chooser_->launchAsync(juce::FileBrowserComponent::openMode
                                            | juce::FileBrowserComponent::canSelectFiles,
                                        [safe](const juce::FileChooser& chooser) {
                if (safe != nullptr && safe->processor_.loadPresetFile(chooser.getResult()))
                {
                    safe->updateControlsFromState();
                    safe->status_.setText("Preset loaded", juce::dontSendNotification);
                }
            });
        }
    });
}

void BeatScholarLinuxEditor::loadAdjacentPreset(int delta)
{
    auto files = contentFiles(dataRoot().getChildFile("Factory/Presets"), MenuContent::preset);
    auto user = contentFiles(dataRoot().getChildFile("User/Presets"), MenuContent::preset);
    files.insert(files.end(), user.begin(), user.end());
    if (loadAdjacentContent(files, currentPresetFile(), delta,
                            [this](const juce::File& file) { return processor_.loadPresetFile(file); }))
    {
        status_.setText("Preset: " + currentPresetFile().getFileNameWithoutExtension(),
                        juce::dontSendNotification);
        updateControlsFromState();
    }
}

void BeatScholarLinuxEditor::loadAdjacentKit(int delta)
{
    auto files = contentFiles(dataRoot().getChildFile("Factory/Kits"), MenuContent::kit);
    auto user = contentFiles(dataRoot().getChildFile("User/Kits"), MenuContent::kit);
    files.insert(files.end(), user.begin(), user.end());
    if (loadAdjacentContent(files, currentKitFile(), delta,
                            [this](const juce::File& file) { return processor_.loadKitFile(file); }))
    {
        status_.setText("Kit: " + currentKitFile().getFileNameWithoutExtension(),
                        juce::dontSendNotification);
        updateControlsFromState();
    }
}

void BeatScholarLinuxEditor::loadAdjacentRandomizer(int delta)
{
    auto files = contentFiles(dataRoot().getChildFile("Factory/Step Randomizer"), MenuContent::randomizer);
    auto user = contentFiles(dataRoot().getChildFile("User/Randomizers"), MenuContent::randomizer);
    files.insert(files.end(), user.begin(), user.end());
    if (loadAdjacentContent(files, currentRandomizerFile(), delta,
                            [this](const juce::File& file) { return processor_.loadRandomizerFile(file); }))
    {
        status_.setText("Randomizer: " + currentRandomizerFile().getFileNameWithoutExtension(),
                        juce::dontSendNotification);
        updateControlsFromState();
    }
}

void BeatScholarLinuxEditor::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
        "Beat Scholar Linux",
        "Private native Linux reconstruction\nVersion 0.1.0\n\nActivation, trials, updates and deactivation are intentionally omitted.");
}

bool BeatScholarLinuxEditor::keyPressed(const juce::KeyPress& key)
{
    refreshRestoredState();
    if (key == juce::KeyPress::spaceKey)
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (processor_.wrapperType != juce::AudioProcessor::wrapperType_Standalone
            || numericEntry_.isVisible()
            || dynamic_cast<juce::TextEditor*>(focused) != nullptr
            || (focused != nullptr && focused->findParentComponentOfClass<juce::TextEditor>() != nullptr))
            return false;
        play_.onClick();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F2Key)
    {
        auto* topLevel = getTopLevelComponent();
        const int frameWidth = topLevel != nullptr ? topLevel->getWidth() - getWidth() : 0;
        const int frameHeight = topLevel != nullptr ? topLevel->getHeight() - getHeight() : 0;
        setSize(1280, 720);
        if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone
            && topLevel != nullptr)
        {
            const int targetWidth = 1280 + std::max(0, frameWidth);
            const int targetHeight = 720 + std::max(0, frameHeight);
            topLevel->setSize(targetWidth, targetHeight);
            if (auto* peer = topLevel->getPeer())
            {
                const auto target = peer->getBounds().withSizeKeepingCentre(targetWidth, targetHeight);
                peer->setBounds(target.withSizeKeepingCentre(targetWidth - 1, targetHeight - 1), false);
                peer->setBounds(target, false);
            }
        }
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F1Key)
    {
        if (!bsl::manualUrl().launchInDefaultBrowser())
            status_.setText("Could not open the manual", juce::dontSendNotification);
        return true;
    }
    // X11 reports Ctrl+letter text as a control character; the key code
    // still identifies the letter for command shortcuts.
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
    {
        restoreHistory(false);
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'y' || key.getKeyCode() == 'Y'))
    {
        restoreHistory(true);
        return true;
    }
    const auto character = static_cast<char>(std::tolower(static_cast<unsigned char>(key.getTextCharacter())));
    const juce::String lowerRow("asdfghjk");
    const juce::String upperRow("qwertyui");
    const int lowerIndex = lowerRow.indexOfChar(character);
    const int upperIndex = upperRow.indexOfChar(character);
    const int padIndex = lowerIndex >= 0 ? lowerIndex : upperIndex >= 0 ? upperIndex + 8 : -1;
    if (padIndex >= 0)
    {
        selectPad(static_cast<std::size_t>(padIndex), false);
        return true;
    }
    if (character >= '1' && character <= '9')
    {
        momentaryKey_ = character;
        updateMomentaryBrush();
        status_.setText("Subdivision " + juce::String(static_cast<int>(requestedSubdivision_)),
                        juce::dontSendNotification);
        return true;
    }
    if (character == '/')
    {
        momentaryKey_ = character;
        updateMomentaryBrush();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        const auto next = brush_ == Brush::drum ? previousBrush_ : Brush::drum;
        previousBrush_ = brush_;
        brush_ = next;
        const bool randomizer = brush_ == Brush::randomizer;
        module_ = randomizer ? Module::randomizer : Module::soundCharacter;
        processor_.projectState().ui.randomizerVisible = randomizer;
        configureModuleControls();
        return true;
    }
    return false;
}

void BeatScholarLinuxEditor::resetOrToggle(bool& value, bool defaultValue,
                                                const juce::ModifierKeys& modifiers)
{
    const bool next = modifiers.isAltDown() ? defaultValue : !value;
    if (next == value)
        return;
    processor_.projectEditor().checkpoint();
    value = next;
    processor_.markProjectDirty();
}

void BeatScholarLinuxEditor::updateMomentaryBrush()
{
    const bool active = momentaryAltActive_ || momentaryKey_ != 0;
    if (!active && !momentaryBrushActive_)
        return;
    if (active && !momentaryBrushActive_)
    {
        momentaryBrushPrevious_ = brush_;
        momentaryModulePrevious_ = module_;
        momentarySubdivisionPrevious_ = requestedSubdivision_;
    }
    momentaryBrushActive_ = active;
    brush_ = !active ? momentaryBrushPrevious_
                    : momentaryAltActive_ ? Brush::velocity
                    : momentaryKey_ == '/' ? Brush::randomizer : Brush::slicer;
    module_ = momentaryKey_ == '/' ? Module::randomizer : momentaryModulePrevious_;
    if (momentaryKey_ >= '1' && momentaryKey_ <= '9')
        requestedSubdivision_ = static_cast<std::size_t>(momentaryKey_ - '0');
    else if (!active)
        requestedSubdivision_ = momentarySubdivisionPrevious_;
    processor_.projectState().ui.randomizerVisible = module_ == Module::randomizer;
    configureModuleControls();
    repaint();
}

void BeatScholarLinuxEditor::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    momentaryAltActive_ = modifiers.isAltDown();
    updateMomentaryBrush();
    juce::AudioProcessorEditor::modifierKeysChanged(modifiers);
}

bool BeatScholarLinuxEditor::keyStateChanged(bool)
{
    if (!juce::KeyPress::isKeyCurrentlyDown('v') || juce::KeyPress::isKeyCurrentlyDown('c'))
        draggingBeatPaste_ = false;
    if (beatClipboard_)
        repaint();
    const bool wasActive = momentaryBrushActive_;
    // Hosts can deliver key state to the focused editor while modifier changes
    // go to the component under the mouse, so both paths must observe Alt.
    momentaryAltActive_ = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    if (momentaryKey_ != 0 && !juce::KeyPress::isKeyCurrentlyDown(momentaryKey_))
    {
        momentaryKey_ = juce::KeyPress::isKeyCurrentlyDown('/') ? '/' : 0;
        for (int key = '1'; key <= '9' && momentaryKey_ == 0; ++key)
            if (juce::KeyPress::isKeyCurrentlyDown(key))
                momentaryKey_ = static_cast<char>(key);
    }
    updateMomentaryBrush();
    return wasActive || momentaryBrushActive_;
}

bool BeatScholarLinuxEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path) { return isAudioFile(juce::File(path)); });
}

void BeatScholarLinuxEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty())
        return;
    auto pad = padAt({ x, y });
    if (!pad)
    {
        const auto sampleArea = designRect(getWidth(), getHeight(), 402.0f, 558.0f, 146.0f, 152.0f);
        if (module_ == Module::randomizer || !sampleArea.contains(x, y))
            return;
        pad = processor_.projectState().ui.selectedPad;
    }
    const auto supported = std::find_if(files.begin(), files.end(),
        [](const auto& path) { return isAudioFile(juce::File(path)); });
    if (supported == files.end())
    {
        status_.setText("Unsupported sample file type", juce::dontSendNotification);
        return;
    }
    const juce::File file(*supported);
    const auto result = processor_.loadCustomSample(*pad, file);
    if (result.wasOk())
    {
        selectPad(*pad, true);
        status_.setText("Loaded " + file.getFileName(), juce::dontSendNotification);
    }
    else
        status_.setText(result.getErrorMessage(), juce::dontSendNotification);
}

void BeatScholarLinuxEditor::timerCallback()
{
    // 20 Hz display with a short release; levels come from rendered samples,
    // never from knob position or transport state.
    for (std::size_t pad = 0; pad < bsl::padCount; ++pad)
        padMeterLevels_[pad] = std::max(processor_.takePadPeak(pad), padMeterLevels_[pad] * 0.72f);
    for (std::size_t channel = 0; channel < 2; ++channel)
        outputMeterLevels_[channel] = std::max(processor_.takeOutputPeak(channel), outputMeterLevels_[channel] * 0.72f);
    refreshRestoredState();
    if (!moduleControlGesture_)
        masterControls_[4].setValue(
            processor_.parameters().getRawParameterValue("masterGain")->load(),
            juce::dontSendNotification);
    if (const int requested = processor_.takeRequestedPatternForUi(); requested >= 0)
    {
        auto& mutableState = processor_.projectState();
        if (static_cast<std::size_t>(requested) < mutableState.patterns.size())
        {
            if (mutableState.activePattern != static_cast<std::size_t>(requested))
                cancelPatternInteraction();
            mutableState.activePattern = static_cast<std::size_t>(requested);
            processor_.markProjectDirty();
        }
    }
    processor_.publishProjectStateIfNeeded();
    const auto& state = processor_.projectState();
    sequencer_.setToggleState(state.global.sequencerEnabled, juce::dontSendNotification);
    sampler_.setToggleState(state.global.samplerEnabled, juce::dontSendNotification);
    auditionDuringPlayback_.setToggleState(state.global.auditionDuringPlayback,
                                           juce::dontSendNotification);
    kitLocked_.setToggleState(state.global.kitLocked, juce::dontSendNotification);
    const bool standaloneTransport = processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    if (!standaloneTransport)
        bpm_.setValue(processor_.effectiveBpm(), juce::dontSendNotification);
    const bool playing = standaloneTransport && processor_.isStandalonePlaying();
    play_.setToggleState(playing, juce::dontSendNotification);
    play_.setTitle(playing ? "Pause" : "Play");
    play_.setTooltip(!standaloneTransport ? "Playback follows the host timeline"
        : playing ? "Pause standalone playback" : "Resume standalone playback");
    syncBrushButtons();
    undo_.setEnabled(processor_.projectEditor().canUndo());
    redo_.setEnabled(processor_.projectEditor().canRedo());
    repaint();
}
