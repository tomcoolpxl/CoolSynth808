#include "Hardware.h"
#include "Text.h"
#include "Theme.h"

namespace cs808
{
    namespace
    {
        juce::Colour rgb(std::uint32_t c, float a = 1.0f) { return juce::Colour(0xff000000u | c).withAlpha(a); }

        /** A vertical gradient through three stops (the middle one at 'mid'). */
        juce::ColourGradient vertical(juce::Rectangle<float> r, juce::Colour top, juce::Colour middle, float mid, juce::Colour bottom)
        {
            juce::ColourGradient grad(top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false);
            grad.addColour(mid, middle);
            return grad;
        }

        /** A radial glow from a point: a few soft rings, as a blurred light. */
        void glow(juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour)
        {
            juce::ColourGradient grad(colour, c.x, c.y, colour.withAlpha(0.0f), c.x + radius, c.y, true);
            g.setGradientFill(grad);
            g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(c));
        }

        /** A round thing's soft shadow, offset: full under it, fading out over 'blur' px past its edge (D306: a
            pattern key's press took 150 ms to show, much of it shadows). A DropShadow blurs an image on every paint. */
        void ellipseShadow(juce::Graphics& g, juce::Rectangle<float> shape, float blur, juce::Point<float> offset, juce::Colour colour)
        {
            // Stacked solid discs, widest first: gradients cost far more than solid fills in the window's renderer.
            const int layers = juce::jlimit(2, 6, juce::roundToInt(blur));
            const auto base = shape.translated(offset.x, offset.y).reduced(blur * 0.5f);
            g.setColour(colour.withMultipliedAlpha(1.0f / static_cast<float>(layers)));
            for (int i = layers; i >= 1; --i)
                g.fillEllipse(base.expanded(blur * static_cast<float>(i) / static_cast<float>(layers)));
        }

        /** A rounded box's soft shadow (D306): stacked solid boxes, widest first, as a blur looks; the DropShadow it
            replaces draws eight gradient pieces, which cost close to a millisecond a key in the window's renderer. */
        void boxShadow(juce::Graphics& g, juce::Rectangle<float> box, float corner, float blur, juce::Point<float> offset, juce::Colour colour)
        {
            const int layers = juce::jlimit(2, 8, juce::roundToInt(blur * 0.75f));
            const auto base = box.translated(offset.x, offset.y).reduced(blur * 0.5f);
            g.setColour(colour.withMultipliedAlpha(1.0f / static_cast<float>(layers)));
            for (int i = layers; i >= 1; --i)
            {
                const float e = blur * static_cast<float>(i) / static_cast<float>(layers);
                g.fillRoundedRectangle(base.expanded(e), corner + e);
            }
        }
    }

    // ---- Keys -------------------------------------------------------------------------------------------------

    void drawKeyCap(juce::Graphics& g, juce::Rectangle<float> area, Cap cap, bool hover, bool pressed, float radius)
    {
        auto r = area.reduced(1.0f);
        if (pressed)
            r = r.translated(0.0f, 1.0f);
        // the drop shadow under a raised key, and the glow round a lit one
        if (cap == Cap::backlit)
            boxShadow(g, r, radius, 12.0f, {}, boardLook().backlit[3].withAlpha(0.55f));
        else if (! pressed)
            boxShadow(g, r, radius, 4.0f, { 0.0f, 2.0f }, juce::Colours::black.withAlpha(0.55f));
        // the dark surround
        g.setColour(rgb(0x0b0a09));
        g.fillRoundedRectangle(r.expanded(1.0f), radius + 1.0f);
        // the cap
        switch (cap)
        {
            case Cap::plain:
            {
                const auto& s = hover ? boardLook().capHover : boardLook().cap; // the theme's (D307)
                g.setGradientFill(vertical(r, s[0], s[1], 0.46f, s[2]));
                break;
            }
            case Cap::selected:
            {
                const auto& s = boardLook().capSelected;
                g.setGradientFill(vertical(r, s[0], s[1], 0.48f, s[2]));
                break;
            }
            case Cap::hot:
            case Cap::orange:
            {
                const auto& s = hover ? boardLook().hotHover : boardLook().hot; // the theme's signal colour (D307)
                g.setGradientFill(vertical(r, s[0], s[1], 0.5f, s[2]));
                break;
            }
            case Cap::backlit:
            {
                const auto& s = boardLook().backlit;
                const auto c = juce::Point<float>(r.getCentreX(), r.getY() + r.getHeight() * 0.35f);
                juce::ColourGradient grad(s[0], c.x, c.y, s[2], r.getRight(), r.getBottom(), true);
                grad.addColour(0.55, s[1]);
                g.setGradientFill(grad);
                break;
            }
        }
        g.fillRoundedRectangle(r, radius);
        // light from above on the top edge, shadow on the bottom edge
        const bool lit = cap == Cap::hot || cap == Cap::orange || cap == Cap::backlit;
        g.setColour(lit ? rgb(0xffebd2, cap == Cap::backlit ? 0.6f : 0.35f) : juce::Colours::white.withAlpha(0.17f));
        g.fillRoundedRectangle(r.withHeight(1.0f).reduced(radius * 0.6f, 0.0f).withY(r.getY() + 0.5f), 0.5f);
        if (! pressed)
        {
            g.setColour(lit ? rgb(0x5a1e00, 0.45f) : juce::Colours::black.withAlpha(0.38f));
            g.fillRoundedRectangle(r.withTop(r.getBottom() - 2.0f).reduced(radius * 0.5f, 0.0f), 1.0f);
        }
        else
        {
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRoundedRectangle(r.withHeight(3.0f), radius);
        }
    }

    void drawKeyText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, Cap cap, const juce::Font& font)
    {
        const bool lit = cap == Cap::hot || cap == Cap::orange || cap == Cap::backlit;
        g.setFont(font);
        if (! lit)
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            shapedText(g, text, area.translated(0.0f, -1.0f), juce::Justification::centred, false);
        }
        g.setColour(lit ? rgb(cap == Cap::backlit ? 0x2a1408 : 0x1a120c) : rgb(0xf1ebde));
        shapedText(g, text, area, juce::Justification::centred, false);
    }

    // ---- Lenses -----------------------------------------------------------------------------------------------

    void drawLens(juce::Graphics& g, juce::Point<float> centre, float diameter, Lens lens)
    {
        const auto r = juce::Rectangle<float>(diameter, diameter).withCentre(centre);
        const auto hot = juce::Point<float>(r.getX() + diameter * 0.38f, r.getY() + diameter * 0.32f);
        // the glow first, under the lens
        switch (lens)
        {
            case Lens::orange: glow(g, centre, diameter * 1.6f, rgb(0xff963c, 0.55f)); break;
            case Lens::orangeBright:
                // the red step light's glow, in orange, a quarter less bright (D291, the user)
                glow(g, centre, diameter * 4.0f, rgb(0xff6414, 0.25f).withMultipliedBrightness(0.75f));
                glow(g, centre, diameter * 2.4f, rgb(0xff6e1e, 0.6f).withMultipliedBrightness(0.75f));
                glow(g, centre, diameter * 1.3f, rgb(0xff7a2a, 0.95f).withMultipliedBrightness(0.75f));
                break;
            case Lens::red: glow(g, centre, diameter * 1.6f, rgb(0xff3c28, 0.55f)); break;
            case Lens::redBright:
                glow(g, centre, diameter * 4.0f, rgb(0xff2814, 0.25f));
                glow(g, centre, diameter * 2.4f, rgb(0xff321e, 0.6f));
                glow(g, centre, diameter * 1.3f, rgb(0xff4a2a, 0.95f));
                break;
            case Lens::green: glow(g, centre, diameter * 1.6f, rgb(0xc8f06e, 0.5f)); break;
            case Lens::off: break;
        }
        juce::ColourGradient grad(rgb(0x6b4a33), hot.x, hot.y, rgb(0x241710), r.getRight(), r.getBottom(), true);
        switch (lens)
        {
            case Lens::off: break;
            case Lens::orange:
                grad = juce::ColourGradient(rgb(0xfff3dc), hot.x, hot.y, rgb(0xe8742a), r.getRight(), r.getBottom(), true);
                grad.addColour(0.42, rgb(0xffb45c));
                break;
            case Lens::orangeBright:
                // the red step light's stops and saturation, in orange, a quarter less bright (D291, the user)
                grad = juce::ColourGradient(juce::Colours::white.withMultipliedBrightness(0.75f), hot.x, hot.y,
                                            rgb(0xe2560f).withMultipliedBrightness(0.75f), r.getRight(), r.getBottom(), true);
                grad.addColour(0.22, rgb(0xffd6b4).withMultipliedBrightness(0.75f));
                grad.addColour(0.52, rgb(0xff7a28).withMultipliedBrightness(0.75f));
                break;
            case Lens::red:
                grad = juce::ColourGradient(rgb(0xffe0d6), hot.x, hot.y, rgb(0xc8241a), r.getRight(), r.getBottom(), true);
                grad.addColour(0.42, rgb(0xff5a3c));
                break;
            case Lens::redBright:
                grad = juce::ColourGradient(juce::Colours::white, hot.x, hot.y, rgb(0xe8200f), r.getRight(), r.getBottom(), true);
                grad.addColour(0.22, rgb(0xffc2b4));
                grad.addColour(0.52, rgb(0xff4528));
                break;
            case Lens::green:
                grad = juce::ColourGradient(rgb(0xf6ffd8), hot.x, hot.y, rgb(0x8fb83a), r.getRight(), r.getBottom(), true);
                grad.addColour(0.45, rgb(0xd4f07a));
                break;
        }
        g.setGradientFill(grad);
        g.fillEllipse(r);
        g.setColour(juce::Colours::black.withAlpha(lens == Lens::off ? 0.7f : 0.5f));
        g.drawEllipse(r.expanded(0.5f), 1.0f);
    }

    // ---- Knobs ------------------------------------------------------------------------------------------------

    void drawHardwareKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, bool accent, bool silver)
    {
        const float size = std::min(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float r = size * 0.5f;
        // printed ticks, every tenth, the ends and the middle longer
        const auto& look = boardLook();
        g.setColour(silver ? look.tick : rgb(0xcfc7b3)); // printed on the board as the theme has it (D303, D307)
        for (int i = 0; i <= 10; ++i)
        {
            const float a = juce::degreesToRadians(-150.0f + 300.0f * static_cast<float>(i) / 10.0f);
            const float r1 = r + 3.0f, r2 = r + (i % 5 == 0 ? 7.0f : 5.0f);
            g.drawLine(c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), c.x + r2 * std::sin(a), c.y - r2 * std::cos(a), 1.1f);
        }
        const auto capArea = juce::Rectangle<float>(size - 2.0f, size - 2.0f).withCentre(c);
        // its shadow
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(capArea.translated(0.0f, 1.5f));
        ellipseShadow(g, capArea, 4.0f, { 0.0f, 2.0f }, juce::Colours::black.withAlpha(0.6f));
        // the cap, lit from the upper left
        // a cap on the board in the theme's colours (D303, D307: silver on Silver), lit from the upper left
        juce::ColourGradient grad(accent ? look.accentKnob[0] : silver ? look.knobTop : rgb(0x4d4943), capArea.getX() + capArea.getWidth() * 0.4f, capArea.getY() + capArea.getHeight() * 0.3f,
                                  accent ? look.accentKnob[1] : silver ? look.knobBottom : rgb(0x1d1b18), capArea.getRight(), capArea.getBottom(), true);
        g.setGradientFill(grad);
        g.fillEllipse(capArea);
        g.setColour(rgb(0x0e0d0c));
        g.drawEllipse(capArea, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawEllipse(juce::Rectangle<float>(r * 1.24f, r * 1.24f).withCentre(c), 1.0f);
        // the pointer
        const float a = juce::degreesToRadians(-150.0f + 300.0f * juce::jlimit(0.0f, 1.0f, value));
        g.setColour(accent ? rgb(0x1a1714) : silver ? look.pointer : rgb(0xece4d2));
        g.drawLine(juce::Line<float>(c.x + r * 0.2f * std::sin(a), c.y - r * 0.2f * std::cos(a), c.x + (r - 4.0f) * std::sin(a), c.y - (r - 4.0f) * std::cos(a)), 2.4f);
    }

    // ---- Slide switch -----------------------------------------------------------------------------------------

    void drawSlideSwitch(juce::Graphics& g, juce::Rectangle<float> area, bool second)
    {
        const float k = std::min(area.getWidth() / 28.0f, area.getHeight() / 46.0f);
        const auto o = area.getCentre() - juce::Point<float>(14.0f * k, 23.0f * k);
        const auto box = [&](float x, float y, float w, float h) { return juce::Rectangle<float>(o.x + x * k, o.y + y * k, w * k, h * k); };
        // the plate: bevelled, lit from above
        const auto plate = box(0.5f, 0.5f, 27.0f, 45.0f);
        boxShadow(g, plate, 3.5f * k, 4.0f, { 0.0f, 2.0f }, juce::Colours::black.withAlpha(0.6f));
        const auto& look = boardLook();
        g.setGradientFill(vertical(plate, look.plateTop, look.plateMid, 0.5f, look.plateBottom) /* the board's plate (D303, D307) */);
        g.fillRoundedRectangle(plate, 3.5f * k);
        g.setColour(rgb(0x0b0a09));
        g.drawRoundedRectangle(plate, 3.5f * k, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRoundedRectangle(box(1.5f, 1.5f, 25.0f, 43.0f), 2.5f * k, 1.0f);
        // the slot
        const auto slot = box(9.0f, 4.0f, 10.0f, 38.0f);
        g.setGradientFill(vertical(slot, juce::Colours::black, rgb(0x0c0b0a), 0.5f, rgb(0x191714)));
        g.fillRoundedRectangle(slot, 2.0f * k);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(slot, 2.0f * k, 1.0f);
        // the thumb, its shadow and its ridges
        const float ty = second ? 23.0f : 7.0f;
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(box(6.5f, ty + 2.0f, 15.0f, 16.0f), 2.5f * k);
        const auto thumb = box(6.0f, ty, 16.0f, 16.0f);
        g.setGradientFill(vertical(thumb, look.thumbTop, look.thumbMid, 0.55f, look.thumbBottom) /* dark on Silver, light on Charcoal */);
        g.fillRoundedRectangle(thumb, 2.5f * k);
        g.setColour(rgb(0x0b0c0d));
        g.drawRoundedRectangle(thumb, 2.5f * k, 0.8f);
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.fillRoundedRectangle(box(7.0f, ty + 0.8f, 14.0f, 2.0f), 1.0f * k);
        for (int i = 0; i < 4; ++i)
        {
            const float y = ty + 4.0f + static_cast<float>(i) * 2.6f;
            g.setColour(juce::Colours::black.withAlpha(0.38f));
            g.drawLine(o.x + 9.0f * k, o.y + y * k, o.x + 19.0f * k, o.y + y * k, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.45f));
            g.drawLine(o.x + 9.0f * k, o.y + (y + 1.0f) * k, o.x + 19.0f * k, o.y + (y + 1.0f) * k, 0.7f);
        }
    }

    // ---- Glass and the sequencer's keys (D292) --------------------------------------------------------------

    void drawSmokedGlass(juce::Graphics& g, juce::Rectangle<float> area, float radius, bool strip)
    {
        juce::Path shape;
        shape.addRoundedRectangle(area, radius);
        // its shadow on the board
        // (drawn with gradients, not a blurred image: D306)
        if (strip)
            boxShadow(g, area, radius, 6.0f, { 0.0f, 2.0f }, juce::Colours::black.withAlpha(0.5f));
        else
            boxShadow(g, area, radius, 12.0f, { 0.0f, 3.0f }, juce::Colours::black.withAlpha(0.6f));
        // the smoke: the board shows through it a little
        g.setGradientFill(juce::ColourGradient(rgb(0x100f0d, strip ? 0.85f : 0.80f), area.getX(), area.getY(),
                                               rgb(0x0c0b0a, strip ? 0.90f : 0.86f), area.getX(), area.getBottom(), false));
        g.fillPath(shape);
        if (! strip)
        {
            // a sheen from the top, falling a little to the right (the mockup's 170 degrees), gone by 30 %
            const auto c = area.getCentre();
            const juce::Point<float> dir(0.1736f, 0.9848f);
            const float length = std::abs(area.getWidth() * dir.x) + std::abs(area.getHeight() * dir.y);
            const auto from = c - dir * (length * 0.5f);
            const auto to = from + dir * (length * 0.3f);
            g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.05f), from, juce::Colours::white.withAlpha(0.0f), to, false));
            g.fillPath(shape);
        }
        // its edges catch the light: a ring, and the top edge brighter
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(strip ? 0.08f : 0.09f));
        g.fillRect(area.getX() + radius, area.getY(), area.getWidth() - 2.0f * radius, 1.0f);
    }

    void drawSlotKey(juce::Graphics& g, juce::Rectangle<float> area, bool has, float radius)
    {
        juce::Path shape;
        shape.addRoundedRectangle(area, radius);
        boxShadow(g, area, radius, 3.0f, { 0.0f, 2.0f }, juce::Colours::black.withAlpha(0.5f)); // no gradients (D306)
        g.setColour(rgb(0x0b0a09));
        g.fillRoundedRectangle(area.expanded(1.0f), radius + 1.0f);
        const auto& s = has ? boardLook().slotHas : boardLook().slotEmpty; // the theme's (D307)
        g.setGradientFill(vertical(area, s[0], s[1], 0.5f, s[2]));
        g.fillPath(shape);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(area.getX() + radius * 0.6f, area.getY(), area.getWidth() - radius * 1.2f, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(area.getX() + radius * 0.6f, area.getBottom() - 2.0f, area.getWidth() - radius * 1.2f, 2.0f);
    }

    void drawStepKey(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour group, float opacity)
    {
        // A face in the group's colour with its edges: lit above, shaded below, a dark line under it.
        g.setColour(juce::Colours::black.withAlpha(0.6f * opacity));
        g.fillRoundedRectangle(area.translated(0.0f, 1.0f), 3.0f);
        g.setColour(group.withAlpha(0.88f * opacity));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.13f * opacity));
        g.fillRect(area.getX() + 2.0f, area.getY(), area.getWidth() - 4.0f, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.25f * opacity));
        g.fillRect(area.getX() + 2.0f, area.getBottom() - 2.0f, area.getWidth() - 4.0f, 2.0f);
    }

    void drawStepKeyOn(juce::Graphics& g, juce::Rectangle<float> area, bool sounding, float opacity, int group)
    {
        // The beat group's colours as the theme has them (D307): all orange on Silver, the 808's four on Charcoal.
        const auto& on = theme().stepOn[static_cast<std::size_t>(juce::jlimit(0, 3, group))];
        juce::Path shape;
        shape.addRoundedRectangle(area, 3.0f);
        // the dark line under the key, and over it the glow of one that sounds
        g.setColour(juce::Colours::black.withAlpha(0.6f * opacity));
        g.fillRoundedRectangle(area.translated(0.0f, 1.0f), 3.0f);
        if (sounding)
            boxShadow(g, area, 3.0f, 18.0f, {}, on.glow.withAlpha(0.95f * opacity)); // no gradients (D306)
        juce::ColourGradient face;
        if (sounding)
        {
            // lit from inside, brightest a little above the middle
            const juce::Point<float> c(area.getCentreX(), area.getY() + area.getHeight() * 0.35f);
            face = juce::ColourGradient(on.hotCentre, c, on.mid, { area.getRight(), area.getBottom() }, true);
            face.addColour(0.55, on.hotMid);
        }
        else
            face = vertical(area, on.top, on.mid, 0.5f, on.bottom);
        face.multiplyOpacity(opacity);
        g.setGradientFill(face);
        g.fillPath(shape);
        g.setColour(juce::Colours::white.withAlpha((sounding ? 1.0f : 0.35f) * opacity));
        g.fillRect(area.getX() + 2.0f, area.getY(), area.getWidth() - 4.0f, 1.0f);
        g.setColour((sounding ? rgb(0x782800, 0.4f) : rgb(0x5a1e00, 0.45f)).withMultipliedAlpha(opacity));
        g.fillRect(area.getX() + 2.0f, area.getBottom() - 2.0f, area.getWidth() - 4.0f, 2.0f);
    }

    // ---- The glass's pins (D303) -----------------------------------------------------------------

    namespace
    {
        // Turned metal: faint rings round the centre, light and dark in turn, as a lathe leaves on a cap or a head.
        void turnedRings(juce::Graphics& g, juce::Rectangle<float> face, int rings)
        {
            for (int i = 1; i <= rings; ++i)
            {
                const float k = static_cast<float>(i) / static_cast<float>(rings + 1);
                g.setColour((i % 2 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha(0.07f));
                g.drawEllipse(face.reduced(face.getWidth() * 0.5f * k), 0.5f);
            }
        }

        // A chamfered metal edge lit from the upper left: bright there, dark on the lower right.
        void chamfer(juce::Graphics& g, juce::Rectangle<float> outer, juce::Colour light, juce::Colour dark)
        {
            juce::ColourGradient edge(light, outer.getX() + outer.getWidth() * 0.15f, outer.getY() + outer.getHeight() * 0.15f,
                                      dark, outer.getRight() - outer.getWidth() * 0.15f, outer.getBottom() - outer.getHeight() * 0.15f, false);
            g.setGradientFill(edge);
            g.fillEllipse(outer);
        }
    }

    void drawStandoff(juce::Graphics& g, juce::Point<float> c, float diameter)
    {
        // A stainless glass standoff's cap, as sign makers hold a pane with (D303): a short barrel whose shadow falls
        // on the glass, a chamfered edge bright on the upper left, a flat face turned on a lathe, and a soft
        // reflection of the light across it.
        const auto cap = juce::Rectangle<float>(diameter, diameter).withCentre(c);
        ellipseShadow(g, cap, 5.0f, { 1.0f, 2.0f }, juce::Colours::black.withAlpha(0.75f));
        // the barrel's side, seen just below the face
        g.setColour(rgb(0x3c4146));
        g.fillEllipse(cap.translated(0.0f, 0.9f));
        chamfer(g, cap, rgb(0xffffff), rgb(0x5a6168));
        const auto face = cap.reduced(diameter * 0.13f);
        juce::ColourGradient steel(rgb(0xdfe3e7), face.getX(), face.getY(), rgb(0x8d949c), face.getRight(), face.getBottom(), false);
        steel.addColour(0.5, rgb(0xb7bdc3));
        g.setGradientFill(steel);
        g.fillEllipse(face);
        turnedRings(g, face, 3);
        // the light's reflection: a soft bright crescent on the face's upper left
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addEllipse(face);
            g.reduceClipRegion(clip);
            juce::ColourGradient shine(juce::Colours::white.withAlpha(0.75f), face.getX() + face.getWidth() * 0.3f, face.getY() + face.getHeight() * 0.28f,
                                       juce::Colours::white.withAlpha(0.0f), face.getX() + face.getWidth() * 0.62f, face.getY() + face.getHeight() * 0.6f, true);
            g.setGradientFill(shine);
            g.fillEllipse(face);
        }
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawEllipse(cap.expanded(0.25f), 0.5f);
    }
}
