#pragma once

#include <vector>

#include "Project.h"

/*
    The pattern library: the classic beats built into the app (resources/library/patterns.json, compiled in) and
    the patterns you keep yourself (%APPDATA%\CoolSynth808\Patterns, one .cs808pattern file each). Quick for now:
    the way it is shown and managed is to be designed with the user (D266).
*/
namespace cs808
{
    struct LibraryEntry
    {
        PatternFile pattern;
        juce::String id;
        juce::String group; // the style, or "My patterns"
        int year = 0;
        juce::String confidence;
        bool builtIn = true;
        juce::File file; // a user pattern's
    };

    /** The built-in beats, in the order of the file. */
    [[nodiscard]] std::vector<LibraryEntry> builtInLibrary();
    /** The built-in beats read from a JSON text (the compiled-in file, or a test's). */
    [[nodiscard]] std::vector<LibraryEntry> libraryFromJson(const juce::String& json, juce::StringArray* problems = nullptr);
    /** Your own patterns, by name. */
    [[nodiscard]] std::vector<LibraryEntry> userLibrary();
    /** Saves a pattern of yours under its name; returns the file, or an invalid one. */
    juce::File saveUserPattern(const PatternFile& pattern);

    /** A style tag as a heading: "acid-house" becomes "Acid house". */
    [[nodiscard]] juce::String styleHeading(const juce::String& style);
}
