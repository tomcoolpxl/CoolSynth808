#include "Hardware.h"

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
    }

    // ---- Keys -------------------------------------------------------------------------------------------------

    void drawKeyCap(juce::Graphics& g, juce::Rectangle<float> area, Cap cap, bool hover, bool pressed, float radius)
    {
        auto r = area.reduced(1.0f);
        if (pressed)
            r = r.translated(0.0f, 1.0f);
        // the drop shadow under a raised key, and the glow round a lit one
        if (cap == Cap::backlit)
            juce::DropShadow(rgb(0xff8228, 0.55f), 12, {}).drawForRectangle(g, r.toNearestInt());
        else if (! pressed)
            juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 4, { 0, 2 }).drawForRectangle(g, r.toNearestInt());
        // the dark surround
        g.setColour(rgb(0x0b0a09));
        g.fillRoundedRectangle(r.expanded(1.0f), radius + 1.0f);
        // the cap
        switch (cap)
        {
            case Cap::plain:
                g.setGradientFill(hover ? vertical(r, rgb(0x55514a), rgb(0x3c3934), 0.46f, rgb(0x2f2d29))
                                        : vertical(r, rgb(0x4c4842), rgb(0x37342f), 0.46f, rgb(0x2c2a26)));
                break;
            case Cap::selected:
                g.setGradientFill(vertical(r, rgb(0x55504a), rgb(0x3d3a35), 0.48f, rgb(0x322f2b)));
                break;
            case Cap::hot:
            case Cap::orange:
                g.setGradientFill(hover ? vertical(r, rgb(0xf8aa72), rgb(0xec7c33), 0.5f, rgb(0xc95e20))
                                        : vertical(r, rgb(0xf5a066), rgb(0xe8742a), 0.5f, rgb(0xc3581b)));
                break;
            case Cap::backlit:
            {
                const auto c = juce::Point<float>(r.getCentreX(), r.getY() + r.getHeight() * 0.35f);
                juce::ColourGradient grad(rgb(0xffc58a), c.x, c.y, rgb(0xc95b1d), r.getRight(), r.getBottom(), true);
                grad.addColour(0.55, rgb(0xf08a3c));
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
            g.drawText(text, area.translated(0.0f, -1.0f), juce::Justification::centred, false);
        }
        g.setColour(lit ? rgb(cap == Cap::backlit ? 0x2a1408 : 0x1a120c) : rgb(0xf1ebde));
        g.drawText(text, area, juce::Justification::centred, false);
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

    void drawHardwareKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, bool accent)
    {
        const float size = std::min(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float r = size * 0.5f;
        // printed ticks, every tenth, the ends and the middle longer
        g.setColour(rgb(0xcfc7b3));
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
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 4, { 0, 2 }).drawForPath(g, [&] { juce::Path p; p.addEllipse(capArea); return p; }());
        // the cap, lit from the upper left
        juce::ColourGradient grad(accent ? rgb(0xf3a066) : rgb(0x4d4943), capArea.getX() + capArea.getWidth() * 0.4f, capArea.getY() + capArea.getHeight() * 0.3f,
                                  accent ? rgb(0xb9541a) : rgb(0x1d1b18), capArea.getRight(), capArea.getBottom(), true);
        g.setGradientFill(grad);
        g.fillEllipse(capArea);
        g.setColour(rgb(0x0e0d0c));
        g.drawEllipse(capArea, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawEllipse(juce::Rectangle<float>(r * 1.24f, r * 1.24f).withCentre(c), 1.0f);
        // the pointer
        const float a = juce::degreesToRadians(-150.0f + 300.0f * juce::jlimit(0.0f, 1.0f, value));
        g.setColour(accent ? rgb(0x1a1714) : rgb(0xece4d2));
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
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 4, { 0, 2 }).drawForRectangle(g, plate.toNearestInt());
        g.setGradientFill(vertical(plate, rgb(0x4a4640), rgb(0x2f2c28), 0.5f, rgb(0x1f1d1a)));
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
        g.setGradientFill(vertical(thumb, rgb(0xf4eee1), rgb(0xc9c1af), 0.55f, rgb(0x8f8775)));
        g.fillRoundedRectangle(thumb, 2.5f * k);
        g.setColour(rgb(0x57514a));
        g.drawRoundedRectangle(thumb, 2.5f * k, 0.8f);
        g.setColour(juce::Colours::white.withAlpha(0.55f));
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
        if (strip)
            juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, { 0, 2 }).drawForPath(g, shape);
        else
            juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 12, { 0, 3 }).drawForPath(g, shape);
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
        juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 3, { 0, 2 }).drawForPath(g, shape);
        g.setColour(rgb(0x0b0a09));
        g.fillRoundedRectangle(area.expanded(1.0f), radius + 1.0f);
        g.setGradientFill(has ? vertical(area, rgb(0x4a4640), rgb(0x3f3b36), 0.5f, rgb(0x34312d))
                              : vertical(area, rgb(0x3b3833), rgb(0x343129), 0.5f, rgb(0x2c2a26)));
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

    void drawStepKeyOn(juce::Graphics& g, juce::Rectangle<float> area, bool sounding, float opacity)
    {
        juce::Path shape;
        shape.addRoundedRectangle(area, 3.0f);
        // the dark line under the key, and over it the glow of one that sounds
        g.setColour(juce::Colours::black.withAlpha(0.6f * opacity));
        g.fillRoundedRectangle(area.translated(0.0f, 1.0f), 3.0f);
        if (sounding)
            juce::DropShadow(rgb(0xffa046, 0.95f * opacity), 18, {}).drawForPath(g, shape);
        juce::ColourGradient face;
        if (sounding)
        {
            // lit from inside, brightest a little above the middle
            const juce::Point<float> c(area.getCentreX(), area.getY() + area.getHeight() * 0.35f);
            face = juce::ColourGradient(rgb(0xfff0dc), c, rgb(0xe8742a), { area.getRight(), area.getBottom() }, true);
            face.addColour(0.55, rgb(0xffb067));
        }
        else
            face = vertical(area, rgb(0xf5a066), rgb(0xe8742a), 0.5f, rgb(0xc3581b));
        face.multiplyOpacity(opacity);
        g.setGradientFill(face);
        g.fillPath(shape);
        g.setColour(juce::Colours::white.withAlpha((sounding ? 1.0f : 0.35f) * opacity));
        g.fillRect(area.getX() + 2.0f, area.getY(), area.getWidth() - 4.0f, 1.0f);
        g.setColour((sounding ? rgb(0x782800, 0.4f) : rgb(0x5a1e00, 0.45f)).withMultipliedAlpha(opacity));
        g.fillRect(area.getX() + 2.0f, area.getBottom() - 2.0f, area.getWidth() - 4.0f, 2.0f);
    }
}
