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
        /** How the circuit shows on the panel (D314, the user, for whom it distracts): its electricity animated or
            still, and the circuit full, faint (all of it at under half, its names too) or hidden. */
        enum class CircuitShow { full = 0, faint, hidden };
        void setCircuitLook(bool animated, CircuitShow show)
        {
            if (animated == animate && show == circuitShow)
                return;
            animate = animated;
            circuitShow = show;
            cache = {};
            repaint();
        }
        [[nodiscard]] bool circuitAnimated() const noexcept { return animate && circuitShow != CircuitShow::hidden; }
        [[nodiscard]] float circuitFade() const noexcept { return circuitShow == CircuitShow::faint ? 0.4f : circuitShow == CircuitShow::hidden ? 0.0f : 1.0f; }
        /** The theme changed (D307): the board is drawn again in its colours. */
        void themeChanged() { cache = {}; repaint(); }

        /** Drums struck (bit l: the drum lane l, 0 the bass drum to 10 the closed hat, as the processor reports them):
            their columns light as their recordings' outputs do (D298). */
        void struck(std::uint32_t lanes, double nowMs);
        /** Each frame: redraws the circuit while anything on it is lit or glowing. */
        void tick(double nowMs);
        /** The sequencer as the CPU section under the glass shows it (D312): the sixteenth playing (-1 stopped) and a
            count of the edits, each a write to the pattern memory. */
        void sequencerState(std::int64_t sixteenth, std::uint64_t edits, double nowMs);
        /** Each drum's level as its circuit plays it, by the panel's lane (D313: Processor::takeLights), and the
            engine's report count: while it moves, the columns light from these, not from the recordings. */
        void liveLevels(const std::array<float, 12>& byLane, unsigned int reports, double nowMs);

        /** A trace through its points, its corners cut at 45 degrees as an etched board's are. */
        static juce::Path tracePath(const std::vector<juce::Point<float>>& points);

    private:
        void paintMetal(juce::Graphics& g) const;
        void paintCopper(juce::Graphics& g) const;
        /** The name plate's faded outline, printed on the metal: it stays when the circuit is hidden. */
        void paintOutline(juce::Graphics& g) const;
        void paintGround(juce::Graphics& g) const;
        /** The glass's shadow on the metal, and the transport strip's (D303). */
        void paintShadows(juce::Graphics& g) const;
        /** The etched circuit unlit (D298), under the keep-outs' clip; its light is drawn over it in paint. */
        void paintCircuit(juce::Graphics& g) const;
        /** The 808's CPU section under the glass (D312, docs/design/COOLSYNTH_808_CPU.md in CoolSynth's repository):
            the uPD650C, its four uPD444C RAMs, the clock's Schmitt trigger and coil, the lamp lines and the trigger
            lines up to the trigger bus. Still into the cache; its light is drawn on the glass (paintCpuGlow). */
        void paintCpu(juce::Graphics& g) const;

    public:
        /** The CPU section's light as it shows on the smoked glass over it (D312): the glass passes little, but light
            under it spreads in it as a soft glow. Drawn by the glass after its smoke; `origin` is the glass's place on
            the board. */
        void paintCpuGlow(juce::Graphics& g, juce::Point<float> origin) const;

    private:

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
        bool animate = true;
        CircuitShow circuitShow = CircuitShow::full;
        std::array<float, 12> live {};
        unsigned int liveReports = 0;
        double liveAt = -1.0e9; // when the engine last reported: its levels are used while it plays
        std::int64_t sixteenthNow = -1;
        std::uint64_t editsSeen = 0;
        double clockAt = -1.0e9, memoryAt = -1.0e9;
        bool cpuLit = false;
        [[nodiscard]] float energyOf(int owner) const noexcept;
    };
}
