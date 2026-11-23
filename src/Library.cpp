#include "Library.h"

#include "LibraryData.h"

namespace cs808
{
    std::vector<LibraryEntry> libraryFromJson(const juce::String& json, juce::StringArray* problems)
    {
        std::vector<LibraryEntry> entries;
        const auto root = juce::JSON::parse(json);
        const auto* list = root.getArray();
        if (list == nullptr)
        {
            if (problems != nullptr)
                problems->add("the library is not a JSON array");
            return entries;
        }
        for (const auto& item : *list)
        {
            LibraryEntry e;
            if (! patternFileFromLibraryVar(item, e.pattern))
            {
                if (problems != nullptr)
                    problems->add("cannot read " + item.getProperty("id", "?").toString());
                continue;
            }
            e.id = item.getProperty("id", "").toString();
            e.group = styleHeading(e.pattern.style);
            e.year = static_cast<int>(item.getProperty("year", 0));
            e.confidence = item.getProperty("confidence", "").toString();
            entries.push_back(std::move(e));
        }
        return entries;
    }

    std::vector<LibraryEntry> builtInLibrary()
    {
        return libraryFromJson(juce::String::fromUTF8(LibraryData::patterns_json, LibraryData::patterns_jsonSize));
    }

    std::vector<LibraryEntry> userLibrary()
    {
        std::vector<LibraryEntry> entries;
        const auto folder = userPatternsFolder();
        for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.cs808pattern"))
        {
            LibraryEntry e;
            if (! patternFileFromVar(juce::JSON::parse(f.loadFileAsString()), e.pattern))
                continue;
            e.id = f.getFileNameWithoutExtension();
            e.group = "My patterns";
            e.builtIn = false;
            e.file = f;
            entries.push_back(std::move(e));
        }
        std::sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b)
                  { return a.pattern.name.compareNatural(b.pattern.name) < 0; });
        return entries;
    }

    juce::File saveUserPattern(const PatternFile& pattern)
    {
        const auto folder = userPatternsFolder();
        if (! folder.createDirectory())
            return {};
        auto name = juce::File::createLegalFileName(pattern.name.trim());
        if (name.isEmpty())
            name = "Pattern";
        const auto file = folder.getNonexistentChildFile(name, ".cs808pattern", false);
        return file.replaceWithText(juce::JSON::toString(patternFileToVar(pattern), false)) ? file : juce::File();
    }

    juce::File renameUserPattern(const juce::File& file, const juce::String& newName)
    {
        PatternFile pattern;
        const auto name = newName.trim();
        if (name.isEmpty() || ! patternFileFromVar(juce::JSON::parse(file.loadFileAsString()), pattern))
            return {};
        pattern.name = name;
        const auto text = juce::JSON::toString(patternFileToVar(pattern), false);
        auto legal = juce::File::createLegalFileName(name);
        if (legal.isEmpty())
            legal = "Pattern";
        // The same file name (or only its case changed, which Windows sees as the same file): the file keeps its place.
        if (legal.equalsIgnoreCase(file.getFileNameWithoutExtension()))
            return file.replaceWithText(text) ? file : juce::File();
        const auto target = file.getParentDirectory().getNonexistentChildFile(legal, ".cs808pattern", false);
        if (! target.replaceWithText(text))
            return {};
        file.deleteFile();
        return target;
    }

    const juce::StringArray& libraryFamilies()
    {
        static const juce::StringArray families { "Electro and hip hop", "R&B and pop", "Bass and the South", "Trap and drill",
                                                  "House and acid", "Techno", "Breaks and big beat", "Jungle and IDM" };
        return families;
    }

    juce::String familyOf(const juce::String& style)
    {
        static const std::vector<std::pair<juce::String, int>> table {
            { "electro", 0 }, { "hip-hop", 0 }, { "rnb", 1 }, { "pop", 1 }, { "miami-bass", 2 }, { "trap", 3 },
            { "chicago-house", 4 }, { "acid-house", 4 }, { "detroit-techno", 5 }, { "breakbeat", 6 }, { "big-beat", 6 },
            { "other", 6 }, { "jungle", 7 }, { "idm", 7 } };
        for (const auto& [tag, family] : table)
            if (tag == style)
                return libraryFamilies()[family];
        return libraryFamilies()[6]; // anything else sits with the breaks
    }

    juce::String libraryKey(const LibraryEntry& e) { return (e.builtIn ? juce::String() : juce::String("mine:")) + e.id; }

    juce::String styleHeading(const juce::String& style)
    {
        if (style.isEmpty())
            return "Other";
        if (style == "idm")
            return "IDM";
        if (style == "rnb")
            return "R&B";
        auto s = style.replaceCharacter('-', ' ');
        return s.substring(0, 1).toUpperCase() + s.substring(1);
    }
}
