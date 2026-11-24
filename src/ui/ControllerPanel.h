#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

/*
    The controller setup (D268), a first and plain one until the real UI: every place a controller can stand for,
    what it does in CoolSynth 808, and the control linked to it; click a row to learn its control, right-click to
    forget it; a controller CoolSynth knows can take its ready setup. Shown over the panel, as the library is.
*/
namespace cs808
{
    class ControllerPanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
    {
    public:
        /** `owner` is what passes the controller's messages on while learning (the editor's Controls). */
        explicit ControllerPanel(const void* owner);
        ~ControllerPanel() override;

        std::function<void()> onClose;
        std::unique_ptr<juce::Button> closeKey; // the cross at the top right

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& k) override;

    private:
        [[nodiscard]] juce::Rectangle<float> card() const;
        [[nodiscard]] juce::Rectangle<float> readyButton() const;
        [[nodiscard]] juce::Rectangle<float> forgetButton() const;
        [[nodiscard]] juce::Rectangle<float> inputButton() const; // which MIDI input is the controller (D271)
        void learn(int row);
        void stopLearning();
        void timerCallback() override;

        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

        const void* owner;
        juce::ListBox list;
        int learningRow = -1;
        int token = 0;
        juce::String status;
        int seenRevision = -1;
    };
}
