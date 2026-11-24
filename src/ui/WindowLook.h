#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*
    The windows' own bar in the theme (D311, the user: JUCE's bar in the theme, no JUCE Options): the bar takes the
    board's metal and printing, and its minimise and close keys are drawn in the board's ink. Used by the standalone
    app's main window and by the options window.
*/
namespace cs808
{
    class WindowLook final : public juce::LookAndFeel_V4
    {
    public:
        void drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int titleSpaceX, int titleSpaceW,
                                        const juce::Image* icon, bool drawTitleTextOnLeft) override;
        juce::Button* createDocumentWindowButton(int buttonType) override;

        /** One look for every window, made on first use and kept until JUCE shuts down. */
        static WindowLook& shared();
    };
}
