#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*
    The panel's hardware, drawn as the fifth layout of the mockups has it (CoolSynth's D288 to D291,
    docs/design/mockups/coolsynth-808-layouts.html): keys as raised plastic caps in a dark surround lit from above, a
    lens beside a key's word that lights for a toggle, knobs as solid caps with a pointer and printed ticks, and a
    slide switch with a bevelled plate, a recessed slot and a ridged thumb. Only drawing here; the components that use
    it keep their own behaviour.
*/
namespace cs808
{
    /** How a key's cap looks. */
    enum class Cap
    {
        plain,      // a dark plastic cap
        selected,   // a toggle that is on: a lighter cap (its lens says it is on)
        hot,        // an orange cap, as Play while playing
        orange,     // a step that is on: an orange plastic cap, unlit
        backlit     // a key lit from inside: the pattern or chain block playing, a step sounding
    };

    /** A lens's colour. */
    enum class Lens { off, orange, orangeBright, red, redBright, green };

    /** A key's cap in its rounded rectangle, with its surround and shadow. */
    void drawKeyCap(juce::Graphics& g, juce::Rectangle<float> area, Cap cap, bool hover, bool pressed, float radius = 4.0f);
    /** A key's word, letter-pressed into the cap. */
    void drawKeyText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, Cap cap, const juce::Font& font);
    /** A small round lens centred on a point; lit lenses glow. */
    void drawLens(juce::Graphics& g, juce::Point<float> centre, float diameter, Lens lens);
    /** A knob: printed ticks, a shadow, a solid cap and a pointer. An accent knob is orange. */
    void drawHardwareKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, bool accent);
    /** A vertical slide switch, its thumb up (first) or down (second), in a 28 by 46 box scaled to the area. */
    void drawSlideSwitch(juce::Graphics& g, juce::Rectangle<float> area, bool second);
}
