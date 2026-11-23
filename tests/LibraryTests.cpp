#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "Library.h"
#include "Processor.h"

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
    }
};

static LibraryTests libraryTests;
