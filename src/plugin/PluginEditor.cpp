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
    title_.setFont(juce::Font(juce::FontOptions("Nimbus Sans Narrow", "Bold", 24.0f)));
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
    bpm_.setTooltip("Standalone tempo. A DAW host supplies tempo while its transport is active.");
    bpm_.setColour(juce::Slider::trackColourId, juce::Colour(lookAndFeel_.palette().accent));
    bpm_.addMouseListener(this, false);
    addAndMakeVisible(bpm_);
    bpmAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.parameters(), "bpm", bpm_);

    defaultVelocity_.setSliderStyle(juce::Slider::LinearHorizontal);
    defaultVelocity_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    defaultVelocity_.setComponentID("toolbar.velocity");
    defaultVelocity_.setRange(0.0, 100.0, 1.0);
    defaultVelocity_.setTextValueSuffix("%");
    defaultVelocity_.setTooltip("Default velocity used when a note slice is enabled.");
    defaultVelocity_.setDoubleClickReturnValue(false, 80.0);
    defaultVelocity_.addMouseListener(this, false);
    defaultVelocity_.setValue(processor_.projectState().global.defaultVelocity * 100.0,
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
            static_cast<float>(defaultVelocity_.getValue() / 100.0);
    };
    addAndMakeVisible(defaultVelocity_);

    routing_.addItem("Stereo", 1);
    routing_.addItem("Multi", 2);
    routing_.setTooltip("Stereo uses master processing; Multi routes pads to sixteen stereo outputs.");
    addAndMakeVisible(routing_);
    routingAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
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
    configureButton(play_, "", [this] { processor_.playStandalone(); });
    play_.setComponentID("toolbar.play");
    play_.setTitle("Play");
    configureButton(pause_, "", [this] { processor_.pauseStandalone(); });
    pause_.setComponentID("toolbar.pause");
    pause_.setTitle("Pause");
    configureButton(stop_, "", [this] { processor_.stopStandalone(); });
    stop_.setComponentID("toolbar.stop");
    stop_.setTitle("Stop");
    configureButton(drumBrush_, "", [this] {
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
    configureButton(barsPatterns_, "BARS", [this] {
        auto& mode = processor_.projectState().ui.patternMode;
        mode = !mode;
        barsPatterns_.setButtonText(mode ? "Patterns" : "Bars");
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
        processor_.projectEditor().checkpoint();
        enabled = !enabled;
        processor_.markProjectDirty();
    });
    auditionDuringPlayback_.setTooltip("Allow drum-pad audition while transport is playing");
    auditionDuringPlayback_.setComponentID("kit.audition");
    configureButton(kitLocked_, "LOCK", [this] {
        auto& locked = processor_.projectState().global.kitLocked;
        processor_.projectEditor().checkpoint();
        locked = !locked;
        processor_.markProjectDirty();
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
            else if (juce::ModifierKeys::getCurrentModifiersRealtime().isPopupMenu())
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
        showPadMenu(processor_.projectState().ui.selectedPad);
    });
    loadSample_.setComponentID("sample.folder");
    loadSample_.setTitle("Open sample menu");
    loadSample_.setTooltip("Load or choose a sample for the selected pad");
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
        processor_.projectEditor().checkpoint();
        pad.solo = !pad.solo;
        processor_.markProjectDirty();
        configureModuleControls();
    });
    configureButton(padMute_, "M", [this] {
        auto& pad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
        processor_.projectEditor().checkpoint();
        pad.muted = !pad.muted;
        processor_.markProjectDirty();
        configureModuleControls();
    });
    configureButton(oneShot_, "One Shot", [this] {
        auto& pad = processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad];
        processor_.projectEditor().checkpoint();
        pad.oneShot = !pad.oneShot;
        processor_.markProjectDirty();
        configureModuleControls();
    });
    oneShot_.setComponentID("sample.oneshot");

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
            processor_.projectEditor().checkpoint();
            auto& random = processor_.projectState().randomizer;
            const std::array<bool*, 4> enabled { &random.noteSelectionEnabled,
                &random.slicingEnabled, &random.triggeringEnabled, &random.velocityEnabled };
            *enabled[index] = !*enabled[index];
            processor_.markProjectDirty();
            configureModuleControls();
        });
        randomModuleToggles_[index].setComponentID("random.module." + juce::String(static_cast<int>(index)));
        randomModuleToggles_[index].setTitle(moduleNames[index]);
        randomModuleToggles_[index].setTooltip(juce::String("Enable ") + moduleNames[index]);
    }

    configureSlider(soundControls_[0], 0.0, 2.0, 0.001, " s");
    configureSlider(soundControls_[1], 0.0, 1.0, 0.001, "");
    configureSlider(soundControls_[2], -12.0, 12.0, 0.01, " st");
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
        soundLabels_[index].setFont(juce::FontOptions(10.0f, juce::Font::bold));
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
            applyModuleControls();
        };
    }

    for (std::size_t index = 0; index < effectControls_.size(); ++index)
    {
        if (index == 3)
            configureSlider(effectControls_[index], -60.0, 60.0, 0.01, " dB");
        else
            configureSlider(effectControls_[index], 0.0, 1.0, 0.001, "");
        effectLabels_[index].setJustificationType(juce::Justification::centred);
        effectLabels_[index].setFont(bsl::ui::font(9.0f, true));
        effectLabels_[index].setColour(juce::Label::textColourId,
                                      juce::Colour(lookAndFeel_.palette().text));
        addAndMakeVisible(effectLabels_[index]);
        effectControls_[index].addMouseListener(this, false);
        effectControls_[index].onDragStart = [this, index] {
            refreshRestoredState(&effectControls_[index]);
            moduleControlGesture_ = true;
            moduleControlCheckpointed_ = false;
        };
        effectControls_[index].onDragEnd = [this] {
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
                pad.driveEnabled = effectControls_[3].getValue() != 0.0;
            if (index == 3 || index == 8)
                pad.independentDistortion = true;
            // The Gain fader controls the shared parallel distortion chain,
            // independently of each pad's Dist send and insert Drive.
            if (index == 9)
                for (auto& sound : state.kit.pads)
                    sound.distortionDrive = static_cast<float>(effectControls_[9].getValue());
            applyModuleControls();
        };
    }
    for (std::size_t index = 0; index < masterControls_.size(); ++index)
    {
        configureSlider(masterControls_[index], 0.0, index == 4 ? 1.5 : 1.0, 0.001, "");
        masterLabels_[index].setJustificationType(juce::Justification::centred);
        masterLabels_[index].setFont(bsl::ui::font(9.0f, true));
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
            applyModuleControls(index == 4);
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
        if (numericEntryControl_ != nullptr)
            numericEntryControl_->setValue(value, juce::sendNotificationSync);
        numericEntry_.setVisible(false);
        numericEntryControl_ = nullptr;
    };
    const auto cancelEntry = [this] {
        numericEntry_.setVisible(false);
        numericEntryControl_ = nullptr;
    };
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

juce::Rectangle<int> BeatScholarLinuxEditor::lowerBounds() const
{
    return designRect(getWidth(), getHeight(), 8.0f, 522.0f, 1264.0f, 190.0f);
}

juce::Rectangle<int> BeatScholarLinuxEditor::soundBounds() const
{
    return designRect(getWidth(), getHeight(), 400.0f, 522.0f, 648.0f, 190.0f);
}

juce::Rectangle<int> BeatScholarLinuxEditor::masterBounds() const
{
    return designRect(getWidth(), getHeight(), 1052.0f, 522.0f, 220.0f, 190.0f);
}

void BeatScholarLinuxEditor::paint(juce::Graphics& graphics)
{
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
    graphics.setFont(bsl::ui::font(12.0f * scale, true));
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
        if (bounds.getWidth() >= 140.0f * scale)
        {
            auto actions = bounds.removeFromRight(std::min(bounds.getWidth() * 0.30f, 95.0f * scale));
            const float actionWidth = actions.getWidth() / 3.0f;
            for (int action = 0; action < 3; ++action)
                drawActionGlyph(actions.removeFromLeft(actionWidth)
                                    .withSizeKeepingCentre(18.0f * scale, 18.0f * scale), action);
        }
        firstQuarter += duration;
    }
    graphics.restoreState();

    // Visible lane column, including the manual's lane-wide brush arrows.
    const auto laneRail = designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 448.0f);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(laneRail);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(laneRail.toFloat(), std::max(1.0f, scale));
    const auto addLane = designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 42.0f);
    graphics.setFont(bsl::ui::font(22.0f * scale, true));
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
    graphics.drawText("+", addLane, juce::Justification::centred);
    const float laneHeight = static_cast<float>(pattern.getHeight())
                             / static_cast<float>(std::max<std::size_t>(1, current.lanes.size()));
    graphics.setFont(bsl::ui::font(9.0f * scale, true));
    for (std::size_t lane = 0; lane < current.lanes.size(); ++lane)
    {
        const float y = static_cast<float>(pattern.getY())
                        + (static_cast<float>(lane) - verticalScroll_) * laneHeight * verticalZoom_;
        const juce::Rectangle<float> cell(static_cast<float>(laneRail.getX()), y,
                                          static_cast<float>(laneRail.getWidth()),
                                          laneHeight * verticalZoom_ - scale);
        if (cell.getBottom() < pattern.getY() || cell.getY() > pattern.getBottom())
            continue;
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.drawRect(cell, std::max(1.0f, scale));
        graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
        if (brush_ == Brush::slicer || brush_ == Brush::randomizer)
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
            auto controls = cell.reduced(4.0f * scale, 5.0f * scale);
            const float controlHeight = controls.getHeight() / 4.0f;
            for (int control = 0; control < 4; ++control)
            {
                auto slot = controls.removeFromTop(controlHeight).reduced(0.0f, 1.0f * scale);
                graphics.setColour(juce::Colour(control == 0 && laneState.solo
                                                     ? lookAndFeel_.palette().accent
                                                 : control == 1 && laneState.muted
                                                     ? lookAndFeel_.palette().magenta
                                                     : lookAndFeel_.palette().panelRaised));
                graphics.fillRect(slot);
                graphics.setColour(juce::Colour(lookAndFeel_.palette().borderBright));
                graphics.drawRect(slot, std::max(0.8f, scale));
                graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
                graphics.setFont(bsl::ui::font(8.0f * scale, true));
                graphics.drawText(control == 0 ? "S" : control == 1 ? "M" : control == 2 ? "-" : "+",
                                  slot.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    // Manual-style zoom/scroll rails.
    const auto horizontalRail = designRect(getWidth(), getHeight(), 62.0f, 511.0f, 1070.0f, 7.0f).toFloat();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.fillRoundedRectangle(horizontalRail, horizontalRail.getHeight() * 0.5f);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
    graphics.fillEllipse(horizontalRail.getRight() - 122.0f * scale,
                         horizontalRail.getY() - 2.0f * scale,
                         11.0f * scale, 11.0f * scale);
    graphics.setFont(bsl::ui::font(13.0f * scale, true));
    graphics.drawText("-", designRect(getWidth(), getHeight(), 1082.0f, 502.0f, 18.0f, 18.0f),
                      juce::Justification::centred);
    graphics.drawText("+", designRect(getWidth(), getHeight(), 1112.0f, 502.0f, 18.0f, 18.0f),
                      juce::Justification::centred);

    // Right-side arrangement rail and vertical pads.
    const auto arrangement = arrangementBounds();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().panel));
    graphics.fillRect(arrangement);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.drawRect(arrangement.toFloat(), std::max(1.0f, scale));
    const auto verticalRail = designRect(getWidth(), getHeight(), 1139.0f, 66.0f, 5.0f, 378.0f).toFloat();
    graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
    graphics.fillRoundedRectangle(verticalRail, verticalRail.getWidth() * 0.5f);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
    graphics.fillEllipse(verticalRail.getX() - 2.0f * scale,
                         verticalRail.getBottom() - 48.0f * scale,
                         9.0f * scale, 9.0f * scale);
    graphics.setFont(bsl::ui::font(13.0f * scale, true));
    graphics.drawText("-", designRect(getWidth(), getHeight(), 1132.0f, 446.0f, 18.0f, 16.0f),
                      juce::Justification::centred);
    graphics.drawText("+", designRect(getWidth(), getHeight(), 1132.0f, 466.0f, 18.0f, 16.0f),
                      juce::Justification::centred);
    graphics.setColour(juce::Colour(lookAndFeel_.palette().textMuted));
    graphics.setFont(bsl::ui::font(10.0f * scale, true));
    graphics.drawText(state.ui.patternMode ? "Q  " + quantizationLabel(current.quantization)
                                           : juce::String(current.name),
                      designRect(getWidth(), getHeight(), 1144.0f, 88.0f, 122.0f, 18.0f),
                      juce::Justification::centred);
    const auto padArea = designRect(getWidth(), getHeight(), 1146.0f, 112.0f, 118.0f, 338.0f);
    const int count = state.ui.patternMode ? static_cast<int>(state.patterns.size())
                                           : static_cast<int>(current.bars.size());
    const int padHeight = std::max(static_cast<int>(54.0f * scale),
                                   std::min(static_cast<int>(72.0f * scale),
                                            padArea.getHeight() / std::max(1, count)));
    if (state.ui.patternMode)
    {
        for (std::size_t index = 0; index < state.patterns.size(); ++index)
        {
            auto bounds = juce::Rectangle<int>(padArea.getX(),
                                               padArea.getY() + static_cast<int>(index) * padHeight,
                                               padArea.getWidth(), padHeight - 4);
            if (bounds.getY() >= padArea.getBottom())
                break;
            graphics.setColour(index == state.activePattern
                                   ? juce::Colour(lookAndFeel_.palette().panelRaised)
                                   : juce::Colour(lookAndFeel_.palette().panel));
            graphics.fillRect(bounds);
            graphics.setColour(index == state.activePattern
                                   ? juce::Colour(lookAndFeel_.palette().borderBright)
                                   : juce::Colour(lookAndFeel_.palette().border));
            graphics.drawRect(bounds, index == state.activePattern ? 2 : 1);
            graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
            graphics.setFont(bsl::ui::font(11.0f * scale, true));
            graphics.drawFittedText(juce::String(state.patterns[index].name), bounds.reduced(5, 3),
                                    juce::Justification::centred, 1);
            drawActionGlyph(bounds.toFloat().removeFromTop(18.0f * scale)
                                .removeFromLeft(18.0f * scale), 1);
            drawActionGlyph(bounds.toFloat().removeFromTop(18.0f * scale)
                                .removeFromRight(18.0f * scale), 2);
            graphics.drawText(midiNoteLabel(state.patterns[index].triggerNote, state.ui),
                              bounds.withY(bounds.getBottom() - 14).withHeight(14),
                              juce::Justification::centredRight);
        }
    }
    else
    {
        for (std::size_t index = 0; index < current.bars.size(); ++index)
        {
            auto bounds = juce::Rectangle<int>(padArea.getX(),
                                               padArea.getY() + static_cast<int>(index) * padHeight,
                                               padArea.getWidth(), padHeight - 4);
            if (bounds.getY() >= padArea.getBottom())
                break;
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
                      designRect(getWidth(), getHeight(), 510.0f, 524.0f, 420.0f, 30.0f),
                      juce::Justification::centred);
    graphics.drawText("Master", designRect(getWidth(), getHeight(), 1060.0f, 524.0f, 150.0f, 30.0f),
                      juce::Justification::centred);

    const auto drawMeter = [&graphics, this](float level, float x, float y, float width, float height) {
        auto bounds = designRect(getWidth(), getHeight(), x, y, width, height).toFloat();
        graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
        graphics.fillRect(bounds);
        const float fraction = level > 0.001f ? juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(level) + 60.0f) / 60.0f) : 0.0f;
        graphics.setColour(level > 1.0f ? juce::Colour(0xffef4665) : juce::Colour(lookAndFeel_.palette().meter));
        graphics.fillRect(bounds.removeFromBottom(bounds.getHeight() * fraction));
    };
    drawMeter(outputMeterLevels_[0], 1205.0f, 602.0f, 8.0f, 88.0f);
    drawMeter(outputMeterLevels_[1], 1225.0f, 602.0f, 8.0f, 88.0f);
    if (module_ != Module::randomizer)
        drawMeter(padMeterLevels_[state.ui.selectedPad], 1031.0f, 616.0f, 7.0f, 76.0f);

    if (module_ == Module::randomizer)
    {
        graphics.setFont(bsl::ui::font(9.0f * scale, true));
        for (const auto& item : std::array<std::pair<const char*, juce::Rectangle<int>>, 4> {{
                 { "Drum Selection", designRect(getWidth(), getHeight(), 402.0f, 558.0f, 126.0f, 152.0f) },
                 { "Beat Slicing", designRect(getWidth(), getHeight(), 530.0f, 558.0f, 166.0f, 152.0f) },
                 { "Step Triggering", designRect(getWidth(), getHeight(), 698.0f, 558.0f, 174.0f, 152.0f) },
                 { "Velocity Range", designRect(getWidth(), getHeight(), 874.0f, 558.0f, 172.0f, 152.0f) }
             }})
        {
            graphics.setColour(juce::Colour(lookAndFeel_.palette().border));
            graphics.drawRect(item.second.toFloat(), std::max(1.0f, scale));
            graphics.setColour(juce::Colour(lookAndFeel_.palette().text));
            auto heading = item.second;
            graphics.drawText(item.first, heading.removeFromTop(static_cast<int>(22.0f * scale)),
                              juce::Justification::centred);
        }
    }
    else
    {
        auto samplePanel = designRect(getWidth(), getHeight(), 402.0f, 558.0f, 146.0f, 152.0f);
        const auto& selectedPad = state.kit.pads[state.ui.selectedPad];
        const auto sampleColour = padColours_[state.ui.selectedPad].withSaturation(0.72f)
                                                              .withBrightness(0.70f);
        const bool custom = !selectedPad.customSampleName.empty();
        graphics.setColour(custom ? juce::Colour(lookAndFeel_.palette().panel) : sampleColour);
        graphics.fillRect(samplePanel);
        graphics.setColour(sampleColour.brighter(0.24f));
        graphics.fillRect(samplePanel.removeFromTop(static_cast<int>((custom ? 7.0f : 17.0f) * scale)));
        graphics.setColour(juce::Colour(lookAndFeel_.palette().text).withAlpha(0.90f));
        const auto sampleGlyph = designRect(getWidth(), getHeight(), 451.0f, custom ? 618.0f : 594.0f,
                                             52.0f, custom ? 36.0f : 42.0f).toFloat();
        if (!selectedPad.customSampleName.empty())
        {
            juce::Path waveform;
            waveform.startNewSubPath(sampleGlyph.getX(), sampleGlyph.getCentreY());
            constexpr std::array<float, 13> waveformHeights {
                0.00f, 0.08f, -0.22f, 0.55f, -0.82f, 0.38f, -0.16f,
                0.73f, -0.48f, 0.18f, -0.06f, 0.02f, 0.00f
            };
            for (std::size_t index = 1; index < waveformHeights.size(); ++index)
                waveform.lineTo(sampleGlyph.getX()
                                      + sampleGlyph.getWidth() * static_cast<float>(index)
                                            / static_cast<float>(waveformHeights.size() - 1),
                                sampleGlyph.getCentreY()
                                      - waveformHeights[index] * sampleGlyph.getHeight() * 0.46f);
            graphics.strokePath(waveform, juce::PathStrokeType(std::max(1.5f, 2.0f * scale),
                                                               juce::PathStrokeType::curved));
        }
        else
        {
            const auto drum = sampleGlyph.withSizeKeepingCentre(36.0f * scale, 36.0f * scale);
            graphics.drawEllipse(drum, std::max(1.4f, 1.7f * scale));
            graphics.fillEllipse(juce::Rectangle<float>(5.0f * scale, 5.0f * scale)
                                     .withCentre(drum.getCentre()));
            graphics.drawLine(drum.getCentreX(), drum.getCentreY() + 2.0f * scale,
                              drum.getCentreX(), drum.getBottom() + 7.0f * scale,
                              std::max(1.0f, scale));
            graphics.drawEllipse(drum.getCentreX() - 3.0f * scale,
                                 drum.getBottom() + 5.0f * scale,
                                 6.0f * scale, 4.0f * scale, std::max(1.0f, scale));
        }
        // Vertical separators reproduce the manual's sound design groupings.
        for (const float x : { 548.0f, 608.0f, 668.0f, 850.0f, 976.0f })
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
            const auto& beat = lane.beats[beatIndex];
            const float radius = std::max(3.0f, std::min(std::min(1.0f, spans[beatIndex].length) * quarterWidth,
                                                       cellHeight) * 0.36f);
            const float x = area.getX()
                            + (spans[beatIndex].start + spans[beatIndex].length * 0.5f - horizontalScroll_) * quarterWidth;
            if (x + radius < area.getX() || x - radius > area.getRight())
                continue;
            const auto slices = beat.slices.size();
            for (std::size_t sliceIndex = 0; sliceIndex < slices; ++sliceIndex)
            {
                const float start = juce::MathConstants<float>::twoPi
                                          * static_cast<float>(sliceIndex) / static_cast<float>(slices);
                const float end = juce::MathConstants<float>::twoPi
                                        * static_cast<float>(sliceIndex + 1) / static_cast<float>(slices);
                juce::Path wedge;
                // JUCE angles start at twelve o'clock and increase clockwise,
                // matching beatAt(). A pie closes through the centre, not a chord.
                wedge.addPieSegment(x - radius, y - radius, radius * 2.0f,
                                    radius * 2.0f, start, end, 0.0f);
                const auto& slice = beat.slices[sliceIndex];
                const std::size_t pad = std::min<std::size_t>(slice.padIndex, bsl::padCount - 1);
                graphics.setColour(slice.enabled
                                       ? padColours_[pad].withSaturation(0.62f)
                                             .withBrightness(0.72f)
                                             .withAlpha(std::clamp(0.55f + slice.velocity * 0.45f,
                                                                   0.0f, 1.0f))
                                       : juce::Colour(lookAndFeel_.palette().panel));
                graphics.fillPath(wedge);
                graphics.setColour(slice.enabled ? juce::Colour(lookAndFeel_.palette().text)
                                                   : juce::Colour(lookAndFeel_.palette().border));
                graphics.strokePath(wedge, juce::PathStrokeType(std::max(1.0f, scale * 1.15f)));
            }
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
    bpm_.setVisible(processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
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
    soundTab_.setBounds(designRect(getWidth(), getHeight(), 468.0f, 524.0f, 438.0f, 30.0f));
    randomizerTab_.setBounds(designRect(getWidth(), getHeight(), 650.0f, 524.0f, 330.0f, 30.0f));
    randomizerPrevious_.setBounds(designRect(getWidth(), getHeight(), 990.0f, 526.0f, 24.0f, 26.0f));
    randomizerNext_.setBounds(designRect(getWidth(), getHeight(), 1018.0f, 526.0f, 24.0f, 26.0f));
    routing_.setBounds(designRect(getWidth(), getHeight(), 1180.0f, 527.0f, 82.0f, 25.0f));
    masterTab_.setBounds(designRect(getWidth(), getHeight(), 1236.0f, 526.0f, 28.0f, 27.0f));
    const bool standalone = processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    routing_.setVisible(!standalone);
    masterTab_.setVisible(standalone);
    kitPrevious_.setVisible(module_ != Module::randomizer);
    kitNext_.setVisible(module_ != Module::randomizer);
    randomizerPrevious_.setVisible(module_ == Module::randomizer);
    randomizerNext_.setVisible(module_ == Module::randomizer);

    const bool custom = !processor_.projectState().kit.pads[processor_.projectState().ui.selectedPad].customSampleName.empty();
    loadSample_.setBounds(designRect(getWidth(), getHeight(), 406.0f, custom ? 563.0f : 596.0f, 40.0f, 26.0f));
    clearSample_.setBounds(designRect(getWidth(), getHeight(), 512.0f, 563.0f, 27.0f, 24.0f));
    layerOne_.setBounds(designRect(getWidth(), getHeight(), 408.0f, custom ? 587.0f : 562.0f,
                                   custom ? 131.0f : 101.0f, 26.0f));
    layerTwo_.setBounds(designRect(getWidth(), getHeight(), 408.0f, 674.0f, 131.0f, 28.0f));
    padMute_.setBounds(designRect(getWidth(), getHeight(), 404.0f, 526.0f, 30.0f, 26.0f));
    padSolo_.setBounds(designRect(getWidth(), getHeight(), 436.0f, 526.0f, 30.0f, 26.0f));
    oneShot_.setBounds(designRect(getWidth(), getHeight(), 448.0f, custom ? 672.0f : 640.0f, 58.0f, 24.0f));

    if (module_ == Module::randomizer)
    {
        const std::array<float, 4> toggleX { 500.0f, 668.0f, 846.0f, 1018.0f };
        for (std::size_t index = 0; index < randomModuleToggles_.size(); ++index)
            randomModuleToggles_[index].setBounds(designRect(getWidth(), getHeight(), toggleX[index], 562.0f, 24.0f, 24.0f));
        randomClearPads_.setBounds(designRect(getWidth(), getHeight(), 438.0f, 600.0f, 54.0f, 28.0f));
        randomAllPads_.setBounds(designRect(getWidth(), getHeight(), 438.0f, 650.0f, 54.0f, 28.0f));
        randomClearSlices_.setBounds(designRect(getWidth(), getHeight(), 548.0f, 584.0f, 54.0f, 24.0f));
        randomAllSlices_.setBounds(designRect(getWidth(), getHeight(), 622.0f, 584.0f, 54.0f, 24.0f));
        randomSlicePrevious_.setBounds(designRect(getWidth(), getHeight(), 536.0f, 625.0f, 24.0f, 32.0f));
        randomSliceNext_.setBounds(designRect(getWidth(), getHeight(), 666.0f, 625.0f, 24.0f, 32.0f));
        for (std::size_t index = 0; index < randomSliceButtons_.size(); ++index)
        {
            const int column = static_cast<int>(index % 3);
            const int row = static_cast<int>(index / 3);
            randomSliceButtons_[index].setBounds(designRect(getWidth(), getHeight(),
                565.0f + static_cast<float>(column) * 32.0f,
                614.0f + static_cast<float>(row) * 30.0f, 28.0f, 27.0f));
        }
        const std::array<juce::Rectangle<int>, 8> controlBounds {
            designRect(getWidth(), getHeight(), 716.0f, 601.0f, 58.0f, 58.0f),
            designRect(getWidth(), getHeight(), 718.0f, 675.0f, 136.0f, 20.0f),
            designRect(getWidth(), getHeight(), 886.0f, 601.0f, 62.0f, 62.0f),
            designRect(getWidth(), getHeight(), 968.0f, 601.0f, 62.0f, 62.0f),
            designRect(getWidth(), getHeight(), 668.0f, 564.0f, 24.0f, 24.0f),
            designRect(getWidth(), getHeight(), 500.0f, 564.0f, 24.0f, 24.0f),
            designRect(getWidth(), getHeight(), 1018.0f, 564.0f, 24.0f, 24.0f),
            designRect(getWidth(), getHeight(), 824.0f, 610.0f, 28.0f, 28.0f)
        };
        const std::array<juce::Rectangle<int>, 8> labelBounds {
            designRect(getWidth(), getHeight(), 704.0f, 583.0f, 82.0f, 14.0f),
            designRect(getWidth(), getHeight(), 748.0f, 658.0f, 76.0f, 14.0f),
            designRect(getWidth(), getHeight(), 886.0f, 583.0f, 62.0f, 14.0f),
            designRect(getWidth(), getHeight(), 968.0f, 583.0f, 62.0f, 14.0f),
            designRect(getWidth(), getHeight(), 668.0f, 564.0f, 24.0f, 14.0f),
            designRect(getWidth(), getHeight(), 500.0f, 564.0f, 24.0f, 14.0f),
            designRect(getWidth(), getHeight(), 1018.0f, 564.0f, 24.0f, 14.0f),
            designRect(getWidth(), getHeight(), 786.0f, 583.0f, 76.0f, 14.0f)
        };
        for (std::size_t index = 0; index < 8; ++index)
        {
            soundLabels_[index].setBounds(labelBounds[index]);
            soundControls_[index].setBounds(controlBounds[index]);
        }
    }
    else
    {
        const std::array<juce::Point<float>, 8> characterPositions {{
            { 552.0f, 566.0f }, { 552.0f, 642.0f }, { 612.0f, 566.0f }, { 612.0f, 642.0f },
            { 508.0f, 605.0f }, { 984.0f, 566.0f }, { 984.0f, 642.0f }, { 468.0f, 638.0f }
        }};
        for (std::size_t index = 0; index < characterPositions.size(); ++index)
        {
            if (index == 4)
            {
                soundLabels_[index].setBounds(designRect(getWidth(), getHeight(),
                    510.0f, 568.0f, 34.0f, 14.0f));
                soundControls_[index].setBounds(designRect(getWidth(), getHeight(),
                    516.0f, 584.0f, 24.0f, 88.0f));
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
                soundControls_[index].setBounds(designRect(getWidth(), getHeight(),
                    characterPositions[index].x, characterPositions[index].y + 12.0f, 54.0f, 55.0f));
            }
        }
        const std::array<juce::Point<float>, 10> effectPositions {{
            { 672.0f, 566.0f }, { 732.0f, 566.0f }, { 792.0f, 566.0f },
            { 672.0f, 642.0f }, { 732.0f, 642.0f }, { 792.0f, 642.0f },
            { 918.0f, 566.0f }, { 858.0f, 566.0f }, { 858.0f, 642.0f }, { 918.0f, 642.0f }
        }};
        for (std::size_t index = 0; index < effectPositions.size(); ++index)
        {
            effectLabels_[index].setBounds(designRect(getWidth(), getHeight(),
                effectPositions[index].x, effectPositions[index].y, 54.0f, 14.0f));
            effectControls_[index].setBounds(designRect(getWidth(), getHeight(),
                effectPositions[index].x, effectPositions[index].y + 12.0f, 54.0f, 55.0f));
        }
    }

    const std::array<juce::Point<float>, 5> masterPositions {{
        { 1064.0f, 566.0f }, { 1120.0f, 566.0f }, { 1064.0f, 642.0f },
        { 1120.0f, 642.0f }, { 1184.0f, 566.0f }
    }};
    for (std::size_t index = 0; index < masterPositions.size(); ++index)
    {
        masterLabels_[index].setBounds(designRect(getWidth(), getHeight(),
            masterPositions[index].x, masterPositions[index].y, 52.0f, 14.0f));
        masterControls_[index].setBounds(designRect(getWidth(), getHeight(),
            index == 4 ? 1190.0f : masterPositions[index].x,
            masterPositions[index].y + 12.0f,
            index == 4 ? 58.0f : 52.0f, index == 4 ? 116.0f : 55.0f));
    }

    barsPatterns_.setBounds(designRect(getWidth(), getHeight(), 1155.0f, 64.0f, 94.0f, 22.0f));
    addItem_.setBounds(designRect(getWidth(), getHeight(), 1148.0f, 456.0f, 114.0f, 46.0f));
    duplicateItem_.setVisible(false);
    removeItem_.setVisible(false);
}

std::optional<BeatScholarLinuxEditor::BeatHit> BeatScholarLinuxEditor::beatAt(juce::Point<int> point) const
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
    float angle = std::atan2(static_cast<float>(point.y) - centreY,
                             static_cast<float>(point.x) - centreX)
                  + juce::MathConstants<float>::halfPi;
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
        const auto area = designRect(getWidth(), getHeight(), 1146.0f, 112.0f, 118.0f, 338.0f);
        if (!area.contains(point) || state.ui.patternMode || pattern.bars.empty())
            return std::nullopt;
        const int itemHeight = std::max(static_cast<int>(54.0f * designScale(getWidth(), getHeight())),
                                        std::min(static_cast<int>(72.0f * designScale(getWidth(), getHeight())),
                                                 area.getHeight() / static_cast<int>(pattern.bars.size())));
        const auto index = static_cast<std::size_t>((point.y - area.getY()) / itemHeight);
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

std::optional<std::size_t> BeatScholarLinuxEditor::patternAt(juce::Point<int> point) const
{
    const auto area = designRect(getWidth(), getHeight(), 1146.0f, 112.0f, 118.0f, 338.0f);
    const auto& state = processor_.projectState();
    if (!state.ui.patternMode || !area.contains(point) || state.patterns.empty())
        return std::nullopt;
    const int itemHeight = std::max(static_cast<int>(54.0f * designScale(getWidth(), getHeight())),
                                    std::min(static_cast<int>(72.0f * designScale(getWidth(), getHeight())),
                                             area.getHeight() / static_cast<int>(state.patterns.size())));
    const auto index = static_cast<std::size_t>((point.y - area.getY()) / itemHeight);
    return index < state.patterns.size() ? std::optional<std::size_t>(index) : std::nullopt;
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
    auto tempo = juce::MidiMessage::tempoMetaEvent(static_cast<int>(
        std::llround(60000000.0 / std::max(1.0, state.global.beatsPerMinute))));
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
    const auto noteLength = static_cast<double>(ticksPerQuarter / 8);
    for (const auto& event : events)
    {
        auto on = juce::MidiMessage::noteOn(1, event.note, event.velocity);
        on.setTimeStamp(static_cast<double>(event.tick));
        track.addEvent(on);
        auto off = juce::MidiMessage::noteOff(1, event.note);
        off.setTimeStamp(std::min(endTick, static_cast<double>(event.tick) + noteLength));
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
    if (slider == nullptr || !slider->isVisible()) return;
    numericEntryControl_ = slider;
    numericEntry_.setText(juce::String(slider->getValue(), slider->getNumDecimalPlacesToDisplay()), false);
    const auto centre = slider->getBounds().getCentre();
    numericEntry_.setBounds(std::clamp(centre.x - 48, 0, std::max(0, getWidth() - 96)),
                            std::clamp(centre.y - 13, 0, std::max(0, getHeight() - 26)), 96, 26);
    numericEntry_.setVisible(true);
    numericEntry_.toFront(true);
    numericEntry_.grabKeyboardFocus();
    numericEntry_.selectAll();
}

void BeatScholarLinuxEditor::mouseDown(const juce::MouseEvent& event)
{
    refreshRestoredState();
    cancelPatternInteraction();
    const auto position = event.getEventRelativeTo(this).getPosition();
    if (designRect(getWidth(), getHeight(), 775.0f, 14.0f, 31.0f, 34.0f).contains(position))
    {
        showSubdivisionMenu();
        return;
    }
    if (designRect(getWidth(), getHeight(), 8.0f, 60.0f, 50.0f, 42.0f).contains(position))
    {
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
        if (bpm_.isVisible() && bpm_.getBounds().contains(position))
        {
            bpm_.setValue(120.0);
            return;
        }
        if (defaultVelocity_.getBounds().contains(position))
        {
            defaultVelocity_.setValue(80.0);
            return;
        }
        std::array<double, 9> defaults {};
        if (module_ == Module::soundCharacter)
            defaults = { 0, 0, 0, 0, 0, 1, 0, 1, 0 };
        else if (module_ == Module::soundEffects)
            defaults = { 0, 1, 0, 0, 0, 0, 0, 0, 0 };
        else if (module_ == Module::randomizer)
            defaults = { 0.5, 0.5, 0.4, 1, 1, 1, 1, 0, 0 };
        else
            defaults = { 1, 0, 0, 0, 1, 0, 0, 0, 0 };
        for (std::size_t index = 0; index < soundControls_.size(); ++index)
            if (soundControls_[index].isVisible()
                && soundControls_[index].getBounds().contains(position))
            {
                soundControls_[index].setValue(defaults[index], juce::sendNotificationSync);
                return;
            }
        static constexpr std::array<double, 10> effectDefaults { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0.5 };
        for (std::size_t index = 0; index < effectControls_.size(); ++index)
            if (effectControls_[index].isVisible()
                && effectControls_[index].getBounds().contains(position))
            {
                effectControls_[index].setValue(effectDefaults[index], juce::sendNotificationSync);
                return;
            }
        static constexpr std::array<double, 5> masterDefaults { 1, 0, 0, 0, 1 };
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
        else if (brush_ == Brush::randomizer || juce::KeyPress::isKeyCurrentlyDown('/'))
            processor_.projectEditor().randomizeLane(
                state.activePattern, *lane,
                static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()));
        else if (brush_ == Brush::slicer)
        {
            processor_.projectEditor().checkpoint();
            auto& beats = state.patterns[state.activePattern].lanes[*lane].beats;
            for (auto& beat : beats)
                beat.setSubdivision(requestedSubdivision_);
        }
        else
        {
            const auto area = patternBounds();
            const auto laneCount = state.patterns[state.activePattern].lanes.size();
            const float laneHeight = static_cast<float>(area.getHeight())
                                     / static_cast<float>(laneCount) * verticalZoom_;
            const float laneTop = static_cast<float>(area.getY())
                                  + (static_cast<float>(*lane) - verticalScroll_) * laneHeight;
            const int action = std::clamp(static_cast<int>(
                (static_cast<float>(position.y) - laneTop) / std::max(1.0f, laneHeight) * 4.0f), 0, 3);
            if (action <= 1)
            {
                processor_.projectEditor().checkpoint();
                auto& laneState = state.patterns[state.activePattern].lanes[*lane];
                if (action == 0)
                    laneState.solo = !laneState.solo;
                else
                    laneState.muted = !laneState.muted;
                processor_.markProjectDirty();
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
        if (arrangementBounds().contains(position) && !barHeaderBounds().contains(position))
        {
            const auto padArea = designRect(getWidth(), getHeight(), 1146.0f, 112.0f, 118.0f, 338.0f);
            const int itemHeight = std::max(static_cast<int>(54.0f * designScale(getWidth(), getHeight())),
                                            std::min(static_cast<int>(72.0f * designScale(getWidth(), getHeight())),
                                                     padArea.getHeight() / static_cast<int>(pattern.bars.size())));
            const auto localX = position.x - padArea.getX();
            const auto localY = (position.y - padArea.getY()) % itemHeight;
            const int actionBand = std::max(14, itemHeight / 4);
            if (localY < actionBand && localX < padArea.getWidth() / 3)
            {
                processor_.projectEditor().addBar(state.activePattern, bsl::BarState::make(4, 4));
                updateControlsFromState();
                return;
            }
            if (localY < actionBand && localX > padArea.getWidth() * 2 / 3)
            {
                processor_.projectEditor().removeBar(state.activePattern, *bar);
                updateControlsFromState();
                return;
            }
            if (localY > itemHeight - actionBand && localX < padArea.getWidth() / 3)
            {
                processor_.projectEditor().duplicateBar(state.activePattern, *bar);
                updateControlsFromState();
                return;
            }
        }
        std::size_t start = 0;
        for (std::size_t index = 0; index < *bar; ++index)
            start += static_cast<std::size_t>(pattern.bars[index].numerator);
        const auto count = static_cast<std::size_t>(pattern.bars[*bar].numerator);
        if (juce::KeyPress::isKeyCurrentlyDown('c'))
        {
            BarClipboard copy;
            copy.meter = pattern.bars[*bar];
            for (const auto& lane : pattern.lanes)
                copy.lanes.emplace_back(lane.beats.begin() + static_cast<std::ptrdiff_t>(start),
                                        lane.beats.begin() + static_cast<std::ptrdiff_t>(start + count));
            barClipboard_ = std::move(copy);
        }
        else if (juce::KeyPress::isKeyCurrentlyDown('v') && barClipboard_)
        {
            processor_.projectEditor().setBarMeter(state.activePattern, *bar,
                                                   barClipboard_->meter.numerator,
                                                   barClipboard_->meter.denominator);
            auto& updated = state.patterns[state.activePattern];
            start = 0;
            for (std::size_t index = 0; index < *bar; ++index)
                start += static_cast<std::size_t>(updated.bars[index].numerator);
            for (std::size_t lane = 0; lane < std::min(updated.lanes.size(), barClipboard_->lanes.size()); ++lane)
                std::copy(barClipboard_->lanes[lane].begin(), barClipboard_->lanes[lane].end(),
                          updated.lanes[lane].beats.begin() + static_cast<std::ptrdiff_t>(start));
        }
        else if (event.mods.isShiftDown())
        {
            processor_.projectEditor().checkpoint();
            for (auto& lane : pattern.lanes)
                for (std::size_t beat = start; beat < start + count; ++beat)
                    lane.beats[beat].reset();
        }
        else if (event.mods.isPopupMenu())
            showBarMenu(*bar);
        else if (barHeaderBounds().contains(position))
        {
            draggingBarMeter_ = true;
            barMeterDragChanged_ = false;
            draggingBarIndex_ = *bar;
            dragStartY_ = position.y;
            dragStartBarNumerator_ = pattern.bars[*bar].numerator;
            dragStartBarDenominator_ = pattern.bars[*bar].denominator;
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
            const float actionsWidth = std::min(drawnWidth * 0.30f, 95.0f * scale);
            const float actionsLeft = barLeft + drawnWidth - actionsWidth;
            if (drawnWidth >= 140.0f * scale && position.x >= actionsLeft)
            {
                draggingBarMeter_ = false;
                lastHit_.reset();
                const int action = std::clamp(static_cast<int>((position.x - actionsLeft) / (actionsWidth / 3.0f)), 0, 2);
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
        repaint();
        return;
    }
    if (const auto pattern = patternAt(position))
    {
        auto& state = processor_.projectState();
        const auto padArea = designRect(getWidth(), getHeight(), 1146.0f, 112.0f, 118.0f, 338.0f);
        const int itemHeight = std::max(static_cast<int>(54.0f * designScale(getWidth(), getHeight())),
                                        std::min(static_cast<int>(72.0f * designScale(getWidth(), getHeight())),
                                                 padArea.getHeight() / static_cast<int>(state.patterns.size())));
        const auto localX = position.x - padArea.getX();
        const auto localY = (position.y - padArea.getY()) % itemHeight;
        const int actionBand = std::max(14, itemHeight / 4);
        if (localY < actionBand && localX < padArea.getWidth() / 3)
        {
            processor_.projectEditor().addPattern();
            updateControlsFromState();
            return;
        }
        if (localY < actionBand && localX > padArea.getWidth() * 2 / 3)
        {
            processor_.projectEditor().removePattern(*pattern);
            updateControlsFromState();
            return;
        }
        if (localY > itemHeight - actionBand && localX < padArea.getWidth() / 3)
        {
            processor_.projectEditor().duplicatePattern(*pattern);
            updateControlsFromState();
            return;
        }
        if (juce::KeyPress::isKeyCurrentlyDown('c'))
            patternClipboard_ = state.patterns[*pattern];
        else if (juce::KeyPress::isKeyCurrentlyDown('v') && patternClipboard_)
        {
            processor_.projectEditor().checkpoint();
            const auto name = state.patterns[*pattern].name;
            const auto note = state.patterns[*pattern].triggerNote;
            state.patterns[*pattern] = *patternClipboard_;
            state.patterns[*pattern].name = name;
            state.patterns[*pattern].triggerNote = note;
        }
        else
        {
            state.activePattern = *pattern;
            processor_.markProjectDirty();
            if (event.mods.isShiftDown())
            {
                midiDragPending_ = true;
                midiDragPattern_ = *pattern;
            }
            else if (event.mods.isPopupMenu())
                showPatternMenu(*pattern);
        }
        repaint();
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
            else if (event.mods.isCommandDown())
            {
                beatDragCheckpointed_ = false;
                draggingSubdivision_ = true;
                dragStartY_ = position.y;
                dragStartSubdivision_ = beat.slices.size();
            }
            else if (brush_ == Brush::velocity || event.mods.isAltDown())
            {
                auto& slice = beat.slices[hit->slice];
                beatDragCheckpointed_ = false;
                draggingVelocity_ = true;
                dragStartY_ = position.y;
                dragStartVelocity_ = slice.velocity;
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
        processor_.markProjectDirty();
    }
    repaint();
}

void BeatScholarLinuxEditor::mouseUp(const juce::MouseEvent&)
{
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

void BeatScholarLinuxEditor::applyBrush(const BeatHit& hit, const juce::ModifierKeys& modifiers)
{
    auto& state = processor_.projectState();
    auto& beat = state.patterns[state.activePattern].lanes[hit.lane].beats[hit.beat];
    if (juce::KeyPress::isKeyCurrentlyDown('c'))
    {
        beatClipboard_ = beat;
        return;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('v') && beatClipboard_)
    {
        processor_.projectEditor().checkpoint();
        beat = *beatClipboard_;
        return;
    }
    processor_.projectEditor().checkpoint();
    if (brush_ == Brush::randomizer || juce::KeyPress::isKeyCurrentlyDown('/'))
    {
        bsl::Randomizer::apply(beat, state.randomizer,
                               static_cast<std::uint32_t>(juce::Time::getMillisecondCounter()),
                               state.global.defaultVelocity);
        return;
    }
    if (brush_ == Brush::slicer)
        beat.setSubdivision(requestedSubdivision_);
    if (modifiers.isShiftDown())
    {
        const bool enable = std::any_of(beat.slices.begin(), beat.slices.end(), [](const auto& slice) {
            return !slice.enabled;
        });
        for (auto& slice : beat.slices)
        {
            slice.enabled = enable;
            if (enable)
            {
                slice.padIndex = static_cast<std::uint8_t>(state.ui.selectedPad);
                slice.velocity = state.global.defaultVelocity;
            }
        }
        return;
    }
    const auto selectedSlice = std::min(hit.slice, beat.slices.size() - 1);
    auto& slice = beat.slices[selectedSlice];
    const auto padIndex = static_cast<std::uint8_t>(state.ui.selectedPad);
    if (slice.enabled && slice.padIndex == padIndex)
        slice.enabled = false;
    else
    {
        slice.padIndex = padIndex;
        slice.velocity = state.global.defaultVelocity;
        slice.enabled = true;
    }
}

void BeatScholarLinuxEditor::showBeatMenu(const BeatHit& hit)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Fill / empty beat");
    menu.addItem(2, "Copy beat");
    menu.addItem(3, "Paste beat", beatClipboard_.has_value());
    menu.addSeparator();
    for (int slices = 1; slices <= 9; ++slices)
        menu.addItem(100 + slices, "Slice into " + juce::String(slices));
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
        if (result == 1)
            for (auto& slice : beat.slices)
                slice.enabled = !slice.enabled;
        else if (result == 3 && safe->beatClipboard_)
            beat = *safe->beatClipboard_;
        else if (result >= 101 && result <= 109)
            beat.setSubdivision(static_cast<std::size_t>(result - 100));
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::selectPad(std::size_t pad, bool audition)
{
    if (pad >= bsl::padCount)
        return;
    processor_.projectState().ui.selectedPad = pad;
    if (audition && (processor_.projectState().global.auditionDuringPlayback
                     || !processor_.isTransportPlaying()))
        processor_.triggerPad(pad);
    updateControlsFromState();
}

void BeatScholarLinuxEditor::updateControlsFromState()
{
    cancelPatternInteraction();
    configureModuleControls();
}

void BeatScholarLinuxEditor::cancelPatternInteraction()
{
    lastHit_.reset();
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
    momentaryBrushActive_ = false;
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
    configureModuleControls();
    if (editedControl != nullptr)
        editedControl->setValue(editedValue, juce::dontSendNotification);
}

void BeatScholarLinuxEditor::configureModuleControls()
{
    updatingControls_ = true;
    auto& state = processor_.projectState();
    lookAndFeel_.setDarkMode(state.ui.darkMode);
    numericEntry_.setColour(juce::TextEditor::backgroundColourId,
                            juce::Colour(lookAndFeel_.palette().panelRaised));
    state.ui.selectedPad = std::min(state.ui.selectedPad, bsl::padCount - 1);
    const auto& pad = state.kit.pads[state.ui.selectedPad];
    const bool randomizerVisible = module_ == Module::randomizer;
    const bool custom = !pad.customSampleName.empty();
    const std::array<bool, 4> moduleEnabled { state.randomizer.noteSelectionEnabled,
        state.randomizer.slicingEnabled, state.randomizer.triggeringEnabled, state.randomizer.velocityEnabled };
    for (std::size_t index = 0; index < randomModuleToggles_.size(); ++index)
    {
        randomModuleToggles_[index].setVisible(randomizerVisible);
        randomModuleToggles_[index].setToggleState(moduleEnabled[index], juce::dontSendNotification);
    }
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
        labels = { "ATTACK", "DAMP", "PITCH", "FINE", "MIX", "LEVEL", "L             R", "ONE SHOT", "" };
        suffixes = { " s", "", " st", " ct", "", "", "", "", "" };
        ranges = {{{ 0, 2, 0.001 }, { 0, 1, 0.001 }, { -12, 12, 0.01 }, { -100, 100, 0.1 },
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
        labels = { "TRIGGER CHANCE", "SPACING", "MIN", "MAX", "", "", "", "FILL EMPTY", "" };
        for (std::size_t index = 0; index < 4; ++index)
            ranges[index] = { 0, 1, 0.001 };
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
        soundControls_[index].setVisible(visibleControl);
        soundLabels_[index].setVisible(visibleControl);
        soundControls_[index].setRange(ranges[index][0], ranges[index][1], ranges[index][2]);
        soundControls_[index].setTextValueSuffix(suffixes[index]);
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
        "HPF", "CUTOFF", "RESO", "DRIVE", "COMP", "CRUSH", "REVERB", "ROOM", "DIST", "GAIN"
    };
    const std::array<double, 10> effectValues {
        pad.highPass, pad.cutoff, pad.resonance, pad.driveGainDb, pad.compression,
        pad.crush, pad.reverb, pad.room, pad.distortionMix, pad.distortionDrive
    };
    const std::array<double, 10> effectDefaults { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0.5 };
    for (std::size_t index = 0; index < effectControls_.size(); ++index)
    {
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

    const std::array<const char*, 5> masterNames { "CUTOFF", "RESO", "CRUSH", "COMP", "MASTER" };
    const std::array<double, 5> masterValues { state.master.cutoff, state.master.resonance,
                                               state.master.crush, state.master.compression,
                                               state.master.gain };
    const std::array<double, 5> masterDefaults { 1, 0, 0, 0, 1 };
    for (std::size_t index = 0; index < masterControls_.size(); ++index)
    {
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
    layerOne_.setButtonText(!pad.customSampleName.empty()
                                ? juce::String(pad.customSampleName)
                                : layerName(pad.factorySampleIds[0], "Layer 1"));
    layerOne_.setComponentID(custom ? "sample.custom" : "sample.layer");
    layerTwo_.setButtonText(layerName(pad.factorySampleIds[1], "Layer 2"));
    padSolo_.setToggleState(pad.solo, juce::dontSendNotification);
    padMute_.setToggleState(pad.muted, juce::dontSendNotification);
    oneShot_.setToggleState(pad.oneShot, juce::dontSendNotification);
    loadSample_.setVisible(!randomizerVisible);
    clearSample_.setVisible(!randomizerVisible && !pad.customSampleName.empty());
    layerOne_.setVisible(!randomizerVisible);
    layerTwo_.setVisible(!randomizerVisible && !custom && state.ui.selectedPad <= 1);
    padSolo_.setVisible(!randomizerVisible);
    padMute_.setVisible(!randomizerVisible);
    oneShot_.setVisible(!randomizerVisible);
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
        if (!padState.customSampleName.empty())
            tooltip << " — custom: " << padState.customSampleName;
        else if (!padState.factorySampleIds[0].empty())
            tooltip << " — " << factorySampleDisplayName(
                juce::File(juce::String(padState.factorySampleIds[0])));
        pads_[index].setTooltip(tooltip);
    }
    barsPatterns_.setButtonText(state.ui.patternMode ? "Patterns" : "Bars");
    defaultVelocity_.setValue(state.global.defaultVelocity * 100.0,
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


void BeatScholarLinuxEditor::applyModuleControls(bool writeMasterGain)
{
    if (updatingControls_)
        return;
    auto& state = processor_.projectState();
    auto& pad = state.kit.pads[state.ui.selectedPad];
    if (module_ != Module::randomizer)
    {
        pad.attackSeconds = static_cast<float>(soundControls_[0].getValue());
        pad.attackNormalized = std::clamp(pad.attackSeconds, 0.0f, 1.0f);
        pad.damp = static_cast<float>(soundControls_[1].getValue());
        pad.pitchSemitones = static_cast<float>(soundControls_[2].getValue());
        pad.fineCents = static_cast<float>(soundControls_[3].getValue());
        pad.layerMix = static_cast<float>(soundControls_[4].getValue());
        pad.gain = static_cast<float>(soundControls_[5].getValue());
        pad.pan = static_cast<float>(soundControls_[6].getValue());
        pad.oneShot = soundControls_[7].getValue() >= 0.5;
    }
    else if (module_ == Module::randomizer)
    {
        auto& random = state.randomizer;
        random.triggerChance = static_cast<float>(soundControls_[0].getValue());
        random.spacing = static_cast<float>(soundControls_[1].getValue());
        random.minimumVelocity = static_cast<float>(soundControls_[2].getValue());
        random.maximumVelocity = static_cast<float>(soundControls_[3].getValue());
        if (random.minimumVelocity > random.maximumVelocity)
            random.maximumVelocity = random.minimumVelocity;
        random.slicingEnabled = soundControls_[4].getValue() >= 0.5;
        random.noteSelectionEnabled = soundControls_[5].getValue() >= 0.5;
        random.velocityEnabled = soundControls_[6].getValue() >= 0.5;
        random.fillEmptyOnly = soundControls_[7].getValue() >= 0.5;
    }
    if (module_ != Module::randomizer)
    {
        pad.highPass = static_cast<float>(effectControls_[0].getValue());
        pad.cutoff = static_cast<float>(effectControls_[1].getValue());
        pad.resonance = static_cast<float>(effectControls_[2].getValue());
        pad.driveGainDb = static_cast<float>(effectControls_[3].getValue());
        pad.compression = static_cast<float>(effectControls_[4].getValue());
        pad.crush = static_cast<float>(effectControls_[5].getValue());
        pad.reverb = static_cast<float>(effectControls_[6].getValue());
        pad.room = static_cast<float>(effectControls_[7].getValue());
        pad.distortionMix = static_cast<float>(effectControls_[8].getValue());
    }
    state.master.cutoff = static_cast<float>(masterControls_[0].getValue());
    state.master.resonance = static_cast<float>(masterControls_[1].getValue());
    state.master.crush = static_cast<float>(masterControls_[2].getValue());
    state.master.compression = static_cast<float>(masterControls_[3].getValue());
    if (writeMasterGain)
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

void BeatScholarLinuxEditor::showBarMenu(std::size_t barIndex)
{
    static constexpr std::array<std::pair<int, int>, 8> meters {{
        { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 }
    }};
    juce::PopupMenu meterMenu;
    for (std::size_t index = 0; index < meters.size(); ++index)
        meterMenu.addItem(100 + static_cast<int>(index), juce::String(meters[index].first)
                                                        + "/" + juce::String(meters[index].second));
    juce::PopupMenu menu;
    menu.addSubMenu("Meter", meterMenu);
    menu.addItem(1, "Reset bar");
    menu.addItem(2, "Duplicate bar");
    menu.addItem(3, "Remove bar");
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
        safe->repaint();
    });
}

void BeatScholarLinuxEditor::showPatternMenu(std::size_t patternIndex)
{
    if (patternIndex >= processor_.projectState().patterns.size())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, "Rename...");
    menu.addItem(2, "Trigger note down");
    menu.addItem(3, "Trigger note up");
    juce::PopupMenu quantization;
    const auto currentQuantization = processor_.projectState().patterns[patternIndex].quantization;
    quantization.addItem(10, "Off", true, currentQuantization == bsl::PatternQuantization::off);
    quantization.addItem(16, "1/32", true, currentQuantization == bsl::PatternQuantization::thirtySecond);
    quantization.addItem(15, "1/16", true, currentQuantization == bsl::PatternQuantization::sixteenth);
    quantization.addItem(11, "1/8", true, currentQuantization == bsl::PatternQuantization::eighth);
    quantization.addItem(12, "1/4", true, currentQuantization == bsl::PatternQuantization::quarter);
    quantization.addItem(13, "2/4", true, currentQuantization == bsl::PatternQuantization::half);
    quantization.addItem(14, "4/4", true, currentQuantization == bsl::PatternQuantization::whole);
    menu.addSubMenu("Trigger quantization", quantization);
    menu.addItem(4, "Export MIDI file");
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
        else if (result >= 10 && result <= 16)
        {
            safe->processor_.projectEditor().checkpoint();
            state.patterns[patternIndex].quantization =
                static_cast<bsl::PatternQuantization>(result - 10);
        }
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
    menu.addItem(2, "Remove custom sample", !pad.customSampleBase64.empty());
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
            safe->chooser_ = std::make_unique<juce::FileChooser>("Load a sample", juce::File {},
                                                                 "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg");
            safe->chooser_->launchAsync(juce::FileBrowserComponent::openMode
                                            | juce::FileBrowserComponent::canSelectFiles,
                                        [safe, padIndex, revision](const juce::FileChooser& chooser) {
                if (safe == nullptr || chooser.getResult() == juce::File {}
                    || safe->processor_.restoredStateRevision() != revision) return;
                const auto result = safe->processor_.loadCustomSample(padIndex, chooser.getResult());
                if (result.wasOk())
                {
                    safe->updateControlsFromState();
                    safe->repaint();
                    safe->status_.setText("Loaded " + chooser.getResult().getFileName(),
                                          juce::dontSendNotification);
                }
                else
                    safe->status_.setText(result.getErrorMessage(), juce::dontSendNotification);
            });
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

void BeatScholarLinuxEditor::showFactorySampleMenu(std::size_t padIndex, std::size_t layer)
{
    if (padIndex >= bsl::padCount || layer >= 2 || (layer == 1 && padIndex > 1))
        return;
    const auto directory = dataRoot().getChildFile("Factory/Samples");
    const auto token = factorySampleToken(padIndex);
    std::vector<juce::File> files;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.flac"))
    {
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
    juce::PopupMenu factory;
    juce::PopupMenu user;
    addDirectoryMenu(factory, factoryDirectory, MenuContent::kit, entries, nextId);
    addDirectoryMenu(user, userDirectory, MenuContent::kit, entries, nextId);
    juce::PopupMenu menu;
    menu.addSubMenu("Factory", factory, factory.getNumItems() > 0);
    menu.addSubMenu("User", user, user.getNumItems() > 0);
    menu.addSeparator();
    menu.addItem(1, "Save Kit");
    menu.addItem(2, "Show Kit Dir");
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(kitMenu_),
                       [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                        entries = std::move(entries), userDirectory](int result) {
        if (safe == nullptr || result == 0)
            return;
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
    juce::PopupMenu factory;
    juce::PopupMenu user;
    addDirectoryMenu(factory, factoryDirectory, MenuContent::randomizer, entries, nextId);
    addDirectoryMenu(user, userDirectory, MenuContent::randomizer, entries, nextId);
    juce::PopupMenu menu;
    menu.addSubMenu("Factory", factory, factory.getNumItems() > 0);
    menu.addSubMenu("User", user, user.getNumItems() > 0);
    menu.addSeparator();
    menu.addItem(1, "Save Preset");
    menu.addItem(2, "Show Preset Dir");
    menu.setLookAndFeel(&lookAndFeel_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(randomizerTab_),
                       [safe = juce::Component::SafePointer<BeatScholarLinuxEditor>(this),
                        entries = std::move(entries), userDirectory](int result) {
        if (safe == nullptr || result == 0)
            return;
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
    juce::PopupMenu factoryPresetMenu;
    juce::PopupMenu userPresetMenu;
    addDirectoryMenu(factoryPresetMenu, factoryPresets, MenuContent::preset, entries, nextId);
    addDirectoryMenu(userPresetMenu, userPresets, MenuContent::preset, entries, nextId);

    juce::PopupMenu menu;
    menu.addSubMenu("Factory", factoryPresetMenu, factoryPresetMenu.getNumItems() > 0);
    menu.addSubMenu("User", userPresetMenu, userPresetMenu.getNumItems() > 0);
    menu.addItem(1, "Load preset file...");
    menu.addSeparator();
    menu.addItem(2, "Save Preset");
    menu.addItem(3, "Show Preset Dir");
    menu.addSeparator();
    const auto& global = processor_.projectState().global;
    menu.addItem(10, "Sequencer", true, global.sequencerEnabled);
    menu.addItem(11, "Sampler", true, global.samplerEnabled);
    juce::PopupMenu enable;
    enable.addItem(12, "AUX", true, global.multiOutput);
    enable.addItem(13, "Master", true, !global.multiOutput);
    menu.addSubMenu("Enable", enable);
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
        if (result == 10 || result == 11)
        {
            safe->processor_.projectEditor().checkpoint();
            auto& mutableGlobal = safe->processor_.projectState().global;
            if (result == 10)
                mutableGlobal.sequencerEnabled = !mutableGlobal.sequencerEnabled;
            else
                mutableGlobal.samplerEnabled = !mutableGlobal.samplerEnabled;
            safe->processor_.markProjectDirty();
            safe->configureModuleControls();
            return;
        }
        if (result == 12 || result == 13)
        {
            safe->routing_.setSelectedId(result == 12 ? 2 : 1, juce::sendNotificationSync);
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
    if (files.empty())
        return;
    auto found = std::find(files.begin(), files.end(), currentPresetFile());
    int index = found == files.end() ? (delta < 0 ? static_cast<int>(files.size()) - 1 : 0)
                                    : static_cast<int>(std::distance(files.begin(), found)) + delta;
    index = (index % static_cast<int>(files.size()) + static_cast<int>(files.size()))
            % static_cast<int>(files.size());
    if (processor_.loadPresetFile(files[static_cast<std::size_t>(index)]))
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
    if (files.empty())
        return;
    auto found = std::find(files.begin(), files.end(), currentKitFile());
    int index = found == files.end() ? (delta < 0 ? static_cast<int>(files.size()) - 1 : 0)
                                    : static_cast<int>(std::distance(files.begin(), found)) + delta;
    index = (index % static_cast<int>(files.size()) + static_cast<int>(files.size()))
            % static_cast<int>(files.size());
    if (processor_.loadKitFile(files[static_cast<std::size_t>(index)]))
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
    if (files.empty())
        return;
    auto found = std::find(files.begin(), files.end(), currentRandomizerFile());
    int index = found == files.end() ? (delta < 0 ? static_cast<int>(files.size()) - 1 : 0)
                                    : static_cast<int>(std::distance(files.begin(), found)) + delta;
    index = (index % static_cast<int>(files.size()) + static_cast<int>(files.size()))
            % static_cast<int>(files.size());
    if (processor_.loadRandomizerFile(files[static_cast<std::size_t>(index)]))
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
    if (key.getModifiers().isCommandDown() && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))
    {
        restoreHistory(false);
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y'))
    {
        restoreHistory(true);
        return true;
    }
    const auto character = static_cast<char>(std::tolower(static_cast<unsigned char>(key.getTextCharacter())));
    const juce::String upperRow("qwertyui");
    const juce::String lowerRow("asdfghjk");
    if (const int index = upperRow.indexOfChar(character); index >= 0)
    {
        selectPad(static_cast<std::size_t>(index), false);
        return true;
    }
    if (const int index = lowerRow.indexOfChar(character); index >= 0)
    {
        selectPad(static_cast<std::size_t>(index + 8), false);
        return true;
    }
    if (character >= '1' && character <= '9')
    {
        requestedSubdivision_ = static_cast<std::size_t>(character - '0');
        if (!momentaryBrushActive_)
        {
            momentaryBrushPrevious_ = brush_;
            momentaryModulePrevious_ = module_;
        }
        momentaryBrushActive_ = true;
        brush_ = Brush::slicer;
        status_.setText("Subdivision " + juce::String(static_cast<int>(requestedSubdivision_)),
                        juce::dontSendNotification);
        return true;
    }
    if (character == '/')
    {
        if (!momentaryBrushActive_)
        {
            momentaryBrushPrevious_ = brush_;
            momentaryModulePrevious_ = module_;
        }
        momentaryBrushActive_ = true;
        brush_ = Brush::randomizer;
        module_ = Module::randomizer;
        processor_.projectState().ui.randomizerVisible = true;
        configureModuleControls();
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
    if (lastHit_ && (key.getKeyCode() == juce::KeyPress::leftKey
                     || key.getKeyCode() == juce::KeyPress::rightKey
                     || key.getKeyCode() == juce::KeyPress::downKey))
    {
        auto& state = processor_.projectState();
        auto& beat = state.patterns[state.activePattern].lanes[lastHit_->lane].beats[lastHit_->beat];
        processor_.projectEditor().checkpoint();
        if (key.getKeyCode() == juce::KeyPress::leftKey)
            beat.rotate(-1);
        else if (key.getKeyCode() == juce::KeyPress::rightKey)
            beat.rotate(1);
        else
            beat.mirror();
        repaint();
        return true;
    }
    return false;
}

bool BeatScholarLinuxEditor::keyStateChanged(bool)
{
    if (!momentaryBrushActive_)
        return false;
    bool shortcutHeld = juce::KeyPress::isKeyCurrentlyDown('/');
    for (int key = '1'; key <= '9' && !shortcutHeld; ++key)
        shortcutHeld = juce::KeyPress::isKeyCurrentlyDown(key);
    if (shortcutHeld)
        return true;
    brush_ = momentaryBrushPrevious_;
    module_ = momentaryModulePrevious_;
    processor_.projectState().ui.randomizerVisible = module_ == Module::randomizer;
    configureModuleControls();
    requestedSubdivision_ = 1;
    momentaryBrushActive_ = false;
    repaint();
    return true;
}

bool BeatScholarLinuxEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path) { return isAudioFile(juce::File(path)); });
}

void BeatScholarLinuxEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty())
        return;
    const auto pad = padAt({ x, y }).value_or(processor_.projectState().ui.selectedPad);
    const juce::File file(files[0]);
    const auto result = isAudioFile(file) ? processor_.loadCustomSample(pad, file)
                                         : juce::Result::fail("Unsupported sample file type");
    if (result.wasOk())
    {
        selectPad(pad, true);
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
    play_.setToggleState(processor_.isStandalonePlaying(), juce::dontSendNotification);
    drumBrush_.setToggleState(brush_ == Brush::drum, juce::dontSendNotification);
    slicerBrush_.setToggleState(brush_ == Brush::slicer, juce::dontSendNotification);
    velocityBrush_.setToggleState(brush_ == Brush::velocity, juce::dontSendNotification);
    randomizerBrush_.setToggleState(brush_ == Brush::randomizer, juce::dontSendNotification);
    undo_.setEnabled(processor_.projectEditor().canUndo());
    redo_.setEnabled(processor_.projectEditor().canRedo());
    repaint();
}
