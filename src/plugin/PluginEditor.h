#pragma once

#include "plugin/BeatScholarLookAndFeel.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>
#include <vector>

class BeatScholarLinuxEditor final : public juce::AudioProcessorEditor,
                                     public juce::FileDragAndDropTarget,
                                     private juce::Timer
{
public:
    explicit BeatScholarLinuxEditor(BeatScholarLinuxProcessor&);
    ~BeatScholarLinuxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;
    // The same MIDI file is used for drag-export and explicit file export.
    [[nodiscard]] juce::File writePatternMidi(std::size_t pattern,
                                             juce::File directory = {}) const;

private:
    enum class Brush { drum, slicer, velocity, randomizer };
    enum class Module { soundCharacter, soundEffects, randomizer, master };
    struct BeatHit { std::size_t lane; std::size_t beat; std::size_t slice; };
    struct BarMeterEntry
    {
        std::size_t pattern;
        std::size_t bar;
        bool denominator;
        std::uint64_t stateRevision;
    };
    struct BarClipboard
    {
        bsl::BarState meter;
        std::vector<std::vector<bsl::BeatState>> lanes;
    };

    class EffectSlider final : public juce::Slider
    {
    public:
        void mouseDown(const juce::MouseEvent& event) override
        {
            mouseGesture = event.mods.isLeftButtonDown();
            dragAnchorX = event.position.x / static_cast<float>(juce::jmax(1, getWidth()));
            juce::Slider::mouseDown(event);
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            juce::Slider::mouseUp(event);
            mouseGesture = false;
        }
        bool mouseGesture = false;
        float dragAnchorX = 0.5f;
    };

    class RoutingButton final : public juce::TextButton
    {
        void mouseUp(const juce::MouseEvent& event) override
        {
            const juce::ScopedValueSetter<bool> reset(resetting_, event.mods.isAltDown());
            juce::TextButton::mouseUp(event);
        }
        using juce::TextButton::clicked;
        void clicked(const juce::ModifierKeys&) override
        {
            // Restore before ButtonAttachment observes the click, without a second click.
            if (resetting_)
                setToggleState(false, juce::dontSendNotification);
        }
        bool resetting_ = false;
    };

    void timerCallback() override;
    void updateMomentaryBrush();
    void resetOrToggle(bool& value, bool defaultValue, const juce::ModifierKeys&);
    void configureButton(juce::TextButton&, const juce::String&, std::function<void()> action);
    void configureSlider(juce::Slider&, double minimum, double maximum, double interval,
                         const juce::String& suffix);
    void selectPad(std::size_t pad, bool audition);
    void updateControlsFromState();
    void cancelPatternInteraction();
    void cancelNumericEntry();
    void refreshRestoredState(juce::Slider* editedControl = nullptr);
    void applyModuleControls(juce::Slider& changedControl);
    void restoreHistory(bool redo);
    void checkpointModuleControl(bool masterGain = false);
    void syncBrushButtons();
    void configureModuleControls();
    void updateEffectDragValue();
    void showPresetMenu();
    void savePresetAs();
    void showKitPresetMenu();
    void showRandomizerPresetMenu();
    void saveKitAs();
    void showLaneMenu(std::size_t lane);
    void copyBar(std::size_t bar);
    void pasteBar(std::size_t bar);
    void copyPattern(std::size_t pattern);
    void pastePattern(std::size_t pattern);
    void showBarMenu(std::size_t bar);
    void showPatternQuantizationMenu();
    void showPatternMenu(std::size_t pattern);
    void showPadMenu(std::size_t pad);
    void chooseCustomSample(std::size_t pad);
    void showFactorySampleMenu(std::size_t pad, std::size_t layer);
    void showRandomizerSelectionMenu();
    void showSubdivisionMenu();
    void showSwingReferenceMenu();
    void showAbout();
    void loadAdjacentPreset(int delta);
    void loadAdjacentKit(int delta);
    void loadAdjacentRandomizer(int delta);
    void showBeatMenu(const BeatHit&);
    void applyBrush(const BeatHit&, const juce::ModifierKeys&);
    void pasteBeat(const BeatHit&);
    void randomizeBeatInStroke(const BeatHit&);
    void paintPattern(juce::Graphics&, juce::Rectangle<float>);
    [[nodiscard]] juce::Rectangle<int> patternBounds() const;
    [[nodiscard]] juce::Rectangle<float> laneCellBounds(std::size_t lane) const;
    [[nodiscard]] juce::Rectangle<float> laneControlBounds(std::size_t lane, int control) const;
    [[nodiscard]] juce::Rectangle<int> kitBounds() const;
    [[nodiscard]] juce::Rectangle<int> barHeaderBounds() const;
    [[nodiscard]] juce::Rectangle<int> arrangementBounds() const;
    [[nodiscard]] bool shortPatternList() const;
    [[nodiscard]] juce::Rectangle<int> arrangementPadBounds() const;
    [[nodiscard]] int arrangementItemHeight() const;
    [[nodiscard]] juce::Rectangle<int> arrangementItemBounds(std::size_t) const;
    [[nodiscard]] float maximumArrangementScroll() const;
    [[nodiscard]] int arrangementActionAt(juce::Point<int>, std::size_t) const;
    void clampView();
    [[nodiscard]] juce::Rectangle<float> scrollRailBounds(int axis) const;
    [[nodiscard]] juce::Rectangle<float> scrollThumbBounds(int axis) const;
    [[nodiscard]] juce::Rectangle<float> zoomRailBounds(int axis) const;
    [[nodiscard]] juce::Rectangle<int> zoomButtonBounds(int axis, bool increase) const;
    void moveScrollRail(juce::Point<int>);
    void moveZoomRail(juce::Point<int>);
    [[nodiscard]] juce::Rectangle<int> lowerBounds() const;
    [[nodiscard]] juce::Rectangle<int> soundBounds() const;
    [[nodiscard]] juce::Rectangle<int> masterBounds() const;
    [[nodiscard]] std::optional<BeatHit> beatAt(juce::Point<int>, bool insidePieOnly = false) const;
    [[nodiscard]] std::optional<std::size_t> padAt(juce::Point<int>) const;
    [[nodiscard]] std::optional<std::size_t> laneAt(juce::Point<int>) const;
    [[nodiscard]] std::optional<std::size_t> barAt(juce::Point<int>, bool arrangement) const;
    [[nodiscard]] std::optional<bool> barMeterDenominatorAt(juce::Point<int>, std::size_t) const;
    [[nodiscard]] std::optional<std::size_t> patternAt(juce::Point<int>) const;

    BeatScholarLinuxProcessor& processor_;
    bsl::ui::BeatScholarLookAndFeel lookAndFeel_;
    std::array<juce::TextButton, bsl::padCount> pads_;
    std::array<juce::Colour, bsl::padCount> padColours_;
    juce::Slider swing_;
    juce::Slider bpm_;
    juce::Slider defaultVelocity_;
    RoutingButton routing_;
    juce::Label title_;
    juce::Label status_;
    juce::TextButton preset_;
    juce::TextButton presetPrevious_;
    juce::TextButton presetNext_;
    juce::TextButton presetSave_;
    juce::TextButton sequencer_;
    juce::TextButton sampler_;
    juce::TextButton play_;
    juce::TextButton pause_;
    juce::TextButton stop_;
    juce::TextButton drumBrush_;
    juce::TextButton slicerBrush_;
    juce::TextButton velocityBrush_;
    juce::TextButton randomizerBrush_;
    juce::TextButton undo_;
    juce::TextButton redo_;
    juce::TextButton about_;
    juce::TextButton barsPatterns_;
    juce::TextButton addItem_;
    juce::TextButton duplicateItem_;
    juce::TextButton removeItem_;
    juce::TextButton soundTab_;
    juce::TextButton randomizerTab_;
    juce::TextButton masterTab_;
    juce::TextButton auditionDuringPlayback_;
    juce::TextButton kitLocked_;
    juce::TextButton kitMenu_;
    juce::TextButton kitSave_;
    juce::TextButton midiMap_;
    juce::TextButton kitPrevious_;
    juce::TextButton kitNext_;
    juce::TextButton randomizerPrevious_;
    juce::TextButton randomizerNext_;
    juce::TextButton loadSample_;
    juce::TextButton clearSample_;
    juce::TextButton layerOne_;
    juce::TextButton layerTwo_;
    juce::TextButton padSolo_;
    juce::TextButton padMute_;
    juce::TextButton oneShot_;
    juce::TextButton mono_;
    juce::TextButton randomClearPads_;
    juce::TextButton randomAllPads_;
    juce::TextButton randomFillEmpty_;
    juce::TextButton randomClearSlices_;
    juce::TextButton randomAllSlices_;
    juce::TextButton randomSlicePrevious_;
    juce::TextButton randomSliceNext_;
    std::array<juce::TextButton, 9> randomSliceButtons_;
    std::array<juce::TextButton, 4> randomModuleToggles_;
    std::array<juce::Slider, 9> soundControls_;
    std::array<juce::Label, 9> soundLabels_;
    std::array<EffectSlider, 10> effectControls_;
    std::array<juce::Label, 10> effectLabels_;
    juce::Label effectDragValue_;
    int effectDragIndex_ = -1;
    std::array<juce::Slider, 5> masterControls_;
    std::array<juce::Label, 5> masterLabels_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> routingAttachment_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::TextEditor numericEntry_;
    juce::Component::SafePointer<juce::Slider> numericEntryControl_;
    std::optional<BarMeterEntry> numericEntryBar_;
    Brush brush_ = Brush::drum;
    Brush previousBrush_ = Brush::drum;
    Module module_ = Module::soundCharacter;
    std::size_t requestedSubdivision_ = 1;
    std::optional<BeatHit> lastHit_;
    std::optional<juce::Point<int>> beatHoverPosition_;
    std::optional<bsl::BeatState> beatClipboard_;
    std::optional<BarClipboard> barClipboard_;
    std::optional<bsl::PatternState> patternClipboard_;
    bool updatingControls_ = false;
    std::array<float, bsl::padCount> padMeterLevels_ {};
    std::array<float, 2> outputMeterLevels_ {};
    bool masterGainGesture_ = false;
    bool moduleControlGesture_ = false;
    bool moduleControlCheckpointed_ = false;
    std::uint64_t observedStateRevision_ = 0;
    bool draggingBeatPaste_ = false;
    bool draggingRandomizer_ = false;
    std::vector<BeatHit> randomizedBeats_;
    bool draggingVelocity_ = false;
    bool draggingSubdivision_ = false;
    bool beatDragCheckpointed_ = false;
    int dragStartY_ = 0;
    float dragStartVelocity_ = 0.8f;
    std::size_t dragStartSubdivision_ = 1;
    float horizontalZoom_ = 1.0f;
    float verticalZoom_ = 1.0f;
    float horizontalScroll_ = 0.0f;
    float verticalScroll_ = 0.0f;
    float arrangementScroll_ = 0.0f;
    int draggingScrollRail_ = 0;
    float scrollRailGrabOffset_ = 0.0f;
    int draggingZoomRail_ = 0;
    bool draggingGrid_ = false;
    juce::Point<int> gridDragStart_;
    float gridDragStartHorizontal_ = 0.0f;
    float gridDragStartVertical_ = 0.0f;
    bool midiDragPending_ = false;
    std::size_t midiDragPattern_ = 0;
    bool momentaryBrushActive_ = false;
    bool momentaryAltActive_ = false;
    char momentaryKey_ = 0;
    Brush momentaryBrushPrevious_ = Brush::drum;
    Module momentaryModulePrevious_ = Module::soundCharacter;
    std::size_t momentarySubdivisionPrevious_ = 1;
    bool draggingBarMeter_ = false;
    bool barMeterDragChanged_ = false;
    bool draggingBarDenominator_ = false;
    std::size_t draggingBarIndex_ = 0;
    int dragStartBarNumerator_ = 4;
    int dragStartBarDenominator_ = 4;
    juce::File currentPresetFile() const { return juce::File(juce::String(processor_.projectState().ui.presetFile)); }
    juce::File currentKitFile() const { return juce::File(juce::String(processor_.projectState().ui.kitFile)); }
    juce::File currentRandomizerFile() const { return juce::File(juce::String(processor_.projectState().ui.randomizerFile)); }
    int randomSlicePage_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatScholarLinuxEditor)
};
