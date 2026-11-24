#include "EtchedBoard.h"

namespace cs808
{
    namespace
    {
        juce::Colour rgb(std::uint32_t c, float a = 1.0f) { return juce::Colour(0xff000000u | c).withAlpha(a); }

        // The copper as the mockup's etched look has it: dull, a little warmer than the metal (D292).
        const juce::Colour copper = rgb(0xb08c5c, 0.32f);
        const juce::Colour padColour = rgb(0xbe9660, 0.55f);
        const juce::Colour outline = rgb(0xbe9660, 0.28f);
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
            paintScrews(c);
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

    void EtchedBoard::tick(double nowMs)
    {
        now = nowMs;
        bool lit = glowing || nowMs - stepAt < 400.0;
        for (int c = 1; c < 12 && ! lit; ++c)
            lit = nowMs - hitAt[static_cast<std::size_t>(c)] < 1000.0;
        if (lit)
            repaint(0, 0, width, juce::roundToInt(glassTop) + 40); // the circuit's strip, not the glass below
    }

    float EtchedBoard::energyOf(int owner) const noexcept
    {
        const auto since = [this](double t) { return now - t; };
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
        const auto w = static_cast<float>(width), h = static_cast<float>(height);
        g.setGradientFill(juce::ColourGradient(rgb(0x2c2a27), 0.0f, 0.0f, rgb(0x23211e), 0.0f, h, false));
        g.fillRect(0.0f, 0.0f, w, h);
        // brushed: a faint light line every 3 px, the two between a shade darker
        for (int x = 0; x < width; x += 3)
        {
            g.setColour(juce::Colours::white.withAlpha(0.012f));
            g.fillRect(static_cast<float>(x), 0.0f, 1.0f, h);
            g.setColour(juce::Colours::black.withAlpha(0.02f));
            g.fillRect(static_cast<float>(x + 1), 0.0f, 2.0f, h);
        }
        // light falling on the upper left: an ellipse 1200 by 400 px round a point on the top edge
        {
            juce::Graphics::ScopedSaveState state(g);
            const juce::Point<float> c(w * 0.3f, 0.0f);
            g.addTransform(juce::AffineTransform::scale(1.0f, 400.0f / 1200.0f, c.x, c.y));
            juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.05f), c, juce::Colours::white.withAlpha(0.0f), c.translated(1200.0f * 0.7f, 0.0f), true);
            g.setGradientFill(sheen);
            g.fillRect(juce::Rectangle<float>(c.x - 1200.0f, -1200.0f, 2400.0f, 2400.0f));
        }
    }

    void EtchedBoard::paintScrews(juce::Graphics& g) const
    {
        for (const float x : { 16.0f, static_cast<float>(width) - 16.0f })
        {
            const auto r = juce::Rectangle<float>(12.0f, 12.0f).withCentre({ x, 16.0f });
            g.setColour(juce::Colours::black);
            g.fillEllipse(r.translated(0.0f, 1.0f));
            g.setGradientFill(juce::ColourGradient(rgb(0x8a857c), r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.35f,
                                                   rgb(0x3b3833), r.getRight(), r.getBottom(), true));
            g.fillEllipse(r);
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
        // The name plate keeps its faded outline where the copper was cut away round it.
        g.setColour(outline);
        for (const auto& k : keeps)
            if (k.outlined)
            {
                if (k.round)
                    g.drawEllipse(k.area, 1.0f);
                else
                    g.drawRoundedRectangle(k.area, 4.0f, 1.0f);
            }
    }

    void EtchedBoard::paintGround(juce::Graphics& g) const
    {
        // A ground ring round the sequencer, as real boards have along their edges; it carries no signal, so it never
        // lights. It leaves the board just under the glass's top edge on both sides.
        const float top = glassTop + 8.0f;
        const std::vector<juce::Point<float>> ring {
            { 34.0f, top }, { 22.0f, top + 12.0f }, { 22.0f, 776.0f }, { 38.0f, 792.0f },
            { 1242.0f, 792.0f }, { 1258.0f, 776.0f }, { 1258.0f, top + 12.0f }, { 1246.0f, top } };
        g.setColour(copper);
        g.strokePath(tracePath(ring), juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (const auto end : { ring.front(), ring.back() })
        {
            g.setColour(padColour);
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
