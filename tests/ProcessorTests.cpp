#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "Processor.h"
#include "Project.h"

/*
    The processor offline: the 808 kit sounding where the steps are, MIDI clock out at 24 a beat, an incoming clock
    taking over tempo and start, recording onto the nearest step, the struck drums reported, undo, and the state.
    At 48 kHz and 120 BPM a beat is 24000 samples.
*/
using namespace cs808;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 480;

    struct Run
    {
        std::vector<float> left;
        std::vector<std::pair<std::int64_t, juce::MidiMessage>> midiOut;
    };

    /** Renders `samples`; `in` gives each block's incoming MIDI (block start, block size). */
    Run render(Processor& p, std::int64_t samples, const std::function<void(juce::MidiBuffer&, std::int64_t, int)>& in = {})
    {
        Run run;
        juce::AudioBuffer<float> buffer(2, kBlock);
        for (std::int64_t pos = 0; pos < samples; pos += kBlock)
        {
            const int n = static_cast<int>(std::min<std::int64_t>(kBlock, samples - pos));
            buffer.setSize(2, n, false, false, true);
            juce::MidiBuffer midi;
            if (in)
                in(midi, pos, n);
            p.processBlock(buffer, midi);
            for (int i = 0; i < n; ++i)
                run.left.push_back(buffer.getSample(0, i));
            for (const auto m : midi)
                run.midiOut.emplace_back(pos + m.samplePosition, m.getMessage());
        }
        return run;
    }

    float peak(const std::vector<float>& x, std::int64_t from, std::int64_t to)
    {
        float m = 0.0f;
        for (auto i = std::max<std::int64_t>(0, from); i < std::min<std::int64_t>(to, static_cast<std::int64_t>(x.size())); ++i)
            m = std::max(m, std::abs(x[static_cast<std::size_t>(i)]));
        return m;
    }

    std::unique_ptr<Processor> prepared()
    {
        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails(0, 2, kRate, kBlock);
        p->prepareToPlay(kRate, kBlock);
        Project empty;
        p->replaceProject(empty, false);
        return p;
    }
}

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest("Processor", "CoolSynth808") {}

    void runTest() override
    {
        beginTest("the_kit_sounds_where_the_steps_are");
        {
            auto p = prepared();
            p->editBank([](SequencerBank& b) { b.pattern[0].lanes[1].a[0].on = true; b.pattern[0].lanes[1].a[8].on = true; }, false);
            const auto silent = render(*p, 24000);
            expectLessThan(peak(silent.left, 0, 24000), 1.0e-6f, "stopped, nothing sounds");
            p->setPlaying(true);
            const auto run = render(*p, 4 * 24000);
            // The snare at beat 1 and beat 3 (steps 1 and 9); quiet just before each, loud just after.
            const float first = peak(run.left, 0, 2400);
            const float before = peak(run.left, 48000 - 3000, 48000 - 100);
            const float second = peak(run.left, 48000, 48000 + 2400);
            expectGreaterThan(first, 0.02f);
            expectGreaterThan(second, before * 3.0f + 0.01f);
            expect(p->isPlaying());
        }

        beginTest("the_lights_follow_each_drum_as_its_circuit_plays");
        {
            // D313: the panel's lights come from the engine's circuits as they play. Each drum struck alone lights its
            // own lane and no other; a longer Decay keeps the bass drum's light up longer. The peaks at the kit's
            // defaults are logged: the panel scales each lane's brightness by them.
            const auto lightsOf = [this](int drum, float bdDecay, std::vector<float>& lane, int& otherLit)
            {
                auto p = prepared();
                p->editBank([drum](SequencerBank& b) { b.pattern[0].lanes[static_cast<std::size_t>(drum)].a[0].on = true; }, false);
                if (bdDecay >= 0.0f)
                    p->editKit([bdDecay](KitValues& kit) { kit.knob[3] = bdDecay; }); // the bass drum's Decay
                p->setPlaying(true);
                juce::AudioBuffer<float> buffer(2, kBlock);
                std::array<float, 12> lights {};
                otherLit = 0;
                for (int b = 0; b < 48000 / kBlock; ++b) // a second: the first bar's half, the hit at its start
                {
                    juce::MidiBuffer midi;
                    p->processBlock(buffer, midi);
                    p->takeLights(lights);
                    lane.push_back(lights[static_cast<std::size_t>(drum + 1)]);
                    for (int l = 1; l < 12; ++l)
                        if (l != drum + 1 && lights[static_cast<std::size_t>(l)] > 0.0f)
                            ++otherLit;
                }
                expectGreaterThan(static_cast<int>(p->lightReports()), 0);
            };
            juce::String peaks;
            for (int drum = 0; drum < laneCount; ++drum)
            {
                std::vector<float> lane;
                int otherLit = 0;
                lightsOf(drum, -1.0f, lane, otherLit);
                const float top = *std::max_element(lane.begin(), lane.end());
                peaks << instruments[static_cast<std::size_t>(drum)].code << " " << juce::String(top, 4) << "  ";
                expectGreaterThan(top, 0.005f, juce::String(instruments[static_cast<std::size_t>(drum)].code) + " lights");
                expectEquals(otherLit, 0, juce::String(instruments[static_cast<std::size_t>(drum)].code) + " lights no other lane");
            }
            logMessage("each drum's light at the kit's defaults: " + peaks);
            const auto heldFor = [&](float decay)
            {
                std::vector<float> lane;
                int otherLit = 0;
                lightsOf(0, decay, lane, otherLit);
                const float top = *std::max_element(lane.begin(), lane.end());
                return static_cast<int>(std::count_if(lane.begin(), lane.end(), [top](float v) { return v > 0.1f * top; }));
            };
            const int shortly = heldFor(0.1f), long_ = heldFor(0.9f);
            logMessage("blocks the bass drum's light holds above a tenth: Decay 0.1 " + juce::String(shortly) + ", 0.9 " + juce::String(long_));
            expectGreaterThan(long_, shortly + 5, "a longer Decay keeps the light longer");
        }

        beginTest("a_switch_strikes_the_other_instrument");
        {
            auto p = prepared();
            p->editBank([](SequencerBank& b) { b.pattern[0].lanes[5].a[0].on = true; }, false); // the rim shot lane
            p->setPlaying(true);
            const auto rim = render(*p, 12000);
            auto q = prepared();
            q->editBank([](SequencerBank& b) { b.pattern[0].lanes[5].a[0].on = true; }, false);
            q->editKit([](KitValues& kit) { kit.alt[5] = true; }); // the claves
            q->setPlaying(true);
            const auto claves = render(*q, 12000);
            expectGreaterThan(peak(rim.left, 0, 12000), 0.01f);
            expectGreaterThan(peak(claves.left, 0, 12000), 0.01f);
            double diff = 0.0;
            for (int i = 0; i < 12000; ++i)
                diff += std::abs(rim.left[static_cast<std::size_t>(i)] - claves.left[static_cast<std::size_t>(i)]);
            expectGreaterThan(diff, 1.0, "the claves are another sound");
        }

        beginTest("its_own_transport_sends_midi_clock_24_a_beat");
        {
            auto p = prepared();
            p->setPlaying(true);
            const auto run = render(*p, 2 * 24000);
            int clocks = 0, starts = 0;
            for (const auto& [at, m] : run.midiOut)
            {
                clocks += m.isMidiClock() ? 1 : 0;
                starts += m.isMidiStart() ? 1 : 0;
            }
            expectEquals(starts, 1);
            expectEquals(clocks, 48);
            p->setPlaying(false);
            const auto after = render(*p, 4800);
            int stops = 0;
            for (const auto& [at, m] : after.midiOut)
                stops += m.isMidiStop() ? 1 : 0;
            expectEquals(stops, 1);
        }

        beginTest("with_the_loop_off_its_transport_stops_after_the_chain");
        {
            // D297: the chain 0, 0 (two bars) with the loop off plays them and stops; with the loop on it plays on.
            for (const bool loop : { false, true })
            {
                auto p = prepared();
                p->editBank([](SequencerBank& b) { b.pattern[0].lanes[1].a[0].on = true; }, false);
                p->editPlay([loop](PlaySettings& s) { s.loop = loop; });
                p->setChain({ 0, 0 });
                p->setPlaying(true);
                render(*p, 7 * 24000); // seven beats: still in the second bar
                expect(p->isPlaying(), "still playing in its last bar");
                render(*p, 2 * 24000);
                expect(p->isPlaying() == loop, loop ? "the loop on plays on" : "the loop off has stopped");
            }
        }

        beginTest("an_incoming_clock_takes_over_tempo_and_start");
        {
            auto p = prepared();
            p->editBank([](SequencerBank& b) { b.pattern[0].lanes[0].a[0].on = true; }, false);
            const double interval = kRate * 60.0 / 100.0 / 24.0; // 100 BPM: 1200 samples a pulse
            std::int64_t nextPulse = 0;
            bool startSent = false;
            const auto run = render(*p, 2 * 48000, [&](juce::MidiBuffer& midi, std::int64_t pos, int n)
            {
                if (! startSent)
                {
                    midi.addEvent(juce::MidiMessage::midiStart(), 0);
                    startSent = true;
                }
                for (; nextPulse < pos + n; nextPulse = static_cast<std::int64_t>(std::llround((std::llround(nextPulse / interval) + 1) * interval)))
                    if (nextPulse >= pos)
                        midi.addEvent(juce::MidiMessage::midiClock(), static_cast<int>(nextPulse - pos));
            });
            expect(p->followingClock());
            expect(p->isPlaying(), "the clock's start started it");
            expectWithinAbsoluteError(p->tempoNow(), 100.0, 1.5);
            int clocksOut = 0;
            for (const auto& [at, m] : run.midiOut)
                clocksOut += m.isMidiClock() ? 1 : 0;
            expectEquals(clocksOut, 0, "no clock of its own while it follows one");
            expectGreaterThan(peak(run.left, 0, 4800), 0.02f, "the first bar's kick");
        }

        beginTest("recording_writes_a_hit_on_the_nearest_step");
        {
            auto p = prepared();
            p->setRecording(true);
            p->setPlaying(true);
            const std::int64_t at = 24000 + 480 + 7; // just after beat 1: step 5
            (void) render(*p, 2 * 24000, [&](juce::MidiBuffer& midi, std::int64_t pos, int n)
            {
                if (at >= pos && at < pos + n)
                    midi.addEvent(juce::MidiMessage::noteOn(10, 38, static_cast<juce::uint8>(100)), static_cast<int>(at - pos));
            });
            expect(p->applyRecordedHits());
            const auto& lane = p->project().currentBank().pattern[0].lanes[1];
            expect(lane.a[4].on, "the snare on step 5");
            int on = 0;
            for (const auto& s : lane.a)
                on += s.on ? 1 : 0;
            expectEquals(on, 1);
            expect(p->canUndo(), "a recording is one undo step");
        }

        beginTest("the_struck_drums_are_reported");
        {
            auto p = prepared();
            p->editBank([](SequencerBank& b) { b.pattern[0].lanes[0].a[0].on = true; b.pattern[0].lanes[10].a[0].on = true; b.pattern[0].accent.a[0].on = true; }, false);
            (void) p->takeStruckLanes();
            p->setPlaying(true);
            (void) render(*p, 4800);
            const auto bits = p->takeStruckLanes();
            expect((bits & 1u) != 0 && (bits & (1u << 10)) != 0 && (bits & (1u << laneCount)) != 0);
            expect((bits & 2u) == 0, "not the snare");
        }

        beginTest("undo_and_redo_take_edits_back_and_again");
        {
            auto p = prepared();
            p->editBank([](SequencerBank& b) { b.pattern[2].lanes[3].a[7].on = true; });
            p->editKit([](KitValues& k) { k.knob[3] = 0.1f; });
            expect(p->undo());
            expectEquals(p->project().kit.knob[3], knobDefault(3));
            expect(p->project().currentBank().pattern[2].lanes[3].a[7].on);
            expect(p->undo());
            expect(! p->project().currentBank().pattern[2].lanes[3].a[7].on);
            expect(p->redo());
            expect(p->project().currentBank().pattern[2].lanes[3].a[7].on);
        }

        beginTest("the_state_reads_back");
        {
            auto p = prepared();
            p->editBank([](SequencerBank& b) { b.pattern[9].lanes[8].a[2].on = true; });
            p->editPlay([](PlaySettings& s) { s.tempo = 87.0; });
            juce::MemoryBlock state;
            p->getStateInformation(state);
            auto q = prepared();
            q->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            expect(q->project().currentBank().pattern[9].lanes[8].a[2].on);
            expectEquals(q->project().play.tempo, 87.0);
            expect(! q->canUndo(), "a restored state starts a fresh history");
        }

        // ---- The review of D267 ----------------------------------------------------------------------------------

        beginTest("a_gesture_that_changes_nothing_leaves_no_undo_step");
        {
            auto p = prepared();
            p->editPlay([](PlaySettings& s) { s.tempo = 90.0; });
            expect(p->undo());
            expect(p->canRedo());
            p->beginGesture(); // a knob clicked and let go
            p->endGesture();
            expect(p->canRedo(), "redo is still there");
            p->beginGesture(); // a knob dragged: one step however many edits
            for (int i = 0; i < 5; ++i)
                p->editPlay([i](PlaySettings& s) { s.tempo = 100.0 + i; });
            p->endGesture();
            expect(p->undo());
            expect(! p->canUndo());
        }

        beginTest("a_recording_pass_is_one_undo_step_however_many_batches");
        {
            auto p = prepared();
            p->setRecording(true);
            p->setPlaying(true);
            for (int beatIndex = 0; beatIndex < 4; ++beatIndex)
            {
                const std::int64_t at = beatIndex * 24000 + 480;
                (void) render(*p, 24000, [&](juce::MidiBuffer& midi, std::int64_t pos, int n)
                {
                    if (at - beatIndex * 24000 >= pos && at - beatIndex * 24000 < pos + n)
                        midi.addEvent(juce::MidiMessage::noteOn(10, 38, static_cast<juce::uint8>(100)), static_cast<int>(at - beatIndex * 24000 - pos));
                });
                expect(p->applyRecordedHits(), "a batch each beat");
            }
            expect(p->undo());
            expect(! p->canUndo(), "one step for the pass");
        }

        beginTest("an_opened_project_starts_its_own_history");
        {
            const auto file = juce::File::createTempFile(".cs808");
            auto p = prepared();
            p->editPlay([](PlaySettings& s) { s.tempo = 133.0; });
            expect(p->saveTo(file));
            p->editPlay([](PlaySettings& s) { s.tempo = 70.0; });
            expect(p->loadFrom(file).wasOk());
            expect(! p->canUndo(), "no undo back into the project before");
            expectEquals(p->project().play.tempo, 133.0);
            (void) file.deleteFile();
        }

        beginTest("a_file_asking_for_endless_banks_is_cut");
        {
            juce::String json = "{\"format\":\"cs808\",\"version\":1,\"banks\":[";
            for (int i = 0; i < 1000; ++i)
                json << (i > 0 ? "," : "") << "{}";
            json << "]}";
            Project project;
            if (projectFromJson(json, project).wasOk())
                expectLessOrEqual(static_cast<int>(project.banks.size()), Project::maxBanks);
        }

        beginTest("a_stopped_clock_holds_its_place_and_continue_follows_the_pulses");
        {
            auto p = prepared();
            const double interval = kRate * 60.0 / 120.0 / 24.0; // 120 BPM: 1000 samples a pulse
            std::int64_t pulses = 0;
            bool stopSent = false, continueSent = false;
            // A beat playing, a beat stopped with the clock still running (as MPCs and Elektrons send it), then
            // continue: the follower picks up where the pulses say, not half a beat ahead.
            const auto run = render(*p, 3 * 24000, [&](juce::MidiBuffer& midi, std::int64_t pos, int n)
            {
                if (pos == 0)
                    midi.addEvent(juce::MidiMessage::midiStart(), 0);
                if (pos >= 24000 && ! stopSent)
                {
                    midi.addEvent(juce::MidiMessage::midiStop(), 0);
                    stopSent = true;
                }
                if (pos >= 48000 && ! continueSent)
                {
                    midi.addEvent(juce::MidiMessage::midiContinue(), 0);
                    continueSent = true;
                }
                for (;; ++pulses)
                {
                    const auto at = static_cast<std::int64_t>(std::llround(static_cast<double>(pulses) * interval));
                    if (at >= pos + n)
                        break;
                    if (at >= pos)
                        midi.addEvent(juce::MidiMessage::midiClock(), static_cast<int>(at - pos));
                }
            });
            expect(p->isPlaying());
            // 24 pulses played before the stop: after a beat more of playing it is near beat 2.
            const auto sixteenth = p->playingSixteenth();
            expect(sixteenth >= 6 && sixteenth <= 8, "at about beat 2 (sixteenth " + juce::String(sixteenth) + ")");
        }
    }
};

static ProcessorTests processorTests;
