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
        const auto& th = theme();
        auto area = getLocalBounds().toFloat();
        const float labelHeight = (small || label.isEmpty()) ? 0.0f : 13.0f;
        auto knobArea = area.removeFromTop(area.getHeight() - labelHeight);
        const float size = std::min(knobArea.getWidth(), knobArea.getHeight());
        const auto valueColour = locked ? th.lit : (isMouseOverOrDragging() ? th.ink : th.ink.withAlpha(0.92f));
        drawArcKnob(g, knobArea.withSizeKeepingCentre(size, size), get ? get() : 0.0f, th.line, valueColour, ! small);
        if (labelHeight > 0.0f)
        {
            g.setColour(dragging || isMouseOver() ? th.ink : th.dim);
            g.setFont(uiFont(9.5f));
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
        const auto& th = theme();
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const bool on = isOn && isOn();
        if (on)
        {
            g.setColour(filledWhenOn ? th.on : th.ink);
            g.fillRoundedRectangle(area, 5.0f);
        }
        g.setColour(on ? (filledWhenOn ? th.on : th.ink) : (isMouseOver() ? th.dim : th.line));
        if (dashed && ! on)
        {
            juce::Path p;
            p.addRoundedRectangle(area, 5.0f);
            juce::Path dashedPath;
            const float lengths[] { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashedPath, p, lengths, 2);
            g.fillPath(dashedPath);
        }
        else
            g.drawRoundedRectangle(area, 5.0f, 1.0f);
        g.setColour(on ? (filledWhenOn ? th.onInk : th.panel) : (isMouseOver() ? th.ink : th.dim));
        g.setFont(uiFont(fontHeight, true));
        g.drawFittedText(text, getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
    }

    // ---- Choices ----------------------------------------------------------------------------------------------

    void Choices::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(th.line);
        g.drawRoundedRectangle(area, 5.0f, 1.0f);
        const int count = static_cast<int>(options.size());
        if (count == 0)
            return;
        const float w = area.getWidth() / count;
        const int chosen = get ? get() : -1;
        for (int i = 0; i < count; ++i)
        {
            const auto cell = juce::Rectangle<float>(area.getX() + w * i, area.getY(), w, area.getHeight());
            if (i == chosen)
            {
                g.setColour(th.ink);
                g.fillRoundedRectangle(cell.reduced(1.0f), 4.0f);
            }
            g.setColour(i == chosen ? th.panel : th.dim);
            g.setFont(uiFont(fontHeight, true));
            g.drawFittedText(options[static_cast<std::size_t>(i)], cell.toNearestInt(), juce::Justification::centred, 1);
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
