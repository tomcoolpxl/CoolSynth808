#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"

/*
    The transport and the files on one strip of smoked glass at the top right of the board (D292, the mockup's
    .tstrip): Play, Rec, the tempo, Tap and the Swing knob; then Library and File.
*/
namespace cs808
{
    class Editor;

    class TransportStrip final : public juce::Component
    {
    public:
        explicit TransportStrip(Editor& e);
        ~TransportStrip() override;

        static constexpr int height = 36;
        /** As wide as its keys and words need. */
        [[nodiscard]] int preferredWidth() const;

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        class TempoReadout;
        Editor& editor;
        juce::OwnedArray<juce::Component> items; // in order, left to right
        std::vector<int> widths;
        int separatorAfter = -1;                 // the item the separator follows
        int separatorX = 0;
        int swingLabelAfter = -1;                // the Swing knob, its word right of it
        static constexpr int padX = 8, gap = 7, swingWordWidth = 34;
    };
}
