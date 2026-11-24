#include "WindowLook.h"

#include "Text.h"
#include "Theme.h"

namespace cs808
{
    namespace
    {
        /** Minimise or close, drawn in the board's ink; close turns red under the mouse, as Windows' own does. */
        class TitleKey final : public juce::Button
        {
        public:
            explicit TitleKey(int kind) : juce::Button(kind == juce::DocumentWindow::closeButton ? "close" : "minimise"), type(kind) {}

            void paintButton(juce::Graphics& g, bool over, bool down) override
            {
                const auto& look = boardLook();
                const auto r = getLocalBounds().toFloat().reduced(1.0f, 2.0f);
                const bool close = type == juce::DocumentWindow::closeButton;
                if (over || down)
                {
                    g.setColour(close ? juce::Colour(0xffc8372d).withMultipliedBrightness(down ? 0.85f : 1.0f)
                                      : look.ink.withAlpha(down ? 0.22f : 0.14f));
                    g.fillRoundedRectangle(r, 3.0f);
                }
                g.setColour(close && (over || down) ? juce::Colours::white : look.ink);
                const auto c = r.getCentre();
                const float s = std::min(r.getWidth(), r.getHeight()) * 0.24f;
                if (close)
                {
                    g.drawLine(c.x - s, c.y - s, c.x + s, c.y + s, 1.4f);
                    g.drawLine(c.x - s, c.y + s, c.x + s, c.y - s, 1.4f);
                }
                else
                    g.drawLine(c.x - s, c.y, c.x + s, c.y, 1.4f);
            }

        private:
            int type;
        };

        struct SharedLook final : public juce::DeletedAtShutdown
        {
            WindowLook look;
            ~SharedLook() override { clearSingletonInstance(); }
            JUCE_DECLARE_SINGLETON_SINGLETHREADED_INLINE(SharedLook, false)
        };
    }

    void WindowLook::drawDocumentWindowTitleBar(juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int titleSpaceX, int titleSpaceW,
                                                const juce::Image*, bool)
    {
        // The board's metal, a shade darker along its foot, and the title printed in the board's ink, centred.
        const auto& look = boardLook();
        const auto bar = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
        g.setGradientFill(juce::ColourGradient(look.metalTop.brighter(0.04f), 0.0f, 0.0f, look.metalBottom, 0.0f, static_cast<float>(h), false));
        g.fillRect(bar);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(bar.withTop(bar.getBottom() - 1.0f));
        g.setColour(window.isActiveWindow() ? look.ink : look.dim);
        g.setFont(headFont(12.0f));
        juce::ignoreUnused(titleSpaceX, titleSpaceW);
        shapedText(g, window.getName().toUpperCase(), juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                   juce::Justification::centred, true);
    }

    juce::Button* WindowLook::createDocumentWindowButton(int buttonType)
    {
        return new TitleKey(buttonType);
    }

    std::unique_ptr<juce::Button> WindowLook::makeCloseKey()
    {
        return std::make_unique<TitleKey>(juce::DocumentWindow::closeButton);
    }

    WindowLook& WindowLook::shared()
    {
        return SharedLook::getInstance()->look;
    }
}
