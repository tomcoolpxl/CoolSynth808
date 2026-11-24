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
        bool silver = false; // mounted on the board (D303): its cap and printing in the theme's board colours (D307)
        // Laid out as the fifth layout's knobs (D292): a cap of this diameter at the top, 8 px below the top for its
        // ticks, its name 5 px under it and its value under that. 0 keeps the knob fitted to the box.
        float diameter = 0.0f;
        juce::Colour labelColour { 0xfff4efe4 };
        std::function<juce::String()> valueText; // drawn in orange under the name, as the Length knob's steps

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
        Chip() { setRepaintsOnMouseActivity(true); }
        juce::String text;
        std::function<juce::String()> textOf; // a word that changes, as Play's and Stop's
        std::function<bool()> isOn;
        std::function<void(const juce::MouseEvent&)> onClick;
        bool filledWhenOn = true;  // on: the "on" colour; otherwise the ink
        bool dashed = false;
        float fontHeight = 11.0f;
        Lens onLens = Lens::orange; // a toggle's light: red for mute, green for solo (D292)
        bool flat = false;          // an item in a pop-up: no cap, its word on the left, a shade under the mouse

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

    /** A two-way slide switch with its two words above and below, the one it is set to lit (D292): the 808's voice
        selectors, tom or conga and the others. A click throws it. */
    class SlideSwitch final : public juce::Component
    {
    public:
        SlideSwitch() { setRepaintsOnMouseActivity(true); }
        juce::String first, second;
        std::function<bool()> get; // true: the second
        std::function<void(bool)> set;
        static constexpr int wordHeight = 10, gap = 3, switchWidth = 28, switchHeight = 46;
        static constexpr int height = 2 * wordHeight + 2 * gap + switchHeight;

        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& e) override;
    };

    /** Draws a value arc from 7 to 5 o'clock, as the mockup's knobs. */
    void drawArcKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, juce::Colour track, juce::Colour valueColour, bool pointer);
}
