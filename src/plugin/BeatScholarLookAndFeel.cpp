#include "plugin/BeatScholarLookAndFeel.h"
#include "BeatScholarFonts.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

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
                         juce::Colour tint, float stroke, bool kitTile)
{
    if (!kitTile && (pad == 1 || pad == 2))
    {
        // The large snare has full-height tension rods behind the short lugs.
        // Original 1.6.2 captures also separate the three strike arcs vertically.
        const float unit = std::min(icon.getWidth(), icon.getHeight()) / 42.0f;
        const auto point = [&icon, unit](float x, float y) {
            return icon.getCentre() + juce::Point<float>(x - 487.0f, y - 628.0f) * unit;
        };
        const auto line = [&g, &point, unit](float x1, float y1, float x2, float y2,
                                           float width) {
            g.drawLine({point(x1,y1),point(x2,y2)},width*unit);
        };
        tint = juce::Colour(0xffd7dbf6);
        g.setColour(tint);
        if (pad == 1)
        {
            const auto shell = juce::Rectangle<float>(point(469.5f,628.5f),point(502.5f,640.0f));
            g.drawRoundedRectangle(shell,0.6f*unit,0.75f*unit);
            line(469.5f,630.0f,502.5f,630.0f,0.55f);
            line(469.5f,638.5f,502.5f,638.5f,0.55f);
            for (const float x : {473.0f,482.0f,490.5f,499.5f})
            {
                line(x,630.0f,x,638.5f,0.55f);
                line(x,633.0f,x,636.0f,1.15f);
            }
            line(482.5f,625.0f,505.5f,617.5f,1.05f);
            line(474.0f,620.0f,478.5f,622.5f,0.65f);
            line(478.5f,614.5f,480.0f,618.8f,0.65f);
            line(484.8f,615.5f,484.0f,620.0f,0.65f);
            return;
        }
        const auto shell = juce::Rectangle<float>(point(469.5f,636.5f),point(505.5f,649.5f));
        g.drawRoundedRectangle(shell,0.6f*unit,0.75f*unit);
        line(469.5f,638.5f,505.5f,638.5f,0.55f);
        line(469.5f,648.5f,505.5f,648.5f,0.55f);
        for (const float x : {473.5f,482.5f,492.0f,501.5f})
        {
            line(x,638.5f,x,648.5f,0.55f);
            line(x,641.5f,x,645.5f,1.2f);
        }
        line(473.8f,632.7f,502.0f,632.3f,0.9f);
        const auto arc = [&g, &point, unit](float y, float rx, float ry) {
            const auto centre = point(487.0f,y);
            juce::Path wave;
            wave.addCentredArc(centre.x,centre.y,rx*unit,ry*unit,0,
                -juce::MathConstants<float>::pi*0.25f,
                 juce::MathConstants<float>::pi*0.25f,true);
            g.strokePath(wave,juce::PathStrokeType(0.85f*unit));
        };
        arc(628.0f,12.7f,9.5f);
        arc(629.3f,8.7f,7.0f);
        arc(630.0f,4.7f,4.5f);
        return;
    }
    if (!kitTile && pad == 3)
    {
        // The large clap has a tilted palm and separated finger valleys;
        // scaling the compact kit drawing flattens the heel of the hand.
        const float unit = std::min(icon.getWidth(), icon.getHeight()) / 42.0f;
        const auto point = [&icon, unit](float x, float y) {
            return icon.getCentre() + juce::Point<float>(x - 487.0f, y - 628.0f) * unit;
        };
        g.setColour(juce::Colour(0xffd7dbf6));
        juce::Path hand;
        hand.startNewSubPath(point(482.0f,633.0f));
        hand.lineTo(point(493.0f,623.9f));
        hand.quadraticTo(point(495.6f,622.0f),point(496.0f,625.5f));
        hand.quadraticTo(point(496.3f,626.5f),point(495.4f,627.8f));
        hand.lineTo(point(486.5f,634.7f));
        hand.lineTo(point(497.2f,626.7f));
        hand.quadraticTo(point(499.5f,625.8f),point(499.0f,629.2f));
        hand.lineTo(point(489.4f,637.0f));
        hand.lineTo(point(499.0f,630.4f));
        hand.quadraticTo(point(500.6f,630.3f),point(500.6f,632.5f));
        hand.lineTo(point(491.4f,640.6f));
        hand.lineTo(point(496.5f,637.3f));
        hand.quadraticTo(point(498.0f,637.0f),point(498.3f,638.8f));
        hand.lineTo(point(489.3f,648.3f));
        hand.cubicTo(point(484.0f,653.7f),point(479.0f,653.5f),point(474.7f,648.5f));
        hand.cubicTo(point(471.5f,645.0f),point(472.0f,641.0f),point(473.2f,637.5f));
        hand.lineTo(point(478.4f,629.0f));
        hand.quadraticTo(point(479.3f,627.3f),point(481.6f,628.4f));
        hand.quadraticTo(point(483.7f,629.5f),point(482.0f,632.5f));
        hand.lineTo(point(480.5f,635.5f));
        g.strokePath(hand,juce::PathStrokeType(0.75f*unit,
                     juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        juce::Path rearHand;
        rearHand.startNewSubPath(point(474.0f,647.0f));
        rearHand.cubicTo(point(469.0f,645.7f),point(469.5f,640.0f),point(471.0f,635.0f));
        rearHand.lineTo(point(477.0f,625.6f));
        rearHand.quadraticTo(point(479.2f,624.0f),point(480.0f,626.5f));
        rearHand.lineTo(point(480.5f,628.4f));
        rearHand.startNewSubPath(point(482.5f,628.5f));
        rearHand.lineTo(point(488.0f,622.8f));
        rearHand.quadraticTo(point(489.5f,621.4f),point(491.0f,624.0f));
        rearHand.lineTo(point(491.5f,625.5f));
        g.strokePath(rearHand,juce::PathStrokeType(0.65f*unit,
                     juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        g.drawLine({point(499.0f,622.3f),point(500.3f,618.4f)},0.65f*unit);
        g.drawLine({point(501.0f,623.3f),point(503.5f,620.7f)},0.65f*unit);
        g.drawLine({point(502.0f,625.0f),point(506.0f,624.2f)},0.65f*unit);
        return;
    }
    // Large sample drawings have their own proportions and baseline. Measured
    // from original 1.6.2 at both 1280 and 960; kit tile transforms stay separate.
    if (!kitTile && pad != 0)
    {
        const float unit = std::min(icon.getWidth(), icon.getHeight()) / 42.0f;
        tint = juce::Colour(0xffd7dbf6);
        float size = 1.0f, dx = 0.0f, dy = 0.0f;
        switch (pad)
        {
            case 12: size = 0.96f; dx = 0.8f; dy = 6.6f; break;
            case 13: size = 0.90f; dx = 0.2f; dy = 9.0f; stroke = 1.20f * unit; break;
            case 14: size = 0.78f; dx = 2.5f; dy = 8.5f; stroke = 1.20f * unit; break;
            case 15: size = 0.88f; dx = 0.3f; dy = 9.0f; stroke = 1.35f * unit;
                     tint = tint.withAlpha(0.80f); break;
            default: break;
        }
        icon = icon.withSizeKeepingCentre(icon.getWidth() * size, icon.getHeight() * size)
                   .translated(dx * unit, dy * unit);
    }
    g.setColour(tint);
    if (!kitTile && pad >= 4 && pad <= 8)
    {
        const float unit = std::min(icon.getWidth(), icon.getHeight()) / 42.0f;
        const auto point = [&icon, unit](float x, float y) {
            return icon.getCentre() + juce::Point<float>(x - 487.0f, y - 628.0f) * unit;
        };
        const auto polygon = [&g, &point](std::initializer_list<juce::Point<float>> vertices) {
            juce::Path path;
            bool first = true;
            for (const auto vertex : vertices)
            {
                if (first) path.startNewSubPath(point(vertex.x, vertex.y));
                else path.lineTo(point(vertex.x, vertex.y));
                first = false;
            }
            path.closeSubPath();
            g.fillPath(path);
        };
        const auto line = [&g, &point, unit](float x1, float y1, float x2, float y2, float width) {
            g.drawLine({ point(x1, y1), point(x2, y2) }, width * unit);
        };
        // Shallow shaded hi-hats and independently tilted ride/crash cymbals.
        // Their stand lengths are not derived from the disc width.
        if (pad == 4)
        {
            polygon({ {469,630.6f}, {484,626.0f}, {487.8f,625.3f},
                      {491.5f,626.0f}, {506.5f,630.6f} });
            g.setColour(tint.withAlpha(0.70f));
            polygon({ {469,632.0f}, {487.8f,635.0f}, {506.5f,632.0f} });
            g.setColour(tint.withAlpha(0.55f));
            line(487.8f,634.5f,487.8f,651.0f,1.6f);
            g.setColour(tint.withAlpha(0.70f));
            line(487.8f,621.5f,487.8f,625.5f,1.3f);
        }
        else if (pad == 5)
        {
            polygon({ {468,630.5f}, {483,626.6f}, {488.2f,625.4f},
                      {492,627.2f}, {507.8f,632.5f} });
            polygon({ {469,633.8f}, {488.2f,634.0f}, {506.8f,633.0f},
                      {488.2f,636.8f} });
            line(488.2f,621.3f,488.2f,651.0f,0.9f);
            g.setColour(tint.withAlpha(0.72f));
            for (const auto offset : { juce::Point<float>(460.0f,626.0f),
                                       juce::Point<float>(507.5f,637.3f) })
            {
                juce::Path motion;
                motion.startNewSubPath(point(offset.x,offset.y));
                motion.lineTo(point(offset.x+1.5f,offset.y+2.0f));
                motion.lineTo(point(offset.x+4.8f,offset.y-0.2f));
                motion.lineTo(point(offset.x+7.8f,offset.y+0.8f));
                g.strokePath(motion,juce::PathStrokeType(0.9f*unit));
                motion.clear();
                motion.startNewSubPath(point(offset.x+6.1f,offset.y-6.1f));
                motion.lineTo(point(offset.x+6.5f,offset.y-2.9f));
                motion.lineTo(point(offset.x+9.5f,offset.y-2.5f));
                motion.lineTo(point(offset.x+9.8f,offset.y+0.5f));
                // The second pair is inverted around the opposite disc edge.
                if (offset.x > 500.0f)
                    motion.applyTransform(juce::AffineTransform::rotation(
                        juce::MathConstants<float>::pi, point(offset.x+4.0f,offset.y).x,
                        point(offset.x+4.0f,offset.y).y));
                g.strokePath(motion,juce::PathStrokeType(0.9f*unit));
            }
        }
        else if (pad == 6)
        {
            polygon({ {463.6f,635.3f}, {483.5f,629.1f}, {487.6f,628.5f},
                      {491.8f,629.1f}, {512.0f,631.1f} });
            line(487.6f,621.0f,487.6f,651.0f,0.9f);
            g.fillEllipse(juce::Rectangle<float>(2.4f*unit,2.4f*unit)
                              .withCentre(point(487.6f,625.7f)));
        }
        else if (pad == 7)
        {
            polygon({ {462.6f,629.1f}, {483.4f,626.3f}, {487.9f,623.6f},
                      {491.0f,626.4f}, {513.0f,635.7f} });
            line(487.9f,621.0f,487.9f,643.5f,1.5f);
            g.fillEllipse(juce::Rectangle<float>(4.3f*unit,4.3f*unit)
                              .withCentre(point(488.0f,644.0f)));
            line(488.0f,644.0f,481.8f,655.0f,1.0f);
            line(489.5f,644.0f,492.2f,644.0f,1.0f);
            line(492.2f,641.5f,492.2f,646.5f,1.0f);
        }
        else
        {
            polygon({ {468.0f,627.2f}, {485.5f,627.2f}, {488.5f,626.8f},
                      {491.3f,627.8f}, {508.0f,635.9f} });
            line(488.5f,619.5f,488.5f,649.5f,0.9f);
            g.fillEllipse(juce::Rectangle<float>(2.3f*unit,2.3f*unit)
                              .withCentre(point(488.5f,624.7f)));
        }
        return;
    }
    if (!kitTile && pad >= 9 && pad <= 11)
    {
        const float unit = std::min(icon.getWidth(), icon.getHeight()) / 42.0f;
        const auto point = [&icon, unit](float x, float y) {
            return icon.getCentre() + juce::Point<float>(x - 487.0f, y - 628.0f) * unit;
        };
        const auto box = [&point, unit](float x, float y, float w, float h) {
            const auto origin = point(x,y);
            return juce::Rectangle<float>(origin.x,origin.y,w*unit,h*unit);
        };
        const auto line = [&g, &point, unit](float x1, float y1, float x2, float y2) {
            g.drawLine({point(x1,y1),point(x2,y2)},0.85f*unit);
        };
        const auto tom = [&g, &box, unit, tint](float x, float y, float w, float h,
                                        float angle, bool selected, bool rack) {
            const auto shell = box(x,y,w,h);
            const float rimHeight = (rack ? 6.2f : 2.4f) * unit;
            const auto head = shell.withHeight(rimHeight);
            juce::Graphics::ScopedSaveState saved(g);
            g.addTransform(juce::AffineTransform::rotation(angle,
                shell.getCentreX(),shell.getCentreY()));
            juce::Path body;
            body.startNewSubPath(shell.getX(),head.getCentreY());
            body.lineTo(shell.getX(),shell.getBottom()-rimHeight*0.5f);
            body.cubicTo(shell.getX(),shell.getBottom()+rimHeight*0.15f,
                         shell.getRight(),shell.getBottom()+rimHeight*0.15f,
                         shell.getRight(),shell.getBottom()-rimHeight*0.5f);
            body.lineTo(shell.getRight(),head.getCentreY());
            if (selected)
            {
                body.addCentredArc(head.getCentreX(),head.getCentreY(),
                    head.getWidth()*0.5f,head.getHeight()*0.5f,0,
                    juce::MathConstants<float>::halfPi,-juce::MathConstants<float>::halfPi);
                body.closeSubPath();
                {
                    juce::Path rim,gap;
                    rim.addCentredArc(head.getCentreX(),head.getCentreY(),
                        head.getWidth()*0.5f,head.getHeight()*0.5f,0,
                        juce::MathConstants<float>::pi*0.60f,
                        juce::MathConstants<float>::pi*1.40f,true);
                    juce::PathStrokeType(0.65f*unit).createStrokedPath(gap,rim);
                    body.setUsingNonZeroWinding(false);
                    body.addPath(gap);
                }
                g.fillPath(body);
            }
            else
            {
                g.strokePath(body,juce::PathStrokeType(0.85f*unit));
                if (!rack)
                    g.setColour(tint.withMultipliedAlpha(0.55f));
                g.drawEllipse(head,0.85f*unit);
            }
        };
        // The large kit has two tilted rack toms, a dim bass drum and
        // independent floor-tom legs. Selected heads retain the front rim gap.
        g.setColour(tint.withAlpha(0.50f));
        line(481,626.5f,485.7f,626.5f);
        line(483.5f,626.5f,483.5f,632.5f);
        g.drawEllipse(box(472.2f,632.3f,18.5f,18.5f),0.85f*unit);
        line(473.6f,646.7f,471.4f,650.5f);
        line(489.0f,646.7f,491.2f,650.5f);
        g.setColour(tint);
        tom(466.6f,619.8f,14.0f,11.0f,0.16f,pad==9,true);
        tom(483.7f,620.1f,14.0f,11.0f,-0.12f,pad==10,true);
        tom(493.7f,633.5f,15.0f,12.8f,0,pad==11,false);
        g.setColour(tint);
        line(494.5f,645.7f,494.5f,650.0f);
        line(508,645.7f,508,650.0f);
        line(498.5f,646.2f,498.5f,648.0f);
        return;
    }
    if (pad >= 9 && pad <= 12)
    {
        const float heightRatio = pad == 12 ? 1.0f : 0.85f;
        const float width = std::min(icon.getWidth(), icon.getHeight() / heightRatio);
        icon = icon.withSizeKeepingCentre(width, width * heightRatio);
    }
    const auto drawDrum = [&g, stroke, kitTile](juce::Rectangle<float> box, bool sticks) {
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
        if (!kitTile)
        {
            const float rimInset = width * 0.045f;
            const float rimStroke = width * (sticks ? 0.011f : 0.022f);
            g.drawLine(shell.getX(),shell.getY()+rimInset,
                       shell.getRight(),shell.getY()+rimInset,rimStroke);
            g.drawLine(shell.getX(),shell.getBottom()-rimInset,
                       shell.getRight(),shell.getBottom()-rimInset,rimStroke);
        }
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
            const float strikeWidth = stroke * (kitTile ? 0.48f : 0.65f);
            for (const float radius : { 0.12f, 0.20f, 0.28f })
            {
                juce::Path wave;
                wave.addCentredArc(point(0.5f, 0.39f).x, point(0.5f, kitTile ? 0.39f : 0.32f).y,
                                   width * radius, width * radius * (kitTile ? 1.0f : 0.90f), 0.0f,
                                   -juce::MathConstants<float>::pi * 0.30f,
                                   juce::MathConstants<float>::pi * 0.30f, true);
                g.strokePath(wave, juce::PathStrokeType(strikeWidth));
            }
            g.drawLine({ point(0.13f, 0.49f), point(0.87f, 0.49f) }, strikeWidth);
        }
    };
    const auto drawCymbal = [&g, stroke, tint, kitTile, pad](juce::Rectangle<float> box, float tilt) {
        const float slope = tilt / box.getHeight();
        const float width = std::min(box.getWidth(), box.getHeight());
        box = box.withSizeKeepingCentre(width, width);
        const auto centre = box.getCentre();
        const float rise = slope * width;
        const bool smallOpenHat = kitTile && pad == 5;
        const bool shadedHat = kitTile && (pad == 4 || pad == 5);
        const float bellTop = centre.y - width * (smallOpenHat ? 0.10f : 0.15f);
        const float bellShoulder = centre.y - width * (smallOpenHat ? 0.07f : 0.09f);
        const float lowerEdge = centre.y + width * (shadedHat ? (smallOpenHat ? 0.03f : 0.11f) : 0.07f);
        // Shallow disc and central bell, as in Samples & Sounds 2:35.
        juce::Path disc;
        disc.startNewSubPath(box.getX(), centre.y + rise);
        disc.lineTo(centre.x - width * 0.10f, bellShoulder);
        disc.lineTo(centre.x, bellTop);
        disc.lineTo(centre.x + width * 0.10f, bellShoulder);
        disc.lineTo(box.getRight(), centre.y - rise);
        if (!shadedHat)
            disc.lineTo(centre.x, lowerEdge);
        disc.closeSubPath();
        g.fillPath(disc);
        if (shadedHat)
        {
            // Original small hi-hats have a shaded underside and a bright rim.
            juce::Path underside;
            underside.startNewSubPath(box.getX(), centre.y + rise);
            underside.lineTo(centre.x, lowerEdge);
            underside.lineTo(box.getRight(), centre.y - rise);
            g.setColour(tint.withMultipliedAlpha(0.55f));
            g.fillPath(underside);
            g.setColour(tint);
            g.strokePath(underside, juce::PathStrokeType(stroke * 0.45f));
        }
        if (kitTile)
            g.setColour(tint.withMultipliedAlpha(0.35f));
        g.drawLine(centre.x, centre.y - width * (smallOpenHat ? 0.20f : 0.23f),
                   centre.x, bellTop, stroke * 0.65f);
        // Stand brightness measured in the small original 1.6.2 kit tiles;
        // keep the discs and the larger sample-panel drawing independent.
        constexpr float kitStandAlpha[] { 0.28f, 0.78f, 0.44f, 0.61f, 0.26f };
        if (kitTile && pad >= 4 && pad <= 8)
            g.setColour(tint.withMultipliedAlpha(kitStandAlpha[pad - 4]));
        g.drawLine(centre.x, lowerEdge,
                   centre.x, box.getBottom(), stroke * 0.65f);
        g.setColour(tint);
    };
    switch (pad)
    {
        case 0:
        {
            if (!kitTile)
            {
                // Original 1.6.2 sample-panel captures: a thin rim, hollow
                // beater and separate shaft, with the pedal below the rim.
                const float unit = std::min(icon.getWidth(), icon.getHeight()) / 42.0f;
                const auto centre = icon.getCentre() + juce::Point<float>(0.6f, -2.6f) * unit;
                const auto circle = [centre, unit](float diameter, float offsetY = 0.0f) {
                    return juce::Rectangle<float>(diameter * unit, diameter * unit)
                        .withCentre(centre + juce::Point<float>(0.0f, offsetY * unit));
                };
                const juce::Colour outline(0xffd7dbf6);
                g.setColour(outline);
                g.drawEllipse(circle(30.6f), 1.25f * unit);
                g.setColour(outline.withAlpha(0.88f));
                g.drawEllipse(circle(8.0f), 0.95f * unit);
                g.drawEllipse(circle(4.2f, 9.0f), 0.90f * unit);
                g.drawRect(juce::Rectangle<float>(centre.x - 0.9f * unit,
                    centre.y + 11.1f * unit, 1.8f * unit, 7.9f * unit), 0.70f * unit);
                juce::Path pedal;
                pedal.startNewSubPath(centre + juce::Point<float>(-3.0f, 14.7f) * unit);
                pedal.lineTo(centre + juce::Point<float>(3.0f, 14.7f) * unit);
                pedal.lineTo(centre + juce::Point<float>(2.1f, 21.1f) * unit);
                pedal.lineTo(centre + juce::Point<float>(-2.1f, 21.1f) * unit);
                pedal.closeSubPath();
                g.strokePath(pedal, juce::PathStrokeType(0.90f * unit));
                break;
            }
            const auto drum = icon.withSizeKeepingCentre(std::min(icon.getWidth(), icon.getHeight()) * 0.72f,
                                                         std::min(icon.getWidth(), icon.getHeight()) * 0.72f);
            g.drawEllipse(drum, stroke);
            const float hubRatio = kitTile ? 0.22f : 0.26f;
            g.drawEllipse(drum.withSizeKeepingCentre(drum.getWidth() * hubRatio,
                                                     drum.getHeight() * hubRatio), stroke);
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
            drawCymbal(box.translated(0.0f, -width * (kitTile ? 0.04f : 0.08f)),
                       kitTile ? -width * 0.04f : 0.0f);
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
        case 6: drawCymbal(icon, icon.getHeight() * (kitTile ? 0.055f : 0.11f)); break;
        case 7:
        {
            const float width = std::min(icon.getWidth(), icon.getHeight());
            const auto box = icon.withSizeKeepingCentre(width, width);
            // Leave room for the pedal below the stand, as in Samples & Sounds.
            drawCymbal(box.withTrimmedBottom(width * 0.18f), -width * (kitTile ? 0.08f : 0.04f));
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
        case 8: drawCymbal(icon, icon.getHeight() * (kitTile ? -0.115f : 0.14f)); break;
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
            const auto tom = [&g, stroke, kitTile, &box](float x, float y, float w, float h,
                                               bool selected, float kitRimRatio = 0.50f,
                                               bool clearHeadRim = true) {
                const auto shell = box(x, y, w, h);
                const float rimHeight = shell.getHeight() * (kitTile ? kitRimRatio : 0.32f);
                juce::Path body;
                body.startNewSubPath(shell.getX(), shell.getY() + rimHeight * 0.5f);
                body.lineTo(shell.getX(), shell.getBottom() - rimHeight * 0.5f);
                body.cubicTo(shell.getX(), shell.getBottom() + rimHeight * 0.15f,
                             shell.getRight(), shell.getBottom() + rimHeight * 0.15f,
                             shell.getRight(), shell.getBottom() - rimHeight * 0.5f);
                body.lineTo(shell.getRight(), shell.getY() + rimHeight * 0.5f);
                if (selected)
                {
                    if (kitTile)
                    {
                        const auto centre = shell.withHeight(rimHeight).getCentre();
                        body.addCentredArc(centre.x, centre.y, shell.getWidth() * 0.5f,
                                           rimHeight * 0.5f, 0.0f,
                                           juce::MathConstants<float>::halfPi,
                                           -juce::MathConstants<float>::halfPi);
                        body.closeSubPath();
                        // Only the high toms have a clear lower head rim in
                        // original 1.6.2. The floor tom has a continuous body.
                        if (clearHeadRim)
                        {
                            juce::Path rim, gap;
                            rim.addCentredArc(centre.x, centre.y, shell.getWidth() * 0.5f,
                                              rimHeight * 0.5f, 0.0f,
                                              juce::MathConstants<float>::pi * 0.65f,
                                              juce::MathConstants<float>::pi * 1.35f, true);
                            juce::PathStrokeType(stroke * 0.65f).createStrokedPath(gap, rim);
                            body.setUsingNonZeroWinding(false);
                            body.addPath(gap);
                        }
                        g.fillPath(body);
                    }
                    else
                    {
                        body.closeSubPath();
                        g.fillPath(body);
                        g.fillEllipse(shell.withHeight(rimHeight));
                    }
                }
                else
                {
                    g.strokePath(body, juce::PathStrokeType(stroke * 0.65f));
                    g.drawEllipse(shell.withHeight(rimHeight), stroke * 0.65f);
                }
            };
            tom(0.02f, 0.08f, 0.32f, 0.30f, pad == 9);
            tom(0.40f, 0.08f, 0.32f, 0.30f, pad == 10);
            if (kitTile)
            {
                g.setColour(tint.withMultipliedAlpha(0.38f));
                g.drawEllipse(box(0.16f, 0.44f, 0.44f, 0.50f), stroke * 0.65f);
                g.setColour(tint);
                tom(0.634f, 0.465f, 0.363f, 0.43f, pad == 11, 0.45f, false);
            }
            else
            {
                g.drawEllipse(box(0.16f, 0.47f, 0.44f, 0.47f), stroke * 0.65f);
                tom(0.65f, 0.48f, 0.33f, 0.41f, pad == 11);
            }
            if (kitTile)
                g.setColour(tint.withMultipliedAlpha(0.38f));
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
                    g.reduceClipRegion(head.withHeight(head.getHeight() * (kitTile ? 0.47f : 0.64f)).toNearestInt());
                    g.fillPath(upper);
                }
                {
                    juce::Graphics::ScopedSaveState saved(g);
                    g.addTransform(rotation);
                    g.reduceClipRegion(head.withTrimmedTop(head.getHeight() * (kitTile ? 0.57f : 0.77f)).toNearestInt());
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
                // Original 1.6.2 kit captures retain the faint outer contour;
                // the three inner contours sit about one pixel farther in.
                const float inset = ring > 0 ? 0.08f : 0.0f;
                constexpr float largeDiameter[] { 0.82f, 0.66f, 0.42f, 0.16f };
                const float diameter = width * (kitTile ? 0.82f - ring * 0.20f - inset
                                                        : largeDiameter[ring]);
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
                // Original 1.6.2 kit captures 18/23: the two major marks are
                // outlined five-point stars; the smaller glints stay filled.
                const bool outlined = sparkle[2] >= 0.08f;
                const float radius = width * sparkle[2];
                juce::Path star;
                star.addStar({ box.getX() + width * (sparkle[0] - (outlined ? 0.02f : 0.0f)),
                               box.getY() + width * (sparkle[1] - (outlined ? 0.035f : 0.0f)) },
                             outlined ? 5 : 4, radius * (outlined ? 0.45f : 0.38f), radius);
                if (outlined)
                    g.strokePath(star, juce::PathStrokeType(stroke * 0.48f,
                                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                else
                    g.fillPath(star);
            }
            break;
        }
    }
}

BeatScholarLookAndFeel::BeatScholarLookAndFeel(bool factorySampleMenu)
    : factorySampleMenu_(factorySampleMenu)
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
        palette_.patternCanvas = 0xff0f0f0f;
        palette_.barHeaderDivider = 0xff303030;
        palette_.panelRaised = 0xff232323;
        palette_.laneControlIdle = 0xff141414;
        palette_.arrangementCard = 0xff232323;
        palette_.verticalScrollGutter = palette_.panel;
        palette_.horizontalScrollThumb = 0xff3c3c3c;
        palette_.header = 0xff232323;
        palette_.lowerHeader = palette_.header;
        palette_.lowerBorder = 0xff3c3c3c;
        palette_.lowerBodyCentre = palette_.panel;
        palette_.lowerBodyEdge = palette_.panel;
        palette_.lowerBodyFrame = palette_.lowerBorder;
        palette_.gridBandCentre = palette_.header;
        palette_.gridBandEdge = palette_.header;
        palette_.grid = 0xff191919;
        palette_.border = 0xff3c3c3c;
        palette_.emptyBeatOutline = 0xff3c3c3c;
        palette_.populatedBeatOutline = palette_.text;
        palette_.borderBright = 0xff747474;
        palette_.zoomRail = palette_.borderBright;
        palette_.zoomThumb = palette_.text;
        palette_.laneControlBorder = palette_.borderBright;
        palette_.laneControlText = palette_.text;
    }
    setColour(juce::PopupMenu::backgroundColourId,
              juce::Colour(darkMode_ ? palette_.panelRaised : 0xff282858U));
    setColour(juce::PopupMenu::textColourId,
              juce::Colour(factorySampleMenu_ && !darkMode_ ? 0xffffffffU : palette_.text));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(palette_.accent));
    setColour(juce::PopupMenu::highlightedTextColourId,
              juce::Colour(factorySampleMenu_ && !darkMode_ ? 0xffffffffU : palette_.text));
    setColour(juce::Label::textColourId, juce::Colour(palette_.text));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(palette_.text));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(palette_.panel));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(palette_.borderBright));
    setColour(juce::ComboBox::textColourId, juce::Colour(palette_.text));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(palette_.textMuted));
}

juce::Font BeatScholarLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    if (button.getComponentID() == "random.save.accept")
        return font(static_cast<float>(buttonHeight) * 0.56f, true);
    if (button.getComponentID() == "toolbar.preset")
        return font(static_cast<float>(buttonHeight) * 0.625f, true);
    if (button.getComponentID() == "sound.title" || button.getComponentID() == "kit.menu")
        return font(static_cast<float>(buttonHeight) * (2.0f / 3.0f), true);
    if (button.getComponentID() == "arrangement.toggle")
        return font(static_cast<float>(buttonHeight) * 0.82f, true);
    return font(std::clamp(static_cast<float>(buttonHeight) * 0.42f, 9.0f, 17.0f), true);
}

juce::Font BeatScholarLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont().getHeight() > 0.0f ? label.getFont() : font(12.0f);
}

juce::Font BeatScholarLookAndFeel::getPopupMenuFont()
{
    // Embed the reference family so menus do not depend on host-installed fonts.
    static const auto typeface = juce::Typeface::createSystemTypefaceFor(
        BeatScholarFonts::MontserratSemiBold_ttf,
        BeatScholarFonts::MontserratSemiBold_ttfSize);
    return juce::Font(juce::FontOptions(typeface).withHeight(factorySampleMenu_ ? 18.0f : 17.0f));
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
    g.setColour(darkMode_ ? juce::Colour(palette_.background) : juce::Colours::black);
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
    // Saved original GUI: a thin curved stem and a filled, angled head.
    juce::Path stem, head;
    stem.startNewSubPath(10.0f, 8.0f);
    stem.cubicTo(23.0f, 5.0f, 27.0f, 17.0f, 19.0f, 23.0f);
    head.startNewSubPath(3.0f, 11.0f);
    head.lineTo(10.0f, 2.0f);
    head.lineTo(12.0f, 15.0f);
    head.closeSubPath();
    const float side = std::min(area.getWidth(), area.getHeight()) * 0.66f;
    auto transform = juce::AffineTransform::scale(side / 24.0f)
        .translated(area.getCentreX() - side * 0.5f, area.getCentreY() - side * 0.5f);
    if (clockwise)
        transform = transform.followedBy(juce::AffineTransform::scale(-1.0f, 1.0f)
            .translated(area.getCentreX() * 2.0f, 0.0f));
    stem.applyTransform(transform);
    head.applyTransform(transform);
    g.setColour(tint);
    g.strokePath(stem, juce::PathStrokeType(side / 16.0f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillPath(head);
}

void BeatScholarLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                   const juce::Colour&,
                                                   bool isMouseOver, bool isDown)
{
    const auto id = button.getComponentID();
    auto area = button.getLocalBounds().toFloat();
    if (id == "random.save.accept" || id == "random.save.add-category")
    {
        g.setColour(juce::Colour(id == "random.save.accept" ? 0xff3c3c72 : 0xff1b1b39)
                        .brighter(isDown ? 0.15f : isMouseOver ? 0.06f : 0.0f));
        g.fillRoundedRectangle(area.reduced(0.5f), id == "random.save.accept" ? 2.0f : 0.0f);
        g.setColour(juce::Colour(id == "random.save.accept" ? 0xff080818 : 0xffd0d0e5));
        g.drawRoundedRectangle(area.reduced(0.5f), id == "random.save.accept" ? 2.0f : 0.0f, 1.0f);
        return;
    }
    if (id == "kit.map" || id == "sample.oneshot")
    {
        if (isMouseOver || isDown)
        {
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.12f : 0.06f));
            g.fillRect(area);
        }
        return;
    }
    if (id.startsWith("pad."))
    {
        if (palette_.panel == colour::panel)
        {
            // Fitted to neutral pad interiors in the original 1.6.2 captures.
            // The shading restarts in each pad, independently of the module.
            const juce::Colour centre(0xff181832), edge(0xff1e1e41);
            juce::ColourGradient gradient(centre, 0.0f, 0.0f, edge, 1.0f, 0.0f, true);
            for (int step = 1; step < 16; ++step)
            {
                const float radius = step / 16.0f;
                gradient.addColour(radius, edge.interpolatedWith(centre,
                                              std::pow(1.0f - radius, 1.241f)));
            }
            juce::FillType fill(gradient);
            fill.transform = juce::AffineTransform::scale(area.getWidth() * 0.603f,
                                                         area.getHeight() * 0.576f)
                                 .translated(area.getCentreX(), area.getCentreY());
            g.setFillType(fill);
        }
        else
            g.setColour(juce::Colour(palette_.panel));
        g.fillRect(area);
        const bool mapping = bool(button.getProperties().getWithDefault("midiMapVisible", false));
        // The reference strip sits below the pad frame (4px at the 960px editor size).
        const float stripHeight = std::max(3.0f, std::round(area.getHeight() * 0.09f));
        const auto strip = area.withTrimmedTop(stripHeight).withHeight(stripHeight)
                               .reduced(1.0f, 0.0f);
        // Original MAP mode leaves the pad strip at 20% over its overlay background.
        if (mapping)
        {
            g.setColour(juce::Colour(palette_.panel == colour::panel ? 0xff181834 : palette_.panel));
            g.fillRect(strip);
        }
        g.setColour(button.findColour(juce::TextButton::buttonOnColourId)
                        .withAlpha(mapping ? 0.20f : 1.0f));
        g.fillRect(strip);
        const float activity = std::clamp(static_cast<float>(button.getProperties().getWithDefault("playbackActivity", 0.0f)), 0.0f, 1.0f);
        // Selection changes the frame only; colour fill follows the audio level.
        if (activity > 0.0f)
        {
            g.setColour(button.findColour(juce::TextButton::buttonOnColourId).withAlpha(activity));
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

    if (id == "arrangement.add")
    {
        g.setColour(juce::Colour(palette_.patternCanvas));
        g.fillRect(area);
        g.setColour(juce::Colour(palette_.emptyBeatOutline));
        g.drawRect(area.reduced(0.5f), 1.0f);
        if (isMouseOver || isDown)
        {
            g.setColour(juce::Colour(palette_.text).withAlpha(isDown ? 0.16f : 0.07f));
            g.fillRect(area.reduced(1.0f));
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

    if (id.startsWith("toolbar.") || id.startsWith("brush.")
        || bool(button.getProperties().getWithDefault("flatButton", false)))
    {
        // The original Drum brush shows its colored circle without a selected box.
        const bool selectedBackground = button.getToggleState() && id != "brush.drum";
        if (selectedBackground || isMouseOver || isDown)
        {
            g.setColour(juce::Colour(selectedBackground ? palette_.header : palette_.panelRaised)
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
        // Saved Windows reference: factory arrow cells use the subdued card
        // fill (35,35,87), not the brighter shared control border.
        g.setColour(juce::Colour(darkMode_ ? palette_.border : 0xff232357U));
        // Factory selectors use filled arrow cells, without an outer outline.
        const auto gutter = area.getHeight();
        g.fillRect(area.removeFromLeft(gutter));
        g.fillRect(area.removeFromRight(gutter));
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
    if (id == "random.save.add-category")
    {
        g.setColour(juce::Colour(0xffe7e7f7));
        const auto cross = area.withSizeKeepingCentre(area.getHeight() * 0.6f, area.getHeight() * 0.6f);
        g.drawLine(cross.getX(), cross.getCentreY(), cross.getRight(), cross.getCentreY(), 1.2f);
        g.drawLine(cross.getCentreX(), cross.getY(), cross.getCentreX(), cross.getBottom(), 1.2f);
        return;
    }
    auto tint = button.isEnabled() || static_cast<bool>(button.getProperties()["randomizerModuleBody"])
                                  ? juce::Colour(palette_.text)
                                   : juce::Colour(palette_.textMuted).withAlpha(0.55f);
    if (id == "kit.map")
    {
        // Compact keyboard/MAP glyph in the original kit header; the hit
        // target stays larger than the drawing and retains keyboard focus.
        const auto icon = area.withSizeKeepingCentre(area.getWidth() * 0.60f,
                                                     area.getHeight() * 0.72f);
        g.setColour(tint.withAlpha(button.getToggleState() ? 1.0f : 0.60f));
        const float keyWidth = icon.getWidth() / 5.0f;
        for (int key = 0; key < 5; ++key)
            g.fillRect(icon.getX() + key * keyWidth, icon.getY(),
                       keyWidth * 0.72f, icon.getHeight());
        const auto label = icon.withSizeKeepingCentre(icon.getWidth(), icon.getHeight() * 0.55f);
        g.setColour(juce::Colour(palette_.header));
        g.fillRect(label);
        g.setColour(tint);
        g.setFont(font(std::max(6.0f, label.getHeight()), true));
        g.drawText("MAP", label, juce::Justification::centred);
        return;
    }
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
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 1),
                         juce::Justification::centred, 1);
        return;
    }
    if (id.startsWith("pad."))
    {
        const int pad = id.fromFirstOccurrenceOf("pad.", false, false).getIntValue();
        auto icon = area.reduced(std::max(5.0f, area.getWidth() * 0.13f),
                                 std::max(6.0f, area.getHeight() * 0.14f));
        float stroke = std::max(1.0f, std::min(area.getWidth(), area.getHeight()) * 0.035f);
        if (pad == 1 || pad == 2)
        {
            // Original 1.6.2 kit captures 18/23: both Snare shells are about
            // 28px wide at 1280px. Keep sample-panel sizing separate.
            const float shift = icon.getWidth() * 0.069f;
            const float horizontalShift = -icon.getWidth() * (pad == 1 ? 0.0276f : 0.0138f);
            const float widthScale = pad == 1 ? 0.80f : 0.75f;
            icon = icon.withSizeKeepingCentre(icon.getWidth() * widthScale,
                                               icon.getHeight() * 0.80f).translated(horizontalShift, shift);
            stroke *= 0.80f;
        }
        // Original 1.6.2 kit captures 18/23 (zoom-rail-original-20261126).
        // These proportions belong to the small kit tiles; the sample-panel
        // drawings retain their own size. Toms are shallower than square.
        float scaleX = 1.0f, scaleY = 1.0f, shiftX = 0.0f, shiftY = 0.0f;
        switch (pad)
        {
            case 0: scaleX = scaleY = 0.90f; shiftY = 3.5f; break;
            case 3: scaleX = scaleY = 0.72f; shiftX = -1.5f; shiftY = 3.0f; break;
            case 4: scaleX = scaleY = 0.80f; shiftX = -0.5f; shiftY = 1.0f; break;
            case 5: scaleX = scaleY = 0.80f; shiftY = 1.5f; break;
            case 6: scaleY = 0.78f; shiftX = -1.0f; shiftY = 2.0f; break;
            case 7: scaleX = 1.25f; scaleY = 0.86f; shiftX = -0.5f; shiftY = 4.0f; break;
            case 8: scaleX = 0.82f; scaleY = 0.80f; shiftY = 0.8f; break;
            case 9: case 10: case 11:
                scaleX = 0.87f; scaleY = 0.75f; shiftY = 3.5f; break;
            case 12: scaleX = 0.82f; scaleY = 0.84f; shiftX = -0.5f; shiftY = 3.5f; break;
            case 13: scaleX = 0.73f; scaleY = 0.70f; shiftX = -1.5f; shiftY = 5.0f; break;
            case 14: scaleX = scaleY = 0.67f; shiftX = -0.5f; shiftY = 3.5f; break;
            case 15: scaleX = 0.74f; scaleY = 0.70f; shiftX = -1.0f; shiftY = 5.5f; break;
            default: break;
        }
        const float tileScale = area.getHeight() / 58.0f;
        juce::Graphics::ScopedSaveState saved(g);
        g.addTransform(juce::AffineTransform::scale(scaleX, scaleY, icon.getCentreX(), icon.getCentreY())
                           .translated(shiftX * tileScale, shiftY * tileScale));
        const bool mapping = bool(button.getProperties().getWithDefault("midiMapVisible", false));
        const auto glyphTint = darkMode_ ? tint : juce::Colour(0xffd7dbf6U);
        const float alpha = mapping ? 0.15f : darkMode_ ? 0.90f : pad == 15 ? 0.80f : 1.0f;
        // Small outlined kit icons use finer strokes than the sample drawings.
        // Fit to original 1.6.2 captures 18/23, keeping the measured tint intact.
        constexpr float outlineWidths[] { 0.67f, 0.72f, 0.84f, 0.60f };
        if (pad >= 0 && pad < 4)
            stroke *= outlineWidths[pad];
        drawInstrumentGlyph(g, icon, pad, glyphTint.withAlpha(alpha), stroke, true);
        return;
    }
    if (id == "toolbar.play")
        return drawTriangle(g, area, true, button.getToggleState() ? juce::Colour(palette_.green) : tint);
    if (id == "toolbar.stop")
    {
        g.setColour(tint);
        // Align the solid square to pixels: fractional edges produced a 20px
        // fringe around the reference's 19px square at the 960px editor size.
        const int side = juce::roundToInt(std::min(area.getWidth(), area.getHeight()) * 0.74f);
        g.fillRect(juce::roundToInt(area.getCentreX() - side * 0.5f),
                   juce::roundToInt(area.getCentreY() - side * 0.5f), side, side);
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
        const auto side = std::min(area.getWidth(), area.getHeight());
        const auto circle = area.withSizeKeepingCentre(side * 0.50f, side * 0.50f)
                                .translated(0.0f, side * 0.04f);
        g.setColour(button.getToggleState() ? tint : juce::Colour(palette_.textMuted));
        g.drawEllipse(circle, side * 0.045f);
        // Reference slicing tool: round head with a diagonal stem, not clock hands.
        g.drawLine(circle.getCentreX(), circle.getCentreY(),
                   circle.getCentreX() + side * 0.24f,
                   circle.getCentreY() + side * 0.40f, side * 0.10f);
        return;
    }
    if (id == "brush.velocity")
    {
        g.setColour(button.getToggleState() ? tint
            : juce::Colour(darkMode_ ? palette_.textMuted : 0xff4d4d58U));
        const auto bars = area.reduced(area.getWidth() * 0.22f, area.getHeight() * 0.22f);
        const float gap = bars.getWidth() * 0.08f;
        const float width = (bars.getWidth() - gap * 2.0f) / 3.0f;
        for (int index = 0; index < 3; ++index)
        {
            const float height = bars.getHeight() * (0.5f + static_cast<float>(index) * 0.25f);
            g.fillRect(bars.getX() + static_cast<float>(index) * (width + gap),
                       bars.getBottom() - height, width, height);
        }
        return;
    }
    if (id == "brush.dice")
    {
        const auto side = std::min(area.getWidth(), area.getHeight());
        return drawDice(g, area.withSizeKeepingCentre(side * 0.75f, side * 0.75f)
                              .translated(0.0f, side * 0.08f),
                        button.getToggleState() ? tint
                            : juce::Colour(darkMode_ ? palette_.textMuted : 0xff737373U));
    }
    if ((id.startsWith("random.module.") || id == "random.fillEmpty"))
    {
        const auto diameter = std::min(area.getWidth(), area.getHeight()) * 0.65f;
        const auto dot = area.withSizeKeepingCentre(diameter, diameter);
        g.setColour(tint);
        g.drawEllipse(dot, std::max(1.0f, diameter * 0.10f));
        if (button.getToggleState())
            g.fillEllipse(dot);
        return;
    }
    if (id == "sample.layer")
    {
        auto textArea = area.reduced(std::max(13.0f, area.getWidth() * 0.16f), 1.0f);
        auto arrows = area;
        const auto gutter = arrows.getHeight();
        const auto inset = gutter * 0.22f;
        drawTriangle(g, arrows.removeFromLeft(gutter).reduced(inset), false, tint);
        drawTriangle(g, arrows.removeFromRight(gutter).reduced(inset), true, tint);
        g.setColour(tint);
        // Matched 960px reference: sample labels have six-pixel ink height.
        g.setFont(font(button.getHeight() * 0.68f, true));
        g.drawFittedText(button.getButtonText(), textArea.toNearestInt(),
                         juce::Justification::centred, 1, 0.80f);
        return;
    }
    if (id == "sample.folder")
    {
        // The reference has a dim upright tab behind the sloping open front.
        const auto folder = area.withX(area.getWidth() * 0.25f)
                                .withWidth(area.getWidth() * 0.57f)
                                .withY(area.getHeight() * 0.20f)
                                .withHeight(area.getHeight() * 0.45f);
        const auto point = [&folder](float x, float y) {
            return juce::Point<float>(folder.getX() + folder.getWidth() * x,
                                      folder.getY() + folder.getHeight() * y);
        };
        juce::Path back;
        back.startNewSubPath(point(0.0f, 0.0f));
        back.lineTo(point(0.25f, 0.0f));
        back.lineTo(point(0.50f, 0.25f));
        back.lineTo(point(0.90f, 0.25f));
        back.lineTo(point(0.90f, 1.0f));
        back.lineTo(point(0.0f, 1.0f));
        back.closeSubPath();
        g.setColour(tint.withMultipliedAlpha(0.55f));
        g.fillPath(back);
        juce::Path front;
        front.startNewSubPath(point(0.15f, 0.38f));
        front.lineTo(point(1.0f, 0.38f));
        front.lineTo(point(0.85f, 1.0f));
        front.lineTo(point(0.0f, 1.0f));
        front.closeSubPath();
        g.setColour(tint);
        g.fillPath(front);
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
            g.fillEllipse(dot);
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
        // JUCE arc angles start at twelve o'clock; sound radiates right.
        wave.addArc(speaker.getRight() - speaker.getWidth() * 0.42f, speaker.getY(),
                    speaker.getWidth() * 0.42f, speaker.getHeight(),
                    juce::MathConstants<float>::halfPi - 0.8f,
                    juce::MathConstants<float>::halfPi + 0.8f, true);
        g.strokePath(wave, juce::PathStrokeType(1.4f));
        return;
    }
    if (id == "kit.lock")
    {
        auto lock = area.reduced(area.getWidth() * 0.30f, area.getHeight() * 0.18f);
        const auto body = lock.removeFromBottom(lock.getHeight() * 0.58f);
        g.setColour(tint.withAlpha(button.getToggleState() ? 1.0f : 0.45f));
        g.fillRoundedRectangle(body, 1.2f);
        juce::Path shackle;
        // JUCE angles begin at twelve o'clock: this is the upper half,
        // not the left half produced by pi..twoPi.
        shackle.addArc(lock.getX() + lock.getWidth() * 0.17f,
                       lock.getY() + lock.getHeight() * 0.18f,
                       lock.getWidth() * 0.66f, lock.getHeight() * 1.22f,
                       -juce::MathConstants<float>::halfPi,
                       juce::MathConstants<float>::halfPi, true);
        shackle.lineTo(body.getRight() - body.getWidth() * 0.17f, body.getY());
        g.strokePath(shackle, juce::PathStrokeType(1.3f));
        return;
    }
    if (id == "master.settings")
    {
        const auto centre = area.getCentre();
        const float outer = std::min(area.getWidth(), area.getHeight()) * 0.29f;
        juce::Path cog;
        for (int point = 0; point < 32; ++point)
        {
            const float angle = juce::MathConstants<float>::twoPi * static_cast<float>(point) / 32.0f;
            const float radius = outer * ((point % 4 == 1 || point % 4 == 2) ? 1.0f : 0.76f);
            const auto x = centre.x + std::cos(angle) * radius;
            const auto y = centre.y + std::sin(angle) * radius;
            if (point == 0) cog.startNewSubPath(x, y);
            else cog.lineTo(x, y);
        }
        cog.closeSubPath();
        cog.addEllipse(juce::Rectangle<float>(outer * 0.72f, outer * 0.72f).withCentre(centre));
        cog.setUsingNonZeroWinding(false);
        g.setColour(tint);
        g.fillPath(cog);
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
                                                : masterDial ? 0xff282856U : palette_.panelRaised));
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
        g.setColour(juce::Colour(randomizerLevel ? 0xff6566a1U
                                                : masterDial ? 0xff6c6cabU : palette_.borderBright));
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
    if (slider.getComponentID() == "toolbar.velocity")
    {
        // This control paints a numeric readout, not a thumb. JUCE's thumb
        // insets leave only four pixels for three digits at the 960px size.
        layout.sliderBounds = slider.getLocalBounds();
    }
    else if (slider.getComponentID() == "toolbar.swing")
    {
        layout.sliderBounds = slider.getLocalBounds();
    }
    else if (slider.getComponentID() == "effect.9" && slider.isVertical())
    {
        // Original 960px client: the small Gain rail spans about 24px.
        const int inset = juce::roundToInt(static_cast<float>(slider.getHeight()) * 8.0f / 55.0f);
        layout.sliderBounds = slider.getLocalBounds().reduced(0, inset);
    }
    else if (slider.getComponentID() == "master.4")
    {
        // Keep the gain gesture range aligned with the painted stereo meters
        // at every editor size, rather than JUCE's fixed 12-pixel thumb inset.
        const int inset = juce::roundToInt(static_cast<float>(slider.getHeight()) * 12.0f / 118.0f);
        layout.sliderBounds = slider.getLocalBounds().reduced(0, inset);
    }
    else if (slider.getComponentID() == "sound.5" && slider.isVertical())
    {
        // Match the reference Level rail and mouse travel at every editor size.
        const int inset = juce::roundToInt(static_cast<float>(slider.getHeight()) * 4.0f / 97.0f);
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
        // The BPM readout is text, so JUCE's inset slider-track height must
        // not shrink its type. Match the original toolbar at the editor scale.
        const auto textHeight = id == "toolbar.bpm"
            ? static_cast<float>(slider.getHeight()) * 0.55f
            : static_cast<float>(slider.getHeight()) * 0.43f;
        g.setFont(getPopupMenuFont().withHeight(textHeight));
        const auto suffix = id == "toolbar.bpm" ? " BPM" : "";
        const auto textArea = id == "toolbar.bpm"
            ? area.translated(0.0f, static_cast<float>(slider.getHeight()) * 0.125f)
            : area.translated(-static_cast<float>(slider.getWidth()) / 24.0f,
                              static_cast<float>(slider.getHeight()) / 7.0f);
        juce::Graphics::ScopedSaveState savedState(g);
        if (id == "toolbar.bpm")
        {
            // Matched 960px reference: the readout spans 50px at an 8px
            // glyph height. Keep that aspect ratio independent of text fitting.
            g.addTransform(juce::AffineTransform::scale(1.16f, 1.0f,
                                                       textArea.getCentreX(), textArea.getCentreY()));
        }
        g.drawFittedText(juce::String(static_cast<int>(std::lround(slider.getValue()))) + suffix,
                         textArea.toNearestInt(), juce::Justification::centred, 1);
        return;
    }
    if (id == "toolbar.swing")
    {
        // Original 960px capture: two solid rail rows with fractional outer
        // edges, centred at y=32. Align the thumb without changing interaction bounds.
        const float scale = area.getHeight() / 20.0f;
        const auto track = juce::Rectangle<float>(
            area.getX(), area.getCentreY() - 2.5f * scale,
            area.getWidth() - 0.5f * scale, 11.0f * scale / 3.0f);
        g.setColour(juce::Colour(darkMode_ ? palette_.border : 0xff343463));
        g.fillRect(track);
        g.setColour(juce::Colour(darkMode_ ? palette_.text : 0xffd7dbf6));
        g.fillEllipse(juce::Rectangle<float>(8.0f * scale, 8.0f * scale)
                          .withCentre({ sliderPos, track.getCentreY() }));
        return;
    }
    if (id == "master.4")
    {
        // The editor paints the live left/right meter rails behind this thumb.
        // Repainting opaque rails here would hide their output levels.
        g.setColour(juce::Colour(palette_.text));
        g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth() * 0.60f,
                                                           area.getHeight() * (10.0f / 94.0f))
                                   .withCentre({ area.getCentreX(), sliderPos }),
                               area.getWidth() * 0.04f);
        return;
    }
    if (id == "sound.5" && style == juce::Slider::LinearVertical)
    {
        const auto track = area.withSizeKeepingCentre(area.getWidth() * (6.0f / 42.0f),
                                                       area.getHeight());
        g.setColour(juce::Colour(darkMode_ ? 0xff282828U : 0xff282856U));
        g.fillRoundedRectangle(track, track.getWidth() * 0.5f);
        g.setColour(juce::Colour(palette_.text));
        g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth() * (20.0f / 42.0f),
                                                           area.getHeight() * (8.0f / 89.0f))
                                   .withCentre({ area.getCentreX(), sliderPos }),
                               area.getWidth() * 0.04f);
        return;
    }
    if (id == "effect.9" && style == juce::Slider::LinearVertical)
    {
        // Saved original: a thin rail and flat thumb, not a rounded capsule.
        const float scale = static_cast<float>(slider.getWidth()) / 54.0f;
        const auto track = area.withSizeKeepingCentre(2.0f * scale, area.getHeight() * 0.82f);
        g.setColour(juce::Colour(palette_.border));
        g.fillRect(track);
        g.setColour(juce::Colour(palette_.text));
        g.fillRect(juce::Rectangle<float>(10.0f * scale, 2.0f * scale)
                       .withCentre({ track.getCentreX(), sliderPos }));
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
    if (id == "sound.6" && style == juce::Slider::LinearHorizontal)
    {
        // Saved original at 960 px: centered pan handle is 2 px wide and 9 px tall.
        g.fillRoundedRectangle(juce::Rectangle<float>(area.getHeight() / 6.0f,
                                                       area.getHeight() * (2.0f / 3.0f))
                                   .withCentre({ sliderPos, track.getCentreY() }),
                               area.getHeight() / 12.0f);
        return;
    }
    const float thumb = std::max(8.0f, area.getHeight() * 0.35f);
    g.fillEllipse(juce::Rectangle<float>(thumb, thumb)
                      .withCentre({ sliderPos, track.getCentreY() }));
}

void BeatScholarLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                           int, int, int, int, juce::ComboBox& box)
{
    if (box.getComponentID() == "random.save.category")
    {
        g.fillAll(juce::Colour(0xff292954));
        g.setColour(juce::Colour(0xff525272));
        g.drawRect(0, 0, width, height);
        const auto cx = static_cast<float>(width) - static_cast<float>(height) * 0.4f;
        const auto cy = static_cast<float>(height) * 0.5f;
        const auto radius = static_cast<float>(height) * 0.22f;
        juce::Path arrow;
        arrow.addTriangle(cx - radius, cy - radius * 0.5f, cx + radius, cy - radius * 0.5f,
                          cx, cy + radius);
        g.setColour(juce::Colour(0xffd0d0e5));
        g.fillPath(arrow);
        return;
    }
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
    if (box.getComponentID() == "random.save.category")
    {
        label.setBounds(0, 0, std::max(0, box.getWidth() - box.getHeight()), box.getHeight());
        label.setJustificationType(juce::Justification::centred);
        label.setFont(font(static_cast<float>(box.getHeight()) * 0.65f));
        return;
    }
    label.setBounds(5, 1, box.getWidth() - box.getHeight(), box.getHeight() - 2);
    label.setFont(font(std::max(9.0f, static_cast<float>(box.getHeight()) * 0.44f), true));
}

void BeatScholarLookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions(
    const juce::String& text, int standardHeight, int& width, int& height,
    const juce::PopupMenu::Options& options)
{
    juce::LookAndFeel_V4::getIdealPopupMenuSectionHeaderSizeWithOptions(
        text, standardHeight, width, height, options);
    // Swing includes its divider here: 33px header + three 23px rows + 4px border.
    if (text == "Swing Ref:")
    {
        // The reference menu is 131 px including its two 2 px insets.
        // Font substitution must not expand the section header beyond it.
        width = 127;
        height = 33;
    }
}

void BeatScholarLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
                                                       const juce::Rectangle<int>& area,
                                                       const juce::String& sectionName)
{
    if (sectionName != "Swing Ref:")
    {
        juce::LookAndFeel_V4::drawPopupMenuSectionHeader(g, area, sectionName);
        return;
    }
    // The original Swing title shares the option-label gutter and is muted.
    g.setFont(getPopupMenuFont().withHeight(18.0f).boldened());
    g.setColour(juce::Colour(darkMode_ ? palette_.textMuted : 0xff85859bU));
    g.drawFittedText(sectionName, area.getX() + 21, area.getY() - 4, area.getWidth() - 25,
                     24, juce::Justification::bottomLeft, 1);
    // Original 131x106 menu: divider at y29, with the 2px menu border above.
    g.setColour(juce::Colour(darkMode_ ? palette_.border : 0xff68688aU));
    // Header bounds already include the 2px horizontal menu inset.
    g.fillRect(area.getX() + 3, area.getY() + 27, area.getWidth() - 6, 1);
}

void BeatScholarLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    // Sampled from the original Swing and Bars context-menu captures.
    g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    g.setColour(juce::Colour(darkMode_ ? palette_.borderBright : 0xffafafafU));
    g.drawRect(juce::Rectangle<int>(0, 0, width, height), 1);
}

void BeatScholarLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                bool separator, bool active, bool highlighted,
                                                bool ticked, bool submenu, const juce::String& text,
                                                const juce::String& shortcut,
                                                const juce::Drawable* icon,
                                                const juce::Colour* textColour)
{
    if (factorySampleMenu_)
    {
        // Factory lists use JUCE's 23px rows and height-constrained text; the
        // reference Bars/Swing menus have separate, fixed-gutter geometry.
        juce::LookAndFeel_V4::drawPopupMenuItem(g, area, separator, active, highlighted,
                                              ticked, submenu, text, shortcut, icon, textColour);
        return;
    }
    if (separator)
    {
        g.setColour(juce::Colour(darkMode_ ? palette_.border : 0xff68688aU));
        // Original popup divider spans x=5..125 in a 131-pixel menu.
        g.fillRect(area.reduced(5, 0).withHeight(1).withCentre(area.getCentre()));
        return;
    }
    if (highlighted && active)
    {
        g.setColour(juce::Colour(palette_.accent));
        g.fillRect(area.reduced(1, 0));
    }
    auto content = area.reduced(9, 1);
    // Original Bars and Swing menus reserve a fixed gutter for the checkmark.
    // Taller rows must not push their labels further to the right.
    const int markWidth = icon != nullptr ? std::max(17, content.getHeight()) : 12;
    auto mark = content.removeFromLeft(markWidth);
    auto arrow = content.removeFromRight(submenu ? std::max(17, content.getHeight()) : 0);
    const auto tint = textColour != nullptr ? *textColour
                     : active ? juce::Colour(darkMode_ ? palette_.text : 0xffffffffU)
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
