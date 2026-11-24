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
            columns.push_back({ c == 0 ? left : left + unit * (0.85f + static_cast<float>(c - 1)), unit * (c == 0 ? 0.85f : 1.0f) });
        circuit.build(columns, 46.0f, glassTop + 12.0f);
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
            paintCopper(c);
            paintCpu(c);
            paintShadows(c);
        }
        g.drawImageTransformed(cache, juce::AffineTransform::scale(1.0f / cacheScale));
        // The circuit lit over its still self, cut round the controls as the copper is (D298).
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(copperClip);
        glowing = circuit.paintLit(g, now, [this](int owner) { return energyOf(owner); });
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

    namespace
    {
        // Where the CPU section lies under the glass (D312): its chips under the empty foot of the tools at the right,
        // the trigger lines up the gap between the steps' lengths and the tools, the lamp lines along the glass's foot.
        const juce::Rectangle<float> cpuChip { 1034.0f, 700.0f, 150.0f, 30.0f };
        const juce::Rectangle<float> coil { 1008.0f, 708.0f, 14.0f, 14.0f };
        const juce::Rectangle<float> schmitt { 1196.0f, 708.0f, 40.0f, 14.0f };
        constexpr float ramY = 752.0f, ramW = 44.0f, ramH = 15.0f;
        constexpr std::array<float, 4> ramX { 1016.0f, 1074.0f, 1132.0f, 1190.0f };
        constexpr std::array<float, 3> trunkX { 988.0f, 992.0f, 996.0f };
        constexpr std::array<float, 4> lampY { 775.0f, 778.0f, 781.0f, 784.0f };
        const juce::Rectangle<int> cpuArea { 980, 690, 290, 100 };
        const juce::Rectangle<int> trunkArea { 982, 350, 20, 350 };
        const juce::Rectangle<int> lampArea { 230, 772, 810, 16 };

        void chip(juce::Graphics& g, juce::Rectangle<float> body, int pinsPerSide, const juce::String& name, const BoardLook& look)
        {
            // pads along both long sides, the black body over them, a notch at its left end, its name printed on it
            g.setColour(look.pad);
            const float pitch = body.getWidth() / static_cast<float>(pinsPerSide);
            for (int i = 0; i < pinsPerSide; ++i)
            {
                const float x = body.getX() + pitch * (static_cast<float>(i) + 0.5f);
                g.fillEllipse(juce::Rectangle<float>(3.2f, 3.2f).withCentre({ x, body.getY() - 2.5f }));
                g.fillEllipse(juce::Rectangle<float>(3.2f, 3.2f).withCentre({ x, body.getBottom() + 2.5f }));
            }
            g.setColour(juce::Colour(0xff141516).withAlpha(0.9f));
            g.fillRoundedRectangle(body, 1.5f);
            g.setColour(look.silk);
            g.drawRoundedRectangle(body.expanded(1.5f), 2.0f, 0.8f);
            g.setColour(juce::Colour(0xff2a2b2d));
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({ body.getX() + 2.0f, body.getCentreY() }));
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(monoFont(body.getHeight() > 20.0f ? 7.0f : 5.5f));
            shapedText(g, name, body.reduced(4.0f, 0.0f), juce::Justification::centred, false);
        }
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

    void EtchedBoard::paintCpu(juce::Graphics& g) const
    {
        const auto& look = boardLook();
        const auto stroke = juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        // Brighter than the copper above the glass, which shows it faintly: the glass passes about a seventh.
        g.setColour(look.copper.withAlpha(1.0f).brighter(0.35f));
        // the trigger lines: the common trigger, the accent and the instrument data, up to the trigger bus
        for (std::size_t i = 0; i < trunkX.size(); ++i)
        {
            const float x = trunkX[i], y0 = cpuChip.getY() - 2.5f, turn = 692.0f - static_cast<float>(i) * 3.0f;
            g.strokePath(tracePath({ { cpuChip.getX() + 4.0f + static_cast<float>(i) * 7.5f, y0 }, { cpuChip.getX() + 4.0f + static_cast<float>(i) * 7.5f, turn },
                                     { x, turn }, { x, glassTop + 12.0f } }), stroke);
        }
        // the lamp lines (PG0 to PG3) along the glass's foot, under the step keys
        for (std::size_t i = 0; i < lampY.size(); ++i)
        {
            const float px = cpuChip.getX() + 4.0f + static_cast<float>(i) * 7.5f;
            g.strokePath(tracePath({ { px, cpuChip.getBottom() + 2.5f }, { px, lampY[i] - 4.0f }, { px - 4.0f, lampY[i] }, { 240.0f, lampY[i] } }), stroke);
        }
        // the data bus (PC0 to PC3) and the addresses, from the CPU's foot to the four memories
        for (int b = 0; b < 4; ++b)
        {
            const float y = 741.0f + static_cast<float>(b) * 2.0f, px = cpuChip.getX() + 60.0f + static_cast<float>(b) * 7.5f;
            g.strokePath(tracePath({ { px, cpuChip.getBottom() + 2.5f }, { px, y } }), stroke);
            g.drawLine(ramX.front() + 6.0f, y, ramX.back() + ramW - 6.0f, y, 1.0f);
        }
        for (const float x : ramX)
            for (int b = 0; b < 4; ++b)
                g.drawLine(x + 6.0f + static_cast<float>(b) * 4.6f, 741.0f + static_cast<float>(b) * 2.0f, x + 6.0f + static_cast<float>(b) * 4.6f, ramY - 2.5f, 1.0f);
        // the clock: the coil beside the CPU, the Schmitt trigger's interrupt into it
        g.strokePath(tracePath({ { coil.getRight(), coil.getCentreY() }, { cpuChip.getX(), coil.getCentreY() } }), stroke);
        g.strokePath(tracePath({ { schmitt.getX(), schmitt.getCentreY() }, { cpuChip.getRight(), schmitt.getCentreY() } }), stroke);
        g.setColour(look.silk);
        g.drawRect(coil, 0.8f);
        g.drawEllipse(coil.reduced(3.0f), 0.8f);
        chip(g, cpuChip, 20, "uPD650C-085", look);
        chip(g, schmitt, 7, "IC1", look);
        for (std::size_t i = 0; i < ramX.size(); ++i)
            chip(g, { ramX[i], ramY, ramW, ramH }, 9, "IC" + juce::String(7 + static_cast<int>(i)), look);
        g.setColour(look.silkText);
        g.setFont(monoFont(6.0f));
        shapedText(g, "IFT-1", coil.translated(0.0f, 15.0f).withSizeKeepingCentre(30.0f, 8.0f), juce::Justification::centred, false);
    }

    void EtchedBoard::paintCpuGlow(juce::Graphics& g, juce::Point<float> origin) const
    {
        // On the glass: the copper beneath shows faintly through the smoke, always; what works lights as thin bright
        // traces with a small halo (D312). Solid strokes only, no gradients (D306).
        const auto& look = boardLook();
        const float tick = static_cast<float>(std::exp(-(now - clockAt) / 120.0));
        const float trig = static_cast<float>(std::exp(-(now - clockAt) / 70.0));
        const float mem = static_cast<float>(std::exp(-(now - memoryAt) / 400.0));
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::translation(-origin.x, -origin.y));

        const auto trunk = [](int i)
        {
            const float x = trunkX[static_cast<std::size_t>(i)], px = cpuChip.getX() + 4.0f + static_cast<float>(i) * 7.5f;
            const float turn = 692.0f - static_cast<float>(i) * 3.0f;
            return tracePath({ { px, cpuChip.getY() - 2.5f }, { px, turn }, { x, turn }, { x, glassTop + 12.0f } });
        };
        const auto lamp = [](int i)
        {
            const float px = cpuChip.getX() + 4.0f + static_cast<float>(i) * 7.5f, y = lampY[static_cast<std::size_t>(i)];
            return tracePath({ { px, cpuChip.getBottom() + 2.5f }, { px, y - 4.0f }, { px - 4.0f, y }, { 240.0f, y } });
        };
        const auto bus = [](int b)
        {
            juce::Path path;
            const float y = 741.0f + static_cast<float>(b) * 2.0f, px = cpuChip.getX() + 60.0f + static_cast<float>(b) * 7.5f;
            path.startNewSubPath(px, cpuChip.getBottom() + 2.5f);
            path.lineTo(px, y);
            path.startNewSubPath(ramX.front() + 6.0f, y);
            path.lineTo(ramX.back() + ramW - 6.0f, y);
            for (const float x : ramX)
            {
                path.startNewSubPath(x + 6.0f + static_cast<float>(b) * 4.6f, y);
                path.lineTo(x + 6.0f + static_cast<float>(b) * 4.6f, ramY - 2.5f);
            }
            return path;
        };
        const auto pins = [&g](juce::Rectangle<float> body, int perSide, float size)
        {
            const float pitch = body.getWidth() / static_cast<float>(perSide);
            for (int i = 0; i < perSide; ++i)
            {
                const float x = body.getX() + pitch * (static_cast<float>(i) + 0.5f);
                g.fillEllipse(juce::Rectangle<float>(size, size).withCentre({ x, body.getY() - 2.5f }));
                g.fillEllipse(juce::Rectangle<float>(size, size).withCentre({ x, body.getBottom() + 2.5f }));
            }
        };
        const auto glowLine = [&g](const juce::Path& path, juce::Colour c, float amount)
        {
            g.setColour(c.withAlpha(0.12f * amount));
            g.strokePath(path, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(c.withAlpha(0.22f * amount));
            g.strokePath(path, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(c.withAlpha(0.9f * amount));
            g.strokePath(path, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        // the board beneath, faint through the smoke
        const auto faint = look.copper.withAlpha(1.0f).brighter(0.5f).withAlpha(0.16f);
        g.setColour(faint);
        const auto thin = juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        for (int i = 0; i < 3; ++i)
            g.strokePath(trunk(i), thin);
        for (int i = 0; i < 4; ++i)
        {
            g.strokePath(lamp(i), thin);
            g.strokePath(bus(i), thin);
        }
        pins(cpuChip, 20, 2.6f);
        pins(schmitt, 7, 2.6f);
        for (const float x : ramX)
            pins({ x, ramY, ramW, ramH }, 9, 2.6f);
        g.setColour(look.silkText.withAlpha(1.0f).brighter(0.6f).withAlpha(0.14f));
        for (const auto& body : { cpuChip, schmitt })
            g.drawRoundedRectangle(body.expanded(1.5f), 2.0f, 0.8f);
        for (const float x : ramX)
            g.drawRoundedRectangle(juce::Rectangle<float>(x, ramY, ramW, ramH).expanded(1.5f), 2.0f, 0.8f);
        g.drawRect(coil, 0.8f);
        g.setFont(monoFont(7.0f));
        shapedText(g, "uPD650C-085", cpuChip, juce::Justification::centred, false);
        g.setFont(monoFont(5.5f));
        for (std::size_t i = 0; i < ramX.size(); ++i)
            shapedText(g, "IC" + juce::String(7 + static_cast<int>(i)), juce::Rectangle<float>(ramX[i], ramY, ramW, ramH), juce::Justification::centred, false);

        // what works, lit
        if (trig > 0.03f)
            for (int i = 0; i < 3; ++i)
                glowLine(trunk(i), look.flowDot, trig);
        if (sixteenthNow >= 0)
            glowLine(lamp(static_cast<int>((sixteenthNow % 16) / 4)), look.flowWire, 0.7f);
        if (tick > 0.03f)
        {
            g.setColour(look.flowDot.withAlpha(0.9f * tick));
            pins(cpuChip, 20, 2.8f);
            g.drawRoundedRectangle(cpuChip.expanded(1.5f), 2.0f, 1.0f);
            juce::Path clock;
            clock.startNewSubPath(coil.getRight(), coil.getCentreY());
            clock.lineTo(cpuChip.getX(), coil.getCentreY());
            clock.startNewSubPath(schmitt.getX(), schmitt.getCentreY());
            clock.lineTo(cpuChip.getRight(), schmitt.getCentreY());
            glowLine(clock, look.flowDot, tick);
        }
        if (mem > 0.03f)
        {
            for (int b = 0; b < 4; ++b)
                glowLine(bus(b), look.flowDot, mem);
            g.setColour(look.flowDot.withAlpha(0.9f * mem));
            for (const float x : ramX)
            {
                pins({ x, ramY, ramW, ramH }, 9, 2.8f);
                g.drawRoundedRectangle(juce::Rectangle<float>(x, ramY, ramW, ramH).expanded(1.5f), 2.0f, 1.0f);
            }
        }
    }

    void EtchedBoard::tick(double nowMs)
    {
        now = nowMs;
        // The CPU section under the glass: only its own small areas drawn again, as the glass over them draws too.
        const bool cpuNow = nowMs - clockAt < 500.0 || nowMs - memoryAt < 1500.0;
        if (cpuNow || cpuLit)
        {
            repaint(cpuArea);
            repaint(trunkArea);
            repaint(lampArea);
        }
        cpuLit = cpuNow;
        bool lit = glowing || nowMs - stepAt < 400.0;
        for (std::size_t c = 1; c < live.size() && ! lit; ++c)
            lit = live[c] > 0.0f;
        for (int c = 1; c < 12 && ! lit; ++c)
            lit = nowMs - hitAt[static_cast<std::size_t>(c)] < 1000.0;
        if (lit)
            repaint(0, 0, width, juce::roundToInt(glassTop) + 40); // the circuit's strip, not the glass below
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
