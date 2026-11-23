#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

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

    inline juce::Font uiFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
    }
}
