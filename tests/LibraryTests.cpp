#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "Library.h"
#include "Editor.h"
#include "Processor.h"
#include "ui/LibraryShelf.h"

/*
    The built-in library: every beat reads without a problem, has a name, a style and its sources' verdict, and plays
    on the kit (each beat for one pass of its chain, nothing silent).
*/
using namespace cs808;

class LibraryTests final : public juce::UnitTest
{
public:
    LibraryTests() : juce::UnitTest("Library", "CoolSynth808") {}

    void runTest() override
    {
        beginTest("every_built_in_beat_reads");
        {
            const auto data = juce::File(COOLSYNTH808_SOURCE_DIR).getChildFile("resources/library/patterns.json");
            juce::StringArray problems;
            const auto entries = libraryFromJson(data.loadFileAsString(), &problems);
            expect(problems.isEmpty(), problems.joinIntoString("; "));
            const auto built = builtInLibrary();
            expectEquals(static_cast<int>(built.size()), static_cast<int>(entries.size()), "the compiled-in file is the repository's");
            juce::StringArray ids;
            for (const auto& e : entries)
            {
                expect(e.pattern.name.isNotEmpty() && e.pattern.style.isNotEmpty(), e.id);
                expect(! ids.contains(e.id), "a second " + e.id);
                ids.add(e.id);
                int hits = 0;
                for (const auto& lane : e.pattern.pattern.lanes)
                    for (const auto& s : lane.a)
                        hits += s.on ? 1 : 0;
                expectGreaterThan(hits, 0, e.id + " has steps");
            }
        }

        beginTest("every_built_in_beat_plays");
        {
            const auto entries = builtInLibrary();
            for (const auto& e : entries)
            {
                auto p = std::make_unique<Processor>();
                p->setPlayConfigDetails(0, 2, 48000.0, 512);
                p->prepareToPlay(48000.0, 512);
                Project project;
                project.play = e.pattern.play;
                project.kit.alt = e.pattern.alt;
                project.currentBank().pattern[0] = e.pattern.pattern;
                for (std::size_t i = 0; i < e.pattern.moreParts.size() && i + 1 < SequencerBank::patterns; ++i)
                    project.currentBank().pattern[i + 1] = e.pattern.moreParts[i].pattern;
                p->replaceProject(project, false);
                std::vector<int> chain;
                for (int i = 0; i <= static_cast<int>(e.pattern.moreParts.size()); ++i)
                    chain.push_back(i);
                p->setChain(chain);
                p->setPlaying(true);
                juce::AudioBuffer<float> buffer(2, 512);
                float loudest = 0.0f;
                bool finite = true;
                const double beats = 4.0 * static_cast<double>(chain.size()) * (e.pattern.play.variation == SequencerVariation::ab ? 2 : 1);
                const auto samples = static_cast<int>(beats * 60.0 / e.pattern.play.tempo * 48000.0);
                for (int pos = 0; pos < samples; pos += 512)
                {
                    juce::MidiBuffer midi;
                    p->processBlock(buffer, midi);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i)
                        {
                            const float v = buffer.getSample(c, i);
                            finite = finite && std::isfinite(v);
                            loudest = std::max(loudest, std::abs(v));
                        }
                }
                expect(finite, e.id + " stays finite");
                expectGreaterThan(loudest, 0.02f, e.id + " sounds");
                expectLessThan(loudest, 1.0001f, e.id + " stays under the clip");
            }
        }

        beginTest("style_headings_read_as_words");
        {
            expectEquals(styleHeading("acid-house"), juce::String("Acid house"));
            expectEquals(styleHeading("idm"), juce::String("IDM"));
            expectEquals(styleHeading(""), juce::String("Other"));
        }

        // D273: the shelf.
        beginTest("every_built_in_beat_sits_in_one_family_and_the_shelves_filter");
        {
            const auto beats = builtInLibrary();
            int inFamilies = 0;
            for (const auto& family : libraryFamilies())
                for (const auto& e : beats)
                    inFamilies += familyOf(e.pattern.style) == family ? 1 : 0;
            expectEquals(inFamilies, static_cast<int>(beats.size()), "each beat in exactly one family");
            expectEquals(familyOf("acid-house"), juce::String("House and acid"));

            LibraryShelf shelf;
            shelf.setSize(1240, 600);
            shelf.showForTesting("all");
            expectEquals(shelf.shownCount(), static_cast<int>(beats.size()), "every classic");
            shelf.showForTesting("House and acid");
            const int house = shelf.shownCount();
            expect(house > 0 && house < static_cast<int>(beats.size()));
            shelf.showForTesting("all", {}, 1980);
            for (int r = 0; r < shelf.shownCount(); ++r)
            {
                shelf.selectForTesting(r);
                expect(shelf.selectedEntry() != nullptr && shelf.selectedEntry()->year / 10 == 198, "the eighties only");
            }
            shelf.showForTesting("all", "planet rock");
            expect(shelf.shownCount() >= 1, "the search finds a name or a record");
            shelf.showForTesting("all", "no such beat anywhere");
            expectEquals(shelf.shownCount(), 0);
        }

        beginTest("an_audition_plays_in_place_and_takes_nothing_back_and_load_is_one_step");
        {
            auto p = std::make_unique<Processor>();
            p->setPlayConfigDetails(0, 2, 48000.0, 480);
            p->prepareToPlay(48000.0, 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditor());
            auto& editor = *dynamic_cast<Editor*>(ed.get());
            const auto before = p->project().currentBank().pattern[0];
            const bool couldUndo = p->canUndo();

            editor.openLibrary();
            auto* shelf = editor.libraryForTesting();
            expect(shelf != nullptr);
            shelf->showForTesting("all");
            shelf->selectForTesting(3);
            const auto beat = *shelf->selectedEntry();
            shelf->toggleAudition();
            expect(editor.isAuditioning());
            expect(patternsEqual(p->project().currentBank().pattern[0], beat.pattern.pattern), "the beat in pattern 1");
            expect(p->canUndo() == couldUndo, "nothing for undo");
            shelf->selectForTesting(4); // listening follows the choice
            expect(patternsEqual(p->project().currentBank().pattern[0], shelf->selectedEntry()->pattern.pattern));
            shelf->toggleAudition();
            expect(! editor.isAuditioning());
            expect(patternsEqual(p->project().currentBank().pattern[0], before), "your pattern back");

            shelf->selectForTesting(3);
            shelf->toggleAudition();
            if (shelf->onLoad)
                shelf->onLoad(*shelf->selectedEntry());
            expect(! editor.isAuditioning());
            expect(patternsEqual(p->project().currentBank().pattern[0], beat.pattern.pattern), "loaded");
            expect(p->undo(), "one step");
            expect(patternsEqual(p->project().currentBank().pattern[0], before), "and undo takes it back");
            ed.reset();
        }

        // D274: in time.
        beginTest("an_audition_while_playing_comes_in_on_the_next_bar_line_to_the_sample");
        {
            constexpr int block = 448; // the bar line falls inside a block, which is split there
            auto p = std::make_unique<Processor>();
            p->setPlayConfigDetails(0, 2, 48000.0, block);
            p->prepareToPlay(48000.0, block);
            std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditor());
            auto& editor = *dynamic_cast<Editor*>(ed.get());
            Project silent; // nothing on any step: the first sound is the beat's
            silent.play.tempo = 120.0;
            p->replaceProject(silent, false);

            // A beat in A with a hit on its first step and a tempo of its own.
            const auto beats = builtInLibrary();
            const LibraryEntry* chosen = nullptr;
            for (const auto& e : beats)
            {
                bool first = false;
                for (const auto& lane : e.pattern.pattern.lanes)
                    first = first || lane.a[0].on;
                if (first && e.pattern.play.variation == SequencerVariation::a && std::abs(e.pattern.play.tempo - 120.0) > 5.0)
                {
                    chosen = &e;
                    break;
                }
            }
            expect(chosen != nullptr);
            if (chosen != nullptr)
            {
                juce::AudioBuffer<float> buffer(2, block);
                std::int64_t at = 0, firstSound = -1;
                const auto run = [&](std::int64_t until)
                {
                    while (at < until)
                    {
                        juce::MidiBuffer midi;
                        p->processBlock(buffer, midi);
                        for (int i = 0; i < block && firstSound < 0; ++i)
                            if (std::abs(buffer.getSample(0, i)) > 1.0e-4f)
                                firstSound = at + i;
                        at += block;
                    }
                };
                p->setPlaying(true);
                run(36000); // a beat and a half in
                editor.startAudition(*chosen);
                expect(p->waitingForBar(), "waits for the bar line");
                run(90000);
                expect(p->waitingForBar() && firstSound < 0, "the bar plays on as it was");
                expectWithinAbsoluteError(p->tempoNow(), 120.0, 1.0e-9);
                run(100000);
                expect(! p->waitingForBar(), "in on the bar line");
                expect(firstSound >= 96000 && firstSound < 96000 + 48, "its first hit on the bar line, to the sample: "
                       + juce::String(firstSound));
                expectWithinAbsoluteError(p->tempoNow(), chosen->pattern.play.tempo, 1.0e-9);

                // Loaded as it plays: no wait, no jump, and one undo step back to the silent pattern.
                editor.openLibrary();
                if (auto* shelf = editor.libraryForTesting(); shelf != nullptr && shelf->onLoad)
                    shelf->onLoad(*chosen);
                expect(! editor.isAuditioning() && ! p->waitingForBar());
                expect(p->isPlaying(), "it plays on");
                expect(p->undo());
                expect(patternsEqual(p->project().currentBank().pattern[0], silent.currentBank().pattern[0]));
            }
            ed.reset();
        }

        beginTest("a_pattern_of_yours_renamed_keeps_its_beat");
        {
            const auto folder = juce::File::createTempFile("cs808-shelf");
            folder.createDirectory();
            PatternFile f;
            f.name = "Old name";
            f.pattern.lanes[0].a[0].on = true;
            const auto file = folder.getChildFile("Old name.cs808pattern");
            file.replaceWithText(juce::JSON::toString(patternFileToVar(f), false));
            const auto renamed = renameUserPattern(file, "New name");
            expect(renamed.existsAsFile() && ! file.existsAsFile());
            expectEquals(renamed.getFileNameWithoutExtension(), juce::String("New name"));
            PatternFile back;
            expect(patternFileFromVar(juce::JSON::parse(renamed.loadFileAsString()), back));
            expectEquals(back.name, juce::String("New name"));
            expect(back.pattern.lanes[0].a[0].on);
            expect(renameUserPattern(renamed, "  ") == juce::File(), "an empty name is refused");
            folder.deleteRecursively();
        }
    }

    static bool patternsEqual(const SequencerPattern& a, const SequencerPattern& b)
    {
        for (std::size_t lane = 0; lane < a.lanes.size(); ++lane)
            for (std::size_t s = 0; s < 16; ++s)
                if (a.lanes[lane].a[s].on != b.lanes[lane].a[s].on || a.lanes[lane].b[s].on != b.lanes[lane].b[s].on)
                    return false;
        return true;
    }
};

static LibraryTests libraryTests;
