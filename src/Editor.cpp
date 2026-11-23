#include "Editor.h"

#include "Controls.h"
#include "ui/ControllerPanel.h"
#include "ui/LibraryShelf.h"

#include "MidiExport.h"
#include "ui/LidView.h"

namespace cs808
{
    namespace
    {
        // The panel's base layout (scaled to the window).
        constexpr int margin = 16;
        constexpr int headerHeight = 64;
        constexpr int soundTop = 64;
        constexpr int soundHeight = 296;
        constexpr int stripY = 364;
        constexpr int transportTop = 378;
        constexpr int keysLeft = 316;
        constexpr int keysRight = 1050;
        constexpr int keysTop = 394;
        constexpr int keysHeight = 56;
        constexpr int handleTop = 458;
        constexpr int lanesTop = 482;
        constexpr int laneHeight = 31;
        constexpr int panelLanes = laneCount + 1;
        constexpr int inspectorHeight = 70;
        constexpr int lidHeight = 470;

        int baseHeight(bool lanesOpen) noexcept
        {
            return lanesTop + (lanesOpen ? panelLanes * laneHeight + 8 : 0) + inspectorHeight + margin;
        }

        /** Where step i of sixteen sits between two x positions, with a gap after every fourth. */
        juce::Rectangle<float> stepCell(int i, float left, float right, float top, float height)
        {
            constexpr float gap = 6.0f, groupGap = 8.0f;
            const float width = (right - left - 15.0f * gap - 3.0f * groupGap) / 16.0f;
            const float x = left + i * (width + gap) + (i / 4) * groupGap;
            return { x, top, width, height };
        }

        int stepAt(float x, float left, float right)
        {
            for (int i = 0; i < 16; ++i)
                if (const auto c = stepCell(i, left, right, 0.0f, 1.0f); x >= c.getX() - 3.0f && x < c.getRight() + 3.0f)
                    return i;
            return -1;
        }

        juce::String laneCode(int panelLane, const KitValues& kit)
        {
            if (panelLane == 0)
                return "AC";
            const auto& inst = instruments[static_cast<std::size_t>(panelLane - 1)];
            return kit.alt[static_cast<std::size_t>(panelLane - 1)] && inst.altCode != nullptr ? inst.altCode : inst.code;
        }

        juce::String laneName(int panelLane, const KitValues& kit)
        {
            if (panelLane == 0)
                return "Accent";
            const auto& inst = instruments[static_cast<std::size_t>(panelLane - 1)];
            return kit.alt[static_cast<std::size_t>(panelLane - 1)] && inst.altName != nullptr ? inst.altName : inst.name;
        }

        /** Paints one step key: its beat group's shade, a fill as tall as its chance, a roll as split bars. */
        void paintStep(juce::Graphics& g, juce::Rectangle<float> r, int index, const coolsynth::synth::SequencerStep& step,
                       bool inLength, bool playhead, bool selected, float radius)
        {
            const auto& th = theme();
            g.setColour(th.group[static_cast<std::size_t>(index / 4)].withAlpha(inLength ? 1.0f : 0.3f));
            g.fillRoundedRectangle(r, radius);
            if (step.on && inLength)
            {
                const float h = r.getHeight() * juce::jlimit(0.12f, 1.0f, step.chance);
                const int hits = juce::jlimit(1, 3, static_cast<int>(step.hits));
                const float w = (r.getWidth() - 2.0f * (hits - 1)) / hits;
                g.setColour(th.on);
                for (int k = 0; k < hits; ++k)
                    g.fillRoundedRectangle(juce::Rectangle<float>(r.getX() + k * (w + 2.0f), r.getBottom() - h, w, h), radius);
            }
            if (selected || playhead)
            {
                g.setColour(playhead ? th.lit : th.ink);
                g.drawRoundedRectangle(r.expanded(2.0f), radius + 1.0f, 2.0f);
            }
        }
    }

    // =============================================================================================================
    // The content, drawn at the base size

    class Editor::Content final : public juce::Component
    {
    public:
        explicit Content(Editor& e) : editor(e) {}

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            const auto& kit = editor.processor.project().kit;
            g.fillAll(th.panel);
            if (th.grid)
            {
                g.setColour(th.ink.withAlpha(0.06f));
                for (int x = 0; x < getWidth(); x += 20)
                    g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
                for (int y = 0; y < getHeight(); y += 20)
                    g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
            }
            // The name.
            g.setColour(th.ink);
            g.setFont(uiFont(27.0f));
            g.drawText("CoolSynth", 24, 14, 140, 32, juce::Justification::centredLeft);
            const int nameWidth = juce::GlyphArrangement::getStringWidthInt(uiFont(27.0f), "CoolSynth ");
            g.setFont(uiFont(27.0f, true));
            g.drawText("808", 24 + nameWidth, 14, 80, 32, juce::Justification::centredLeft);
            g.setColour(th.dim);
            g.setFont(uiFont(10.0f));
            g.drawText("BEAT COMPOSER", 25, 44, 200, 14, juce::Justification::centredLeft);

            if (! editor.lidView)
            {
                // The knob columns: a line between each, the selected one shaded, each code on top.
                const float colW = static_cast<float>(getWidth() - 2 * margin) / panelLanes;
                for (int c = 0; c < panelLanes; ++c)
                {
                    const float x = margin + c * colW;
                    if (c == editor.selectedLane)
                    {
                        g.setColour(th.ink.withAlpha(0.07f));
                        g.fillRect(juce::Rectangle<float>(x, static_cast<float>(soundTop), colW, static_cast<float>(soundHeight)));
                    }
                    if (c > 0)
                    {
                        g.setColour(th.line);
                        g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(soundTop), static_cast<float>(soundTop + soundHeight));
                    }
                    g.setColour(th.ink);
                    g.setFont(uiFont(15.0f, true));
                    g.drawText(laneCode(c, kit), juce::Rectangle<float>(x, soundTop + 6.0f, colW, 20.0f), juce::Justification::centred);
                }
            }
            // The strip.
            g.setColour(th.strip);
            g.fillRoundedRectangle(24.0f, static_cast<float>(editor.lidView ? lidTop() + lidHeight - 4 : stripY), getWidth() - 48.0f, 3.0f, 1.5f);

            // The lane the step keys show.
            const int t = transportY();
            g.setColour(th.ink);
            g.setFont(uiFont(13.0f, true));
            g.drawText(laneCode(editor.selectedLane, kit) + "  " + laneName(editor.selectedLane, kit), 222, t + 10, 100, 18, juce::Justification::centredLeft);
            g.setColour(th.dim);
            g.setFont(uiFont(9.5f));
            g.drawText(editor.patternMode ? "PATTERNS SHOWN" : "STEPS SHOWN", 222, t + 28, 100, 12, juce::Justification::centredLeft);

            // The step numbers.
            g.setFont(uiFont(10.0f));
            for (int i = 0; i < 16; ++i)
            {
                const auto c = stepCell(i, keysLeft, keysRight, static_cast<float>(t), 14.0f);
                g.drawText(juce::String(i + 1), c, juce::Justification::centred);
            }

            // The handle.
            const int h = handleY();
            g.setColour(th.line);
            g.drawHorizontalLine(h + 10, 24.0f, getWidth() / 2.0f - 70.0f);
            g.drawHorizontalLine(h + 10, getWidth() / 2.0f + 70.0f, getWidth() - 24.0f);
            g.setColour(th.dim);
            g.setFont(uiFont(10.0f));
            g.drawText(editor.lanesOpen ? "CLOSE THE LANES" : "OPEN THE LANES", getWidth() / 2 - 70, h, 140, 20, juce::Justification::centred);

            // The inspector's line.
            g.setColour(th.line);
            g.drawHorizontalLine(inspectorY() - 6, static_cast<float>(margin), static_cast<float>(getWidth() - margin));
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            const int h = handleY();
            if (e.mouseWasClicked() && e.y >= h && e.y < h + 22 && std::abs(e.x - getWidth() / 2) < 120)
            {
                editor.lanesOpen = ! editor.lanesOpen;
                editor.layoutForLanes();
            }
        }

        [[nodiscard]] int lidTop() const noexcept { return soundTop; }
        [[nodiscard]] int shift() const noexcept { return editor.lidView ? lidHeight - soundHeight : 0; }
        [[nodiscard]] int transportY() const noexcept { return transportTop + shift(); }
        [[nodiscard]] int handleY() const noexcept { return handleTop + shift(); }
        [[nodiscard]] int lanesY() const noexcept { return lanesTop + shift(); }
        [[nodiscard]] int inspectorY() const noexcept { return lanesY() + (editor.lanesOpen ? panelLanes * laneHeight + 8 : 0) + 6; }

    private:
        Editor& editor;
    };

    // =============================================================================================================
    // A drum's name key: strikes it and selects its lane; its light shows each hit

    class NameKey final : public juce::Component
    {
    public:
        Editor* editor = nullptr;
        int panelLane = 0;

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            const bool sel = editor->selectedLane == panelLane;
            const auto r = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(sel ? th.ink : th.line);
            g.drawRoundedRectangle(r, 6.0f, 1.0f);
            const bool lit = juce::Time::getMillisecondCounterHiRes() < editor->ledUntil[static_cast<std::size_t>(panelLane)];
            const auto led = juce::Rectangle<float>(r.getCentreX() - 3.0f, r.getY() + 7.0f, 6.0f, 6.0f);
            if (lit)
            {
                g.setColour(th.lit.withAlpha(0.35f));
                g.fillEllipse(led.expanded(3.0f));
            }
            g.setColour(lit ? th.lit : th.line);
            g.fillEllipse(led);
            g.setColour(sel ? th.ink : th.dim);
            g.setFont(uiFont(10.5f));
            g.drawFittedText(laneName(panelLane, editor->processor.project().kit), getLocalBounds().withTrimmedTop(16), juce::Justification::centred, 1);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            editor->selectedLane = panelLane;
            if (panelLane > 0)
                editor->processor.strike(panelLane - 1, e.mods.isShiftDown());
            editor->refresh();
        }
    };

    // =============================================================================================================
    // The sixteen step keys: the selected lane's steps, or the bank's patterns

    class Editor::StepKeys final : public juce::Component
    {
    public:
        explicit StepKeys(Editor& e) : editor(e) {}

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            const float w = static_cast<float>(getWidth());
            if (editor.patternMode)
            {
                const auto chain = editor.processor.chain();
                const int playingPattern = editor.processor.isPlaying() ? editor.processor.playingPattern() : -2;
                for (int i = 0; i < 16; ++i)
                {
                    const auto r = stepCell(i, 0.0f, w, 0.0f, static_cast<float>(getHeight()));
                    g.setColour(th.group[static_cast<std::size_t>(i / 4)]);
                    g.fillRoundedRectangle(r, 6.0f);
                    const bool inChain = std::find(chain.begin(), chain.end(), i) != chain.end();
                    if (inChain)
                    {
                        g.setColour(th.on);
                        g.fillRoundedRectangle(r.reduced(0.0f, r.getHeight() * 0.62f).withY(r.getBottom() - r.getHeight() * 0.38f), 6.0f);
                    }
                    if (i == playingPattern || i == editor.editPattern)
                    {
                        g.setColour(i == playingPattern ? th.lit : th.ink);
                        g.drawRoundedRectangle(r.expanded(2.0f), 7.0f, 2.0f);
                    }
                    g.setColour(inChain ? th.onInk : th.ink);
                    g.setFont(uiFont(12.0f, true));
                    g.drawText(juce::String(i + 1).paddedLeft('0', 2), r.withTrimmedBottom(r.getHeight() * 0.45f), juce::Justification::centred);
                }
                return;
            }
            const auto& lane = editor.laneShown(editor.selectedLane);
            const auto& steps = editor.editB ? lane.b : lane.a;
            const int head = editor.playheadStep(editor.selectedLane);
            for (int i = 0; i < 16; ++i)
                paintStep(g, stepCell(i, 0.0f, w, 0.0f, static_cast<float>(getHeight())), i, steps[static_cast<std::size_t>(i)],
                          i < lane.length, i == head, i == editor.selectedStep, 6.0f);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            pressed = stepAt(static_cast<float>(e.x), 0.0f, static_cast<float>(getWidth()));
            dragged = false;
            if (pressed < 0)
                return;
            if (editor.patternMode)
            {
                editor.selectPatternKey(pressed, e.mods.isShiftDown());
                return;
            }
            if (e.mods.isPopupMenu())
            {
                editor.selectedStep = pressed;
                editor.refresh();
                pressed = -1;
                return;
            }
            // What the press edits is fixed at the press: a controller may move the drum or pattern while it is held.
            pressLane = editor.selectedLane;
            pressPattern = editor.editPattern;
            pressB = editor.editB;
            const auto& steps = editor.editB ? editor.laneShown(editor.selectedLane).b : editor.laneShown(editor.selectedLane).a;
            startChance = steps[static_cast<std::size_t>(pressed)].on ? steps[static_cast<std::size_t>(pressed)].chance : 1.0f;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (pressed < 0 || editor.patternMode || std::abs(e.getDistanceFromDragStartY()) < 4)
                return;
            if (! dragged)
                editor.processor.beginGesture();
            dragged = true;
            const float chance = juce::jlimit(0.05f, 1.0f, startChance - e.getDistanceFromDragStartY() / 50.0f);
            const int step = pressed;
            const bool b = pressB;
            editor.editLane(pressLane, [&](SequencerLane& l)
            {
                auto& s = (b ? l.b : l.a)[static_cast<std::size_t>(step)];
                s.on = true;
                s.chance = std::round(chance * 20.0f) / 20.0f;
            }, false, pressPattern);
            editor.selectedStep = step;
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            if (pressed >= 0 && ! editor.patternMode)
            {
                if (dragged)
                    editor.processor.endGesture();
                else
                {
                    const int step = pressed;
                    const bool b = pressB;
                    bool nowOn = false;
                    editor.editLane(pressLane, [&](SequencerLane& l)
                    {
                        auto& s = (b ? l.b : l.a)[static_cast<std::size_t>(step)];
                        s.on = ! s.on;
                        if (s.on)
                        {
                            s.chance = 1.0f;
                            s.hits = 1;
                        }
                        nowOn = s.on;
                    }, true, pressPattern);
                    editor.selectedStep = nowOn ? step : -1;
                }
            }
            pressed = -1;
            editor.refresh();
        }

    private:
        Editor& editor;
        int pressed = -1;
        int pressLane = 1, pressPattern = 0;
        bool pressB = false;
        bool dragged = false;
        float startChance = 1.0f;
    };

    // =============================================================================================================
    // The lanes: one row per drum, its steps, mute, solo and length

    class Editor::LaneRows final : public juce::Component
    {
    public:
        explicit LaneRows(Editor& e) : editor(e)
        {
            // Each lane's knobs, small, as the mockup has them: the same knobs as the columns above.
            std::array<int, panelLanes> count {};
            for (int k = 0; k < knobCount; ++k)
            {
                const int l = knobs[static_cast<std::size_t>(k)].lane + 1;
                auto* knob = new Knob();
                minis.add(knob);
                addAndMakeVisible(knob);
                knob->small = true;
                knob->label = knobs[static_cast<std::size_t>(k)].label;
                knob->defaultValue = knobDefault(k);
                knob->get = [this, k] { return editor.processor.project().kit.knob[static_cast<std::size_t>(k)]; };
                knob->set = [this, k](float v) { editor.processor.editKit([k, v](KitValues& kit) { kit.knob[static_cast<std::size_t>(k)] = v; }); };
                knob->gestureStart = [this] { editor.processor.beginGesture(); };
                knob->gestureEnd = [this] { editor.processor.endGesture(); editor.refresh(); };
                miniPlace.push_back({ l, count[static_cast<std::size_t>(l)]++ });
            }
            for (int l = 0; l < panelLanes; ++l)
            {
                auto* box = new NumberBox();
                lengths.add(box);
                addAndMakeVisible(box);
                box->label = "Length";
                box->labelBelow = false;
                box->valueHeight = 12.0f;
                box->minimum = 1;
                box->maximum = 16;
                box->defaultValue = 16;
                box->pixelsPerStep = 8.0;
                box->get = [this, l] { return static_cast<double>(editor.laneShown(l).length); };
                box->set = [this, l](double v) { editor.editLane(l, [v](SequencerLane& lane) { lane.length = static_cast<int>(v); }, false); };
                box->gestureStart = [this] { editor.processor.beginGesture(); };
                box->gestureEnd = [this] { editor.processor.endGesture(); };
            }
        }

        void resized() override
        {
            for (int l = 0; l < panelLanes; ++l)
                lengths[l]->setBounds(keysRight - margin + 22, l * laneHeight + 6, 90, laneHeight - 12);
            for (int i = 0; i < minis.size(); ++i)
                minis[i]->setBounds(222 + miniPlace[static_cast<std::size_t>(i)].second * 23, miniPlace[static_cast<std::size_t>(i)].first * laneHeight + 5, 21, 21);
        }

        void paint(juce::Graphics& g) override
        {
            const auto& th = theme();
            const auto& kit = editor.processor.project().kit;
            bool anySolo = false;
            for (int l = 1; l < panelLanes; ++l)
                anySolo = anySolo || editor.laneShown(l).solo;
            for (int l = 0; l < panelLanes; ++l)
            {
                const float y = static_cast<float>(l * laneHeight);
                const auto& lane = editor.laneShown(l);
                if (l == editor.selectedLane)
                {
                    g.setColour(th.ink.withAlpha(0.07f));
                    g.fillRect(0.0f, y, static_cast<float>(getWidth()), static_cast<float>(laneHeight));
                }
                const bool lit = juce::Time::getMillisecondCounterHiRes() < editor.ledUntil[static_cast<std::size_t>(l)];
                g.setColour(lit ? th.lit : th.line);
                g.fillEllipse(8.0f, y + 12.5f, 6.0f, 6.0f);
                g.setColour(th.ink);
                g.setFont(uiFont(12.0f, true));
                g.drawText(laneCode(l, kit), juce::Rectangle<float>(20.0f, y, 34.0f, static_cast<float>(laneHeight)), juce::Justification::centredLeft);
                g.setColour(th.dim);
                g.setFont(uiFont(10.5f));
                g.drawText(laneName(l, kit), juce::Rectangle<float>(54.0f, y, 110.0f, static_cast<float>(laneHeight)), juce::Justification::centredLeft);
                if (l > 0)
                {
                    paintToggle(g, muteBox(l), "M", lane.mute);
                    paintToggle(g, soloBox(l), "S", lane.solo);
                }
                const bool silent = l > 0 && (lane.mute || (anySolo && ! lane.solo));
                const auto& steps = editor.editB ? lane.b : lane.a;
                const int head = editor.playheadStep(l);
                juce::Graphics::ScopedSaveState state(g);
                if (silent)
                    g.setOpacity(0.35f);
                for (int i = 0; i < 16; ++i)
                    paintStep(g, stepCell(i, keysLeft - margin, keysRight - margin, y + 4.5f, laneHeight - 9.0f), i, steps[static_cast<std::size_t>(i)],
                              i < lane.length, i == head, l == editor.selectedLane && i == editor.selectedStep, 4.0f);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            const int l = juce::jlimit(0, panelLanes - 1, e.y / laneHeight);
            pressedLane = l;
            pressedStep = -1;
            dragged = false;
            if (e.x < 170)
            {
                editor.selectedLane = l;
                if (l > 0)
                    editor.processor.strike(l - 1, e.mods.isShiftDown());
                editor.refresh();
                return;
            }
            if (l > 0 && muteBox(l).contains(e.position))
            {
                editor.editLane(l, [](SequencerLane& lane) { lane.mute = ! lane.mute; });
                return;
            }
            if (l > 0 && soloBox(l).contains(e.position))
            {
                editor.editLane(l, [](SequencerLane& lane) { lane.solo = ! lane.solo; });
                return;
            }
            pressedStep = stepAt(static_cast<float>(e.x), keysLeft - margin, keysRight - margin);
            if (pressedStep < 0)
                return;
            editor.selectedLane = l;
            if (e.mods.isPopupMenu())
            {
                editor.selectedStep = pressedStep;
                pressedStep = -1;
                editor.refresh();
                return;
            }
            pressPattern = editor.editPattern;
            pressB = editor.editB;
            const auto& steps = editor.editB ? editor.laneShown(l).b : editor.laneShown(l).a;
            startChance = steps[static_cast<std::size_t>(pressedStep)].on ? steps[static_cast<std::size_t>(pressedStep)].chance : 1.0f;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (pressedStep < 0 || std::abs(e.getDistanceFromDragStartY()) < 4)
                return;
            if (! dragged)
                editor.processor.beginGesture();
            dragged = true;
            const float chance = juce::jlimit(0.05f, 1.0f, startChance - e.getDistanceFromDragStartY() / 40.0f);
            const int step = pressedStep;
            const bool b = pressB;
            editor.editLane(pressedLane, [&](SequencerLane& lane)
            {
                auto& s = (b ? lane.b : lane.a)[static_cast<std::size_t>(step)];
                s.on = true;
                s.chance = std::round(chance * 20.0f) / 20.0f;
            }, false, pressPattern);
            editor.selectedStep = step;
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            if (pressedStep >= 0)
            {
                if (dragged)
                    editor.processor.endGesture();
                else
                {
                    const int step = pressedStep;
                    const bool b = pressB;
                    bool nowOn = false;
                    editor.editLane(pressedLane, [&](SequencerLane& lane)
                    {
                        auto& s = (b ? lane.b : lane.a)[static_cast<std::size_t>(step)];
                        s.on = ! s.on;
                        if (s.on)
                        {
                            s.chance = 1.0f;
                            s.hits = 1;
                        }
                        nowOn = s.on;
                    }, true, pressPattern);
                    editor.selectedStep = nowOn ? step : -1;
                }
            }
            pressedStep = -1;
            editor.refresh();
        }

    private:
        [[nodiscard]] juce::Rectangle<float> muteBox(int l) const { return { 172.0f, l * laneHeight + 6.5f, 20.0f, 18.0f }; }
        [[nodiscard]] juce::Rectangle<float> soloBox(int l) const { return { 196.0f, l * laneHeight + 6.5f, 20.0f, 18.0f }; }

        static void paintToggle(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on)
        {
            const auto& th = theme();
            if (on)
            {
                g.setColour(th.ink);
                g.fillRoundedRectangle(r, 4.0f);
            }
            g.setColour(on ? th.ink : th.line);
            g.drawRoundedRectangle(r, 4.0f, 1.0f);
            g.setColour(on ? th.panel : th.dim);
            g.setFont(uiFont(9.5f, true));
            g.drawText(text, r, juce::Justification::centred);
        }

        Editor& editor;
        juce::OwnedArray<NumberBox> lengths;
        juce::OwnedArray<Knob> minis;
        std::vector<std::pair<int, int>> miniPlace; // lane, place in the lane
        int pressedLane = 0, pressedStep = -1, pressPattern = 0;
        bool pressB = false;
        bool dragged = false;
        float startChance = 1.0f;
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
        themeIndex = juce::jlimit(0, 3, settings->getIntValue("theme", 0));
        lanesOpen = settings->getBoolValue("lanesOpen", true);
        setTheme(themeIndex);

        content = std::make_unique<Content>(*this);
        addAndMakeVisible(*content);
        stepKeys = std::make_unique<StepKeys>(*this);
        content->addAndMakeVisible(*stepKeys);
        laneRows = std::make_unique<LaneRows>(*this);
        content->addAndMakeVisible(*laneRows);
        lid = std::make_unique<LidView>(p);
        content->addChildComponent(*lid);

        buildHeader();
        buildSound();
        buildTransport();
        buildInspector();

        setWantsKeyboardFocus(true);
        setResizable(true, true);
        setResizeLimits(620, 280, 2480, 2200);
        layoutForLanes();
        controls = std::make_unique<Controls>(*this);
        // Space and the shortcuts work from the start, before any click.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Editor>(this)] { if (safe != nullptr) safe->grabKeyboardFocus(); });
        startTimerHz(30);
    }

    Editor::~Editor()
    {
        stopAudition(); // the project as it was, before the state is kept
        controls.reset(); // its gesture closes while the panel is whole
        settings->setValue("theme", themeIndex);
        settings->setValue("lanesOpen", lanesOpen);
        settings->saveIfNeeded();
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
        refresh();
    }

    int Editor::playheadStep(int panelLane) const
    {
        if (! processor.isPlaying() || processor.playingPattern() != editPattern || processor.playingVariationB() != editB)
            return -1;
        const int length = juce::jlimit(1, 16, laneShown(panelLane).length);
        const auto k = processor.playingSixteenth();
        return static_cast<int>(((k % length) + length) % length);
    }

    void Editor::refresh() { content->repaint(); } // its children with it

    void Editor::layoutForLanes()
    {
        const int height = baseHeight(lanesOpen) + content->shift();
        content->setSize(baseWidth, height);
        laneRows->setVisible(lanesOpen);
        laneRows->setBounds(margin, content->lanesY(), baseWidth - 2 * margin, panelLanes * laneHeight);
        stepKeys->setBounds(keysLeft, content->transportY() + keysTop - transportTop, keysRight - keysLeft, keysHeight);
        lid->setVisible(lidView);
        lid->setBounds(margin, soundTop, baseWidth - 2 * margin, lidHeight - 14);
        // The widgets below the knobs move with the view and the lanes.
        for (auto* c : parts)
            if (const auto y = c->getProperties()["baseY"]; ! y.isVoid())
            {
                const int region = c->getProperties()["region"];
                const int offset = region == 0 ? 0 : region == 1 ? content->shift() : content->inspectorY() - (lanesTop + panelLanes * laneHeight + 8 + 6);
                c->setTopLeftPosition(c->getX(), static_cast<int>(y) + offset);
                if (static_cast<bool>(c->getProperties()["sound"]))
                    c->setVisible(! lidView);
            }
        // The window keeps the panel's proportions as it is resized: the height follows the width (the review of D267).
        if (auto* constrainer = getConstrainer())
            constrainer->setFixedAspectRatio(baseWidth / static_cast<double>(height));
        const double scale = getWidth() > 0 ? getWidth() / static_cast<double>(baseWidth) : 1.0;
        setSize(getWidth() > 0 ? getWidth() : baseWidth, juce::roundToInt(height * (getWidth() > 0 ? scale : 1.0)));
        resized();
        refresh();
    }

    void Editor::paint(juce::Graphics& g) { g.fillAll(theme().desk); }

    void Editor::resized()
    {
        const float scale = getWidth() / static_cast<float>(baseWidth);
        content->setTransform(juce::AffineTransform::scale(scale));
        // The overlays cover the window, whatever its size; in a window narrower than the panel they are laid out at
        // the panel's width and drawn smaller with it, so their columns keep their room (D273).
        for (juce::Component* overlay : { static_cast<juce::Component*>(library.get()), static_cast<juce::Component*>(controllerPanel.get()) })
        {
            if (overlay == nullptr)
                continue;
            if (scale < 1.0f)
            {
                overlay->setTransform(juce::AffineTransform::scale(scale));
                overlay->setBounds(0, 0, baseWidth, juce::roundToInt(static_cast<float>(getHeight()) / scale));
            }
            else
            {
                overlay->setTransform({});
                overlay->setBounds(getLocalBounds());
            }
        }
    }

    // ---- Building the panel -----------------------------------------------------------------------------------

    void Editor::buildHeader()
    {
        const auto place = [](juce::Component& c, int x, int y, int w, int h)
        {
            c.setBounds(x, y, w, h);
            c.getProperties().set("baseY", y);
            c.getProperties().set("region", 0);
        };
        int x = 520;
        const auto chip = [&](const juce::String& text, int w, std::function<void(const juce::MouseEvent&)> click, std::function<bool()> on = {})
        {
            auto& c = add<Chip>(*content);
            c.text = text;
            c.onClick = std::move(click);
            c.isOn = std::move(on);
            place(c, x, 22, w, 26);
            x += w + 6;
            return &c;
        };
        chip("File", 52, [this](const juce::MouseEvent&) { showFileMenu(); });
        chip("Library", 66, [this](const juce::MouseEvent&) { openLibrary(); });
        chip("Bank", 56, [this](const juce::MouseEvent&) { showBankMenu(); });
        chip("Theme", 60, [this](const juce::MouseEvent&) { showThemeMenu(); });
        chip("Undo", 52, [this](const juce::MouseEvent&) { processor.undo(); refresh(); });
        chip("Redo", 52, [this](const juce::MouseEvent&) { processor.redo(); refresh(); });
        chip("Controller", 84, [this](const juce::MouseEvent&) { openControllerSetup(); },
             [] { return ! coolsynth::midi::ControllerSetup::instance().map().empty(); });

        auto& views = add<Choices>(*content);
        views.options = { "Panel", "Under the Lid" };
        views.get = [this] { return lidView ? 1 : 0; };
        views.set = [this](int v) { lidView = v == 1; layoutForLanes(); };
        place(views, baseWidth - margin - 210, 22, 200, 26);
    }

    void Editor::buildSound()
    {
        const float colW = static_cast<float>(baseWidth - 2 * margin) / panelLanes;
        std::array<int, panelLanes> rows {};
        for (int k = 0; k < knobCount; ++k)
        {
            const auto& spec = knobs[static_cast<std::size_t>(k)];
            const int col = spec.lane + 1;
            auto& knob = add<Knob>(*content);
            knob.label = spec.label;
            knob.defaultValue = knobDefault(k);
            knob.get = [this, k] { return processor.project().kit.knob[static_cast<std::size_t>(k)]; };
            knob.set = [this, k](float v) { processor.editKit([k, v](KitValues& kit) { kit.knob[static_cast<std::size_t>(k)] = v; }); };
            knob.gestureStart = [this] { processor.beginGesture(); };
            knob.gestureEnd = [this] { processor.endGesture(); };
            const int row = rows[static_cast<std::size_t>(col)]++;
            const int y = soundTop + 34 + row * 66;
            knob.setBounds(juce::roundToInt(margin + col * colW + colW / 2 - 30), y, 60, 60);
            knob.getProperties().set("baseY", y);
            knob.getProperties().set("region", 0);
            knob.getProperties().set("sound", true);
        }
        {
            // The master volume, under the Accent as on the 808 (the review of D267: a controller's knob 8 turned
            // it with nothing on the panel to show it).
            auto& volume = add<Knob>(*content);
            volume.label = "Volume";
            volume.defaultValue = volumePosition(0.0f);
            volume.get = [this] { return volumePosition(processor.project().kit.volumeDb); };
            volume.set = [this](float v) { processor.editKit([v](KitValues& kit) { kit.volumeDb = volumeDbAt(v); }); };
            volume.gestureStart = [this] { processor.beginGesture(); };
            volume.gestureEnd = [this] { processor.endGesture(); };
            const int y = soundTop + 34 + rows[0]++ * 66;
            volume.setBounds(juce::roundToInt(margin + colW / 2 - 30), y, 60, 60);
            volume.getProperties().set("baseY", y);
            volume.getProperties().set("region", 0);
            volume.getProperties().set("sound", true);
        }
        for (int c = 0; c < panelLanes; ++c)
        {
            const int x = juce::roundToInt(margin + c * colW);
            if (c > 0 && instruments[static_cast<std::size_t>(c - 1)].altCode != nullptr)
            {
                auto& pair = add<Choices>(*content);
                const auto& inst = instruments[static_cast<std::size_t>(c - 1)];
                pair.options = { inst.code, inst.altCode };
                pair.fontHeight = 10.0f;
                pair.get = [this, c] { return processor.project().kit.alt[static_cast<std::size_t>(c - 1)] ? 1 : 0; };
                pair.set = [this, c](int v) { processor.editKit([c, v](KitValues& kit) { kit.alt[static_cast<std::size_t>(c - 1)] = v == 1; }); refresh(); };
                const int y = soundTop + 34 + rows[static_cast<std::size_t>(c)] * 66 + 4;
                pair.setBounds(x + juce::roundToInt(colW / 2) - 30, y, 60, 22);
                pair.getProperties().set("baseY", y);
                pair.getProperties().set("region", 0);
                pair.getProperties().set("sound", true);
            }
            auto& key = add<NameKey>(*content);
            key.editor = this;
            key.panelLane = c;
            const int y = soundTop + soundHeight - 44;
            key.setBounds(x + 6, y, juce::roundToInt(colW) - 12, 38);
            key.getProperties().set("baseY", y);
            key.getProperties().set("region", 0);
            key.getProperties().set("sound", true);
        }
    }

    void Editor::buildTransport()
    {
        const auto place = [](juce::Component& c, int x, int y, int w, int h)
        {
            c.setBounds(x, y, w, h);
            c.getProperties().set("baseY", y);
            c.getProperties().set("region", 1);
        };
        auto& play = add<Chip>(*content);
        play.text = "PLAY";
        play.fontHeight = 12.0f;
        play.isOn = [this] { return processor.isPlaying(); };
        play.onClick = [this](const juce::MouseEvent&) { processor.setPlaying(! processor.isPlaying()); };
        place(play, margin, keysTop, 58, keysHeight);

        auto& tempo = add<NumberBox>(*content);
        tempo.label = "Tempo";
        tempo.minimum = 30.0;
        tempo.maximum = 300.0;
        tempo.step = 0.5;
        tempo.pixelsPerStep = 2.0;
        tempo.defaultValue = 120.0;
        tempo.text = [this](double v) { return juce::String(processor.followingClock() ? processor.tempoNow() : v, 1); };
        tempo.get = [this] { return processor.project().play.tempo; };
        // Following a MIDI clock, the clock sets the tempo: the box shows it and takes no edits.
        tempo.set = [this](double v)
        {
            if (! processor.followingClock())
                processor.editPlay([v](PlaySettings& p) { p.tempo = v; });
        };
        tempo.gestureStart = [this] { processor.beginGesture(); };
        tempo.gestureEnd = [this] { processor.endGesture(); };
        place(tempo, 84, keysTop + 6, 70, 46);

        auto& swing = add<Knob>(*content);
        swing.label = "Swing";
        swing.defaultValue = 0.0f;
        swing.get = [this] { return (processor.project().play.swing - 0.5f) / 0.25f; };
        swing.set = [this](float v) { processor.editPlay([v](PlaySettings& p) { p.swing = 0.5f + 0.25f * v; }); };
        swing.gestureStart = [this] { processor.beginGesture(); };
        swing.gestureEnd = [this] { processor.endGesture(); };
        place(swing, 158, keysTop - 2, 58, 60);

        auto& editAB = add<Choices>(*content);
        editAB.options = { "Edit A", "Edit B" };
        editAB.fontHeight = 10.0f;
        editAB.get = [this] { return editB ? 1 : 0; };
        editAB.set = [this](int v) { editB = v == 1; refresh(); };
        place(editAB, 222, keysTop + 32, 88, 22);

        const int right = keysRight + 16;
        auto& variation = add<Choices>(*content);
        variation.options = { "A", "AB", "B" };
        variation.get = [this]
        {
            const auto v = processor.project().play.variation;
            return v == SequencerVariation::a ? 0 : v == SequencerVariation::ab ? 1 : 2;
        };
        variation.set = [this](int v)
        {
            processor.editPlay([v](PlaySettings& p) { p.variation = v == 0 ? SequencerVariation::a : v == 1 ? SequencerVariation::ab : SequencerVariation::b; });
            if (v != 1)
                editB = v == 2;
            refresh();
        };
        place(variation, right, keysTop, 78, 26);

        auto& fill = add<Chip>(*content);
        fill.text = "Fill";
        fill.onClick = [this](const juce::MouseEvent& e)
        {
            if (e.mods.isShiftDown() || ! processor.isPlaying())
            {
                // Shift-click, or while stopped: edit the fill; again, back to the pattern edited before it.
                if (editPattern == -1)
                    editPattern = patternBeforeFill;
                else
                {
                    patternBeforeFill = editPattern;
                    editPattern = -1;
                }
                patternMode = false;
                refresh();
            }
            else
                processor.requestFill();
        };
        fill.isOn = [this] { return editPattern == -1; };
        place(fill, right + 84, keysTop, 36, 26);

        auto& rec = add<Chip>(*content);
        rec.text = "Rec";
        rec.isOn = [this] { return processor.isRecording(); };
        rec.onClick = [this](const juce::MouseEvent&) { processor.setRecording(! processor.isRecording()); };
        place(rec, right + 124, keysTop, 34, 26);

        auto& patternKey = add<Chip>(*content);
        patternKey.filledWhenOn = false;
        patternKey.isOn = [this] { return patternMode; };
        patternKey.onClick = [this](const juce::MouseEvent&) { patternMode = ! patternMode; firstChainKey = -1; refresh(); };
        patternKey.text = "Pattern";
        place(patternKey, right, keysTop + 30, 78, 26);

        auto& drag = add<Chip>(*content);
        drag.text = "Drag MIDI";
        drag.dashed = true;
        drag.fontHeight = 10.0f;
        drag.onClick = [](const juce::MouseEvent&) {};
        drag.addMouseListener(this, false);
        drag.getProperties().set("midiDrag", true);
        place(drag, right + 84, keysTop + 30, 74, 26);
    }

    void Editor::buildInspector()
    {
        const int base = lanesTop + panelLanes * laneHeight + 8 + 6;
        const auto place = [base](juce::Component& c, int x, int dy, int w, int h)
        {
            c.setBounds(x, base + dy, w, h);
            c.getProperties().set("baseY", base + dy);
            c.getProperties().set("region", 2);
        };
        auto& chance = add<NumberBox>(*content);
        chance.label = "Chance";
        chance.labelBelow = false;
        chance.valueHeight = 13.0f;
        chance.minimum = 5;
        chance.maximum = 100;
        chance.step = 5;
        chance.defaultValue = 100;
        chance.text = [](double v) { return juce::String(juce::roundToInt(v)) + " %"; };
        chance.get = [this]
        {
            if (selectedStep < 0)
                return 100.0;
            const auto& l = laneShown(selectedLane);
            return static_cast<double>((editB ? l.b : l.a)[static_cast<std::size_t>(selectedStep)].chance) * 100.0;
        };
        chance.set = [this](double v)
        {
            if (selectedStep < 0)
                return;
            const int step = selectedStep;
            const bool b = editB;
            editLane(selectedLane, [&](SequencerLane& l) { auto& s = (b ? l.b : l.a)[static_cast<std::size_t>(step)]; s.on = true; s.chance = static_cast<float>(v / 100.0); }, false);
        };
        chance.gestureStart = [this] { processor.beginGesture(); };
        chance.gestureEnd = [this] { processor.endGesture(); };
        place(chance, 220, 20, 120, 24);

        auto& hits = add<Choices>(*content);
        hits.options = { "1 hit", "2", "3" };
        hits.fontHeight = 10.0f;
        hits.get = [this]
        {
            if (selectedStep < 0)
                return -1;
            const auto& l = laneShown(selectedLane);
            return static_cast<int>((editB ? l.b : l.a)[static_cast<std::size_t>(selectedStep)].hits) - 1;
        };
        hits.set = [this](int v)
        {
            if (selectedStep < 0)
                return;
            const int step = selectedStep;
            const bool b = editB;
            editLane(selectedLane, [&](SequencerLane& l) { auto& s = (b ? l.b : l.a)[static_cast<std::size_t>(step)]; s.on = true; s.hits = static_cast<std::uint8_t>(v + 1); });
        };
        place(hits, 356, 20, 110, 24);

        int x = 760;
        const auto op = [&](const juce::String& text, std::function<void()> act)
        {
            auto& c = add<Chip>(*content);
            c.text = text;
            c.fontHeight = 10.5f;
            c.onClick = [act](const juce::MouseEvent&) { act(); };
            place(c, x, 20, 62, 24);
            x += 68;
        };
        op("Copy", [this] { clipboard = patternShown(); });
        op("Paste", [this]
        {
            if (! clipboard)
                return;
            const int pattern = editPattern;
            const auto copy = *clipboard;
            processor.editBank([&](SequencerBank& bank) { (pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)]) = copy; });
            refresh();
        });
        op("Clear", [this]
        {
            const int pattern = editPattern;
            processor.editBank([&](SequencerBank& bank) { (pattern < 0 ? bank.fill : bank.pattern[static_cast<std::size_t>(pattern)]) = SequencerPattern {}; });
            refresh();
        });
        op("Keep", [this] { savePatternToLibrary(); });

        auto& info = add<juce::Label>(*content);
        info.setInterceptsMouseClicks(false, false);
        info.getProperties().set("info", true);
        place(info, margin, 6, 200, 48);
    }

    // ---- Patterns and chains ----------------------------------------------------------------------------------

    void Editor::selectPatternKey(int index, bool extend)
    {
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
            processor.setChain(chain);
        }
        else
        {
            firstChainKey = index;
            processor.setChain({ index });
        }
        editPattern = index;
        refresh();
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
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this](int r)
        {
            if (r == 1) newProject();
            else if (r == 2) openProject();
            else if (r == 3) saveProject(false);
            else if (r == 4) saveProject(true);
            else if (r == 5) savePatternToLibrary();
        });
    }

    void Editor::showThemeMenu()
    {
        juce::PopupMenu m;
        for (int i = 0; i < 4; ++i)
            m.addItem(i + 1, themes()[static_cast<std::size_t>(i)].name, true, i == themeIndex);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this](int r)
        {
            if (r <= 0)
                return;
            themeIndex = r - 1;
            setTheme(themeIndex);
            settings->setValue("theme", themeIndex);
            repaint();
            refresh();
        });
    }

    void Editor::showBankMenu()
    {
        juce::PopupMenu m;
        const auto& p = processor.project();
        for (int i = 0; i < static_cast<int>(p.banks.size()); ++i)
            m.addItem(i + 1, "Bank " + juce::String(i + 1), true, i == p.bank);
        m.addSeparator();
        m.addItem(1000, "New bank");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this](int r)
        {
            if (r <= 0)
                return;
            if (r == 1000)
                processor.addBank();
            else
                processor.selectBank(r - 1);
            refresh();
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

    void Editor::savePatternToLibrary()
    {
        auto* window = new juce::AlertWindow("Keep this pattern", "A name for it in the library:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor("name", "My pattern");
        window->addButton("Keep", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        window->enterModalState(true, juce::ModalCallbackFunction::create([this, window](int r)
        {
            if (r == 1)
            {
                PatternFile f;
                f.name = window->getTextEditorContents("name");
                f.pattern = patternShown();
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
        patternMode = false;
        refresh();
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

    // ---- Keys and time ----------------------------------------------------------------------------------------

    bool Editor::keyPressed(const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
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
        if (processor.applyRecordedHits())
            refresh();
        const auto bits = processor.takeStruckLanes();
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (bits != 0)
        {
            for (int l = 0; l < laneCount; ++l)
                if ((bits >> l) & 1u)
                    ledUntil[static_cast<std::size_t>(l + 1)] = now + 90.0;
            if ((bits >> laneCount) & 1u)
                ledUntil[0] = now + 90.0;
            lid->struck(bits);
        }
        lid->tick(now);
        // The step inspector's text.
        for (auto* c : parts)
            if (static_cast<bool>(c->getProperties()["info"]))
                if (auto* label = dynamic_cast<juce::Label*>(c))
                {
                    const auto& kit = processor.project().kit;
                    juce::String text;
                    if (selectedStep >= 0)
                        text << "Step " << (selectedStep + 1) << "\n" << laneName(selectedLane, kit);
                    else
                        text << "No step selected\nRight-click a step";
                    text << "\n" << (editPattern < 0 ? juce::String("Fill") : "Pattern " + juce::String(editPattern + 1).paddedLeft('0', 2))
                         << (editB ? " B" : " A") << ", bank " << (processor.project().bank + 1);
                    label->setColour(juce::Label::textColourId, theme().dim);
                    label->setFont(uiFont(11.0f));
                    if (label->getText() != text)
                        label->setText(text, juce::dontSendNotification);
                }
        // The panel is drawn again only when something it shows changed, or a light is lit or just went out (the
        // review of D267: it was drawn whole 30 times a second).
        std::uint64_t look = processor.editCount();
        const auto fold = [&look](std::int64_t v) { look = look * 1000003u ^ static_cast<std::uint64_t>(v); };
        fold(processor.isPlaying() ? processor.playingSixteenth() : -1);
        fold(processor.playingPattern());
        fold(processor.playingVariationB());
        fold(processor.isRecording());
        fold(processor.followingClock() ? juce::roundToInt(processor.tempoNow() * 10.0) : -1);
        fold(static_cast<std::int64_t>(processor.chain().size()));
        fold(processor.chain().front());
        fold(selectedLane);
        fold(editPattern);
        fold(editB);
        fold(selectedStep);
        fold(patternMode);
        fold(firstChainKey);
        for (const auto until : ledUntil)
            fold(until > now ? 1 : 0);
        if (look != lastLook)
        {
            lastLook = look;
            refresh();
        }
    }

    void Editor::mouseDrag(const juce::MouseEvent& e)
    {
        if (static_cast<bool>(e.eventComponent->getProperties()["midiDrag"]) && e.getDistanceFromDragStart() > 6 && ! midiDragging)
            startMidiDrag();
    }
}
