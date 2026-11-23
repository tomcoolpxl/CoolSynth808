#include "Project.h"

namespace cs808
{
    namespace
    {
        constexpr int formatVersion = 1;
        constexpr int steps = SequencerLane::steps;

        juce::String stepsToString(const std::array<coolsynth::synth::SequencerStep, steps>& a)
        {
            juce::String s;
            for (const auto& step : a)
                s << (! step.on ? "." : step.hits >= 3 ? "3" : step.hits == 2 ? "2" : "x");
            return s;
        }

        void stepsFromString(const juce::String& s, std::array<coolsynth::synth::SequencerStep, steps>& a)
        {
            for (int i = 0; i < steps; ++i)
            {
                const auto c = i < s.length() ? s[i] : '.';
                auto& step = a[static_cast<std::size_t>(i)];
                step.on = c == 'x' || c == 'X' || c == '2' || c == '3';
                step.hits = static_cast<std::uint8_t>(c == '3' ? 3 : c == '2' ? 2 : 1);
            }
        }

        juce::var chancesToVar(const std::array<coolsynth::synth::SequencerStep, steps>& a)
        {
            bool any = false;
            juce::Array<juce::var> list;
            for (const auto& step : a)
            {
                any = any || step.chance < 1.0f;
                list.add(static_cast<double>(step.chance));
            }
            return any ? juce::var(list) : juce::var();
        }

        void chancesFromVar(const juce::var& v, std::array<coolsynth::synth::SequencerStep, steps>& a)
        {
            if (const auto* list = v.getArray())
                for (int i = 0; i < steps && i < list->size(); ++i)
                    a[static_cast<std::size_t>(i)].chance = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>((*list)[i])));
        }

        juce::var laneToVar(const SequencerLane& lane)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("a", stepsToString(lane.a));
            o->setProperty("b", stepsToString(lane.b));
            if (auto c = chancesToVar(lane.a); ! c.isVoid())
                o->setProperty("chanceA", c);
            if (auto c = chancesToVar(lane.b); ! c.isVoid())
                o->setProperty("chanceB", c);
            if (lane.length != steps)
                o->setProperty("length", lane.length);
            if (lane.mute)
                o->setProperty("mute", true);
            if (lane.solo)
                o->setProperty("solo", true);
            return juce::var(o);
        }

        void laneFromVar(const juce::var& v, SequencerLane& lane)
        {
            stepsFromString(v.getProperty("a", "").toString(), lane.a);
            stepsFromString(v.getProperty("b", "").toString(), lane.b);
            chancesFromVar(v.getProperty("chanceA", {}), lane.a);
            chancesFromVar(v.getProperty("chanceB", {}), lane.b);
            lane.length = juce::jlimit(1, steps, static_cast<int>(v.getProperty("length", steps)));
            lane.mute = static_cast<bool>(v.getProperty("mute", false));
            lane.solo = static_cast<bool>(v.getProperty("solo", false));
        }

        juce::String variationName(SequencerVariation v)
        {
            return v == SequencerVariation::b ? "B" : v == SequencerVariation::ab ? "AB" : "A";
        }

        SequencerVariation variationOf(const juce::String& s)
        {
            return s == "B" ? SequencerVariation::b : s == "AB" ? SequencerVariation::ab : SequencerVariation::a;
        }

        juce::var playToVar(const PlaySettings& p)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("tempo", p.tempo);
            o->setProperty("swing", juce::roundToInt(p.swing * 100.0f));
            o->setProperty("variation", variationName(p.variation));
            return juce::var(o);
        }

        void playFromVar(const juce::var& v, PlaySettings& p)
        {
            p.tempo = juce::jlimit(30.0, 300.0, static_cast<double>(v.getProperty("tempo", 120.0)));
            p.swing = juce::jlimit(0.5f, 0.75f, static_cast<float>(static_cast<double>(v.getProperty("swing", 50))) / 100.0f);
            p.variation = variationOf(v.getProperty("variation", "A").toString());
        }

        juce::var altToVar(const std::array<bool, laneCount>& alt)
        {
            juce::Array<juce::var> codes;
            for (int i = 0; i < laneCount; ++i)
                if (alt[static_cast<std::size_t>(i)] && instruments[static_cast<std::size_t>(i)].altCode != nullptr)
                    codes.add(juce::String(instruments[static_cast<std::size_t>(i)].altCode));
            return juce::var(codes);
        }

        void altFromVar(const juce::var& v, std::array<bool, laneCount>& alt)
        {
            alt = {};
            if (const auto* list = v.getArray())
                for (const auto& code : *list)
                {
                    bool isAlt = false;
                    if (const int lane = laneOfCode(code.toString().toRawUTF8(), isAlt); lane >= 0 && isAlt)
                        alt[static_cast<std::size_t>(lane)] = true;
                }
        }

        juce::var bankToVar(const SequencerBank& bank)
        {
            auto* o = new juce::DynamicObject();
            juce::Array<juce::var> patterns;
            for (const auto& p : bank.pattern)
                patterns.add(patternToVar(p));
            o->setProperty("patterns", patterns);
            o->setProperty("fill", patternToVar(bank.fill));
            return juce::var(o);
        }

        void bankFromVar(const juce::var& v, SequencerBank& bank)
        {
            if (const auto* list = v.getProperty("patterns", {}).getArray())
                for (int i = 0; i < SequencerBank::patterns && i < list->size(); ++i)
                    patternFromVar((*list)[i], bank.pattern[static_cast<std::size_t>(i)]);
            patternFromVar(v.getProperty("fill", {}), bank.fill);
        }
    }

    Project::Project()
    {
        banks.push_back(std::make_unique<SequencerBank>());
    }

    Project::Project(const Project& other) : kit(other.kit), play(other.play), bank(other.bank)
    {
        for (const auto& b : other.banks)
            banks.push_back(std::make_unique<SequencerBank>(*b));
    }

    Project& Project::operator=(const Project& other)
    {
        if (this != &other)
        {
            kit = other.kit;
            play = other.play;
            bank = other.bank;
            banks.clear();
            for (const auto& b : other.banks)
                banks.push_back(std::make_unique<SequencerBank>(*b));
        }
        return *this;
    }

    juce::var patternToVar(const SequencerPattern& p)
    {
        auto* o = new juce::DynamicObject();
        auto* lanes = new juce::DynamicObject();
        for (int i = 0; i < laneCount; ++i)
            lanes->setProperty(instruments[static_cast<std::size_t>(i)].code, laneToVar(p.lanes[static_cast<std::size_t>(i)]));
        o->setProperty("lanes", juce::var(lanes));
        o->setProperty("accent", laneToVar(p.accent));
        return juce::var(o);
    }

    bool patternFromVar(const juce::var& v, SequencerPattern& out)
    {
        const auto lanes = v.getProperty("lanes", {});
        if (! lanes.isObject())
            return false;
        out = {};
        for (int i = 0; i < laneCount; ++i)
        {
            const auto lane = lanes.getProperty(instruments[static_cast<std::size_t>(i)].code, {});
            if (lane.isObject())
                laneFromVar(lane, out.lanes[static_cast<std::size_t>(i)]);
        }
        if (const auto accent = v.getProperty("accent", {}); accent.isObject())
            laneFromVar(accent, out.accent);
        return true;
    }

    juce::String projectToJson(const Project& project)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("format", "cs808");
        root->setProperty("version", formatVersion);

        auto* kit = new juce::DynamicObject();
        auto* knobValues = new juce::DynamicObject();
        for (int i = 0; i < knobCount; ++i)
            knobValues->setProperty(knobs[static_cast<std::size_t>(i)].id, static_cast<double>(project.kit.knob[static_cast<std::size_t>(i)]));
        kit->setProperty("knobs", juce::var(knobValues));
        kit->setProperty("switches", altToVar(project.kit.alt));
        kit->setProperty("volumeDb", static_cast<double>(project.kit.volumeDb));
        root->setProperty("kit", juce::var(kit));

        root->setProperty("play", playToVar(project.play));
        juce::Array<juce::var> banks;
        for (const auto& b : project.banks)
            banks.add(bankToVar(*b));
        root->setProperty("banks", banks);
        root->setProperty("bank", project.bank);
        return juce::JSON::toString(juce::var(root), false);
    }

    juce::Result projectFromJson(const juce::String& json, Project& out)
    {
        juce::var root;
        if (const auto parsed = juce::JSON::parse(json, root); parsed.failed())
            return parsed;
        if (root.getProperty("format", "").toString() != "cs808")
            return juce::Result::fail("not a CoolSynth 808 project");
        if (static_cast<int>(root.getProperty("version", 0)) > formatVersion)
            return juce::Result::fail("made by a newer CoolSynth 808");

        Project p;
        const auto kit = root.getProperty("kit", {});
        const auto knobValues = kit.getProperty("knobs", {});
        for (int i = 0; i < knobCount; ++i)
            p.kit.knob[static_cast<std::size_t>(i)] = juce::jlimit(0.0f, 1.0f,
                static_cast<float>(static_cast<double>(knobValues.getProperty(knobs[static_cast<std::size_t>(i)].id, knobDefault(i)))));
        altFromVar(kit.getProperty("switches", {}), p.kit.alt);
        p.kit.volumeDb = juce::jlimit(-60.0f, 6.0f, static_cast<float>(static_cast<double>(kit.getProperty("volumeDb", 0.0))));
        playFromVar(root.getProperty("play", {}), p.play);

        p.banks.clear();
        if (const auto* list = root.getProperty("banks", {}).getArray())
            for (const auto& b : *list)
            {
                p.banks.push_back(std::make_unique<SequencerBank>());
                bankFromVar(b, *p.banks.back());
            }
        if (p.banks.empty())
            p.banks.push_back(std::make_unique<SequencerBank>());
        p.bank = juce::jlimit(0, static_cast<int>(p.banks.size()) - 1, static_cast<int>(root.getProperty("bank", 0)));
        out = p;
        return juce::Result::ok();
    }

    juce::var patternFileToVar(const PatternFile& f)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("format", "cs808pattern");
        o->setProperty("version", formatVersion);
        o->setProperty("name", f.name);
        if (f.style.isNotEmpty())
            o->setProperty("style", f.style);
        if (f.after.isNotEmpty())
            o->setProperty("after", f.after);
        if (f.notes.isNotEmpty())
            o->setProperty("notes", f.notes);
        o->setProperty("pattern", patternToVar(f.pattern));
        o->setProperty("switches", altToVar(f.alt));
        o->setProperty("play", playToVar(f.play));
        o->setProperty("usesB", f.usesB);
        return juce::var(o);
    }

    bool patternFileFromVar(const juce::var& v, PatternFile& out)
    {
        if (v.getProperty("format", "").toString() != "cs808pattern")
            return false;
        PatternFile f;
        if (! patternFromVar(v.getProperty("pattern", {}), f.pattern))
            return false;
        f.name = v.getProperty("name", "Untitled").toString();
        f.style = v.getProperty("style", "").toString();
        f.after = v.getProperty("after", "").toString();
        f.notes = v.getProperty("notes", "").toString();
        altFromVar(v.getProperty("switches", {}), f.alt);
        playFromVar(v.getProperty("play", {}), f.play);
        f.usesB = static_cast<bool>(v.getProperty("usesB", false));
        out = f;
        return true;
    }

    namespace
    {
        /** One pattern of the library's compact form: its lanes, accent and variations; the switches it needs. */
        bool libraryPatternFromVar(const juce::var& v, SequencerPattern& pattern, bool& usesB, std::array<bool, laneCount>& alt)
        {
            const auto lanes = v.getProperty("lanes", {});
            if (! lanes.isObject())
                return false;
            usesB = v.getProperty("variations", "A").toString() == "AB";
            if (const auto* props = lanes.getDynamicObject())
                for (const auto& entry : props->getProperties())
                {
                    bool isAlt = false;
                    const int lane = laneOfCode(entry.name.toString().toRawUTF8(), isAlt);
                    if (lane < 0)
                        return false; // a lane this kit does not have
                    auto& l = pattern.lanes[static_cast<std::size_t>(lane)];
                    stepsFromString(entry.value.getProperty("A", "").toString(), l.a);
                    stepsFromString(entry.value.getProperty("B", "").toString(), l.b);
                    l.length = juce::jlimit(1, steps, static_cast<int>(entry.value.getProperty("length", steps)));
                    if (isAlt)
                        alt[static_cast<std::size_t>(lane)] = true;
                }
            if (const auto accent = v.getProperty("accent", {}); accent.isObject())
            {
                stepsFromString(accent.getProperty("A", "").toString(), pattern.accent.a);
                stepsFromString(accent.getProperty("B", "").toString(), pattern.accent.b);
            }
            return true;
        }
    }

    bool patternFileFromLibraryVar(const juce::var& v, PatternFile& out)
    {
        PatternFile f;
        f.name = v.getProperty("name", "Untitled").toString();
        f.style = v.getProperty("style", "").toString();
        f.after = v.getProperty("after", "").toString();
        f.notes = v.getProperty("notes", "").toString();
        f.play.tempo = juce::jlimit(30.0, 300.0, static_cast<double>(v.getProperty("bpm", 120.0)));
        f.play.swing = juce::jlimit(0.5f, 0.75f, static_cast<float>(static_cast<double>(v.getProperty("swing", 50))) / 100.0f);
        if (const auto* parts = v.getProperty("parts", {}).getArray(); parts != nullptr && ! parts->isEmpty())
        {
            // A beat of several patterns in a chain. They share one variation setting: AB when any part uses B.
            for (int i = 0; i < parts->size(); ++i)
            {
                SequencerPattern p {};
                bool usesB = false;
                if (! libraryPatternFromVar((*parts)[i], p, usesB, f.alt))
                    return false;
                if (i == 0)
                {
                    f.pattern = p;
                    f.usesB = usesB;
                }
                else
                    f.moreParts.push_back({ p, usesB });
            }
            bool anyB = f.usesB;
            for (const auto& part : f.moreParts)
                anyB = anyB || part.usesB;
            f.play.variation = anyB ? SequencerVariation::ab : SequencerVariation::a;
            out = f;
            return true;
        }
        if (! libraryPatternFromVar(v, f.pattern, f.usesB, f.alt))
            return false;
        f.play.variation = f.usesB ? SequencerVariation::ab : SequencerVariation::a;
        out = f;
        return true;
    }

    void applyNotes(SequencerBank& bank, const KitValues& kit) noexcept
    {
        const auto apply = [&](SequencerPattern& p)
        {
            for (int i = 0; i < SequencerPattern::maxLanes; ++i)
                p.lanes[static_cast<std::size_t>(i)].note = i < laneCount ? laneNote(i, kit) : -1;
        };
        for (auto& p : bank.pattern)
            apply(p);
        apply(bank.fill);
    }

    juce::File appDataFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CoolSynth808");
    }

    juce::File userPatternsFolder()
    {
        return appDataFolder().getChildFile("Patterns");
    }
}
