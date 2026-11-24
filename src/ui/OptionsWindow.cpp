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
        // In pages (D335): the pages' list down the left, a page beside it; every page fits the one size.
        constexpr int panelWidth = 600, panelHeight = 372, sideWidth = 148, pad = 22, labelWidth = 104, rowHeight = 28, rowGap = 8,
                      headHeight = 22;

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

    /** The MIDI inputs as one list (D335): a tick and a name a row; a click switches that input on or off. */
    class InputList final : public juce::Component
    {
    public:
        struct Input
        {
            juce::String name, id;
        };
        std::vector<Input> rows;
        std::function<bool(const juce::String&)> isOn;
        std::function<void(const juce::String&)> toggle;
        static constexpr int rowPitch = 24;

        InputList() { setRepaintsOnMouseActivity(true); }

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            const int hover = isMouseOver() ? getMouseXYRelative().y / rowPitch : -1;
            for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            {
                const auto r = juce::Rectangle<int>(0, i * rowPitch, getWidth(), rowPitch);
                if (i == hover)
                {
                    g.setColour(th.ink.withAlpha(0.06f));
                    g.fillRect(r);
                }
                if (i > 0)
                {
                    g.setColour(juce::Colour(0xff222222));
                    g.fillRect(r.getX(), r.getY(), r.getWidth(), 1);
                }
                const bool on = isOn && isOn(rows[static_cast<std::size_t>(i)].id);
                const auto tick = juce::Rectangle<float>(10.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f);
                if (on)
                {
                    g.setColour(th.lit);
                    g.fillRoundedRectangle(tick, 3.0f);
                    juce::Path mark;
                    mark.startNewSubPath(tick.getX() + 2.8f, tick.getCentreY());
                    mark.lineTo(tick.getX() + 5.2f, tick.getBottom() - 3.0f);
                    mark.lineTo(tick.getRight() - 2.6f, tick.getY() + 3.0f);
                    g.setColour(juce::Colour(0xff161616));
                    g.strokePath(mark, juce::PathStrokeType(1.6f));
                }
                else
                {
                    g.setColour(juce::Colour(0xff5a5a5a));
                    g.drawRoundedRectangle(tick.reduced(0.5f), 3.0f, 1.0f);
                }
                g.setColour(juce::Colour(0xfff1ebde));
                g.setFont(uiFont(12.5f));
                shapedText(g, rows[static_cast<std::size_t>(i)].name, r.withTrimmedLeft(30).withTrimmedRight(8), juce::Justification::centredLeft, true);
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            const int i = e.y / rowPitch;
            if (e.mouseWasClicked() && i >= 0 && i < static_cast<int>(rows.size()) && toggle)
                toggle(rows[static_cast<std::size_t>(i)].id);
            repaint();
        }
        void mouseMove(const juce::MouseEvent&) override { repaint(); }
    };

    /** The options in pages (D335, layout A): their list down the left, one page at a time beside it. The standalone
        app's pages are Audio, MIDI, Drum notes, Panel and Controller; a plugin's Host takes Audio's and MIDI's place. */
    class OptionsPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        enum Page { audioPage, midiPage, hostPage, drumsPage, lookPage, controllerPage };

        OptionsPanel(OptionsHooks h, juce::AudioDeviceManager* dm) : hooks(std::move(h)), devices(dm)
        {
            setWantsKeyboardFocus(false);
            if (devices != nullptr)
                pages = { audioPage, midiPage, drumsPage, lookPage, controllerPage };
            else
                pages = { hostPage, drumsPage, lookPage, controllerPage };
            page = pages.front();

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
                    addChildComponent(*p);

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
                addChildComponent(testTone);

                inputList.isOn = [this](const juce::String& id) { return devices->isMidiInputDeviceEnabled(id); };
                inputList.toggle = [this](const juce::String& id)
                {
                    devices->setMidiInputDeviceEnabled(id, ! devices->isMidiInputDeviceEnabled(id));
                    changed();
                };
                inputView.setViewedComponent(&inputList, false);
                inputView.setScrollBarsShown(true, false);
                inputView.setScrollBarThickness(8);
                addChildComponent(inputView);

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
                addChildComponent(midiOut);
                devices->addChangeListener(this);
            }

            follow.text = "Follow incoming";
            follow.isOn = [this] { return hooks.followClock(); };
            follow.filledWhenOn = false;
            follow.onClick = [this](const juce::MouseEvent&) { hooks.chooseFollowClock(! hooks.followClock()); repaint(); };
            send.text = "Send";
            send.isOn = [this] { return hooks.sendClock(); };
            send.filledWhenOn = false;
            send.onClick = [this](const juce::MouseEvent&) { hooks.chooseSendClock(! hooks.sendClock()); repaint(); };
            if (inPlugin())
            {
                follow.text = "Follow the host's Play";
                follow.isOn = [this] { return hooks.followHostPlay && hooks.followHostPlay(); };
                follow.onClick = [this](const juce::MouseEvent&)
                {
                    if (hooks.chooseFollowHostPlay)
                        hooks.chooseFollowHostPlay(! hooks.followHostPlay());
                    repaint();
                };
            }
            addChildComponent(follow);
            addChildComponent(send);

            for (const auto& t : themes())
                themeChoice.options.push_back(t.name);
            themeChoice.get = [this] { return hooks.theme(); };
            themeChoice.set = [this](int i) { hooks.chooseTheme(i); };
            addChildComponent(themeChoice);
            zoomPicker.value = [this] { return juce::String(hooks.zoom()) + " %"; };
            zoomPicker.items = []
            {
                juce::StringArray names;
                for (const auto z : zooms)
                    names.add(juce::String(z) + " %");
                return names;
            };
            zoomPicker.choose = [this](int i) { hooks.chooseZoom(zooms[static_cast<std::size_t>(i)]); repaint(); };
            addChildComponent(zoomPicker);

            showChoice.options = { "Full", "Faint", "Hidden" };
            showChoice.get = [this] { return hooks.circuitShow(); };
            showChoice.set = [this](int i) { hooks.chooseCircuitShow(i); };
            addChildComponent(showChoice);
            animateChoice.options = { "Animated", "Still" };
            animateChoice.get = [this] { return hooks.circuitAnimated() ? 0 : 1; };
            animateChoice.set = [this](int i) { hooks.chooseCircuitAnimated(i == 0); repaint(); };
            addChildComponent(animateChoice);

            // The drums' notes from outside (D329): a ready set, or each drum learned from the note played next.
            drumSet.options = { "General MIDI", "From 36", "From 60" };
            drumSet.get = [this]
            {
                if (! hooks.drumNotes)
                    return 0;
                const auto notes = hooks.drumNotes();
                for (int set = 0; set < 3; ++set)
                {
                    bool same = true;
                    for (int s = 0; s < 16; ++s)
                        same = same && notes[static_cast<std::size_t>(s)] == setNote(set, s);
                    if (same)
                        return set;
                }
                return -1; // learned
            };
            drumSet.set = [this](int i)
            {
                if (hooks.chooseDrumSet)
                    hooks.chooseDrumSet(i);
                repaint();
            };
            addChildComponent(drumSet);
            static constexpr std::array<const char*, 16> codes { "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH", "LC", "MC", "HC", "CL", "MA" };
            for (int s = 0; s < 16; ++s)
            {
                auto* chip = drumChips.add(new Chip());
                chip->filledWhenOn = false;
                chip->fontHeight = 10.5f;
                chip->isOn = [this, s] { return hooks.learningDrumNote && hooks.learningDrumNote() == s; };
                chip->textOf = [this, s]
                {
                    const juce::String code(codes[static_cast<std::size_t>(s)]);
                    if (hooks.learningDrumNote && hooks.learningDrumNote() == s)
                        return code + "  play";
                    const int n = hooks.drumNotes ? hooks.drumNotes()[static_cast<std::size_t>(s)] : -1;
                    return code + "  " + (n >= 0 ? juce::String(n) : juce::String("none"));
                };
                chip->onClick = [this, s](const juce::MouseEvent&)
                {
                    if (hooks.learnDrumNote)
                        hooks.learnDrumNote(hooks.learningDrumNote && hooks.learningDrumNote() == s ? -1 : s);
                    repaint();
                };
                addChildComponent(chip);
            }

            controller.text = "Controller setup...";
            controller.onClick = [this](const juce::MouseEvent&) { hooks.controllerSetup(); };
            addChildComponent(controller);

            rebuildInputs();
            startTimer(500);
        }

        ~OptionsPanel() override
        {
            stopTimer();
            if (devices != nullptr)
                devices->removeChangeListener(this);
        }

        /** The page shown, by its place in the list; the list says its pages' names. */
        void showPage(int index)
        {
            page = pages[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(pages.size()) - 1, index))];
            if (onPageChanged)
                onPageChanged(index);
            layOut();
            repaint();
        }
        [[nodiscard]] int pageIndex() const
        {
            for (std::size_t i = 0; i < pages.size(); ++i)
                if (pages[i] == page)
                    return static_cast<int>(i);
            return 0;
        }
        [[nodiscard]] int pageCount() const { return static_cast<int>(pages.size()); }
        [[nodiscard]] juce::String pageName(int index) const { return nameOf(pages[static_cast<std::size_t>(index)]); }
        std::function<void(int)> onPageChanged;

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            g.fillAll(th.panel.darker(0.25f));

            // The pages' list: a lens and a name each, the page shown on a key of its own.
            g.setColour(th.panel.darker(0.45f));
            g.fillRect(0, 0, sideWidth, getHeight());
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRect(sideWidth - 1, 0, 1, getHeight());
            for (int i = 0; i < pageCount(); ++i)
            {
                const auto r = pageArea(i).toFloat();
                const bool shown = i == pageIndex();
                if (shown)
                    drawKeyCap(g, r, Cap::plain, false, false);
                else if (i == hoverPage)
                {
                    g.setColour(th.ink.withAlpha(0.06f));
                    g.fillRoundedRectangle(r, 4.0f);
                }
                drawLens(g, { r.getX() + 13.0f, r.getCentreY() }, 6.0f, shown ? Lens::orange : Lens::off);
                g.setColour(shown || i == hoverPage ? juce::Colour(0xfff1ebde) : th.dim);
                g.setFont(labelFont(11.0f));
                shapedText(g, pageName(i).toUpperCase(), r.withTrimmedLeft(26.0f), juce::Justification::centredLeft, false);
            }

            // The page: its name, and each row's label.
            const int x = sideWidth + pad, w = getWidth() - x - pad;
            g.setColour(th.ink);
            g.setFont(headFont(13.0f));
            shapedText(g, nameOf(page).toUpperCase(), juce::Rectangle<int>(x, topPad, w, headHeight), juce::Justification::centredLeft, false);
            g.setColour(th.line);
            g.fillRect(x, topPad + headHeight, w, 1);
            for (const auto& [text, y] : labels)
            {
                g.setColour(th.dim);
                g.setFont(labelFont(11.0f));
                shapedText(g, text.toUpperCase(), juce::Rectangle<int>(x, y, labelWidth, rowHeight), juce::Justification::centredLeft, false);
            }
            g.setFont(uiFont(12.0f));
            g.setColour(th.dim);
            if (page == audioPage)
            {
                // the latency the app adds and how busy the audio is, under the buffer
                g.setColour(th.lit);
                g.setFont(monoFont(12.0f));
                shapedText(g, readout(), juce::Rectangle<int>(x + labelWidth, readoutY, w - labelWidth - 120, rowHeight), juce::Justification::centredLeft, false);
                g.setColour(th.dim);
                g.setFont(uiFont(12.0f));
                shapedText(g, status, juce::Rectangle<int>(x + labelWidth, readoutY + rowHeight + 4, w - labelWidth, 36), juce::Justification::topLeft, true);
            }
            if (page == midiPage && inputList.rows.empty())
                shapedText(g, "No MIDI input is connected.", juce::Rectangle<int>(x + labelWidth, inputsY, w - labelWidth, rowHeight), juce::Justification::centredLeft, false);
            if (page == midiPage && ! inputList.rows.empty())
            {
                g.setColour(juce::Colour(0xff161616));
                g.fillRoundedRectangle(inputView.getBounds().toFloat().expanded(1.0f), 4.0f);
                g.setColour(juce::Colour(0xff0c0c0c));
                g.drawRoundedRectangle(inputView.getBounds().toFloat().expanded(1.0f), 4.0f, 1.0f);
            }
            if (! hint.isEmpty())
                shapedText(g, hint, juce::Rectangle<int>(x + labelWidth, hintY, w - labelWidth, 36), juce::Justification::topLeft, true);
        }

        void resized() override { layOut(); }

        void mouseMove(const juce::MouseEvent& e) override
        {
            const int over = pageAt(e.getPosition());
            if (over != hoverPage)
            {
                hoverPage = over;
                repaint(0, 0, sideWidth, getHeight());
            }
        }
        void mouseExit(const juce::MouseEvent&) override
        {
            hoverPage = -1;
            repaint(0, 0, sideWidth, getHeight());
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (const int i = pageAt(e.getPosition()); i >= 0)
                showPage(i);
        }

    private:
        static constexpr std::array<int, 6> zooms { 75, 100, 125, 150, 175, 200 };
        static constexpr int topPad = 18, sidePad = 8, pagePitch = 33, pageHeight = 30;

        [[nodiscard]] static juce::String nameOf(Page p)
        {
            switch (p)
            {
                case audioPage: return "Audio";
                case midiPage: return "MIDI";
                case hostPage: return "Host";
                case drumsPage: return "Drum notes";
                case lookPage: return "Panel";
                case controllerPage: return "Controller";
            }
            return {};
        }
        [[nodiscard]] juce::Rectangle<int> pageArea(int i) const { return { sidePad, 12 + i * pagePitch, sideWidth - 2 * sidePad, pageHeight }; }
        [[nodiscard]] int pageAt(juce::Point<int> p) const
        {
            for (int i = 0; i < pageCount(); ++i)
                if (pageArea(i).contains(p))
                    return i;
            return -1;
        }

        void layOut()
        {
            labels.clear();
            hint.clear();
            for (auto* c : getChildren())
                c->setVisible(false);
            const int x = sideWidth + pad, w = getWidth() - x - pad, cw = w - labelWidth;
            int y = topPad + headHeight + 12;
            const auto row = [&](juce::Component& c, const juce::String& label, int width = -1)
            {
                labels.push_back({ label, y });
                c.setBounds(x + labelWidth, y, width < 0 ? cw : width, rowHeight);
                c.setVisible(true);
                y += rowHeight + rowGap;
            };
            switch (page)
            {
                case audioPage:
                {
                    row(driver, "Driver");
                    row(output, "Output");
                    row(rate, "Sample rate");
                    const bool asio = devices->getCurrentAudioDevice() != nullptr && devices->getCurrentAudioDevice()->hasControlPanel();
                    row(asio ? static_cast<juce::Component&>(controlPanel) : buffer, "Buffer");
                    readoutY = y;
                    testTone.setBounds(x + w - 110, y, 110, rowHeight);
                    testTone.setVisible(true);
                    break;
                }
                case midiPage:
                {
                    inputsY = y;
                    labels.push_back({ "Inputs", y });
                    const int shownRows = juce::jlimit(1, maxInputRows, static_cast<int>(inputList.rows.size()));
                    inputList.setSize(cw - (static_cast<int>(inputList.rows.size()) > maxInputRows ? 10 : 0),
                                      static_cast<int>(inputList.rows.size()) * InputList::rowPitch);
                    if (! inputList.rows.empty())
                    {
                        inputView.setBounds(x + labelWidth, y, cw, shownRows * InputList::rowPitch);
                        inputView.setVisible(true);
                        y += shownRows * InputList::rowPitch + rowGap + 4;
                    }
                    else
                        y += rowHeight + rowGap;
                    row(midiOut, "Output");
                    labels.push_back({ "Clock", y });
                    const int half = (cw - 4) / 2;
                    follow.setBounds(x + labelWidth, y, half, rowHeight);
                    send.setBounds(x + labelWidth + half + 4, y, cw - half - 4, rowHeight);
                    follow.setVisible(true);
                    send.setVisible(true);
                    break;
                }
                case hostPage:
                    row(follow, "Play");
                    hint = "Off, the pattern waits for its own Play key, at the host's tempo.";
                    hintY = y;
                    break;
                case drumsPage:
                {
                    // A ready set, then the sixteen drums four to a row; a click learns its note (D329).
                    row(drumSet, "Set");
                    labels.push_back({ "Learn", y });
                    const int cell = (cw + 4) / 4;
                    for (int s = 0; s < drumChips.size(); ++s)
                    {
                        drumChips[s]->setBounds(x + labelWidth + (s % 4) * cell, y + (s / 4) * (rowHeight + 6), cell - 4, rowHeight);
                        drumChips[s]->setVisible(true);
                    }
                    y += 4 * (rowHeight + 6) + 4;
                    hint = "Click a drum, then play the note it should answer to.";
                    hintY = y;
                    break;
                }
                case lookPage:
                    row(themeChoice, "Theme");
                    row(zoomPicker, "Window size");
                    row(showChoice, "Circuit");
                    row(animateChoice, "Electricity", (cw * 2) / 3);
                    break;
                case controllerPage:
                    row(controller, "Setup", 190);
                    hint = "Link a MIDI controller's knobs, keys and pads to the panel.";
                    hintY = y;
                    break;
            }
        }

        void rebuildInputs()
        {
            if (devices == nullptr)
                return;
            const auto list = juce::MidiInput::getAvailableDevices();
            knownInputs = list.size();
            inputList.rows.clear();
            for (const auto& d : list)
                inputList.rows.push_back({ d.name, d.identifier });
            layOut();
            repaint();
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
            inputList.repaint();
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
            inputList.repaint();
        }

        void timerCallback() override
        {
            if (page == drumsPage)
            {
                for (auto* chip : drumChips)
                    chip->repaint(); // a note learned on the audio thread (D329)
                drumSet.repaint();
            }
            if (devices == nullptr)
                return;
            if (juce::MidiInput::getAvailableDevices().size() != knownInputs)
                rebuildInputs();
            if (page == audioPage)
                repaint(sideWidth, readoutY, getWidth() - sideWidth, rowHeight);
        }

        OptionsHooks hooks;
        juce::AudioDeviceManager* devices;
        std::vector<Page> pages;
        Page page = audioPage;
        int hoverPage = -1;
        Picker driver, output, rate, buffer, midiOut, zoomPicker;
        Chip controlPanel, testTone, follow, send, controller;
        [[nodiscard]] bool inPlugin() const { return hooks.plugin && hooks.plugin(); }
        static constexpr int maxInputRows = 8;
        InputList inputList;
        juce::Viewport inputView;
        int knownInputs = -1;
        Choices themeChoice, showChoice, animateChoice;
        Choices drumSet;
        juce::OwnedArray<Chip> drumChips;
        /** A ready set's note for a slot, as Kit.h's drumNoteSet. */
        static int setNote(int set, int slot)
        {
            static constexpr std::array<int, 16> gm { 36, 38, 43, 47, 50, 37, 39, 56, 49, 46, 42, 64, 63, 62, 75, 70 };
            return set == 1 ? 36 + slot : set == 2 ? 60 + slot : gm[static_cast<std::size_t>(slot)];
        }
        std::vector<std::pair<juce::String, int>> labels;
        juce::String hint;
        int readoutY = 0, inputsY = 0, hintY = 0;
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
        panel->setSize(panelWidth, panelHeight);
        // It opens on the page last shown.
        if (settings != nullptr)
            panel->showPage(settings->getIntValue("optionsPage", 0));
        panel->onPageChanged = [this](int index)
        {
            if (settings != nullptr)
                settings->setValue("optionsPage", index);
        };
        setContentNonOwned(panel.get(), true);
        setContentComponentSize(panelWidth, panelHeight);
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
        p->setSize(panelWidth, panelHeight);
        return p;
    }

    int showOptionsPageForTesting(juce::Component& panel, int page)
    {
        auto* p = dynamic_cast<OptionsPanel*>(&panel);
        if (p == nullptr)
            return 0;
        if (page >= 0 && page < p->pageCount())
            p->showPage(page);
        return p->pageCount();
    }
}
