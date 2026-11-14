#include "plugin/BeatScholarLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace bsl::ui {

namespace {
void drawKeyboardFocus(juce::Graphics& g, const juce::Component& component)
{
    if (component.hasKeyboardFocus(true))
    {
        g.setColour(juce::Colour(colour::text));
        g.drawRect(component.getLocalBounds().toFloat().reduced(1.0f), 2.0f);
    }
}
}

juce::Font font(float height, bool heavy)
{
    return juce::Font(juce::FontOptions("Lato", heavy ? "Heavy" : "Regular", height));
}

void drawInstrumentGlyph(juce::Graphics& g, juce::Rectangle<float> icon, int pad,
                         juce::Colour tint, float stroke)
{
    g.setColour(tint);
    if (pad >= 9 && pad <= 12)
    {
        const float heightRatio = pad == 12 ? 1.0f : 0.85f;
        const float width = std::min(icon.getWidth(), icon.getHeight() / heightRatio);
        icon = icon.withSizeKeepingCentre(width, width * heightRatio);
    }
    const auto drawDrum = [&g, stroke](juce::Rectangle<float> box, bool sticks) {
        // Samples & Sounds 2:35: shallow shell, lugs, and distinct strike marks.
        const float width = std::min(box.getWidth(), box.getHeight());
        box = box.withSizeKeepingCentre(width, width);
        const auto point = [&box](float x, float y) {
            return juce::Point<float>(box.getX() + box.getWidth() * x,
                                      box.getY() + box.getHeight() * y);
        };
        const float lineWidth = stroke * 0.65f;
        const auto shell = juce::Rectangle<float>(point(0.03f, 0.59f), point(0.97f, 0.91f));
        g.drawRoundedRectangle(shell, width * 0.025f, lineWidth);
        g.drawLine({ point(0.05f, 0.64f), point(0.95f, 0.64f) }, lineWidth);
        g.drawLine({ point(0.05f, 0.87f), point(0.95f, 0.87f) }, lineWidth);
        for (const float x : { 0.12f, 0.37f, 0.63f, 0.88f })
            g.drawLine({ point(x, 0.69f), point(x, 0.82f) }, stroke);
        if (sticks)
        {
            g.drawLine({ point(0.32f, 0.50f), point(1.0f, 0.27f) }, stroke);
            g.drawLine({ point(0.18f, 0.40f), point(0.09f, 0.30f) }, lineWidth);
            g.drawLine({ point(0.25f, 0.35f), point(0.22f, 0.20f) }, lineWidth);
            g.drawLine({ point(0.33f, 0.35f), point(0.37f, 0.22f) }, lineWidth);
        }
        else
        {
            for (const float radius : { 0.12f, 0.20f, 0.28f })
            {
                juce::Path wave;
                wave.addCentredArc(point(0.5f, 0.39f).x, point(0.5f, 0.39f).y,
                                   width * radius, width * radius, 0.0f,
                                   -juce::MathConstants<float>::pi * 0.30f,
                                   juce::MathConstants<float>::pi * 0.30f, true);
                g.strokePath(wave, juce::PathStrokeType(lineWidth));
            }
            g.drawLine({ point(0.13f, 0.49f), point(0.87f, 0.49f) }, lineWidth);
        }
    };
    const auto drawCymbal = [&g, stroke](juce::Rectangle<float> box, float tilt) {
        const float slope = tilt / box.getHeight();
        const float width = std::min(box.getWidth(), box.getHeight());
        box = box.withSizeKeepingCentre(width, width);
        const auto centre = box.getCentre();
        const float rise = slope * width;
        // Shallow disc and central bell, as in Samples & Sounds 2:35.
        juce::Path disc;
        disc.startNewSubPath(box.getX(), centre.y + rise);
        disc.lineTo(centre.x - width * 0.10f, centre.y - width * 0.09f);
        disc.lineTo(centre.x, centre.y - width * 0.15f);
        disc.lineTo(centre.x + width * 0.10f, centre.y - width * 0.09f);
        disc.lineTo(box.getRight(), centre.y - rise);
        disc.lineTo(centre.x, centre.y + width * 0.07f);
        disc.closeSubPath();
        g.fillPath(disc);
        g.drawLine(centre.x, centre.y - width * 0.23f,
                   centre.x, centre.y - width * 0.15f, stroke * 0.65f);
        g.drawLine(centre.x, centre.y + width * 0.07f,
                   centre.x, box.getBottom(), stroke * 0.65f);
    };
    switch (pad)
    {
        case 0:
        {
            const auto drum = icon.withSizeKeepingCentre(std::min(icon.getWidth(), icon.getHeight()) * 0.72f,
                                                         std::min(icon.getWidth(), icon.getHeight()) * 0.72f);
            g.drawEllipse(drum, stroke);
            g.drawEllipse(drum.withSizeKeepingCentre(drum.getWidth() * 0.26f,
                                                     drum.getHeight() * 0.26f), stroke);
            const auto beater = drum.withSizeKeepingCentre(drum.getWidth() * 0.13f,
                                                            drum.getHeight() * 0.13f)
                                    .translated(0.0f, drum.getHeight() * 0.32f);
            g.drawEllipse(beater, stroke);
            juce::Path pedal;
            pedal.startNewSubPath(drum.getCentreX() - drum.getWidth() * 0.08f, beater.getBottom());
            pedal.lineTo(drum.getCentreX() + drum.getWidth() * 0.08f, beater.getBottom());
            pedal.lineTo(drum.getCentreX() + drum.getWidth() * 0.05f,
                         drum.getBottom() + drum.getHeight() * 0.18f);
            pedal.lineTo(drum.getCentreX() - drum.getWidth() * 0.05f,
                         drum.getBottom() + drum.getHeight() * 0.18f);
            pedal.closeSubPath();
            g.strokePath(pedal, juce::PathStrokeType(stroke));
            break;
        }
        case 1: drawDrum(icon, true); break;
        case 2: drawDrum(icon, false); break;
        case 3:
        {
            const float width = std::min(icon.getWidth(), icon.getHeight());
            const auto box = icon.withSizeKeepingCentre(width, width);
            const auto point = [&box, width](float x, float y) {
                return juce::Point<float>(box.getX() + width * x, box.getY() + width * y);
            };
            // Rounded fingers and an angled palm, rather than a vertical comb.
            juce::Path hand;
            hand.startNewSubPath(point(0.34f, 0.51f));
            hand.lineTo(point(0.61f, 0.22f));
            hand.quadraticTo(point(0.68f, 0.16f), point(0.71f, 0.23f));
            hand.lineTo(point(0.43f, 0.55f));
            hand.lineTo(point(0.73f, 0.28f));
            hand.quadraticTo(point(0.80f, 0.24f), point(0.81f, 0.32f));
            hand.lineTo(point(0.52f, 0.62f));
            hand.lineTo(point(0.80f, 0.39f));
            hand.quadraticTo(point(0.87f, 0.36f), point(0.85f, 0.44f));
            hand.lineTo(point(0.59f, 0.70f));
            hand.lineTo(point(0.76f, 0.56f));
            hand.quadraticTo(point(0.83f, 0.54f), point(0.80f, 0.62f));
            hand.lineTo(point(0.48f, 0.91f));
            hand.cubicTo(point(0.29f, 1.04f), point(0.09f, 0.85f), point(0.14f, 0.66f));
            hand.lineTo(point(0.30f, 0.36f));
            hand.quadraticTo(point(0.34f, 0.30f), point(0.38f, 0.37f));
            hand.lineTo(point(0.30f, 0.56f));
            g.strokePath(hand, juce::PathStrokeType(stroke * 0.65f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path rearHand;
            rearHand.startNewSubPath(point(0.20f, 0.87f));
            rearHand.cubicTo(point(0.04f, 0.77f), point(0.05f, 0.65f), point(0.12f, 0.52f));
            rearHand.lineTo(point(0.24f, 0.30f));
            rearHand.quadraticTo(point(0.30f, 0.24f), point(0.33f, 0.32f));
            rearHand.lineTo(point(0.54f, 0.15f));
            rearHand.quadraticTo(point(0.60f, 0.11f), point(0.63f, 0.20f));
            g.strokePath(rearHand, juce::PathStrokeType(stroke * 0.55f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine({ point(0.77f, 0.16f), point(0.80f, 0.04f) }, stroke * 0.55f);
            g.drawLine({ point(0.83f, 0.20f), point(0.92f, 0.11f) }, stroke * 0.55f);
            g.drawLine({ point(0.87f, 0.26f), point(0.99f, 0.23f) }, stroke * 0.55f);
            break;
        }
        case 4: drawCymbal(icon, 0.0f); break;
        case 5:
        {
            // Open hi-hat: two separated discs, not a disc over a straight bar.
            const float width = std::min(icon.getWidth(), icon.getHeight());
            const auto box = icon.withSizeKeepingCentre(width, width);
            drawCymbal(box.translated(0.0f, -width * 0.08f), 0.0f);
            const auto centre = box.getCentre();
            juce::Path lowerDisc;
            lowerDisc.startNewSubPath(box.getX(), centre.y + width * 0.10f);
            lowerDisc.lineTo(centre.x, centre.y + width * 0.04f);
            lowerDisc.lineTo(box.getRight(), centre.y + width * 0.10f);
            lowerDisc.lineTo(centre.x, centre.y + width * 0.17f);
            lowerDisc.closeSubPath();
            g.fillPath(lowerDisc);
            // Small vibration marks distinguish the open hat in the reference.
            const auto point = [&box](float x, float y) {
                return juce::Point<float>(box.getX() + box.getWidth() * x,
                                          box.getY() + box.getHeight() * y);
            };
            for (const auto offset : { juce::Point<float>(0.03f, 0.23f),
                                       juce::Point<float>(0.86f, 0.68f) })
            {
                juce::Path motion;
                motion.startNewSubPath(point(offset.x, offset.y));
                motion.lineTo(point(offset.x + 0.03f, offset.y + 0.06f));
                motion.lineTo(point(offset.x + 0.09f, offset.y + 0.03f));
                motion.lineTo(point(offset.x + 0.12f, offset.y + 0.09f));
                g.strokePath(motion, juce::PathStrokeType(stroke * 0.55f));
            }
            break;
        }
        case 6: drawCymbal(icon, icon.getHeight() * 0.11f); break;
        case 7:
        {
            const float width = std::min(icon.getWidth(), icon.getHeight());
            const auto box = icon.withSizeKeepingCentre(width, width);
            // Leave room for the pedal below the stand, as in Samples & Sounds.
            drawCymbal(box.withTrimmedBottom(width * 0.18f), -width * 0.04f);
            const auto point = [&box, width](float x, float y) {
                return juce::Point<float>(box.getX() + width * x, box.getY() + width * y);
            };
            juce::Path pedal;
            pedal.startNewSubPath(point(0.48f, 0.76f));
            pedal.lineTo(point(0.54f, 0.79f));
            pedal.lineTo(point(0.40f, 0.98f));
            pedal.lineTo(point(0.35f, 0.96f));
            pedal.closeSubPath();
            g.fillPath(pedal);
            g.drawLine({ point(0.55f, 0.78f), point(0.64f, 0.81f) }, stroke * 0.65f);
            break;
        }
        case 8: drawCymbal(icon, icon.getHeight() * 0.14f); break;
        case 9:
        case 10:
        case 11:
        {
            // Samples & Sounds 2:35: the same kit, with the selected tom filled.
            const auto box = [&icon](float x, float y, float w, float h) {
                return juce::Rectangle<float>(icon.getX() + x * icon.getWidth(),
                                              icon.getY() + y * icon.getHeight(),
                                              w * icon.getWidth(), h * icon.getHeight());
            };
            const auto tom = [&g, stroke, &box](float x, float y, float w, float h,
                                               bool selected) {
                const auto shell = box(x, y, w, h);
                const float rimHeight = shell.getHeight() * 0.32f;
                juce::Path body;
                body.startNewSubPath(shell.getX(), shell.getY() + rimHeight * 0.5f);
                body.lineTo(shell.getX(), shell.getBottom() - rimHeight * 0.5f);
                body.cubicTo(shell.getX(), shell.getBottom() + rimHeight * 0.15f,
                             shell.getRight(), shell.getBottom() + rimHeight * 0.15f,
                             shell.getRight(), shell.getBottom() - rimHeight * 0.5f);
                body.lineTo(shell.getRight(), shell.getY() + rimHeight * 0.5f);
                if (selected)
                {
                    body.closeSubPath();
                    g.fillPath(body);
                    g.fillEllipse(shell.withHeight(rimHeight));
                }
                else
                {
                    g.strokePath(body, juce::PathStrokeType(stroke * 0.65f));
                    g.drawEllipse(shell.withHeight(rimHeight), stroke * 0.65f);
                }
            };
            tom(0.02f, 0.08f, 0.32f, 0.30f, pad == 9);
            tom(0.40f, 0.08f, 0.32f, 0.30f, pad == 10);
            g.drawEllipse(box(0.16f, 0.47f, 0.44f, 0.47f), stroke * 0.65f);
            tom(0.65f, 0.48f, 0.33f, 0.41f, pad == 11);
            g.drawLine(icon.getX() + icon.getWidth() * 0.21f, icon.getBottom() - stroke,
                       icon.getX() + icon.getWidth() * 0.16f, icon.getBottom(), stroke * 0.65f);
            g.drawLine(icon.getX() + icon.getWidth() * 0.56f, icon.getBottom() - stroke,
                       icon.getX() + icon.getWidth() * 0.61f, icon.getBottom(), stroke * 0.65f);
            break;
        }
        case 12:
        {
            // Paired maracas: heads above, handles angled inward, with a rim gap.
            for (const float side : { -1.0f, 1.0f })
            {
                const auto centre = juce::Point<float>(icon.getCentreX() + side * icon.getWidth() * 0.28f,
                                                       icon.getY() + icon.getHeight() * (side < 0 ? 0.22f : 0.43f));
                const auto rotation = juce::AffineTransform::rotation(side * 0.48f, centre.x, centre.y);
                const auto head = juce::Rectangle<float>(icon.getWidth() * 0.25f,
                                                         icon.getHeight() * 0.35f).withCentre(centre);
                juce::Path upper, handle;
                upper.addEllipse(head);
                // Clip the two halves separately so the stripe stays transparent.
                {
                    juce::Graphics::ScopedSaveState saved(g);
                    g.addTransform(rotation);
                    g.reduceClipRegion(head.withHeight(head.getHeight() * 0.47f).toNearestInt());
                    g.fillPath(upper);
                }
                {
                    juce::Graphics::ScopedSaveState saved(g);
                    g.addTransform(rotation);
                    g.reduceClipRegion(head.withTrimmedTop(head.getHeight() * 0.57f).toNearestInt());
                    g.fillPath(upper);
                }
                handle.addRoundedRectangle(centre.x - icon.getWidth() * 0.026f,
                                           head.getBottom() + icon.getHeight() * 0.03f,
                                           icon.getWidth() * 0.052f, icon.getHeight() * 0.37f,
                                           icon.getWidth() * 0.026f);
                g.fillPath(handle, rotation);
            }
            break;
        }
        case 13:
        {
            const float width = std::min(icon.getWidth(), icon.getHeight());
            const auto box = icon.withSizeKeepingCentre(width, width);
            const auto point = [&box, width](float x, float y) {
                return juce::Point<float>(box.getX() + width * x, box.getY() + width * y);
            };
            // The reference triangle has an open lower-right corner and a beater.
            juce::Path triangle;
            triangle.startNewSubPath(point(0.81f, 0.85f));
            triangle.lineTo(point(0.12f, 0.85f));
            triangle.lineTo(point(0.51f, 0.17f));
            triangle.lineTo(point(0.87f, 0.77f));
            g.strokePath(triangle, juce::PathStrokeType(stroke * 0.85f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawEllipse(juce::Rectangle<float>(width * 0.08f, width * 0.08f)
                              .withCentre(point(0.51f, 0.12f)), stroke * 0.55f);
            g.setColour(tint.withMultipliedAlpha(0.6f));
            g.drawLine({ point(0.55f, 0.55f), point(0.94f, 0.27f) }, stroke * 0.65f);
            g.setColour(tint);
            g.fillEllipse(juce::Rectangle<float>(width * 0.09f, width * 0.09f)
                              .withCentre(point(0.55f, 0.55f)));
            break;
        }
        case 14:
        {
            const float width = std::min(icon.getWidth(), icon.getHeight());
            for (int ring = 0; ring < 4; ++ring)
            {
                const float diameter = width * (0.82f - ring * 0.20f);
                g.setColour(tint.withMultipliedAlpha(0.18f + ring * 0.27f));
                g.drawEllipse(icon.withSizeKeepingCentre(diameter, diameter), stroke * 0.85f);
            }
            break;
        }
        default:
        {
            const float width = std::min(icon.getWidth(), icon.getHeight());
            const auto box = icon.withSizeKeepingCentre(width, width);
            // Unevenly spaced, varied sparkles from Samples & Sounds 2:35.
            const float sparkles[][3] = {
                { 0.41f, 0.12f, 0.070f }, { 0.73f, 0.28f, 0.045f },
                { 0.13f, 0.45f, 0.085f }, { 0.45f, 0.50f, 0.040f },
                { 0.73f, 0.57f, 0.090f }, { 0.96f, 0.39f, 0.030f },
                { 0.30f, 0.81f, 0.060f }, { 0.69f, 0.80f, 0.045f }
            };
            for (const auto& sparkle : sparkles)
            {
                juce::Path star;
                star.addStar({ box.getX() + width * sparkle[0],
                               box.getY() + width * sparkle[1] },
                             4, width * sparkle[2] * 0.38f, width * sparkle[2]);
                g.fillPath(star);
            }
            break;
        }
    }
}

BeatScholarLookAndFeel::BeatScholarLookAndFeel()
{
    setDarkMode(false);
}

void BeatScholarLookAndFeel::setDarkMode(bool enabled)
{
    darkMode_ = enabled;
    palette_ = Palette {};
    if (enabled)
    {
        palette_.background = 0xff050505;
        palette_.panel = 0xff0f0f0f;
        palette_.panelRaised = 0xff232323;
        palette_.header = 0xff232323;
        palette_.grid = 0xff191919;
        palette_.border = 0xff3c3c3c;
        palette_.borderBright = 0xff747474;
    }
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(palette_.panelRaised));
    setColour(juce::PopupMenu::textColourId, juce::Colour(palette_.text));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(palette_.accent));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(palette_.text));
    setColour(juce::Label::textColourId, juce::Colour(palette_.text));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(palette_.text));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(palette_.panel));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(palette_.borderBright));
    setColour(juce::ComboBox::textColourId, juce::Colour(palette_.text));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(palette_.textMuted));
}

juce::Font BeatScholarLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return font(std::clamp(static_cast<float>(buttonHeight) * 0.42f, 9.0f, 17.0f), true);
}

juce::Font BeatScholarLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont().getHeight() > 0.0f ? label.getFont() : font(12.0f);
}

juce::Font BeatScholarLookAndFeel::getPopupMenuFont()
{
    return font(15.0f, true);
}

void BeatScholarLookAndFeel::drawTriangle(juce::Graphics& g, juce::Rectangle<float> area,
                                           bool pointsRight, juce::Colour tint)
{
    area = area.reduced(area.getWidth() * 0.22f, area.getHeight() * 0.15f);
    juce::Path path;
    if (pointsRight)
    {
        path.startNewSubPath(area.getX(), area.getY());
        path.lineTo(area.getRight(), area.getCentreY());
        path.lineTo(area.getX(), area.getBottom());
    }
    else
    {
        path.startNewSubPath(area.getRight(), area.getY());
        path.lineTo(area.getX(), area.getCentreY());
        path.lineTo(area.getRight(), area.getBottom());
    }
    path.closeSubPath();
    g.setColour(tint);
    g.fillPath(path);
}

void BeatScholarLookAndFeel::drawDice(juce::Graphics& g, juce::Rectangle<float> area,
                                       juce::Colour tint)
{
    area = area.reduced(std::min(area.getWidth(), area.getHeight()) * 0.18f);
    const auto square = area.withSizeKeepingCentre(std::min(area.getWidth(), area.getHeight()),
                                                   std::min(area.getWidth(), area.getHeight()));
    g.setColour(tint);
    g.fillRoundedRectangle(square, square.getWidth() * 0.12f);
    g.setColour(juce::Colour(palette_.background));
    const float diameter = square.getWidth() * 0.14f;
    for (const auto point : { juce::Point<float>(0.27f, 0.27f), { 0.73f, 0.27f },
                              { 0.50f, 0.50f }, { 0.27f, 0.73f }, { 0.73f, 0.73f } })
        g.fillEllipse(square.getX() + square.getWidth() * point.x - diameter * 0.5f,
                      square.getY() + square.getHeight() * point.y - diameter * 0.5f,
                      diameter, diameter);
}

void BeatScholarLookAndFeel::drawUndo(juce::Graphics& g, juce::Rectangle<float> area,
                                       bool clockwise, juce::Colour tint)
{
    const auto centre = area.getCentre();
    const float radius = std::min(area.getWidth(), area.getHeight()) * 0.28f;
    juce::Path arc;
    const float start = clockwise ? -2.4f : -0.7f;
    const float end = clockwise ? 0.7f : 2.4f;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(tint);
    g.strokePath(arc, juce::PathStrokeType(std::max(1.5f, radius * 0.18f),
                                           juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    auto head = juce::Rectangle<float>(radius * 0.65f, radius * 0.65f)
                    .withCentre({ centre.x + (clockwise ? -radius : radius) * 0.72f,
                                  centre.y - radius * 0.43f });
    drawTriangle(g, head, !clockwise, tint);
}

void BeatScholarLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour&,
                                                   bool isMouseOver, bool isDown)
{
    const auto id = button.getComponentID();
    auto area = button.getLocalBounds().toFloat();
    if (id.startsWith("pad."))
    {
        g.setColour(juce::Colour(palette_.panel));
        g.fillRect(area);
        g.setColour(button.findColour(juce::TextButton::buttonOnColourId)
                        .withSaturation(0.78f).withBrightness(0.82f));
        g.fillRect(area.removeFromTop(std::max(3.0f, area.getHeight() * 0.08f)));
        if (button.getToggleState())
        {
            g.setColour(button.findColour(juce::TextButton::buttonOnColourId).withAlpha(0.22f));
            g.fillRect(button.getLocalBounds().toFloat().reduced(1.0f));
        }
        g.setColour(button.getToggleState() ? juce::Colour(palette_.text)
                                             : juce::Colour(palette_.border));
        g.drawRect(button.getLocalBounds().toFloat().reduced(0.75f),
                   button.getToggleState() ? 2.0f : 1.0f);
        if (isMouseOver || isDown)
        {
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.16f : 0.08f));
            g.fillRect(button.getLocalBounds().toFloat().reduced(1.0f));
        }
        return;
    }

    if (id == "routing.mode")
    {
        if (isMouseOver || isDown)
        {
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.16f : 0.07f));
            g.fillRect(area.reduced(1.0f));
        }
        return;
    }

    if (id.startsWith("toolbar.") || id.startsWith("brush."))
    {
        if (button.getToggleState() || isMouseOver || isDown)
        {
            g.setColour(juce::Colour(button.getToggleState() ? palette_.header : palette_.panelRaised)
                            .withAlpha(isDown ? 0.75f : 1.0f));
            g.fillRoundedRectangle(area.reduced(1.0f), 2.0f);
        }
        return;
    }

    if (id == "sample.mono")
    {
        g.setColour(juce::Colour(button.getToggleState() ? palette_.text : palette_.borderBright));
        g.drawRect(area.reduced(1.0f), std::max(1.0f, area.getHeight() / 26.0f));
        if (isMouseOver || isDown)
        {
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.16f : 0.07f));
            g.fillRect(area.reduced(2.0f));
        }
        return;
    }

    if (id.startsWith("random.module.") || id == "random.fillEmpty")
    {
        if (isMouseOver || isDown)
        {
            const auto diameter = std::min(area.getWidth(), area.getHeight()) * 0.85f;
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.18f : 0.07f));
            g.fillEllipse(area.withSizeKeepingCentre(diameter, diameter));
        }
        return;
    }

    if (id == "sample.layer")
    {
        g.setColour(juce::Colour(palette_.panel));
        g.fillRect(area);
        if (isMouseOver || isDown)
        {
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.18f : 0.07f));
            g.fillRect(area.reduced(1.0f));
        }
        g.setColour(juce::Colour(palette_.border));
        const auto stroke = std::max(1.0f, area.getHeight() / 26.0f);
        g.drawRect(area.reduced(stroke * 0.5f), stroke);
        const auto gutter = area.getHeight() * 0.72f;
        g.fillRect(area.getX() + gutter, area.getY(), stroke, area.getHeight());
        g.fillRect(area.getRight() - gutter - stroke, area.getY(), stroke, area.getHeight());
        return;
    }

    if (id.startsWith("sample.") || id == "kit.audition" || id == "kit.lock"
        || id == "master.settings")
    {
        if (isMouseOver || isDown || button.getToggleState())
        {
            g.setColour(juce::Colour(palette_.text)
                            .withAlpha(isDown ? 0.18f : button.getToggleState() ? 0.12f : 0.07f));
            g.fillRect(area.reduced(1.0f));
        }
        return;
    }

    if (id == "arrangement.toggle")
    {
        g.setColour(juce::Colour(palette_.text));
        g.fillRoundedRectangle(area.reduced(2.0f, 1.0f), std::max(2.0f, area.getHeight() * 0.22f));
        return;
    }

    auto fill = juce::Colour(palette_.panelRaised);
    if (button.getToggleState())
        fill = juce::Colour(palette_.accent);
    else if (isDown)
        fill = fill.brighter(0.18f);
    else if (isMouseOver)
        fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRect(area);
    g.setColour(button.getToggleState() ? juce::Colour(palette_.borderBright)
                                         : juce::Colour(palette_.border));
    g.drawRect(area.reduced(0.5f), 1.0f);
}

void BeatScholarLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                             bool, bool)
{
    drawKeyboardFocus(g, button);
    const auto id = button.getComponentID();
    const auto area = button.getLocalBounds().toFloat();
    auto tint = button.isEnabled() ? juce::Colour(palette_.text)
                                   : juce::Colour(palette_.textMuted).withAlpha(0.55f);
    if (id == "routing.mode")
    {
        g.setColour(tint);
        g.setFont(font(std::max(8.0f, area.getHeight() * 0.56f), false));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(2, 1),
                         juce::Justification::centred, 1);
        return;
    }
    if (id == "arrangement.toggle")
    {
        g.setColour(juce::Colour(palette_.panel));
        g.setFont(font(std::max(8.0f, area.getHeight() * 0.42f), true));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 1),
                         juce::Justification::centred, 1);
        return;
    }
    if (id.startsWith("pad."))
    {
        const int pad = id.fromFirstOccurrenceOf("pad.", false, false).getIntValue();
        auto icon = area.reduced(std::max(5.0f, area.getWidth() * 0.13f),
                                 std::max(6.0f, area.getHeight() * 0.14f));
        const float stroke = std::max(1.0f, std::min(area.getWidth(), area.getHeight()) * 0.035f);
        drawInstrumentGlyph(g, icon, pad, tint.withAlpha(0.90f), stroke);
        return;
    }
    if (id == "toolbar.play")
        return drawTriangle(g, area, true, button.getToggleState() ? juce::Colour(palette_.green) : tint);
    if (id == "toolbar.stop")
    {
        g.setColour(tint);
        const float side = std::min(area.getWidth(), area.getHeight()) * 0.48f;
        g.fillRect(juce::Rectangle<float>(side, side).withCentre(area.getCentre()));
        return;
    }
    if (id == "toolbar.pause")
    {
        g.setColour(tint);
        auto centre = area.withSizeKeepingCentre(area.getWidth() * 0.45f, area.getHeight() * 0.48f);
        g.fillRect(centre.removeFromLeft(centre.getWidth() * 0.35f));
        g.fillRect(centre.removeFromRight(centre.getWidth() * 0.54f));
        return;
    }
    if (id == "icon.prev" || id == "icon.next")
        return drawTriangle(g, area, id == "icon.next", tint);
    if (id == "icon.save")
    {
        auto disk = area.reduced(area.getWidth() * 0.22f, area.getHeight() * 0.16f);
        g.setColour(tint);
        g.drawRect(disk, std::max(1.2f, area.getWidth() * 0.055f));
        g.fillRect(disk.removeFromTop(disk.getHeight() * 0.34f)
                       .reduced(disk.getWidth() * 0.18f, 0.0f));
        g.drawRect(disk.removeFromBottom(disk.getHeight() * 0.48f)
                       .reduced(disk.getWidth() * 0.18f, 0.0f),
                   std::max(1.0f, area.getWidth() * 0.045f));
        return;
    }
    if (id == "toolbar.undo" || id == "toolbar.redo")
        return drawUndo(g, area, id == "toolbar.redo", tint);
    if (id == "brush.drum")
    {
        const float side = std::min(area.getWidth(), area.getHeight()) * 0.62f;
        const auto circle = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
        g.setColour(button.findColour(juce::TextButton::buttonOnColourId));
        g.fillEllipse(circle);
        g.setColour(juce::Colour(palette_.textMuted));
        g.drawEllipse(circle, 1.5f);
        return;
    }
    if (id == "brush.slicer")
    {
        auto circle = area.withSizeKeepingCentre(std::min(area.getWidth(), area.getHeight()) * 0.62f,
                                                 std::min(area.getWidth(), area.getHeight()) * 0.62f);
        g.setColour(tint);
        g.drawEllipse(circle, 1.7f);
        g.drawLine(circle.getCentreX(), circle.getY(), circle.getCentreX(), circle.getCentreY(), 1.5f);
        g.drawLine(circle.getCentreX(), circle.getCentreY(), circle.getRight(), circle.getCentreY(), 1.5f);
        return;
    }
    if (id == "brush.velocity")
    {
        g.setColour(tint);
        const auto bars = area.reduced(area.getWidth() * 0.22f, area.getHeight() * 0.18f);
        const float gap = bars.getWidth() * 0.08f;
        const float width = (bars.getWidth() - gap * 3.0f) / 4.0f;
        for (int index = 0; index < 4; ++index)
        {
            const float height = bars.getHeight() * (0.35f + static_cast<float>(index) * 0.2f);
            g.fillRect(bars.getX() + static_cast<float>(index) * (width + gap),
                       bars.getBottom() - height, width, height);
        }
        return;
    }
    if (id == "brush.dice")
        return drawDice(g, area, tint);
    if ((id.startsWith("random.module.") || id == "random.fillEmpty"))
    {
        const auto diameter = std::min(area.getWidth(), area.getHeight()) * 0.65f;
        const auto dot = area.withSizeKeepingCentre(diameter, diameter);
        g.setColour(tint);
        g.drawEllipse(dot, std::max(1.0f, diameter * 0.10f));
        if (button.getToggleState())
            g.fillEllipse(dot.reduced(diameter * 0.24f));
        return;
    }
    if (id == "sample.layer")
    {
        auto textArea = area.reduced(std::max(13.0f, area.getWidth() * 0.16f), 1.0f);
        auto arrows = area;
        drawTriangle(g, arrows.removeFromLeft(arrows.getHeight() * 0.72f), false, tint);
        drawTriangle(g, arrows.removeFromRight(arrows.getHeight() * 0.72f), true, tint);
        g.setColour(tint);
        g.setFont(font(std::max(8.0f, button.getHeight() * 0.34f), true));
        g.drawFittedText(button.getButtonText(), textArea.toNearestInt(),
                         juce::Justification::centred, 1, 0.80f);
        return;
    }
    if (id == "sample.folder")
    {
        auto folder = area.reduced(area.getWidth() * 0.18f, area.getHeight() * 0.25f);
        juce::Path path;
        path.startNewSubPath(folder.getX(), folder.getY() + folder.getHeight() * 0.24f);
        path.lineTo(folder.getX() + folder.getWidth() * 0.33f,
                    folder.getY() + folder.getHeight() * 0.24f);
        path.lineTo(folder.getX() + folder.getWidth() * 0.42f, folder.getY());
        path.lineTo(folder.getRight(), folder.getY());
        path.lineTo(folder.getRight(), folder.getBottom());
        path.lineTo(folder.getX(), folder.getBottom());
        path.closeSubPath();
        g.setColour(tint);
        g.fillPath(path);
        return;
    }
    if (id == "sample.clear")
    {
        const auto clear = area.reduced(area.getWidth() * 0.31f, area.getHeight() * 0.27f);
        g.setColour(tint);
        const float stroke = std::max(1.5f, area.getWidth() * 0.08f);
        g.drawLine(juce::Line<float>(clear.getTopLeft(), clear.getBottomRight()), stroke);
        g.drawLine(juce::Line<float>(clear.getTopRight(), clear.getBottomLeft()), stroke);
        return;
    }
    if (id == "sample.mono")
    {
        const float diameter = area.getHeight() * 0.55f;
        const auto left = juce::Rectangle<float>(diameter, diameter)
            .withCentre({ area.getX() + diameter * 0.85f, area.getCentreY() });
        const auto right = left.translated(diameter * 0.50f, 0.0f);
        g.setColour(juce::Colour(button.getToggleState() ? palette_.accentBright : palette_.borderBright));
        g.fillEllipse(left);
        g.setColour(tint);
        if (button.getToggleState()) g.fillEllipse(right);
        g.drawEllipse(right, std::max(1.0f, diameter * 0.08f));
        g.setFont(font(std::max(8.0f, area.getHeight() * 0.43f)));
        g.drawFittedText("MONO", area.withTrimmedLeft(diameter * 1.95f).reduced(2.0f, 0.0f).toNearestInt(),
                         juce::Justification::centred, 1);
        return;
    }
    if (id == "sample.oneshot")
    {
        auto content = area.reduced(2.0f);
        const float diameter = std::min(content.getHeight() * 0.56f, content.getWidth() * 0.22f);
        const auto dot = juce::Rectangle<float>(diameter, diameter)
                             .withCentre({ content.getX() + diameter * 0.7f, content.getCentreY() });
        g.setColour(tint);
        g.drawEllipse(dot, std::max(1.0f, diameter * 0.10f));
        if (button.getToggleState())
            g.fillEllipse(dot.reduced(diameter * 0.24f));
        g.setFont(font(std::max(8.0f, area.getHeight() * 0.37f)));
        g.drawText("One Shot", content.withTrimmedLeft(diameter * 1.55f).toNearestInt(),
                   juce::Justification::centredLeft);
        return;
    }
    if (id == "kit.audition")
    {
        auto speaker = area.reduced(area.getWidth() * 0.23f, area.getHeight() * 0.27f);
        juce::Path body;
        body.startNewSubPath(speaker.getX(), speaker.getCentreY() - speaker.getHeight() * 0.18f);
        body.lineTo(speaker.getCentreX() - speaker.getWidth() * 0.05f,
                    speaker.getCentreY() - speaker.getHeight() * 0.18f);
        body.lineTo(speaker.getRight() - speaker.getWidth() * 0.24f, speaker.getY());
        body.lineTo(speaker.getRight() - speaker.getWidth() * 0.24f, speaker.getBottom());
        body.lineTo(speaker.getCentreX() - speaker.getWidth() * 0.05f,
                    speaker.getCentreY() + speaker.getHeight() * 0.18f);
        body.lineTo(speaker.getX(), speaker.getCentreY() + speaker.getHeight() * 0.18f);
        body.closeSubPath();
        g.setColour(tint);
        g.strokePath(body, juce::PathStrokeType(1.4f));
        juce::Path wave;
        wave.addArc(speaker.getRight() - speaker.getWidth() * 0.42f, speaker.getY(),
                    speaker.getWidth() * 0.42f, speaker.getHeight(), -0.8f, 0.8f, true);
        g.strokePath(wave, juce::PathStrokeType(1.4f));
        return;
    }
    if (id == "kit.lock")
    {
        auto lock = area.reduced(area.getWidth() * 0.28f, area.getHeight() * 0.18f);
        const auto body = lock.removeFromBottom(lock.getHeight() * 0.58f);
        g.setColour(tint);
        g.drawRoundedRectangle(body, 1.5f, 1.3f);
        juce::Path shackle;
        shackle.addArc(lock.getX() + lock.getWidth() * 0.17f,
                       lock.getY() + lock.getHeight() * 0.18f,
                       lock.getWidth() * 0.66f, lock.getHeight() * 1.22f,
                       juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, true);
        g.strokePath(shackle, juce::PathStrokeType(1.3f));
        return;
    }
    if (id == "master.settings")
    {
        const auto centre = area.getCentre();
        const float outer = std::min(area.getWidth(), area.getHeight()) * 0.34f;
        const float inner = outer * 0.34f;
        g.setColour(tint);
        g.drawEllipse(juce::Rectangle<float>(outer * 2.0f, outer * 2.0f).withCentre(centre), 2.0f);
        g.fillEllipse(juce::Rectangle<float>(inner * 2.0f, inner * 2.0f).withCentre(centre));
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(tooth) / 8.0f;
            g.drawLine(centre.x + std::cos(angle) * outer * 0.88f,
                       centre.y + std::sin(angle) * outer * 0.88f,
                       centre.x + std::cos(angle) * outer * 1.28f,
                       centre.y + std::sin(angle) * outer * 1.28f,
                       std::max(1.5f, outer * 0.22f));
        }
        return;
    }
    if (id == "toolbar.about")
    {
        auto content = area.reduced(3.0f);
        const float logoSide = std::min(content.getHeight(), 25.0f);
        auto logo = content.removeFromLeft(logoSide).reduced(2.0f);
        g.setColour(tint);
        g.drawEllipse(logo, 1.2f);
        const auto mesh = logo.reduced(2.0f);
        g.drawEllipse(mesh, 0.8f);
        {
            juce::Graphics::ScopedSaveState saved(g);
            juce::Path clip;
            clip.addEllipse(mesh);
            g.reduceClipRegion(clip);
            const auto centre = mesh.getCentre();
            const float radius = mesh.getWidth() * 0.5f;
            for (int line = -3; line <= 3; ++line)
            {
                const float offset = static_cast<float>(line) * radius * 0.45f;
                g.drawLine(centre.x - radius, centre.y - radius + offset,
                           centre.x + radius, centre.y + radius + offset, 0.7f);
                g.drawLine(centre.x - radius, centre.y + radius + offset,
                           centre.x + radius, centre.y - radius + offset, 0.7f);
            }
        }
        g.setFont(font(std::max(9.0f, area.getHeight() * 0.33f), true));
        g.drawFittedText("MODALICS", content.toNearestInt(), juce::Justification::centred, 1);
        return;
    }

    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour(tint);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(3, 1),
                     juce::Justification::centred, 2, 0.85f);
}

void BeatScholarLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                               int height, float sliderPos, float startAngle,
                                               float endAngle, juce::Slider& slider)
{
    drawKeyboardFocus(g, slider);
    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(width), static_cast<float>(height));
    const float textSpace = slider.getTextBoxPosition() == juce::Slider::NoTextBox
                                ? 0.0f : std::min(18.0f, area.getHeight() * 0.24f);
    area.removeFromBottom(textSpace);
    const float diameter = std::min(area.getWidth(), area.getHeight()) * 0.76f;
    const auto knob = juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
    const auto centre = knob.getCentre();
    const float radius = diameter * 0.5f;

    const auto id = slider.getComponentID();
    const bool percentage = id == "effect.6" || id == "effect.7" || id == "effect.8";
    const bool randomizerLevel = id == "random.0" || id == "random.2" || id == "random.3";
    const bool levelFill = percentage || randomizerLevel;
    const bool masterDial = id == "master.0" || id == "master.1"
                            || id == "master.2" || id == "master.3";
    if (darkMode_ && !levelFill)
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff232323), centre.x, knob.getY(),
                                              juce::Colour(0xff141414), centre.x, knob.getBottom(), false));
    else
        g.setColour(juce::Colour(randomizerLevel ? 0xff1f2043U
                                                : masterDial ? 0xff222349U : palette_.panelRaised));
    g.fillEllipse(knob);
    if (levelFill && sliderPos > 0.0f)
    {
        // Send and Randomizer level dials fill from the bottom with a
        // horizontal surface, clipped to the circular control.
        const juce::Graphics::ScopedSaveState saved(g);
        juce::Path circle;
        circle.addEllipse(knob);
        g.reduceClipRegion(circle);
        const float surface = knob.getBottom() - diameter * std::clamp(sliderPos, 0.0f, 1.0f);
        // The tutorial's Min/Max dials have the same shade at the same height,
        // regardless of level: only the filled region moves, not its gradient.
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(randomizerLevel ? 0xff6060f8U : 0xff454090U),
            centre.x, randomizerLevel ? knob.getY() : surface,
            juce::Colour(randomizerLevel ? 0xff21224cU : 0xff2c295cU),
            centre.x, knob.getBottom(), false));
        g.fillRect(knob.withTop(surface));
    }
    if (!darkMode_ || levelFill)
    {
        g.setColour(juce::Colour(randomizerLevel ? 0xff6566a1U : palette_.borderBright));
        g.drawEllipse(knob, std::max(1.5f, diameter * (randomizerLevel ? 0.045f : 0.055f)));
    }
    if (slider.isEnabled())
    {
        const float valueAngle = startAngle + sliderPos * (endAngle - startAngle);
        // Insert controls use a dot; level dials show their value over the fill.
        if (!levelFill)
        {
            const auto pointer = juce::Point<float>(centre.x + std::sin(valueAngle) * radius * 0.68f,
                                                    centre.y - std::cos(valueAngle) * radius * 0.68f);
            g.setColour(juce::Colour(palette_.text));
            g.fillEllipse(juce::Rectangle<float>(std::max(3.0f, diameter * 0.11f),
                                                 std::max(3.0f, diameter * 0.11f)).withCentre(pointer));
        }
    }

    if (levelFill)
    {
        const auto text = percentage
                              ? juce::String(static_cast<int>(std::lround(slider.getValue() * 100.0))) + "%"
                              : slider.getTextFromValue(slider.getValue());
        g.setColour(juce::Colour(palette_.text));
        g.setFont(font(randomizerLevel ? diameter * 0.15f : std::max(8.0f, diameter * 0.22f),
                       !percentage && !randomizerLevel));
        g.drawText(text, knob.toNearestInt(), juce::Justification::centred);
    }
}

juce::Slider::SliderLayout BeatScholarLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout(slider);
    if (slider.getComponentID() == "master.4")
    {
        // Keep the gain gesture range aligned with the painted stereo meters
        // at every editor size, rather than JUCE's fixed 12-pixel thumb inset.
        const int inset = juce::roundToInt(static_cast<float>(slider.getHeight()) * 12.0f / 116.0f);
        layout.sliderBounds = slider.getLocalBounds().reduced(0, inset);
    }
    return layout;
}

void BeatScholarLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width,
                                               int height, float sliderPos, float, float,
                                               juce::Slider::SliderStyle style, juce::Slider& slider)
{
    drawKeyboardFocus(g, slider);
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(width), static_cast<float>(height));
    const auto id = slider.getComponentID();
    if (id == "toolbar.bpm" || id == "toolbar.velocity")
    {
        g.setColour(juce::Colour(palette_.text));
        g.setFont(font(std::max(10.0f, area.getHeight() * 0.46f), true));
        const auto suffix = id == "toolbar.bpm" ? " BPM" : "";
        g.drawFittedText(juce::String(static_cast<int>(std::lround(slider.getValue()))) + suffix,
                         area.toNearestInt(), juce::Justification::centred, 1);
        return;
    }
    if (id == "master.4")
    {
        // The editor paints the live left/right meter rails behind this thumb.
        // Repainting opaque rails here would hide their output levels.
        g.setColour(juce::Colour(palette_.text));
        g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth() * 0.56f,
                                                           area.getHeight() * (8.0f / 92.0f))
                                   .withCentre({ area.getCentreX(), sliderPos }),
                               area.getWidth() * 0.04f);
        return;
    }
    if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
    {
        const auto track = area.withSizeKeepingCentre(std::max(3.0f, area.getWidth() * 0.16f),
                                                      area.getHeight() * 0.82f);
        g.setColour(juce::Colour(palette_.border));
        g.fillRoundedRectangle(track, track.getWidth() * 0.5f);
        g.setColour(juce::Colour(palette_.text));
        // Official Samples & Sounds 2:35: Gain thumb is 14 px across a 216 px
        // sound stripe (146 px in our design). Keep the full slider hit area.
        const auto thumbWidth = area.getWidth() * (id == "effect.9" ? 9.46f / 54.0f : 0.56f);
        g.fillRoundedRectangle(track.withSizeKeepingCentre(thumbWidth,
                                                           std::max(5.0f, area.getHeight() * 0.08f))
                                     .withCentre({ track.getCentreX(), sliderPos }), 2.0f);
        return;
    }
    const auto track = area.withSizeKeepingCentre(area.getWidth() * 0.9f,
                                                  std::max(3.0f, area.getHeight() * 0.11f));
    g.setColour(juce::Colour(palette_.border));
    g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
    g.setColour(juce::Colour(palette_.text));
    const float thumb = std::max(8.0f, area.getHeight() * 0.35f);
    g.fillEllipse(juce::Rectangle<float>(thumb, thumb)
                      .withCentre({ sliderPos, track.getCentreY() }));
}

void BeatScholarLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                           int, int, int, int, juce::ComboBox&)
{
    const auto area = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1),
                                             static_cast<float>(height - 1));
    g.setColour(juce::Colour(palette_.panel));
    g.fillRect(area);
    g.setColour(juce::Colour(palette_.borderBright));
    g.drawRect(area, 1.0f);
    drawTriangle(g, juce::Rectangle<float>(static_cast<float>(width - height), 0.0f,
                                           static_cast<float>(height), static_cast<float>(height)),
                 false, juce::Colour(palette_.textMuted));
}

void BeatScholarLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(5, 1, box.getWidth() - box.getHeight(), box.getHeight() - 2);
    label.setFont(font(std::max(9.0f, static_cast<float>(box.getHeight()) * 0.44f), true));
}

void BeatScholarLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(juce::Colour(palette_.panelRaised));
    g.setColour(juce::Colour(palette_.borderBright));
    g.drawRect(juce::Rectangle<int>(0, 0, width, height), 1);
}

void BeatScholarLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                bool separator, bool active, bool highlighted,
                                                bool ticked, bool submenu, const juce::String& text,
                                                const juce::String& shortcut,
                                                const juce::Drawable* icon,
                                                const juce::Colour* textColour)
{
    if (separator)
    {
        g.setColour(juce::Colour(palette_.border));
        g.fillRect(area.reduced(7, 0).withHeight(1).withCentre(area.getCentre()));
        return;
    }
    if (highlighted && active)
    {
        g.setColour(juce::Colour(palette_.accent));
        g.fillRect(area.reduced(1, 0));
    }
    auto content = area.reduced(9, 1);
    const int markWidth = std::max(17, content.getHeight());
    auto mark = content.removeFromLeft(markWidth);
    auto arrow = content.removeFromRight(submenu ? markWidth : 0);
    const auto tint = textColour != nullptr ? *textColour
                     : active ? juce::Colour(palette_.text)
                              : juce::Colour(palette_.textMuted).withAlpha(0.6f);
    if (icon != nullptr)
        icon->drawWithin(g, mark.toFloat(), juce::RectanglePlacement::centred, 1.0f);
    else if (ticked)
    {
        g.setColour(tint);
        juce::Path check;
        check.startNewSubPath(mark.getX() + mark.getWidth() * 0.18f, mark.getCentreY());
        check.lineTo(mark.getX() + mark.getWidth() * 0.42f, mark.getBottom() - mark.getHeight() * 0.2f);
        check.lineTo(mark.getRight() - mark.getWidth() * 0.14f, mark.getY() + mark.getHeight() * 0.18f);
        g.strokePath(check, juce::PathStrokeType(2.0f));
    }
    g.setColour(tint);
    g.setFont(getPopupMenuFont());
    g.drawFittedText(text, content, juce::Justification::centredLeft, 1);
    if (shortcut.isNotEmpty())
        g.drawText(shortcut, content, juce::Justification::centredRight);
    if (submenu)
        drawTriangle(g, arrow.toFloat(), true, tint);
}

} // namespace bsl::ui
