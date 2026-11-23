#include "LidView.h"

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "../Kit.h"
#include "../Processor.h"
#include "LibraryData.h"
#include "Theme.h"
#include "Widgets.h"

/*
    Under the Lid (D263, D264): the whole board as one reduced schematic, ported from the mockup that the user chose
    (docs/design/mockups/coolsynth-808.html in CoolSynth's repository, "buildLid2"): every drum's circuit reduced to the
    parts that make its sound, under the trigger line, the noise and the six oscillators they share, down to the mix.

    The current is drawn on the wires as dots, lit by the drums the app strikes: travelling for a pulse or a charge,
    shaking for sound, thicker for more; a wire's tint is its voltage. As in the mockup, the currents are drawn by hand
    from what each circuit does; the engine's own (a tap in its solver) and each drum complete are the next steps.
*/
namespace cs808
{
    namespace
    {
        constexpr float boardW = 1200.0f, boardH = 462.0f;
        constexpr float Y0 = 96.0f, TRIG = 10.0f, MIX = 446.0f, NW = 44.0f, NP = 52.0f, SUM = 58.0f, HI = 76.0f, LO = 84.0f;

        enum class Kind { pulse, ac, nz, env, burst, volt };
        enum class Mode { pulse, ac, env, burst, dc };

        using Pt = juce::Point<float>;
        using Pts = std::vector<Pt>;

        struct Part
        {
            juce::Path stroke;    // drawn as lines
            juce::Path fill;      // arrow heads
            std::vector<std::pair<Pt, float>> circles;
            juce::Rectangle<float> box; // a filled box (an oscillator, an IC)
            juce::String name;
            int lane = -1;
        };

        struct Text
        {
            Pt at;
            juce::String text;
            juce::Justification justification = juce::Justification::centred;
            bool strong = false;
        };

        struct Flow
        {
            int key = 0;        // the lane it belongs to; -1 every strike (the trigger's source); -2 always (idle)
            int part = 0;       // 0 trig, 1 ring, 2 env, 3 sig, 4 out, 5 cut
            Kind kind = Kind::pulse;
            Mode mode = Mode::pulse; // a voltage tint's
            Pts points;
            float level = 0.4f;
            float idle = 0.0f;
            float frequency = 8.0f;
            bool burstEnvelope = false;
            // animation
            double t0 = -1.0, duration = 300.0;
            float offset = 0.0f, phase = 0.0f;
            float length = 0.0f;
        };

        juce::Path polyline(const Pts& p)
        {
            juce::Path path;
            if (p.empty())
                return path;
            path.startNewSubPath(p[0]);
            for (std::size_t i = 1; i < p.size(); ++i)
                path.lineTo(p[i]);
            return path;
        }

        float polylineLength(const Pts& p)
        {
            float l = 0.0f;
            for (std::size_t i = 1; i < p.size(); ++i)
                l += p[i - 1].getDistanceFrom(p[i]);
            return l;
        }

        Pt pointAlong(const Pts& p, float s)
        {
            for (std::size_t i = 1; i < p.size(); ++i)
            {
                const float seg = p[i - 1].getDistanceFrom(p[i]);
                if (s <= seg || i + 1 == p.size())
                    return seg > 0.0f ? p[i - 1] + (p[i] - p[i - 1]) * juce::jlimit(0.0f, 1.0f, s / seg) : p[i];
                s -= seg;
            }
            return p.empty() ? Pt() : p.back();
        }

        void arrowHead(juce::Path& path, Pt tip, Pt from)
        {
            const auto d = tip - from;
            const float l = std::hypot(d.x, d.y);
            if (l <= 0.0f)
                return;
            const float ux = d.x / l, uy = d.y / l;
            path.addTriangle(tip, { tip.x - ux * 4 - uy * 1.8f, tip.y - uy * 4 + ux * 1.8f }, { tip.x - ux * 4 + uy * 1.8f, tip.y - uy * 4 - ux * 1.8f });
        }

        /** The drawing: the mockup's builder, kept close to it so the two can be compared line by line. */
        struct Board
        {
            std::vector<juce::Path> wires;
            std::vector<Pt> dots;
            std::vector<Part> parts;
            std::vector<Text> texts;
            std::vector<Flow> flows;

            struct Sheet
            {
                Board& b;
                float ox, oy;
                int lane;

                Pt g(float x, float y) const { return { ox + x, oy + y }; }
                Pts gs(std::initializer_list<Pt> pts) const
                {
                    Pts r;
                    for (const auto& p : pts)
                        r.push_back(g(p.x, p.y));
                    return r;
                }
                void w(std::initializer_list<Pt> pts) const { b.wires.push_back(polyline(gs(pts))); }
                void wv(const Pts& pts) const { Pts r; for (const auto& p : pts) r.push_back(g(p.x, p.y)); b.wires.push_back(polyline(r)); }
                void dot(float x, float y) const { b.dots.push_back(g(x, y)); }
                void tx(float x, float y, const juce::String& s, juce::Justification j = juce::Justification::centred, bool strong = false) const
                {
                    b.texts.push_back({ g(x, y), s, j, strong });
                }
                Flow& f(int part, Kind kind, Pts pts, float level = 0.4f, float frequency = 8.0f) const
                {
                    Flow fl;
                    fl.key = lane;
                    fl.part = part;
                    fl.kind = kind;
                    for (auto& p : pts)
                        p = g(p.x, p.y);
                    fl.points = std::move(pts);
                    fl.level = level;
                    fl.frequency = frequency;
                    b.flows.push_back(std::move(fl));
                    return b.flows.back();
                }
                Flow& v(int part, Mode mode, Pts pts, float frequency = 8.0f) const
                {
                    auto& fl = f(part, Kind::volt, std::move(pts), 0.4f, frequency);
                    fl.mode = mode;
                    return fl;
                }
                Part& add(const juce::String& name) const
                {
                    b.parts.push_back({});
                    b.parts.back().name = name;
                    b.parts.back().lane = lane;
                    return b.parts.back();
                }
                static void zig(juce::Path& p, Pt o, bool h, float L)
                {
                    const float a = (L - 12.0f) / 2.0f;
                    p.startNewSubPath(o);
                    if (h)
                    {
                        auto c = o + Pt(a, 0);
                        p.lineTo(c);
                        for (const auto& d : { Pt(1.5f, -3), Pt(3, 6), Pt(3, -6), Pt(3, 6), Pt(1.5f, -3), Pt(a, 0) })
                            p.lineTo(c += d);
                    }
                    else
                    {
                        auto c = o + Pt(0, a);
                        p.lineTo(c);
                        for (const auto& d : { Pt(-3, 1.5f), Pt(6, 3), Pt(-6, 3), Pt(6, 3), Pt(-3, 1.5f), Pt(0, a) })
                            p.lineTo(c += d);
                    }
                }
                void R(float x, float y, bool h, float L, const juce::String& name) const { zig(add(name).stroke, g(x, y), h, L); }
                void C(float x, float y, bool h, float L, const juce::String& name) const
                {
                    auto& p = add(name).stroke;
                    const float a = (L - 3.0f) / 2.0f;
                    const auto o = g(x, y);
                    if (h)
                    {
                        p.addLineSegment({ o, o + Pt(a, 0) }, 0);
                        p.addLineSegment({ o + Pt(a, -4), o + Pt(a, 4) }, 0);
                        p.addLineSegment({ o + Pt(a + 3, -4), o + Pt(a + 3, 4) }, 0);
                        p.addLineSegment({ o + Pt(a + 3, 0), o + Pt(L, 0) }, 0);
                    }
                    else
                    {
                        p.addLineSegment({ o, o + Pt(0, a) }, 0);
                        p.addLineSegment({ o + Pt(-4, a), o + Pt(4, a) }, 0);
                        p.addLineSegment({ o + Pt(-4, a + 3), o + Pt(4, a + 3) }, 0);
                        p.addLineSegment({ o + Pt(0, a + 3), o + Pt(0, L) }, 0);
                    }
                }
                void D(float x, float y, float L, const juce::String& name) const // anode on the left
                {
                    auto& part = add(name);
                    const float a = (L - 6.0f) / 2.0f;
                    const auto o = g(x, y);
                    part.stroke.addLineSegment({ o, o + Pt(a, 0) }, 0);
                    part.stroke.addTriangle(o + Pt(a, -3.5f), o + Pt(a + 6, 0), o + Pt(a, 3.5f));
                    part.stroke.addLineSegment({ o + Pt(a + 6, -3.5f), o + Pt(a + 6, 3.5f) }, 0);
                    part.stroke.addLineSegment({ o + Pt(a + 6, 0), o + Pt(L, 0) }, 0);
                }
                void Dv(float x, float y, float L, const juce::String& name) const // anode on top
                {
                    auto& part = add(name);
                    const float a = (L - 6.0f) / 2.0f;
                    const auto o = g(x, y);
                    part.stroke.addLineSegment({ o, o + Pt(0, a) }, 0);
                    part.stroke.addTriangle(o + Pt(-3.5f, a), o + Pt(0, a + 6), o + Pt(3.5f, a));
                    part.stroke.addLineSegment({ o + Pt(-3.5f, a + 6), o + Pt(3.5f, a + 6) }, 0);
                    part.stroke.addLineSegment({ o + Pt(0, a + 6), o + Pt(0, L) }, 0);
                }
                void POT(float x, float y, bool h, float L, const juce::String& name, const juce::String& label = {}) const
                {
                    auto& part = add(name);
                    const auto o = g(x, y);
                    zig(part.stroke, o, h, L);
                    const float m = L / 2.0f;
                    const Pt from = h ? o + Pt(m - 6, 6) : o + Pt(-6, m + 6);
                    const Pt tip = h ? o + Pt(m + 6, -6) : o + Pt(6, m - 6);
                    part.stroke.addLineSegment({ from, tip }, 0);
                    arrowHead(part.fill, tip, from);
                    if (label.isNotEmpty())
                    {
                        if (h)
                            tx(x + m, y - 9, label, juce::Justification::centred, true);
                        else
                            tx(x - 9, y + m + 2, label, juce::Justification::centredRight, true);
                    }
                }
                void Q(float x, float cy, int m, bool pnp, const juce::String& name) const
                {
                    auto& part = add(name);
                    const auto gx = ox + x, gy = oy + cy;
                    const float cx = gx - 4.0f * m, bx = cx - 3.0f * m;
                    auto& p = part.stroke;
                    p.addLineSegment({ bx, gy - 5, bx, gy + 5 }, 0);
                    p.addLineSegment({ cx - 12.0f * m, gy, bx, gy }, 0);
                    p.startNewSubPath(bx, gy - 2.5f); p.lineTo(gx, gy - 7); p.lineTo(gx, gy - 12);
                    p.startNewSubPath(bx, gy + 2.5f); p.lineTo(gx, gy + 7); p.lineTo(gx, gy + 12);
                    part.circles.push_back({ Pt(cx, gy), 8.5f });
                    if (pnp)
                        arrowHead(part.fill, { bx, gy - 2.5f }, { gx, gy - 7 });
                    else
                        arrowHead(part.fill, { gx, gy + 7 }, { bx, gy + 2.5f });
                }
                void OP(float x, float y, const juce::String& name) const
                {
                    auto& p = add(name).stroke;
                    const auto o = g(x, y);
                    p.startNewSubPath(o); p.lineTo(o + Pt(0, 24)); p.lineTo(o + Pt(26, 12)); p.closeSubPath();
                    p.addLineSegment({ o + Pt(3, 6), o + Pt(7, 6) }, 0);
                    p.addLineSegment({ o + Pt(3, 18), o + Pt(7, 18) }, 0);
                    p.addLineSegment({ o + Pt(5, 16), o + Pt(5, 20) }, 0);
                }
                void G(float x, float y) const
                {
                    const auto o = g(x, y);
                    juce::Path p;
                    p.addLineSegment({ o, o + Pt(0, 3) }, 0);
                    p.addLineSegment({ o + Pt(-4, 3), o + Pt(4, 3) }, 0);
                    p.addLineSegment({ o + Pt(-2.5f, 5), o + Pt(2.5f, 5) }, 0);
                    p.addLineSegment({ o + Pt(-1, 7), o + Pt(1, 7) }, 0);
                    b.wires.push_back(p);
                }
                void BOX(float x, float y, float w2, float h2, const juce::String& name, const juce::String& label = {}) const
                {
                    auto& part = add(name);
                    part.box = { ox + x, oy + y, w2, h2 };
                    if (label.isNotEmpty())
                        tx(x + w2 / 2, y + h2 / 2 + 2.3f, label);
                }
                void glyph(std::initializer_list<Pt> pts) const
                {
                    auto& part = add({});
                    part.stroke = polyline(gs(pts));
                }
            };

            // One drum's strip: the trigger enters at the top through its switch and runs down the left rail; the
            // sound leaves down the right rail to the mix.
            struct Voice
            {
                Sheet s;
                float fd = 0.0f;
                std::vector<std::pair<float, float>> cuts;
                float railTop = -1.0f;
                float leftEnd = 30.0f;

                void sw(const juce::String& name)
                {
                    s.w({ { 50, TRIG - Y0 }, { 50, 2 } });
                    s.dot(50, TRIG - Y0);
                    s.Q(50, 14, 1, true, name);
                    s.w({ { 34, 14 }, { 30, 14 } });
                    s.dot(28.5f, 14);
                    s.w({ { 50, 26 }, { 50, 30 }, { 10, 30 } });
                }

                struct Res { bool inR = false; juce::String inName, op, c1, c2, r, gnd, gndLabel, fb, fbLabel; bool gndPot = false, fbPot = false; float fd = 8; bool noJoin = false; };
                void res(float y, const Res& o)
                {
                    leftEnd = std::max(leftEnd, y + 70);
                    s.dot(10, y + 70);
                    s.w({ { 10, y + 70 }, { 14, y + 70 } });
                    if (o.inR) s.R(14, y + 70, true, 22, o.inName); else s.C(14, y + 70, true, 22, o.inName);
                    s.w({ { 36, y + 70 }, { 42, y + 70 } });
                    s.OP(42, y + 52, o.op);
                    s.w({ { 42, y + 58 }, { 30, y + 58 }, { 30, y + 28 }, { 32, y + 28 } });
                    s.C(32, y + 28, true, 14, o.c1);
                    s.w({ { 46, y + 28 }, { 64, y + 28 } });
                    s.C(64, y + 28, true, 14, o.c2);
                    s.w({ { 78, y + 28 }, { 80, y + 28 }, { 80, y + 64 } });
                    s.dot(55, y + 28); s.dot(30, y + 42); s.dot(80, y + 42); s.dot(80, y + 64);
                    s.w({ { 30, y + 42 }, { 44, y + 42 } });
                    s.R(44, y + 42, true, 22, o.r);
                    s.w({ { 66, y + 42 }, { 80, y + 42 } });
                    s.w({ { 68, y + 64 }, { 94, y + 64 } });
                    s.w({ { 55, y + 28 }, { 55, y + 10 }, { 50, y + 10 } });
                    if (o.gndPot) s.POT(28, y + 10, true, 22, o.gnd, o.gndLabel); else s.R(28, y + 10, true, 22, o.gnd);
                    s.w({ { 28, y + 10 }, { 24, y + 10 } });
                    s.G(24, y + 10);
                    if (o.fb.isNotEmpty())
                    {
                        s.dot(55, y + 10);
                        s.w({ { 55, y + 10 }, { 58, y + 10 } });
                        if (o.fbPot) s.POT(58, y + 10, true, 22, o.fb, o.fbLabel); else s.R(58, y + 10, true, 22, o.fb);
                        s.w({ { 80, y + 10 }, { 80, y + 28 } });
                        s.dot(80, y + 28);
                    }
                    s.f(0, Kind::pulse, { { 10, y + 70 }, { 42, y + 70 } });
                    s.f(1, Kind::ac, { { 42, y + 58 }, { 30, y + 58 }, { 30, y + 28 }, { 80, y + 28 }, { 80, y + 64 }, { 68, y + 64 } }, 0.55f, o.fd);
                    s.f(1, Kind::ac, { { 55, y + 28 }, { 55, y + 10 }, { 24, y + 10 } }, 0.3f, o.fd);
                    if (o.fb.isNotEmpty())
                        s.f(1, Kind::ac, { { 80, y + 28 }, { 80, y + 10 }, { 55, y + 10 } }, 0.3f, o.fd);
                    s.f(1, Kind::ac, { { 80, y + 64 }, { 94, y + 64 } }, 0.4f, o.fd);
                    s.v(1, Mode::ac, { { 80, y + 28 }, { 80, y + 64 }, { 68, y + 64 } }, o.fd);
                    s.v(1, Mode::ac, { { 80, y + 64 }, { 94, y + 64 } }, o.fd);
                    if (! o.noJoin)
                    {
                        railTop = railTop < 0 ? y + 64 : std::min(railTop, y + 64);
                        s.dot(94, y + 64);
                    }
                }

                enum class Feed { diode, pot, box, wire };
                struct Gate { float tapX = -1, tapY = 0; Feed feed = Feed::diode; juce::String feedName, feedLabel, cap, r, q, decayName, decayLabel; Pt decayAt { -1, -1 }; juce::Justification decayJust = juce::Justification::centred; Kind sig = Kind::nz; float fd = 8; bool burst = false; bool noJoin = false; };
                void gate(float y, const Gate& o)
                {
                    leftEnd = std::max(leftEnd, y + 14);
                    s.dot(10, y + 14);
                    s.w({ { 10, y + 14 }, { 12, y + 14 } });
                    if (o.feed == Feed::pot) s.POT(12, y + 14, true, 12, o.feedName, o.feedLabel);
                    else if (o.feed == Feed::box) s.BOX(11, y + 8, 14, 12, o.feedName, o.feedLabel);
                    else if (o.feed == Feed::wire) s.w({ { 12, y + 14 }, { 24, y + 14 } });
                    else s.D(12, y + 14, 12, o.feedName);
                    s.w({ { 24, y + 14 }, { 26, y + 14 } });
                    if (o.cap.isNotEmpty())
                    {
                        s.dot(26, y + 14);
                        s.C(26, y + 14, false, 14, o.cap);
                        s.G(26, y + 28);
                    }
                    if (o.decayName.isNotEmpty())
                    {
                        s.w({ { 26, y + 14 }, { 42, y + 14 } });
                        s.dot(38, y + 14);
                        s.POT(38, y + 14, false, 18, o.decayName);
                        s.G(38, y + 32);
                        const auto at = o.decayAt.x < 0 ? Pt(38, y + 45) : o.decayAt;
                        s.tx(at.x, at.y, o.decayLabel, o.decayJust, true);
                        s.R(42, y + 14, true, 16, o.r);
                        s.w({ { 58, y + 14 }, { 60, y + 14 } });
                    }
                    else
                    {
                        s.w({ { 26, y + 14 }, { 34, y + 14 } });
                        s.R(34, y + 14, true, 22, o.r);
                        s.w({ { 56, y + 14 }, { 60, y + 14 } });
                    }
                    s.Q(60, y + 26, -1, false, o.q);
                    s.w({ { 60, y + 38 }, { 60, y + 46 }, { 94, y + 46 } });
                    if (o.tapX >= 0)
                    {
                        const float ty = o.tapY - Y0;
                        s.w({ { o.tapX, ty }, { o.tapX, y + 26 }, { 76, y + 26 } });
                        s.dot(o.tapX, ty);
                        auto& idle = s.f(3, o.sig, { { o.tapX, ty }, { o.tapX, y + 26 }, { 76, y + 26 } }, 0.4f, o.fd);
                        idle.key = -2;
                        idle.idle = 0.06f;
                    }
                    s.f(2, o.burst ? Kind::burst : Kind::env, { { 10, y + 14 }, { 60, y + 14 } });
                    s.v(2, o.burst ? Mode::burst : Mode::env, { { 24, y + 14 }, { 60, y + 14 } });
                    if (o.cap.isNotEmpty())
                        s.v(2, Mode::env, { { 26, y + 14 }, { 26, y + 20 } });
                    auto& sig = s.f(3, o.sig, { { 76, y + 26 }, { 64, y + 26 }, { 60, y + 38 }, { 60, y + 46 }, { 94, y + 46 } }, 0.45f, o.fd);
                    sig.burstEnvelope = o.burst;
                    if (! o.noJoin)
                    {
                        railTop = railTop < 0 ? y + 46 : std::min(railTop, y + 46);
                        s.dot(94, y + 46);
                    }
                }

                void hp(float y, const juce::String& cName, const juce::String& rName)
                {
                    s.C(94, y, false, 14, cName);
                    s.w({ { 94, y + 14 }, { 94, y + 18 } });
                    s.dot(94, y + 18);
                    s.R(72, y + 18, true, 22, rName);
                    s.G(72, y + 18);
                    cuts.push_back({ y, y + 14 });
                }

                void pot(float y, const juce::String& name, const juce::String& label)
                {
                    s.POT(94, y, false, 22, name, label);
                    cuts.push_back({ y, y + 22 });
                }
            };

            void voice(int col, int lane, const std::function<void(Voice&)>& build)
            {
                Voice v { Sheet { *this, col * 100.0f, Y0, lane } };
                build(v);
                const float mixLocal = MIX - Y0;
                v.s.w({ { 10, 30 }, { 10, v.leftEnd } });
                std::sort(v.cuts.begin(), v.cuts.end());
                float y = v.railTop;
                for (const auto& [a, bEnd] : v.cuts)
                {
                    if (a > y)
                        v.s.w({ { 94, y }, { 94, a } });
                    y = std::max(y, bEnd);
                }
                v.s.w({ { 94, y }, { 94, mixLocal } });
                v.s.dot(94, mixLocal);
                {
                    Flow trig;
                    trig.key = lane;
                    trig.part = 0;
                    trig.kind = Kind::pulse;
                    trig.points = { { 64, TRIG }, { col * 100.0f + 50, TRIG }, v.s.g(50, 30), v.s.g(10, 30), v.s.g(10, v.leftEnd) };
                    flows.push_back(trig);
                    Flow tint = trig;
                    tint.kind = Kind::volt;
                    tint.mode = Mode::pulse;
                    tint.points.erase(tint.points.begin());
                    flows.push_back(tint);
                    Flow outTint;
                    outTint.key = lane;
                    outTint.part = 4;
                    outTint.kind = Kind::volt;
                    outTint.mode = Mode::ac;
                    outTint.frequency = v.fd > 0 ? v.fd : 17;
                    outTint.points = { v.s.g(94, v.railTop), v.s.g(94, mixLocal) };
                    flows.push_back(outTint);
                    Flow out;
                    out.key = lane;
                    out.part = 4;
                    out.kind = v.fd > 0 ? Kind::ac : Kind::nz;
                    out.level = 0.4f;
                    out.frequency = v.fd;
                    out.points = { v.s.g(94, v.railTop), v.s.g(94, mixLocal), { 1188, MIX } };
                    flows.push_back(out);
                }
            }

            struct Empty {};
            explicit Board(Empty) {}

            Board()
            {
                using Res = Voice::Res;
                using Gate = Voice::Gate;
                using Feed = Voice::Feed;

                voice(1, 0, [](Voice& v)
                {
                    v.fd = 5;
                    v.sw("Q40 ~ lets the trigger in");
                    Res r; r.inName = "C40 ~ the pulse shaper, with R162, R163 and D53"; r.op = "The resonator's op-amp"; r.c1 = "C41"; r.c2 = "C42"; r.r = "R167"; r.fd = 5;
                    r.gnd = "R165, R166 ~ to ground. Q43 shorts R165 for the first 5 ms: the higher pitch of the attack";
                    r.fb = "VR6 ~ Decay: the feedback through R164, C43 and a second op-amp"; r.fbPot = true; r.fbLabel = "DECAY";
                    v.res(40, r);
                    v.pot(150, "VR5 ~ Tone, with R171 and R172", "TONE");
                    v.s.dot(94, 176); v.s.C(80, 176, true, 14, "C45"); v.s.G(80, 176);
                    v.pot(200, "VR4 ~ Level, then the buffer Q44", "LEVEL");
                });
                voice(2, 1, [](Voice& v)
                {
                    v.fd = 11;
                    v.sw("Q46 ~ lets the trigger in");
                    Gate gt; gt.tapX = 78; gt.tapY = NW; gt.feed = Feed::pot; gt.feedName = "VR9 ~ Snappy: how far the trigger opens the noise"; gt.feedLabel = "SNAPPY";
                    gt.cap = "C51 ~ the snappy envelope"; gt.r = "R184"; gt.q = "Q48 ~ the swing VCA on the white noise"; gt.sig = Kind::nz;
                    v.gate(34, gt);
                    Res a; a.inR = true; a.inName = "R189"; a.op = "IC14a ~ the lower shell, 173 Hz"; a.c1 = "C58"; a.c2 = "C59"; a.r = "R197"; a.gnd = "R196"; a.fd = 9;
                    v.res(96, a);
                    Res b2; b2.inR = true; b2.inName = "R191"; b2.op = "IC14b ~ the upper shell, 334 Hz"; b2.c1 = "C60"; b2.c2 = "C61"; b2.r = "R198"; b2.gnd = "R195"; b2.fd = 13;
                    v.res(178, b2);
                    v.pot(268, "VR8 ~ Tone: the balance of the two shells", "TONE");
                    v.pot(298, "VR7 ~ Level", "LEVEL");
                });
                const std::array<std::tuple<int, int, const char*, float>, 3> toms { { { 3, 2, "Q51", 6.0f }, { 4, 3, "Q54", 8.0f }, { 5, 4, "Q57", 10.0f } } };
                for (const auto& [col, lane, q, fd] : toms)
                {
                    const juce::String qName(q);
                    const float f = fd;
                    voice(col, lane, [qName, f](Voice& v)
                    {
                        v.fd = f;
                        v.sw(qName + " ~ lets the trigger in");
                        Gate gt; gt.tapX = 78; gt.tapY = NP; gt.feedName = "D55 ~ charges the noise envelope"; gt.cap = "C72 ~ the noise envelope";
                        gt.r = "R218"; gt.q = "Q52 ~ the gate on the pink noise. The conga leaves it out"; gt.sig = Kind::nz;
                        v.gate(34, gt);
                        Res r; r.inName = "C73 ~ passes the pulse"; r.op = "The resonator's op-amp"; r.c1 = "C76"; r.c2 = "C78"; r.r = "R228"; r.fd = f;
                        r.gnd = "TUNING ~ with the germanium pair D80, D81: the pitch drops as it rings"; r.gndPot = true; r.gndLabel = "TUNING";
                        r.fb = "R230 ~ fitted to both real units' decay";
                        v.res(110, r);
                        v.pot(292, "LEVEL ~ in the mixer", "LEVEL");
                    });
                }
                voice(6, 5, [](Voice& v)
                {
                    v.fd = 15;
                    v.sw("Q60 ~ lets the trigger in");
                    Res a; a.inR = true; a.inName = "R301"; a.op = "IC21 ~ 455 Hz"; a.c1 = "C116"; a.c2 = "C115"; a.r = "R316"; a.gnd = "R315"; a.fd = 14; a.noJoin = true;
                    v.res(40, a);
                    Res b2; b2.inR = true; b2.inName = "R310"; b2.op = "IC20 ~ 1.7 kHz for the rim shot, 2.5 kHz for the claves"; b2.c1 = "C119"; b2.c2 = "C117"; b2.r = "R308"; b2.gnd = "R312"; b2.fd = 18; b2.noJoin = true;
                    v.res(122, b2);
                    v.s.w({ { 94, 104 }, { 94, 200 }, { 84, 200 }, { 84, 238 }, { 76, 238 } });
                    v.s.dot(94, 186);
                    v.s.f(1, Kind::ac, { { 94, 104 }, { 94, 200 }, { 84, 200 }, { 84, 238 }, { 76, 238 } }, 0.4f, 15);
                    Gate gt; gt.feedName = "D61 ~ charges the envelope"; gt.cap = "C24 ~ the envelope"; gt.r = "R108"; gt.q = "Q62 ~ the swing VCA: chops the two rings together"; gt.sig = Kind::ac; gt.fd = 15;
                    v.gate(212, gt);
                    v.pot(292, "LEVEL ~ in the mixer", "LEVEL");
                });
                voice(7, 6, [](Voice& v)
                {
                    v.sw("Q29 ~ lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = NW; a.feed = Feed::box; a.feedName = "IC23 ~ four switches that time the four claps"; a.feedLabel = "x4";
                    a.r = "R374"; a.q = "IC22 ~ the BA662 amplifier the claps open"; a.sig = Kind::nz; a.burst = true;
                    v.gate(34, a);
                    Gate b2; b2.tapX = 78; b2.tapY = Y0 + 60; b2.feedName = "D69 ~ charges the reverb envelope"; b2.cap = "C139 ~ the reverb envelope"; b2.r = "R348";
                    b2.q = "Q70 ~ the reverb: the noise under C139's slow envelope"; b2.sig = Kind::nz;
                    v.gate(110, b2);
                    v.pot(292, "LEVEL ~ in the mixer", "LEVEL");
                });
                voice(8, 7, [](Voice& v)
                {
                    v.fd = 12;
                    v.sw("Q5 ~ lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = 40; a.feedName = "D2 ~ charges the envelope"; a.cap = "C9 ~ the envelope. R82 and C34 take part of it at once: the abrupt fall";
                    a.r = "R28"; a.q = "Q15 ~ lets the 554 Hz oscillator through"; a.sig = Kind::ac; a.fd = 11;
                    v.gate(34, a);
                    Gate b2; b2.tapX = 86; b2.tapY = 34; b2.feed = Feed::wire; b2.r = "R29"; b2.q = "Q14 ~ lets the 825 Hz oscillator through"; b2.sig = Kind::ac; b2.fd = 16;
                    v.gate(100, b2);
                    v.hp(176, "C28, C29 ~ IC2b, the band-pass at 870 Hz", "R24, R25");
                    v.pot(292, "VR5 ~ Level", "LEVEL");
                });
                voice(9, 8, [](Voice& v)
                {
                    v.sw("Q6 ~ lets the trigger in. Q19 then charges three envelopes at once");
                    Gate a; a.tapX = 78; a.tapY = HI; a.feedName = "D6"; a.cap = "C38 ~ the top band's envelope"; a.r = "R94"; a.q = "Q16 ~ the swing VCA on the high band, then IC5a's high-pass";
                    v.gate(34, a);
                    Gate b2; b2.tapX = 78; b2.tapY = Y0 + 60; b2.feedName = "D7"; b2.cap = "C40 ~ the middle band's envelope"; b2.r = "R90"; b2.q = "Q17 ~ the swing VCA on the high band again, then IC5b's high-pass";
                    v.gate(100, b2);
                    Gate c; c.tapX = 86; c.tapY = LO; c.feedName = "D8"; c.cap = "C41 ~ the low band's envelope"; c.decayName = "VR2 ~ Decay: drains C41"; c.decayLabel = "DECAY";
                    c.r = "R104"; c.q = "Q18 ~ the swing VCA on the low band, then Q25's high-pass";
                    v.gate(166, c);
                    v.pot(240, "VR4 ~ Tone: the top band against the others", "TONE");
                    v.pot(292, "VR6 ~ Level", "LEVEL");
                });
                voice(10, 9, [](Voice& v)
                {
                    v.sw("Q7 ~ lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = HI; a.feedName = "Q24 ~ charges the envelope"; a.cap = "C62 ~ the envelope";
                    a.decayName = "VR3 ~ Decay: sets when Q22 drains C62"; a.decayLabel = "DECAY"; a.decayAt = { 44, 74 }; a.decayJust = juce::Justification::centredLeft;
                    a.r = "R144"; a.q = "Q27 ~ the swing VCA on the high band";
                    v.gate(34, a);
                    v.s.dot(32, 48);
                    v.s.w({ { 32, 48 }, { 32, 80 } });
                    v.s.Q(32, 92, -1, false, "Q23 ~ drains the open hat the moment the closed hat plays");
                    v.s.G(32, 104);
                    v.s.w({ { 48, 92 }, { 60, 92 } });
                    v.s.R(60, 92, true, 22, "R173 ~ from the closed hat's envelope");
                    v.s.w({ { 82, 92 }, { 100, 92 } });
                    v.hp(116, "The high-pass around Q26, at 8.2 kHz", "Its resistor to ground");
                    v.pot(292, "VR7 ~ Level", "LEVEL");
                });
                voice(11, 10, [](Voice& v)
                {
                    v.sw("Q8 ~ lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = HI; a.feedName = "Q28 ~ charges the envelope"; a.cap = "C63 ~ the envelope"; a.r = "R142"; a.q = "Q30 ~ the swing VCA on the high band";
                    v.gate(34, a);
                    v.s.dot(31, 48);
                    v.s.w({ { 31, 48 }, { 31, 92 }, { 0, 92 } });
                    v.s.f(5, Kind::env, { { 31, 48 }, { 31, 92 }, { -52, 92 } });
                    v.hp(116, "The high-pass around Q31, at 12.2 kHz", "Its resistor to ground");
                    v.pot(292, "VR8 ~ Level", "LEVEL");
                });

                // What every drum shares: the trigger, the noise, the six oscillators and the mix.
                const Sheet a { *this, 0, 0, -1 };
                {
                    auto& pulse = a.add("The step's pulse ~ from the sequencer, one for every drum that plays on this step");
                    pulse.circles.push_back({ Pt(20, 112), 8.5f });
                }
                a.glyph({ { 15, 115 }, { 18, 115 }, { 18, 109 }, { 22, 109 }, { 22, 115 }, { 25, 115 } });
                a.w({ { 20, 103.5f }, { 20, 92 } });
                a.POT(20, 70, false, 22, "VR3 ~ Accent: the height of the trigger, up to 11.5 V");
                a.tx(29, 84, "ACCENT", juce::Justification::centredLeft, true);
                a.w({ { 20, 70 }, { 20, 40 }, { 34, 40 } });
                a.Q(50, 40, 1, false, "Q32 ~ a follower that puts the pulse on the trigger line");
                a.w({ { 50, 28 }, { 50, 22 } });
                a.tx(50, 19, "+15 V");
                a.v(0, Mode::dc, { { 50, 28 }, { 50, 22 } }).key = -2;
                flows.back().idle = 0.8f;
                a.v(0, Mode::pulse, { { 50, 52 }, { 64, 52 }, { 64, TRIG } });
                a.w({ { 50, 52 }, { 64, 52 }, { 64, TRIG }, { 1150, TRIG } });
                a.tx(70, 7, "TRIGGER LINE", juce::Justification::centredLeft);
                a.f(0, Kind::pulse, { { 20, 103 }, { 20, 40 }, { 34, 40 } });
                a.f(0, Kind::pulse, { { 50, 52 }, { 64, 52 }, { 64, TRIG } });

                a.BOX(208, 24, 14, 12, "The noise source ~ one hiss for the whole machine");
                a.glyph({ { 210, 31 }, { 212, 27 }, { 214, 33 }, { 216, 28 }, { 218, 32 }, { 220, 29 } });
                a.tx(226, 32, "NOISE", juce::Justification::centredLeft);
                a.w({ { 215, 36 }, { 215, NW }, { 778, NW } });
                a.dot(296, NW);
                a.w({ { 296, NW }, { 296, NP } });
                a.R(296, NP, true, 22, "The pink filter ~ the toms take the noise through it");
                a.w({ { 318, NP }, { 578, NP } });
                a.tx(322, 62, "PINK", juce::Justification::centredLeft);
                { auto& f = a.f(0, Kind::nz, { { 215, 36 }, { 215, NW }, { 778, NW } }); f.key = -2; f.idle = 0.06f; }
                { auto& f = a.f(0, Kind::nz, { { 296, NW }, { 296, NP }, { 578, NP } }); f.key = -2; f.idle = 0.05f; }

                const std::array<std::pair<float, const char*>, 6> oscillators { {
                    { 862.0f, "554 Hz ~ the cowbell's, set by TM1" }, { 886.0f, "825 Hz ~ the cowbell's, set by TM2" }, { 915.0f, "205 Hz" },
                    { 932.0f, "304 Hz" }, { 968.0f, "370 Hz" }, { 985.0f, "523 Hz" } } };
                for (std::size_t i = 0; i < oscillators.size(); ++i)
                {
                    const float x = oscillators[i].first;
                    a.BOX(x - 7, 22, 14, 12, juce::String::fromUTF8(oscillators[i].second) + " ~ a Schmitt-trigger oscillator that never stops");
                    a.glyph({ { x - 5, 31 }, { x - 5, 26 }, { x, 26 }, { x, 31 }, { x + 5, 31 }, { x + 5, 26 } });
                    if (i == 0)
                        a.w({ { 862, 34 }, { 862, 40 }, { 878, 40 }, { 878, SUM } });
                    else
                        a.w({ { x, 34 }, { x, SUM } });
                    a.dot(i == 0 ? 878.0f : x, SUM);
                }
                a.w({ { 878, SUM }, { 1069, SUM } });
                a.tx(1060, 31, "SIX OSCILLATORS", juce::Justification::centredLeft);
                a.BOX(1020, 62, 26, 11, "IC3 ~ a band-pass at 7.09 kHz: the high band", "7.1k");
                a.dot(1033, SUM); a.w({ { 1033, SUM }, { 1033, 62 } }); a.w({ { 1033, 73 }, { 1033, HI } }); a.dot(1033, HI);
                a.BOX(1056, 62, 26, 11, "IC3 ~ a band-pass at 3.43 kHz: the low band", "3.4k");
                a.w({ { 1069, SUM }, { 1069, 62 } }); a.w({ { 1069, 73 }, { 1069, LO } });
                a.w({ { 978, HI }, { 1178, HI } }); a.w({ { 986, LO }, { 1069, LO } });
                { auto& f = a.f(0, Kind::ac, { { 878, SUM }, { 1069, SUM } }, 0.4f, 9); f.key = -2; f.idle = 0.07f; }
                { auto& f = a.f(0, Kind::ac, { { 978, HI }, { 1178, HI } }, 0.4f, 13); f.key = -2; f.idle = 0.07f; }
                { auto& f = a.f(0, Kind::ac, { { 986, LO }, { 1069, LO } }, 0.4f, 7); f.key = -2; f.idle = 0.07f; }

                a.w({ { 194, MIX }, { 1188, MIX } });
                {
                    auto& end = a.add({});
                    end.fill.addTriangle({ 1197, MIX }, { 1189, MIX - 3.5f }, { 1189, MIX + 3.5f });
                }
                a.tx(1186, 458, "MIX OUT", juce::Justification::centredRight);

                juce::Random r(808);
                for (auto& f : flows)
                {
                    f.length = polylineLength(f.points);
                    f.offset = r.nextFloat() * 6.5f;
                    f.phase = r.nextFloat() * 6.5f;
                }
            }
        };

        // ---- The bass drum complete ---------------------------------------------------------------------------
        /*
            Every part of the engine's tools/kick808/bassdrum.cir, its supplies and ground drawn as rails so every loop
            closes; the mockup's "buildFullBD", ported. The current on each wire and the voltage of each net are
            ngspice's own, recorded by the engine's tools/kick808/mockup_currents.py (resources/lid). Each wire's current
            is worked out from the parts on it (Kirchhoff), and the drawing is checked against the netlist: one node per
            net, one net per node, no part forgotten. Left out: the glide bend (the engine's own) and the op-amps' supply
            pins.
        */
        struct Recording
        {
            int samples = 0;
            std::vector<double> ms;
            std::map<std::string, std::vector<float>> series;
            std::map<std::string, std::vector<std::string>> parts;
            juce::String setting;
        };

        Recording loadRecording()
        {
            Recording r;
            const auto root = juce::JSON::parse(juce::String::fromUTF8(LibraryData::bassdrum_currents_json, LibraryData::bassdrum_currents_jsonSize));
            const auto* times = root.getProperty("t", {}).getArray();
            const auto* names = root.getProperty("names", {}).getArray();
            const auto* scales = root.getProperty("scales", {}).getArray();
            if (times == nullptr || names == nullptr || scales == nullptr || names->size() != scales->size())
                return r;
            r.samples = times->size();
            for (const auto& t : *times)
                r.ms.push_back(static_cast<double>(t));
            juce::MemoryOutputStream raw;
            if (! juce::Base64::convertFromBase64(raw, root.getProperty("data", "").toString()))
                return {};
            const auto* bytes = static_cast<const std::uint8_t*>(raw.getData());
            const auto n = static_cast<std::size_t>(r.samples);
            if (raw.getDataSize() < n * 2 * static_cast<std::size_t>(names->size()))
                return {};
            for (int k = 0; k < names->size(); ++k)
            {
                std::vector<float> s(n);
                const double scale = static_cast<double>((*scales)[k]);
                for (std::size_t i = 0; i < n; ++i)
                {
                    const auto at = (static_cast<std::size_t>(k) * n + i) * 2;
                    const auto v = static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8)));
                    s[i] = static_cast<float>(v * scale);
                }
                r.series[(*names)[k].toString().toStdString()] = std::move(s);
            }
            if (const auto* obj = root.getProperty("parts", {}).getDynamicObject())
                for (const auto& p : obj->getProperties())
                {
                    std::vector<std::string> nodes;
                    if (const auto* list = p.value.getArray())
                        for (const auto& node : *list)
                            nodes.push_back(node.toString().toStdString());
                    r.parts[p.name.toString().toStdString()] = nodes;
                }
            const auto set = root.getProperty("setting", {});
            r.setting << static_cast<double>(set.getProperty("accent_volts", 0.0)) << " V on the trigger line, Decay "
                      << static_cast<double>(set.getProperty("decay", 0.0)) << ", Tone " << static_cast<double>(set.getProperty("tone", 0.0))
                      << ", Level " << static_cast<double>(set.getProperty("level", 0.0));
            return r;
        }

        struct FullDrum
        {
            using Coef = std::vector<std::pair<std::string, float>>;
            struct Term { Pt pt; Coef coef; std::string node; bool root = false; };
            struct Body { Pt a, b; Coef coef; };
            struct Node { Pt p; std::vector<int> edges; std::vector<int> terms; int net = -1; };
            struct Edge { int a = 0, b = 0; int net = -1; };
            struct Net { std::vector<int> nodes, edges; std::string name; };
            struct Wire { Pt a, b; std::vector<float> current; float offset = 0.0f; };

            Recording rec;
            Board drawing { Board::Empty {} };
            std::vector<std::vector<Pt>> segs;
            std::vector<Term> terms;
            std::vector<Body> bodies;
            std::vector<Node> nodes;
            std::vector<Edge> edges;
            std::vector<Net> nets;
            std::vector<Wire> wires;
            std::vector<std::pair<Pt, float>> pins;
            juce::StringArray errors;
            double pos = 0.0;
            bool playing = false;

            int pointIndex(Pt p)
            {
                for (std::size_t i = 0; i < nodes.size(); ++i)
                    if (nodes[i].p.getDistanceSquaredFrom(p) < 1.0e-4f)
                        return static_cast<int>(i);
                nodes.push_back({ p, {}, {}, -1 });
                return static_cast<int>(nodes.size()) - 1;
            }

            FullDrum() : rec(loadRecording())
            {
                if (rec.samples < 2)
                {
                    errors.add("the recording did not load");
                    return;
                }
                Board::Sheet a { drawing, 0, 0, 0 };
                const auto W = [&](std::initializer_list<Pt> pts) { segs.emplace_back(pts); };
                const auto term = [&](Pt p, Coef c, const std::string& node, bool root = false) { terms.push_back({ p, std::move(c), node, root }); };
                const auto nodeOf = [&](const std::string& el, std::size_t k) -> std::string
                {
                    const auto it = rec.parts.find(el);
                    if (it == rec.parts.end() || k >= it->second.size())
                    {
                        errors.add(juce::String(el) + " is not in the netlist");
                        return "?";
                    }
                    return it->second[k];
                };
                const auto two = [&](const std::string& el, Pt p1, Pt p2, const std::string& series = {})
                {
                    const auto c = series.empty() ? el + ".i" : series;
                    term(p1, { { c, 1.0f } }, nodeOf(el, 0));
                    term(p2, { { c, -1.0f } }, nodeOf(el, 1));
                    bodies.push_back({ p1, p2, { { c, 1.0f } } });
                };
                const auto ends = [](float x, float y, char o, float L) -> std::pair<Pt, Pt>
                {
                    return { Pt(x, y), o == 'h' ? Pt(x + L, y) : o == 'u' ? Pt(x, y - L) : Pt(x, y + L) };
                };
                const auto R = [&](const std::string& el, float x, float y, char o, float L, const juce::String& label, bool rev = false)
                {
                    a.R(x, y, o == 'h', L, label);
                    const auto [p, q] = ends(x, y, o, L);
                    if (rev) two(el, q, p); else two(el, p, q);
                };
                const auto C = [&](const std::string& el, float x, float y, char o, float L, const juce::String& label, bool rev = false)
                {
                    a.C(x, y, o == 'h', L, label);
                    const auto [p, q] = ends(x, y, o, L);
                    if (rev) two(el, q, p); else two(el, p, q);
                };
                const auto POT = [&](const std::string& el, float x, float y, char o, float L, const juce::String& label)
                {
                    a.POT(x, y, o == 'h', L, label);
                    const auto [p, q] = ends(x, y, o, L);
                    two(el, p, q);
                };
                const auto DI = [&](const std::string& el, float x, float y, float L, const juce::String& label)
                {
                    a.Dv(x, y, L, label);
                    two(el, Pt(x, y), Pt(x, y + L), el + ".id");
                };
                const auto Q = [&](const std::string& el, float x, float cy, bool pnp, const juce::String& label)
                {
                    a.Q(x, cy, 1, pnp, label);
                    const Pt top(x, cy - 12), bot(x, cy + 12), pc = pnp ? bot : top, pe = pnp ? top : bot;
                    term(pc, { { el + ".ic", 1.0f } }, nodeOf(el, 0));
                    term(Pt(x - 16, cy), { { el + ".ib", 1.0f } }, nodeOf(el, 1));
                    term(pe, { { el + ".ie", 1.0f } }, nodeOf(el, 2));
                    bodies.push_back({ pc, pe, { { el + ".ic", 1.0f } } });
                };
                const auto pin = [&](float x, float y, const std::string& node, const juce::String& label, float lx, float ly, juce::Justification j)
                {
                    term(Pt(x, y), {}, node, true);
                    pins.push_back({ Pt(x, y), 2.2f });
                    a.tx(lx, ly, label, j);
                };
                const auto open = [&](float x, float y, const std::string& node) { term(Pt(x, y), {}, node); };
                const auto L = juce::Justification::centredLeft;
                const auto Rj = juce::Justification::centredRight;
                const auto M = juce::Justification::centred;

                // The rails
                pin(40, 12, "vcc", "+15 V", 40, 7, M); W({ { 40, 12 }, { 628, 12 } });
                pin(40, 244, "0", "0 V", 40, 255, M); W({ { 40, 244 }, { 664, 244 } });
                // The common trigger
                {
                    auto& p = a.add("The step's pulse ~ 1 ms, from the sequencer");
                    p.circles.push_back({ Pt(17, 150), 8.5f });
                }
                a.glyph({ { 12, 153 }, { 15, 153 }, { 15, 147 }, { 19, 147 }, { 19, 153 }, { 22, 153 } });
                term(Pt(25.5f, 150), {}, "n6", true); W({ { 25.5f, 150 }, { 28, 150 } });
                R("r117", 28, 150, 'h', 22, "R117 ~ 47 k"); Q("q31", 66, 150, false, "Q31 ~ turns off for the pulse, letting Q32 rise"); W({ { 66, 162 }, { 66, 244 } });
                W({ { 66, 138 }, { 66, 106 } }); R("r120", 66, 84, 'v', 22, "R120 ~ 4.7 k"); pin(66, 84, "nx", "ACCENT, 5 TO 15 V", 71, 80, L);
                W({ { 66, 110 }, { 76, 110 } }); Q("q32", 92, 110, false, "Q32 ~ the follower onto the trigger line"); W({ { 92, 98 }, { 92, 12 } });
                W({ { 92, 122 }, { 92, 130 } }); R("r113", 92, 130, 'v', 22, "R113 ~ 4.7 k, holds the line down"); W({ { 92, 152 }, { 92, 166 }, { 66, 166 } });
                W({ { 92, 130 }, { 110, 130 }, { 110, 112 }, { 150, 112 }, { 150, 118 } });
                // The bass drum's switch
                R("r155", 122, 112, 'v', 18, "R155 ~ 4.7 k"); W({ { 122, 130 }, { 134, 130 } });
                Q("q40", 150, 130, true, "Q40 ~ a PNP switch: it can only push current into the drum");
                R("r154", 122, 130, 'v', 26, "R154 ~ 10 k"); W({ { 122, 156 }, { 122, 160 } }); Q("q39", 122, 172, false, "Q39 ~ on while the bass drum's step data stands"); W({ { 122, 184 }, { 122, 244 } });
                pin(74, 172, "nbd", "BD ON", 72, 182, M); W({ { 74, 172 }, { 76, 172 } }); R("r152", 76, 172, 'h', 22, "R152 ~ 22 k"); W({ { 98, 172 }, { 106, 172 } });
                R("r153", 102, 172, 'v', 18, "R153 ~ 10 k"); W({ { 102, 190 }, { 102, 244 } });
                W({ { 150, 142 }, { 150, 262 }, { 170, 262 } }); W({ { 150, 150 }, { 162, 150 }, { 162, 84 }, { 166, 84 } });
                // The pulse shaper, under the ground rail
                W({ { 166, 262 }, { 166, 273 }, { 170, 273 } }); R("r163", 170, 273, 'h', 22, "R163 ~ 100 k"); W({ { 192, 273 }, { 196, 273 }, { 196, 262 } });
                C("c40", 170, 262, 'h', 22, "C40 ~ 0.015 uF"); W({ { 192, 262 }, { 388, 262 }, { 388, 156 }, { 404, 156 } });
                R("r162", 196, 244, 'v', 18, "R162 ~ 4.7 k", true); DI("d53", 212, 244, 18, "D53 ~ clamps the falling edge");
                // The envelope generator
                R("r157", 166, 84, 'h', 22, "R157 ~ 8.2 k"); W({ { 188, 84 }, { 198, 84 } }); R("r158", 192, 84, 'v', 20, "R158 ~ 2.7 k"); W({ { 192, 104 }, { 192, 244 } });
                Q("q41", 214, 84, false, "Q41 ~ with Q42, holds Q43 on for some 5 ms"); W({ { 214, 96 }, { 214, 244 } }); W({ { 214, 72 }, { 214, 44 } });
                R("r156", 214, 20, 'v', 24, "R156 ~ 1 M"); W({ { 214, 12 }, { 214, 20 } });
                W({ { 214, 60 }, { 242, 60 } }); C("c38", 232, 60, 'v', 14, "C38 ~ 0.1 uF"); W({ { 232, 74 }, { 232, 244 } });
                Q("q42", 258, 60, false, "Q42 ~ on at rest: its standing current flows through R160"); W({ { 258, 72 }, { 258, 244 } }); W({ { 258, 48 }, { 258, 38 } });
                R("r160", 258, 16, 'v', 22, "R160 ~ 22 k"); W({ { 258, 12 }, { 258, 16 } });
                // The retrigger network and the attack transistor
                W({ { 258, 40 }, { 272, 40 } }); C("c39", 272, 40, 'h', 14, "C39 ~ 0.033 uF: the retrigger pulse, the sigh after the attack"); W({ { 286, 40 }, { 300, 40 } });
                DI("d52", 292, 40, 20, "D52"); W({ { 292, 60 }, { 292, 244 } }); R("r161", 300, 40, 'h', 22, "R161 ~ 1 M"); W({ { 322, 40 }, { 400, 40 }, { 400, 110 } });
                W({ { 266, 40 }, { 266, 140 }, { 300, 140 } }); R("r159", 300, 140, 'h', 22, "R159 ~ 100 k"); W({ { 322, 140 }, { 330, 140 } });
                Q("q43", 346, 140, false, "Q43 ~ shorts R165 in the attack: the higher pitch of the first 5 ms"); W({ { 346, 152 }, { 346, 244 } });
                W({ { 346, 128 }, { 346, 112 } }); W({ { 346, 120 }, { 362, 120 } }); R("r165", 362, 120, 'v', 22, "R165 ~ 47 k"); W({ { 362, 142 }, { 362, 244 } });
                R("r166", 346, 90, 'v', 22, "R166 ~ 6.8 k"); W({ { 346, 90 }, { 346, 84 }, { 400, 84 } });
                // The bridged-T around the first op-amp
                W({ { 404, 144 }, { 376, 144 }, { 376, 110 }, { 382, 110 } }); C("c41", 382, 110, 'h', 14, "C41 ~ 0.015 uF", true); W({ { 396, 110 }, { 404, 110 } });
                C("c42", 404, 110, 'h', 14, "C42 ~ 0.015 uF"); W({ { 418, 110 }, { 440, 110 }, { 440, 150 }, { 440, 214 }, { 452, 214 } });
                W({ { 376, 124 }, { 396, 124 } }); R("r167", 396, 124, 'h', 22, "R167 ~ 1 M"); W({ { 418, 124 }, { 440, 124 } });
                a.OP(404, 138, "The first op-amp ~ half a uPC4558: the resonator"); open(404, 144, "ninv"); open(404, 156, "npulse"); term(Pt(430, 150), {}, "nout", true); W({ { 430, 150 }, { 452, 150 } });
                // The feedback buffer: the Decay
                R("r164", 452, 150, 'h', 22, "R164 ~ 47 k"); W({ { 474, 150 }, { 492, 150 } });
                a.OP(492, 144, "The second op-amp ~ the feedback buffer"); open(492, 150, "ninv2"); open(492, 162, "nplus2"); term(Pt(518, 156), {}, "nfb", true);
                W({ { 492, 162 }, { 482, 162 }, { 482, 170 } }); R("r168", 482, 170, 'v', 20, "R168 ~ 22 k"); W({ { 482, 190 }, { 482, 244 } });
                W({ { 480, 150 }, { 480, 104 }, { 482, 104 } }); W({ { 480, 124 }, { 488, 124 } }); R("r169", 488, 124, 'h', 22, "R169 ~ 47 k"); W({ { 510, 124 }, { 526, 124 } });
                POT("rvr6", 482, 104, 'h', 22, "VR6 DECAY ~ 500 k"); W({ { 504, 104 }, { 506, 104 } }); C("c43", 506, 104, 'h', 14, "C43 ~ 33 uF"); W({ { 520, 104 }, { 526, 104 } });
                W({ { 518, 156 }, { 526, 156 }, { 526, 70 }, { 470, 70 } }); R("r170", 448, 70, 'h', 22, "R170 ~ 470 k, back into the bridged-T", true); W({ { 448, 70 }, { 400, 70 } });
                // The output filter: tone, level, the follower
                R("r171", 452, 214, 'h', 22, "R171 ~ 220"); W({ { 474, 214 }, { 484, 214 } }); R("r172", 484, 214, 'h', 22, "R172 ~ 10 k"); W({ { 506, 214 }, { 540, 214 } });
                W({ { 478, 214 }, { 478, 230 }, { 484, 230 } }); POT("rvr5", 484, 230, 'h', 22, "VR5 TONE ~ 10 k"); W({ { 506, 230 }, { 512, 230 }, { 512, 214 } });
                C("c45", 524, 214, 'v', 14, "C45 ~ 0.1 uF"); W({ { 524, 228 }, { 524, 244 } }); C("c47", 540, 214, 'h', 14, "C47 ~ 0.47 uF"); W({ { 554, 214 }, { 560, 214 } });
                a.R(560, 214, false, 22, "VR4 LEVEL ~ 100 k: its wiper takes the sound off"); two("rvr4t", Pt(560, 214), Pt(560, 225)); two("rvr4b", Pt(560, 225), Pt(560, 236));
                {
                    auto& wiper = a.add({});
                    wiper.fill.addTriangle({ 563.5f, 225 }, { 568.5f, 222.5f }, { 568.5f, 227.5f });
                }
                W({ { 560, 236 }, { 560, 244 } });
                W({ { 560, 225 }, { 570, 225 } }); C("c49", 570, 225, 'h', 14, "C49 ~ 0.47 uF"); W({ { 584, 225 }, { 604, 225 }, { 604, 178 } });
                R("r177", 604, 156, 'v', 22, "R177 ~ 82 k", true); W({ { 604, 156 }, { 604, 150 }, { 612, 150 } }); W({ { 604, 150 }, { 588, 150 } }); R("r176", 588, 150, 'v', 22, "R176 ~ 100 k"); W({ { 588, 172 }, { 588, 244 } });
                Q("q44", 628, 150, false, "Q44 ~ the output follower: about 2 mA stand in it all the time"); W({ { 628, 138 }, { 628, 12 } });
                W({ { 628, 162 }, { 628, 166 }, { 644, 166 } }); R("r173", 628, 166, 'v', 22, "R173 ~ 6.8 k"); W({ { 628, 188 }, { 628, 204 } }); pin(628, 204, "vee", "-15 V", 622, 207, Rj);
                C("c48", 636, 166, 'v', 14, "C48 ~ 220 pF"); W({ { 636, 180 }, { 636, 244 } }); C("c50", 644, 166, 'h', 14, "C50 ~ 1 uF"); W({ { 658, 166 }, { 676, 166 } });
                R("r174", 664, 166, 'v', 22, "R174 ~ 100 k"); W({ { 664, 188 }, { 664, 244 } });
                {
                    auto& out = a.add({});
                    out.fill.addTriangle({ 684, 166 }, { 676, 162.5f }, { 676, 169.5f });
                }
                a.tx(676, 160, "OUT", Rj);

                buildGraph();
            }

            void buildGraph()
            {
                // Every wire corner, junction and part lead is a point; a wire is split where a point lies on it.
                for (const auto& s : segs)
                    for (const auto& p : s)
                        pointIndex(p);
                for (std::size_t t = 0; t < terms.size(); ++t)
                    nodes[static_cast<std::size_t>(pointIndex(terms[t].pt))].terms.push_back(static_cast<int>(t));
                for (const auto& s : segs)
                    for (std::size_t i = 0; i + 1 < s.size(); ++i)
                    {
                        const auto p = s[i], q = s[i + 1];
                        const bool horizontal = std::abs(p.y - q.y) < 1.0e-3f;
                        const float lo = horizontal ? std::min(p.x, q.x) : std::min(p.y, q.y);
                        const float hi = horizontal ? std::max(p.x, q.x) : std::max(p.y, q.y);
                        std::vector<int> on;
                        for (std::size_t n = 0; n < nodes.size(); ++n)
                        {
                            const auto& np = nodes[n].p;
                            const float across = horizontal ? np.y - p.y : np.x - p.x;
                            const float along = horizontal ? np.x : np.y;
                            if (std::abs(across) < 1.0e-3f && along >= lo - 1.0e-3f && along <= hi + 1.0e-3f)
                                on.push_back(static_cast<int>(n));
                        }
                        std::sort(on.begin(), on.end(), [&](int m, int n)
                                  { return horizontal ? nodes[static_cast<std::size_t>(m)].p.x < nodes[static_cast<std::size_t>(n)].p.x
                                                      : nodes[static_cast<std::size_t>(m)].p.y < nodes[static_cast<std::size_t>(n)].p.y; });
                        for (std::size_t j = 0; j + 1 < on.size(); ++j)
                        {
                            edges.push_back({ on[j], on[j + 1], -1 });
                            const int e = static_cast<int>(edges.size()) - 1;
                            nodes[static_cast<std::size_t>(on[j])].edges.push_back(e);
                            nodes[static_cast<std::size_t>(on[j + 1])].edges.push_back(e);
                        }
                    }
                // Nets, each checked against the netlist.
                for (std::size_t start = 0; start < nodes.size(); ++start)
                {
                    if (nodes[start].net >= 0)
                        continue;
                    Net net;
                    const int id = static_cast<int>(nets.size());
                    std::vector<int> stack { static_cast<int>(start) };
                    nodes[start].net = id;
                    while (! stack.empty())
                    {
                        const int n = stack.back();
                        stack.pop_back();
                        net.nodes.push_back(n);
                        for (const int e : nodes[static_cast<std::size_t>(n)].edges)
                        {
                            auto& edge = edges[static_cast<std::size_t>(e)];
                            if (edge.net < 0)
                            {
                                edge.net = id;
                                net.edges.push_back(e);
                            }
                            const int o = edge.a == n ? edge.b : edge.a;
                            if (nodes[static_cast<std::size_t>(o)].net < 0)
                            {
                                nodes[static_cast<std::size_t>(o)].net = id;
                                stack.push_back(o);
                            }
                        }
                    }
                    std::vector<std::string> names;
                    for (const int n : net.nodes)
                        for (const int t : nodes[static_cast<std::size_t>(n)].terms)
                            if (std::find(names.begin(), names.end(), terms[static_cast<std::size_t>(t)].node) == names.end())
                                names.push_back(terms[static_cast<std::size_t>(t)].node);
                    net.name = names.empty() ? std::string() : names[0];
                    if (names.size() != 1)
                        errors.add("one wire joins " + juce::String(names.empty() ? "nothing" : juce::StringArray([&] { juce::StringArray a; for (const auto& s : names) a.add(s); return a; }()).joinIntoString(" and ")));
                    if (net.edges.size() + 1 != net.nodes.size())
                        errors.add(juce::String(net.name) + " is drawn with a loop");
                    nets.push_back(std::move(net));
                }
                std::map<std::string, int> count;
                for (const auto& net : nets)
                    ++count[net.name];
                for (const auto& [name, c] : count)
                    if (c > 1)
                        errors.add(juce::String(name) + " is drawn in " + juce::String(c) + " pieces");
                std::set<std::string> drawn;
                for (const auto& t : terms)
                    if (! t.coef.empty())
                        drawn.insert(t.coef[0].first.substr(0, t.coef[0].first.find('.')));
                for (const auto& [el, nodesOfPart] : rec.parts)
                    if (drawn.count(el) == 0)
                        errors.add(juce::String(el) + " is not drawn");
                for (const auto& t : terms)
                    for (const auto& [s, c] : t.coef)
                        if (rec.series.count(s) == 0)
                            errors.add(juce::String(s) + " was not recorded");

                // Each wire's current, by Kirchhoff: walking out from a net's source, a wire carries what the part
                // leads beyond it take.
                std::vector<std::pair<std::pair<Pt, Pt>, Coef>> flows;
                const auto add = [](Coef& into, const Coef& from)
                {
                    for (const auto& [s, c] : from)
                    {
                        auto it = std::find_if(into.begin(), into.end(), [&](const auto& x) { return x.first == s; });
                        if (it == into.end())
                            into.push_back({ s, c });
                        else
                            it->second += c;
                    }
                };
                std::function<Coef(int, int)> walk = [&](int n, int from) -> Coef
                {
                    Coef out;
                    for (const int t : nodes[static_cast<std::size_t>(n)].terms)
                        if (! terms[static_cast<std::size_t>(t)].root)
                            add(out, terms[static_cast<std::size_t>(t)].coef);
                    for (const int e : nodes[static_cast<std::size_t>(n)].edges)
                    {
                        if (e == from)
                            continue;
                        const auto& edge = edges[static_cast<std::size_t>(e)];
                        const int o = edge.a == n ? edge.b : edge.a;
                        const auto sub = walk(o, e);
                        flows.push_back({ { nodes[static_cast<std::size_t>(n)].p, nodes[static_cast<std::size_t>(o)].p }, sub });
                        add(out, sub);
                    }
                    return out;
                };
                for (const auto& net : nets)
                {
                    if (net.edges.size() + 1 != net.nodes.size() || net.nodes.empty())
                        continue;
                    int root = -1;
                    for (const int n : net.nodes)
                        for (const int t : nodes[static_cast<std::size_t>(n)].terms)
                            if (root < 0 && terms[static_cast<std::size_t>(t)].root)
                                root = n;
                    if (root < 0)
                        for (const int n : net.nodes)
                            if (root < 0 && ! nodes[static_cast<std::size_t>(n)].terms.empty())
                                root = n;
                    walk(root < 0 ? net.nodes[0] : root, -1);
                }
                for (const auto& b : bodies)
                    flows.push_back({ { b.a, b.b }, b.coef });

                const auto n = static_cast<std::size_t>(rec.samples);
                juce::Random random(808);
                for (const auto& [seg, coef] : flows)
                {
                    Wire w { seg.first, seg.second, std::vector<float>(n, 0.0f), random.nextFloat() * 6.5f };
                    for (const auto& [s, c] : coef)
                        if (const auto it = rec.series.find(s); it != rec.series.end() && c != 0.0f)
                            for (std::size_t i = 0; i < n; ++i)
                                w.current[i] += c * it->second[i];
                    wires.push_back(std::move(w));
                }
            }

            [[nodiscard]] double msAt(double p) const
            {
                const auto i = std::min<std::size_t>(static_cast<std::size_t>(std::max(0.0, std::floor(p))), rec.ms.size() - 2);
                return rec.ms[i] + (rec.ms[i + 1] - rec.ms[i]) * (p - static_cast<double>(i));
            }
            [[nodiscard]] float valueAt(const std::vector<float>& s) const
            {
                const auto i = std::min<std::size_t>(static_cast<std::size_t>(std::max(0.0, std::floor(pos))), s.size() - 2);
                const float fr = static_cast<float>(pos - static_cast<double>(i));
                return s[i] + (s[i + 1] - s[i]) * fr;
            }
        };
    }

    struct LidView::Impl
    {
        Board board;
        std::unique_ptr<FullDrum> full; // made the first time it is shown
        bool showFull = false;
        double now = 0.0, last = 0.0;
        int selected = -1;
        juce::Random random { 1980 };
        Choices sheet;
        Chip play;
        juce::Slider time { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    };

    LidView::LidView(Processor& p) : impl(std::make_unique<Impl>()), processor(p)
    {
        auto& i = *impl;
        i.sheet.options = { "Whole board", "Bass drum, complete" };
        i.sheet.fontHeight = 10.0f;
        i.sheet.get = [this] { return impl->showFull ? 1 : 0; };
        i.sheet.set = [this](int v)
        {
            impl->showFull = v == 1;
            impl->selected = -1;
            if (impl->showFull && ! impl->full)
                impl->full = std::make_unique<FullDrum>();
            resized();
            repaint();
        };
        addAndMakeVisible(i.sheet);
        i.play.fontHeight = 10.5f;
        i.play.text = "Play the hit";
        i.play.isOn = [this] { return impl->full && impl->full->playing; };
        i.play.onClick = [this](const juce::MouseEvent&)
        {
            auto& f = *impl->full;
            if (! f.playing && f.pos >= f.rec.samples - 2)
                f.pos = 0.0;
            f.playing = ! f.playing;
        };
        addChildComponent(i.play);
        i.time.onValueChange = [this]
        {
            if (impl->full && std::abs(impl->time.getValue() - impl->full->pos) > 0.5)
            {
                impl->full->pos = impl->time.getValue();
                impl->full->playing = false;
            }
        };
        addChildComponent(i.time);
    }

    void LidView::showSheet(int sheet) { impl->sheet.set(sheet); }

    juce::StringArray LidView::completeDrumProblems()
    {
        if (! impl->full)
            impl->full = std::make_unique<FullDrum>();
        return impl->full->errors;
    }

    void LidView::resized()
    {
        auto& i = *impl;
        auto bottom = getLocalBounds().removeFromBottom(24);
        i.sheet.setBounds(bottom.removeFromRight(250).reduced(0, 1));
        const bool full = i.showFull && i.full && i.full->rec.samples > 1;
        i.play.setVisible(full);
        i.time.setVisible(full);
        if (full)
        {
            auto bar = getLocalBounds().withTrimmedBottom(26).removeFromBottom(26);
            i.play.setBounds(bar.removeFromLeft(96).reduced(0, 2));
            bar.removeFromLeft(10);
            bar.removeFromRight(330);
            i.time.setBounds(bar);
            i.time.setRange(0.0, i.full->rec.samples - 1.0, 0.0);
        }
    }
    LidView::~LidView() = default;

    namespace
    {
        /** How long a struck drum rings, from its Decay knob where it has one (the mockup's). */
        double ringFor(int lane, const KitValues& kit)
        {
            const auto knob = [&](const char* id)
            {
                for (int i = 0; i < knobCount; ++i)
                    if (juce::String(knobs[static_cast<std::size_t>(i)].id) == id)
                        return kit.knob[static_cast<std::size_t>(i)];
                return 0.5f;
            };
            switch (lane)
            {
                case 0: return 200.0 + 1300.0 * knob("bdDecay");
                case 1: return 240.0;
                case 2: return 380.0;
                case 3: return 320.0;
                case 4: return 270.0;
                case 5: return kit.alt[5] ? 140.0 : 70.0;
                case 6: return kit.alt[6] ? 90.0 : 420.0;
                case 7: return 200.0;
                case 8: return 400.0 + 1400.0 * knob("cyDecay");
                case 9: return 150.0 + 900.0 * knob("ohDecay");
                case 10: return 70.0;
                default: return 90.0;
            }
        }
    }

    void LidView::struck(std::uint32_t lanes)
    {
        if (impl->showFull && impl->full && (lanes & 1u) != 0)
        {
            impl->full->pos = 0.0; // the bass drum struck: its hit plays from the start
            impl->full->playing = true;
        }
        const auto& kit = processor.project().kit;
        for (int l = 0; l < laneCount; ++l)
        {
            if (((lanes >> l) & 1u) == 0)
                continue;
            const double d = std::max(280.0, ringFor(l, kit));
            for (auto& f : impl->board.flows)
            {
                if (f.key == l || f.key == -1)
                {
                    f.t0 = impl->now;
                    f.duration = d;
                }
                else if (l == 10 && f.key == 9 && f.part != 0)
                    f.t0 = -1.0; // the closed hat cuts the open one
            }
        }
    }

    void LidView::tick(double nowMs)
    {
        const double dt = std::min(50.0, nowMs - impl->now) / 1000.0;
        impl->now = nowMs;
        if (! isVisible())
            return;
        if (impl->showFull && impl->full && impl->full->rec.samples > 1)
        {
            auto& f = *impl->full;
            if (f.playing)
            {
                f.pos += dt * f.rec.samples / 16.0; // the whole recording in 16 seconds
                if (f.pos >= f.rec.samples - 1)
                {
                    f.pos = 0.0;
                    f.playing = false;
                }
                impl->time.setValue(f.pos, juce::dontSendNotification);
            }
            for (auto& w : f.wires)
            {
                const float current = f.valueAt(w.current);
                const float g = std::min(1.0f, std::log10(1.0f + std::abs(current) / 1.0e-6f) / 3.5f);
                w.offset -= (current > 0.0f ? 1.0f : current < 0.0f ? -1.0f : 0.0f) * (10.0f + 60.0f * g) * static_cast<float>(dt);
            }
            impl->play.text = f.playing ? "Pause" : "Play the hit";
            repaint();
            return;
        }
        for (auto& f : impl->board.flows)
        {
            if (f.kind == Kind::volt || (f.t0 < 0.0 && f.idle <= 0.0f))
                continue;
            if (f.kind == Kind::ac)
                f.offset = f.phase + 3.0f * static_cast<float>(std::sin(nowMs / 1000.0 * juce::MathConstants<double>::twoPi * (f.frequency > 0 ? f.frequency : 8.0f)));
            else if (f.kind == Kind::nz)
                f.offset += (impl->random.nextFloat() - 0.5f) * 5.0f;
            else
                f.offset -= static_cast<float>((50.0 + 120.0 * 0.5) * dt);
        }
        repaint();
    }

    namespace
    {
        void paintParts(juce::Graphics& g, const Board& b, const juce::AffineTransform& t, float scale, int selected, bool names)
        {
            const auto& th = theme();
            for (std::size_t i = 0; i < b.parts.size(); ++i)
            {
                const auto& part = b.parts[i];
                const auto colour = static_cast<int>(i) == selected ? th.flow : th.ink;
                if (! part.box.isEmpty())
                {
                    const auto r = part.box.transformedBy(t);
                    g.setColour(th.panel);
                    g.fillRoundedRectangle(r, 1.5f * scale);
                    g.setColour(colour);
                    g.drawRoundedRectangle(r, 1.5f * scale, 1.2f * scale);
                }
                g.setColour(colour);
                g.strokePath(part.stroke, juce::PathStrokeType(1.2f * scale, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded), t);
                g.fillPath(part.fill, t);
                for (const auto& [c, r] : part.circles)
                {
                    const auto p = c.transformedBy(t);
                    g.drawEllipse(p.x - r * scale, p.y - r * scale, 2 * r * scale, 2 * r * scale, 1.2f * scale);
                }
                if (names && part.name.isNotEmpty())
                {
                    // Each part's name beside it, as the mockup writes them.
                    auto label = part.name.upToFirstOccurrenceOf(" ~ ", false, false);
                    if (label.startsWith("The first op-amp") || label.startsWith("The second op-amp"))
                        label = "IC";
                    if (label.startsWith("The step"))
                        continue;
                    auto bounds = part.stroke.getBounds();
                    for (const auto& [c, r] : part.circles)
                        bounds = bounds.getUnion({ c.x - r, c.y - r, 2 * r, 2 * r });
                    const bool tall = bounds.getHeight() > bounds.getWidth() + 2.0f;
                    const auto at = tall ? Pt(bounds.getRight() + 2.0f, bounds.getCentreY() + 2.0f) : Pt(bounds.getCentreX(), bounds.getY() - 1.5f);
                    const auto p = at.transformedBy(t);
                    g.setColour(th.dim);
                    g.setFont(uiFont(5.6f * scale));
                    const float w = 60.0f * scale, h = 8.0f * scale;
                    g.drawText(label, tall ? juce::Rectangle<float>(p.x, p.y - h * 0.75f, w, h) : juce::Rectangle<float>(p.x - w / 2, p.y - h * 0.75f, w, h),
                               tall ? juce::Justification::centredLeft : juce::Justification::centred, false);
                }
            }
        }

        void paintTexts(juce::Graphics& g, const Board& b, const juce::AffineTransform& t, float scale)
        {
            const auto& th = theme();
            for (const auto& text : b.texts)
            {
                g.setColour(text.strong ? th.ink : th.dim);
                g.setFont(uiFont(6.5f * scale, text.strong));
                const auto p = text.at.transformedBy(t);
                const float w = 200.0f * scale, h = 10.0f * scale;
                juce::Rectangle<float> r;
                if (text.justification == juce::Justification::centredLeft)
                    r = { p.x, p.y - h * 0.75f, w, h };
                else if (text.justification == juce::Justification::centredRight)
                    r = { p.x - w, p.y - h * 0.75f, w, h };
                else
                    r = { p.x - w / 2, p.y - h * 0.75f, w, h };
                g.drawText(text.text, r, text.justification, false);
            }
        }
    }

    juce::AffineTransform LidView::sheetTransform() const
    {
        const bool full = impl->showFull && impl->full && impl->full->rec.samples > 1;
        const float w = full ? 690.0f : boardW, h = full ? 282.0f : boardH;
        const float room = static_cast<float>(getHeight()) - (full ? 54.0f : 26.0f);
        const float scale = std::min(getWidth() / w, room / h);
        return juce::AffineTransform::scale(scale).translated((getWidth() - w * scale) / 2.0f, 0.0f);
    }

    void LidView::paintFull(juce::Graphics& g)
    {
        const auto& th = theme();
        auto& f = *impl->full;
        const auto t = sheetTransform();
        const float scale = std::sqrt(std::abs(t.getDeterminant()));

        // The wires and their voltage.
        g.setColour(th.dim);
        for (const auto& e : f.edges)
            g.drawLine(juce::Line<float>(f.nodes[static_cast<std::size_t>(e.a)].p.transformedBy(t), f.nodes[static_cast<std::size_t>(e.b)].p.transformedBy(t)), 1.0f * scale);
        for (const auto& n : f.nodes)
            if (n.edges.size() + n.terms.size() >= 3)
            {
                const auto p = n.p.transformedBy(t);
                g.fillEllipse(p.x - 1.7f * scale, p.y - 1.7f * scale, 3.4f * scale, 3.4f * scale);
            }
        for (const auto& net : f.nets)
        {
            const auto it = f.rec.series.find(net.name);
            if (it == f.rec.series.end())
                continue;
            const float v = f.valueAt(it->second);
            const float m = std::min(1.0f, std::sqrt(std::abs(v) / 15.0f));
            if (m < 0.08f)
                continue;
            g.setColour((v > 0 ? th.vpos : th.vneg).withAlpha(m * 0.9f));
            for (const int e : net.edges)
                g.drawLine(juce::Line<float>(f.nodes[static_cast<std::size_t>(f.edges[static_cast<std::size_t>(e)].a)].p.transformedBy(t),
                                             f.nodes[static_cast<std::size_t>(f.edges[static_cast<std::size_t>(e)].b)].p.transformedBy(t)), 2.8f * scale);
        }
        paintParts(g, f.drawing, t, scale, impl->selected, true);
        for (const auto& [c, r] : f.pins)
        {
            const auto p = c.transformedBy(t);
            g.setColour(th.ink);
            g.drawEllipse(p.x - r * scale, p.y - r * scale, 2 * r * scale, 2 * r * scale, 1.0f * scale);
        }
        paintTexts(g, f.drawing, t, scale);

        // The current: thickness and speed follow the logarithm of it, from under 1 uA to about 2 mA.
        for (const auto& w : f.wires)
        {
            const float current = f.valueAt(w.current);
            const float gl = std::min(1.0f, std::log10(1.0f + std::abs(current) / 1.0e-6f) / 3.5f);
            if (gl < 0.04f)
                continue;
            g.setColour(th.flow.withAlpha(0.45f + 0.55f * gl));
            const Pts pts { w.a, w.b };
            const float length = w.a.getDistanceFrom(w.b);
            const float r = (0.8f + 2.4f * gl) * 0.5f * scale;
            float s = std::fmod(-w.offset, 6.5f);
            if (s < 0.0f)
                s += 6.5f;
            for (; s < length; s += 6.5f)
            {
                const auto p = pointAlong(pts, s).transformedBy(t);
                g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
            }
        }

        // The time and what the circuit does then, and where the numbers come from.
        const double ms = f.msAt(f.pos);
        const juce::String phase = f.pos < 0.3 ? "at rest: only the standing currents flow" : ms <= 1.05 ? "the trigger: a pulse of 1 ms"
                                   : ms <= 6.0 ? "the attack: Q43 holds the pitch up" : "the ring: the resonator sounds on";
        auto bar = getLocalBounds().withTrimmedBottom(26).removeFromBottom(26).toFloat();
        g.setColour(th.ink);
        g.setFont(uiFont(11.5f));
        g.drawText(juce::String(ms, ms < 10.0 ? 2 : 0) + " ms, " + phase, bar.removeFromRight(320.0f), juce::Justification::centredLeft);
        if (impl->selected < 0)
        {
            auto caption = getLocalBounds().removeFromBottom(24).toFloat().withTrimmedRight(260.0f);
            g.setColour(th.dim);
            g.setFont(uiFont(10.5f));
            g.drawFittedText("Recorded from ngspice, the reference the engine is checked against: " + f.rec.setting + ". "
                                 + (f.errors.isEmpty() ? "Checked against the netlist: " + juce::String(static_cast<int>(f.nets.size())) + " nets and "
                                                             + juce::String(static_cast<int>(f.rec.parts.size())) + " parts, all matching."
                                                       : "THE DRAWING DOES NOT MATCH THE NETLIST: " + f.errors.joinIntoString("; ") + "."),
                             caption.toNearestInt(), juce::Justification::centredLeft, 2);
        }
    }

    void LidView::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        if (impl->showFull && impl->full && impl->full->rec.samples > 1)
        {
            paintFull(g);
            paintCaption(g);
            return;
        }
        const auto t = sheetTransform();
        const float scale = std::sqrt(std::abs(t.getDeterminant()));
        const auto& b = impl->board;
        const double now = impl->now;

        // The wires.
        g.setColour(th.dim);
        for (const auto& w : b.wires)
            g.strokePath(w, juce::PathStrokeType(1.0f * scale), t);
        for (const auto& d : b.dots)
        {
            const auto p = d.transformedBy(t);
            g.fillEllipse(p.x - 1.7f * scale, p.y - 1.7f * scale, 3.4f * scale, 3.4f * scale);
        }

        // The voltage tints and the current, as the mockup animates them.
        for (const auto& f : b.flows)
        {
            const double el = f.t0 >= 0.0 ? now - f.t0 : -1.0;
            if (f.kind == Kind::volt)
            {
                double v = f.idle;
                if (el >= 0.0)
                {
                    const double tau = f.duration / 2.0;
                    switch (f.mode)
                    {
                        case Mode::pulse: v = el < 140.0 ? 1.0 : 0.0; break;
                        case Mode::burst: v = el < 120.0 ? 0.9 : 0.8 * std::exp(-(el - 120.0) / tau); break;
                        case Mode::ac: v = std::sin(now / 1000.0 * juce::MathConstants<double>::twoPi * f.frequency + f.phase) * 0.9 * std::exp(-el / tau); break;
                        default: v = 0.9 * std::exp(-el / tau); break;
                    }
                }
                const double m = std::abs(v);
                if (m < 0.03)
                    continue;
                g.setColour((v > 0 ? th.vpos : th.vneg).withAlpha(static_cast<float>(std::min(0.9, m))));
                g.strokePath(polyline(f.points), juce::PathStrokeType(2.8f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
                continue;
            }
            double a = f.idle;
            if (el >= 0.0)
            {
                const double tau = f.duration / 3.0;
                double e;
                if (f.kind == Kind::pulse)
                    e = el < 140.0 ? 1.0 : 0.0;
                else if (f.kind == Kind::burst || f.burstEnvelope)
                    e = (el < 22.0 || (el > 40.0 && el < 62.0) || (el > 80.0 && el < 102.0)) ? 0.7 : el > 120.0 ? 0.55 * std::exp(-(el - 120.0) / tau) : 0.04;
                else if (f.kind == Kind::env)
                    e = 0.6 * std::exp(-el / tau);
                else
                    e = f.level * std::exp(-el / tau);
                a = std::max(a, e);
            }
            if (a < 0.012)
                continue;
            const float width = static_cast<float>(1.2 + 3.6 * std::sqrt(a));
            g.setColour(th.flow.withAlpha(static_cast<float>(std::min(1.0, 0.4 + a * 2.0))));
            const float spacing = 6.5f;
            float s = std::fmod(-f.offset, spacing);
            if (s < 0.0f)
                s += spacing;
            for (; s < f.length; s += spacing)
            {
                const auto p = pointAlong(f.points, s).transformedBy(t);
                const float r = width * 0.5f * scale;
                g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
            }
        }

        // The parts on top.
        paintParts(g, b, t, scale, impl->selected, false);
        paintTexts(g, b, t, scale);

        paintCaption(g);
    }

    void LidView::paintCaption(juce::Graphics& g)
    {
        const auto& th = theme();
        const auto& b = impl->showFull && impl->full ? impl->full->drawing : impl->board;
        auto caption = getLocalBounds().removeFromBottom(24).toFloat().withTrimmedRight(260.0f);
        if (impl->selected < 0 && impl->showFull && impl->full)
            return; // the complete drum writes its own: where its numbers come from
        if (impl->selected >= 0 && impl->selected < static_cast<int>(b.parts.size()))
        {
            const auto& part = b.parts[static_cast<std::size_t>(impl->selected)];
            juce::String drum = part.lane >= 0 ? juce::String(instruments[static_cast<std::size_t>(part.lane)].name) + ": " : juce::String();
            g.setColour(th.ink);
            g.setFont(uiFont(12.0f, true));
            g.drawText(drum + part.name.upToFirstOccurrenceOf(" ~ ", false, false), caption.removeFromLeft(caption.getWidth() * 0.35f), juce::Justification::centredLeft);
            g.setColour(th.dim);
            g.setFont(uiFont(11.5f));
            g.drawText(part.name.fromFirstOccurrenceOf(" ~ ", false, false), caption, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(th.dim);
            g.setFont(uiFont(11.5f));
            g.drawText("Every drum's circuit, reduced to the parts that make its sound. Click a part to read it; play, and the current lights up.",
                       caption, juce::Justification::centredLeft);
        }
    }

    void LidView::mouseUp(const juce::MouseEvent& e)
    {
        const auto at = e.position.transformedBy(sheetTransform().inverted());
        int found = -1;
        float best = 1.0e9f;
        const auto& parts = impl->showFull && impl->full ? impl->full->drawing.parts : impl->board.parts;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i].name.isEmpty())
                continue;
            auto bounds = parts[i].stroke.getBounds().getUnion(parts[i].box);
            for (const auto& [c, r] : parts[i].circles)
                bounds = bounds.getUnion({ c.x - r, c.y - r, 2 * r, 2 * r });
            if (bounds.expanded(3.0f).contains(at))
            {
                const float d = bounds.getCentre().getDistanceFrom(at);
                if (d < best)
                {
                    best = d;
                    found = static_cast<int>(i);
                }
            }
        }
        impl->selected = found;
        repaint();
    }
}
