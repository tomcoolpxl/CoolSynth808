#include "Widgets.h"

namespace cs808
{
    namespace
    {
        constexpr float startAngle = -0.75f * juce::MathConstants<float>::pi;
        constexpr float endAngle = 0.75f * juce::MathConstants<float>::pi;
    }

    void drawArcKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, juce::Colour track, juce::Colour valueColour, bool pointer)
    {
        const auto size = std::min(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float stroke = size > 30.0f ? 3.0f : 2.5f;
        const float r = size * 0.5f - stroke;
        juce::Path t;
        t.addCentredArc(c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour(track);
        g.strokePath(t, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float a = startAngle + (endAngle - startAngle) * juce::jlimit(0.0f, 1.0f, value);
        if (value > 0.002f)
        {
            juce::Path v;
            v.addCentredArc(c.x, c.y, r, r, 0.0f, startAngle, a, true);
            g.setColour(valueColour);
            g.strokePath(v, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (pointer)
        {
            const juce::Point<float> tip(c.x + r * std::sin(a), c.y - r * std::cos(a));
            g.setColour(valueColour);
            g.drawLine(juce::Line<float>(c + (tip - c) * 0.35f, tip), 2.0f);
        }
    }

    // ---- Knob -------------------------------------------------------------------------------------------------

    void Knob::paint(juce::Graphics& g)
    {
        // A hardware knob (D292): a solid cap, its pointer and printed ticks; its name in near-white below.
        if (diameter > 0.0f)
        {
            const float w = static_cast<float>(getWidth());
            constexpr float top = 8.0f; // room for the ticks
            drawHardwareKnob(g, juce::Rectangle<float>(diameter, diameter).withCentre({ w * 0.5f, top + diameter * 0.5f }), get ? get() : 0.0f, accent || locked, silver);
            float y = top + diameter + 5.0f;
            if (label.isNotEmpty())
            {
                g.setColour(dragging || isMouseOver() ? (silver ? juce::Colours::black : juce::Colours::white) : labelColour);
                g.setFont(labelFont(10.0f));
                const auto shown = dragging && get && ! valueText ? juce::String(get() * 10.0f, 1) : label.toUpperCase();
                g.drawText(shown, juce::Rectangle<float>(-20.0f, y, w + 40.0f, 10.0f), juce::Justification::centred, false);
                y += 10.0f + 3.0f;
            }
            if (valueText)
            {
                g.setColour(theme().lit);
                g.setFont(monoFont(11.0f));
                g.drawText(valueText(), juce::Rectangle<float>(0.0f, y, w, 13.0f), juce::Justification::centred, false);
            }
            return;
        }
        auto area = getLocalBounds().toFloat();
        const float labelHeight = (small || label.isEmpty()) ? 0.0f : 14.0f;
        auto knobArea = area.removeFromTop(area.getHeight() - labelHeight);
        const float size = std::min(knobArea.getWidth(), knobArea.getHeight()) - (small ? 8.0f : 16.0f);
        drawHardwareKnob(g, knobArea.withSizeKeepingCentre(size, size), get ? get() : 0.0f, accent || locked);
        if (labelHeight > 0.0f)
        {
            g.setColour(dragging || isMouseOver() ? juce::Colours::white : juce::Colour(0xfff4efe4));
            g.setFont(labelFont(10.0f));
            const auto shown = dragging && get ? juce::String(get() * 10.0f, 1) : label.toUpperCase();
            g.drawFittedText(shown, area.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    void Knob::mouseDown(const juce::MouseEvent&)
    {
        startValue = get ? get() : 0.0f;
        dragging = true;
        if (gestureStart)
            gestureStart();
    }

    void Knob::mouseDrag(const juce::MouseEvent& e)
    {
        if (! set)
            return;
        const float pixels = e.mods.isShiftDown() ? 800.0f : 160.0f; // shift: fine
        set(juce::jlimit(0.0f, 1.0f, startValue - static_cast<float>(e.getDistanceFromDragStartY()) / pixels));
        repaint();
    }

    void Knob::mouseUp(const juce::MouseEvent&)
    {
        dragging = false;
        if (gestureEnd)
            gestureEnd();
        repaint();
    }

    void Knob::mouseDoubleClick(const juce::MouseEvent&)
    {
        if (gestureStart)
            gestureStart();
        if (set)
            set(defaultValue);
        if (gestureEnd)
            gestureEnd();
        repaint();
    }

    // ---- NumberBox --------------------------------------------------------------------------------------------

    void NumberBox::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        auto area = getLocalBounds().toFloat();
        const double v = get ? get() : 0.0;
        const auto shown = text ? text(v) : juce::String(v, step < 1.0 ? 1 : 0);
        if (labelBelow)
        {
            g.setColour(th.ink);
            g.setFont(uiFont(valueHeight));
            g.drawText(shown, area.removeFromTop(valueHeight + 4.0f), juce::Justification::centredLeft);
            g.setColour(th.dim);
            g.setFont(uiFont(9.5f));
            g.drawText(label.toUpperCase(), area, juce::Justification::topLeft);
        }
        else
        {
            g.setColour(th.dim);
            g.setFont(uiFont(9.5f));
            const float labelWidth = label.isEmpty() ? 0.0f : static_cast<float>(juce::GlyphArrangement::getStringWidthInt(uiFont(9.5f), label.toUpperCase())) + 6.0f;
            g.drawText(label.toUpperCase(), area.removeFromLeft(labelWidth), juce::Justification::centredLeft);
            g.setColour(th.ink);
            g.setFont(uiFont(valueHeight, true));
            g.drawText(shown, area, juce::Justification::centredLeft);
        }
    }

    void NumberBox::mouseDown(const juce::MouseEvent&)
    {
        startValue = get ? get() : 0.0;
        if (gestureStart)
            gestureStart();
    }

    void NumberBox::mouseDrag(const juce::MouseEvent& e)
    {
        if (! set)
            return;
        const double steps = std::round(-e.getDistanceFromDragStartY() / pixelsPerStep);
        set(juce::jlimit(minimum, maximum, startValue + steps * step));
        repaint();
    }

    void NumberBox::mouseUp(const juce::MouseEvent&)
    {
        if (gestureEnd)
            gestureEnd();
    }

    void NumberBox::mouseDoubleClick(const juce::MouseEvent&)
    {
        if (gestureStart)
            gestureStart();
        if (set)
            set(defaultValue);
        if (gestureEnd)
            gestureEnd();
        repaint();
    }

    // ---- Chip -------------------------------------------------------------------------------------------------

    void Chip::paint(juce::Graphics& g)
    {
        // A key (D292): a raised plastic cap. One that only acts has no light; a toggle carries a lens beside its
        // word that lights when on; one that is "hot" when on (Play) turns orange.
        const bool on = isOn && isOn();
        const bool toggle = isOn != nullptr && ! filledWhenOn;
        const auto word = (textOf ? textOf() : text).toUpperCase();
        const auto area = getLocalBounds().toFloat();
        const auto font = labelFont(fontHeight);
        if (flat)
        {
            if (isMouseOver())
            {
                g.setColour(juce::Colour(0xff3a3733));
                g.fillRoundedRectangle(area, 4.0f);
            }
            g.setColour(juce::Colour(0xfff1ebde));
            g.setFont(font);
            g.drawText(word, area.reduced(10.0f, 0.0f), juce::Justification::centredLeft, false);
            return;
        }
        const Cap cap = on && filledWhenOn ? Cap::hot : (on ? Cap::selected : Cap::plain);
        drawKeyCap(g, area, cap, isMouseOver(), isMouseButtonDown());
        auto textArea = area.reduced(4.0f, 0.0f);
        if (isMouseButtonDown())
            textArea = textArea.translated(0.0f, 1.0f);
        if (toggle)
        {
            // the lens and the word centred together, 6 px apart, as the mockup's keys
            const float w = juce::GlyphArrangement::getStringWidth(font, word);
            const float x = area.getCentreX() - (w + 12.0f) * 0.5f;
            drawLens(g, { x + 3.0f, textArea.getCentreY() }, 6.0f, on ? onLens : Lens::off);
            textArea = juce::Rectangle<float>(x + 12.0f, textArea.getY(), w + 2.0f, textArea.getHeight());
        }
        drawKeyText(g, word, textArea, cap, font);
    }

    // ---- Choices ----------------------------------------------------------------------------------------------

    void Choices::paint(juce::Graphics& g)
    {
        // A row of keys (D292), each with its lens; the chosen one lit.
        auto area = getLocalBounds().toFloat();
        const int count = static_cast<int>(options.size());
        if (count == 0)
            return;
        const float gap = 4.0f, w = (area.getWidth() - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        const int chosen = get ? get() : -1;
        const auto font = labelFont(fontHeight);
        for (int i = 0; i < count; ++i)
        {
            const auto cell = juce::Rectangle<float>(area.getX() + (w + gap) * static_cast<float>(i), area.getY(), w, area.getHeight());
            const bool on = i == chosen;
            drawKeyCap(g, cell, on ? Cap::selected : Cap::plain, false, false);
            const auto& word = options[static_cast<std::size_t>(i)];
            const float tw = juce::GlyphArrangement::getStringWidth(font, word.toUpperCase());
            const float x = cell.getCentreX() - (tw + 12.0f) * 0.5f;
            drawLens(g, { x + 3.0f, cell.getCentreY() }, 6.0f, on ? Lens::orange : Lens::off);
            drawKeyText(g, word.toUpperCase(), juce::Rectangle<float>(x + 12.0f, cell.getY(), tw + 2.0f, cell.getHeight()), on ? Cap::selected : Cap::plain, font);
        }
    }

    // ---- SlideSwitch ------------------------------------------------------------------------------------------

    void SlideSwitch::paint(juce::Graphics& g)
    {
        const bool down = get && get();
        const float w = static_cast<float>(getWidth());
        const auto font = labelFont(10.0f);
        g.setFont(font);
        const auto word = [&](const juce::String& text, float y, bool lit)
        {
            g.setColour(lit ? boardInk::label : boardInk::dim); // printed on the silver board (D303)
            g.drawText(text.toUpperCase(), juce::Rectangle<float>(-20.0f, y, w + 40.0f, static_cast<float>(wordHeight)), juce::Justification::centred, false);
        };
        word(first, 0.0f, ! down);
        drawSlideSwitch(g, juce::Rectangle<float>(static_cast<float>(switchWidth), static_cast<float>(switchHeight))
                               .withCentre({ w * 0.5f, wordHeight + gap + switchHeight * 0.5f }), down);
        word(second, static_cast<float>(wordHeight + 2 * gap + switchHeight), down);
    }

    void SlideSwitch::mouseUp(const juce::MouseEvent& e)
    {
        if (set && e.mouseWasClicked())
        {
            set(! (get && get()));
            repaint();
        }
    }

    void Choices::mouseUp(const juce::MouseEvent& e)
    {
        const int count = static_cast<int>(options.size());
        if (count == 0 || ! set || ! e.mouseWasClicked())
            return;
        set(juce::jlimit(0, count - 1, e.x * count / std::max(1, getWidth())));
        repaint();
    }
}
