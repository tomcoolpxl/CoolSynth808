#pragma once

#include <array>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "EtchedCircuit.h"

/*
    The fifth layout's board (D292): the panel is a circuit board seen through its metal, the knobs and switches
    mounted on it, the sequencer on smoked glass over it. This paints the metal, the name plate's faded
    outline and the ground ring round the sequencer, which carries no signal and never lights.

    The etched circuit itself comes in the next step. Its copper stops short of everything mounted on the board, as
    on a real board: the panel hands over the keep-outs (each knob's circle, each word, each switch, the strips), and
    the copper layer is clipped round them.
*/
namespace cs808
{
    /** Where no copper may run: a box, or the circle inside it. An outlined one keeps a faded ring, as the name
        plate's. */
    struct KeepOut
    {
        juce::Rectangle<float> area;
        bool round = false;
        bool outlined = false;
    };

    class EtchedBoard final : public juce::Component
    {
    public:
        static constexpr int width = 1280, height = 800;
        // The sequencer's glass, which the ground ring runs round (the mockup's .glass5).
        static constexpr float glassTop = 344.0f;

        EtchedBoard();

        /** What the controls cover; the panel fills it after laying them out. */
        void setKeepOuts(std::vector<KeepOut> list);
        /** Where the transport strip sits, for its shadow (D303). */
        void setStripArea(juce::Rectangle<float> area) { stripArea = area; cache = {}; }
        [[nodiscard]] const std::vector<KeepOut>& keepOuts() const noexcept { return keeps; }

        void paint(juce::Graphics& g) override;
        void resized() override { cache = {}; }
        /** The theme changed (D307): the board is drawn again in its colours. */
        void themeChanged() { cache = {}; repaint(); }

        /** Drums struck (bit l: the drum lane l, 0 the bass drum to 10 the closed hat, as the processor reports them):
            their columns light as their recordings' outputs do (D298). */
        void struck(std::uint32_t lanes, double nowMs);
        /** Each frame: redraws the circuit while anything on it is lit or glowing. */
        void tick(double nowMs);

        /** A trace through its points, its corners cut at 45 degrees as an etched board's are. */
        static juce::Path tracePath(const std::vector<juce::Point<float>>& points);

    private:
        void paintMetal(juce::Graphics& g) const;
        void paintCopper(juce::Graphics& g) const;
        void paintGround(juce::Graphics& g) const;
        /** The glass's shadow on the metal, and the transport strip's (D303). */
        void paintShadows(juce::Graphics& g) const;
        /** The etched circuit unlit (D298), under the keep-outs' clip; its light is drawn over it in paint. */
        void paintCircuit(juce::Graphics& g) const;

        std::vector<KeepOut> keeps;
        juce::Rectangle<float> stripArea;
        // The board changes only when the keep-outs do: drawn once per pixel scale, then copied.
        juce::Image cache;
        float cacheScale = 0.0f;

        // The etched circuit (D298): drawn unlit into the cache, lit over it each frame.
        EtchedCircuit circuit;
        BoardEnvelopes envelopes;
        juce::Path copperClip; // everything but the keep-outs
        std::array<double, 12> hitAt {}; // per column: when its drum was last struck (ms)
        double stepAt = -1.0e9, now = 0.0;
        bool glowing = false;
        [[nodiscard]] float energyOf(int owner) const noexcept;
    };
}
