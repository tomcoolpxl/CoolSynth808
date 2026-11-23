#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cs808
{
    class Processor;

    /** Under the Lid: the whole board as one reduced schematic, the current on its wires (D263, D264). */
    class LidView final : public juce::Component
    {
    public:
        explicit LidView(Processor& p);
        ~LidView() override;
        void paint(juce::Graphics& g) override;
        void mouseUp(const juce::MouseEvent& e) override;
        /** Lights the drums struck since the last frame (bits as Processor::takeStruckLanes gives them). */
        void struck(std::uint32_t lanes);
        void tick(double nowMs);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        Processor& processor;
    };
}
