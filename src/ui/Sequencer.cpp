#include "Sequencer.h"

#include "Editor.h"
#include "Text.h"

namespace cs808
{
    namespace
    {
        juce::Colour rgb(std::uint32_t c, float a = 1.0f) { return juce::Colour(0xff000000u | c).withAlpha(a); }

        // The words on the glass, as the mockup prints them (D292).
        const juce::Colour labelInk = rgb(0xddd5c3);  // labels, lane names, lengths, the status
        const juce::Colour headInk = rgb(0xf4efe4);   // a section's heading, a lit word
        const juce::Colour keyInk = rgb(0xf1ebde);    // a key's word
        const juce::Colour emptyInk = rgb(0xb9b19f);  // an empty slot's number, the hint
        const juce::Colour backlitInk = rgb(0x2a1408);
        const juce::Colour ink = rgb(0xece4d2);
        const juce::Colour dim = rgb(0xa39b88);
        constexpr int panelLanes = laneCount + 1;

        bool hasContent(const SequencerPattern& p)
        {
            const auto any = [](const SequencerLane& lane)
            {
                for (int i = 0; i < SequencerLane::steps; ++i)
                    if (lane.a[static_cast<std::size_t>(i)].on || lane.b[static_cast<std::size_t>(i)].on)
                        return true;
                return false;
            };
            if (any(p.accent))
                return true;
            for (int l = 0; l < laneCount; ++l)
                if (any(p.lanes[static_cast<std::size_t>(l)]))
                    return true;
            return false;
        }

        void word(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour, const juce::Font& font,
                  juce::Justification just = juce::Justification::centredLeft)
        {
            g.setColour(colour);
            g.setFont(font);
            shapedText(g, text, area, just, false);
        }

        /** A slot as the patterns row draws it: a key, backlit while it plays, ringed while edited (D292). */
        void paintSlot(juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, bool has, bool edited, bool playing,
                       bool queuedLit, float progress)
        {
            if (playing)
            {
                drawKeyCap(g, box, Cap::backlit, false, false, 3.0f);
                // how far through the bar it is, a pale bar along its bottom
                g.setColour(rgb(0xfff4e4, 0.9f));
                g.fillRoundedRectangle(box.reduced(1.0f).withTop(box.getBottom() - 4.0f).withWidth((box.getWidth() - 2.0f) * juce::jlimit(0.0f, 1.0f, progress)), 1.5f);
            }
            else
            {
                // Picked and waiting for its bar line: lit at once, a steady orange cap, so the press shows at once (D303).
                if (queuedLit)
                    drawKeyCap(g, box, Cap::orange, false, false, 3.0f);
                else
                    drawSlotKey(g, box, has);
            }
            if (edited)
            {
                g.setColour(ink);
                g.drawRoundedRectangle(box.expanded(2.0f), 5.0f, 2.0f);
            }
            word(g, text, box, playing || queuedLit ? backlitInk : has ? keyInk : emptyInk, monoFont(11.0f), juce::Justification::centred);
        }

        /** The lane's Level knob, the Accent's for the accent lane: what its level bar shows. */
        int levelKnob(int panelLane)
        {
            if (panelLane == 0)
                return 0;
            for (int k = 0; k < knobCount; ++k)
                if (knobs[static_cast<std::size_t>(k)].lane == panelLane - 1 && juce::String(knobs[static_cast<std::size_t>(k)].label) == "Level")
                    return k;
            return -1;
        }

        void setStep(coolsynth::synth::SequencerStep& s, bool on)
        {
            s.on = on;
            s.chance = 1.0f;
            s.hits = 1;
        }

        // The lanes' columns, from the lane grid's left edge (the mockup's grid: 30 84 54 40 1fr 26, 8 apart).
        constexpr float idX = 8.0f, nameX = 38.0f, muteX = 130.0f, soloX = 158.5f, msWidth = 25.5f, levelX = 192.0f, levelWidth = 40.0f;
        constexpr float stepsLeft = 240.0f, stepsRight = 922.0f, lengthRight = 956.0f;
        // The patterns and chain rows reach this far past their keys, for the rings and shadows round them.
        constexpr float rowMargin = 6.0f;
        // The grid begins 10 px above the step lights, for the lit one's glow.
        constexpr float lampTop = 10.0f, lampsHeight = 14.0f, laneTop = lampTop + 17.0f, laneHeight = 24.0f, lanePitch = 27.0f;

        float stepTrack() { return (stepsRight - stepsLeft - 15.0f * 4.0f) / 16.0f; }

        /** Step i's key: sixteen tracks 4 px apart, the first of each group of four 3 px in. */
        juce::Rectangle<float> stepBox(int i, float y, float h)
        {
            const float track = stepTrack();
            float x = stepsLeft + static_cast<float>(i) * (track + 4.0f), w = track;
            if (i % 4 == 0)
            {
                x += 3.0f;
                w -= 3.0f;
            }
            return { x, y, w, h };
        }

        int stepAtX(float x)
        {
            const float track = stepTrack();
            for (int i = 0; i < 16; ++i)
            {
                const float left = stepsLeft + static_cast<float>(i) * (track + 4.0f);
                if (x >= left - 2.0f && x < left + track + 2.0f)
                    return i;
            }
            return -1;
        }
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

    juce::String patternLabel(int pattern)
    {
        return pattern < 0 ? juce::String("fill") : juce::String(pattern + 1).paddedLeft('0', 2);
    }

    // =============================================================================================================
    // The patterns: the banks, sixteen slots, the fill and the variation

    class SequencerGlass::PatternRow final : public juce::Component
    {
    public:
        explicit PatternRow(Editor& e) : editor(e)
        {
            fillKey.text = "Fill";
            fillKey.fontHeight = 10.0f;
            fillKey.filledWhenOn = false;
            fillKey.isOn = [this] { return editor.fillQueued(); };
            fillKey.onClick = [this](const juce::MouseEvent&) { editor.queueFill(); };
            addAndMakeVisible(fillKey);

            variation.options = { "A", "AB", "B" };
            variation.fontHeight = 10.0f;
            variation.get = [this]
            {
                const auto v = editor.processor.project().play.variation;
                return v == SequencerVariation::a ? 0 : v == SequencerVariation::ab ? 1 : 2;
            };
            variation.set = [this](int v)
            {
                auto& p = editor.processor;
                // A second press of AB swaps the half edited, A or B: the lanes show it and the status names it.
                if (v == 1 && p.project().play.variation == SequencerVariation::ab)
                    editor.editB = ! editor.editB;
                p.editPlay([v](PlaySettings& s) { s.variation = v == 0 ? SequencerVariation::a : v == 1 ? SequencerVariation::ab : SequencerVariation::b; });
                if (v != 1)
                    editor.editB = v == 2;
                editor.refresh();
            };
            addAndMakeVisible(variation);
        }

        static juce::Rectangle<float> bankBox(int i) { return { 66.0f + static_cast<float>(i) * 30.0f, rowMargin, 26.0f, 30.0f }; }
        static juce::Rectangle<float> slotBox(int i) { return { 190.0f + static_cast<float>(i) * 42.0f + static_cast<float>(i / 4) * 5.0f, rowMargin, 38.0f, 30.0f }; }
        static juce::Rectangle<float> fillBox() { return { 885.0f, rowMargin, 38.0f, 30.0f }; }

        void resized() override
        {
            const int fillWidth = juce::roundToInt(shapedWidth(labelFont(10.0f), "FILL") + 34.0f);
            fillKey.setBounds(935, 2 + static_cast<int>(rowMargin), fillWidth, 26);
            variation.setBounds(getWidth() - 128, 2 + static_cast<int>(rowMargin), 128, 26);
        }

        void paint(juce::Graphics& g) override
        {
            const auto& p = editor.processor;
            const auto& project = p.project();
            word(g, "PATTERNS", { 0.0f, rowMargin, 60.0f, 30.0f }, labelInk, labelFont(10.0f));
            // The banks: those there are lighter, the one in use ringed. A fifth and more are in the File menu.
            for (int i = 0; i < 4; ++i)
            {
                const auto box = bankBox(i);
                const bool exists = i < static_cast<int>(project.banks.size());
                drawSlotKey(g, box, exists);
                if (i == project.bank)
                {
                    g.setColour(ink);
                    g.drawRoundedRectangle(box.expanded(2.0f), 5.0f, 2.0f);
                }
                word(g, juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)), box, exists ? keyInk : emptyInk, monoFont(11.0f), juce::Justification::centred);
            }
            const bool playing = p.isPlaying();
            const int heard = playing ? p.playingPattern() : -2;
            const int step = editor.stepInBar();
            const float progress = static_cast<float>(step + 1) / 16.0f;
            const int queued = editor.queuedPattern();
            const auto& bank = project.currentBank();
            for (int i = 0; i < SequencerBank::patterns; ++i)
                paintSlot(g, slotBox(i), patternLabel(i), hasContent(bank.pattern[static_cast<std::size_t>(i)]), editor.editPattern == i,
                          heard == i, queued == i, progress);
            paintSlot(g, fillBox(), "F", hasContent(bank.fill), editor.editPattern == -1, heard == -1, editor.fillQueued(), progress);
        }

        // A key acts as it goes down, as a hardware key does: no wait for the release (D303).
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                for (int i = 0; i < SequencerBank::patterns; ++i)
                    if (slotBox(i).contains(e.position))
                        showSlotMenu(i);
                return;
            }
            for (int i = 0; i < 4; ++i)
                if (bankBox(i).contains(e.position))
                    editor.selectBankTab(i);
            for (int i = 0; i < SequencerBank::patterns; ++i)
                if (slotBox(i).contains(e.position))
                    editor.selectPatternKey(i, e.mods.isShiftDown());
            if (fillBox().contains(e.position))
                editor.editFill();
        }

    private:
        void showSlotMenu(int i)
        {
            juce::PopupMenu m;
            m.addSectionHeader("Pattern " + patternLabel(i));
            m.addItem(1, "Edit without playing it");
            m.addItem(2, "Copy");
            m.addItem(3, "Paste", editor.clipboard.has_value());
            m.addItem(4, "Clear");
            m.addItem(5, "Save to the library...");
            m.addItem(6, "Add to the chain");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                            [safe = juce::Component::SafePointer<Editor>(&editor), i](int r)
            {
                if (safe == nullptr)
                    return;
                switch (r)
                {
                    case 1: safe->editPattern = i; safe->refresh(); break;
                    case 2: safe->copyPattern(i); break;
                    case 3: safe->pastePattern(i); break;
                    case 4: safe->clearPattern(i); break;
                    case 5: safe->savePatternToLibrary(i); break;
                    case 6: safe->addToChain(i); break;
                    default: break;
                }
            });
        }

        Editor& editor;
        Chip fillKey;
        Choices variation;
    };

    // =============================================================================================================
    // The chain: the patterns in the order they play, a block for each run of one pattern; where it plays

    class SequencerGlass::ChainRow final : public juce::Component
    {
    public:
        explicit ChainRow(Editor& e) : editor(e) {}

        struct Block
        {
            int first = 0, count = 1, pattern = 0;
            juce::Rectangle<float> box;
        };

        void paint(juce::Graphics& g) override
        {
            const auto& p = editor.processor;
            word(g, "CHAIN", { 0.0f, rowMargin, 60.0f, 26.0f }, labelInk, labelFont(10.0f));
            layoutBlocks();
            const bool playing = p.isPlaying();
            const int entry = playing ? editor.chainEntryPlaying() : -1;
            const int step = std::max(0, editor.stepInBar());
            int current = -1;
            for (int b = 0; b < static_cast<int>(blocks.size()); ++b)
                if (entry >= blocks[static_cast<std::size_t>(b)].first && entry < blocks[static_cast<std::size_t>(b)].first + blocks[static_cast<std::size_t>(b)].count)
                    current = b;
            // The block after the one playing is outlined in its last bar: it comes next.
            int next = -1;
            if (current >= 0 && blocks.size() > 1)
            {
                const auto& c = blocks[static_cast<std::size_t>(current)];
                if (entry == c.first + c.count - 1)
                    next = (current + 1) % static_cast<int>(blocks.size());
            }
            const auto mono = monoFont(11.0f);
            for (int b = 0; b < static_cast<int>(blocks.size()); ++b)
            {
                const auto& block = blocks[static_cast<std::size_t>(b)];
                const bool cur = b == current;
                if (cur)
                {
                    drawKeyCap(g, block.box, Cap::backlit, false, false, 3.0f);
                    const float done = static_cast<float>((entry - block.first) * 16 + step + 1) / static_cast<float>(block.count * 16);
                    g.setColour(rgb(0xfff4e4, 0.28f));
                    g.fillRoundedRectangle(block.box.reduced(1.0f).withWidth((block.box.getWidth() - 2.0f) * juce::jlimit(0.0f, 1.0f, done)), 2.0f);
                }
                else
                    drawSlotKey(g, block.box, true);
                if (b == next)
                {
                    g.setColour(theme().lit);
                    g.drawRoundedRectangle(block.box.expanded(1.5f), 4.5f, 1.0f);
                }
                const auto number = patternLabel(block.pattern);
                const float nw = shapedWidth(mono, number);
                word(g, number, block.box.withTrimmedLeft(8.0f).withWidth(nw + 2.0f), cur ? backlitInk : keyInk, mono);
                if (block.count > 1)
                    word(g, juce::String(juce::CharPointer_UTF8("\xc3\x97")) + juce::String(block.count), block.box.withTrimmedLeft(8.0f + nw + 4.0f),
                         cur ? rgb(0x3a2010) : dim, mono);
            }
            g.setColour(rgb(0x4a463f));
            g.drawRoundedRectangle(addBox.reduced(0.5f), 3.0f, 1.0f);
            word(g, "+", addBox, dim, mono, juce::Justification::centred);
            // Loop: the chain round and round, or once through and then the transport stops (D297). Change: a
            // pattern picked while playing comes in at the next bar line, or at the end of the pattern that plays.
            const auto& play = p.project().play;
            const auto key = [&](juce::Rectangle<float> box, const juce::String& text, bool on)
            {
                drawKeyCap(g, box, on ? Cap::selected : Cap::plain, false, false);
                const auto font = labelFont(10.5f);
                const float w = shapedWidth(font, text);
                const float x = box.getCentreX() - (w + 12.0f) * 0.5f;
                // the step lights' own lamp: their size, their glass, their glow, in orange (the user, D291)
                drawLens(g, { x + 3.0f, box.getCentreY() }, 5.0f, on ? Lens::orangeBright : Lens::off);
                drawKeyText(g, text, box.withTrimmedLeft(x + 12.0f - box.getX()).withWidth(w + 2.0f), on ? Cap::selected : Cap::plain, font);
            };
            key(loopBox, "LOOP", play.loop);
            word(g, "CHANGE", changeLabel, labelInk, labelFont(10.0f));
            key(barBox, "NEXT BAR", play.changeAtBar);
            key(endBox, "END OF PATTERN", ! play.changeAtBar);
            paintStatus(g);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                return;
            // Right-click: the block goes; the chain keeps one at least.
            for (const auto& b : blocks)
                if (b.box.contains(e.position) && blocks.size() > 1)
                {
                    auto list = editor.processor.chain();
                    list.erase(list.begin() + b.first, list.begin() + b.first + b.count);
                    editor.processor.setChain(list);
                    editor.refresh();
                    return;
                }
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || ! e.mouseWasClicked())
                return;
            if (addBox.contains(e.position))
            {
                editor.addToChain(editor.editPattern);
                return;
            }
            if (loopBox.contains(e.position) || barBox.contains(e.position) || endBox.contains(e.position))
            {
                const bool loop = loopBox.contains(e.position), bar = barBox.contains(e.position);
                editor.processor.editPlay([loop, bar](PlaySettings& s)
                {
                    if (loop)
                        s.loop = ! s.loop;
                    else
                        s.changeAtBar = bar;
                });
                editor.refresh();
                return;
            }
            // A click: more repeats, 1, 2, 4 and back to 1.
            for (const auto& b : blocks)
                if (b.box.contains(e.position))
                {
                    const int count = b.count < 2 ? 2 : b.count < 4 ? 4 : 1;
                    auto list = editor.processor.chain();
                    list.erase(list.begin() + b.first, list.begin() + b.first + b.count);
                    list.insert(list.begin() + b.first, static_cast<std::size_t>(count), b.pattern);
                    editor.processor.setChain(list);
                    editor.refresh();
                    return;
                }
        }

    private:
        void layoutBlocks()
        {
            blocks.clear();
            const auto list = editor.processor.chain();
            const auto mono = monoFont(11.0f);
            float x = 66.0f;
            for (int i = 0; i < static_cast<int>(list.size());)
            {
                Block b;
                b.first = i;
                b.pattern = list[static_cast<std::size_t>(i)];
                while (i < static_cast<int>(list.size()) && list[static_cast<std::size_t>(i)] == b.pattern)
                    ++i;
                b.count = i - b.first;
                float w = 16.0f + shapedWidth(mono, patternLabel(b.pattern));
                if (b.count > 1)
                    w += 4.0f + shapedWidth(mono, juce::String(juce::CharPointer_UTF8("\xc3\x97")) + juce::String(b.count));
                b.box = { x, rowMargin, std::max(44.0f, std::ceil(w)), 26.0f };
                x = b.box.getRight() + 3.0f;
                blocks.push_back(b);
            }
            addBox = { x, rowMargin, 26.0f, 26.0f };
            loopBox = { addBox.getRight() + 8.0f, rowMargin, 68.0f, 26.0f };
            const float right = static_cast<float>(getWidth());
            endBox = { right - 128.0f, rowMargin, 128.0f, 26.0f };
            barBox = { endBox.getX() - 92.0f, rowMargin, 88.0f, 26.0f };
            changeLabel = { barBox.getX() - 62.0f, rowMargin, 54.0f, 26.0f };
        }

        void paintStatus(juce::Graphics& g)
        {
            // "Editing 01 · hearing 03 · next: 05 in 7 steps", right-aligned: the numbers brighter, what comes next lit.
            struct Piece
            {
                juce::String text;
                juce::Colour colour;
                bool strong = false;
            };
            std::vector<Piece> pieces;
            const auto dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
            const auto& p = editor.processor;
            pieces.push_back({ "Editing ", labelInk });
            pieces.push_back({ patternLabel(editor.editPattern) + (editor.editB ? " B" : ""), ink, true });
            if (p.isPlaying())
            {
                const int heard = p.playingPattern();
                if (heard >= -1)
                {
                    pieces.push_back({ dot + "hearing ", labelInk });
                    pieces.push_back({ patternLabel(heard), ink, true });
                }
                const int queued = editor.queuedPattern();
                const juce::String next = editor.fillQueued() ? juce::String("fill") : queued >= 0 ? patternLabel(queued) : juce::String();
                const int steps = 15 - std::max(0, editor.stepInBar());
                if (next.isNotEmpty())
                {
                    pieces.push_back({ dot, labelInk });
                    pieces.push_back({ "next: " + next + " in " + juce::String(steps) + (steps == 1 ? " step" : " steps"), theme().lit });
                }
                else if (p.chain().size() > 1)
                    pieces.push_back({ dot + "chain", labelInk });
            }
            else
                pieces.push_back({ dot + "stopped", labelInk });
            float total = 0.0f;
            for (const auto& piece : pieces)
                total += shapedWidth(monoFont(11.0f, piece.strong), piece.text);
            float x = changeLabel.getX() - 14.0f - total; // right-aligned before the Change keys
            for (const auto& piece : pieces)
            {
                const auto font = monoFont(11.0f, piece.strong);
                const float w = shapedWidth(font, piece.text);
                word(g, piece.text, { x, rowMargin, w + 4.0f, 26.0f }, piece.colour, font);
                x += w;
            }
        }

        Editor& editor;
        std::vector<Block> blocks;
        juce::Rectangle<float> addBox, loopBox, changeLabel, barBox, endBox;
    };

    // =============================================================================================================
    // The step lights and the twelve lanes

    class SequencerGlass::LaneGrid final : public juce::Component
    {
    public:
        LaneGrid(Editor& e, SequencerGlass& owner) : editor(e), glass(owner) {}

        static juce::Rectangle<float> rowBox(int l, float width) { return { 0.0f, laneTop + static_cast<float>(l) * lanePitch, width, laneHeight }; }
        static juce::Rectangle<float> muteBox(int l) { return { muteX, laneTop + static_cast<float>(l) * lanePitch + 1.0f, msWidth, 22.0f }; }
        static juce::Rectangle<float> soloBox(int l) { return { soloX, laneTop + static_cast<float>(l) * lanePitch + 1.0f, msWidth, 22.0f }; }

        void paint(juce::Graphics& g) override
        {
            const auto& kit = editor.processor.project().kit;
            const auto& th = theme();
            // The step lights: one runs along the bar as it plays, as on the hardware.
            const int now = editor.stepInBar();
            for (int i = 0; i < 16; ++i)
            {
                const auto box = stepBox(i, lampTop, lampsHeight);
                const auto number = juce::String(i + 1);
                const auto font = monoFont(9.0f);
                const float nw = shapedWidth(font, number);
                const float x = box.getCentreX() - (5.0f + 4.0f + nw) * 0.5f;
                drawLens(g, { x + 2.5f, box.getCentreY() }, 5.0f, i == now ? Lens::redBright : Lens::off);
                word(g, number, { x + 9.0f, box.getY(), nw + 2.0f, lampsHeight }, i == now ? headInk : rgb(0x8f8877), font);
            }

            bool anySolo = false;
            for (int l = 1; l < panelLanes; ++l)
                anySolo = anySolo || editor.laneShown(l).solo;
            const float width = static_cast<float>(getWidth());
            for (int l = 0; l < panelLanes; ++l)
            {
                const auto row = rowBox(l, width);
                const auto& lane = editor.laneShown(l);
                const bool sel = l == editor.selectedLane;
                if (sel)
                {
                    // The selected lane: an orange edge and a warm shade fading to the right.
                    juce::ColourGradient shade(th.on.withAlpha(0.20f), row.getX(), 0.0f, ink.withAlpha(0.05f), row.getRight(), 0.0f, false);
                    shade.addColour(0.3, ink.withAlpha(0.07f));
                    g.setGradientFill(shade);
                    g.fillRoundedRectangle(row, 3.0f);
                    g.setColour(th.on);
                    g.fillRoundedRectangle(row.withWidth(3.0f), 1.0f);
                }
                word(g, laneCode(l, kit), row.withTrimmedLeft(idX), sel ? th.lit : ink, monoFont(11.0f, true));
                word(g, laneName(l, kit).toUpperCase(), row.withX(nameX).withWidth(muteX - nameX - 4.0f), sel ? th.lit : labelInk, labelFont(10.0f));
                // Mute and solo: the accent lane has neither, the sequencer ignores them there.
                if (l > 0)
                {
                    paintToggle(g, muteBox(l), "M", lane.mute, Lens::red);
                    paintToggle(g, soloBox(l), "S", lane.solo, Lens::green);
                }
                // The level bar: the lane's Level knob, the Accent's for the accent.
                if (const int k = levelKnob(l); k >= 0)
                {
                    const auto bar = juce::Rectangle<float>(levelX, row.getCentreY() - 3.0f, levelWidth, 6.0f);
                    g.setColour(rgb(0x1b1815));
                    g.fillRoundedRectangle(bar, 3.0f);
                    g.setColour(dim);
                    g.fillRoundedRectangle(bar.withWidth(levelWidth * editor.processor.project().kit.knob[static_cast<std::size_t>(k)]), 3.0f);
                }
                // The steps.
                const bool silent = l > 0 && (lane.mute || (anySolo && ! lane.solo));
                const auto& steps = editor.editB ? lane.b : lane.a;
                const int head = editor.playheadStep(l);
                const int length = juce::jlimit(1, 16, lane.length);
                for (int i = 0; i < 16; ++i)
                {
                    const auto box = stepBox(i, row.getY(), laneHeight);
                    const auto& s = steps[static_cast<std::size_t>(i)];
                    const float opacity = (i < length ? 1.0f : 0.38f) * (silent ? 0.5f : 1.0f);
                    if (s.on)
                    {
                        // An orange cap, lit while it sounds; a step that may not play is paler, a roll notched.
                        drawStepKeyOn(g, box, i == head, opacity * (0.4f + 0.6f * juce::jlimit(0.0f, 1.0f, s.chance)));
                        const int hits = juce::jlimit(1, 3, static_cast<int>(s.hits));
                        if (hits > 1)
                        {
                            g.setColour(rgb(0x3a1606, 0.75f));
                            for (int h = 0; h < hits; ++h)
                                g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre({ box.getCentreX() + (static_cast<float>(h) - (hits - 1) * 0.5f) * 6.0f, box.getBottom() - 6.0f }));
                        }
                    }
                    else
                        drawStepKey(g, box, th.group[static_cast<std::size_t>(i / 4)], opacity);
                }
                if (length < 16)
                {
                    // where the lane ends and loops, a lit edge
                    g.setColour(th.lit);
                    g.fillRect(stepBox(length - 1, row.getY(), laneHeight).getRight() + 1.0f, row.getY() + 1.0f, 2.0f, laneHeight - 2.0f);
                }
                word(g, juce::String(lane.length), row.withLeft(stepsRight).withRight(lengthRight), labelInk, monoFont(11.0f), juce::Justification::centredRight);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            pressedStep = -1;
            dragged = false;
            if (e.position.y < laneTop - 2.0f)
                return;
            const int l = juce::jlimit(0, panelLanes - 1, static_cast<int>((e.position.y - laneTop + 1.5f) / lanePitch));
            pressedLane = l;
            if (e.position.x < muteX)
            {
                // A lane's name: it is selected, and its drum struck.
                editor.selectedLane = l;
                if (l > 0)
                    editor.processor.strike(l - 1, e.mods.isShiftDown());
                editor.refresh();
                return;
            }
            if (l > 0 && muteBox(l).expanded(1.5f, 2.0f).contains(e.position))
            {
                editor.editLane(l, [](SequencerLane& lane) { lane.mute = ! lane.mute; });
                return;
            }
            if (l > 0 && soloBox(l).expanded(1.5f, 2.0f).contains(e.position))
            {
                editor.editLane(l, [](SequencerLane& lane) { lane.solo = ! lane.solo; });
                return;
            }
            const int step = e.position.x >= stepsLeft - 2.0f && e.position.x < stepsRight + 2.0f ? stepAtX(e.position.x) : -1;
            if (step < 0)
                return;
            editor.selectedLane = l;
            if (e.mods.isPopupMenu())
            {
                editor.refresh();
                glass.showStepPop(l, step, e.getEventRelativeTo(glass.getParentComponent()).position);
                return;
            }
            // What the press edits is fixed at the press: a controller may move the pattern while it is held.
            pressedStep = step;
            pressPattern = editor.editPattern;
            pressB = editor.editB;
            const auto& steps = pressB ? editor.laneShown(l).b : editor.laneShown(l).a;
            startChance = steps[static_cast<std::size_t>(step)].on ? steps[static_cast<std::size_t>(step)].chance : 1.0f;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            // Dragged down, a step plays less often: its chance, 5 % at a time.
            if (pressedStep < 0 || std::abs(e.getDistanceFromDragStartY()) < 4)
                return;
            if (! dragged)
                editor.processor.beginGesture();
            dragged = true;
            const float chance = juce::jlimit(0.05f, 1.0f, startChance - static_cast<float>(e.getDistanceFromDragStartY()) / 40.0f);
            const int step = pressedStep;
            const bool b = pressB;
            editor.editLane(pressedLane, [&](SequencerLane& lane)
            {
                auto& s = (b ? lane.b : lane.a)[static_cast<std::size_t>(step)];
                s.on = true;
                s.chance = std::round(chance * 20.0f) / 20.0f;
            }, false, pressPattern);
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
                    editor.editLane(pressedLane, [&](SequencerLane& lane)
                    {
                        auto& s = (b ? lane.b : lane.a)[static_cast<std::size_t>(step)];
                        setStep(s, ! s.on);
                    }, true, pressPattern);
                }
            }
            pressedStep = -1;
            editor.refresh();
        }

    private:
        static void paintToggle(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, Lens lens)
        {
            drawKeyCap(g, r, on ? Cap::selected : Cap::plain, false, false);
            const auto font = labelFont(10.0f);
            const float w = shapedWidth(font, text);
            const float x = r.getCentreX() - (w + 11.0f) * 0.5f;
            drawLens(g, { x + 3.0f, r.getCentreY() }, 6.0f, on ? lens : Lens::off);
            drawKeyText(g, text, { x + 11.0f, r.getY(), w + 2.0f, r.getHeight() }, on ? Cap::selected : Cap::plain, font);
        }

        Editor& editor;
        SequencerGlass& glass;
        int pressedLane = 0, pressedStep = -1, pressPattern = 0;
        bool pressB = false;
        bool dragged = false;
        float startChance = 1.0f;
    };

    // =============================================================================================================
    // The right side: the selected lane's tools and the pattern's

    class SequencerGlass::LanePanel final : public juce::Component
    {
    public:
        explicit LanePanel(Editor& e) : editor(e)
        {
            auto& p = editor.processor;
            length.diameter = 30.0f;
            length.label = "Length";
            length.labelColour = labelInk;
            length.defaultValue = 1.0f;
            length.get = [this] { return static_cast<float>(juce::jlimit(1, 16, editor.laneShown(editor.selectedLane).length) - 1) / 15.0f; };
            length.set = [this](float v)
            {
                const int n = juce::roundToInt(v * 15.0f) + 1;
                editor.editLane(editor.selectedLane, [n](SequencerLane& lane) { lane.length = n; }, false);
            };
            length.valueText = [this] { return juce::String(editor.laneShown(editor.selectedLane).length); };
            length.gestureStart = [&p] { p.beginGesture(); };
            length.gestureEnd = [this, &p] { p.endGesture(); editor.refresh(); };
            addAndMakeVisible(length);

            const auto every = [this](int n)
            {
                const bool b = editor.editB;
                editor.editLane(editor.selectedLane, [n, b](SequencerLane& lane)
                {
                    for (int i = 0; i < SequencerLane::steps; ++i)
                        setStep((b ? lane.b : lane.a)[static_cast<std::size_t>(i)], i % n == 0);
                });
            };
            const auto shift = [this](int by)
            {
                // The steps move round within the lane's length, so a short lane keeps its loop.
                const bool b = editor.editB;
                editor.editLane(editor.selectedLane, [by, b](SequencerLane& lane)
                {
                    auto& steps = b ? lane.b : lane.a;
                    const int n = juce::jlimit(1, SequencerLane::steps, lane.length);
                    auto copy = steps;
                    for (int i = 0; i < n; ++i)
                        steps[static_cast<std::size_t>((i + by + n) % n)] = copy[static_cast<std::size_t>(i)];
                });
            };
            key("Every 4", [every] { every(4); });
            key("Every 2", [every] { every(2); });
            key("Clear", [this]
            {
                const bool b = editor.editB;
                editor.editLane(editor.selectedLane, [b](SequencerLane& lane)
                {
                    for (auto& s : b ? lane.b : lane.a)
                        setStep(s, false);
                });
            });
            key(juce::String(juce::CharPointer_UTF8("\xe2\x97\x80")), [shift] { shift(-1); });
            key(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")), [shift] { shift(1); });
            key("Copy", [this] { editor.copyPattern(editor.editPattern); });
            key("Paste", [this] { editor.pastePattern(editor.editPattern); });
            key("Clear", [this] { editor.clearPattern(editor.editPattern); });
            key("Save to library", [this] { editor.savePatternToLibrary(editor.editPattern); });
            editor.makeMidiDragSource(*key("Drag MIDI", [] {}));
        }

        void resized() override
        {
            length.setBounds(12, 52, 40, 70);
            // the lane's keys, three across right of the knob; the pattern's, three across the width
            for (int i = 0; i < 5; ++i)
                keys[i]->setBounds(65 + (i % 3) * 59, 60 + (i / 3) * 32, 53, 26);
            for (int i = 0; i < 3; ++i)
                keys[5 + i]->setBounds(14 + i * 76, 159, 70, 26);
            keys[8]->setBounds(14, 191, 146, 26);
            keys[9]->setBounds(166, 191, 70, 26);
        }

        void paint(juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat();
            g.setColour(juce::Colours::black.withAlpha(0.25f));
            g.fillRoundedRectangle(area, 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
            word(g, "Right-click a step for its chance, roll and accent.", { 14.0f, 14.0f, 230.0f, 17.0f }, emptyInk, fontOf(Face::label, 11.0f, 0.02f));
            const auto dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
            word(g, ("Lane" + dot + laneName(editor.selectedLane, editor.processor.project().kit)).toUpperCase(), { 14.0f, 41.5f, 222.0f, 10.0f }, headInk, labelFont(10.0f));
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.fillRect(14.0f, 131.0f, 222.0f, 1.0f);
            word(g, ("Pattern " + patternLabel(editor.editPattern)).toUpperCase(), { 14.0f, 140.5f, 222.0f, 10.0f }, headInk, labelFont(10.0f));
        }

    private:
        Chip* key(const juce::String& text, std::function<void()> act)
        {
            auto* c = new Chip();
            c->text = text;
            c->fontHeight = 10.0f;
            c->onClick = [act = std::move(act)](const juce::MouseEvent&) { act(); };
            keys.add(c);
            addAndMakeVisible(c);
            return c;
        }

        Editor& editor;
        Knob length;
        juce::OwnedArray<Chip> keys;
    };

    // =============================================================================================================
    // A step's pop-up: its chance, its roll, its accent, and where the lane ends

    class SequencerGlass::Scrim final : public juce::Component
    {
    public:
        explicit Scrim(SequencerGlass& g) : glass(g) {}
        void mouseDown(const juce::MouseEvent&) override { glass.closeStepPop(); }

    private:
        SequencerGlass& glass;
    };

    class SequencerGlass::StepPop final : public juce::Component
    {
    public:
        static constexpr int width = 220, height = 198;

        StepPop(Editor& e, SequencerGlass& g, int panelLane, int stepIndex) : editor(e), glass(g), lane(panelLane), step(stepIndex)
        {
            const auto edit = [this](std::function<void(coolsynth::synth::SequencerStep&)> change)
            {
                const bool b = editor.editB;
                const int i = step;
                editor.editLane(lane, [&](SequencerLane& l) { change((b ? l.b : l.a)[static_cast<std::size_t>(i)]); });
            };
            const auto chanceBy = [edit](float by)
            {
                edit([by](coolsynth::synth::SequencerStep& s)
                {
                    s.chance = juce::jlimit(0.05f, 1.0f, std::round(((s.on ? s.chance : 1.0f) + by) * 20.0f) / 20.0f);
                    s.on = true;
                });
            };
            key(juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")), [chanceBy] { chanceBy(-0.05f); }).setBounds(60, 28, 26, 26);
            key("+", [chanceBy] { chanceBy(0.05f); }).setBounds(144, 28, 26, 26);

            roll.options = { "1 hit", "2", "3" };
            roll.fontHeight = 10.0f;
            roll.get = [this] { return current().on ? static_cast<int>(current().hits) - 1 : -1; };
            roll.set = [edit](int v) { edit([v](coolsynth::synth::SequencerStep& s) { s.on = true; s.hits = static_cast<std::uint8_t>(v + 1); }); };
            roll.setBounds(60, 62, 150, 26);
            addAndMakeVisible(roll);

            auto& accent = key("Accent", [this]
            {
                const bool b = editor.editB;
                const int i = step;
                editor.editLane(0, [&](SequencerLane& l)
                {
                    auto& s = (b ? l.b : l.a)[static_cast<std::size_t>(i)];
                    setStep(s, ! s.on);
                });
            });
            accent.filledWhenOn = false;
            accent.isOn = [this] { return (editor.editB ? editor.laneShown(0).b : editor.laneShown(0).a)[static_cast<std::size_t>(step)].on; };
            accent.setBounds(60, 96, 90, 26);

            auto& ends = key({}, [this]
            {
                const int n = step + 1;
                editor.editLane(lane, [n](SequencerLane& l) { l.length = n; });
                glass.closeStepPop();
            });
            ends.flat = true;
            ends.textOf = [this] { return "The lane ends here (" + juce::String(step + 1) + (step == 0 ? " step)" : " steps)"); };
            ends.setBounds(4, 130, width - 8, 26);
            auto& full = key("Full length (16)", [this]
            {
                editor.editLane(lane, [](SequencerLane& l) { l.length = SequencerLane::steps; });
                glass.closeStepPop();
            });
            full.flat = true;
            full.setBounds(4, 162, width - 8, 26);
        }

        void paint(juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat();
            juce::Path shape;
            shape.addRoundedRectangle(area, 5.0f);
            juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 30, { 0, 10 }).drawForPath(g, shape);
            g.setColour(rgb(0x1e1c19, 0.97f));
            g.fillPath(shape);
            g.setColour(rgb(0x4a463f));
            g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);
            const auto dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
            word(g, (laneCode(lane, editor.processor.project().kit) + dot + "step " + juce::String(step + 1)).toUpperCase(), { 10.0f, 10.0f, 200.0f, 10.0f }, labelInk, labelFont(10.0f));
            word(g, "CHANCE", { 10.0f, 28.0f, 50.0f, 26.0f }, labelInk, labelFont(10.0f));
            const auto& s = current();
            word(g, juce::String(juce::roundToInt((s.on ? s.chance : 1.0f) * 100.0f)) + " %", { 88.0f, 28.0f, 54.0f, 26.0f }, ink, monoFont(12.0f), juce::Justification::centred);
            word(g, "ROLL", { 10.0f, 62.0f, 50.0f, 26.0f }, labelInk, labelFont(10.0f));
            word(g, "ACCENT", { 10.0f, 96.0f, 50.0f, 26.0f }, labelInk, labelFont(10.0f));
        }

    private:
        [[nodiscard]] const coolsynth::synth::SequencerStep& current() const
        {
            const auto& l = editor.laneShown(lane);
            return (editor.editB ? l.b : l.a)[static_cast<std::size_t>(step)];
        }

        Chip& key(const juce::String& text, std::function<void()> act)
        {
            auto* c = new Chip();
            c->text = text;
            c->fontHeight = 10.0f;
            c->onClick = [act = std::move(act)](const juce::MouseEvent&) { act(); };
            keys.add(c);
            addAndMakeVisible(c);
            return *c;
        }

        Editor& editor;
        SequencerGlass& glass;
        int lane, step;
        Choices roll;
        juce::OwnedArray<Chip> keys;
    };

    // =============================================================================================================

    SequencerGlass::SequencerGlass(Editor& e) : editor(e)
    {
        patterns = std::make_unique<PatternRow>(e);
        chain = std::make_unique<ChainRow>(e);
        lanes = std::make_unique<LaneGrid>(e, *this);
        panel = std::make_unique<LanePanel>(e);
        for (juce::Component* c : { static_cast<juce::Component*>(patterns.get()), static_cast<juce::Component*>(chain.get()),
                                    static_cast<juce::Component*>(lanes.get()), static_cast<juce::Component*>(panel.get()) })
            addAndMakeVisible(c);
    }

    SequencerGlass::~SequencerGlass() = default;

    void SequencerGlass::resized()
    {
        // The mockup's pane: 12 px in at the top, 16 at the sides; rows 30 and 26 px, 9 apart; the lanes 4 px below.
        patterns->setBounds(16, 12 - 6, 1220, 30 + 12);
        chain->setBounds(16, 51 - 6, 1220, 26 + 12);
        lanes->setBounds(16, 80, 956, 362); // to the glass's edge, for the lit steps' glow
        panel->setBounds(986, 90, 250, 342);
    }

    void SequencerGlass::paint(juce::Graphics& g)
    {
        drawSmokedGlass(g, getLocalBounds().toFloat(), 6.0f, false);
        // The pane is held to the board by four polished pins at its corners (D303).
        const float w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight()), in = 9.0f;
        for (const auto c : { juce::Point<float>(in, in), { w - in, in }, { in, h - in }, { w - in, h - in } })
            drawStandoff(g, c, 9.0f);
    }

    void SequencerGlass::repaintPlaying()
    {
        patterns->repaint();
        chain->repaint();
        lanes->repaint();
    }

    void SequencerGlass::showStepPop(int panelLane, int step, juce::Point<float> where)
    {
        auto* board = getParentComponent();
        if (board == nullptr)
            return;
        closeStepPop();
        scrim = std::make_unique<Scrim>(*this);
        board->addAndMakeVisible(*scrim);
        scrim->setBounds(board->getLocalBounds());
        pop = std::make_unique<StepPop>(editor, *this, panelLane, step);
        board->addAndMakeVisible(*pop);
        // beside the pointer, turned back when it would leave the board
        float x = where.x + 6.0f, y = where.y + 6.0f;
        if (x + StepPop::width > board->getWidth() - 10.0f)
            x -= StepPop::width + 12.0f;
        if (y + StepPop::height > board->getHeight() - 10.0f)
            y -= StepPop::height + 12.0f;
        pop->setBounds(juce::roundToInt(x), juce::roundToInt(y), StepPop::width, StepPop::height);
    }

    void SequencerGlass::closeStepPop()
    {
        // Let go after the click that closed it has returned: it may be one of the pop-up's own keys.
        if (pop != nullptr)
            pop->setVisible(false);
        if (scrim != nullptr)
            scrim->setVisible(false);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SequencerGlass>(this),
                                         p = pop.release(), s = scrim.release()]
        {
            delete p;
            delete s;
            juce::ignoreUnused(safe);
        });
    }
}
