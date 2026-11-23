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
            // Its label on the complete drum, worked out once (Board::prepareLabels), not at every frame.
            juce::String label;
            Pt labelAt;
            bool labelTall = false;
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
            juce::Path path; // the points as a line, made once
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

            /** Each part's label and where it goes, as the mockup writes them: once, after the drawing is built. */
            void prepareLabels()
            {
                for (auto& part : parts)
                {
                    auto label = part.name.upToFirstOccurrenceOf(" ~ ", false, false);
                    if (label.startsWith("The first op-amp") || label.startsWith("The second op-amp"))
                        label = "IC";
                    if (label.startsWith("The step"))
                        label = {};
                    part.label = label;
                    auto bounds = part.stroke.getBounds();
                    for (const auto& [c, r] : part.circles)
                        bounds = bounds.getUnion({ c.x - r, c.y - r, 2 * r, 2 * r });
                    part.labelTall = bounds.getHeight() > bounds.getWidth() + 2.0f;
                    part.labelAt = part.labelTall ? Pt(bounds.getRight() + 2.0f, bounds.getCentreY() + 2.0f)
                                                  : Pt(bounds.getCentreX(), bounds.getY() - 1.5f);
                }
            }

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
                    f.path = polyline(f.points);
                    f.offset = r.nextFloat() * 6.5f;
                    f.phase = r.nextFloat() * 6.5f;
                }
                prepareLabels();
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

        // The drums drawn complete: the bass drum, then the snare (D278).
        enum DrumDrawn { bassDrum = 0, snareDrum = 1, lowTom = 2, rimShot = 3, handClap = 4, drumsDrawn = 5 };
        /** The lane each drum drawn complete plays in. */
        constexpr int laneOfDrum[drumsDrawn] { 0, 1, 2, 5, 6 };

        Recording loadRecording(int which)
        {
            Recording r;
            const auto root = which == snareDrum ? juce::JSON::parse(juce::String::fromUTF8(LibraryData::snare_currents_json, LibraryData::snare_currents_jsonSize))
                              : which == lowTom  ? juce::JSON::parse(juce::String::fromUTF8(LibraryData::tom_currents_json, LibraryData::tom_currents_jsonSize))
                              : which == rimShot ? juce::JSON::parse(juce::String::fromUTF8(LibraryData::rimshot_currents_json, LibraryData::rimshot_currents_jsonSize))
                              : which == handClap ? juce::JSON::parse(juce::String::fromUTF8(LibraryData::clap_currents_json, LibraryData::clap_currents_jsonSize))
                                                 : juce::JSON::parse(juce::String::fromUTF8(LibraryData::bassdrum_currents_json, LibraryData::bassdrum_currents_jsonSize));
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
            if (which == handClap)
                r.setting << "the hand clap (the maracas' side, switched out, not drawn), " << set.getProperty("amp", "?").toString()
                          << " V on the trigger line, Level " << set.getProperty("lev", "?").toString() << ", C.P. OFFSET at "
                          << set.getProperty("tm3", "?").toString() << ", the noise bus at 130 mV rms (two sines standing in for it)";
            else if (which == rimShot)
                r.setting << "the rim shot (the claves' parts switched out), " << set.getProperty("amp", "?").toString()
                          << " V on the trigger line, Level " << set.getProperty("lev", "?").toString();
            else if (which == lowTom)
                r.setting << "the low tom, " << set.getProperty("amp", "?").toString() << " V on the trigger line, Tuning "
                          << set.getProperty("tune", "?").toString() << ", Level " << set.getProperty("lev", "?").toString()
                          << ", the pink noise as a " << juce::String(juce::roundToInt(set.getProperty("pn", "0").toString().getDoubleValue() * 1000.0))
                          << " mV sine at 150 Hz (so the hit is the same every time)";
            else if (which == snareDrum)
                r.setting << set.getProperty("amp", "?").toString() << " V on the trigger line, Tone " << set.getProperty("tone", "?").toString()
                          << ", Snappy " << set.getProperty("snappy", "?").toString() << ", Level " << set.getProperty("lev", "?").toString()
                          << ", the noise bus at " << juce::String(juce::roundToInt(set.getProperty("wn", "0").toString().getDoubleValue() * 1000.0))
                          << " mV rms (two sines standing in for it, so the hit is the same every time)";
            else
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

            int which = bassDrum;
            float width = 690.0f, height = 282.0f; // the sheet, in drawing units
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

            /** What the circuit does at this moment of the hit, for the time bar. */
            [[nodiscard]] juce::String phaseAt(double ms) const
            {
                if (pos < 0.3)
                    return "at rest: only the standing currents flow";
                if (ms <= 1.05)
                    return "the trigger: a pulse of 1 ms";
                if (which == handClap)
                    return ms <= 32.0 ? "the claps: the sawtooth fires the VCA again and again, each clap the band-passed noise"
                                      : "the reverb: Q70 lets the noise through as its envelope dies away";
                if (which == rimShot)
                    return ms <= 5.0 ? "the attack: both resonators ring, and Q62 lets them through while the envelope lasts"
                                     : "the tail: the envelope has closed Q62, the resonators ring on unheard";
                if (which == lowTom)
                    return ms <= 15.0 ? "the attack: the resonator rings, the diodes bend its pitch down, the noise gate opens"
                                      : "the ring: the tone dies away, the gate lets less and less noise through";
                if (which == snareDrum)
                    return ms <= 12.0 ? "the attack: both shells ring and the snappy's envelope opens"
                                      : "the tail: the shells die away, the noise follows the snappy's envelope";
                return ms <= 6.0 ? "the attack: Q43 holds the pitch up" : "the ring: the resonator sounds on";
            }

            explicit FullDrum(int drum) : which(drum), rec(loadRecording(drum))
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
                const auto wiperAt = [&](float x, float y)
                {
                    auto& w = a.add({});
                    w.fill.addTriangle({ x + 3.5f, y }, { x + 8.5f, y - 2.5f }, { x + 8.5f, y + 2.5f });
                };

                if (which == snareDrum || which == lowTom || which == rimShot || which == handClap)
                {
                    if (which == snareDrum)
                        buildSnare(a, W, term, R, C, POT, DI, Q, pin, open, wiperAt, two);
                    else if (which == rimShot)
                        buildRimShot(a, W, term, R, C, POT, DI, Q, pin, open, two);
                    else if (which == handClap)
                        buildClap(a, W, term, R, C, POT, DI, Q, pin, open, wiperAt, two);
                    else
                        buildTom(a, W, term, R, C, POT, DI, Q, pin, open, wiperAt, two);
                    buildGraph();
                    drawing.prepareLabels();
                    return;
                }

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
                drawing.prepareLabels();
            }

            /*
                The snare (D278): every part of the engine's tools/tr808/snare.cir in its changed version, as the
                service notes draw it. The trigger and the snare's switch as the bass drum's; the two bridged-T shells
                (IC14a lower, IC14b upper) and the Tone pot between them along the top; the Snappy's envelope, the
                noise VCA Q48 and the high-pass Q49 along the bottom; the mixer IC13 with the Level pot on the right.
                The op-amps' own parts and the noise generator are drawn as symbols and a pin.
            */
            void buildSnare(Board::Sheet& a, auto& W, auto& term, auto& R, auto& C, auto& POT, auto& DI, auto& Q, auto& pin,
                            auto& open, auto& wiper, auto& two)
            {
                width = 690.0f;
                height = 362.0f;
                const auto L = juce::Justification::centredLeft;
                const auto Rj = juce::Justification::centredRight;
                const auto M = juce::Justification::centred;

                // The rails: +15 V along the top and down to the lower half; 0 V along the bottom, and a second
                // ground line under the shells.
                pin(40, 12, "vcc", "+15 V", 40, 7, M); W({ { 40, 12 }, { 560, 12 } }); W({ { 560, 12 }, { 560, 208 } }); W({ { 230, 208 }, { 560, 208 } });
                pin(40, 340, "0", "0 V", 40, 351, M); W({ { 40, 340 }, { 600, 340 } });
                W({ { 168, 196 }, { 168, 340 } }); W({ { 168, 196 }, { 470, 196 } });

                // The common trigger, as the bass drum's
                {
                    auto& p = a.add("The step's pulse ~ 1 ms, from the sequencer");
                    p.circles.push_back({ Pt(17, 150), 8.5f });
                }
                a.glyph({ { 12, 153 }, { 15, 153 }, { 15, 147 }, { 19, 147 }, { 19, 153 }, { 22, 153 } });
                term(Pt(25.5f, 150), {}, "n6", true); W({ { 25.5f, 150 }, { 28, 150 } });
                R("r117", 28, 150, 'h', 22, "R117 ~ 47 k"); Q("q31", 66, 150, false, "Q31 ~ turns off for the pulse, letting Q32 rise"); W({ { 66, 162 }, { 66, 340 } });
                W({ { 66, 138 }, { 66, 106 } }); R("r120", 66, 84, 'v', 22, "R120 ~ 4.7 k"); pin(66, 84, "nx", "ACCENT, 5 TO 15 V", 71, 80, L);
                W({ { 66, 110 }, { 76, 110 } }); Q("q32", 92, 110, false, "Q32 ~ the follower onto the trigger line"); W({ { 92, 98 }, { 92, 12 } });
                W({ { 92, 122 }, { 92, 130 } }); R("r113", 92, 130, 'v', 22, "R113 ~ 4.7 k, holds the line down"); W({ { 92, 152 }, { 92, 166 }, { 66, 166 } });
                W({ { 92, 130 }, { 110, 130 }, { 110, 112 }, { 150, 112 }, { 150, 118 } });
                // The snare's switch
                R("r183", 122, 112, 'v', 18, "R183 ~ 4.7 k"); W({ { 122, 130 }, { 134, 130 } });
                Q("q46", 150, 130, true, "Q46 ~ a PNP switch: it can only push current into the snare");
                R("r182", 122, 130, 'v', 26, "R182 ~ 10 k"); W({ { 122, 156 }, { 122, 160 } }); Q("q45", 122, 172, false, "Q45 ~ on while the snare's step data stands"); W({ { 122, 184 }, { 122, 340 } });
                pin(74, 172, "nsd", "SD ON", 72, 182, M); W({ { 74, 172 }, { 76, 172 } }); R("r180", 76, 172, 'h', 22, "R180 ~ 22 k"); W({ { 98, 172 }, { 106, 172 } });
                R("r181", 102, 172, 'v', 18, "R181 ~ 10 k"); W({ { 102, 190 }, { 102, 340 } });
                // The switched pulse: up to the shells, down to the Snappy
                W({ { 150, 142 }, { 150, 220 }, { 196, 220 }, { 196, 226 } }); W({ { 176, 220 }, { 176, 30 }, { 326, 30 }, { 326, 46 } });

                // The lower shell: IC14a and its bridged-T
                R("r189", 176, 46, 'h', 22, "R189 ~ 100 k"); C("c57", 176, 60, 'h', 14, "C57 ~ 6.8 nF: the click on the front of the pulse");
                W({ { 190, 60 }, { 204, 60 } }); W({ { 198, 46 }, { 204, 46 }, { 204, 158 }, { 264, 158 } });
                R("r190", 216, 158, 'v', 18, "R190 ~ 680"); W({ { 216, 176 }, { 216, 196 } });
                a.OP(264, 140, "IC14a ~ the lower shell: a bridged-T that rings at about 180 Hz"); open(264, 146, "nm1"); open(264, 158, "np1"); term(Pt(290, 152), {}, "no1", true);
                W({ { 264, 146 }, { 256, 146 }, { 256, 100 }, { 246, 100 } }); C("c58", 232, 100, 'h', 14, "C58 ~ 0.056 uF", true);
                W({ { 232, 100 }, { 232, 70 } }); C("c59", 232, 70, 'h', 14, "C59 ~ 0.027 uF"); W({ { 246, 70 }, { 304, 70 }, { 304, 152 }, { 290, 152 } });
                W({ { 256, 100 }, { 256, 86 } }); R("r197", 256, 86, 'h', 22, "R197 ~ 820 k: how long the shell rings"); W({ { 278, 86 }, { 304, 86 } });
                R("r196", 232, 100, 'v', 16, "R196 ~ 680"); W({ { 232, 116 }, { 232, 196 } });

                // The upper shell: IC14b and its bridged-T
                R("r191", 326, 46, 'h', 22, "R191 ~ 10 k"); W({ { 348, 46 }, { 354, 46 }, { 354, 158 }, { 414, 158 } });
                R("r192", 366, 158, 'v', 18, "R192 ~ 220"); W({ { 366, 176 }, { 366, 196 } });
                a.OP(414, 140, "IC14b ~ the upper shell: a bridged-T that rings at about 330 Hz"); open(414, 146, "nm2"); open(414, 158, "np2"); term(Pt(440, 152), {}, "no2", true);
                W({ { 414, 146 }, { 406, 146 }, { 406, 100 }, { 396, 100 } }); C("c60", 382, 100, 'h', 14, "C60 ~ 6.8 nF", true);
                W({ { 382, 100 }, { 382, 70 } }); C("c61", 382, 70, 'h', 14, "C61 ~ 0.015 uF"); W({ { 396, 70 }, { 454, 70 }, { 454, 152 }, { 440, 152 } });
                W({ { 406, 100 }, { 406, 86 } }); R("r198", 406, 86, 'h', 22, "R198 ~ 1 M"); W({ { 428, 86 }, { 454, 86 } });
                R("r195", 382, 100, 'v', 16, "R195 ~ 2.2 k"); W({ { 382, 116 }, { 382, 196 } });

                // The Tone pot between the two shells, its wiper into the mixer
                R("r200", 454, 70, 'h', 18, "R200 ~ a link in the changed version"); C("c65", 472, 70, 'h', 14, "C65 ~ 0.047 uF"); W({ { 486, 70 }, { 490, 70 }, { 490, 104 } });
                W({ { 304, 152 }, { 304, 190 }, { 462, 190 } }); C("c63", 462, 190, 'h', 14, "C63 ~ 0.047 uF"); W({ { 476, 190 }, { 490, 190 }, { 490, 144 } });
                a.R(490, 104, false, 40, "VR8 TONE ~ 100 k: the upper shell at the top, the lower at the bottom");
                two("rv8a", Pt(490, 104), Pt(490, 124)); two("rv8b", Pt(490, 124), Pt(490, 144)); wiper(490, 124);
                W({ { 490, 124 }, { 500, 124 } }); R("r204", 500, 124, 'h', 22, "R204 ~ 100 k"); W({ { 522, 124 }, { 580, 124 } });

                // The Snappy: its pot, the envelope transistor Q47 and the envelope's filter
                a.R(196, 226, false, 40, "VR9 SNAPPY ~ 10 k: how much of the pulse charges the envelope");
                two("rv9a", Pt(196, 226), Pt(196, 246)); two("rv9b", Pt(196, 246), Pt(196, 266)); wiper(196, 246);
                R("r199", 196, 266, 'v', 18, "R199 ~ 1 k"); W({ { 196, 284 }, { 196, 340 } });
                W({ { 196, 246 }, { 206, 246 } }); R("r186", 206, 246, 'h', 22, "R186 ~ 33 k"); W({ { 228, 246 }, { 244, 246 } });
                Q("q47", 260, 246, false, "Q47 ~ charges C51 for the length of the pulse");
                W({ { 260, 234 }, { 260, 224 }, { 266, 224 } }); R("r187", 266, 224, 'h', 22, "R187 ~ 330", true);
                R("r193", 300, 208, 'v', 16, "R193 ~ 100"); W({ { 288, 224 }, { 300, 224 } }); C("c55", 300, 224, 'v', 14, "C55 ~ 47 uF: Q47's own supply"); W({ { 300, 238 }, { 300, 340 } });
                W({ { 260, 258 }, { 260, 270 }, { 272, 270 } }); C("c51", 260, 270, 'v', 14, "C51 ~ 0.47 uF: holds the envelope"); W({ { 260, 284 }, { 260, 340 } });
                R("r184", 272, 270, 'h', 22, "R184 ~ 15 k"); W({ { 294, 270 }, { 324, 270 } }); C("c52", 316, 270, 'v', 14, "C52 ~ 0.1 uF"); W({ { 316, 284 }, { 316, 340 } });
                R("r185", 324, 270, 'h', 22, "R185 ~ 15 k"); W({ { 346, 270 }, { 430, 270 } }); C("c53", 360, 270, 'v', 14, "C53 ~ 1 nF"); W({ { 360, 284 }, { 360, 340 } });

                // The noise VCA: the white noise into Q48's base, its collector fed from the envelope through D54
                pin(338, 250, "nwn", "WHITE NOISE, 130 mV", 338, 241, M); W({ { 338, 250 }, { 350, 250 } });
                C("c56", 350, 250, 'h', 14, "C56 ~ 0.022 uF"); W({ { 364, 250 }, { 376, 250 } });
                W({ { 376, 208 }, { 376, 214 } }); R("r194", 376, 214, 'v', 22, "R194 ~ 2.2 M"); W({ { 376, 236 }, { 376, 302 } });
                DI("d54", 392, 270, 20, "D54 ~ lets the envelope feed Q48");
                Q("q48", 392, 302, false, "Q48 ~ the swing VCA: the noise, shaped by the envelope");
                R("r188", 392, 314, 'v', 16, "R188 ~ 100"); W({ { 392, 330 }, { 392, 340 } });
                W({ { 392, 290 }, { 408, 290 } }); C("c54", 408, 290, 'v', 14, "C54 ~ 2.7 nF"); W({ { 408, 304 }, { 408, 340 } });

                // The high-pass after the VCA: Q49, a follower in a two-pole filter
                C("c66", 430, 270, 'h', 14, "C66 ~ 1.8 nF"); W({ { 444, 270 }, { 450, 270 } }); C("c67", 450, 270, 'h', 14, "C67 ~ 1.8 nF");
                W({ { 464, 270 }, { 476, 270 } }); R("r202", 470, 270, 'v', 20, "R202 ~ 47 k"); W({ { 470, 290 }, { 470, 340 } });
                Q("q49", 492, 270, false, "Q49 ~ the follower of the high-pass: only the hiss is left"); W({ { 492, 258 }, { 492, 208 } });
                R("r201", 444, 270, 'v', 20, "R201 ~ 22 k"); W({ { 444, 290 }, { 444, 300 }, { 492, 300 }, { 492, 282 } });
                R("r203", 492, 300, 'v', 20, "R203 ~ 22 k"); pin(492, 320, "vee", "-15 V", 498, 324, L);
                W({ { 492, 290 }, { 506, 290 } }); C("c69", 506, 290, 'h', 14, "C69 ~ 0.01 uF"); R("r205", 520, 290, 'h', 22, "R205 ~ 27 k"); W({ { 542, 290 }, { 580, 290 } });

                // The mixer, IC13, and the Level
                W({ { 580, 72 }, { 580, 290 } });
                a.OP(600, 130, "IC13 ~ the mixer: the shells and the snappy summed"); open(600, 136, "nsum"); open(600, 148, "0"); term(Pt(626, 142), {}, "vout", true);
                W({ { 580, 136 }, { 600, 136 } }); W({ { 600, 148 }, { 592, 148 }, { 592, 340 } });
                C("c70", 580, 72, 'h', 14, "C70 ~ 220 pF"); W({ { 594, 72 }, { 640, 72 } });
                R("r210", 580, 90, 'h', 22, "R210 ~ 470 k"); W({ { 602, 90 }, { 640, 90 } });
                POT("rvr7", 580, 110, 'h', 22, "VR7 LEVEL ~ 100 k"); C("c68", 602, 110, 'h', 14, "C68 ~ 33 uF"); W({ { 616, 110 }, { 640, 110 } });
                W({ { 626, 142 }, { 640, 142 }, { 640, 72 } }); W({ { 640, 142 }, { 660, 142 } });
                {
                    auto& out = a.add({});
                    out.fill.addTriangle({ 668, 142 }, { 660, 138.5f }, { 660, 145.5f });
                }
                a.tx(660, 136, "OUT", Rj);
            }

            /*
                The low tom (D278): every part of the engine's tools/tr808/tom.cir with the low pair's values, as a
                tom (the conga's parts drawn, switched out). The mid and high toms are the same circuit with other
                values. The trigger and the tom's switch as the bass drum's; the pulse into the resonator, a bridged-T
                around IC15a with the Tuning pot and the germanium pair that bends the pitch, IC15b inverting it back;
                the pink noise gate along the bottom; the mixer IC13 with the Level pot.
            */
            void buildTom(Board::Sheet& a, auto& W, auto& term, auto& R, auto& C, auto& POT, auto& DI, auto& Q, auto& pin,
                          auto& open, auto& wiper, auto& two)
            {
                width = 640.0f;
                height = 362.0f;
                const auto L = juce::Justification::centredLeft;
                const auto Rj = juce::Justification::centredRight;
                const auto M = juce::Justification::centred;
                // A diode with its anode at the bottom: the cathode at (x, y).
                const auto DU = [&](const std::string& el, float x, float y, float Lh, const juce::String& label)
                {
                    auto& part = a.add(label);
                    const float h = (Lh - 6.0f) / 2.0f;
                    const Pt o(x, y);
                    part.stroke.addLineSegment({ o, o + Pt(0, h) }, 0);
                    part.stroke.addLineSegment({ o + Pt(-3.5f, h), o + Pt(3.5f, h) }, 0);
                    part.stroke.addTriangle(o + Pt(0, h), o + Pt(-3.5f, h + 6), o + Pt(3.5f, h + 6));
                    part.stroke.addLineSegment({ o + Pt(0, h + 6), o + Pt(0, Lh) }, 0);
                    two(el, Pt(x, y + Lh), Pt(x, y), el + ".id");
                };

                // The rails: +15 V along the top and down to the lower half; 0 V along the bottom, and a second
                // ground line under the resonator.
                pin(40, 12, "vcc", "+15 V", 40, 7, M); W({ { 40, 12 }, { 600, 12 } }); W({ { 600, 12 }, { 600, 208 } }); W({ { 230, 208 }, { 600, 208 } });
                pin(40, 340, "0", "0 V", 40, 351, M); W({ { 40, 340 }, { 520, 340 } });
                W({ { 168, 148 }, { 168, 196 }, { 168, 340 } }); W({ { 160, 196 }, { 520, 196 } });

                // The common trigger, as the bass drum's
                {
                    auto& p = a.add("The step's pulse ~ 1 ms, from the sequencer");
                    p.circles.push_back({ Pt(17, 150), 8.5f });
                }
                a.glyph({ { 12, 153 }, { 15, 153 }, { 15, 147 }, { 19, 147 }, { 19, 153 }, { 22, 153 } });
                term(Pt(25.5f, 150), {}, "n6", true); W({ { 25.5f, 150 }, { 28, 150 } });
                R("r117", 28, 150, 'h', 22, "R117 ~ 47 k"); Q("q31", 66, 150, false, "Q31 ~ turns off for the pulse, letting Q32 rise"); W({ { 66, 162 }, { 66, 340 } });
                W({ { 66, 138 }, { 66, 106 } }); R("r120", 66, 84, 'v', 22, "R120 ~ 4.7 k"); pin(66, 84, "nx", "ACCENT, 5 TO 15 V", 71, 80, L);
                W({ { 66, 110 }, { 76, 110 } }); Q("q32", 92, 110, false, "Q32 ~ the follower onto the trigger line"); W({ { 92, 98 }, { 92, 12 } });
                W({ { 92, 122 }, { 92, 130 } }); R("r113", 92, 130, 'v', 22, "R113 ~ 4.7 k, holds the line down"); W({ { 92, 152 }, { 92, 166 }, { 66, 166 } });
                W({ { 92, 130 }, { 110, 130 }, { 110, 112 }, { 150, 112 }, { 150, 118 } });
                // The tom's switch
                R("r214", 122, 112, 'v', 18, "R214 ~ 4.7 k"); W({ { 122, 130 }, { 134, 130 } });
                Q("q51", 150, 130, true, "Q51 ~ a PNP switch: it can only push current into the tom");
                R("r213", 122, 130, 'v', 26, "R213 ~ 10 k"); W({ { 122, 156 }, { 122, 160 } }); Q("q50", 122, 172, false, "Q50 ~ on while the tom's step data stands"); W({ { 122, 184 }, { 122, 340 } });
                pin(74, 172, "nlt", "LT ON", 72, 182, M); W({ { 74, 172 }, { 76, 172 } }); R("r211", 76, 172, 'h', 22, "R211 ~ 22 k"); W({ { 98, 172 }, { 106, 172 } });
                R("r212", 102, 172, 'v', 18, "R212 ~ 10 k"); W({ { 102, 190 }, { 102, 340 } });
                W({ { 150, 142 }, { 150, 220 }, { 196, 220 } }); W({ { 150, 150 }, { 160, 150 } }); R("r215", 160, 150, 'v', 18, "R215 ~ 4.7 k"); W({ { 160, 168 }, { 160, 196 } });

                // The pulse into the resonator's + input
                W({ { 176, 220 }, { 176, 170 } }); C("c73", 176, 170, 'h', 14, "C73 ~ 6.8 nF: the pulse's front edge"); W({ { 190, 170 }, { 208, 170 } });
                DU("d56", 208, 170, 20, "D56 ~ clamps the falling edge"); W({ { 208, 190 }, { 208, 196 } });
                R("r226", 208, 170, 'h', 22, "R226 ~ 22 k"); W({ { 230, 170 }, { 276, 170 }, { 276, 158 }, { 290, 158 } });
                R("r225", 238, 170, 'v', 18, "R225 ~ 4.7 k"); W({ { 238, 188 }, { 238, 196 } });
                R("r224", 262, 170, 'v', 18, "R224 ~ 1.5 k, switched in for the conga"); W({ { 262, 188 }, { 262, 196 } });

                // The resonator: IC15a and its bridged-T, the Tuning pot and the germanium pair on the middle node
                a.OP(290, 140, "IC15a ~ the resonator: a bridged-T that rings at the tom's pitch"); open(290, 146, "ninv"); open(290, 158, "np"); term(Pt(316, 152), {}, "no1", true);
                W({ { 290, 146 }, { 282, 146 }, { 282, 88 }, { 272, 88 } }); W({ { 272, 104 }, { 282, 104 } });
                C("c76", 258, 104, 'h', 14, "C76 ~ 0.056 uF", true); C("cint", 258, 88, 'h', 14, "C89 ~ on the mid tom only: 1 pF here, as if absent", true);
                W({ { 258, 40 }, { 258, 104 } }); C("c78", 258, 56, 'h', 14, "C78 ~ 0.012 uF"); C("c77", 258, 72, 'h', 14, "C77 ~ 0.047 uF, for the tom only");
                W({ { 272, 56 }, { 330, 56 } }); W({ { 272, 72 }, { 330, 72 } }); W({ { 330, 56 }, { 330, 152 }, { 316, 152 } });
                W({ { 282, 124 }, { 296, 124 } }); R("r228", 296, 124, 'h', 22, "R228 ~ 820 k"); W({ { 318, 124 }, { 330, 124 } });
                W({ { 236, 70 }, { 214, 70 }, { 214, 104 }, { 258, 104 } });
                R("r231", 214, 104, 'v', 18, "R231 ~ 1 k"); W({ { 214, 122 }, { 214, 128 }, { 200, 128 } });
                a.R(200, 108, false, 40, "VR TUNING ~ 500 ohm: the pitch");
                two("rva", Pt(200, 108), Pt(200, 128)); two("rvb", Pt(200, 128), Pt(200, 148)); wiper(200, 128);
                W({ { 200, 148 }, { 168, 148 } });
                W({ { 200, 108 }, { 200, 50 }, { 236, 50 } });
                DU("d80", 222, 50, 20, "D80 ~ germanium: bends the pitch down as the tom rings out");
                DI("d81", 236, 50, 20, "D81 ~ germanium, the other way");

                // IC15b inverts the resonator's output and R230 feeds it back to the middle node
                W({ { 330, 146 }, { 340, 146 } }); R("r229", 340, 146, 'h', 22, "R229 ~ 33 k"); W({ { 362, 146 }, { 380, 146 } });
                a.OP(380, 140, "IC15b ~ turns the resonator's output over"); open(380, 146, "nin2"); open(380, 158, "0"); term(Pt(406, 152), {}, "no2", true);
                W({ { 380, 158 }, { 372, 158 }, { 372, 196 } });
                W({ { 370, 146 }, { 370, 120 } }); R("r227", 370, 120, 'h', 22, "R227 ~ 33 k"); W({ { 392, 120 }, { 416, 120 }, { 416, 152 }, { 406, 152 } });
                W({ { 416, 120 }, { 416, 40 }, { 292, 40 } }); R("r230", 270, 40, 'h', 22, "R230 ~ 865 k: how long the tom rings", true); W({ { 258, 40 }, { 270, 40 } });

                // The noise: the envelope (R219, D55, C72), the pink noise through R216 and C71, Q52 the gate, its filter
                R("r219", 196, 220, 'h', 22, "R219 ~ 4.7 k"); W({ { 218, 220 }, { 226, 220 } });
                DI("d55", 226, 220, 20, "D55"); W({ { 226, 240 }, { 226, 250 } });
                C("c72", 226, 250, 'v', 14, "C72 ~ 0.039 uF: the noise's envelope"); W({ { 226, 264 }, { 226, 340 } });
                W({ { 226, 246 }, { 236, 246 } }); R("r218", 236, 246, 'h', 22, "R218 ~ 2.2 M"); W({ { 258, 246 }, { 300, 246 } });
                pin(318, 262, "npn", "PINK NOISE", 318, 274, M); W({ { 312, 262 }, { 318, 262 } });
                R("r216", 290, 262, 'h', 22, "R216 ~ 15 k", true); W({ { 266, 262 }, { 290, 262 } });
                C("c71", 266, 248, 'v', 14, "C71 ~ 0.047 uF", true); W({ { 266, 246 }, { 266, 248 } });
                R("r217", 280, 262, 'v', 18, "R217 ~ 3.3 k"); W({ { 280, 280 }, { 280, 340 } });
                R("r220", 300, 246, 'h', 22, "R220 ~ 33 k");
                Q("q52", 338, 246, false, "Q52 ~ the gate: lets the noise through while the envelope lasts"); W({ { 338, 258 }, { 338, 340 } });
                R("r221", 338, 208, 'v', 18, "R221 ~ 4.7 k"); W({ { 338, 226 }, { 338, 234 } });
                W({ { 338, 230 }, { 350, 230 } }); C("c74", 350, 230, 'h', 14, "C74 ~ 0.047 uF"); W({ { 364, 230 }, { 380, 230 } });
                R("r223", 372, 230, 'v', 18, "R223 ~ 22 k"); W({ { 372, 248 }, { 372, 340 } });
                R("r222", 380, 230, 'h', 22, "R222 ~ 22 k"); W({ { 402, 230 }, { 430, 230 } });
                C("c75", 414, 230, 'v', 14, "C75 ~ 0.018 uF"); W({ { 414, 244 }, { 414, 340 } });
                R("r232", 430, 230, 'h', 22, "R232 ~ 100 k, open for the conga"); W({ { 452, 230 }, { 500, 230 } });

                // The mixer, IC13, and the Level
                W({ { 416, 152 }, { 430, 152 } }); R("r233", 430, 152, 'h', 22, "R233 ~ 100 k"); C("c81", 452, 152, 'h', 14, "C81 ~ 0.047 uF"); W({ { 466, 152 }, { 500, 152 } });
                W({ { 500, 84 }, { 500, 230 } });
                a.OP(520, 140, "IC13 ~ the mixer: the tone and the noise summed"); open(520, 146, "nsum"); open(520, 158, "0"); term(Pt(546, 152), {}, "vout", true);
                W({ { 500, 146 }, { 520, 146 } }); W({ { 520, 158 }, { 512, 158 }, { 512, 196 } });
                C("c83", 500, 84, 'h', 14, "C83 ~ 1 nF"); W({ { 514, 84 }, { 560, 84 } });
                R("r235", 500, 100, 'h', 22, "R235 ~ 470 k"); W({ { 522, 100 }, { 560, 100 } });
                POT("rvr10", 500, 116, 'h', 22, "VR10 LEVEL ~ 100 k"); C("c82", 522, 116, 'h', 14, "C82 ~ 33 uF"); W({ { 536, 116 }, { 560, 116 } });
                W({ { 546, 152 }, { 560, 152 }, { 560, 84 } }); W({ { 560, 152 }, { 580, 152 } });
                {
                    auto& out = a.add({});
                    out.fill.addTriangle({ 588, 152 }, { 580, 148.5f }, { 580, 155.5f });
                }
                a.tx(580, 146, "OUT", Rj);
            }

            /*
                The rim shot (D278): every part of the engine's tools/tr808/rimclave.cir as a rim shot (the claves'
                parts drawn, switched out by SW11). Two pulses into two bridged-T resonators along the top, IC21 at
                about 455 Hz and IC20a at about 1.8 kHz with IC20b turning it over; along the bottom the envelope, the
                VCA Q62 that lets the two through while it lasts, the high-pass Q63 and the way into the mixer IC19.
            */
            void buildRimShot(Board::Sheet& a, auto& W, auto& term, auto& R, auto& C, auto& POT, auto& DI, auto& Q,
                              auto& pin, auto& open, auto& two)
            {
                width = 700.0f;
                height = 362.0f;
                const auto L = juce::Justification::centredLeft;
                const auto Rj = juce::Justification::centredRight;
                const auto M = juce::Justification::centred;
                // A diode lying down, its anode at (x, y) on the left.
                const auto DH = [&](const std::string& el, float x, float y, float Lw, const juce::String& label)
                {
                    auto& part = a.add(label);
                    const float h = (Lw - 6.0f) / 2.0f;
                    const Pt o(x, y);
                    part.stroke.addLineSegment({ o, o + Pt(h, 0) }, 0);
                    part.stroke.addTriangle(o + Pt(h, -3.5f), o + Pt(h, 3.5f), o + Pt(h + 6, 0));
                    part.stroke.addLineSegment({ o + Pt(h + 6, -3.5f), o + Pt(h + 6, 3.5f) }, 0);
                    part.stroke.addLineSegment({ o + Pt(h + 6, 0), o + Pt(Lw, 0) }, 0);
                    two(el, Pt(x, y), Pt(x + Lw, y), el + ".id");
                };

                // The rails: +15 V along the top and down to the lower half; 0 V along the bottom, a line over the
                // resonators and one under them.
                pin(40, 12, "vcc", "+15 V", 40, 7, M); W({ { 40, 12 }, { 580, 12 } }); W({ { 580, 12 }, { 580, 208 } }); W({ { 230, 208 }, { 580, 208 } });
                pin(40, 340, "0", "0 V", 40, 351, M); W({ { 40, 340 }, { 612, 340 } });
                W({ { 168, 24 }, { 168, 196 }, { 168, 340 } }); W({ { 168, 24 }, { 420, 24 } }); W({ { 160, 196 }, { 612, 196 } });

                // The common trigger, as the bass drum's
                {
                    auto& p = a.add("The step's pulse ~ 1 ms, from the sequencer");
                    p.circles.push_back({ Pt(17, 150), 8.5f });
                }
                a.glyph({ { 12, 153 }, { 15, 153 }, { 15, 147 }, { 19, 147 }, { 19, 153 }, { 22, 153 } });
                term(Pt(25.5f, 150), {}, "n6", true); W({ { 25.5f, 150 }, { 28, 150 } });
                R("r117", 28, 150, 'h', 22, "R117 ~ 47 k"); Q("q31", 66, 150, false, "Q31 ~ turns off for the pulse, letting Q32 rise"); W({ { 66, 162 }, { 66, 340 } });
                W({ { 66, 138 }, { 66, 106 } }); R("r120", 66, 84, 'v', 22, "R120 ~ 4.7 k"); pin(66, 84, "nx", "ACCENT, 5 TO 15 V", 71, 80, L);
                W({ { 66, 110 }, { 76, 110 } }); Q("q32", 92, 110, false, "Q32 ~ the follower onto the trigger line"); W({ { 92, 98 }, { 92, 12 } });
                W({ { 92, 122 }, { 92, 130 } }); R("r113", 92, 130, 'v', 22, "R113 ~ 4.7 k, holds the line down"); W({ { 92, 152 }, { 92, 166 }, { 66, 166 } });
                W({ { 92, 130 }, { 110, 130 }, { 110, 112 }, { 150, 112 }, { 150, 118 } });
                // The rim shot's switch
                R("r299", 122, 112, 'v', 18, "R299 ~ 4.7 k"); W({ { 122, 130 }, { 134, 130 } });
                Q("q60", 150, 130, true, "Q60 ~ a PNP switch: it can only push current into the voice");
                R("r298", 122, 130, 'v', 26, "R298 ~ 10 k"); W({ { 122, 156 }, { 122, 160 } }); Q("q59", 122, 172, false, "Q59 ~ on while the rim shot's step data stands"); W({ { 122, 184 }, { 122, 340 } });
                pin(74, 172, "nrs", "RS ON", 72, 182, M); W({ { 74, 172 }, { 76, 172 } }); R("r297", 76, 172, 'h', 22, "R297 ~ 22 k"); W({ { 98, 172 }, { 106, 172 } });
                R("r303", 102, 172, 'v', 18, "R303 ~ 10 k"); W({ { 102, 190 }, { 102, 340 } });
                W({ { 150, 142 }, { 150, 220 }, { 344, 220 } }); W({ { 150, 150 }, { 160, 150 } }); R("r300", 160, 150, 'v', 18, "R300 ~ 22 k"); W({ { 160, 168 }, { 160, 196 } });

                // The first resonator: its pulse, IC21 and its bridged-T (about 455 Hz)
                W({ { 176, 220 }, { 176, 170 } }); C("c111", 176, 170, 'h', 14, "C111 ~ 0.01 uF"); W({ { 190, 170 }, { 204, 170 } });
                R("r307", 196, 170, 'v', 18, "R307 ~ 100 k"); W({ { 196, 188 }, { 196, 196 } });
                DH("d62", 204, 170, 14, "D62 ~ passes the pulse's front only"); R("r301", 218, 170, 'h', 22, "R301 ~ 47 k");
                W({ { 240, 170 }, { 276, 170 }, { 276, 158 }, { 290, 158 } }); R("r302", 256, 170, 'v', 18, "R302 ~ 10 k"); W({ { 256, 188 }, { 256, 196 } });
                a.OP(290, 140, "IC21 ~ the low resonator: a bridged-T at about 455 Hz"); open(290, 146, "ninv21"); open(290, 158, "np21"); term(Pt(316, 152), {}, "no21", true);
                W({ { 290, 146 }, { 282, 146 }, { 282, 104 }, { 272, 104 } }); C("c116", 258, 104, 'h', 14, "C116 ~ 4.7 nF", true);
                R("r315", 258, 24, 'v', 18, "R315 ~ 5.6 k", true); W({ { 258, 42 }, { 258, 104 } });
                C("c115", 258, 72, 'h', 14, "C115 ~ 4.7 nF"); W({ { 272, 72 }, { 330, 72 } }); W({ { 330, 72 }, { 330, 152 }, { 316, 152 } });
                W({ { 282, 124 }, { 296, 124 } }); R("r316", 296, 124, 'h', 22, "R316 ~ 1 M"); W({ { 318, 124 }, { 330, 124 } });

                // The second resonator: its pulse, IC20a and its bridged-T (about 1.8 kHz with C118), IC20b turning it over
                W({ { 344, 220 }, { 344, 176 } }); C("c114", 344, 176, 'h', 14, "C114 ~ 1 nF"); W({ { 358, 176 }, { 372, 176 } });
                R("r306", 364, 176, 'v', 16, "R306 ~ 22 k"); W({ { 364, 192 }, { 364, 196 } });
                DH("d64", 372, 176, 14, "D64"); R("r310", 386, 176, 'h', 22, "R310 ~ 33 k");
                W({ { 408, 176 }, { 426, 176 }, { 426, 158 }, { 440, 158 } }); R("r311", 416, 176, 'v', 16, "R311 ~ 10 k"); W({ { 416, 192 }, { 416, 196 } });
                a.OP(440, 140, "IC20a ~ the high resonator: a bridged-T at about 1.8 kHz for the rim shot"); open(440, 146, "ninv20"); open(440, 158, "np20"); term(Pt(466, 152), {}, "no20a", true);
                W({ { 440, 146 }, { 432, 146 }, { 432, 104 }, { 422, 104 } }); C("c119", 408, 104, 'h', 14, "C119 ~ 2.2 nF", true);
                R("r312", 408, 24, 'v', 18, "R312 ~ 1.07 k", true); W({ { 408, 42 }, { 408, 104 } });
                C("c117", 408, 72, 'h', 14, "C117 ~ 2.2 nF"); C("c118", 408, 88, 'h', 14, "C118 ~ 2.2 nF, across C117 for the rim shot");
                W({ { 422, 72 }, { 480, 72 } }); W({ { 422, 88 }, { 480, 88 } }); W({ { 480, 72 }, { 480, 152 }, { 466, 152 } });
                W({ { 432, 124 }, { 446, 124 } }); R("r308", 446, 124, 'h', 22, "R308 ~ 820 k"); W({ { 468, 124 }, { 480, 124 } });
                R("r313", 408, 56, 'h', 22, "R313 ~ 500 k, for the claves: they ring on"); W({ { 430, 56 }, { 556, 56 }, { 556, 120 } });
                W({ { 480, 146 }, { 490, 146 } }); R("r314", 490, 146, 'h', 22, "R314 ~ 33 k"); W({ { 512, 146 }, { 520, 146 } });
                a.OP(520, 140, "IC20b ~ turns the high resonator over"); open(520, 146, "nin20b"); open(520, 158, "0"); term(Pt(546, 152), {}, "no20b", true);
                W({ { 520, 158 }, { 512, 158 }, { 512, 196 } });
                W({ { 516, 146 }, { 516, 120 } }); R("r309", 516, 120, 'h', 22, "R309 ~ 33 k"); W({ { 538, 120 }, { 556, 120 }, { 556, 152 }, { 546, 152 } });

                // The envelope from the trigger, and the VCA Q62 that lets the two resonators through while it lasts
                DI("d61", 200, 220, 20, "D61"); W({ { 200, 240 }, { 206, 240 } }); R("r107", 206, 240, 'h', 22, "R107 ~ 1 k");
                W({ { 228, 240 }, { 250, 240 } }); C("c24", 236, 240, 'v', 14, "C24 ~ 0.47 uF: the envelope"); W({ { 236, 254 }, { 236, 340 } });
                R("r108", 250, 240, 'h', 22, "R108 ~ 10 k"); DH("d63", 272, 240, 14, "D63"); W({ { 286, 240 }, { 336, 240 } }); W({ { 320, 240 }, { 320, 244 } });
                Q("q62", 320, 256, false, "Q62 ~ the swing VCA: the resonators through, shaped by the envelope"); W({ { 320, 268 }, { 320, 340 } });
                W({ { 330, 152 }, { 330, 276 } }); R("r318", 330, 276, 'v', 22, "R318 ~ 220 k");
                W({ { 556, 152 }, { 556, 166 } }); W({ { 556, 160 }, { 566, 160 }, { 566, 298 }, { 542, 298 } });
                R("r320", 520, 298, 'h', 22, "R320 ~ 22 k", true); W({ { 520, 298 }, { 280, 298 } });
                R("r317", 290, 298, 'v', 18, "R317 ~ 10 k"); W({ { 290, 316 }, { 290, 340 } });
                C("c120", 280, 270, 'v', 14, "C120 ~ 10 uF", true); W({ { 280, 284 }, { 280, 298 } }); W({ { 280, 256 }, { 280, 270 } }); W({ { 280, 256 }, { 304, 256 } });
                R("r321", 292, 208, 'v', 22, "R321 ~ 2.2 M"); W({ { 292, 230 }, { 292, 256 } });

                // The high-pass after the VCA: Q63, a follower in a two-pole filter
                C("c121", 336, 240, 'h', 14, "C121 ~ 4.7 nF"); W({ { 350, 240 }, { 364, 240 } });
                R("r322", 358, 240, 'v', 18, "R322 ~ 10 k"); W({ { 358, 258 }, { 358, 270 }, { 408, 270 } });
                C("c122", 364, 240, 'h', 14, "C122 ~ 4.7 nF"); W({ { 378, 240 }, { 392, 240 } });
                R("r325", 384, 240, 'v', 18, "R325 ~ 330 k"); W({ { 384, 258 }, { 384, 340 } });
                Q("q63", 408, 240, false, "Q63 ~ the follower of the high-pass"); W({ { 408, 228 }, { 408, 208 } }); W({ { 408, 252 }, { 408, 270 } });
                R("r323", 408, 270, 'v', 18, "R323 ~ 22 k"); pin(408, 288, "vee", "-15 V", 414, 292, L);

                // Into the mixer: the rim shot through R324, the claves (switched out) through R319
                W({ { 408, 262 }, { 424, 262 } }); R("r324", 424, 262, 'h', 22, "R324 ~ 130 k, for the rim shot"); W({ { 446, 262 }, { 470, 262 } });
                R("r319", 556, 166, 'v', 22, "R319 ~ 220 k, for the claves"); W({ { 556, 188 }, { 556, 250 }, { 458, 250 }, { 458, 262 } });
                C("c125", 470, 262, 'h', 14, "C125 ~ 0.01 uF"); R("r326", 484, 262, 'h', 22, "R326 ~ 10 k"); W({ { 506, 262 }, { 600, 262 } });

                // The mixer, IC19, and the Level
                W({ { 600, 84 }, { 600, 262 } });
                a.OP(620, 140, "IC19 ~ the output amplifier"); open(620, 146, "nsum"); open(620, 158, "0"); term(Pt(646, 152), {}, "vout", true);
                W({ { 600, 146 }, { 620, 146 } }); W({ { 620, 158 }, { 612, 158 }, { 612, 196 } });
                C("c153", 600, 84, 'h', 14, "C153 ~ 22 pF"); W({ { 614, 84 }, { 660, 84 } });
                R("r330", 600, 100, 'h', 22, "R330 ~ 470 k"); W({ { 622, 100 }, { 660, 100 } });
                POT("rvr16", 600, 116, 'h', 22, "VR16 LEVEL ~ 100 k"); C("c126", 622, 116, 'h', 14, "C126 ~ 33 uF"); W({ { 636, 116 }, { 660, 116 } });
                W({ { 646, 152 }, { 660, 152 }, { 660, 84 } }); W({ { 660, 152 }, { 680, 152 } });
                {
                    auto& out = a.add({});
                    out.fill.addTriangle({ 688, 152 }, { 680, 148.5f }, { 680, 155.5f });
                }
                a.tx(680, 146, "OUT", Rj);
            }

            /*
                The hand clap (D278): every part of the engine's tools/tr808/clap.cir on the clap's side of SW12 (the
                maracas' side carries nothing in a clap and is left off this sheet). Along the top: the trigger, the
                clap's envelopes and the C.P. OFFSET, the band-pass IC21 on the white noise, the reverb's VCA Q70, the
                BA662 (IC22) and the mixer IC19. Along the bottom: IC23's four comparators (each a switch to -15 V)
                that make the sawtooth, Q73 emptying it, and the exponential converter Q71 and Q72 that turns it into
                the BA662's control current: the claps.
            */
            void buildClap(Board::Sheet& a, auto& W, auto& term, auto& R, auto& C, auto& POT, auto& DI, auto& Q, auto& pin,
                           auto& open, auto& wiper, auto& two)
            {
                width = 1060.0f;
                height = 420.0f;
                static_cast<void>(DI);
                const auto L = juce::Justification::centredLeft;
                const auto Rj = juce::Justification::centredRight;
                const auto M = juce::Justification::centred;
                const auto DH = [&](const std::string& el, float x, float y, float Lw, const juce::String& label)
                {
                    auto& part = a.add(label);
                    const float h = (Lw - 6.0f) / 2.0f;
                    const Pt o(x, y);
                    part.stroke.addLineSegment({ o, o + Pt(h, 0) }, 0);
                    part.stroke.addTriangle(o + Pt(h, -3.5f), o + Pt(h, 3.5f), o + Pt(h + 6, 0));
                    part.stroke.addLineSegment({ o + Pt(h + 6, -3.5f), o + Pt(h + 6, 3.5f) }, 0);
                    part.stroke.addLineSegment({ o + Pt(h + 6, 0), o + Pt(Lw, 0) }, 0);
                    two(el, Pt(x, y), Pt(x + Lw, y), el + ".id");
                };
                const auto DU = [&](const std::string& el, float x, float y, float Lh, const juce::String& label)
                {
                    auto& part = a.add(label);
                    const float h = (Lh - 6.0f) / 2.0f;
                    const Pt o(x, y);
                    part.stroke.addLineSegment({ o, o + Pt(0, h) }, 0);
                    part.stroke.addLineSegment({ o + Pt(-3.5f, h), o + Pt(3.5f, h) }, 0);
                    part.stroke.addTriangle(o + Pt(0, h), o + Pt(-3.5f, h + 6), o + Pt(3.5f, h + 6));
                    part.stroke.addLineSegment({ o + Pt(0, h + 6), o + Pt(0, Lh) }, 0);
                    two(el, Pt(x, y + Lh), Pt(x, y), el + ".id");
                };
                // A comparator of IC23: on, it ties its output to -15 V while its - input is above its + input.
                const auto CMP = [&](float x, float y, const std::string& minus, const std::string& plus, const std::string& out, const juce::String& label)
                {
                    a.OP(x, y, label);
                    open(x, y + 6, minus);
                    open(x, y + 18, plus);
                    term(Pt(x + 26, y + 12), {}, out, true);
                };

                // The rails: +15 V along the top and down to the lower half, 0 V and -15 V along the bottom, a ground
                // line over the envelopes and one between the halves.
                pin(40, 12, "vcc", "+15 V", 40, 7, M); W({ { 40, 12 }, { 1040, 12 } }); W({ { 1040, 12 }, { 1040, 208 } }); W({ { 190, 208 }, { 1040, 208 } });
                pin(40, 392, "0", "0 V", 34, 392, Rj); W({ { 40, 392 }, { 1000, 392 } });
                pin(40, 408, "vee", "-15 V", 34, 408, Rj); W({ { 40, 408 }, { 1000, 408 } });
                W({ { 168, 24 }, { 168, 196 }, { 168, 392 } }); W({ { 168, 24 }, { 460, 24 } }); W({ { 160, 196 }, { 1000, 196 } });

                // The common trigger, as the bass drum's
                {
                    auto& p = a.add("The step's pulse ~ 1 ms, from the sequencer");
                    p.circles.push_back({ Pt(17, 150), 8.5f });
                }
                a.glyph({ { 12, 153 }, { 15, 153 }, { 15, 147 }, { 19, 147 }, { 19, 153 }, { 22, 153 } });
                term(Pt(25.5f, 150), {}, "n6", true); W({ { 25.5f, 150 }, { 28, 150 } });
                R("r117", 28, 150, 'h', 22, "R117 ~ 47 k"); Q("q31", 66, 150, false, "Q31 ~ turns off for the pulse, letting Q32 rise"); W({ { 66, 162 }, { 66, 392 } });
                W({ { 66, 138 }, { 66, 106 } }); R("r120", 66, 84, 'v', 22, "R120 ~ 4.7 k"); pin(66, 84, "nx", "ACCENT, 5 TO 15 V", 71, 80, L);
                W({ { 66, 110 }, { 76, 110 } }); Q("q32", 92, 110, false, "Q32 ~ the follower onto the trigger line"); W({ { 92, 98 }, { 92, 12 } });
                W({ { 92, 122 }, { 92, 130 } }); R("r113", 92, 130, 'v', 22, "R113 ~ 4.7 k, holds the line down"); W({ { 92, 152 }, { 92, 166 }, { 66, 166 } });
                W({ { 92, 130 }, { 110, 130 }, { 110, 112 }, { 150, 112 }, { 150, 118 } });
                // The clap's switch, and its trigger out (Q30)
                R("r105", 122, 112, 'v', 18, "R105 ~ 4.7 k"); W({ { 122, 130 }, { 134, 130 } });
                Q("q29", 150, 130, true, "Q29 ~ a PNP switch: it can only push current into the clap");
                R("r106", 122, 130, 'v', 26, "R106 ~ 10 k"); W({ { 122, 156 }, { 122, 160 } }); Q("q28", 122, 172, false, "Q28 ~ on while the clap's step data stands"); W({ { 122, 184 }, { 122, 392 } });
                pin(74, 172, "ncpd", "CP ON", 72, 182, M); W({ { 74, 172 }, { 76, 172 } }); R("r103", 76, 172, 'h', 22, "R103 ~ 22 k"); W({ { 98, 172 }, { 106, 172 } });
                R("r104", 102, 172, 'v', 18, "R104 ~ 10 k"); W({ { 102, 190 }, { 102, 392 } });
                W({ { 150, 142 }, { 176, 142 } }); W({ { 176, 40 }, { 176, 300 } });
                R("r109", 176, 300, 'h', 22, "R109 ~ 33 k"); W({ { 198, 300 }, { 214, 300 } }); R("r110", 206, 300, 'v', 18, "R110 ~ 10 k"); W({ { 206, 318 }, { 206, 392 } });
                Q("q30", 230, 300, false, "Q30 ~ the clap's trigger out, to the CP TRIG jack"); W({ { 230, 312 }, { 230, 392 } });
                W({ { 230, 208 }, { 230, 256 } }); R("r111", 230, 256, 'v', 22, "R111 ~ 33 k"); W({ { 230, 278 }, { 230, 288 } });

                // The clap's envelopes: Q69 follows the trigger, charging the reverb's (C139, C138) and the accent's (C143)
                R("rswc", 176, 60, 'h', 22, "SW12 ~ on the clap's side: closed"); W({ { 198, 60 }, { 220, 60 } });
                Q("q69", 236, 60, false, "Q69 ~ follows the trigger"); W({ { 236, 48 }, { 236, 12 } }); W({ { 236, 72 }, { 236, 116 } });
                R("r360", 210, 60, 'v', 22, "R360 ~ 27 k"); W({ { 210, 82 }, { 210, 228 }, { 330, 228 }, { 330, 276 }, { 340, 276 } });
                R("r361", 244, 228, 'v', 22, "R361 ~ 22 k"); W({ { 244, 250 }, { 244, 408 } });
                DH("d69", 236, 84, 14, "D69"); W({ { 250, 84 }, { 280, 84 } }); C("c139", 264, 70, 'v', 14, "C139 ~ 0.039 uF: the reverb's envelope", true); W({ { 264, 70 }, { 264, 24 } });
                R("r348", 280, 84, 'h', 22, "R348 ~ 1 M"); W({ { 302, 84 }, { 320, 84 } }); C("c138", 312, 70, 'v', 14, "C138 ~ 0.047 uF", true); W({ { 312, 70 }, { 312, 24 } });
                R("r349", 320, 84, 'h', 22, "R349 ~ 1 M, to Q70"); W({ { 342, 84 }, { 350, 84 }, { 350, 40 }, { 540, 40 }, { 540, 142 } });
                DH("d68", 236, 116, 14, "D68"); W({ { 250, 116 }, { 272, 116 } }); C("c143", 260, 116, 'v', 14, "C143 ~ 1 uF: the accent"); W({ { 260, 130 }, { 260, 196 } });
                R("r362", 272, 116, 'h', 22, "R362 ~ 330 k"); W({ { 294, 116 }, { 346, 116 } }); R("r364", 306, 116, 'v', 18, "R364 ~ 2.2 k"); W({ { 306, 134 }, { 306, 196 } });
                a.R(320, 136, false, 40, "TM3 C.P. OFFSET ~ 10 k trimmer: where the claps start");
                two("rtma", Pt(320, 136), Pt(320, 156)); two("rtmb", Pt(320, 156), Pt(320, 176)); wiper(320, 156);
                W({ { 320, 136 }, { 306, 136 } }); W({ { 320, 176 }, { 320, 186 }, { 280, 186 }, { 280, 408 } });
                R("r370", 336, 116, 'v', 22, "R370 ~ 47 k", true); W({ { 336, 138 }, { 336, 156 }, { 320, 156 } });

                // The band-pass on the white noise: IC21
                pin(360, 100, "nwn", "WHITE NOISE, 130 mV", 356, 104, Rj); W({ { 360, 100 }, { 364, 100 } });
                R("r332", 364, 100, 'h', 22, "R332 ~ 27 k"); W({ { 386, 100 }, { 392, 100 } }); W({ { 364, 100 }, { 364, 88 } });
                C("c127", 364, 88, 'h', 14, "C127 ~ 820 pF"); W({ { 378, 88 }, { 392, 88 }, { 392, 100 } });
                R("r333", 392, 100, 'v', 18, "R333 ~ 10 k"); W({ { 392, 118 }, { 392, 196 } });
                W({ { 392, 100 }, { 404, 100 } }); C("c128", 404, 100, 'h', 14, "C128 ~ 5.2 nF"); W({ { 418, 100 }, { 418, 136 }, { 430, 136 } });
                a.OP(430, 130, "IC21 ~ a band-pass: the noise's clap colour"); open(430, 136, "nm21"); open(430, 148, "0"); term(Pt(456, 142), {}, "no21", true);
                W({ { 430, 148 }, { 424, 148 }, { 424, 196 } });
                W({ { 392, 88 }, { 392, 64 } }); C("c129", 392, 64, 'h', 14, "C129 ~ 5.2 nF"); W({ { 406, 64 }, { 470, 64 }, { 470, 142 }, { 456, 142 } });
                W({ { 418, 118 }, { 440, 118 } }); R("r334", 440, 118, 'h', 22, "R334 ~ 100 k", true); W({ { 462, 118 }, { 470, 118 } });

                // The reverb's VCA: Q70
                W({ { 470, 142 }, { 484, 142 } }); R("r342", 484, 142, 'h', 22, "R342 ~ 15 k"); W({ { 506, 142 }, { 520, 142 } });
                R("r346", 514, 142, 'v', 18, "R346 ~ 680"); W({ { 514, 160 }, { 514, 196 } });
                C("c137", 520, 142, 'h', 14, "C137 ~ 0.033 uF"); W({ { 534, 142 }, { 550, 142 } });
                Q("q70", 566, 142, false, "Q70 ~ the reverb's VCA: the noise, fading with C139");
                R("r363", 566, 100, 'v', 22, "R363 ~ 10 k"); W({ { 566, 100 }, { 566, 12 } }); W({ { 566, 122 }, { 566, 130 } });
                W({ { 566, 126 }, { 590, 126 } }); C("c146", 590, 126, 'v', 14, "C146 ~ 0.018 uF"); W({ { 590, 140 }, { 590, 196 } });
                W({ { 566, 154 }, { 566, 160 }, { 582, 160 } }); R("r369", 566, 160, 'v', 18, "R369 ~ 330"); W({ { 566, 178 }, { 566, 196 } });
                R("r200", 582, 160, 'v', 18, "R200's place ~ 10 k to -15 V, the change"); W({ { 582, 178 }, { 582, 186 }, { 684, 186 }, { 684, 408 } });
                W({ { 590, 126 }, { 604, 126 } }); C("c152", 604, 126, 'h', 14, "C152 ~ 4.7 nF"); R("r376", 618, 126, 'h', 22, "R376 ~ 47 k"); W({ { 640, 126 }, { 940, 126 } });

                // IC22, the BA662: the noise in at pins 2 and 3, the control current at pin 1, the claps out
                {
                    auto& block = a.add("IC22 ~ BA662: an amplifier whose gain follows the current into pin 1");
                    block.stroke.addRectangle(700, 40, 60, 70);
                }
                a.tx(730, 76, "IC22", M);
                W({ { 470, 64 }, { 470, 50 }, { 650, 50 } }); C("c136", 650, 50, 'h', 14, "C136 ~ 0.022 uF"); R("r372", 664, 50, 'h', 22, "R372 ~ 39 k");
                W({ { 686, 50 }, { 700, 50 } }); open(700, 50, "np2"); R("r382", 692, 50, 'v', 18, "R382 ~ 1 k"); W({ { 692, 68 }, { 692, 196 } });
                open(700, 70, "np3"); W({ { 700, 70 }, { 698, 70 } }); R("r381", 698, 70, 'v', 18, "R381 ~ 1 k"); W({ { 698, 88 }, { 698, 196 } });
                term(Pt(760, 60), {}, "np6", true); W({ { 760, 60 }, { 790, 60 } }); R("r383", 790, 60, 'v', 18, "R383 ~ 82 k"); W({ { 790, 78 }, { 790, 196 } });
                term(Pt(760, 90), {}, "np8", true); W({ { 760, 90 }, { 770, 90 }, { 770, 150 } });
                C("c150", 770, 150, 'h', 14, "C150 ~ 1 nF"); R("r378", 784, 150, 'h', 22, "R378 ~ 100 k"); W({ { 806, 150 }, { 940, 150 } });
                open(730, 110, "np1"); W({ { 730, 110 }, { 730, 120 }, { 800, 120 } }); R("r374", 800, 120, 'h', 22, "R374 ~ 10 k", true);
                W({ { 822, 120 }, { 910, 120 }, { 910, 320 }, { 846, 320 }, { 846, 308 } });

                // IC23: the ladder, and four comparators that make the sawtooth
                W({ { 300, 196 }, { 300, 240 } }); R("r353", 300, 240, 'v', 16, "R353 ~ 4.7 k"); R("r352", 300, 256, 'v', 16, "R352 ~ 4.7 k");
                R("r351", 300, 272, 'v', 18, "R351 ~ 4.7 k"); W({ { 300, 290 }, { 300, 408 } });
                CMP(340, 270, "n8", "n9r", "n14", "IC23a ~ holds C140 at -15 V while the trigger stands");
                W({ { 300, 272 }, { 316, 272 }, { 316, 288 }, { 340, 288 } });
                W({ { 300, 256 }, { 310, 256 }, { 310, 244 }, { 412, 244 }, { 412, 276 }, { 420, 276 } });
                W({ { 366, 282 }, { 406, 282 }, { 406, 288 }, { 420, 288 } });
                R("r350", 380, 264, 'v', 18, "R350 ~ 1 M", true); W({ { 380, 264 }, { 380, 196 } });
                C("c140", 394, 268, 'v', 14, "C140 ~ 0.027 uF: about 30 ms of claps", true); W({ { 394, 268 }, { 394, 196 } });
                CMP(420, 270, "n6r", "n14", "n1", "IC23b ~ holds pin 1 low while C140 is under -5 V");
                W({ { 446, 282 }, { 490, 282 }, { 490, 276 }, { 500, 276 } });
                W({ { 412, 244 }, { 470, 244 }, { 470, 300 }, { 494, 300 }, { 494, 288 }, { 500, 288 } });
                CMP(500, 270, "n1", "n6r", "n13", "IC23c ~ lets C144 charge while pin 1 is low");
                W({ { 526, 282 }, { 560, 282 } }); R("r365", 540, 264, 'v', 18, "R365 ~ 82 k"); W({ { 540, 264 }, { 540, 196 } });
                DH("d71", 560, 282, 14, "D71"); W({ { 574, 282 }, { 620, 282 } });
                C("c144", 586, 282, 'v', 14, "C144 ~ 0.39 uF: the sawtooth"); W({ { 586, 296 }, { 586, 392 } });
                R("r356", 600, 282, 'v', 22, "R356 ~ 1 M"); W({ { 600, 304 }, { 600, 334 }, { 640, 334 } });
                R("r355", 456, 282, 'v', 22, "R355 ~ 2.7 k"); W({ { 456, 304 }, { 456, 314 }, { 632, 314 }, { 632, 322 }, { 640, 322 } });
                R("r354", 484, 314, 'v', 18, "R354 ~ 5.6 k"); W({ { 484, 332 }, { 484, 392 } });
                CMP(640, 316, "n4", "n5", "n2", "IC23d ~ fires Q73 at the top of each tooth");
                W({ { 600, 334 }, { 600, 350 }, { 610, 350 } }); C("c142", 610, 350, 'h', 14, "C142 ~ 220 pF"); W({ { 624, 350 }, { 676, 350 }, { 676, 328 }, { 666, 328 } });
                R("r359", 676, 350, 'v', 18, "R359 ~ 10 k"); W({ { 676, 368 }, { 676, 392 } });
                W({ { 676, 328 }, { 700, 328 } }); R("r358", 700, 328, 'h', 22, "R358 ~ 68 k"); W({ { 722, 328 }, { 744, 328 } });
                Q("q73", 760, 328, false, "Q73 ~ empties C144: the end of each clap"); W({ { 760, 340 }, { 760, 408 } });
                W({ { 480, 282 }, { 480, 256 }, { 700, 256 } }); C("c141", 700, 256, 'h', 14, "C141 ~ 1 nF"); W({ { 714, 256 }, { 744, 256 } });
                DU("d70", 730, 256, 20, "D70"); W({ { 730, 276 }, { 730, 408 } });
                R("r357", 744, 256, 'v', 22, "R357 ~ 68 k"); W({ { 744, 278 }, { 744, 328 } });
                W({ { 620, 282 }, { 620, 296 }, { 628, 296 } }); R("r367", 628, 296, 'h', 22, "R367 ~ 330"); W({ { 650, 296 }, { 760, 296 }, { 760, 316 } });

                // The exponential converter: Q72 from the sawtooth, Q71 from the offset and the accent
                W({ { 760, 296 }, { 780, 296 } }); R("r368", 780, 296, 'h', 22, "R368 ~ 470 k"); W({ { 802, 296 }, { 830, 296 } });
                R("r371", 816, 296, 'v', 18, "R371 ~ 15 k"); W({ { 816, 314 }, { 816, 392 } });
                Q("q72", 846, 296, true, "Q72 ~ the sawtooth, exponentially, into IC22's pin 1");
                W({ { 846, 284 }, { 846, 258 }, { 880, 258 } }); R("r366", 856, 208, 'v', 22, "R366 ~ 15 k"); W({ { 856, 230 }, { 856, 258 } });
                Q("q71", 880, 270, true, "Q71 ~ sets where the claps start: the offset and the accent"); W({ { 880, 282 }, { 880, 408 } });
                W({ { 346, 116 }, { 346, 236 }, { 864, 236 }, { 864, 270 } });

                // The mixer, IC19, and the Level
                W({ { 940, 84 }, { 940, 150 } });
                a.OP(960, 130, "IC19 ~ the mixer: the claps and the reverb summed"); open(960, 136, "nsum"); open(960, 148, "0"); term(Pt(986, 142), {}, "vout", true);
                W({ { 940, 136 }, { 960, 136 } }); W({ { 960, 148 }, { 952, 148 }, { 952, 196 } });
                C("c149", 940, 84, 'h', 14, "C149 ~ 220 pF"); W({ { 954, 84 }, { 1000, 84 } });
                R("r375", 940, 100, 'h', 22, "R375 ~ 470 k"); W({ { 962, 100 }, { 1000, 100 } });
                POT("rvr17", 940, 116, 'h', 22, "VR17 LEVEL ~ 100 k"); C("c145", 962, 116, 'h', 14, "C145 ~ 33 uF"); W({ { 976, 116 }, { 1000, 116 } });
                W({ { 986, 142 }, { 1000, 142 }, { 1000, 84 } }); W({ { 1000, 142 }, { 1016, 142 } });
                {
                    auto& out = a.add({});
                    out.fill.addTriangle({ 1024, 142 }, { 1016, 138.5f }, { 1016, 145.5f });
                }
                a.tx(1016, 136, "OUT", Rj);
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
        std::array<std::unique_ptr<FullDrum>, drumsDrawn> drums; // each made the first time it is shown
        FullDrum* full = nullptr;                                // the one shown
        bool showFull = false;

        FullDrum& drum(int which)
        {
            auto& d = drums[static_cast<std::size_t>(which)];
            if (! d)
                d = std::make_unique<FullDrum>(which);
            return *d;
        }
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
        i.sheet.options = { "Whole board", "Bass drum, complete", "Snare, complete", "Low tom, complete", "Rim shot, complete", "Hand clap, complete" };
        i.sheet.fontHeight = 10.0f;
        i.sheet.get = [this] { return impl->showFull && impl->full ? 1 + impl->full->which : 0; };
        i.sheet.set = [this](int v)
        {
            impl->showFull = v >= 1 && v <= drumsDrawn;
            impl->selected = -1;
            impl->full = impl->showFull ? &impl->drum(v - 1) : nullptr;
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
        juce::StringArray all;
        for (int d = 0; d < drumsDrawn; ++d)
            for (const auto& e : impl->drum(d).errors)
                all.add(juce::String(d == handClap ? "the hand clap: " : d == rimShot ? "the rim shot: " : d == lowTom ? "the low tom: " : d == snareDrum ? "the snare: " : "the bass drum: ") + e);
        return all;
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
        if (impl->showFull && impl->full && (lanes & (1u << laneOfDrum[impl->full->which])) != 0)
        {
            impl->full->pos = 0.0; // its drum struck: the hit plays from the start
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
                if (names && part.label.isNotEmpty())
                {
                    // Each part's name beside it, as the mockup writes them (worked out in prepareLabels).
                    const bool tall = part.labelTall;
                    const auto& label = part.label;
                    const auto p = part.labelAt.transformedBy(t);
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
        const float w = full ? impl->full->width : boardW, h = full ? impl->full->height : boardH;
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
            const float length = w.a.getDistanceFrom(w.b);
            const float r = (0.8f + 2.4f * gl) * 0.5f * scale;
            float s = std::fmod(-w.offset, 6.5f);
            if (s < 0.0f)
                s += 6.5f;
            for (; s < length; s += 6.5f)
            {
                const auto p = (w.a + (w.b - w.a) * (length > 0.0f ? s / length : 0.0f)).transformedBy(t);
                g.fillEllipse(p.x - r, p.y - r, 2 * r, 2 * r);
            }
        }

        // The time and what the circuit does then, and where the numbers come from.
        const double ms = f.msAt(f.pos);
        const juce::String phase = f.phaseAt(ms);
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
                g.strokePath(f.path, juce::PathStrokeType(2.8f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
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
