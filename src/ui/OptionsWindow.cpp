#include "OptionsWindow.h"

#include "../StandaloneDevices.h"
#include "Hardware.h"
#include "Text.h"
#include "Theme.h"
#include "WindowLook.h"
#include "Widgets.h"

#if JUCE_WINDOWS
// The two calls an owned window needs, declared here rather than pulling all of windows.h in beside JUCE.
extern "C" __declspec(dllimport) std::intptr_t __stdcall SetWindowLongPtrW(void* window, int index, std::intptr_t value);
extern "C" __declspec(dllimport) int __stdcall SetWindowPos(void* window, void* after, int x, int y, int w, int h, unsigned int flags);
#endif

namespace cs808
{
    namespace
    {
        constexpr int panelWidth = 600, pad = 22, labelWidth = 130, rowHeight = 30, rowGap = 8, sectionGap = 22, headHeight = 22;

        /** A value to pick from a list: a key with the value written on it in plain case and a small arrow; a click
            opens the list. */
        class Picker final : public juce::Component
        {
        public:
            std::function<juce::String()> value;
            std::function<juce::StringArray()> items;
            std::function<void(int)> choose;

            Picker() { setRepaintsOnMouseActivity(true); }

            void paint(juce::Graphics& g) override
            {
                const auto area = getLocalBounds().toFloat();
                drawKeyCap(g, area, Cap::plain, isMouseOver(), isMouseButtonDown());
                auto text = area.reduced(12.0f, 0.0f).withTrimmedRight(14.0f);
                if (isMouseButtonDown())
                    text = text.translated(0.0f, 1.0f);
                g.setFont(uiFont(13.0f));
                g.setColour(juce::Colour(0xfff1ebde));
                shapedText(g, value ? value() : juce::String(), text, juce::Justification::centredLeft, true);
                juce::Path arrow;
                const float x = area.getRight() - 16.0f, y = area.getCentreY();
                arrow.addTriangle(x - 4.0f, y - 2.0f, x + 4.0f, y - 2.0f, x, y + 3.0f);
                g.fillPath(arrow);
            }

            void mouseUp(const juce::MouseEvent& e) override
            {
                if (! e.mouseWasClicked() || ! items)
                    return;
                juce::PopupMenu menu;
                const auto list = items();
                const auto now = value ? value() : juce::String();
                for (int i = 0; i < list.size(); ++i)
                    menu.addItem(i + 1, list[i], true, list[i] == now);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                                   [safe = juce::Component::SafePointer<Picker>(this)](int r)
                {
                    if (safe != nullptr && r > 0 && safe->choose)
                        safe->choose(r - 1);
                });
            }
        };

        juce::String rateText(double rate) { return juce::String(juce::roundToInt(rate)) + " Hz"; }
        juce::String bufferText(int size, double rate)
        {
            return juce::String(size) + " samples" + (rate > 0.0 ? " (" + juce::String(1000.0 * size / rate, 1) + " ms)" : juce::String());
        }
    }

    // =============================================================================================================

    class OptionsPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        OptionsPanel(OptionsHooks h, juce::AudioDeviceManager* dm) : hooks(std::move(h)), devices(dm)
        {
            setWantsKeyboardFocus(false);
            if (devices != nullptr)
            {
                driver.value = [this] { return devices->getCurrentAudioDeviceType(); };
                driver.items = [this]
                {
                    // Windows Audio, its Low Latency mode and ASIO (the user, D311): not the Exclusive mode, which
                    // takes the device from every other app, nor the old DirectSound.
                    juce::StringArray names;
                    for (auto* type : devices->getAvailableDeviceTypes())
                        if (const auto name = type->getTypeName(); ! name.containsIgnoreCase("Exclusive") && ! name.containsIgnoreCase("DirectSound"))
                            names.add(name);
                    return names;
                };
                driver.choose = [this](int i)
                {
                    devices->setCurrentAudioDeviceType(driver.items()[i], true);
                    changed();
                };
                output.value = [this]
                {
                    auto* device = devices->getCurrentAudioDevice();
                    return device != nullptr ? device->getName() : juce::String("None");
                };
                output.items = [this]
                {
                    juce::StringArray names;
                    if (auto* type = devices->getCurrentDeviceTypeObject())
                    {
                        type->scanForDevices();
                        names = type->getDeviceNames(false);
                    }
                    return names;
                };
                output.choose = [this](int i) { applySetup([name = output.items()[i]](auto& s) { s.outputDeviceName = name; s.inputDeviceName = {}; }); };
                rate.value = [this]
                {
                    auto* device = devices->getCurrentAudioDevice();
                    return device != nullptr ? rateText(device->getCurrentSampleRate()) : juce::String("-");
                };
                rate.items = [this]
                {
                    juce::StringArray names;
                    if (auto* device = devices->getCurrentAudioDevice())
                        for (const auto r : device->getAvailableSampleRates())
                            names.add(rateText(r));
                    return names;
                };
                rate.choose = [this](int i)
                {
                    if (auto* device = devices->getCurrentAudioDevice())
                        applySetup([r = device->getAvailableSampleRates()[i]](auto& s) { s.sampleRate = r; });
                };
                buffer.value = [this]
                {
                    auto* device = devices->getCurrentAudioDevice();
                    return device != nullptr ? bufferText(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate()) : juce::String("-");
                };
                buffer.items = [this]
                {
                    juce::StringArray names;
                    if (auto* device = devices->getCurrentAudioDevice())
                        for (const auto b : device->getAvailableBufferSizes())
                            names.add(bufferText(b, device->getCurrentSampleRate()));
                    return names;
                };
                buffer.choose = [this](int i)
                {
                    if (auto* device = devices->getCurrentAudioDevice())
                        applySetup([b = device->getAvailableBufferSizes()[i]](auto& s) { s.bufferSize = b; });
                };
                for (auto* p : { &driver, &output, &rate, &buffer })
                    addAndMakeVisible(*p);

                controlPanel.text = "Driver's control panel";
                controlPanel.onClick = [this](const juce::MouseEvent&)
                {
                    // ASIO sets its buffer in its own panel; the device is opened again after it (as JUCE's own does).
                    if (auto* device = devices->getCurrentAudioDevice(); device != nullptr && device->showControlPanel())
                    {
                        devices->closeAudioDevice();
                        devices->restartLastAudioDevice();
                        changed();
                    }
                };
                addChildComponent(controlPanel);
                testTone.text = "Test tone";
                testTone.onClick = [this](const juce::MouseEvent&) { devices->playTestSound(); };
                addAndMakeVisible(testTone);

                midiOut.value = [this]
                {
                    const auto id = devices->getDefaultMidiOutputIdentifier();
                    for (const auto& d : juce::MidiOutput::getAvailableDevices())
                        if (d.identifier == id)
                            return d.name;
                    return juce::String("None");
                };
                midiOut.items = []
                {
                    juce::StringArray names { "None" };
                    for (const auto& d : juce::MidiOutput::getAvailableDevices())
                        names.add(d.name);
                    return names;
                };
                midiOut.choose = [this](int i)
                {
                    const auto list = juce::MidiOutput::getAvailableDevices();
                    devices->setDefaultMidiOutputDevice(i == 0 || i > list.size() ? juce::String() : list[i - 1].identifier);
                    changed();
                };
                addAndMakeVisible(midiOut);
                devices->addChangeListener(this);
            }

            follow.text = "Follow an incoming clock";
            follow.isOn = [this] { return hooks.followClock(); };
            follow.filledWhenOn = false;
            follow.onClick = [this](const juce::MouseEvent&) { hooks.chooseFollowClock(! hooks.followClock()); repaint(); };
            send.text = "Send clock";
            send.isOn = [this] { return hooks.sendClock(); };
            send.filledWhenOn = false;
            send.onClick = [this](const juce::MouseEvent&) { hooks.chooseSendClock(! hooks.sendClock()); repaint(); };
            addAndMakeVisible(follow);
            addAndMakeVisible(send);

            for (const auto& t : themes())
                themeChoice.options.push_back(t.name);
            themeChoice.get = [this] { return hooks.theme(); };
            themeChoice.set = [this](int i) { hooks.chooseTheme(i); };
            addAndMakeVisible(themeChoice);
            for (const auto z : zooms)
                zoomChoice.options.push_back(juce::String(z) + " %");
            zoomChoice.get = [this]
            {
                const int now = hooks.zoom();
                for (std::size_t i = 0; i < zooms.size(); ++i)
                    if (zooms[i] == now)
                        return static_cast<int>(i);
                return 1;
            };
            zoomChoice.set = [this](int i) { hooks.chooseZoom(zooms[static_cast<std::size_t>(i)]); };
            addAndMakeVisible(zoomChoice);

            controller.text = "Controller setup...";
            controller.onClick = [this](const juce::MouseEvent&) { hooks.controllerSetup(); };
            addAndMakeVisible(controller);

            rebuildInputs();
            startTimer(500);
        }

        ~OptionsPanel() override
        {
            stopTimer();
            if (devices != nullptr)
                devices->removeChangeListener(this);
        }

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            g.fillAll(th.panel.darker(0.25f));
            const auto head = [&](const juce::String& text, int y)
            {
                g.setColour(th.ink);
                g.setFont(headFont(13.0f));
                shapedText(g, text, juce::Rectangle<int>(pad, y, panelWidth - 2 * pad, headHeight), juce::Justification::centredLeft, false);
                g.setColour(th.line);
                g.fillRect(pad, y + headHeight, panelWidth - 2 * pad, 1);
            };
            const auto label = [&](const juce::String& text, int y)
            {
                g.setColour(th.dim);
                g.setFont(labelFont(11.0f));
                shapedText(g, text.toUpperCase(), juce::Rectangle<int>(pad, y, labelWidth, rowHeight), juce::Justification::centredLeft, false);
            };
            for (const auto& [text, y] : heads)
                head(text, y);
            for (const auto& [text, y] : labels)
                label(text, y);
            if (devices != nullptr)
            {
                // the latency the app adds and how busy the audio is, under the buffer
                g.setColour(th.lit);
                g.setFont(monoFont(12.0f));
                shapedText(g, readout(), juce::Rectangle<int>(pad + labelWidth, readoutY, panelWidth - 2 * pad - labelWidth, rowHeight - 8), juce::Justification::centredLeft, false);
                g.setColour(th.dim);
                g.setFont(uiFont(12.0f));
                shapedText(g, status, juce::Rectangle<int>(pad + labelWidth, readoutY + rowHeight - 8, panelWidth - 2 * pad - labelWidth, 18), juce::Justification::centredLeft, true);
            }
            if (devices != nullptr && inputs.isEmpty())
            {
                g.setColour(th.dim);
                g.setFont(uiFont(12.0f));
                shapedText(g, "No MIDI input is connected.", juce::Rectangle<int>(pad + labelWidth, inputsY, panelWidth - 2 * pad - labelWidth, rowHeight), juce::Justification::centredLeft, false);
            }
        }

        void resized() override { layOut(); }

        [[nodiscard]] int preferredHeight() const { return contentHeight; }

    private:
        static constexpr std::array<int, 6> zooms { 75, 100, 125, 150, 175, 200 };

        void layOut()
        {
            heads.clear();
            labels.clear();
            int y = pad;
            const auto row = [&](juce::Component& c, const juce::String& label, int width = panelWidth - 2 * pad - labelWidth)
            {
                labels.push_back({ label, y });
                c.setBounds(pad + labelWidth, y, width, rowHeight);
                y += rowHeight + rowGap;
            };
            if (devices != nullptr)
            {
                heads.push_back({ "AUDIO", y });
                y += headHeight + 12;
                row(driver, "Driver");
                row(output, "Output");
                row(rate, "Sample rate");
                const bool asio = devices->getCurrentAudioDevice() != nullptr && devices->getCurrentAudioDevice()->hasControlPanel();
                buffer.setVisible(! asio);
                controlPanel.setVisible(asio);
                row(asio ? static_cast<juce::Component&>(controlPanel) : buffer, "Buffer");
                readoutY = y;
                testTone.setBounds(panelWidth - pad - 110, y, 110, rowHeight);
                y += rowHeight + 14 + sectionGap;
            }
            heads.push_back({ "MIDI", y });
            y += headHeight + 12;
            if (devices != nullptr)
            {
                inputsY = y;
                if (inputs.isEmpty())
                {
                    labels.push_back({ "Inputs", y });
                    y += rowHeight + rowGap;
                }
                for (int i = 0; i < inputs.size(); ++i)
                    row(*inputs[i], i == 0 ? "Inputs" : juce::String());
                row(midiOut, "Output");
            }
            labels.push_back({ "Clock", y });
            follow.setBounds(pad + labelWidth, y, 220, rowHeight);
            send.setBounds(pad + labelWidth + 230, y, 140, rowHeight);
            y += rowHeight + rowGap + sectionGap;
            heads.push_back({ "PANEL", y });
            y += headHeight + 12;
            row(themeChoice, "Theme");
            row(zoomChoice, "Window size");
            y += sectionGap - rowGap;
            heads.push_back({ "CONTROLLER", y });
            y += headHeight + 12;
            row(controller, "Setup", 190);
            contentHeight = y + pad - rowGap;
        }

        void rebuildInputs()
        {
            if (devices == nullptr)
                return;
            const auto list = juce::MidiInput::getAvailableDevices();
            knownInputs = list.size();
            inputs.clear();
            for (const auto& d : list)
            {
                auto* chip = inputs.add(std::make_unique<Chip>());
                chip->text = d.name;
                chip->filledWhenOn = false;
                chip->isOn = [this, id = d.identifier] { return devices->isMidiInputDeviceEnabled(id); };
                chip->onClick = [this, id = d.identifier](const juce::MouseEvent&)
                {
                    devices->setMidiInputDeviceEnabled(id, ! devices->isMidiInputDeviceEnabled(id));
                    changed();
                };
                addAndMakeVisible(*chip);
            }
            const int before = contentHeight;
            layOut();
            if (contentHeight != before && onHeightChanged)
                onHeightChanged();
        }

        template <typename Edit>
        void applySetup(Edit edit)
        {
            auto setup = devices->getAudioDeviceSetup();
            edit(setup);
            const auto error = devices->setAudioDeviceSetup(setup, true);
            status = error.isNotEmpty() ? "Could not open it: " + error : juce::String();
            changed();
        }

        void changed()
        {
            saveStandaloneDeviceState();
            layOut();
            repaint();
        }

        juce::String readout() const
        {
            auto* device = devices->getCurrentAudioDevice();
            if (device == nullptr)
                return "No audio device open";
            const double r = device->getCurrentSampleRate();
            const double ms = r > 0.0 ? 1000.0 * (device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples()) / r : 0.0;
            return "Latency " + juce::String(ms, 1) + " ms   CPU " + juce::String(juce::roundToInt(devices->getCpuUsage() * 100.0)) + " %";
        }

        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            layOut();
            repaint();
        }

        void timerCallback() override
        {
            if (devices == nullptr)
                return;
            if (juce::MidiInput::getAvailableDevices().size() != knownInputs)
                rebuildInputs();
            repaint(pad + labelWidth, readoutY, panelWidth, rowHeight);
        }

    public:
        std::function<void()> onHeightChanged;

    private:
        OptionsHooks hooks;
        juce::AudioDeviceManager* devices;
        Picker driver, output, rate, buffer, midiOut;
        Chip controlPanel, testTone, follow, send, controller;
        juce::OwnedArray<Chip> inputs;
        int knownInputs = -1;
        Choices themeChoice, zoomChoice;
        std::vector<std::pair<juce::String, int>> heads, labels;
        int contentHeight = 400, readoutY = 0, inputsY = 0;
        juce::String status;
    };

    // =============================================================================================================

    OptionsWindow::OptionsWindow(OptionsHooks hooks, juce::AudioDeviceManager* devices, juce::PropertiesFile* settingsFile)
        : juce::DocumentWindow("CoolSynth 808 Options", juce::Colours::black, juce::DocumentWindow::closeButton, false),
          settings(settingsFile)
    {
        setLookAndFeel(&WindowLook::shared());
        setTitleBarHeight(28);
        panel = std::make_unique<OptionsPanel>(std::move(hooks), devices);
        panel->setSize(panelWidth, 400);
        panel->onHeightChanged = [this] { setContentComponentSize(panelWidth, panel->preferredHeight()); };
        setContentNonOwned(panel.get(), true);
        setContentComponentSize(panelWidth, panel->preferredHeight());
        setResizable(false, false);
        // Where it was last put; else beside the middle of the screen.
        const int x = settings != nullptr ? settings->getIntValue("optionsX", -100000) : -100000;
        const int y = settings != nullptr ? settings->getIntValue("optionsY", -100000) : -100000;
        if (x != -100000 && y != -100000)
            setBoundsConstrained({ x, y, getWidth(), getHeight() });
        else
            centreWithSize(getWidth(), getHeight());
        setWantsKeyboardFocus(true);
        addToDesktop(getDesktopWindowStyleFlags()); // with this window's flags: no taskbar button
    }

    OptionsWindow::~OptionsWindow()
    {
        clearContentComponent();
        setLookAndFeel(nullptr);
    }

    void OptionsWindow::closeButtonPressed()
    {
        if (onClose)
            onClose();
    }

    bool OptionsWindow::keyPressed(const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeButtonPressed();
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

    void OptionsWindow::moved()
    {
        juce::DocumentWindow::moved();
        if (settings != nullptr && isOnDesktop())
        {
            settings->setValue("optionsX", getX());
            settings->setValue("optionsY", getY());
        }
    }

    void OptionsWindow::keepAbove(juce::Component& owner)
    {
       #if JUCE_WINDOWS
        // JUCE cannot give a window an owner (forum.juce.com/t/fr-floating-child-windows/69136): Windows' own call
        // does. An owned window stays above its owner and goes with it, but not above other apps.
        auto* top = owner.getTopLevelComponent();
        auto* mine = getPeer();
        auto* theirs = top != nullptr ? top->getPeer() : nullptr;
        if (mine != nullptr && theirs != nullptr)
        {
            constexpr int ownerIndex = -8; // GWLP_HWNDPARENT: the owner of a top-level window
            SetWindowLongPtrW(mine->getNativeHandle(), ownerIndex, reinterpret_cast<std::intptr_t>(theirs->getNativeHandle()));
            constexpr unsigned int noSize = 0x1, noMove = 0x2, noActivate = 0x10;
            SetWindowPos(mine->getNativeHandle(), reinterpret_cast<void*>(static_cast<std::intptr_t>(-2)) /* HWND_NOTOPMOST */, 0, 0, 0, 0, noSize | noMove | noActivate);
        }
       #else
        juce::ignoreUnused(owner);
       #endif
    }

    int OptionsWindow::getDesktopWindowStyleFlags() const
    {
        // no button of its own on the taskbar: it belongs to the 808's window
        return juce::DocumentWindow::getDesktopWindowStyleFlags() & ~juce::ComponentPeer::windowAppearsOnTaskbar;
    }

    void OptionsWindow::themeChanged()
    {
        repaint();
        panel->repaint();
    }

    juce::Component& OptionsWindow::panelForTesting() { return *panel; }

    std::unique_ptr<juce::Component> makeOptionsPanelForTesting(OptionsHooks hooks, juce::AudioDeviceManager* devices)
    {
        auto p = std::make_unique<OptionsPanel>(std::move(hooks), devices);
        p->setSize(panelWidth, 400);
        p->setSize(panelWidth, p->preferredHeight());
        return p;
    }
}
