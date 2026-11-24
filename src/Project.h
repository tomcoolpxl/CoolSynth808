#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "Kit.h"
#include "synth/StepSequencer.h"

/*
    A project: the kit, the banks of patterns and the playing settings, saved as one JSON file (.cs808). The app
    keeps the last one and opens it again. Lane notes are not stored with the patterns: a lane is an instrument, and
    the kit's switches say whether it strikes the tom or the conga (and the rim shot or the claves, the clap or the
    maracas); the sequencer gets the notes when the bank is handed to it.

    Pattern files (.cs808pattern) hold one pattern and the switches it was written with: the library's and your own.
*/
namespace cs808
{
    using coolsynth::synth::SequencerBank;
    using coolsynth::synth::SequencerLane;
    using coolsynth::synth::SequencerPattern;
    using coolsynth::synth::SequencerSettings;
    using coolsynth::synth::SequencerVariation;

    struct PlaySettings
    {
        double tempo = 120.0;
        float swing = 0.5f; // 0.5 straight .. 0.75
        SequencerVariation variation = SequencerVariation::a;
        bool loop = true;        // the chain round and round; off, once through and then the transport stops (D297)
        bool changeAtBar = true; // a pattern picked while playing comes in at the next bar line; off, at the end of the
                                 // pattern that plays (D297)
    };

    struct Project
    {
        KitValues kit {};
        PlaySettings play {};
        static constexpr int maxBanks = 64;
        std::vector<std::unique_ptr<SequencerBank>> banks; // at least one, at most maxBanks
        int bank = 0;

        Project();
        Project(const Project& other);
        Project& operator=(const Project& other);
        [[nodiscard]] SequencerBank& currentBank() noexcept { return *banks[static_cast<std::size_t>(bank)]; }
        [[nodiscard]] const SequencerBank& currentBank() const noexcept { return *banks[static_cast<std::size_t>(bank)]; }
    };

    /** A pattern as it sits in a file: its steps, and the switches and settings it was written for. */
    struct PatternFile
    {
        juce::String name;
        juce::String style;
        juce::String after; // the record(s) it follows, for the library's beats
        juce::String notes;
        SequencerPattern pattern {};
        std::array<bool, laneCount> alt {};
        PlaySettings play {};
        bool usesB = false;
        /** A beat longer than A and B: the patterns after the first, played after it in a chain. */
        struct Part
        {
            SequencerPattern pattern {};
            bool usesB = false;
        };
        std::vector<Part> moreParts;
    };

    // ---- JSON ---------------------------------------------------------------------------------------------------
    [[nodiscard]] juce::var patternToVar(const SequencerPattern& p);
    /** Reads what it can; returns false if the value is not a pattern. */
    bool patternFromVar(const juce::var& v, SequencerPattern& out);

    [[nodiscard]] juce::String projectToJson(const Project& project);
    /** Reads a project; on failure leaves `out` untouched and returns the reason. */
    [[nodiscard]] juce::Result projectFromJson(const juce::String& json, Project& out);

    [[nodiscard]] juce::var patternFileToVar(const PatternFile& f);
    bool patternFileFromVar(const juce::var& v, PatternFile& out);

    /**
        The library's compact form: each lane as step strings, "x" a hit, "." a rest, "2" or "3" a roll; lanes named by
        the instrument's code (BD, LC for the low conga...), an accent row, the tempo, swing (50..75) and variations.
    */
    bool patternFileFromLibraryVar(const juce::var& v, PatternFile& out);

    /** The bank's lanes take the kit's notes: what the sequencer is handed. */
    void applyNotes(SequencerBank& bank, const KitValues& kit) noexcept;

    /** Where the app keeps its things: %APPDATA%\CoolSynth808 (and its folder for patterns). */
    [[nodiscard]] juce::File appDataFolder();
    [[nodiscard]] juce::File userPatternsFolder();
}
