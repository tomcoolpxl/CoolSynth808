#pragma once

#include <vector>

#include "Project.h"

/*
    The pattern library: the classic beats built into the app (resources/library/patterns.json, compiled in) and
    the patterns you keep yourself (%APPDATA%\CoolSynth808\Patterns, one .cs808pattern file each). Shown on a shelf
    (ui/LibraryShelf, CoolSynth's D273); the built-in beats are read-only, yours can be renamed or recycled.
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

    /** Gives a pattern of yours a new name, in the file and as its file's name; returns the file, or an invalid one. */
    juce::File renameUserPattern(const juce::File& file, const juce::String& newName);

    /** A style tag as a heading: "acid-house" becomes "Acid house". */
    [[nodiscard]] juce::String styleHeading(const juce::String& style);

    /** The shelf's families of styles, in their order (CoolSynth's D273), and the family a style belongs to. */
    [[nodiscard]] const juce::StringArray& libraryFamilies();
    [[nodiscard]] juce::String familyOf(const juce::String& style);
    /** An entry's key, unique across the built-in beats and yours. */
    [[nodiscard]] juce::String libraryKey(const LibraryEntry& e);
}
