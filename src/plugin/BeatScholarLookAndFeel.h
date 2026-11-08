#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bsl::ui {

namespace colour {
inline constexpr auto background = 0xff0e0e2dU;
inline constexpr auto panel = 0xff131332U;
inline constexpr auto panelRaised = 0xff202052U;
inline constexpr auto header = 0xff272761U;
inline constexpr auto grid = 0xff1c1c4cU;
inline constexpr auto border = 0xff343474U;
inline constexpr auto borderBright = 0xff7778bcU;
inline constexpr auto text = 0xffe7e7f7U;
inline constexpr auto textMuted = 0xff9292bdU;
inline constexpr auto accent = 0xff6668baU;
inline constexpr auto accentBright = 0xff8588ecU;
inline constexpr auto magenta = 0xffbf316bU;
inline constexpr auto green = 0xff24c86aU;
inline constexpr auto meter = 0xffa7f000U;
} // namespace colour

struct Palette
{
    juce::uint32 background = colour::background;
    juce::uint32 panel = colour::panel;
    juce::uint32 panelRaised = colour::panelRaised;
    juce::uint32 header = colour::header;
    juce::uint32 grid = colour::grid;
    juce::uint32 border = colour::border;
    juce::uint32 borderBright = colour::borderBright;
    juce::uint32 text = colour::text;
    juce::uint32 textMuted = colour::textMuted;
    juce::uint32 accent = colour::accent;
    juce::uint32 accentBright = colour::accentBright;
    juce::uint32 magenta = colour::magenta;
    juce::uint32 green = colour::green;
    juce::uint32 meter = colour::meter;
};

[[nodiscard]] juce::Font font(float height, bool heavy = false);
void drawInstrumentGlyph(juce::Graphics&, juce::Rectangle<float> icon, int pad,
                         juce::Colour tint, float stroke);

class BeatScholarLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    BeatScholarLookAndFeel();
    void setDarkMode(bool);
    [[nodiscard]] const Palette& palette() const noexcept { return palette_; }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool isMouseOverButton,
                        bool isButtonDown) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

private:
    Palette palette_;
    bool darkMode_ = false;
    static void drawTriangle(juce::Graphics&, juce::Rectangle<float>, bool pointsRight,
                             juce::Colour);
    void drawDice(juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    static void drawUndo(juce::Graphics&, juce::Rectangle<float>, bool clockwise,
                         juce::Colour);
};

} // namespace bsl::ui
