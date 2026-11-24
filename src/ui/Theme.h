#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

#include "LibraryData.h"

/*
    The panel's colours as named roles (the mockup's, docs/design/mockups/coolsynth-808.html in CoolSynth's
    repository): every theme is only this list. Own colours, not Roland's (D263): the four beat groups of step keys
    are shades of one hue.
*/
namespace cs808
{
    struct Theme
    {
        const char* id;
        const char* name;
        juce::Colour desk, panel, ink, dim, line, strip, on, onInk, lit, flow, vpos, vneg;
        std::array<juce::Colour, 4> group;
        bool grid = false;
    };

    inline const std::array<Theme, 4>& themes()
    {
        static const std::array<Theme, 4> list { {
            { "studio", "Studio 1982",
              juce::Colour(0xff171615), juce::Colour(0xff2d2b28), juce::Colour(0xffece4d2), juce::Colour(0xff9a927f),
              juce::Colour(0xff4a463f), juce::Colour(0xffe8742a), juce::Colour(0xffe8742a), juce::Colour(0xff1a1512),
              juce::Colour(0xffffc36b), juce::Colour(0xffffa04a), juce::Colour(0xff8fd694), juce::Colour(0xffe8685a),
              { juce::Colour(0xff4b463e), juce::Colour(0xff5b554b), juce::Colour(0xff6c6558), juce::Colour(0xff7e7666) }, false },
            { "paper", "Paper",
              juce::Colour(0xffd8d1bf), juce::Colour(0xfff3ecdb), juce::Colour(0xff1d1c1a), juce::Colour(0xff7b7566),
              juce::Colour(0xffc9c0aa), juce::Colour(0xffc8372d), juce::Colour(0xff1d1c1a), juce::Colour(0xfff3ecdb),
              juce::Colour(0xffc8372d), juce::Colour(0xffc8372d), juce::Colour(0xff2f8f4e), juce::Colour(0xff2b5fb3),
              { juce::Colour(0xffe6dec9), juce::Colour(0xffdbd2bb), juce::Colour(0xffcfc5ac), juce::Colour(0xffc3b89d) }, false },
            { "night", "Night",
              juce::Colour(0xff000000), juce::Colour(0xff101012), juce::Colour(0xffc9c5bb), juce::Colour(0xff6d6a63),
              juce::Colour(0xff2a2a2f), juce::Colour(0xffffb000), juce::Colour(0xffffb000), juce::Colour(0xff141000),
              juce::Colour(0xffffd266), juce::Colour(0xffffb000), juce::Colour(0xff7bd88f), juce::Colour(0xffff6b5e),
              { juce::Colour(0xff1b1b1f), juce::Colour(0xff232329), juce::Colour(0xff2c2c33), juce::Colour(0xff35353e) }, false },
            { "blueprint", "Blueprint",
              juce::Colour(0xff0b2444), juce::Colour(0xff133d70), juce::Colour(0xffeaf3ff), juce::Colour(0xff8fb1dc),
              juce::Colour(0xff3d6aa6), juce::Colour(0xffeaf3ff), juce::Colour(0xffeaf3ff), juce::Colour(0xff133d70),
              juce::Colour(0xff9fe3ff), juce::Colour(0xff9fe3ff), juce::Colour(0xffb6f5a0), juce::Colour(0xffffb3a0),
              { juce::Colour(0xff1b4b87), juce::Colour(0xff235799), juce::Colour(0xff2b63ab), juce::Colour(0xff346fbd) }, true },
        } };
        return list;
    }

    /** The theme in use, shared by every component of the panel (message thread). */
    inline const Theme*& currentThemePointer()
    {
        static const Theme* current = &themes()[0];
        return current;
    }
    inline const Theme& theme() { return *currentThemePointer(); }
    inline void setTheme(int index) { currentThemePointer() = &themes()[static_cast<std::size_t>(juce::jlimit(0, 3, index))]; }

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

    inline juce::Font fontOf(Face face, float pointHeight, float tracking = 0.0f)
    {
        return juce::Font(juce::FontOptions().withTypeface(typefaceOf(face)).withPointHeight(pointHeight).withKerningFactor(tracking));
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
