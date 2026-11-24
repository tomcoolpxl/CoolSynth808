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
    void drawHardwareKnob(juce::Graphics& g, juce::Rectangle<float> area, float value, bool accent, bool silver = false);
    /** A vertical slide switch, its thumb up (first) or down (second), in a 28 by 46 box scaled to the area. */
    void drawSlideSwitch(juce::Graphics& g, juce::Rectangle<float> area, bool second);

    /** Smoked glass mounted on the board (D292): the sequencer's pane, or the thinner strip the transport sits on.
        Translucent, so the board shows faintly through it. */
    void drawSmokedGlass(juce::Graphics& g, juce::Rectangle<float> area, float radius, bool strip);
    /** A pattern slot or a chain block: a darker key than a button, lighter when it holds something. */
    void drawSlotKey(juce::Graphics& g, juce::Rectangle<float> area, bool has, float radius = 3.0f);
    /** A step key that is off, in its beat group's colour; `opacity` dims it past its lane's length. */
    void drawStepKey(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour group, float opacity);
    /** A step key that is on, in its beat group's colour as the theme has it (0 to 3; D307), lit bright from inside
        while it sounds.
        No dark surround, so it keeps the size of the keys beside it (the mockup's .ksteps .key.on and .ph.on). */
    void drawStepKeyOn(juce::Graphics& g, juce::Rectangle<float> area, bool sounding, float opacity, int group);
    /** A polished cap that holds the glass to the board, with its shadow on the glass (D303). */
    void drawStandoff(juce::Graphics& g, juce::Point<float> centre, float diameter);
}
