#pragma once

#include <array>
#include <map>
#include <tuple>

#include <juce_graphics/juce_graphics.h>

#include "LibraryData.h"

/*
    The panel's colours as named roles (the mockup's, docs/design/mockups/coolsynth-808.html in CoolSynth's
    repository): every theme is only this list. Own colours, not Roland's (D263): the four beat groups of step keys
    are shades of one hue.
*/
namespace cs808
{
    /** A step key that is on, per beat group (D307): its cap's three stops, and lit from inside while it sounds. */
    struct StepOn
    {
        juce::Colour top, mid, bottom, hotCentre, hotMid, glow;
    };

    /** The board above the glass and what is mounted on it (D307): the metal, its printing, the copper and its light,
        the knobs, the switches and the keys. */
    struct BoardLook
    {
        juce::Colour metalTop, metalBottom;
        float sheen, brushLight, brushDark;
        juce::Colour ink, label, dim, accent, hoverInk;       // printed on the board
        juce::Colour copper, pad, silk, silkText;             // the etched circuit unlit
        juce::Colour flowWire, flowDot, heat;                 // its light: lit copper, the current's dots, the afterglow
        juce::Colour knobTop, knobBottom, pointer, tick;      // the knobs on the board
        juce::Colour plateTop, plateMid, plateBottom, thumbTop, thumbMid, thumbBottom; // the slide switches
        std::array<juce::Colour, 3> cap, capHover, capSelected, slotHas, slotEmpty;    // the dark keys
        // The theme's signal colour on keys and the Accent knob: orange, or Olive's red.
        std::array<juce::Colour, 3> hot { juce::Colour(0xfff5a066), juce::Colour(0xffe8742a), juce::Colour(0xffc3581b) };
        std::array<juce::Colour, 3> hotHover { juce::Colour(0xfff8aa72), juce::Colour(0xffec7c33), juce::Colour(0xffc95e20) };
        std::array<juce::Colour, 4> backlit { juce::Colour(0xffffc58a), juce::Colour(0xfff08a3c), juce::Colour(0xffc95b1d), juce::Colour(0xffff8228) }; // centre, middle, edge, glow
        std::array<juce::Colour, 2> accentKnob { juce::Colour(0xfff3a066), juce::Colour(0xffb9541a) };
        // The name plate (D330, after CoolSynth 303's title): a dark shade of the metal, its edge darker still.
        juce::Colour nameStrip { 0xff1c1c1d }, nameEdge { 0xff0d0d0e };
    };

    struct Theme
    {
        const char* id;
        const char* name;
        juce::Colour desk, panel, ink, dim, line, strip, on, onInk, lit, flow, vpos, vneg;
        std::array<juce::Colour, 4> group;
        bool grid = false;
        BoardLook board;
        std::array<StepOn, 4> stepOn;
    };

    namespace detail
    {
        inline juce::Colour c(std::uint32_t rgb, float a = 1.0f) { return juce::Colour(0xff000000u | rgb).withAlpha(a); }

        /** A step colour as the mockups' 5.1 draws it: lighter above, darker below, lit near white from inside. */
        inline StepOn stepOnFrom(juce::Colour on)
        {
            return { on.interpolatedWith(juce::Colours::white, 0.28f), on, on.interpolatedWith(juce::Colours::black, 0.25f),
                     on.interpolatedWith(juce::Colours::white, 0.85f), on.interpolatedWith(juce::Colours::white, 0.35f), on };
        }

        /** The keys' signal colour from the mockups' three stops (hot-a, hot-b, hot-c) and the Accent knob's two. */
        inline void signal(BoardLook& b, juce::Colour top, juce::Colour mid, juce::Colour bottom, juce::Colour knobTop, juce::Colour knobBottom)
        {
            b.hot = { top, mid, bottom };
            b.hotHover = { top.brighter(0.08f), mid.brighter(0.08f), bottom.brighter(0.08f) };
            b.backlit = { mid.interpolatedWith(juce::Colours::white, 0.45f), mid.brighter(0.1f), bottom, mid };
            b.accentKnob = { knobTop, knobBottom };
        }

        /** The dark keys from the mockups' cap-a, cap-b and cap-c: a hovered key a little lighter, a toggle that is on
            lighter still; a slot that holds a pattern as the cap's upper half, an empty one as its lower. */
        inline void keys(BoardLook& b, juce::Colour a, juce::Colour m, juce::Colour z)
        {
            b.cap = { a, m, z };
            b.capHover = { a.brighter(0.08f), m.brighter(0.08f), z.brighter(0.08f) };
            b.capSelected = { a.brighter(0.15f), m.brighter(0.15f), z.brighter(0.15f) };
            b.slotHas = { a, a.interpolatedWith(m, 0.5f), m };
            b.slotEmpty = { m, m.interpolatedWith(z, 0.5f), z };
        }
    }

    inline const std::array<Theme, 4>& themes()
    {
        using detail::c;
        static const std::array<Theme, 4> list = []
        {
            // The user's choice (D303): the mockups' 5.4, brushed aluminium on top; under the glass cool greys and the
            // same orange. Its current runs yellow (D307, the user).
            Theme silver { "silver", "Silver",
                c(0x2b2d30), c(0x2f3236), c(0xeef0f2), c(0xa3a9b0), c(0x4a4e54), c(0xef6b21), c(0xef6b21), c(0x1a1714),
                c(0xffa04a), c(0xff8c3a), c(0x8fd694), c(0xe8685a),
                { c(0x464a50), c(0x51555b), c(0x5c6066), c(0x686c72) }, false, {}, {} };
            silver.board = { c(0xc6c9cc), c(0xa9adb1), 0.18f, 0.05f, 0.025f,
                c(0x1d2024), c(0x2a2e33), c(0x6c7178), c(0xd9581a), c(0x000000),
                c(0x966834, 0.48f), c(0x8c6436, 0.7f), c(0x1e2024, 0.42f), c(0x1e2024, 0.78f),
                c(0xf2a810), c(0xffc81e), c(0xf09a14),
                c(0xf6f6f6), c(0x9da2a8), c(0x1d2024), c(0x2a2e33),
                c(0xe4e6e8), c(0xc2c5c8), c(0xa2a6aa), c(0x3a3d42), c(0x23262a), c(0x131517),
                { c(0x4c4842), c(0x37342f), c(0x2c2a26) }, { c(0x55514a), c(0x3c3934), c(0x2f2d29) },
                { c(0x55504a), c(0x3d3a35), c(0x322f2b) }, { c(0x4a4640), c(0x3f3b36), c(0x34312d) },
                { c(0x3b3833), c(0x343129), c(0x2c2a26) } };
            const StepOn orange { c(0xf5a066), c(0xe8742a), c(0xc3581b), c(0xfff0dc), c(0xffb067), c(0xffa046) };
            silver.stepOn = { orange, orange, orange, orange };
            silver.board.nameStrip = c(0x26292d);
            silver.board.nameEdge = c(0x121416);

            // The 808's own colours (D307, the mockups' 5.1): a charcoal panel printed in white and orange, dark
            // knobs with an orange line, the steps in red, orange, yellow and white, dim when off, full when on.
            Theme charcoal { "charcoal", "Charcoal",
                c(0x161617), c(0x242425), c(0xece7d6), c(0x8d8c88), c(0x3c3c3e), c(0xf28a2c), c(0xee7d28), c(0x1c120a),
                c(0xf6a25a), c(0xf07828), c(0x8fd694), c(0xe8685a),
                { c(0x6d2c27), c(0x79492a), c(0x7a6a35), c(0x77746c) }, false, {}, {} };
            charcoal.board = { c(0x333334), c(0x242425), 0.06f, 0.012f, 0.02f,
                c(0xf4f4f2), c(0xecebe6), c(0x8d8c88), c(0xf28a2c), c(0xffffff),
                c(0xb29268, 0.30f), c(0xb29268, 0.5f), c(0xecece8, 0.26f), c(0xe8e8e4, 0.62f),
                c(0xf07828), c(0xffaf64), c(0xe86e28),
                c(0x6a6a6c), c(0x242426), c(0xf28a2c), c(0xdcdcd8),
                c(0x555557), c(0x38383a), c(0x262628), c(0xf2f2ef), c(0xc9c9c4), c(0x8f8f8a),
                { c(0x505052), c(0x39393b), c(0x2c2c2e) }, { c(0x575759), c(0x3e3e40), c(0x303032) },
                { c(0x5c5c5e), c(0x424244), c(0x333335) }, { c(0x505052), c(0x454547), c(0x39393b) },
                { c(0x39393b), c(0x323234), c(0x2c2c2e) } };
            charcoal.stepOn = { detail::stepOnFrom(c(0xd8362c)), detail::stepOnFrom(c(0xee7d28)),
                              detail::stepOnFrom(c(0xf1c63c)), detail::stepOnFrom(c(0xece7d6)) };
            detail::signal(charcoal.board, c(0xf6a25a), c(0xee7d28), c(0xc45a16), c(0xf6a25a), c(0xc45a16));

            // The mockups' 5.2 Steel (D307): blue-grey anodised aluminium, gold traces, the same orange.
            Theme steel { "steel", "Steel",
                c(0x1e2328), c(0x2a3038), c(0xf0f4f7), c(0xa3aeb8), c(0x46505a), c(0xe8742a), c(0xe8742a), c(0x1a120c),
                c(0xffa04a), c(0xff8c3a), c(0x8fd694), c(0xe8685a),
                { c(0x3c4550), c(0x454e59), c(0x4f5965), c(0x5a6572) }, false, {}, {} };
            steel.board = { c(0x5c6670), c(0x4a535d), 0.07f, 0.012f, 0.02f,
                c(0xf5f8fa), c(0xe8eef3), c(0xa3aeb8), c(0xffa04a), c(0xffffff),
                c(0xd6b676, 0.42f), c(0xd6b676, 0.6f), c(0xecf4fa, 0.30f), c(0xecf4fa, 0.72f),
                c(0xeb6e23), c(0xffaa5a), c(0xe8742a),
                c(0x78828c), c(0x2c333a), c(0xf3f7fa), c(0xdfe7ee),
                c(0x6a747e), c(0x4c5660), c(0x3a434c), c(0xf5f8fa), c(0xc9d2da), c(0x8d98a3),
                {}, {}, {}, {}, {} };
            detail::keys(steel.board, c(0x626c77), c(0x4b545e), c(0x3e4650));
            detail::signal(steel.board, c(0xf5a066), c(0xe8742a), c(0xc3581b), c(0xf3a066), c(0xb9541a));
            steel.stepOn = silver.stepOn;
            steel.board.nameStrip = c(0x2a3037);
            steel.board.nameEdge = c(0x15191d);

            // The mockups' 5.3 Olive (D307): olive drab, brass traces, signal red.
            Theme olive { "olive", "Olive",
                c(0x22231a), c(0x2e3024), c(0xf2efe2), c(0xaaa78f), c(0x50523e), c(0xe2512b), c(0xe2512b), c(0x230b04),
                c(0xff6a43), c(0xff6e46), c(0x8fd694), c(0xe8685a),
                { c(0x45473a), c(0x4e5042), c(0x585a4b), c(0x636555) }, false, {}, {} };
            olive.board = { c(0x646650), c(0x50523e), 0.06f, 0.012f, 0.02f,
                c(0xf7f4e8), c(0xeeead6), c(0xaaa78f), c(0xff6a43), c(0xffffff),
                c(0xdeba7a, 0.40f), c(0xdeba7a, 0.6f), c(0xf4f0de, 0.30f), c(0xf4f0de, 0.72f),
                c(0xff6e46), c(0xffaf87), c(0xe2512b),
                c(0x76785f), c(0x2b2c21), c(0xf7f4e8), c(0xe6e2cd),
                c(0x6c6e57), c(0x505240), c(0x3c3e2f), c(0xf7f4e8), c(0xd2cdb6), c(0x969179),
                {}, {}, {}, {}, {} };
            detail::keys(olive.board, c(0x6a6c55), c(0x535540), c(0x464835));
            detail::signal(olive.board, c(0xf68866), c(0xe2512b), c(0xb33a1a), c(0xf68866), c(0xb33a1a));
            const auto red = detail::stepOnFrom(c(0xe2512b));
            olive.stepOn = { red, red, red, red };
            olive.board.nameStrip = c(0x2a2b20);
            olive.board.nameEdge = c(0x141510);

            return std::array<Theme, 4> { charcoal, silver, steel, olive }; // Charcoal first: the default (D307, the user)
        }();
        return list;
    }

    /** The theme in use, shared by every component of the panel (message thread). */
    inline const Theme*& currentThemePointer()
    {
        static const Theme* current = &themes()[0];
        return current;
    }
    inline const Theme& theme() { return *currentThemePointer(); }
    inline void setTheme(int index) { currentThemePointer() = &themes()[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(themes().size()) - 1, index))]; }

    /** What is printed on the board (D303, D307): dark on the aluminium, light on the charcoal. */
    inline const BoardLook& boardLook() { return theme().board; }

    // The panel's type (D292): one family, Archivo, condensed for labels as an instrument's printing is, and IBM Plex
    // Mono for numbers, so digits keep their width. Embedded, so it looks the same on every computer. Sizes are point
    // sizes, as the mockup's CSS pixels.
    enum class Face { label, head, headBold, logo, logoBold, mono, monoBold };

    inline juce::Typeface::Ptr typefaceOf(Face face)
    {
        static const std::array<juce::Typeface::Ptr, 7> faces = []
        {
            const auto make = [](const char* data, int size) { return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size)); };
            return std::array<juce::Typeface::Ptr, 7> {
                make(LibraryData::ArchivoCondensedSemiBold_ttf, LibraryData::ArchivoCondensedSemiBold_ttfSize),
                make(LibraryData::ArchivoSemiCondensedSemiBold_ttf, LibraryData::ArchivoSemiCondensedSemiBold_ttfSize),
                make(LibraryData::ArchivoSemiCondensedBold_ttf, LibraryData::ArchivoSemiCondensedBold_ttfSize),
                make(LibraryData::ArchivoMedium_ttf, LibraryData::ArchivoMedium_ttfSize),
                make(LibraryData::ArchivoExtraBold_ttf, LibraryData::ArchivoExtraBold_ttfSize),
                make(LibraryData::IBMPlexMonoMedium_ttf, LibraryData::IBMPlexMonoMedium_ttfSize),
                make(LibraryData::IBMPlexMonoSemiBold_ttf, LibraryData::IBMPlexMonoSemiBold_ttfSize) };
        }();
        return faces[static_cast<std::size_t>(face)];
    }

    /** The fonts made, deleted with JUCE's shutdown (a plain static outlived JUCE's fonts and hung the quit). */
    struct MadeFonts final : public juce::DeletedAtShutdown
    {
        std::map<std::tuple<int, int, int>, juce::Font> map;
        ~MadeFonts() override { clearSingletonInstance(); }
        JUCE_DECLARE_SINGLETON_SINGLETHREADED_INLINE(MadeFonts, false)
    };

    inline juce::Font fontOf(Face face, float pointHeight, float tracking = 0.0f)
    {
        // Made once per face, size and tracking and copied after: making one took 22 us, many times a paint (D306).
        auto& made = MadeFonts::getInstance()->map;
        const auto key = std::make_tuple(static_cast<int>(face), juce::roundToInt(pointHeight * 100.0f), juce::roundToInt(tracking * 10000.0f));
        if (const auto found = made.find(key); found != made.end())
            return found->second;
        return made.emplace(key, juce::Font(juce::FontOptions().withTypeface(typefaceOf(face)).withPointHeight(pointHeight).withKerningFactor(tracking))).first->second;
    }

    /** Small capitals printed on the panel: condensed, spaced. */
    inline juce::Font labelFont(float pointHeight = 10.0f) { return fontOf(Face::label, pointHeight, 0.09f); }
    /** A drum's name or a heading. */
    inline juce::Font headFont(float pointHeight = 13.0f) { return fontOf(Face::headBold, pointHeight, 0.05f); }
    /** Numbers: tempo, values, pattern numbers. */
    inline juce::Font monoFont(float pointHeight = 13.0f, bool strong = false) { return fontOf(strong ? Face::monoBold : Face::mono, pointHeight); }
    /** The name plate: CoolSynth in medium, 808 in extra bold. */
    inline juce::Font logoFont(float pointHeight, bool strong) { return fontOf(strong ? Face::logoBold : Face::logo, pointHeight, 0.01f); }

    /** Everything written before D292 asked for this; it now takes Archivo too. */
    inline juce::Font uiFont(float height, bool bold = false)
    {
        return fontOf(bold ? Face::headBold : Face::head, height, 0.02f);
    }
}
