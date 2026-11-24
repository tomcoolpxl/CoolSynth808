#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

/*
    The circuit etched into the panel's top (D298): twelve columns of copper, one per drum and the master, each
    drawn by a generator in the spirit of the real circuits (a trigger in, a transistor, a ring, an amplifier, the
    level out), a trigger bus along their foot and the mix along their head, as the fifth layout of the mockups
    (CoolSynth's docs/design/mockups/coolsynth-808-layouts.html, build5 and genCell). It is the panel's texture and its
    light, not a schematic to read: the exact circuits are in Under the Lid.

    It lights from the drums as they are struck: each drum's column glows as the output of its ngspice recording does
    after a strike (resources/lid/board_envelopes.json, made by tools/board_envelopes.py), the level in decibels so a
    quiet tail still shows. Where a control is mounted the copper is cut away.
*/
namespace cs808
{
    class EtchedCircuit final
    {
    public:
        /** Owners of the copper: 0 the master column (the trigger and accent), 1 to 11 the drums, and the buses. */
        static constexpr int triggerBus = 100, mixBus = 101;

        /** The columns: their left edges and widths, the trigger side (y, the foot) and the mix side (y, the head). */
        struct Column { float x = 0.0f, w = 0.0f; };
        void build(const std::vector<Column>& columns, float top, float bottom);

        /** Where controls are mounted (boxes in panel units): no part's name is printed there. The copper itself is cut
            away by the caller's clip. */
        void setTaken(std::vector<juce::Rectangle<float>> boxes) { taken = std::move(boxes); }

        /** The board unlit: copper, parts, pads and names. Drawn once and kept. */
        void paintStill(juce::Graphics& g) const;

        /** How lit an owner is now, 0..1: the drums from their strikes, the buses from the steps. */
        using Energy = std::function<float(int owner)>;
        /** Over the still board, each frame: the afterglow, the lit copper, the current running along it, the lit
            parts and the transistors' lights. Returns whether anything is lit or glowing, so the caller knows to keep
            drawing. */
        bool paintLit(juce::Graphics& g, double nowMs, const Energy& energy);

    private:
        struct Wire { std::vector<juce::Point<float>> pts; int owner = 0; bool inCell = true; float length = 0.0f; };
        struct Part { char type = 'R'; juce::Point<float> at; bool vertical = false; float k = 8.0f; int owner = 0; juce::String name; };
        std::vector<Wire> wires;
        std::vector<juce::Path> paths; // each wire's copper, made once
        std::vector<Part> parts;
        std::vector<juce::Rectangle<float>> taken;
        std::array<float, 128> heat {};
        double lastNow = 0.0;

        void addWire(std::vector<juce::Point<float>> pts, int owner, bool inCell);
        juce::Point<float> pointAlong(const Wire& w, float d) const;
        void paintPart(juce::Graphics& g, const Part& p, float lit) const;
        void paintNames(juce::Graphics& g) const;
    };

    /** The drums' envelopes after a strike, from their recordings: brightness 0..1 at a time since the strike. */
    class BoardEnvelopes final
    {
    public:
        BoardEnvelopes();
        /** Lane 1 to 11; ms since its strike. The trigger's millisecond is a full flash; after the recording, nothing. */
        [[nodiscard]] float brightness(int lane, double ms) const noexcept;

    private:
        std::array<std::vector<float>, 12> env;
    };
}
