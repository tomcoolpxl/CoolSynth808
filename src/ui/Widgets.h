#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Hardware.h"
#include "Theme.h"

/*
    The panel's small parts, drawn in the theme's roles: a knob drawn as an arc (dragged up and down, double-click
    for its default), a number dragged up and down, a chip (a button that may stay on) and a row of choices.
*/
namespace cs808
{
    class Knob final : public juce::Component
    {
    public:
        std::function<float()> get;
        std::function<void(float)> set;
        std::function<void()> gestureStart, gestureEnd;
        float defaultValue = 0.5f;
        juce::String label;
        bool small = false;
        bool locked = false; // drawn in the lit colour
        bool accent = false; // an orange cap, as the Accent knob's (D292)

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

    private:
        float startValue = 0.0f;
        bool dragging = false;
    };

    class NumberBox final : public juce::Component
    {
    public:
        std::function<double()> get;
        std::function<void(double)> set;
        std::function<void()> gestureStart, gestureEnd;
        std::function<juce::String(double)> text;
        double minimum = 0.0, maximum = 100.0, step = 1.0, pixelsPerStep = 4.0, defaultValue = 0.0;
        juce::String label;
        float valueHeight = 22.0f;
        bool labelBelow = true;

        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

    private:
        double startValue = 0.0;
    };

    class Chip final : public juce::Component
    {
    public:
        juce::String text;
        std::function<bool()> isOn;
        std::function<void(const juce::MouseEvent&)> onClick;
        bool filledWhenOn = true;  // on: the "on" colour; otherwise the ink
        bool dashed = false;
        float fontHeight = 11.0f;

        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& e) override
        {
            if (onClick && e.mouseWasClicked() && contains(e.getPosition()))
                onClick(e);
        }
    };

    /** One of several choices, side by side. */
    class Choices final : public juce::Component
    {
    public:
        std::vector<juce::String> options;
        std::function<int()> get;
        std::function<void(int)> set;
        float fontHeight = 11.0f;

        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& e) override;
    };

    /** Draws a value arc from 7 to 5 o'clock, as the mockup's knobs. */
    void drawArcKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, juce::Colour track, juce::Colour valueColour, bool pointer);
}
