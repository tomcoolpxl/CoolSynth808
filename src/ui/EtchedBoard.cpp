#include "EtchedBoard.h"

#include "Hardware.h"
#include "Theme.h"
#include "Text.h"

namespace cs808
{
    namespace
    {
        juce::Colour rgb(std::uint32_t c, float a = 1.0f) { return juce::Colour(0xff000000u | c).withAlpha(a); }
    }

    EtchedBoard::EtchedBoard()
    {
        setOpaque(true);
        hitAt.fill(-1.0e9);
        // The columns as the panel lays its knobs out (D292): twelve across x 34 to 1246, the Master's 0.85 as wide;
        // the mix along their head at y 46, the trigger bus at their foot under the glass's top edge (D298).
        constexpr float left = 34.0f, right = 1246.0f;
        const float unit = (right - left) / (0.85f + 11.0f);
        std::vector<EtchedCircuit::Column> columns;
        for (int c = 0; c < 12; ++c)
        {
            columns.push_back({ c == 0 ? left : left + unit * (0.85f + static_cast<float>(c - 1)), unit * (c == 0 ? 0.85f : 1.0f) });
            columnSpans.push_back({ columns.back().x, columns.back().x + columns.back().w });
        }
        circuit.build(columns, 46.0f, glassTop + 12.0f);
        std::vector<float> centres;
        for (int c = 1; c < 12; ++c)
            centres.push_back(columns[static_cast<std::size_t>(c)].x + columns[static_cast<std::size_t>(c)].w * 0.5f);
        buildCpu(centres);
    }

    void EtchedBoard::setKeepOuts(std::vector<KeepOut> list)
    {
        keeps = std::move(list);
        std::vector<juce::Rectangle<float>> boxes;
        copperClip.clear();
        copperClip.addRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        for (const auto& k : keeps)
        {
            boxes.push_back(k.area);
            if (k.round)
                copperClip.addEllipse(k.area);
            else
                copperClip.addRoundedRectangle(k.area, 4.0f);
        }
        copperClip.setUsingNonZeroWinding(false);
        circuit.setTaken(std::move(boxes));
        cache = {};
        repaint();
    }

    void EtchedBoard::paint(juce::Graphics& g)
    {
        const float scale = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (! cache.isValid() || std::abs(cacheScale - scale) > 0.01f)
        {
            cacheScale = scale;
            cache = juce::Image(juce::Image::RGB, juce::roundToInt(width * scale), juce::roundToInt(height * scale), false);
            juce::Graphics c(cache);
            c.addTransform(juce::AffineTransform::scale(scale));
            paintMetal(c);
            if (lifted || circuitShow != CircuitShow::hidden)
            {
                // faint: the whole circuit under half, its names and still copper too (D314)
                if (! lifted && circuitShow == CircuitShow::faint)
                    c.beginTransparencyLayer(circuitFade());
                paintCopper(c);
                paintCpu(c);
                if (! lifted && circuitShow == CircuitShow::faint)
                    c.endTransparencyLayer();
            }
            paintOutline(c);
            if (lifted)
                paintFootprints(c);
            else
                paintShadows(c);
        }
        g.drawImageTransformed(cache, juce::AffineTransform::scale(1.0f / cacheScale));
        // The circuit lit over its still self, cut round the controls as the copper is (D298).
        if (! circuitAnimated())
        {
            glowing = false;
            return;
        }
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(copperClip);
        glowing = circuit.paintLit(g, now, [this](int owner) { return energyOf(owner); }, circuitFade(),
                                   lifted ? EtchedCircuit::Tint([this](int owner, int role) { return tintOf(owner, role); }) : EtchedCircuit::Tint());
        if (lifted)
        {
            juce::Graphics::ScopedSaveState cpuState(g);
            g.reduceClipRegion(juce::Rectangle<int>(14, juce::roundToInt(glassTop), 1252, 442));
            cpu.paintLit(g, now, [this](int owner) { return energyOf(owner); });
        }
        if (lifted)
            paintOpenFrames(g);
    }

    float EtchedBoard::tintOf(int owner, int role) const
    {
        if (role < 0 || role >= PanelSwings::roleCount)
            return 0.0f;
        const bool liveNow = liveBoard != nullptr && liveBoard->live(now);
        const auto of = [&](int column)
        {
            const auto lane = static_cast<std::size_t>(column - 1);
            if (liveNow) // as the engine moves the node (D320)
            {
                const int slot = liveBoard->slotOf(column - 1, swings.names[lane][static_cast<std::size_t>(role)]);
                return slot >= 0 ? liveBoard->value(column - 1, slot) : 0.0f;
            }
            const double el = now - hitAt[static_cast<std::size_t>(column)];
            if (el < 0.0 || el > 5000.0)
                return 0.0f;
            float s = 1.0f;
            const float m = swings.lanes[static_cast<std::size_t>(column - 1)][static_cast<std::size_t>(role)].at(el, s);
            return s * m;
        };
        if (owner >= 1 && owner <= 11)
            return of(owner);
        if (owner != 0 && owner != EtchedCircuit::triggerBus && owner != EtchedCircuit::mixBus)
            return 0.0f;
        float v = 0.0f; // the master and the buses: the drum that moves it most now
        for (int c = 1; c < 12; ++c)
            if (const float x = of(c); std::abs(x) > std::abs(v))
                v = x;
        return v;
    }

    juce::Rectangle<float> EtchedBoard::openArea(int column) const
    {
        const auto& span = columnSpans[static_cast<std::size_t>(column)];
        return { span.getStart() + 3.0f, 40.0f, span.getLength() - 6.0f, glassTop - 40.0f };
    }

    int EtchedBoard::columnAt(juce::Point<float> p) const
    {
        for (int c = 0; c < static_cast<int>(columnSpans.size()); ++c)
            if (openArea(c).contains(p))
                return c;
        return -1;
    }

    void EtchedBoard::paintOpenFrames(juce::Graphics& g) const
    {
        // What opens (D319): each column (the master's opens the whole board) in a faint frame, bright under the
        // mouse; most people never click when the only hint is a hover.
        const auto& look = boardLook();
        for (int c = 0; c < static_cast<int>(columnSpans.size()); ++c)
        {
            const bool over = c == hovered;
            g.setColour(look.ink.withAlpha(over ? 0.75f : 0.16f));
            g.drawRoundedRectangle(openArea(c), 6.0f, over ? 1.6f : 1.0f);
        }
    }

    void EtchedBoard::mouseMove(const juce::MouseEvent& e)
    {
        const int now = lifted ? columnAt(e.position) : -1;
        if (now == hovered)
            return;
        hovered = now;
        setMouseCursor(now >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void EtchedBoard::mouseExit(const juce::MouseEvent&)
    {
        if (hovered < 0)
            return;
        hovered = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

    void EtchedBoard::struck(std::uint32_t lanes, double nowMs)
    {
        for (int l = 0; l < 11; ++l)
            if (((lanes >> l) & 1u) != 0)
            {
                hitAt[static_cast<std::size_t>(l + 1)] = nowMs;
                if (l == 10)
                    hitAt[10] = -1.0e9; // the closed hat cuts the open one
            }
        if (lanes != 0)
            stepAt = nowMs;
    }

    void EtchedBoard::sequencerState(std::int64_t sixteenth, std::uint64_t edits, double nowMs)
    {
        if (sixteenth != sixteenthNow)
        {
            sixteenthNow = sixteenth;
            if (sixteenth >= 0)
                clockAt = nowMs; // the tempo clock's tick, read by the CPU, and the step it sends
        }
        if (edits != editsSeen)
        {
            if (editsSeen != 0)
                memoryAt = nowMs; // written to the pattern memory
            editsSeen = edits;
        }
    }

    void EtchedBoard::buildCpu(const std::vector<float>& columnCentres)
    {
        // The 808's CPU board under the glass (D315; docs/design/COOLSYNTH_808_CPU.md in CoolSynth's repository): the
        // main board's real parts by their names, laid out by hand so no two traces cross, drawn as the columns above
        // are. Our sequencer drives its light, not Roland's program: the clock, the step, the memory as it is read and
        // written, the keys' scan, the lamps, and each drum's instrument line up to the trigger bus.
        using P = juce::Point<float>;
        using C = EtchedCircuit;
        cpu.clear();
        cpu.setNamesOffCopper(true);
        const float busY = glassTop + 12.0f; // the trigger bus the columns' circuits hang from

        // The CPU, a uPD650C-085 in 40 pins (0.6 in), at the left; its pins 0.1 in apart, numbered from the left.
        const P cpuAt { 130.0f, 488.0f };
        constexpr float cpuH = 42.0f, pitch = 7.0f;
        const auto cpuTop = [&](int i) { return P(cpuAt.x - 70.0f + pitch * (static_cast<float>(i) + 0.5f), cpuAt.y - cpuH * 0.5f - 3.5f); };
        const auto cpuBottom = [&](int i) { return P(cpuAt.x - 70.0f + pitch * (static_cast<float>(i) + 0.5f), cpuAt.y + cpuH * 0.5f + 3.5f); };
        cpu.addPart('D', cpuAt, cpuH, C::cpuCore, "IC4", 40, "uPD650C-085");

        // The instrument lines (PD, PE, PF): one per drum, from the CPU's top pins up to the trigger bus at its column,
        // the leftmost drum's line the highest so none crosses another.
        for (int c = 1; c <= 11 && c <= static_cast<int>(columnCentres.size()); ++c)
        {
            const auto pin = cpuTop(c - 1);
            const float y = 380.0f + 4.0f * static_cast<float>(c - 1), x = columnCentres[static_cast<std::size_t>(c - 1)];
            cpu.addTrace({ pin, { pin.x, y }, { x, y }, { x, busY } }, c);
        }

        // The step's pulse (PI-2) and the accent (PF-3) to the trigger shaper at the centre; write and enable (PI-0,
        // PI-1) to the memories' decoder.
        const P shaper { 760.0f, 480.0f };
        cpu.addTrace({ cpuTop(11), { cpuTop(11).x, 432.0f }, { 690.0f, 432.0f }, { 690.0f, 466.0f } }, C::triggerBus);
        cpu.addTrace({ cpuTop(12), { cpuTop(12).x, 438.0f }, { 650.0f, 438.0f }, { 650.0f, 560.0f }, { 700.0f, 560.0f } }, C::triggerBus);
        const P decoder { 470.0f, 520.0f };
        cpu.addTrace({ cpuTop(13), { cpuTop(13).x, 444.0f }, { decoder.x - 10.5f, 444.0f }, { decoder.x - 10.5f, decoder.y - 14.0f } }, C::cpuMemory);
        cpu.addTrace({ cpuTop(14), { cpuTop(14).x, 450.0f }, { decoder.x - 3.5f, 450.0f }, { decoder.x - 3.5f, decoder.y - 14.0f } }, C::cpuMemory);
        for (int i = 15; i < 20; ++i) // the supply and the reset at the CPU's far end
            cpu.addTrace({ cpuTop(i), { cpuTop(i).x, cpuTop(i).y - 8.0f - 2.0f * static_cast<float>(i - 15) } }, C::cpuPower);

        // The trigger shaper: Q27 turns the pulse over, IC6 (a TC4011) widens it to about 1 ms (R100 C23, R102 C27);
        // the accent's Q31 to Q34 and VR3 set its height; the common trigger runs right and up to the trigger bus.
        cpu.addPart('Q', { 700.0f, 470.0f }, 8.0f, C::triggerBus, "Q27");
        cpu.addPart('D', shaper, 21.0f, C::triggerBus, "IC6", 14, "TC4011BP");
        cpu.addPart('R', { 830.0f, 452.0f }, 7.0f, C::triggerBus, "R100");
        cpu.addPart('C', { 870.0f, 452.0f }, 7.0f, C::triggerBus, "C23");
        cpu.addPart('R', { 830.0f, 508.0f }, 7.0f, C::triggerBus, "R102");
        cpu.addPart('C', { 870.0f, 508.0f }, 7.0f, C::triggerBus, "C27");
        cpu.addTrace({ { 708.0f, 470.0f }, { 735.0f, 470.0f } }, C::triggerBus);
        cpu.addTrace({ { 784.0f, 466.0f }, { 784.0f, 452.0f }, { 821.0f, 452.0f } }, C::triggerBus);
        cpu.addTrace({ { 839.0f, 452.0f }, { 865.0f, 452.0f } }, C::triggerBus);
        cpu.addTrace({ { 784.0f, 494.0f }, { 784.0f, 508.0f }, { 821.0f, 508.0f } }, C::triggerBus);
        cpu.addTrace({ { 839.0f, 508.0f }, { 865.0f, 508.0f } }, C::triggerBus);
        cpu.addTrace({ { 875.0f, 452.0f }, { 900.0f, 452.0f }, { 900.0f, 470.0f }, { 1232.0f, 470.0f }, { 1232.0f, busY } }, C::triggerBus);
        for (int i = 0; i < 4; ++i)
        {
            const float x = 730.0f + 52.0f * static_cast<float>(i);
            cpu.addPart('Q', { x, 575.0f }, 8.0f, C::triggerBus, "Q" + juce::String(31 + i));
            cpu.addPart('R', { x, 612.0f }, 6.0f, C::triggerBus, "R" + juce::String(104 + i), 0, {}, true);
            cpu.addTrace({ { x - 3.6f, 575.0f }, { x - 26.0f, 575.0f } }, C::triggerBus);
            cpu.addTrace({ { x, 582.0f }, { x, 604.0f } }, C::triggerBus);
            cpu.addTrace({ { x, 620.0f }, { x, 640.0f }, { 890.0f, 640.0f } }, C::cpuPower);
        }
        cpu.addPart('T', { 912.0f, 600.0f }, 8.0f, C::triggerBus, "VR3");
        cpu.addTrace({ { 912.0f, 591.6f }, { 912.0f, 575.0f }, { 891.0f, 575.0f } }, C::triggerBus);

        // The memories: IC5 decodes PE2 and PE3 to pick one of four uPD444C (1K x 4 each, 18 pins), its four selects
        // reaching each memory's pin 1 without a crossing.
        cpu.addPart('D', decoder, 21.0f, C::cpuMemory, "IC5", 14, "MC14001");
        constexpr std::array<float, 4> ramX { 330.0f, 420.0f, 510.0f, 600.0f };
        constexpr float ramY = 600.0f;
        for (std::size_t r = 0; r < ramX.size(); ++r)
            cpu.addPart('D', { ramX[r], ramY }, 21.0f, C::cpuMemory, "IC" + juce::String(7 + static_cast<int>(r)), 18, "uPD444C");
        const auto ramTopPin0 = [&](std::size_t r) { return P(ramX[r] - 31.5f + 3.5f, ramY - 10.5f - 3.5f); };
        const auto decoderBottom = [&](int i) { return P(decoder.x - 24.5f + 3.5f + 7.0f * static_cast<float>(i), decoder.y + 14.0f); };
        cpu.addTrace({ decoderBottom(0), { decoderBottom(0).x, 560.0f }, { ramTopPin0(0).x, 560.0f }, ramTopPin0(0) }, C::cpuMemory);
        cpu.addTrace({ decoderBottom(1), { decoderBottom(1).x, 564.0f }, { ramTopPin0(1).x, 564.0f }, ramTopPin0(1) }, C::cpuMemory);
        cpu.addTrace({ decoderBottom(3), { decoderBottom(3).x, 560.0f }, { ramTopPin0(3).x, 560.0f }, ramTopPin0(3) }, C::cpuMemory);
        cpu.addTrace({ decoderBottom(2), { decoderBottom(2).x, 564.0f }, { ramTopPin0(2).x, 564.0f }, ramTopPin0(2) }, C::cpuMemory);

        // The data and address bus (PC and PD to PF): six lines from the CPU's last pins, down and along under the
        // memories, each memory reaching down to them as a comb; R85 to R88 in the data lines against write clashes.
        for (int j = 0; j < 6; ++j)
        {
            const auto pin = cpuBottom(14 + j);
            const float y = 650.0f + 4.0f * static_cast<float>(j);
            cpu.addTrace({ pin, { pin.x, y }, { 644.0f, y } }, C::cpuMemory, true);
            for (const float x : ramX)
            {
                const float px = x - 31.5f + 3.5f + 7.0f * static_cast<float>(j + 1);
                cpu.addTrace({ { px, ramY + 10.5f + 3.5f }, { px, y } }, C::cpuMemory, true);
            }
            if (j < 4)
                cpu.addPart('R', { pin.x, 540.0f }, 5.0f, C::cpuMemory, "R" + juce::String(85 + j), 0, {}, true);
        }

        // The keys and lamps (PH scan, PG lamps, PB and PA keys): twelve lines from the CPU's foot to the switch
        // board's connector, the further right a pin, the higher its line, so none crosses another.
        const P keysAt { 560.0f, 766.0f };
        constexpr int keyLines = 12;
        cpu.addPart('J', keysAt, 7.0f, C::cpuPower, "CN2", keyLines + 2);
        for (int k = 0; k < keyLines; ++k)
        {
            const auto pin = cpuBottom(2 + k);
            const float y = 744.0f - 3.0f * static_cast<float>(k);
            const float cx = keysAt.x - static_cast<float>(keyLines + 2) * 3.5f + 3.5f + 7.0f * static_cast<float>(k + 1);
            const int owner = k < 4 ? C::cpuScan + k : k < 8 ? C::cpuLamp + (k - 4) : C::cpuScan + (k - 8);
            cpu.addTrace({ pin, { pin.x, y }, { cx, y }, { cx, keysAt.y } }, owner, true);
        }

        // The clocks: IFT-1's coil and the CPU's own 500 kHz, IC1 (an HD14584) the 1.9 ms interrupt, TM-1 its trim.
        cpu.addPart('X', { 44.0f, 548.0f }, 16.0f, C::cpuClock, "IFT-1");
        cpu.addTrace({ { 44.0f + 4.8f, 548.0f + 11.5f }, { 52.0f, 572.0f }, { cpuBottom(0).x, 572.0f }, cpuBottom(0) }, C::cpuClock);
        cpu.addPart('D', { 48.0f, 640.0f }, 21.0f, C::cpuClock, "IC1", 14, "HD14584", true);
        cpu.addTrace({ { 48.0f + 14.0f, 640.0f - 21.0f }, { cpuBottom(1).x, 619.0f }, cpuBottom(1) }, C::cpuClock);
        cpu.addPart('T', { 48.0f, 700.0f }, 8.0f, C::cpuClock, "TM-1");
        cpu.addTrace({ { 48.0f, 691.6f }, { 48.0f, 676.0f } }, C::cpuClock);
        cpu.addPart('C', { 80.0f, 700.0f }, 7.0f, C::cpuClock, "C11");

        // The tempo clock: IC12 (a TC4013 as a toggle) halves the tempo oscillator, TM-2 trims its range, CN3 takes the
        // Tempo knob; IC11 (an MC14001) gates the keys' scan.
        cpu.addPart('D', { 790.0f, 700.0f }, 21.0f, C::cpuClock, "IC12", 14, "TC4013BP");
        cpu.addPart('T', { 868.0f, 700.0f }, 8.0f, C::cpuClock, "TM-2");
        cpu.addPart('J', { 940.0f, 700.0f }, 7.0f, C::cpuClock, "CN3", 3);
        cpu.addTrace({ { 814.5f - 3.5f, 686.0f }, { 811.0f, 676.0f }, { 868.0f, 676.0f }, { 868.0f, 691.6f } }, C::cpuClock);
        cpu.addTrace({ { 872.8f, 708.4f }, { 872.8f, 724.0f }, { 933.0f, 724.0f }, { 933.0f, 700.0f } }, C::cpuClock);
        cpu.addTrace({ { 863.2f, 708.4f }, { 863.2f, 730.0f }, { 947.0f, 730.0f }, { 947.0f, 700.0f } }, C::cpuPower);
        cpu.addPart('D', { 700.0f, 700.0f }, 21.0f, C::cpuScan, "IC11", 14, "MC14001");
        for (int i = 0; i < 3; ++i)
        {
            // nested, the outermost the deepest, so none crosses another
            const float from = 707.0f + 7.0f * static_cast<float>(i), to = 783.0f - 7.0f * static_cast<float>(i), y = 752.0f - 4.0f * static_cast<float>(i);
            cpu.addTrace({ { from, 714.0f }, { from, y }, { to, y }, { to, 714.0f } }, C::cpuScan + i, true);
        }
        // the chips' decoupling capacitors, each beside its chip's supply pin
        const std::array<std::pair<P, const char*>, 6> decoupling { { { { 222.0f, 470.0f }, "C1" }, { { 375.0f, 640.0f }, "C2" },
                                                                    { { 525.0f, 520.0f }, "C3" }, { { 815.0f, 480.0f }, "C4" },
                                                                    { { 1070.0f, 590.0f }, "C5" }, { { 985.0f, 700.0f }, "C6" } } };
        for (const auto& [at, name] : decoupling)
            cpu.addPart('C', at, 6.0f, C::cpuPower, name);

        // The status gate (IC3) and start/stop (IC2, a TC4013), the scan's inverters (Q18, Q23, Q24), the DIN socket
        // for sync, the backup battery and its switches (Q64, Q75) that keep the memories alive.
        const P gate { 1010.0f, 540.0f }, startStop { 1130.0f, 540.0f };
        cpu.addPart('D', gate, 21.0f, C::cpuCore, "IC3", 16, "MC14051");
        cpu.addPart('D', startStop, 21.0f, C::cpuCore, "IC2", 14, "TC4013BP");
        for (int i = 0; i < 3; ++i)
        {
            const float x = 980.0f + 60.0f * static_cast<float>(i);
            cpu.addPart('Q', { x, 610.0f }, 8.0f, C::cpuScan + i, juce::String(i == 0 ? "Q18" : i == 1 ? "Q23" : "Q24"));
            cpu.addPart('R', { x + 22.0f, 640.0f }, 6.0f, C::cpuScan + i, "R" + juce::String(60 + i), 0, {}, true);
            cpu.addTrace({ { x, 603.0f }, { x, 558.0f } }, C::cpuScan + i);
            cpu.addTrace({ { x + 3.6f, 610.0f }, { x + 22.0f, 610.0f }, { x + 22.0f, 632.0f } }, C::cpuScan + i);
        }
        for (int i = 0; i < 4; ++i)
            cpu.addTrace({ { gate.x + 31.5f, 528.0f - 3.0f * static_cast<float>(i) - 4.0f }, { startStop.x - 24.5f, 528.0f - 3.0f * static_cast<float>(i) - 4.0f } }, C::cpuCore, true);
        cpu.addPart('K', { 1110.0f, 722.0f }, 17.0f, C::cpuClock, "DIN SYNC");
        cpu.addTrace({ { 1110.0f - 10.2f, 722.0f }, { 1070.0f, 722.0f }, { 1070.0f, 680.0f }, { startStop.x - 10.5f, 680.0f }, { startStop.x - 10.5f, startStop.y + 14.0f } }, C::cpuClock);
        cpu.addPart('B', { 1200.0f, 722.0f }, 15.0f, C::cpuPower, "BT1");
        cpu.addPart('Q', { 1200.0f, 660.0f }, 8.0f, C::cpuPower, "Q64");
        cpu.addPart('Q', { 1200.0f, 610.0f }, 8.0f, C::cpuPower, "Q75");
        cpu.addTrace({ { 1200.0f + 19.0f, 722.0f }, { 1236.0f, 722.0f }, { 1236.0f, 660.0f }, { 1203.6f, 660.0f } }, C::cpuPower);
        cpu.addTrace({ { 1200.0f, 653.0f }, { 1200.0f, 617.0f } }, C::cpuPower);
        // the battery's line to the memories, along the board's foot and up past their right end
        cpu.addTrace({ { 1200.0f - 19.0f, 722.0f }, { 1150.0f, 722.0f }, { 1150.0f, 778.0f }, { 652.0f, 778.0f }, { 652.0f, ramY + 14.0f }, { ramX.back() + 31.5f - 3.5f, ramY + 14.0f } }, C::cpuPower);
        cpu.finish();
    }

    void EtchedBoard::paintCpu(juce::Graphics& g) const
    {
        // The CPU board unlit, under the glass (D315): seen whole with the lid lifted.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(juce::Rectangle<int>(14, juce::roundToInt(glassTop), 1252, 442));
        cpu.paintStill(g);
    }

    void EtchedBoard::paintCpuGlow(juce::Graphics& g, juce::Point<float> origin)
    {
        // With the lid closed, the CPU board shows through the smoked glass as a faint hint: its copper and names barely
        // there, and the drums' instrument lines glowing softly as they are struck (the user, D314). Drawn by the glass.
        if (circuitShow == CircuitShow::hidden || lifted)
            return;
        const float scale = std::max(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (! cpuImage.isValid() || std::abs(cpuImageScale - scale) > 0.01f)
        {
            cpuImageScale = scale;
            cpuImage = juce::Image(juce::Image::ARGB, juce::roundToInt(1252 * scale), juce::roundToInt(442 * scale), true);
            juce::Graphics c(cpuImage);
            c.addTransform(juce::AffineTransform::translation(-14.0f, -glassTop).scaled(scale));
            cpu.paintStill(c);
        }
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::translation(-origin.x, -origin.y));
        const float fade = circuitShow == CircuitShow::faint ? 0.5f : 1.0f;
        g.setOpacity(0.07f * fade);
        g.drawImageTransformed(cpuImage, juce::AffineTransform::scale(1.0f / cpuImageScale).translated(14.0f, glassTop));
        g.setOpacity(1.0f);
        if (circuitAnimated())
            cpu.paintLit(g, now, [this](int owner) { return owner >= 1 && owner <= 11 ? energyOf(owner) : 0.0f; }, 0.3f * fade);
    }

    void EtchedBoard::tick(double nowMs)
    {
        now = nowMs;
        if (lifted)
        {
            // the lid lifted (D315): the whole board is the view, always lit and animated
            repaint();
            return;
        }
        if (! circuitAnimated())
            return; // still: nothing to draw again
        bool lit = glowing || nowMs - stepAt < 400.0;
        for (std::size_t c = 1; c < live.size() && ! lit; ++c)
            lit = live[c] > 0.0f;
        for (int c = 1; c < 12 && ! lit; ++c)
            lit = nowMs - hitAt[static_cast<std::size_t>(c)] < 1000.0;
        if (lit)
        {
            repaint(0, 0, width, juce::roundToInt(glassTop) + 40); // the circuit's strip
            repaint(14, juce::roundToInt(glassTop), 1252, 126);     // and the instrument lines' under the glass's top
        }
    }

    void EtchedBoard::liveLevels(const std::array<float, 12>& byLane, unsigned int reports, double nowMs)
    {
        live = byLane;
        if (reports != liveReports)
        {
            liveReports = reports;
            liveAt = nowMs;
        }
    }

    float EtchedBoard::energyOf(int owner) const noexcept
    {
        const auto since = [this](double t) { return now - t; };
        // Live (D313): each drum's level as its circuit plays it, on the recordings' scale: 36 dB from full to dark,
        // full at the drum's peak with the kit's defaults and no accent (ProcessorTests logs them). An accent or a
        // louder setting lights it full; a quieter one dims it, as it sounds. The recordings only while no engine runs.
        static constexpr std::array<float, 12> defaultPeak { 1.0f, 0.3707f, 0.3554f, 0.4395f, 0.4241f, 0.4677f, 0.5060f,
                                                             0.6667f, 0.3881f, 0.4375f, 0.4993f, 0.3604f };
        const bool engineRuns = now - liveAt < 250.0;
        const auto liveBrightness = [&](int lane)
        {
            const float level = live[static_cast<std::size_t>(lane)] / defaultPeak[static_cast<std::size_t>(lane)];
            return level <= 0.0f ? 0.0f : juce::jlimit(0.0f, 1.0f, 1.0f + 20.0f * std::log10(level) / 36.0f);
        };
        if (engineRuns && owner >= 1 && owner <= 11)
            return liveBrightness(owner);
        if (engineRuns && owner == EtchedCircuit::mixBus)
        {
            float m = 0.0f;
            for (int c = 1; c < 12; ++c)
                m = std::max(m, liveBrightness(c));
            return m * 0.75f;
        }
        // The CPU board's (D315): its clocks always ticking a little, more on each step; the CPU and its memory on each
        // step's read and on each edit's write; the keys' scan turning round; the playing step's group of lamps.
        const double sinceStep = since(clockAt);
        const bool running = sixteenthNow >= 0;
        if (owner == EtchedCircuit::cpuClock)
            return static_cast<float>((running ? 0.08 : 0.0) + 0.45 * std::exp(-sinceStep / 80.0));
        if (owner == EtchedCircuit::cpuCore)
            return static_cast<float>(0.7 * std::exp(-sinceStep / 110.0));
        if (owner == EtchedCircuit::cpuMemory)
            return static_cast<float>(std::max(0.9 * std::exp(-since(memoryAt) / 400.0), 0.35 * std::exp(-sinceStep / 60.0)));
        if (owner >= EtchedCircuit::cpuScan && owner < EtchedCircuit::cpuScan + 4)
            return running && (static_cast<int>(now / 140.0) & 3) == owner - EtchedCircuit::cpuScan ? 0.3f : 0.0f;
        if (owner >= EtchedCircuit::cpuLamp && owner < EtchedCircuit::cpuLamp + 4)
            return running && (sixteenthNow % 16) / 4 == owner - EtchedCircuit::cpuLamp ? 0.45f : 0.0f;
        if (owner == EtchedCircuit::cpuPower)
            return 0.0f;
        if (owner == EtchedCircuit::triggerBus)
            return static_cast<float>(std::exp(-since(stepAt) / 70.0));
        if (owner == 0) // the master column: the trigger and the accent
            return static_cast<float>(0.6 * std::exp(-since(stepAt) / 45.0));
        if (owner == EtchedCircuit::mixBus)
        {
            float m = 0.0f;
            for (int c = 1; c < 12; ++c)
                m = std::max(m, envelopes.brightness(c, since(hitAt[static_cast<std::size_t>(c)])));
            return m * 0.75f;
        }
        return owner >= 1 && owner <= 11 ? envelopes.brightness(owner, since(hitAt[static_cast<std::size_t>(owner)])) : 0.0f;
    }

    void EtchedBoard::paintMetal(juce::Graphics& g) const
    {
        // Brushed aluminium (D303, the mockups' 5.4): light, a little cooler at the foot.
        const auto w = static_cast<float>(width), h = static_cast<float>(height);
        // The theme's metal (D307): Silver's aluminium, Roland's charcoal.
        const auto& look = boardLook();
        g.setGradientFill(juce::ColourGradient(look.metalTop, 0.0f, 0.0f, look.metalBottom, 0.0f, h, false));
        g.fillRect(0.0f, 0.0f, w, h);
        // brushed: a faint light line every 3 px, the two between a shade darker
        for (int x = 0; x < width; x += 3)
        {
            g.setColour(juce::Colours::white.withAlpha(look.brushLight));
            g.fillRect(static_cast<float>(x), 0.0f, 1.0f, h);
            g.setColour(juce::Colours::black.withAlpha(look.brushDark));
            g.fillRect(static_cast<float>(x + 1), 0.0f, 2.0f, h);
        }
        // light falling on the upper left: an ellipse 1200 by 400 px round a point on the top edge
        {
            juce::Graphics::ScopedSaveState state(g);
            const juce::Point<float> c(w * 0.3f, 0.0f);
            g.addTransform(juce::AffineTransform::scale(1.0f, 400.0f / 1200.0f, c.x, c.y));
            juce::ColourGradient sheen(juce::Colours::white.withAlpha(look.sheen), c, juce::Colours::white.withAlpha(0.0f), c.translated(1200.0f * 0.7f, 0.0f), true);
            g.setGradientFill(sheen);
            g.fillRect(juce::Rectangle<float>(c.x - 1200.0f, -1200.0f, 2400.0f, 2400.0f));
        }
    }

    juce::Path EtchedBoard::tracePath(const std::vector<juce::Point<float>>& pts)
    {
        juce::Path p;
        if (pts.empty())
            return p;
        p.startNewSubPath(pts.front());
        for (std::size_t i = 1; i < pts.size(); ++i)
        {
            if (i + 1 < pts.size())
            {
                const auto a = pts[i - 1], b = pts[i], c = pts[i + 1];
                const float d1 = a.getDistanceFrom(b), d2 = b.getDistanceFrom(c);
                const float r = std::min({ 7.0f, d1 * 0.5f, d2 * 0.5f });
                if (d1 > 0.0f && d2 > 0.0f)
                {
                    p.lineTo(b - (b - a) * (r / d1));
                    p.lineTo(b + (c - b) * (r / d2));
                    continue;
                }
            }
            p.lineTo(pts[i]);
        }
        return p;
    }

    void EtchedBoard::paintCopper(juce::Graphics& g) const
    {
        {
            // The copper stops short of whatever is mounted: everything but the keep-outs. Even-odd, so the keep-outs
            // must not overlap one another, or the overlap would carry copper again.
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
            for (const auto& k : keeps)
            {
                if (k.round)
                    clip.addEllipse(k.area);
                else
                    clip.addRoundedRectangle(k.area, 4.0f);
            }
            clip.setUsingNonZeroWinding(false);
            g.reduceClipRegion(clip);
            paintGround(g);
            paintCircuit(g);
        }
    }

    void EtchedBoard::mouseUp(const juce::MouseEvent& e)
    {
        if (! lifted || ! onPartClicked || ! e.mouseWasClicked())
            return;
        if (e.position.y >= glassTop)
        {
            onPartClicked(-1); // the CPU board
            return;
        }
        if (const int c = columnAt(e.position); c >= 0)
            onPartClicked(c);
    }

    void EtchedBoard::paintFootprints(juce::Graphics& g) const
    {
        const auto& look = boardLook();
        const auto pad = [&](juce::Point<float> at, float d)
        {
            g.setColour(look.pad);
            g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(at));
            g.setColour(rgb(0x141210, 0.9f));
            g.fillEllipse(juce::Rectangle<float>(d * 0.4f, d * 0.4f).withCentre(at));
        };
        for (const auto& k : keeps)
        {
            if (k.footprint == 1)
            {
                // a pot's footprint: its round outline, the shaft's hole, three legs below
                const auto r = k.area.reduced(8.0f);
                g.setColour(look.silk);
                g.drawEllipse(r, 1.0f);
                g.setColour(look.metalBottom.darker(0.6f));
                g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(r.getCentre()));
                for (int i = -1; i <= 1; ++i)
                    pad({ r.getCentreX() + static_cast<float>(i) * 8.0f, r.getBottom() + 5.0f }, 5.2f);
            }
            else if (k.footprint == 2)
            {
                // a slide switch's: its outline and two rows of three pins
                const auto r = k.area.reduced(4.0f, 6.0f);
                g.setColour(look.silk);
                g.drawRoundedRectangle(r, 2.0f, 1.0f);
                for (int i = 0; i < 3; ++i)
                {
                    const float y = r.getY() + r.getHeight() * (0.25f + 0.25f * static_cast<float>(i));
                    pad({ r.getX() + r.getWidth() * 0.3f, y }, 5.0f);
                    pad({ r.getX() + r.getWidth() * 0.7f, y }, 5.0f);
                }
            }
        }
        // the holes the glass's four pins held it by, a lighter rim round each
        const juce::Rectangle<float> glass { 14.0f, glassTop, 1252.0f, 442.0f };
        for (const auto c : { glass.getTopLeft() + juce::Point<float>(9.0f, 9.0f), glass.getTopRight() + juce::Point<float>(-9.0f, 9.0f),
                              glass.getBottomLeft() + juce::Point<float>(9.0f, -9.0f), glass.getBottomRight() + juce::Point<float>(-9.0f, -9.0f) })
        {
            g.setColour(look.metalTop.brighter(0.25f));
            g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(c));
            g.setColour(rgb(0x0b0b0c));
            g.fillEllipse(juce::Rectangle<float>(6.5f, 6.5f).withCentre(c));
        }
    }

    void EtchedBoard::paintOutline(juce::Graphics& g) const
    {
        // The name plate keeps its faded outline where the copper was cut away round it, in the board's ink.
        g.setColour(boardLook().ink.withAlpha(0.32f));
        for (const auto& k : keeps)
            if (k.outlined)
            {
                if (k.round)
                    g.drawEllipse(k.area, 1.0f);
                else
                    g.drawRoundedRectangle(k.area, 6.0f, 1.0f);
            }
    }

    void EtchedBoard::paintShadows(juce::Graphics& g) const
    {
        // The sequencer's glass floats a little above the board on its four pins: its shadow falls on the metal,
        // offset down and soft; the transport strip, closer to the board, casts a smaller one (D303).
        // The user (D306): the first shade did not show. It falls down and to the right, away from the light on the
        // upper left, dark close to the glass and soft further out; the glass draws none of its own, as that is cut
        // at its edge.
        const juce::Rectangle<int> glass { 14, 344, 1252, 442 };
        juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 18, { 4, 9 }).drawForRectangle(g, glass);
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 6, { 2, 3 }).drawForRectangle(g, glass);
        if (! stripArea.isEmpty())
            juce::DropShadow(juce::Colours::black.withAlpha(0.3f), 8, { 0, 3 }).drawForRectangle(g, stripArea.toNearestInt());
    }

    void EtchedBoard::paintGround(juce::Graphics& g) const
    {
        // A ground ring round the sequencer, as real boards have along their edges; it carries no signal, so it never
        // lights. It leaves the board just under the glass's top edge on both sides.
        const float top = glassTop + 8.0f;
        const std::vector<juce::Point<float>> ring {
            { 34.0f, top }, { 22.0f, top + 12.0f }, { 22.0f, 776.0f }, { 38.0f, 792.0f },
            { 1242.0f, 792.0f }, { 1258.0f, 776.0f }, { 1258.0f, top + 12.0f }, { 1246.0f, top } };
        g.setColour(boardLook().copper);
        g.strokePath(tracePath(ring), juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (const auto end : { ring.front(), ring.back() })
        {
            g.setColour(boardLook().pad);
            g.fillEllipse(juce::Rectangle<float>(5.2f, 5.2f).withCentre(end));
            g.setColour(rgb(0x141210, 0.9f));
            g.fillEllipse(juce::Rectangle<float>(2.0f, 2.0f).withCentre(end));
        }
    }

    void EtchedBoard::paintCircuit(juce::Graphics& g) const
    {
        // The etched circuit unlit (D298): each column's drum under its knobs, the trigger bus under the glass's top
        // edge and the mix along the top. Its light is drawn over it each frame in paint.
        circuit.paintStill(g);
    }
}
