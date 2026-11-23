#include "LidView.h"

#include <array>
#include <cmath>
#include <vector>

#include "../Kit.h"
#include "../Processor.h"
#include "Theme.h"

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

            Board()
            {
                using Res = Voice::Res;
                using Gate = Voice::Gate;
                using Feed = Voice::Feed;

                voice(1, 0, [](Voice& v)
                {
                    v.fd = 5;
                    v.sw("Q40 \xc2\xb7 lets the trigger in");
                    Res r; r.inName = "C40 \xc2\xb7 the pulse shaper, with R162, R163 and D53"; r.op = "The resonator's op-amp"; r.c1 = "C41"; r.c2 = "C42"; r.r = "R167"; r.fd = 5;
                    r.gnd = "R165, R166 \xc2\xb7 to ground. Q43 shorts R165 for the first 5 ms: the higher pitch of the attack";
                    r.fb = "VR6 \xc2\xb7 Decay: the feedback through R164, C43 and a second op-amp"; r.fbPot = true; r.fbLabel = "DECAY";
                    v.res(40, r);
                    v.pot(150, "VR5 \xc2\xb7 Tone, with R171 and R172", "TONE");
                    v.s.dot(94, 176); v.s.C(80, 176, true, 14, "C45"); v.s.G(80, 176);
                    v.pot(200, "VR4 \xc2\xb7 Level, then the buffer Q44", "LEVEL");
                });
                voice(2, 1, [](Voice& v)
                {
                    v.fd = 11;
                    v.sw("Q46 \xc2\xb7 lets the trigger in");
                    Gate gt; gt.tapX = 78; gt.tapY = NW; gt.feed = Feed::pot; gt.feedName = "VR9 \xc2\xb7 Snappy: how far the trigger opens the noise"; gt.feedLabel = "SNAPPY";
                    gt.cap = "C51 \xc2\xb7 the snappy envelope"; gt.r = "R184"; gt.q = "Q48 \xc2\xb7 the swing VCA on the white noise"; gt.sig = Kind::nz;
                    v.gate(34, gt);
                    Res a; a.inR = true; a.inName = "R189"; a.op = "IC14a \xc2\xb7 the lower shell, 173 Hz"; a.c1 = "C58"; a.c2 = "C59"; a.r = "R197"; a.gnd = "R196"; a.fd = 9;
                    v.res(96, a);
                    Res b2; b2.inR = true; b2.inName = "R191"; b2.op = "IC14b \xc2\xb7 the upper shell, 334 Hz"; b2.c1 = "C60"; b2.c2 = "C61"; b2.r = "R198"; b2.gnd = "R195"; b2.fd = 13;
                    v.res(178, b2);
                    v.pot(268, "VR8 \xc2\xb7 Tone: the balance of the two shells", "TONE");
                    v.pot(298, "VR7 \xc2\xb7 Level", "LEVEL");
                });
                const std::array<std::tuple<int, int, const char*, float>, 3> toms { { { 3, 2, "Q51", 6.0f }, { 4, 3, "Q54", 8.0f }, { 5, 4, "Q57", 10.0f } } };
                for (const auto& [col, lane, q, fd] : toms)
                {
                    const juce::String qName(q);
                    const float f = fd;
                    voice(col, lane, [qName, f](Voice& v)
                    {
                        v.fd = f;
                        v.sw(qName + " \xc2\xb7 lets the trigger in");
                        Gate gt; gt.tapX = 78; gt.tapY = NP; gt.feedName = "D55 \xc2\xb7 charges the noise envelope"; gt.cap = "C72 \xc2\xb7 the noise envelope";
                        gt.r = "R218"; gt.q = "Q52 \xc2\xb7 the gate on the pink noise. The conga leaves it out"; gt.sig = Kind::nz;
                        v.gate(34, gt);
                        Res r; r.inName = "C73 \xc2\xb7 passes the pulse"; r.op = "The resonator's op-amp"; r.c1 = "C76"; r.c2 = "C78"; r.r = "R228"; r.fd = f;
                        r.gnd = "TUNING \xc2\xb7 with the germanium pair D80, D81: the pitch drops as it rings"; r.gndPot = true; r.gndLabel = "TUNING";
                        r.fb = "R230 \xc2\xb7 fitted to both real units' decay";
                        v.res(110, r);
                        v.pot(292, "LEVEL \xc2\xb7 in the mixer", "LEVEL");
                    });
                }
                voice(6, 5, [](Voice& v)
                {
                    v.fd = 15;
                    v.sw("Q60 \xc2\xb7 lets the trigger in");
                    Res a; a.inR = true; a.inName = "R301"; a.op = "IC21 \xc2\xb7 455 Hz"; a.c1 = "C116"; a.c2 = "C115"; a.r = "R316"; a.gnd = "R315"; a.fd = 14; a.noJoin = true;
                    v.res(40, a);
                    Res b2; b2.inR = true; b2.inName = "R310"; b2.op = "IC20 \xc2\xb7 1.7 kHz for the rim shot, 2.5 kHz for the claves"; b2.c1 = "C119"; b2.c2 = "C117"; b2.r = "R308"; b2.gnd = "R312"; b2.fd = 18; b2.noJoin = true;
                    v.res(122, b2);
                    v.s.w({ { 94, 104 }, { 94, 200 }, { 84, 200 }, { 84, 238 }, { 76, 238 } });
                    v.s.dot(94, 186);
                    v.s.f(1, Kind::ac, { { 94, 104 }, { 94, 200 }, { 84, 200 }, { 84, 238 }, { 76, 238 } }, 0.4f, 15);
                    Gate gt; gt.feedName = "D61 \xc2\xb7 charges the envelope"; gt.cap = "C24 \xc2\xb7 the envelope"; gt.r = "R108"; gt.q = "Q62 \xc2\xb7 the swing VCA: chops the two rings together"; gt.sig = Kind::ac; gt.fd = 15;
                    v.gate(212, gt);
                    v.pot(292, "LEVEL \xc2\xb7 in the mixer", "LEVEL");
                });
                voice(7, 6, [](Voice& v)
                {
                    v.sw("Q29 \xc2\xb7 lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = NW; a.feed = Feed::box; a.feedName = "IC23 \xc2\xb7 four switches that time the four claps"; a.feedLabel = "x4";
                    a.r = "R374"; a.q = "IC22 \xc2\xb7 the BA662 amplifier the claps open"; a.sig = Kind::nz; a.burst = true;
                    v.gate(34, a);
                    Gate b2; b2.tapX = 78; b2.tapY = Y0 + 60; b2.feedName = "D69 \xc2\xb7 charges the reverb envelope"; b2.cap = "C139 \xc2\xb7 the reverb envelope"; b2.r = "R348";
                    b2.q = "Q70 \xc2\xb7 the reverb: the noise under C139's slow envelope"; b2.sig = Kind::nz;
                    v.gate(110, b2);
                    v.pot(292, "LEVEL \xc2\xb7 in the mixer", "LEVEL");
                });
                voice(8, 7, [](Voice& v)
                {
                    v.fd = 12;
                    v.sw("Q5 \xc2\xb7 lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = 40; a.feedName = "D2 \xc2\xb7 charges the envelope"; a.cap = "C9 \xc2\xb7 the envelope. R82 and C34 take part of it at once: the abrupt fall";
                    a.r = "R28"; a.q = "Q15 \xc2\xb7 lets the 554 Hz oscillator through"; a.sig = Kind::ac; a.fd = 11;
                    v.gate(34, a);
                    Gate b2; b2.tapX = 86; b2.tapY = 34; b2.feed = Feed::wire; b2.r = "R29"; b2.q = "Q14 \xc2\xb7 lets the 825 Hz oscillator through"; b2.sig = Kind::ac; b2.fd = 16;
                    v.gate(100, b2);
                    v.hp(176, "C28, C29 \xc2\xb7 IC2b, the band-pass at 870 Hz", "R24, R25");
                    v.pot(292, "VR5 \xc2\xb7 Level", "LEVEL");
                });
                voice(9, 8, [](Voice& v)
                {
                    v.sw("Q6 \xc2\xb7 lets the trigger in. Q19 then charges three envelopes at once");
                    Gate a; a.tapX = 78; a.tapY = HI; a.feedName = "D6"; a.cap = "C38 \xc2\xb7 the top band's envelope"; a.r = "R94"; a.q = "Q16 \xc2\xb7 the swing VCA on the high band, then IC5a's high-pass";
                    v.gate(34, a);
                    Gate b2; b2.tapX = 78; b2.tapY = Y0 + 60; b2.feedName = "D7"; b2.cap = "C40 \xc2\xb7 the middle band's envelope"; b2.r = "R90"; b2.q = "Q17 \xc2\xb7 the swing VCA on the high band again, then IC5b's high-pass";
                    v.gate(100, b2);
                    Gate c; c.tapX = 86; c.tapY = LO; c.feedName = "D8"; c.cap = "C41 \xc2\xb7 the low band's envelope"; c.decayName = "VR2 \xc2\xb7 Decay: drains C41"; c.decayLabel = "DECAY";
                    c.r = "R104"; c.q = "Q18 \xc2\xb7 the swing VCA on the low band, then Q25's high-pass";
                    v.gate(166, c);
                    v.pot(240, "VR4 \xc2\xb7 Tone: the top band against the others", "TONE");
                    v.pot(292, "VR6 \xc2\xb7 Level", "LEVEL");
                });
                voice(10, 9, [](Voice& v)
                {
                    v.sw("Q7 \xc2\xb7 lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = HI; a.feedName = "Q24 \xc2\xb7 charges the envelope"; a.cap = "C62 \xc2\xb7 the envelope";
                    a.decayName = "VR3 \xc2\xb7 Decay: sets when Q22 drains C62"; a.decayLabel = "DECAY"; a.decayAt = { 44, 74 }; a.decayJust = juce::Justification::centredLeft;
                    a.r = "R144"; a.q = "Q27 \xc2\xb7 the swing VCA on the high band";
                    v.gate(34, a);
                    v.s.dot(32, 48);
                    v.s.w({ { 32, 48 }, { 32, 80 } });
                    v.s.Q(32, 92, -1, false, "Q23 \xc2\xb7 drains the open hat the moment the closed hat plays");
                    v.s.G(32, 104);
                    v.s.w({ { 48, 92 }, { 60, 92 } });
                    v.s.R(60, 92, true, 22, "R173 \xc2\xb7 from the closed hat's envelope");
                    v.s.w({ { 82, 92 }, { 100, 92 } });
                    v.hp(116, "The high-pass around Q26, at 8.2 kHz", "Its resistor to ground");
                    v.pot(292, "VR7 \xc2\xb7 Level", "LEVEL");
                });
                voice(11, 10, [](Voice& v)
                {
                    v.sw("Q8 \xc2\xb7 lets the trigger in");
                    Gate a; a.tapX = 78; a.tapY = HI; a.feedName = "Q28 \xc2\xb7 charges the envelope"; a.cap = "C63 \xc2\xb7 the envelope"; a.r = "R142"; a.q = "Q30 \xc2\xb7 the swing VCA on the high band";
                    v.gate(34, a);
                    v.s.dot(31, 48);
                    v.s.w({ { 31, 48 }, { 31, 92 }, { 0, 92 } });
                    v.s.f(5, Kind::env, { { 31, 48 }, { 31, 92 }, { -52, 92 } });
                    v.hp(116, "The high-pass around Q31, at 12.2 kHz", "Its resistor to ground");
                    v.pot(292, "VR8 \xc2\xb7 Level", "LEVEL");
                });

                // What every drum shares: the trigger, the noise, the six oscillators and the mix.
                const Sheet a { *this, 0, 0, -1 };
                {
                    auto& pulse = a.add("The step's pulse \xc2\xb7 from the sequencer, one for every drum that plays on this step");
                    pulse.circles.push_back({ Pt(20, 112), 8.5f });
                }
                a.glyph({ { 15, 115 }, { 18, 115 }, { 18, 109 }, { 22, 109 }, { 22, 115 }, { 25, 115 } });
                a.w({ { 20, 103.5f }, { 20, 92 } });
                a.POT(20, 70, false, 22, "VR3 \xc2\xb7 Accent: the height of the trigger, up to 11.5 V");
                a.tx(29, 84, "ACCENT", juce::Justification::centredLeft, true);
                a.w({ { 20, 70 }, { 20, 40 }, { 34, 40 } });
                a.Q(50, 40, 1, false, "Q32 \xc2\xb7 a follower that puts the pulse on the trigger line");
                a.w({ { 50, 28 }, { 50, 22 } });
                a.tx(50, 19, "+15 V");
                a.v(0, Mode::dc, { { 50, 28 }, { 50, 22 } }).key = -2;
                flows.back().idle = 0.8f;
                a.v(0, Mode::pulse, { { 50, 52 }, { 64, 52 }, { 64, TRIG } });
                a.w({ { 50, 52 }, { 64, 52 }, { 64, TRIG }, { 1150, TRIG } });
                a.tx(70, 7, "TRIGGER LINE", juce::Justification::centredLeft);
                a.f(0, Kind::pulse, { { 20, 103 }, { 20, 40 }, { 34, 40 } });
                a.f(0, Kind::pulse, { { 50, 52 }, { 64, 52 }, { 64, TRIG } });

                a.BOX(208, 24, 14, 12, "The noise source \xc2\xb7 one hiss for the whole machine");
                a.glyph({ { 210, 31 }, { 212, 27 }, { 214, 33 }, { 216, 28 }, { 218, 32 }, { 220, 29 } });
                a.tx(226, 32, "NOISE", juce::Justification::centredLeft);
                a.w({ { 215, 36 }, { 215, NW }, { 778, NW } });
                a.dot(296, NW);
                a.w({ { 296, NW }, { 296, NP } });
                a.R(296, NP, true, 22, "The pink filter \xc2\xb7 the toms take the noise through it");
                a.w({ { 318, NP }, { 578, NP } });
                a.tx(322, 62, "PINK", juce::Justification::centredLeft);
                { auto& f = a.f(0, Kind::nz, { { 215, 36 }, { 215, NW }, { 778, NW } }); f.key = -2; f.idle = 0.06f; }
                { auto& f = a.f(0, Kind::nz, { { 296, NW }, { 296, NP }, { 578, NP } }); f.key = -2; f.idle = 0.05f; }

                const std::array<std::pair<float, const char*>, 6> oscillators { {
                    { 862.0f, "554 Hz \xc2\xb7 the cowbell's, set by TM1" }, { 886.0f, "825 Hz \xc2\xb7 the cowbell's, set by TM2" }, { 915.0f, "205 Hz" },
                    { 932.0f, "304 Hz" }, { 968.0f, "370 Hz" }, { 985.0f, "523 Hz" } } };
                for (std::size_t i = 0; i < oscillators.size(); ++i)
                {
                    const float x = oscillators[i].first;
                    a.BOX(x - 7, 22, 14, 12, juce::String::fromUTF8(oscillators[i].second) + " \xc2\xb7 a Schmitt-trigger oscillator that never stops");
                    a.glyph({ { x - 5, 31 }, { x - 5, 26 }, { x, 26 }, { x, 31 }, { x + 5, 31 }, { x + 5, 26 } });
                    if (i == 0)
                        a.w({ { 862, 34 }, { 862, 40 }, { 878, 40 }, { 878, SUM } });
                    else
                        a.w({ { x, 34 }, { x, SUM } });
                    a.dot(i == 0 ? 878.0f : x, SUM);
                }
                a.w({ { 878, SUM }, { 1069, SUM } });
                a.tx(1060, 31, "SIX OSCILLATORS", juce::Justification::centredLeft);
                a.BOX(1020, 62, 26, 11, "IC3 \xc2\xb7 a band-pass at 7.09 kHz: the high band", "7.1k");
                a.dot(1033, SUM); a.w({ { 1033, SUM }, { 1033, 62 } }); a.w({ { 1033, 73 }, { 1033, HI } }); a.dot(1033, HI);
                a.BOX(1056, 62, 26, 11, "IC3 \xc2\xb7 a band-pass at 3.43 kHz: the low band", "3.4k");
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
    }

    struct LidView::Impl
    {
        Board board;
        double now = 0.0, last = 0.0;
        int selected = -1;
        juce::Random random { 1980 };
    };

    LidView::LidView(Processor& p) : impl(std::make_unique<Impl>()), processor(p) {}
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

    void LidView::paint(juce::Graphics& g)
    {
        const auto& th = theme();
        const float scale = std::min(getWidth() / boardW, (getHeight() - 26.0f) / boardH);
        const juce::AffineTransform t = juce::AffineTransform::scale(scale).translated((getWidth() - boardW * scale) / 2.0f, 0.0f);
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
        for (std::size_t i = 0; i < b.parts.size(); ++i)
        {
            const auto& part = b.parts[i];
            const bool sel = static_cast<int>(i) == impl->selected;
            const auto colour = sel ? th.flow : th.ink;
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
        }
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

        // What the selected part is.
        auto caption = getLocalBounds().removeFromBottom(24).toFloat();
        if (impl->selected >= 0)
        {
            const auto& part = b.parts[static_cast<std::size_t>(impl->selected)];
            juce::String drum = part.lane >= 0 ? juce::String(instruments[static_cast<std::size_t>(part.lane)].name) + ": " : juce::String();
            g.setColour(th.ink);
            g.setFont(uiFont(12.0f, true));
            g.drawText(drum + part.name.upToFirstOccurrenceOf(" \xc2\xb7 ", false, false), caption.removeFromLeft(caption.getWidth() * 0.35f), juce::Justification::centredLeft);
            g.setColour(th.dim);
            g.setFont(uiFont(11.5f));
            g.drawText(part.name.fromFirstOccurrenceOf(" \xc2\xb7 ", false, false), caption, juce::Justification::centredLeft);
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
        const float scale = std::min(getWidth() / boardW, (getHeight() - 26.0f) / boardH);
        const auto at = e.position.transformedBy(juce::AffineTransform::scale(scale).translated((getWidth() - boardW * scale) / 2.0f, 0.0f).inverted());
        int found = -1;
        float best = 1.0e9f;
        const auto& parts = impl->board.parts;
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
