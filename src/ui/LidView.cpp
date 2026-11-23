#include "LidView.h"

#include <array>

#include "../Kit.h"
#include "../Processor.h"
#include "Theme.h"

namespace cs808
{
    // A first view: each circuit as a box under its drum, lit while it rings. The schematic with its current follows.
    struct LidView::Impl
    {
        std::array<double, laneCount> litUntil {};
        double now = 0.0;
    };

    LidView::LidView(Processor& p) : impl(std::make_unique<Impl>()), processor(p) {}
    LidView::~LidView() = default;

    void LidView::struck(std::uint32_t lanes)
    {
        for (int l = 0; l < laneCount; ++l)
            if ((lanes >> l) & 1u)
                impl->litUntil[static_cast<std::size_t>(l)] = impl->now + 250.0;
    }

    void LidView::tick(double nowMs)
    {
        impl->now = nowMs;
        if (isVisible())
            repaint();
    }

    void LidView::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        const float w = static_cast<float>(getWidth()) / laneCount;
        for (int l = 0; l < laneCount; ++l)
        {
            const auto r = juce::Rectangle<float>(l * w + 4.0f, 40.0f, w - 8.0f, getHeight() - 80.0f);
            const bool lit = impl->now < impl->litUntil[static_cast<std::size_t>(l)];
            g.setColour(lit ? th.flow.withAlpha(0.25f) : th.panel);
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(lit ? th.flow : th.line);
            g.drawRoundedRectangle(r, 5.0f, 1.0f);
            g.setColour(th.ink);
            g.setFont(uiFont(13.0f, true));
            g.drawText(instruments[static_cast<std::size_t>(l)].code, r.withHeight(24.0f), juce::Justification::centred);
        }
        g.setColour(th.dim);
        g.setFont(uiFont(11.0f));
        g.drawText("Under the Lid: the circuits light as they are struck.", getLocalBounds().removeFromBottom(30), juce::Justification::centred);
    }

    void LidView::mouseUp(const juce::MouseEvent&) {}
}
