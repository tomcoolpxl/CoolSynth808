#include <juce_core/juce_core.h>

#include <memory>

#include "Kit.h"
#include "MidiExport.h"
#include "Project.h"
#include "synth/v3/Voice.h"

/*
    The project: its file round trip, the library's compact form (one pattern and several), the lanes' notes from the
    kit's switches, the kit's engine settings, and a pattern as a MIDI file.
*/
using namespace cs808;

class ProjectTests final : public juce::UnitTest
{
public:
    ProjectTests() : juce::UnitTest("Project", "CoolSynth808") {}

    void runTest() override
    {
        beginTest("a_project_reads_back_as_it_was_written");
        {
            auto p = std::make_unique<Project>();
            p->kit.knob[2] = 0.31f;
            p->kit.alt[2] = true; // the low conga
            p->kit.volumeDb = -6.0f;
            p->play.tempo = 98.5;
            p->play.swing = 0.62f;
            p->play.variation = SequencerVariation::ab;
            auto& lane = p->currentBank().pattern[3].lanes[4];
            lane.a[5].on = true;
            lane.a[5].chance = 0.35f;
            lane.a[5].hits = 3;
            lane.b[9].on = true;
            lane.length = 7;
            lane.mute = true;
            p->currentBank().fill.accent.a[0].on = true;
            p->banks.push_back(std::make_unique<SequencerBank>());
            p->banks[1]->pattern[15].lanes[10].a[15].on = true;
            p->bank = 1;

            auto q = std::make_unique<Project>();
            expect(projectFromJson(projectToJson(*p), *q).wasOk());
            expectWithinAbsoluteError(q->kit.knob[2], 0.31f, 1.0e-6f);
            expect(q->kit.alt[2] && ! q->kit.alt[3]);
            expectEquals(q->kit.volumeDb, -6.0f);
            expectEquals(q->play.tempo, 98.5);
            expectWithinAbsoluteError(q->play.swing, 0.62f, 1.0e-6f);
            expect(q->play.variation == SequencerVariation::ab);
            expectEquals(static_cast<int>(q->banks.size()), 2);
            expectEquals(q->bank, 1);
            const auto& l = q->banks[0]->pattern[3].lanes[4];
            expect(l.a[5].on && l.a[5].hits == 3 && l.b[9].on && ! l.a[4].on);
            expectWithinAbsoluteError(l.a[5].chance, 0.35f, 1.0e-6f);
            expectEquals(l.length, 7);
            expect(l.mute && ! l.solo);
            expect(q->banks[0]->fill.accent.a[0].on);
            expect(q->banks[1]->pattern[15].lanes[10].a[15].on);
        }

        beginTest("a_file_that_is_not_a_project_is_refused_and_changes_nothing");
        {
            auto p = std::make_unique<Project>();
            p->play.tempo = 133.0;
            expect(projectFromJson("{\"format\":\"something else\"}", *p).failed());
            expect(projectFromJson("not json at all", *p).failed());
            expectEquals(p->play.tempo, 133.0);
        }

        beginTest("the_library_form_reads_its_lanes_switches_and_settings");
        {
            const auto json = juce::String(R"({"id":"t","name":"Test 1982","style":"electro","bpm":112,"swing":58,"variations":"AB",
                "lanes":{"BD":{"A":"x.....x...x.....","B":"x...x...x...x..."},"LC":{"A":"....3...........","length":12},"CH":{"A":"x.x.x.x.x.x.x.x."}},
                "accent":{"A":"x...............","B":"....x..........."}})");
            PatternFile f;
            expect(patternFileFromLibraryVar(juce::JSON::parse(json), f));
            expectEquals(f.name, juce::String("Test 1982"));
            expectEquals(f.play.tempo, 112.0);
            expectWithinAbsoluteError(f.play.swing, 0.58f, 1.0e-6f);
            expect(f.usesB && f.play.variation == SequencerVariation::ab);
            expect(f.alt[2], "LC sets the low tom's switch to the conga");
            expect(f.pattern.lanes[0].a[6].on && f.pattern.lanes[0].b[4].on && ! f.pattern.lanes[0].a[4].on);
            expectEquals(static_cast<int>(f.pattern.lanes[2].a[4].hits), 3);
            expectEquals(f.pattern.lanes[2].length, 12);
            expect(f.pattern.accent.a[0].on && f.pattern.accent.b[4].on);

            PatternFile bad;
            expect(! patternFileFromLibraryVar(juce::JSON::parse(R"({"lanes":{"XX":{"A":"x"}}})"), bad), "an unknown lane is refused");
        }

        beginTest("a_library_beat_of_several_parts_reads_each_part");
        {
            const auto json = juce::String(R"({"id":"amen","name":"Breakbeat 1969","bpm":136,"parts":[
                {"lanes":{"BD":{"A":"x.x.......xx...."},"SD":{"A":"....x..x.x..x..x"}},"variations":"A"},
                {"lanes":{"BD":{"A":"x.x.......x....."},"SD":{"A":"....x..x.x....x."}},"variations":"AB"}]})");
            PatternFile f;
            expect(patternFileFromLibraryVar(juce::JSON::parse(json), f));
            expectEquals(static_cast<int>(f.moreParts.size()), 1);
            expect(f.pattern.lanes[0].a[11].on && ! f.moreParts[0].pattern.lanes[0].a[11].on);
            expect(f.play.variation == SequencerVariation::ab, "AB when a part uses B");
        }

        beginTest("a_pattern_file_reads_back");
        {
            PatternFile f;
            f.name = "Mine";
            f.pattern.lanes[6].a[3].on = true;
            f.alt[6] = true;
            f.play.tempo = 101.0;
            PatternFile g;
            expect(patternFileFromVar(patternFileToVar(f), g));
            expectEquals(g.name, juce::String("Mine"));
            expect(g.pattern.lanes[6].a[3].on && g.alt[6]);
            expectEquals(g.play.tempo, 101.0);
        }

        beginTest("the_lanes_take_the_kits_notes_and_switches");
        {
            auto bank = std::make_unique<SequencerBank>();
            KitValues kit;
            applyNotes(*bank, kit);
            expectEquals(bank->pattern[0].lanes[0].note, 36);
            expectEquals(bank->pattern[0].lanes[2].note, 43);
            expectEquals(bank->fill.lanes[10].note, 42);
            expectEquals(bank->pattern[0].lanes[11].note, -1, "no twelfth drum");
            kit.alt[2] = kit.alt[5] = kit.alt[6] = true;
            applyNotes(*bank, kit);
            expectEquals(bank->pattern[7].lanes[2].note, 64);
            expectEquals(bank->pattern[7].lanes[5].note, 75);
            expectEquals(bank->pattern[7].lanes[6].note, 70);
            bool alt = false;
            expectEquals(laneOfNote(63, alt), 3);
            expect(alt);
            expectEquals(laneOfNote(44, alt), 10);
        }

        beginTest("the_kit_sets_the_three_808_generators_in_the_kit_mode");
        {
            KitValues kit;
            auto settings = std::make_unique<coolsynth::synth::v3::EngineSettings>();
            fillEngineSettings(kit, *settings);
            namespace v3 = coolsynth::synth::v3;
            const auto& part = settings->parts[0];
            expect(part.playMode == v3::PlayMode::kit);
            expectEquals(part.voice.generators[0].type, v3::generatorType::drums808);
            expectEquals(part.voice.generators[1].type, v3::generatorType::snareClap808);
            expectEquals(part.voice.generators[2].type, v3::generatorType::metal808);
            expectEquals(part.voice.generators[0].p[1], 0.5f, "BD Tone at 12 o'clock");
            expectEquals(part.voice.generators[0].p[0], 1.0f, "BD Level full");
            expect(v3::layerEmpty(settings->parts[1]), "layer B silent");
            kit.knob[0] = 0.9f; // the Accent, on all three
            fillEngineSettings(kit, *settings);
            expectWithinAbsoluteError(settings->parts[0].voice.generators[0].p[10], 0.9f, 1.0e-6f);
            expectWithinAbsoluteError(settings->parts[0].voice.generators[1].p[4], 0.9f, 1.0e-6f);
            expectWithinAbsoluteError(settings->parts[0].voice.generators[2].p[7], 0.9f, 1.0e-6f);
        }

        beginTest("a_pattern_becomes_a_midi_file_on_the_sixteenths");
        {
            SequencerPattern p {};
            p.lanes[0].a[0].on = true;
            p.lanes[0].a[8].on = true;
            p.lanes[10].a[4].on = true;
            p.lanes[10].a[4].hits = 2;
            p.accent.a[8].on = true;
            KitValues kit;
            PlaySettings play;
            play.tempo = 100.0;
            const auto file = patternToMidi(p, kit, play);
            expectEquals(file.getNumTracks(), 1);
            std::vector<std::pair<int, int>> ons; // tick, note
            std::vector<int> velocities;
            const auto* track = file.getTrack(0);
            for (const auto* e : *track)
                if (e->message.isNoteOn())
                {
                    ons.emplace_back(juce::roundToInt(e->message.getTimeStamp()), e->message.getNoteNumber());
                    velocities.push_back(e->message.getVelocity());
                }
            expectEquals(static_cast<int>(ons.size()), 4);
            expect(ons[0] == std::pair<int, int> { 0, 36 });
            expect(ons[1] == std::pair<int, int> { 960, 42 });
            expect(ons[2] == std::pair<int, int> { 1080, 42 }, "the roll's second hit half a sixteenth later");
            expect(ons[3] == std::pair<int, int> { 1920, 36 });
            expectEquals(velocities[0], 100);
            expectEquals(velocities[3], 127, "the accented step");
            int tempoEvents = 0;
            for (const auto* e : *track)
                if (e->message.isTempoMetaEvent())
                {
                    ++tempoEvents;
                    expectWithinAbsoluteError(60.0 / e->message.getTempoSecondsPerQuarterNote(), 100.0, 0.01);
                }
            expectEquals(tempoEvents, 1);
        }
    }
};

static ProjectTests projectTests;
