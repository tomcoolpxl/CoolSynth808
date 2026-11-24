#include "Editor.h"

#include "Controls.h"
#include "ui/ControllerPanel.h"
#include "ui/LibraryShelf.h"

#include "MidiExport.h"
#include "ui/LidView.h"
#include "ui/Sequencer.h"
#include "ui/Transport.h"
#include "ui/Text.h"
#include "ui/OptionsWindow.h"
#include "StandaloneDevices.h"

namespace cs808
{
    namespace
    {
        // The board's columns (D292, the mockup's build5): twelve across x 34 to 1246, the Master's 0.85 as wide; a
        // knob 34 px across every 58 px from y 84.
        constexpr float columnsLeft = 34.0f, columnsRight = 1246.0f;
        constexpr int knobTop = 84, knobPitch = 58, knobSize = 34;

        float columnUnit() { return (columnsRight - columnsLeft) / (0.85f + laneCount); }
        float columnX(int c) { return c == 0 ? columnsLeft : columnsLeft + columnUnit() * (0.85f + static_cast<float>(c - 1)); }
        float columnWidth(int c) { return columnUnit() * (c == 0 ? 0.85f : 1.0f); }
        float columnCentre(int c) { return columnX(c) + columnWidth(c) * 0.5f; }

        // The words printed on the board take the theme's board colours (D307).

        /** A word's box on the board, 5 px of clearance each side, for the copper to keep off. */
        KeepOut wordKeepOut(const juce::String& text, const juce::Font& font, float centreX, float top, float height)
        {
            const float w = shapedWidth(font, text) + 10.0f;
            return { { centreX - w * 0.5f, top, w, height } };
        }
    }

    // =============================================================================================================
    // A column's name on the board: the drum's two letters and its name (MASTER over the first); a click selects it

    class ColumnLabel final : public juce::Component
    {
    public:
        Editor* editor = nullptr;
        int column = 0;

        void paint(juce::Graphics& g) override
        {
            const auto& kit = editor->processor.project().kit;
            const bool sel = editor->selectedLane == column;
            const auto w = static_cast<float>(getWidth());
            const auto id = column == 0 ? juce::String("MASTER") : laneCode(column, kit);
            // the selected drum's letters lit, glowing a little
            const auto idArea = juce::Rectangle<float>(0.0f, column == 0 ? 9.0f : 2.0f, w, 13.0f);
            g.setFont(headFont(13.0f));
            if (sel)
            {
                g.setColour(juce::Colour(0xffffa04a).withAlpha(0.18f));
                for (const auto d : { juce::Point<float>(-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
                    shapedText(g, id, idArea.translated(d.x, d.y), juce::Justification::centred, false);
            }
            g.setColour(sel ? boardLook().accent : boardLook().ink);
            shapedText(g, id, idArea, juce::Justification::centred, false);
            if (column > 0)
            {
                g.setColour(boardLook().label);
                g.setFont(labelFont(10.0f));
                shapedText(g, laneName(column, kit).toUpperCase(), juce::Rectangle<float>(0.0f, 18.0f, w, 10.0f), juce::Justification::centred, false);
            }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked())
                return;
            editor->selectedLane = column;
            editor->refresh();
        }
    };

    // =============================================================================================================
    // The name plate: printed on the board in its faded outline

    class NamePlate final : public juce::Component
    {
    public:
        NamePlate() { setInterceptsMouseClicks(false, false); }

        void paint(juce::Graphics& g) override
        {
            const auto light = logoFont(17.0f, false), bold = logoFont(17.0f, true);
            const juce::String first = "CoolSynth ";
            const float w1 = shapedWidth(light, first), w2 = shapedWidth(bold, "808");
            const juce::Rectangle<float> line(14.0f, 5.0f, 300.0f, 20.0f);
            g.setColour(boardLook().ink);
            g.setFont(light);
            shapedText(g, first, line, juce::Justification::centredLeft, false);
            g.setFont(bold);
            shapedText(g, "808", line.withTrimmedLeft(w1), juce::Justification::centredLeft, false);
            g.setColour(boardLook().label);
            g.setFont(labelFont(10.0f));
            shapedText(g, "BEAT COMPOSER", line.withTrimmedLeft(w1 + w2 + 4.0f + 10.0f).translated(0.0f, 1.0f), juce::Justification::centredLeft, false);
        }
    };

    // =============================================================================================================
    // Under the Lid over the whole window: the Lid key on the transport strip opens it, its own key or Escape closes
    // it (D308, the user: the green and red currents back; D292 had left the key out).

    class Editor::LidOverlay final : public juce::Component
    {
    public:
        LidOverlay(LidView& lidView, std::function<void()> close) : lid(lidView)
        {
            setOpaque(true);
            addAndMakeVisible(lid);
            back.text = "Back to the panel";
            back.onClick = [close = std::move(close)](const juce::MouseEvent&) { close(); };
            addAndMakeVisible(back);
        }

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            g.fillAll(th.panel);
            g.setColour(th.ink);
            g.setFont(headFont(13.0f));
            shapedText(g, "UNDER THE LID", juce::Rectangle<int>(16, 0, 300, 44), juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            lid.setBounds(getLocalBounds().withTrimmedTop(44).reduced(16, 0).withTrimmedBottom(12));
            const int w = juce::roundToInt(shapedWidth(labelFont(11.0f), "BACK TO THE PANEL") + 28.0f);
            back.setBounds(getWidth() - 16 - w, 8, w, 28);
        }

    private:
        LidView& lid;
        Chip back;
    };

    // =============================================================================================================
    // The editor

    Editor::Editor(Processor& p) : juce::AudioProcessorEditor(p), processor(p)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "CoolSynth808";
        options.filenameSuffix = ".settings";
        options.folderName = "CoolSynth808";
        options.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile>(options);
        // The theme kept by its name (D307): the older setting counted the themes of the old layouts.
        const auto keptTheme = settings->getValue("panelTheme", themes()[0].id);
        for (int i = 0; i < static_cast<int>(themes().size()); ++i)
            if (keptTheme == themes()[static_cast<std::size_t>(i)].id)
                themeIndex = i;
        setTheme(themeIndex);
        // The clock switches as last chosen in the options (D311): on, as before, until switched off.
        p.setFollowClock(settings->getBoolValue("followClock", true));
        p.setSendClock(settings->getBoolValue("sendClock", true));

        board = std::make_unique<EtchedBoard>();
        addAndMakeVisible(*board);
        board->setBounds(0, 0, baseWidth, baseHeight);
        auto& plate = add<NamePlate>(*board);
        plate.setBounds(26, 6, 344, 34);
        buildColumns();
        buildTransport();
        sequencer = std::make_unique<SequencerGlass>(*this);
        board->addAndMakeVisible(*sequencer);
        sequencer->setBounds(SequencerGlass::boardArea());
        updateKeepOuts();
        circuitAnimated = settings->getBoolValue("circuitAnimated", true);
        circuitShow = juce::jlimit(0, 2, settings->getIntValue("circuitShow", 1)); // faint unless chosen otherwise (the user)
        board->setCircuitLook(circuitAnimated, static_cast<EtchedBoard::CircuitShow>(circuitShow));

        lid = std::make_unique<LidView>(p);
        lidOverlay = std::make_unique<LidOverlay>(*lid, [this] { showLid(false); });
        addChildComponent(*lidOverlay);

        setWantsKeyboardFocus(true);
        setResizable(true, true);
        setResizeLimits(620, 300, 3840, 2400);
        zoomPercent = juce::jlimit(75, 200, settings->getIntValue("zoom", 100));
        setSize(baseWidth * zoomPercent / 100, baseHeight * zoomPercent / 100);
        controls = std::make_unique<Controls>(*this);
        // Space and the shortcuts work from the start, before any click.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Editor>(this)] { if (safe != nullptr) safe->grabKeyboardFocus(); });
        startTimerHz(30);
    }

    Editor::~Editor()
    {
        stopAudition(); // the project as it was, before the state is kept
        controls.reset(); // its gesture closes while the panel is whole
        settings->setValue("panelTheme", themes()[static_cast<std::size_t>(themeIndex)].id);
        optionsWindow.reset(); // its settings are saved below
        if (watchedWindow != nullptr)
            watchedWindow->removeComponentListener(this);
        settings->saveIfNeeded();
        lidOverlay.reset(); // before the lid it shows
    }

    // ---- What the panel edits ---------------------------------------------------------------------------------

    const SequencerPattern& Editor::patternShown() const
    {
        const auto& bank = processor.project().currentBank();
        return editPattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(editPattern)];
    }

    const SequencerLane& Editor::laneShown(int panelLane) const
    {
        const auto& p = patternShown();
        return panelLane <= 0 ? p.accent : p.lanes[static_cast<std::size_t>(panelLane - 1)];
    }

    void Editor::editLane(int panelLane, const std::function<void(SequencerLane&)>& edit, bool undoable, int patternIndex)
    {
        const int pattern = patternIndex == currentPattern ? editPattern : patternIndex;
        processor.editBank([&](SequencerBank& bank)
        {
            auto& p = pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)];
            edit(panelLane <= 0 ? p.accent : p.lanes[static_cast<std::size_t>(panelLane - 1)]);
        }, undoable);
        refreshGlass();
    }

    int Editor::playheadStep(int panelLane) const
    {
        if (! processor.isPlaying() || processor.playingPattern() != editPattern || processor.playingVariationB() != editB)
            return -1;
        const int length = juce::jlimit(1, 16, laneShown(panelLane).length);
        const auto k = processor.playingSixteenth();
        return static_cast<int>(((k % length) + length) % length);
    }

    int Editor::stepInBar() const
    {
        if (! processor.isPlaying())
            return -1;
        const auto k = processor.playingSixteenth();
        return static_cast<int>(((k % 16) + 16) % 16);
    }

    int Editor::queuedPattern() const
    {
        // A pattern picked while another plays is the whole chain until the bar line takes it.
        if (! processor.isPlaying())
            return -1;
        const int heard = processor.playingPattern();
        // Picked for the next bar line: the processor has not got it yet, but the key lights at the press (D303).
        if (pickedPattern >= 0 && pickedPattern != heard)
            return pickedPattern;
        const auto chain = processor.chain();
        return chain.size() == 1 && heard >= 0 && chain.front() != heard ? chain.front() : -1;
    }

    void Editor::refresh() { board->repaint(); } // its children with it

    void Editor::refreshGlass()
    {
        // What a pattern key or a step changes is under the glass: the board and its knobs stay as drawn (D306).
        sequencer->repaint();
        transport->repaint();
    }

    void Editor::layOut()
    {
        followTransport();
        lidOverlay->setVisible(lidView);
        if (lidView)
            lidOverlay->toFront(false);
        resized();
        refresh();
    }

    void Editor::paint(juce::Graphics& g) { g.fillAll(theme().desk); }


    void Editor::showLid(bool show)
    {
        lidView = show;
        layOut();
    }

    // ---- The options (D311) -----------------------------------------------------------------------------------

    bool Editor::optionsOpen() const noexcept { return optionsWindow != nullptr && optionsWindow->isVisible(); }

    void Editor::toggleOptions()
    {
        if (optionsOpen())
        {
            optionsWindow.reset();
            settings->saveIfNeeded();
            transport->repaint();
            return;
        }
        OptionsHooks hooks;
        hooks.theme = [this] { return themeIndex; };
        hooks.chooseTheme = [this](int i) { chooseTheme(i); };
        hooks.zoom = [this] { return zoomPercent; };
        hooks.chooseZoom = [this](int z) { chooseZoom(z); };
        hooks.followClock = [this] { return processor.followsClock(); };
        hooks.sendClock = [this] { return processor.sendsClock(); };
        hooks.chooseFollowClock = [this](bool on) { processor.setFollowClock(on); settings->setValue("followClock", on); };
        hooks.chooseSendClock = [this](bool on) { processor.setSendClock(on); settings->setValue("sendClock", on); };
        hooks.controllerSetup = [this] { openControllerSetup(); };
        hooks.circuitAnimated = [this] { return circuitAnimated; };
        hooks.chooseCircuitAnimated = [this](bool on) { circuitAnimated = on; settings->setValue("circuitAnimated", on); applyCircuitLook(); };
        hooks.circuitShow = [this] { return circuitShow; };
        hooks.chooseCircuitShow = [this](int i) { circuitShow = juce::jlimit(0, 2, i); settings->setValue("circuitShow", circuitShow); applyCircuitLook(); };
        optionsWindow = std::make_unique<OptionsWindow>(std::move(hooks), standaloneDeviceManager(), settings.get());
        optionsWindow->onClose = [this]
        {
            // after the click that asked has returned
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Editor>(this)]
            {
                if (safe != nullptr && safe->optionsOpen())
                    safe->toggleOptions();
            });
        };
        optionsWindow->keepAbove(*this);
        optionsWindow->setVisible(true);
        optionsWindow->toFront(true);
        // It stays above the 808's window: brought to the front whenever that window is (not above other apps).
        if (auto* top = getTopLevelComponent(); top != nullptr && top != this && top != watchedWindow)
        {
            if (watchedWindow != nullptr)
                watchedWindow->removeComponentListener(this);
            watchedWindow = top;
            watchedWindow->addComponentListener(this);
        }
        transport->repaint();
    }

    void Editor::componentBroughtToFront(juce::Component&)
    {
        if (optionsOpen())
            optionsWindow->toFront(false);
    }

    void Editor::chooseTheme(int index)
    {
        themeIndex = juce::jlimit(0, static_cast<int>(themes().size()) - 1, index);
        setTheme(themeIndex);
        settings->setValue("panelTheme", themes()[static_cast<std::size_t>(themeIndex)].id);
        board->themeChanged();
        if (auto* top = getTopLevelComponent())
            top->repaint();
        if (optionsWindow != nullptr)
            optionsWindow->themeChanged();
        repaint();
        refresh();
    }

    void Editor::applyCircuitLook()
    {
        // The panel's circuit as the options set it (D314); the glass shows its part of it too.
        board->setCircuitLook(circuitAnimated, static_cast<EtchedBoard::CircuitShow>(circuitShow));
        sequencer->repaint();
    }

    void Editor::chooseZoom(int percent)
    {
        zoomPercent = juce::jlimit(75, 200, percent);
        settings->setValue("zoom", zoomPercent);
        setSize(baseWidth * zoomPercent / 100, baseHeight * zoomPercent / 100);
    }

    void Editor::resized()
    {
        // The panel keeps its shape, as large as the window lets it; the desk shows round it.
        const float scale = std::min(getWidth() / static_cast<float>(baseWidth), getHeight() / static_cast<float>(baseHeight));
        const float x = (getWidth() - baseWidth * scale) * 0.5f, y = (getHeight() - baseHeight * scale) * 0.5f;
        board->setTransform(juce::AffineTransform::scale(scale).translated(x, y));
        lidOverlay->setBounds(getLocalBounds());
        // The overlays cover the window, whatever its size; in a window narrower than the panel they are laid out at
        // the panel's width and drawn smaller with it, so their columns keep their room (D273).
        const float widthScale = getWidth() / static_cast<float>(baseWidth);
        for (juce::Component* overlay : { static_cast<juce::Component*>(library.get()), static_cast<juce::Component*>(controllerPanel.get()) })
        {
            if (overlay == nullptr)
                continue;
            if (widthScale < 1.0f)
            {
                overlay->setTransform(juce::AffineTransform::scale(widthScale));
                overlay->setBounds(0, 0, baseWidth, juce::roundToInt(static_cast<float>(getHeight()) / widthScale));
            }
            else
            {
                overlay->setTransform({});
                overlay->setBounds(getLocalBounds());
            }
        }
    }

    // ---- Building the panel -----------------------------------------------------------------------------------

    void Editor::buildColumns()
    {
        // Each column: its name, its knobs in the kit's order, and the voice switch where the drum has one (D292).
        std::array<int, panelLanes> rows {};
        const auto placeKnob = [&](Knob& knob, int column)
        {
            const int row = rows[static_cast<std::size_t>(column)]++;
            const float cx = columnCentre(column);
            const int top = knobTop + row * knobPitch;
            knob.diameter = static_cast<float>(knobSize);
            knob.silver = true; // mounted on the board (D303), coloured as the theme has it (D307)
            knob.setBounds(juce::roundToInt(cx - 30.0f), top - 8, 60, 8 + knobSize + 5 + 12);
            columnKeepOuts.push_back({ juce::Rectangle<float>(2.0f * (knobSize / 2 + 9), 2.0f * (knobSize / 2 + 9)).withCentre({ cx, top + knobSize * 0.5f }), true });
            columnKeepOuts.push_back(wordKeepOut(knob.label.toUpperCase(), labelFont(10.0f), cx, static_cast<float>(top + knobSize + 4), 14.0f));
        };
        for (int k = 0; k < knobCount; ++k)
        {
            const auto& spec = knobs[static_cast<std::size_t>(k)];
            auto& knob = add<Knob>(*board);
            knob.label = spec.label;
            knob.accent = spec.lane < 0; // the Accent's cap is orange
            knob.defaultValue = knobDefault(k);
            knob.get = [this, k] { return processor.project().kit.knob[static_cast<std::size_t>(k)]; };
            knob.set = [this, k](float v) { processor.editKit([k, v](KitValues& kit) { kit.knob[static_cast<std::size_t>(k)] = v; }); };
            knob.gestureStart = [this] { processor.beginGesture(); };
            knob.gestureEnd = [this] { processor.endGesture(); refresh(); };
            placeKnob(knob, spec.lane + 1);
        }
        {
            // The master volume, under the Accent as on the 808 (the review of D267: a controller's knob 8 turned
            // it with nothing on the panel to show it).
            auto& volume = add<Knob>(*board);
            volume.label = "Volume";
            volume.defaultValue = volumePosition(0.0f);
            volume.get = [this] { return volumePosition(processor.project().kit.volumeDb); };
            volume.set = [this](float v) { processor.editKit([v](KitValues& kit) { kit.volumeDb = volumeDbAt(v); }); };
            volume.gestureStart = [this] { processor.beginGesture(); };
            volume.gestureEnd = [this] { processor.endGesture(); };
            placeKnob(volume, 0);
        }
        // The 808's voice selectors, as slide switches: the toms or the congas, the rim shot or the claves, the clap
        // or the maracas.
        const auto words = [](int lane) -> std::pair<const char*, const char*>
        {
            const juce::String code = instruments[static_cast<std::size_t>(lane)].code;
            if (code == "RS")
                return { "Rim shot", "Claves" };
            if (code == "CP")
                return { "Clap", "Maracas" };
            return { "Tom", "Conga" };
        };
        for (int c = 0; c < panelLanes; ++c)
        {
            const float cx = columnCentre(c);
            if (c > 0 && instruments[static_cast<std::size_t>(c - 1)].altCode != nullptr)
            {
                auto& slide = add<SlideSwitch>(*board);
                const auto [first, second] = words(c - 1);
                slide.first = first;
                slide.second = second;
                slide.get = [this, c] { return processor.project().kit.alt[static_cast<std::size_t>(c - 1)]; };
                slide.set = [this, c](bool v) { processor.editKit([c, v](KitValues& kit) { kit.alt[static_cast<std::size_t>(c - 1)] = v; }); refresh(); };
                const int top = knobTop + rows[static_cast<std::size_t>(c)] * knobPitch;
                slide.setBounds(juce::roundToInt(cx - 40.0f), top, 80, SlideSwitch::height);
                columnKeepOuts.push_back(wordKeepOut(slide.first.toUpperCase(), labelFont(10.0f), cx, top - 1.0f, 14.0f));
                columnKeepOuts.push_back({ { cx - 16.0f, top + 13.0f, 32.0f, 50.0f } });
                columnKeepOuts.push_back(wordKeepOut(slide.second.toUpperCase(), labelFont(10.0f), cx, top + 62.0f, 14.0f));
            }
            auto& label = add<ColumnLabel>(*board);
            label.editor = this;
            label.column = c;
            label.setBounds(juce::roundToInt(columnX(c)), 50, juce::roundToInt(columnWidth(c)), 30);
            if (c == 0)
                columnKeepOuts.push_back(wordKeepOut("MASTER", headFont(13.0f), cx, 56.0f, 20.0f));
            else
            {
                columnKeepOuts.push_back(wordKeepOut(instruments[static_cast<std::size_t>(c - 1)].code, headFont(13.0f), cx, 50.0f, 18.0f));
                columnKeepOuts.push_back(wordKeepOut(juce::String(instruments[static_cast<std::size_t>(c - 1)].name).toUpperCase(), labelFont(10.0f), cx, 66.0f, 14.0f));
            }
        }
    }

    void Editor::buildTransport()
    {
        transport = std::make_unique<TransportStrip>(*this);
        board->addAndMakeVisible(*transport);
        const int w = transport->preferredWidth();
        transport->setBounds(baseWidth - 30 - w, 5, w, TransportStrip::height);
        board->setStripArea(transport->getBounds().toFloat());
    }

    void Editor::updateKeepOuts()
    {
        auto list = columnKeepOuts;
        list.push_back({ { 26.0f, 6.0f, 344.0f, 34.0f }, false, true }); // the name plate keeps its outline
        list.push_back({ transport->getBounds().toFloat().expanded(2.0f) });
        board->setKeepOuts(std::move(list));
    }

    // ---- Patterns and chains ----------------------------------------------------------------------------------

    void Editor::selectPatternKey(int index, bool extend)
    {
        // Picked while playing, it comes in at the next bar line or at the end of the pattern that plays, as the
        // Change keys say (D297).
        const auto setChain = [this](const std::vector<int>& chain)
        {
            if (processor.isPlaying() && processor.project().play.changeAtBar)
                processor.atNextBar([&] { processor.setChain(chain); });
            else
                processor.setChain(chain);
        };
        if (extend && firstChainKey >= 0)
        {
            std::vector<int> chain;
            const int step = index >= firstChainKey ? 1 : -1;
            for (int i = firstChainKey; ; i += step)
            {
                chain.push_back(i);
                if (i == index)
                    break;
            }
            setChain(chain);
        }
        else
        {
            firstChainKey = index;
            setChain({ index });
        }
        pickedPattern = processor.isPlaying() && ! extend ? index : -1;
        editPattern = index;
        refreshGlass();
    }

    void Editor::editFill()
    {
        // The F slot: the fill is edited; again, back to the pattern edited before it.
        if (editPattern == -1)
            editPattern = patternBeforeFill;
        else
        {
            patternBeforeFill = editPattern;
            editPattern = -1;
        }
        refreshGlass();
    }

    void Editor::queueFill()
    {
        // The fill plays in the next bar; asked while stopped, nothing happens, so nothing is lit.
        if (! processor.isPlaying())
            return;
        processor.requestFill();
        fillAsked = true;
        refreshGlass();
    }

    void Editor::addToChain(int pattern)
    {
        if (pattern < 0)
            return;
        auto chain = processor.chain();
        if (static_cast<int>(chain.size()) >= coolsynth::synth::StepSequencer::maxChain)
            return;
        chain.push_back(pattern);
        processor.setChain(chain);
        refresh();
    }

    void Editor::selectBankTab(int index)
    {
        // A tab past the banks there are makes the banks up to it, each new and empty.
        while (static_cast<int>(processor.project().banks.size()) <= index && static_cast<int>(processor.project().banks.size()) < Project::maxBanks)
            processor.addBank();
        processor.selectBank(index);
        refresh();
    }

    void Editor::copyPattern(int pattern)
    {
        const auto& bank = processor.project().currentBank();
        clipboard = pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)];
    }

    void Editor::pastePattern(int pattern)
    {
        if (! clipboard)
            return;
        const auto copy = *clipboard;
        processor.editBank([&](SequencerBank& bank) { (pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)]) = copy; });
        refresh();
    }

    void Editor::clearPattern(int pattern)
    {
        processor.editBank([&](SequencerBank& bank) { (pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)]) = SequencerPattern {}; });
        refresh();
    }

    void Editor::followTransport()
    {
        // Where the chain plays: the audio thread reports the pattern and the step, so each bar line moves on to the
        // next entry that plays that pattern. A fill takes its bar's entry, as the chain counts on under it.
        if (! processor.isPlaying())
        {
            chainPlace = -1;
            lastPatternHeard = -2;
            lastStep = -1;
            fillAsked = false;
            return;
        }
        const auto chain = processor.chain();
        const int heard = processor.playingPattern();
        const int step = stepInBar();
        if (heard == -1)
            fillAsked = false;
        const bool chainChanged = chain != lastChain;
        const bool newBar = heard != lastPatternHeard || step < lastStep || chainChanged;
        if (newBar && ! chain.empty())
        {
            const int n = static_cast<int>(chain.size());
            const auto find = [&](int from)
            {
                for (int i = 0; i < n; ++i)
                    if (chain[static_cast<std::size_t>((from + i) % n)] == heard)
                        return (from + i) % n;
                return -1;
            };
            if (chainChanged || chainPlace < 0 || chainPlace >= n)
                chainPlace = heard == -1 ? std::max(0, chainPlace) : find(0);
            else if (heard == -1)
                chainPlace = (chainPlace + 1) % n;
            else
                chainPlace = find((chainPlace + 1) % n);
        }
        lastChain = chain;
        lastPatternHeard = heard;
        lastStep = step;
    }

    // ---- Files, library, themes -------------------------------------------------------------------------------

    void Editor::showFileMenu()
    {
        juce::PopupMenu m;
        m.addItem(1, "New project");
        m.addItem(2, "Open project...");
        m.addItem(3, "Save project", true);
        m.addItem(4, "Save project as...");
        m.addSeparator();
        m.addItem(5, "Keep this pattern in the library...");
        m.addSeparator();
        juce::PopupMenu banks;
        const auto& p = processor.project();
        for (int i = 0; i < static_cast<int>(p.banks.size()); ++i)
            banks.addItem(100 + i, "Bank " + (i < 26 ? juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)) : juce::String(i + 1)), true, i == p.bank);
        banks.addSeparator();
        banks.addItem(99, "New bank", static_cast<int>(p.banks.size()) < Project::maxBanks);
        m.addSubMenu("Bank", banks);
        m.addSeparator();
        m.addItem(7, "Undo", processor.canUndo());
        m.addItem(8, "Redo", processor.canRedo());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                        [safe = juce::Component::SafePointer<Editor>(this)](int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            auto& e = *safe;
            if (r == 1) e.newProject();
            else if (r == 2) e.openProject();
            else if (r == 3) e.saveProject(false);
            else if (r == 4) e.saveProject(true);
            else if (r == 5) e.savePatternToLibrary(e.editPattern);
            else if (r == 7) { e.processor.undo(); e.refresh(); }
            else if (r == 8) { e.processor.redo(); e.refresh(); }
            else if (r == 99) { e.processor.addBank(); e.refresh(); }
            else if (r >= 100 && r < 200) { e.processor.selectBank(r - 100); e.refresh(); }
        });
    }

    void Editor::newProject()
    {
        processor.replaceProject(Project {}, false);
        processor.forgetHistory(); // a new project starts its own history, as an opened one
        processor.setCurrentFile({});
        resetSelection();
    }

    void Editor::openProject()
    {
        chooser = std::make_unique<juce::FileChooser>("Open a CoolSynth 808 project", appDataFolder(), "*.cs808");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile())
                return;
            if (const auto r = processor.loadFrom(f); r.failed())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Cannot open the project", r.getErrorMessage());
                return;
            }
            resetSelection();
        });
    }

    void Editor::saveProject(bool askName)
    {
        const auto save = [this](const juce::File& f)
        {
            // A save that fails says so (the review of D267): the file read-only, its folder gone.
            if (! processor.saveTo(f))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Cannot save the project", f.getFullPathName());
        };
        if (! askName && processor.currentFile() != juce::File())
        {
            save(processor.currentFile());
            return;
        }
        (void) appDataFolder().createDirectory();
        const auto current = processor.currentFile();
        const auto proposed = current != juce::File() ? current : appDataFolder().getChildFile("Untitled.cs808");
        chooser = std::make_unique<juce::FileChooser>("Save the project", proposed, "*.cs808");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this, save](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File())
                return;
            if (f.hasFileExtension("cs808"))
            {
                save(f);
                return;
            }
            // The extension added here, after the dialog's own warning: a file of that name is asked about again.
            f = f.withFileExtension("cs808");
            if (! f.existsAsFile())
            {
                save(f);
                return;
            }
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Replace the project?",
                                               f.getFileName() + " is there already. Replace it?", "Replace", "Cancel", this,
                                               juce::ModalCallbackFunction::create([save, f](int r) { if (r == 1) save(f); }));
        });
    }

    void Editor::savePatternToLibrary(int pattern)
    {
        auto* window = new juce::AlertWindow("Keep this pattern", "A name for it in the library:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor("name", "My pattern");
        window->addButton("Keep", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        window->enterModalState(true, juce::ModalCallbackFunction::create([this, window, pattern](int r)
        {
            if (r == 1)
            {
                const auto& bank = processor.project().currentBank();
                PatternFile f;
                f.name = window->getTextEditorContents("name");
                f.pattern = pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)];
                f.alt = processor.project().kit.alt;
                f.play = processor.project().play;
                f.usesB = f.play.variation != SequencerVariation::a;
                if (saveUserPattern(f) == juce::File())
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Cannot keep the pattern", userPatternsFolder().getFullPathName());
                else if (library)
                    library->reload();
            }
        }), true);
    }

    void Editor::openLibrary()
    {
        library = std::make_unique<LibraryShelf>();
        library->loadTarget = editPattern < 0 ? juce::String("the fill") : "pattern " + juce::String(editPattern + 1);
        // An overlay is let go after its own callback has returned, never inside it (the review of D267).
        library->onClose = [this] { closeOverlaysLater(); };
        library->onLoad = [this](const LibraryEntry& e)
        {
            loadFromShelf(e);
            closeOverlaysLater();
        };
        library->onAudition = [this](const LibraryEntry* e)
        {
            if (e != nullptr)
                startAudition(*e);
            else
                stopAudition();
        };
        addAndMakeVisible(*library);
        resized();
        library->grabKeyboardFocus();
    }

    void Editor::openControllerSetup()
    {
        controllerPanel = std::make_unique<ControllerPanel>(controls.get());
        controllerPanel->onClose = [this] { closeOverlaysLater(); };
        addAndMakeVisible(*controllerPanel);
        resized();
        controllerPanel->grabKeyboardFocus();
    }

    void Editor::loadLibraryEntry(const LibraryEntry& e, bool undoable) { writeLibraryEntry(e, undoable, editPattern); }

    void Editor::loadFromShelf(const LibraryEntry& e)
    {
        // The beat that plays in an audition is kept as it plays, without a stop or a jump: what it replaced is the
        // undo step (D274). Another beat, or the fill, is loaded as ever, after the audition has given yours back.
        if (auditionSaved != nullptr && auditionKey == libraryKey(e) && editPattern >= 0)
        {
            processor.keepAsOneStep(*auditionSaved);
            if (auditionStartedPlaying)
                processor.setPlaying(false);
            auditionSaved.reset();
            auditionChain.clear();
            auditionStartedPlaying = false;
            auditionKey.clear();
            if (library)
                library->auditionEnded();
            refresh();
            return;
        }
        stopAudition();
        loadLibraryEntry(e);
    }

    void Editor::writeLibraryEntry(const LibraryEntry& e, bool undoable, int into)
    {
        // The beat goes into the pattern being edited; a beat of several parts into the patterns after it too, and
        // they become the chain. Edited, the fill takes the beat's first part and the chain stays. Its tempo, swing,
        // variation and switches come with it. One undo step takes it all back (the review of D267); an audition
        // takes none (D273).
        if (undoable)
            processor.beginGesture();
        processor.editPlay([&](PlaySettings& p) { p = e.pattern.play; }, undoable);
        processor.editKit([&](KitValues& kit) { kit.alt = e.pattern.alt; }, undoable);
        if (into < 0)
            processor.editBank([&](SequencerBank& bank) { bank.fill = e.pattern.pattern; }, undoable);
        else
        {
            const int first = into;
            const int partCount = std::min(1 + static_cast<int>(e.pattern.moreParts.size()), SequencerBank::patterns - first);
            processor.editBank([&](SequencerBank& bank)
            {
                bank.pattern[static_cast<std::size_t>(first)] = e.pattern.pattern;
                for (int i = 1; i < partCount; ++i)
                    bank.pattern[static_cast<std::size_t>(first + i)] = e.pattern.moreParts[static_cast<std::size_t>(i - 1)].pattern;
            }, undoable);
            std::vector<int> chain;
            for (int i = 0; i < partCount; ++i)
                chain.push_back(first + i);
            processor.setChain(chain);
        }
        if (undoable)
            processor.endGesture();
        editB = false;
        refresh();
    }

    void Editor::startAudition(const LibraryEntry& e)
    {
        // The first beat heard keeps the project as it was; another beat after it starts again from that. While
        // playing, it comes in on the next bar line, from its start, as an MPC's browser auditions in sync (D274).
        // Heard while the fill is edited, it plays from the first pattern of the chain: the fill alone is never heard.
        processor.atNextBar([&]
        {
            if (auditionSaved == nullptr)
            {
                auditionSaved = std::make_unique<Project>(processor.project());
                auditionChain = processor.chain();
                auditionStartedPlaying = ! processor.isPlaying();
            }
            else
            {
                processor.replaceProject(*auditionSaved, false);
                processor.setChain(auditionChain);
            }
            writeLibraryEntry(e, false, editPattern >= 0 ? editPattern : (auditionChain.empty() ? 0 : auditionChain.front()));
        });
        auditionKey = libraryKey(e);
        if (auditionStartedPlaying)
            processor.setPlaying(true);
    }

    void Editor::stopAudition()
    {
        if (auditionSaved == nullptr)
            return;
        // Yours comes back on the next bar line too, its chain from the start; at once when the audition stops playing.
        processor.atNextBar([&]
        {
            processor.replaceProject(*auditionSaved, false);
            processor.setChain(auditionChain);
        });
        if (auditionStartedPlaying)
            processor.setPlaying(false);
        auditionSaved.reset();
        auditionChain.clear();
        auditionStartedPlaying = false;
        auditionKey.clear();
        if (library)
            library->auditionEnded();
        refresh();
    }

    void Editor::closeOverlaysLater()
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Editor>(this)]
        {
            if (safe == nullptr)
                return;
            safe->stopAudition();
            safe->library.reset();
            safe->controllerPanel.reset();
            safe->grabKeyboardFocus();
        });
    }

    void Editor::resetSelection()
    {
        // A project opened or begun anew: nothing selected from the one before.
        editPattern = 0;
        patternBeforeFill = 0;
        editB = false;
        selectedStep = -1;
        firstChainKey = -1;
        pickedPattern = -1;
        refresh();
    }

    void Editor::makeMidiDragSource(juce::Component& key)
    {
        key.addMouseListener(this, false);
        key.getProperties().set("midiDrag", true);
    }

    void Editor::startMidiDrag()
    {
        const auto& p = processor.project();
        const auto name = editPattern < 0 ? juce::String("Fill") : "Pattern " + juce::String(editPattern + 1).paddedLeft('0', 2);
        const auto f = writePatternMidi(patternShown(), p.kit, p.play, "CoolSynth 808 " + name);
        if (f.existsAsFile())
        {
            midiDragging = true;
            performExternalDragDropOfFiles({ f.getFullPathName() }, false, this, [this] { midiDragging = false; });
        }
    }

    void Editor::tapTempo()
    {
        // The tempo from the last taps' average gap; a pause of two seconds begins the count again.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0)
            taps.clear();
        taps.push_back(now);
        if (taps.size() > 5)
            taps.erase(taps.begin());
        if (taps.size() < 2 || processor.followingClock())
            return;
        const double gap = (taps.back() - taps.front()) / static_cast<double>(taps.size() - 1);
        const double tempo = juce::jlimit(30.0, 300.0, std::round(60000.0 / gap * 2.0) / 2.0);
        processor.editPlay([tempo](PlaySettings& s) { s.tempo = tempo; });
        refresh();
    }

    // ---- Keys and time ----------------------------------------------------------------------------------------

    bool Editor::keyPressed(const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (lidView && key == juce::KeyPress::escapeKey)
        {
            showLid(false);
            return true;
        }
        if (key == juce::KeyPress::spaceKey)
        {
            processor.setPlaying(! processor.isPlaying());
            return true;
        }
        if (mods.isCommandDown() && key.getKeyCode() == 'Z')
        {
            mods.isShiftDown() ? processor.redo() : processor.undo();
            refresh();
            return true;
        }
        if (mods.isCommandDown() && key.getKeyCode() == 'Y')
        {
            processor.redo();
            refresh();
            return true;
        }
        if (mods.isCommandDown() && key.getKeyCode() == 'S')
        {
            saveProject(mods.isShiftDown());
            return true;
        }
        if (mods.isCommandDown() && key.getKeyCode() == 'O')
        {
            openProject();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
        {
            selectedLane = juce::jlimit(0, panelLanes - 1, selectedLane + (key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1));
            refresh();
            return true;
        }
        return false;
    }

    void Editor::timerCallback()
    {
        bool edited = processor.applyRecordedHits();
        const auto bits = processor.takeStruckLanes();
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (bits != 0)
        {
            lid->struck(bits);
            board->struck(bits, now);
        }
        lid->tick(now);
        board->sequencerState(processor.isPlaying() ? processor.playingSixteenth() : -1, processor.editCount(), now);
        std::array<float, 12> lights {};
        processor.takeLights(lights);
        board->liveLevels(lights, processor.lightReports(), now);
        board->tick(now);
        followTransport();
        // The panel is drawn again only when something it shows changed (the review of D267: it was drawn whole 30
        // times a second); what only moves with the beat redraws the glass and the strip, not the board.
        // The board shows the kit (its knobs, its switches, the volume) and the lane selected; everything else of the
        // project is under the glass. An edit of the steps, patterns or chain redraws the glass alone, not the board
        // with its knobs (D306: a whole redraw is what made a press show late).
        const auto fold = [](std::uint64_t& h, std::int64_t v) { h = h * 1000003u ^ static_cast<std::uint64_t>(v); };
        std::uint64_t look = 0;
        const auto& kit = processor.project().kit;
        for (const float v : kit.knob)
            fold(look, juce::roundToInt(v * 100000.0f));
        for (const bool a : kit.alt)
            fold(look, a);
        fold(look, juce::roundToInt(kit.volumeDb * 1000.0f));
        fold(look, selectedLane);
        std::uint64_t glassLook = processor.editCount();
        fold(glassLook, editPattern);
        fold(glassLook, editB);
        fold(glassLook, static_cast<std::int64_t>(processor.project().bank));
        for (const int c : processor.chain())
            fold(glassLook, c);
        std::uint64_t beat = 0;
        fold(beat, processor.isPlaying() ? processor.playingSixteenth() : -1);
        fold(beat, processor.playingPattern());
        fold(beat, processor.playingVariationB());
        fold(beat, processor.isRecording());
        fold(beat, processor.followingClock() ? juce::roundToInt(processor.tempoNow() * 10.0) : -1);
        fold(beat, fillAsked);
        fold(beat, chainPlace);
        // A pick that has come in, or a stopped transport, ends the wait (D303).
        if (pickedPattern >= 0 && (! processor.isPlaying() || processor.playingPattern() == pickedPattern))
            pickedPattern = -1;
        fold(beat, queuedPattern()); // the queued slot is lit steady, no longer blinking
        if (edited || look != lastLook)
        {
            lastLook = look;
            lastGlassLook = glassLook;
            lastBeat = beat;
            refresh();
        }
        else if (glassLook != lastGlassLook)
        {
            lastGlassLook = glassLook;
            lastBeat = beat;
            sequencer->repaint();
            transport->repaint();
        }
        else if (beat != lastBeat)
        {
            lastBeat = beat;
            sequencer->repaintPlaying();
            transport->repaint();
        }
    }

    void Editor::mouseDrag(const juce::MouseEvent& e)
    {
        if (static_cast<bool>(e.eventComponent->getProperties()["midiDrag"]) && e.getDistanceFromDragStart() > 6 && ! midiDragging)
            startMidiDrag();
    }
}
