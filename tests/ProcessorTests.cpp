#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "Processor.h"
#include "Project.h"
#include "ui/NodeSwing.h"

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

        beginTest("each_sheet_lives_from_the_engine_as_its_recording_does");
        {
            // D317: each sheet of Under the Lid shows the engine's circuit as it plays. Struck at its recording's own
            // settings, each captured series follows its ngspice recording: sample by sample where the circuit is
            // deterministic, or in its level over time where noise or the free-running metal oscillators drive it
            // (ngspice stood two sines in for the noise, and the oscillators start at their own phases). A part mapped
            // to the wrong port, or its sign turned round, follows neither.
            using W = coolsynth::synth::v3::CircuitWatch;
            struct Case { const char* name; int lane, light; const char* file; float ms; std::function<void(KitValues&)> kit; int secondLane; bool squares = false; };
            const std::vector<Case> cases {
                { "bass drum", 0, W::bassDrum, "bassdrum_currents.json", 300.0f, [](KitValues& k) { k.knob[1] = 0.8f; k.knob[2] = 0.5f; k.knob[3] = 0.6f; }, -1 },
                { "snare", 1, W::snare, "snare_currents.json", 300.0f, [](KitValues& k) { k.knob[4] = 0.8f; k.knob[5] = 0.5f; k.knob[6] = 0.6f; }, -1 },
                { "low tom", 2, W::lowTom, "tom_currents.json", 600.0f, [](KitValues& k) { k.knob[7] = 0.8f; k.knob[8] = 0.5f; }, -1 },
                { "rim shot", 5, W::rimShot, "rimshot_currents.json", 150.0f, [](KitValues& k) { k.knob[13] = 0.8f; }, -1 },
                { "hand clap", 6, W::clap, "clap_currents.json", 300.0f, [](KitValues& k) { k.knob[14] = 0.8f; }, -1 },
                { "cowbell", 7, W::cowbell, "cowbell_currents.json", 250.0f, [](KitValues& k) { k.knob[15] = 0.8f; }, -1 },
                { "cymbal", 8, W::cymbal, "cymbal_currents.json", 600.0f, [](KitValues& k) { k.knob[16] = 0.8f; k.knob[17] = 0.5f; k.knob[18] = 0.5f; }, -1 },
                { "hi-hats", 9, W::openHat, "hats_currents.json", 400.0f, [](KitValues& k) { k.knob[19] = 0.8f; k.knob[20] = 0.5f; k.knob[21] = 0.8f; }, 10 },
                // the six oscillators, free-running from their own phases: their size and their pitch
                { "metal oscillators", 7, W::metalBankSheet, "metal_currents.json", 30.0f, [](KitValues&) {}, -1, true },
            };
            for (const auto& c : cases)
            {
                auto p = prepared();
                p->editKit(c.kit);
                // 100 beats a minute: the second step is 150 ms on, the closed hat's place in the hats' recording
                p->editPlay([](PlaySettings& s) { s.tempo = 100.0; });
                p->editBank([&c](SequencerBank& b)
                {
                    b.pattern[0].lanes[static_cast<std::size_t>(c.lane)].a[0].on = true;
                    if (c.secondLane >= 0)
                        b.pattern[0].lanes[static_cast<std::size_t>(c.secondLane)].a[1].on = true;
                }, false);
                p->circuitWatch().watchCircuit(c.light);
                p->setPlaying(true);
                (void) render(*p, static_cast<std::int64_t>(48.0 * (c.ms + 100.0f)));
                auto& watch = p->circuitWatch();
                const int frames = watch.framesCaptured();
                if (frames < 100)
                {
                    expect(false, juce::String(c.name) + ": no capture (" + juce::String(frames) + " frames)");
                    continue;
                }

                const auto json = juce::JSON::parse(juce::File(COOLSYNTH808_SOURCE_DIR).getChildFile("resources/lid").getChildFile(c.file));
                std::vector<double> ms;
                for (const auto& v : *json["t"].getArray())
                    ms.push_back(static_cast<double>(v));
                juce::MemoryOutputStream raw;
                juce::Base64::convertFromBase64(raw, json["data"].toString());
                const auto* data = static_cast<const std::int16_t*>(raw.getData());
                const auto names = *json["names"].getArray();
                const auto scales = *json["scales"].getArray();
                const auto recorded = [&](int s, double at)
                {
                    std::size_t k = 0;
                    while (k + 2 < ms.size() && ms[k + 1] < at)
                        ++k;
                    const double scale = static_cast<double>(scales[s]);
                    const double a = data[static_cast<std::size_t>(s) * ms.size() + k] * scale, b = data[static_cast<std::size_t>(s) * ms.size() + k + 1] * scale;
                    const double fr = ms[k + 1] > ms[k] ? juce::jlimit(0.0, 1.0, (at - ms[k]) / (ms[k + 1] - ms[k])) : 0.0;
                    return a + fr * (b - a);
                };
                const auto correlation = [](const std::vector<double>& x, const std::vector<double>& y)
                {
                    double mx = 0.0, my = 0.0;
                    for (std::size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; }
                    mx /= static_cast<double>(x.size());
                    my /= static_cast<double>(y.size());
                    double sxy = 0.0, sxx = 0.0, syy = 0.0;
                    for (std::size_t i = 0; i < x.size(); ++i)
                    {
                        sxy += (x[i] - mx) * (y[i] - my);
                        sxx += (x[i] - mx) * (x[i] - mx);
                        syy += (y[i] - my) * (y[i] - my);
                    }
                    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt(sxx * syy) : 0.0;
                };
                // a series' level over time: its spread (standard deviation) in each 10 ms of the hit
                const auto levels = [](const std::vector<double>& v, const std::vector<double>& at)
                {
                    std::vector<double> out;
                    std::size_t i = 0;
                    for (double from = 0.0; i < v.size(); from += 10.0)
                    {
                        double sum = 0.0, squares = 0.0;
                        int n = 0;
                        for (; i < v.size() && at[i] < from + 10.0; ++i, ++n)
                        {
                            sum += v[i];
                            squares += v[i] * v[i];
                        }
                        out.push_back(n > 0 ? std::sqrt(std::max(0.0, squares / n - (sum / n) * (sum / n))) : 0.0);
                    }
                    return out;
                };
                const auto spread = [](const std::vector<double>& v)
                {
                    double sum = 0.0, squares = 0.0;
                    for (const double a : v) { sum += a; squares += a * a; }
                    const double n = static_cast<double>(v.size());
                    return std::sqrt(std::max(0.0, squares / n - (sum / n) * (sum / n)));
                };
                int checked = 0, good = 0, turned = 0;
                juce::String poor, turnedNames;
                for (int s = 0; s < watch.capturedSeries(); ++s)
                {
                    const juce::String name(watch.capturedName(s));
                    const int r = names.indexOf(name);
                    if (r < 0)
                    {
                        expect(false, juce::String(c.name) + ": " + name + " is not in the recording");
                        continue;
                    }
                    std::vector<double> x, y, at;
                    for (int f = 0; f < frames; ++f)
                    {
                        const double e = watch.captured(f, s), v = recorded(r, watch.capturedTime(f));
                        if (std::isfinite(e) && std::isfinite(v))
                        {
                            x.push_back(e);
                            y.push_back(v);
                            at.push_back(watch.capturedTime(f));
                        }
                    }
                    if (x.size() < 50)
                        continue;
                    double lo = 1.0e30, hi = -1.0e30;
                    for (const double v : y) { lo = std::min(lo, v); hi = std::max(hi, v); }
                    if (hi - lo < (name.contains(".") ? 1.0e-6 : 5.0e-3))
                        continue; // still in the recording (a node within 5 mV: a virtual ground): nothing to follow
                    const double direct = correlation(x, y), level = correlation(levels(x, at), levels(y, at));
                    const double size = spread(y) > 0.0 ? spread(x) / spread(y) : 0.0;
                    ++checked;
                    if (direct < -0.5 && ! c.squares)
                    {
                        ++turned;
                        turnedNames << name << " " << juce::String(direct, 2) << "  ";
                    }
                    // a square's pitch (no1 to no6, the six oscillators): how often it crosses its middle; the mixes after them
                    // cross it raggedly, so they are judged on their size
                    const auto crossings = [](const std::vector<double>& v)
                    {
                        double sum = 0.0;
                        for (const double a : v) sum += a;
                        const double middle = sum / static_cast<double>(v.size());
                        int n = 0;
                        for (std::size_t i = 1; i < v.size(); ++i)
                            n += (v[i - 1] < middle) != (v[i] < middle) ? 1 : 0;
                        return n;
                    };
                    const double pitch = crossings(y) > 0 ? static_cast<double>(crossings(x)) / crossings(y) : 0.0;
                    if (c.squares ? size > 0.67 && size < 1.5 && (! name.startsWith("no") || (pitch > 0.9 && pitch < 1.1)) : direct > 0.6 || level > 0.6)
                        ++good;
                    else
                        poor << name << " " << juce::String(direct, 2) << "/" << juce::String(level, 2) << "/x" << juce::String(size, 2) << "/f" << juce::String(pitch, 2) << "  ";
                }
                logMessage(juce::String(c.name) + ": " + juce::String(frames) + " frames; series that move, " + juce::String(checked)
                           + "; following their recording, " + juce::String(good) + "; turned round, " + juce::String(turned) + " " + turnedNames + "; the rest: " + poor);
                expectGreaterThan(checked, 20, juce::String(c.name) + ": most of the circuit moves");
                expectGreaterThan(good, checked * 4 / 5, juce::String(c.name) + ": four in five follow their recording");
                expectEquals(turned, 0, juce::String(c.name) + ": none turned round (a sign or a port wrong)");
            }
        }

        beginTest("the_board_follows_the_engine_as_its_recordings_do");
        {
            // D320: with the lid lifted, every drum's board nodes are fed from the engine as it plays. Each drum struck
            // alone: each node's largest swing from rest goes the way its recording's does, and comes near its size.
            const auto board = cs808::makeLiveBoard();
            int nodes = 0, sameWay = 0, nearSize = 0;
            for (int lane = 0; lane < 11; ++lane)
            {
                auto p = prepared();
                p->editKit([](KitValues& k)
                {
                    k.knob[1] = 0.8f; k.knob[2] = 0.5f; k.knob[3] = 0.6f;   // the bass drum as recorded
                    k.knob[4] = 0.8f; k.knob[5] = 0.5f; k.knob[6] = 0.6f;   // the snare
                    k.knob[13] = 0.8f; k.knob[14] = 0.8f; k.knob[15] = 0.8f; // rim shot, clap, cowbell
                    k.knob[16] = 0.8f; k.knob[17] = 0.5f; k.knob[18] = 0.5f; // the cymbal
                    k.knob[19] = 0.8f; k.knob[20] = 0.5f; k.knob[21] = 0.8f; // the hats
                });
                p->editBank([lane](SequencerBank& b) { b.pattern[0].lanes[static_cast<std::size_t>(lane)].a[0].on = true; }, false);
                {
                    std::array<std::array<float, 9>, 11> rests {};
                    for (std::size_t l = 0; l < rests.size(); ++l)
                        for (std::size_t k = 0; k < 9; ++k)
                            rests[l][k] = board.nodes[l][k].recordedRest;
                    p->setBoardRests(rests);
                }
                p->circuitWatch().watchBoard(true);
                p->setPlaying(true);
                std::array<float, 9> extreme {};
                const auto take = [&]
                {
                    std::array<unsigned int, 11> hits {};
                    std::array<std::array<float, 9>, 11> swings {};
                    p->takeBoard(hits, swings);
                    for (std::size_t k = 0; k < 9; ++k)
                        if (std::abs(swings[static_cast<std::size_t>(lane)][k]) > std::abs(extreme[k]))
                            extreme[k] = swings[static_cast<std::size_t>(lane)][k];
                };
                (void) render(*p, 48000, [&](juce::MidiBuffer&, std::int64_t, int) { take(); });
                take();
                juce::String line = "lane " + juce::String(lane) + ":";
                for (std::size_t k = 0; k < 9; ++k)
                {
                    const auto& n = board.nodes[static_cast<std::size_t>(lane)][k];
                    if (n.name.empty() || n.recordedPeak < 1.0e-3f)
                        continue;
                    ++nodes;
                    const float ratio = std::abs(extreme[k]) / n.recordedPeak;
                    const bool way = (extreme[k] >= 0.0f ? 1.0f : -1.0f) == n.recordedSign;
                    sameWay += way ? 1 : 0;
                    nearSize += ratio > 1.0f / 3.0f && ratio < 3.0f ? 1 : 0;
                    line << "  " << juce::String(n.name) << " x" << juce::String(ratio, 2) << (way ? "" : " (turned)");
                }
                logMessage(line);
            }
            logMessage("board nodes " + juce::String(nodes) + ": the same way " + juce::String(sameWay) + ", near their size " + juce::String(nearSize));
            expectGreaterThan(nodes, 50, "the drums' board nodes");
            expectGreaterThan(sameWay, nodes * 9 / 10, "nine in ten swing the way their recording does (a ringing output's largest swing may go either way)");
            expectGreaterThan(nearSize, nodes * 9 / 10, "nine in ten come within three times of their recording's size");

            // What the feed costs: a bar of every drum, the board shown and not.
            const auto timed = [&](bool shown)
            {
                auto p = prepared();
                p->editBank([](SequencerBank& b)
                {
                    for (int lane = 0; lane < 11; ++lane)
                        for (int step = 0; step < 16; step += 2)
                            b.pattern[0].lanes[static_cast<std::size_t>(lane)].a[static_cast<std::size_t>(step)].on = true;
                }, false);
                p->circuitWatch().watchBoard(shown);
                p->setPlaying(true);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                (void) render(*p, 96000);
                return juce::Time::getMillisecondCounterHiRes() - start;
            };
            const double off = timed(false), on = timed(true);
            logMessage("two seconds of every drum: " + juce::String(off, 0) + " ms without the board, " + juce::String(on, 0) + " ms with it");
        }

        beginTest("in_a_daw_the_pattern_follows_the_host_unless_switched_off");
        {
            // D325: under a host's transport the pattern plays while it plays, from where it is, at its tempo; with
            // the switch off it waits for its own Play key, and plays at the host's tempo still.
            struct Head final : juce::AudioPlayHead
            {
                double ppq = 0.0;
                bool playing = true;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setIsPlaying(playing);
                    info.setPpqPosition(ppq);
                    info.setBpm(125.0);
                    return info;
                }
            } head;
            auto p = prepared();
            p->editBank([](SequencerBank& b) { for (int s = 0; s < 16; s += 4) b.pattern[0].lanes[0].a[static_cast<std::size_t>(s)].on = true; }, false);
            p->setPlayHead(&head);
            const auto run = [&](int blocks)
            {
                juce::AudioBuffer<float> buffer(2, kBlock);
                double energy = 0.0;
                for (int b = 0; b < blocks; ++b)
                {
                    juce::MidiBuffer midi;
                    buffer.clear();
                    p->processBlock(buffer, midi);
                    if (head.playing)
                        head.ppq += kBlock / kRate * 125.0 / 60.0;
                    for (int i = 0; i < kBlock; ++i)
                        energy += static_cast<double>(buffer.getSample(0, i)) * buffer.getSample(0, i);
                }
                return energy;
            };
            const int second = static_cast<int>(kRate) / kBlock;
            expect(run(second) > 1.0e-3, "the host plays: the pattern plays with it");
            expect(p->isPlaying() && p->followingHost(), "led by the host");
            expectWithinAbsoluteError(p->tempoNow(), 125.0, 1.0e-6, "at the host's tempo");
            head.playing = false;
            (void) run(second);
            expect(! p->isPlaying(), "the host stops: so does the pattern");

            p->setFollowHostPlay(false);
            head.playing = true;
            (void) run(second);
            expect(! p->isPlaying(), "the switch off: the host's Play does not start the pattern");
            p->setPlaying(true);
            expect(run(second) > 1.0e-3, "its own Play key starts it");
            expect(p->isPlaying() && ! p->followingHost(), "not led by the host");
            expectWithinAbsoluteError(p->tempoNow(), 125.0, 1.0e-6, "at the host's tempo still");
            p->setPlayHead(nullptr);
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
