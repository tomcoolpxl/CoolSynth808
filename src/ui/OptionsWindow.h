#pragma once

#include <functional>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

/*
    CoolSynth 808's own options (D311), in a window of their own in the theme, opened by the panel's Options key:
    the audio device (standalone only: driver, output, sample rate, buffer, latency, a test tone), the MIDI inputs and
    output (standalone only) and the clock, the theme and the window's size, and the controller setup. It stays above
    the 808's window, remembers its place, and closes on its close key, Escape or the Options key.
*/
namespace cs808
{
    class OptionsPanel;

    /** What the options act on, handed in by the editor. */
    struct OptionsHooks
    {
        std::function<int()> theme;
        std::function<void(int)> chooseTheme;
        std::function<int()> zoom; // in percent
        std::function<void(int)> chooseZoom;
        std::function<bool()> followClock, sendClock;
        std::function<void(bool)> chooseFollowClock, chooseSendClock;
        std::function<void()> controllerSetup;
    };

    class OptionsWindow final : public juce::DocumentWindow
    {
    public:
        /** `devices` is the standalone app's device manager, or null in a plugin (those parts left out). */
        OptionsWindow(OptionsHooks hooks, juce::AudioDeviceManager* devices, juce::PropertiesFile* settings);
        ~OptionsWindow() override;

        std::function<void()> onClose;

        void closeButtonPressed() override;
        bool keyPressed(const juce::KeyPress& key) override;
        void moved() override;
        /** Kept above `owner`'s window, not above other apps (D311): on Windows the owner of its native window. */
        void keepAbove(juce::Component& owner);
        int getDesktopWindowStyleFlags() const override;
        /** The theme changed: the bar and the panel drawn again. */
        void themeChanged();
        /** The panel inside, for the snapshot tests. */
        [[nodiscard]] juce::Component& panelForTesting();

    private:
        std::unique_ptr<OptionsPanel> panel;
        juce::PropertiesFile* settings;
    };

    /** The options as the window shows them, alone, for the snapshot tests. */
    std::unique_ptr<juce::Component> makeOptionsPanelForTesting(OptionsHooks hooks, juce::AudioDeviceManager* devices);
}
