#include "EtchedCircuit.h"

#include "LibraryData.h"
#include "Theme.h"
#include "Text.h"

namespace cs808
{
    namespace
    {
        juce::Colour rgba(int r, int g, int b, float a) { return juce::Colour(static_cast<juce::uint8>(r), static_cast<juce::uint8>(g), static_cast<juce::uint8>(b), a); }

        /** The mockup's random numbers (mulberry32), so the board is drawn as the mockup drew it. */
        struct Random32
        {
            std::uint32_t s;
            double next() noexcept
            {
                s += 0x6D2B79F5u;
                std::uint32_t t = (s ^ (s >> 15)) * (1u | s);
                t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
                return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
            }
        };

        struct UV { float u, v; };
        struct GenWire { std::vector<UV> pts; };
        struct GenPart { char type; float u, v; bool vertical; };

        /** One column's circuit in its own coordinates: u across, v from the trigger (0) to the output (1). */
        void generate(char kind, std::uint32_t seed, std::vector<GenWire>& w, std::vector<GenPart>& p)
        {
            Random32 rnd { seed };
            const auto R = [&] { return static_cast<float>(rnd.next()); };
            if (kind == 't') // the master column: the trigger and the accent
            {
                w.push_back({ { { .3f, .2f }, { .3f, .5f }, { .7f, .5f }, { .7f, 0.0f } } });
                p.push_back({ 'Q', .3f, .5f, false }); p.push_back({ 'R', .5f, .5f, false }); p.push_back({ 'R', .7f, .25f, true });
                w.push_back({ { { .3f, .5f }, { .3f, .8f }, { .55f, .8f } } });
                p.push_back({ 'P', .3f, .66f, true }); p.push_back({ 'S', .62f, .8f, false });
                w.push_back({ { { .3f, .2f }, { .15f, .2f } } });
                p.push_back({ 'G', .15f, .23f, false });
                return;
            }
            const float sx = .46f + R() * .14f, tx = .16f + R() * .1f;
            w.push_back({ { { tx, 0.0f }, { tx, .2f } } });
            p.push_back({ 'R', tx, .09f, true }); p.push_back({ 'Q', tx, .22f, false });
            w.push_back({ { { tx, .22f }, { sx, .22f } } });
            p.push_back({ 'R', (tx + sx) / 2.0f + .03f, .22f, false });
            w.push_back({ { { sx, .22f }, { sx, .74f } } });
            for (const float v : { .32f, .47f, .62f })
                if (R() < .7f)
                    p.push_back({ R() < .5f ? 'C' : 'R', sx, v, true });
            const int branches = 1 + static_cast<int>(R() * 2.0f);
            for (int i = 0; i < branches; ++i)
            {
                const float v = .28f + R() * .3f, ex = std::min(.9f, sx + .2f + R() * .14f);
                w.push_back({ { { sx, v }, { ex, v }, { ex, v + .1f } } });
                p.push_back({ R() < .5f ? 'R' : 'C', (sx + ex) / 2.0f + .02f, v, false });
                p.push_back({ 'G', ex, v + .1f, false });
            }
            if (kind == 'r') // a ring: the bridged T
            {
                const float lx = std::max(.07f, sx - .3f);
                w.push_back({ { { sx, .38f }, { lx, .38f }, { lx, .58f }, { sx, .58f } } });
                p.push_back({ 'C', lx, .48f, true }); p.push_back({ 'R', (lx + sx) / 2.0f, .38f, false }); p.push_back({ 'C', (lx + sx) / 2.0f, .58f, false });
            }
            else if (kind == 'n') // noise
            {
                const float lx = std::max(.1f, sx - .3f);
                p.push_back({ 'N', lx, .42f, false });
                w.push_back({ { { lx, .47f }, { lx, .62f }, { sx, .62f } } });
                p.push_back({ 'R', (lx + sx) / 2.0f, .62f, false });
            }
            else // metal: a square wave in
            {
                const float lx = std::max(.1f, sx - .3f);
                w.push_back({ { { lx, .3f }, { lx, .55f }, { sx, .55f } } });
                p.push_back({ 'S', lx, .28f, false }); p.push_back({ 'C', lx, .44f, true }); p.push_back({ 'R', (lx + sx) / 2.0f, .55f, false });
            }
            p.push_back({ 'A', sx, .77f, true });
            w.push_back({ { { sx, .8f }, { sx, 1.0f } } });
            p.push_back({ 'P', sx, .9f, true });
            const float fx = std::min(.93f, sx + .22f);
            w.push_back({ { { sx, .69f }, { fx, .69f }, { fx, .86f }, { sx, .86f } } });
            p.push_back({ 'R', fx, .775f, true });
        }

        constexpr char kindOf[12] { 't', 'r', 'n', 'r', 'r', 'r', 'r', 'n', 'm', 'm', 'm', 'm' };
    }

    // ---- Building -------------------------------------------------------------------------------------------------

    void EtchedCircuit::addWire(std::vector<juce::Point<float>> pts, int owner, bool inCell)
    {
        Wire w { std::move(pts), owner, inCell, 0.0f };
        for (std::size_t i = 1; i < w.pts.size(); ++i)
            w.length += w.pts[i - 1].getDistanceFrom(w.pts[i]);
        wires.push_back(std::move(w));
    }

    void EtchedCircuit::build(const std::vector<Column>& columns, float top, float bottom)
    {
        wires.clear();
        paths.clear();
        parts.clear();
        int n = 1;
        for (std::size_t l = 0; l < columns.size() && l < 12; ++l)
        {
            const auto& c = columns[l];
            const float x = c.x + 4.0f, w = c.w - 8.0f, h = bottom - top;
            const auto map = [&](UV uv) { return juce::Point<float>(x + uv.u * w, top + (1.0f - uv.v) * h); }; // the trigger at the foot
            std::vector<GenWire> gw;
            std::vector<GenPart> gp;
            const int owner = static_cast<int>(l);
            generate(kindOf[l], static_cast<std::uint32_t>(((l == 0 ? -1 : owner) + 7) * 97 + 13), gw, gp);
            for (const auto& wire : gw)
            {
                std::vector<juce::Point<float>> pts;
                for (const auto& uv : wire.pts)
                    pts.push_back(map(uv));
                addWire(std::move(pts), owner, true);
            }
            const float k = juce::jlimit(5.0f, 15.0f, std::min(w, h * 0.55f) / 7.0f);
            for (const auto& gpart : gp)
            {
                Part part { gpart.type, map({ gpart.u, gpart.v }), gpart.vertical, k, owner, {} };
                if (gpart.type == 'Q')
                    part.name = "Q" + juce::String(40 + n++);
                else if (gpart.type == 'A')
                    part.name = "IC" + juce::String(n++ % 9 + 1);
                else
                    part.name = (gpart.type == 'P' ? juce::String("VR") : juce::String::charToString(static_cast<juce::juce_wchar>(gpart.type))) + juce::String(100 + n++);
                parts.push_back(part);
            }
        }
        if (! columns.empty())
        {
            const float x0 = columns.front().x, x1 = columns.back().x + columns.back().w;
            addWire({ { x0, bottom }, { x1, bottom } }, triggerBus, false);
            addWire({ { x0 + 20.0f, top }, { x1, top } }, mixBus, false);
            for (int i = 0; i < 6; ++i) // short stubs off the trigger bus, seen through the glass
            {
                const float sx = 120.0f + static_cast<float>(i) * 190.0f;
                addWire({ { sx, bottom }, { sx, bottom + 12.0f }, { sx + 10.0f, bottom + 22.0f }, { sx + 80.0f, bottom + 22.0f } }, triggerBus, false);
            }
        }
        // Copper turns at 45 degrees: each corner cut back up to 7 units. Made once, drawn every frame.
        for (const auto& w : wires)
        {
            juce::Path p;
            p.startNewSubPath(w.pts.front());
            for (std::size_t i = 1; i < w.pts.size(); ++i)
            {
                if (i + 1 < w.pts.size())
                {
                    const auto a = w.pts[i - 1], b = w.pts[i], c = w.pts[i + 1];
                    const float d1 = a.getDistanceFrom(b), d2 = b.getDistanceFrom(c), r = std::min({ 7.0f, d1 * 0.5f, d2 * 0.5f });
                    if (d1 > 0.0f && d2 > 0.0f)
                    {
                        p.lineTo(b - (b - a) / d1 * r);
                        p.lineTo(b + (c - b) / d2 * r);
                        continue;
                    }
                }
                p.lineTo(w.pts[i]);
            }
            paths.push_back(std::move(p));
        }
    }

    // ---- Drawing ----------------------------------------------------------------------------------------------

    juce::Point<float> EtchedCircuit::pointAlong(const Wire& w, float d) const
    {
        for (std::size_t i = 1; i < w.pts.size(); ++i)
        {
            const float seg = w.pts[i - 1].getDistanceFrom(w.pts[i]);
            if (d <= seg && seg > 0.0f)
                return w.pts[i - 1] + (w.pts[i] - w.pts[i - 1]) * (d / seg);
            d -= seg;
        }
        return w.pts.back();
    }

    namespace
    {
        float widthOf(bool inCell) { return inCell ? 2.0f : 1.6f; }
        juce::PathStrokeType stroke(float width) { return juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }
    }

    void EtchedCircuit::paintStill(juce::Graphics& g) const
    {
        g.setColour(boardLook().copper);
        for (std::size_t i = 0; i < wires.size(); ++i)
            g.strokePath(paths[i], stroke(widthOf(wires[i].inCell)));
        for (const auto& p : parts)
            paintPart(g, p, 0.0f);
        // a pad at every end of copper
        for (const auto& w : wires)
            for (const auto& q : { w.pts.front(), w.pts.back() })
            {
                g.setColour(boardLook().pad);
                g.fillEllipse(juce::Rectangle<float>(5.2f, 5.2f).withCentre(q));
                g.setColour(rgba(20, 18, 16, 0.9f));
                g.fillEllipse(juce::Rectangle<float>(2.0f, 2.0f).withCentre(q));
            }
        paintNames(g);
    }

    bool EtchedCircuit::paintLit(juce::Graphics& g, double nowMs, const Energy& energy)
    {
        const float dt = static_cast<float>(juce::jlimit(0.0, 100.0, nowMs - lastNow));
        lastNow = nowMs;
        std::array<float, 128> lit {};
        for (const auto& w : wires)
            lit[static_cast<std::size_t>(w.owner)] = juce::jlimit(0.0f, 1.0f, energy(w.owner));
        bool any = false;
        for (std::size_t i = 0; i < heat.size(); ++i)
        {
            heat[i] = std::max(lit[i], heat[i] * std::exp(-dt / 1600.0f));
            any = any || heat[i] > 0.02f;
        }
        // the afterglow of the last bar; the light in the theme's colours (D307: yellow on Silver)
        const auto& look = boardLook();
        for (std::size_t i = 0; i < wires.size(); ++i)
            if (const float h = heat[static_cast<std::size_t>(wires[i].owner)]; h > 0.02f)
            {
                g.setColour(look.heat.withAlpha(0.14f * h));
                g.strokePath(paths[i], stroke(widthOf(wires[i].inCell) + 4.0f));
            }
        // lit copper, and current: dots running along it, faster for more
        for (std::size_t i = 0; i < wires.size(); ++i)
        {
            const auto& w = wires[i];
            const float e = lit[static_cast<std::size_t>(w.owner)];
            if (e < 0.03f)
                continue;
            g.setColour(look.flowWire.withAlpha(0.06f * e));
            g.strokePath(paths[i], stroke(widthOf(w.inCell) + 4.0f));
            g.setColour(look.flowWire.withAlpha(0.42f * e));
            g.strokePath(paths[i], stroke(widthOf(w.inCell) + 0.5f));
            if (e < 0.05f)
                continue;
            g.setColour(look.flowDot.withAlpha(std::min(1.0f, e * 1.3f)));
            const float spacing = 11.0f, offset = static_cast<float>(std::fmod(nowMs * (0.02 + 0.07 * e), spacing));
            const float r = w.inCell ? 1.4f : 1.15f;
            for (float d = offset; d < w.length; d += spacing)
                g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(pointAlong(w, d)));
        }
        // the parts lit, and the transistors' lights
        for (const auto& p : parts)
            if (const float e = lit[static_cast<std::size_t>(p.owner)]; e > 0.05f)
                paintPart(g, p, e);
        return any;
    }

    void EtchedCircuit::paintPart(juce::Graphics& g, const Part& p, float e) const
    {
        juce::Graphics::ScopedSaveState keep(g);
        g.addTransform(juce::AffineTransform::rotation(p.vertical ? juce::MathConstants<float>::halfPi : 0.0f).translated(p.at));
        const float k = p.k;
        const auto pad = [&](float x, float y)
        {
            g.setColour(boardLook().pad.withMultipliedAlpha(0.75f / 0.7f));
            g.fillEllipse(juce::Rectangle<float>(4.8f, 4.8f).withCentre({ x, y }));
        };
        const auto ink = e > 0.05f ? boardLook().flowDot.withAlpha(0.45f + 0.55f * e) : boardLook().silk;
        const auto box = [&](float w, float h)
        {
            g.setColour(ink);
            g.drawRect(juce::Rectangle<float>(w, h).withCentre({}), 1.0f);
        };
        switch (p.type)
        {
            case 'R': case 'P':
                box(k * 2.1f, k * 0.76f); pad(-k * 1.25f, 0.0f); pad(k * 1.25f, 0.0f);
                if (p.type == 'P') { g.setColour(ink); g.drawEllipse(juce::Rectangle<float>(k * 0.6f, k * 0.6f).withCentre({}), 1.0f); }
                break;
            case 'C': box(k * 0.64f, k * 1.2f); pad(-k * 0.4f, 0.0f); pad(k * 0.4f, 0.0f); break;
            case 'Q':
            {
                juce::Path d;
                d.addCentredArc(0.0f, 0.0f, k * 0.85f, k * 0.85f, 0.0f, juce::MathConstants<float>::pi * 0.65f, juce::MathConstants<float>::pi * 2.35f, true);
                d.closeSubPath();
                g.setColour(ink);
                g.strokePath(d, juce::PathStrokeType(1.0f));
                pad(-k * 0.45f, 0.0f); pad(0.0f, 0.0f); pad(k * 0.45f, 0.0f);
                // an LED by every transistor: the board's own lights
                const auto led = juce::Point<float>(k * 1.4f, -k * 0.9f);
                if (e > 0.1f)
                {
                    g.setColour(rgba(255, 150, 60, 0.25f * e));
                    g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(led));
                }
                g.setColour(rgba(255, static_cast<int>(120 + 80 * e), 60, 0.15f + 0.85f * e));
                g.fillEllipse(juce::Rectangle<float>(4.4f, 4.4f).withCentre(led));
                break;
            }
            case 'A':
                box(k * 2.2f, k * 2.8f);
                for (int i = 0; i < 4; ++i)
                {
                    pad(-k * 1.1f, -k * 1.05f + static_cast<float>(i) * k * 0.7f);
                    pad(k * 1.1f, -k * 1.05f + static_cast<float>(i) * k * 0.7f);
                }
                if (e > 0.1f)
                {
                    g.setColour(boardLook().flowDot.withAlpha(0.25f * e));
                    g.fillRect(juce::Rectangle<float>(k * 2.2f, k * 2.8f).withCentre({}));
                }
                break;
            case 'G':
                pad(0.0f, 0.0f);
                g.setColour(ink);
                g.drawEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre({}), 1.0f);
                break;
            default: // N, S: a small module
                box(k * 1.8f, k * 1.8f); pad(-k * 0.6f, -k * 0.6f); pad(k * 0.6f, k * 0.6f);
                break;
        }
    }

    void EtchedCircuit::paintNames(juce::Graphics& g) const
    {
        // Each part's name printed beside it where it touches no part, no other name and no control; else left out.
        const float fs = 6.0f;
        const auto font = monoFont(fs);
        g.setFont(font);
        g.setColour(boardLook().silkText);
        const auto boxOf = [](const Part& p)
        {
            const float w = (p.type == 'A' ? 1.2f : (p.type == 'R' || p.type == 'P') ? 1.35f : 1.0f) * p.k;
            const float h = (p.type == 'A' ? 1.5f : (p.type == 'R' || p.type == 'P') ? 0.55f : 1.0f) * p.k;
            return p.vertical ? juce::Rectangle<float>(p.at.x - h, p.at.y - w, h * 2.0f, w * 2.0f)
                              : juce::Rectangle<float>(p.at.x - w, p.at.y - h, w * 2.0f, h * 2.0f);
        };
        auto used = taken;
        for (const auto& p : parts)
            used.push_back(boxOf(p));
        const auto hits = [&](juce::Rectangle<float> r) { return std::any_of(used.begin(), used.end(), [&](const auto& t) { return t.intersects(r); }); };
        for (const auto& p : parts)
        {
            if (p.type == 'G')
                continue;
            const float w = static_cast<float>(p.name.length()) * fs * 0.6f + 1.0f;
            const auto b = boxOf(p);
            const juce::Point<float> spots[] { { b.getRight() + 2.0f, b.getCentreY() - fs * 0.5f }, { b.getX() - 2.0f - w, b.getCentreY() - fs * 0.5f },
                                              { b.getCentreX() - w * 0.5f, b.getY() - 2.0f - fs }, { b.getCentreX() - w * 0.5f, b.getBottom() + 2.0f } };
            for (const auto& s : spots)
            {
                const auto r = juce::Rectangle<float>(s.x, s.y, w, fs + 1.0f);
                if (! hits(r))
                {
                    shapedText(g, p.name, r, juce::Justification::centredLeft, false);
                    used.push_back(r);
                    break;
                }
            }
        }
    }

    // ---- The envelopes ----------------------------------------------------------------------------------------

    BoardEnvelopes::BoardEnvelopes()
    {
        const auto json = juce::JSON::parse(juce::String::fromUTF8(LibraryData::board_envelopes_json, LibraryData::board_envelopes_jsonSize));
        static const char* const ids[12] { "", "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH" };
        for (int l = 1; l < 12; ++l)
            if (const auto* list = json.getProperty(ids[l], {}).getArray())
                for (const auto& v : *list)
                    env[static_cast<std::size_t>(l)].push_back(static_cast<float>(static_cast<double>(v)));
    }

    float BoardEnvelopes::brightness(int lane, double ms) const noexcept
    {
        if (lane < 1 || lane > 11 || ms < 0.0)
            return 0.0f;
        const auto& e = env[static_cast<std::size_t>(lane)];
        const auto i = static_cast<std::size_t>(ms);
        if (i >= e.size())
            return 0.0f;
        // The level in decibels, 36 dB from full to dark: a quiet tail still glows, as it is still heard.
        const float level = e[i];
        return level <= 0.0f ? 0.0f : juce::jlimit(0.0f, 1.0f, 1.0f + 20.0f * std::log10(level) / 36.0f);
    }
}
