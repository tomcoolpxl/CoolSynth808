#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Kit.h"
#include "Widgets.h"

/*
    The sequencer on its pane of smoked glass (D292, the mockup's .glass5): the patterns row with the banks, the fill
    and the variation; the chain with where it plays; a row of step lights over the twelve lanes; and at the right the
    selected lane's and the pattern's tools. A step's own settings (chance, roll, accent) are on its right-click.
*/
namespace cs808
{
    class Editor;

    /** A lane's two letters and its name as the panel prints them, the switched instrument's when switched. */
    [[nodiscard]] juce::String laneCode(int panelLane, const KitValues& kit);
    [[nodiscard]] juce::String laneName(int panelLane, const KitValues& kit);
    /** A pattern's number as the panel prints it: 01 to 16, or "fill". */
    [[nodiscard]] juce::String patternLabel(int pattern);

    class SequencerGlass final : public juce::Component
    {
    public:
        explicit SequencerGlass(Editor& e);
        ~SequencerGlass() override;

        // Where it sits on the board, 1280 by 800.
        static juce::Rectangle<int> boardArea() { return { 14, 344, 1252, 442 }; }

        void paint(juce::Graphics& g) override;
        void resized() override;
        /** The pop-up of a step's settings, at a point on the board. */
        void showStepPop(int panelLane, int step, juce::Point<float> where);
        void closeStepPop();
        /** The parts that change as the beat plays, drawn again without the board under them. */
        void repaintPlaying();

    private:
        class PatternRow;
        class ChainRow;
        class LaneGrid;
        class LanePanel;
        class StepPop;
        class Scrim;
        Editor& editor;
        std::unique_ptr<PatternRow> patterns;
        std::unique_ptr<ChainRow> chain;
        std::unique_ptr<LaneGrid> lanes;
        std::unique_ptr<LanePanel> panel;
        std::unique_ptr<Scrim> scrim;
        std::unique_ptr<StepPop> pop;
    };
}
