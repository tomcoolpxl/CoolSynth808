#include <juce_audio_processors/juce_audio_processors.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "Editor.h"
#include "Processor.h"
#include "Project.h"
#include "sync/BeatSync.h"
#include "sync/SyncClock.h"
#include "ui/Transport.h"

/*
    The beat sync in the 808 (CoolSynth's D361): its two switches kept in the settings, the SYNC key's word and click,
    the processor following the sync through its play head, alone and with another peer over the loopback (real
    sockets: COOLSYNTH_SKIP_NETWORK_TESTS=1 skips those), incoming MIDI clock ignored while others are in the session
    and followed while alone. At 48 kHz and 120 BPM a step is 6000 samples; a block here is 480 samples, 10 ms.
*/
using namespace cs808;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 480;
    constexpr std::int64_t kBlockMicros = 10000;
    constexpr double kMicrosPerSample = 1.0e6 / kRate;

    bool networkTestsSkipped()
    {
        return juce::SystemStats::getEnvironmentVariable("COOLSYNTH_SKIP_NETWORK_TESTS", {}).getIntValue() != 0;
    }

    double waitFor(double limitSeconds, const std::function<bool()>& condition)
    {
        const auto start = std::chrono::steady_clock::now();
        for (;;)
        {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (condition())
                return t;
            if (t > limitSeconds)
                return -1.0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    /** A processor with a beat sync of its own and the rim shot on all sixteen steps of pattern 1, at the lightest
        accuracy so it renders well ahead of real time. */
    std::unique_ptr<Processor> syncedProcessor(bool network)
    {
        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails(0, 2, kRate, kBlock);
        p->setAccuracy(2);
        p->prepareToPlay(kRate, kBlock);
        Project empty;
        p->replaceProject(empty, false);
        p->editBank([](SequencerBank& b)
        {
            for (auto& step : b.pattern[0].lanes[5].a)
                step.on = true;
        }, false);
        p->useBeatSyncForTesting(network);
        return p;
    }

    /** Drives the processor block after block on a clock of its own (block i heard at origin + i * 10 ms), as a device
        would, and a peer's audio side at the same moments; keeps every sequencer hit and the MIDI that came out. */
    struct Rig
    {
        Processor& p;
        coolsynth::sync::BeatSync* peer = nullptr;
        std::int64_t origin = 0, blocks = 0, now = 0;
        std::vector<std::int64_t> hits;
        std::vector<coolsynth::sync::BlockBeats> peerBlocks;
        std::vector<std::pair<std::int64_t, juce::MidiMessage>> midiOut;
        std::function<void(juce::MidiBuffer&, std::int64_t, int)> in;
        juce::AudioBuffer<float> buffer { 2, kBlock };

        Rig(Processor& processor, std::int64_t start) : p(processor), origin(start)
        {
            p.setHostClockForTesting([this] { return now; });
            p.logSequencerHitsForTesting(&hits);
        }

        ~Rig()
        {
            p.setHostClockForTesting({});
            p.logSequencerHitsForTesting(nullptr);
        }

        void step()
        {
            now = origin + blocks * kBlockMicros;
            juce::MidiBuffer midi;
            if (in)
                in(midi, blocks * kBlock, kBlock);
            buffer.clear();
            p.processBlock(buffer, midi);
            for (const auto m : midi)
                midiOut.emplace_back(blocks * kBlock + m.samplePosition, m.getMessage());
            peerBlocks.push_back(peer != nullptr ? peer->audio().beginBlock(now, kBlock, kMicrosPerSample) : coolsynth::sync::BlockBeats {});
            ++blocks;
        }

        void run(int count)
        {
            for (int i = 0; i < count; ++i)
                step();
        }

        /** The blocks due by now on the real clock (the network's tests). */
        void catchUp()
        {
            const std::int64_t due = (coolsynth::sync::hostMicrosNow() - origin) / kBlockMicros;
            while (blocks < due)
                step();
        }

        double until(double limitSeconds, const std::function<bool()>& condition)
        {
            return waitFor(limitSeconds, [&] { catchUp(); return condition(); });
        }

        void runFor(double seconds) { until(seconds, [] { return false; }); }

        /** How far a sample is from the peer's grid of sixteenths, in samples. */
        double offGrid(std::int64_t sample) const
        {
            const auto& b = peerBlocks[static_cast<std::size_t>(sample / kBlock)];
            const double beat = b.beat + static_cast<double>(sample % kBlock) * b.beatsPerSample;
            return (beat - std::round(beat * 4.0) / 4.0) / b.beatsPerSample;
        }

        double peerBeatAt(std::int64_t sample) const
        {
            const auto& b = peerBlocks[static_cast<std::size_t>(sample / kBlock)];
            return b.beat + static_cast<double>(sample % kBlock) * b.beatsPerSample;
        }
    };

    /** A MIDI clock coming in at this tempo, a start first. */
    std::function<void(juce::MidiBuffer&, std::int64_t, int)> incomingClock(double bpm)
    {
        auto next = std::make_shared<double>(-1.0);
        const double interval = kRate * 60.0 / bpm / 24.0;
        return [next, interval](juce::MidiBuffer& midi, std::int64_t pos, int n)
        {
            if (*next < 0.0)
            {
                midi.addEvent(juce::MidiMessage::midiStart(), 0);
                *next = static_cast<double>(pos);
            }
            for (; *next < static_cast<double>(pos + n); *next += interval)
                midi.addEvent(juce::MidiMessage::midiClock(), juce::jlimit(0, n - 1, static_cast<int>(std::llround(*next)) - static_cast<int>(pos)));
        };
    }

    /** A key's click, as its mouse-up runs it (off screen a key contains no point, so its own check would fail). */
    void click(Chip& c)
    {
        const auto centre = c.getLocalBounds().getCentre().toFloat();
        const auto when = juce::Time::getCurrentTime();
        const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), centre, juce::ModifierKeys(),
                                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                 juce::MouseInputSource::defaultTiltY, &c, &c, when, centre, when, 1, false);
        if (c.onClick && ! c.getBounds().isEmpty())
            c.onClick(e);
    }
}

class BeatSyncChoiceTests final : public juce::UnitTest
{
public:
    BeatSyncChoiceTests() : juce::UnitTest("Beat sync choice and key", "CoolSynth808") {}

    void runTest() override
    {
        beginTest("on_and_shared_until_switched_off_and_kept");
        {
            juce::PropertySet none;
            const auto first = syncChoiceFrom(none);
            expect(first.on && first.sharing, "both on by default");
            keepSyncChoice(none, { false, false });
            const auto kept = syncChoiceFrom(none);
            expect(! kept.on && ! kept.sharing, "both off, read back");
        }

        beginTest("the_sync_keys_word_counts_the_other_peers");
        {
            expectEquals(syncKeyText(0), juce::String("Sync"));
            expectEquals(syncKeyText(1), juce::String("Sync 1"));
            expectEquals(syncKeyText(2), juce::String("Sync 2"));
        }

        beginTest("the_sync_key_switches_it_and_the_settings_keep_it");
        {
            const auto file = juce::File::createTempFile(".settings");
            Editor::useSettingsFileForTesting(file);
            {
                auto p = std::make_unique<Processor>();
                p->useBeatSyncForTesting(false);
                std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
                auto* editor = dynamic_cast<Editor*>(e.get());
                auto* key = editor->transportForTesting().syncKeyForTesting();
                expect(key != nullptr, "the standalone's strip has a SYNC key");
                if (key != nullptr)
                {
                    expect(key->isOn() && key->textOf() == "Sync", "lit, alone: SYNC");
                    expect(key->getTooltip().isNotEmpty(), "with a hint on what it does");
                    const auto read = [&]
                    {
                        juce::PropertiesFile settings(file, {});
                        return syncChoiceFrom(settings);
                    };
                    click(*key);
                    expect(! p->beatSyncOn() && ! key->isOn(), "a click switches it off");
                    editor->frameForTesting();
                    expect(! read().on, "kept off in the settings");
                    click(*key);
                    expect(p->beatSyncOn(), "and on again");
                    p->setStartStopSharing(false);
                    editor->frameForTesting();
                    expect(read().on && ! read().sharing, "on, Play and Stop not shared, kept");
                }
            }
            {
                auto plain = std::make_unique<Processor>();
                std::unique_ptr<juce::AudioProcessorEditor> e(plain->createEditor());
                expect(dynamic_cast<Editor*>(e.get())->transportForTesting().syncKeyForTesting() == nullptr, "no SYNC key without a beat sync");
            }
            Editor::useSettingsFileForTesting({});
            file.deleteFile();
        }
    }
};

class BeatSyncAloneTests final : public juce::UnitTest
{
public:
    BeatSyncAloneTests() : juce::UnitTest("Beat sync alone", "CoolSynth808") {}

    void runTest() override
    {
        beginTest("alone_play_starts_at_once_and_the_steps_and_clock_keep_the_grid");
        {
            auto p = syncedProcessor(false);
            auto rig = std::make_unique<Rig>(*p, 5'000'000'000);
            auto& r = *rig;
            r.run(10);
            expect(! p->isPlaying() && r.hits.empty(), "stopped, nothing plays");
            p->setPlaying(true);
            const std::int64_t pressed = r.blocks * kBlock;
            r.run(1);
            expect(p->isPlaying() && ! p->countingIn() && ! p->followingSync(), "alone, Play plays at once");
            r.run(200); // two seconds: 16 steps
            expectGreaterOrEqual(static_cast<int>(r.hits.size()), 16);
            if (! r.hits.empty())
            {
                expectEquals(r.hits.front(), pressed, "the first step at the press");
                std::int64_t worst = 0;
                for (std::size_t k = 0; k < r.hits.size(); ++k)
                    worst = std::max<std::int64_t>(worst, std::abs(r.hits[k] - (pressed + static_cast<std::int64_t>(k) * 6000)));
                expectLessOrEqual(static_cast<int>(worst), 1, "every step on the grid, within a sample");
            }
            int clocks = 0, worstClock = 0;
            for (const auto& [at, m] : r.midiOut)
                if (m.isMidiClock())
                {
                    ++clocks;
                    const auto from = at - pressed;
                    worstClock = std::max(worstClock, static_cast<int>(std::abs(from - std::llround(static_cast<double>(from) / 1000.0) * 1000)));
                }
            expectGreaterOrEqual(clocks, 95, "its MIDI clock out goes on while the sync leads");
            expectLessOrEqual(worstClock, 1, "24 a beat, on the grid");

            // the panel's tempo goes to the sync
            p->editPlay([](PlaySettings& s) { s.tempo = 125.0; }, false);
            r.run(2);
            expectWithinAbsoluteError(p->tempoNow(), 125.0, 1.0e-6, "the panel's tempo is the sync's");
            const auto before = r.hits.size();
            r.run(100);
            if (r.hits.size() > before + 2)
                expectWithinAbsoluteError(static_cast<double>(r.hits.back() - r.hits[r.hits.size() - 2]), 5760.0, 1.0, "and plays at it");

            p->setPlaying(false);
            r.run(2);
            expect(! p->isPlaying(), "Stop through the sync");
        }

        beginTest("alone_an_incoming_midi_clock_leads_as_before");
        {
            auto p = syncedProcessor(false);
            auto rig = std::make_unique<Rig>(*p, 5'000'000'000);
            auto& r = *rig;
            r.in = incomingClock(100.0);
            r.run(100);
            expect(p->followingClock() && ! p->followingSync(), "the clock leads while the sync is alone");
            expect(p->isPlaying(), "its start started it");
            expectWithinAbsoluteError(p->tempoNow(), 100.0, 1.5);
        }
    }
};

class BeatSyncNetworkProcessorTests final : public juce::UnitTest
{
public:
    BeatSyncNetworkProcessorTests() : juce::UnitTest("Beat sync network with a peer", "CoolSynth808") {}

    void runTest() override
    {
        beginTest("the_808_plays_with_another_peer_on_the_loopback");
        if (networkTestsSkipped())
        {
            logMessage("skipped: COOLSYNTH_SKIP_NETWORK_TESTS");
            return;
        }
        auto p = syncedProcessor(true);
        coolsynth::sync::BeatSync::Options options;
        options.loopbackOnly = true;
        auto peer = std::make_unique<coolsynth::sync::BeatSync>(97.0, options);
        auto rig = std::make_unique<Rig>(*p, coolsynth::sync::hostMicrosNow());
        auto& r = *rig;
        r.peer = peer.get();
        r.runFor(1.0); // the 808's session is the older
        peer->setEnabled(true);
        const double met = r.until(3.0, [&] { return p->syncPeers() == 1 && peer->peerCount() == 1; });
        if (met < 0.0 && peer->status().gateways == 0)
        {
            logMessage("skipped: no loopback gateway could open (no multicast on the loopback interface?)");
            return;
        }
        logMessage("met in " + juce::String(met * 1000.0, 1) + " ms");
        expect(met >= 0.0, "the two meet");
        r.runFor(0.1);
        expectWithinAbsoluteError(r.peerBlocks.back().bpm, 120.0, 1.0e-6, "the peer takes the 808's tempo");
        expect(p->followingSync(), "with a peer the sync leads");

        // An incoming clock is not followed while the sync has a peer.
        r.in = incomingClock(100.0);
        r.runFor(0.5);
        expect(! p->followingClock() && p->followingSync() && ! p->isPlaying(), "a MIDI clock is ignored, its start too");
        r.in = {};
        r.runFor(0.6);

        // The peer plays: the 808 counts in and starts on the session's bar line, its steps on the peer's grid.
        r.catchUp();
        r.hits.clear();
        peer->audio().play(0);
        const double countIn = r.until(1.0, [&] { return p->countingIn() || p->isPlaying(); });
        expect(countIn >= 0.0, "the peer's Play reaches the 808");
        const double started = r.until(3.0, [&] { return p->isPlaying(); });
        expect(started >= 0.0, "it starts on the next bar line");
        r.runFor(1.5);
        if (! r.hits.empty())
        {
            const auto& b = r.peerBlocks[static_cast<std::size_t>(r.hits.front() / kBlock)];
            const double beat = r.peerBeatAt(r.hits.front());
            const double offBar = (beat - 4.0 * std::round(beat / 4.0)) / b.beatsPerSample;
            logMessage("the 808's first step: " + juce::String(offBar, 3) + " samples off the peer's bar line, its beat " + juce::String(beat, 4));
            expectLessOrEqual(std::abs(offBar), 1.5, "its first step on the peer's bar line");
        }
        expect(r.peerBlocks.back().playing, "the peer plays");
        expectGreaterOrEqual(static_cast<int>(r.hits.size()), 10);
        double worst = 0.0;
        for (const auto h : r.hits)
            worst = std::max(worst, std::abs(r.offGrid(h)));
        logMessage("steps off the peer's grid by " + juce::String(worst, 3) + " samples at most; behind real time by "
                   + juce::String(static_cast<double>(coolsynth::sync::hostMicrosNow() - r.now) / 1000.0, 1) + " ms");
        expectLessOrEqual(worst, 1.5, "every step on the peer's grid, within a sample or so");

        // The peer's tempo: the 808 plays at it, and its panel takes it.
        r.catchUp();
        peer->audio().setTempo(133.5);
        const double took = r.until(1.0, [&] { return coolsynth::sync::tempoFromBpm(p->tempoNow()) == coolsynth::sync::tempoFromBpm(133.5); });
        expect(took >= 0.0 && took < 0.2, "the peer's tempo reaches the 808");
        p->syncTick();
        expectWithinAbsoluteError(p->project().play.tempo, 133.5, 1.0e-3, "and becomes the panel's");
        r.hits.clear();
        r.runFor(1.0);
        worst = 0.0;
        for (const auto h : r.hits)
            worst = std::max(worst, std::abs(r.offGrid(h)));
        expectLessOrEqual(worst, 1.5, "on the grid at the new tempo");

        // Stop and Play both ways, shared.
        p->setPlaying(false);
        const double stopped = r.until(1.0, [&] { return ! p->isPlaying() && ! r.peerBlocks.back().playing; });
        expect(stopped >= 0.0 && stopped < 0.2, "the 808's Stop stops the peer");
        r.hits.clear();
        p->setPlaying(true);
        const double bothPlay = r.until(3.0, [&] { return p->isPlaying() && r.peerBlocks.back().playing; });
        expect(bothPlay >= 0.0, "the 808's Play starts the peer");
        r.runFor(0.3);
        expect(! r.hits.empty(), "and plays");
        if (! r.hits.empty())
        {
            const auto& b = r.peerBlocks[static_cast<std::size_t>(r.hits.front() / kBlock)];
            const double beat = r.peerBeatAt(r.hits.front());
            const double offBar = (beat - 4.0 * std::round(beat / 4.0)) / b.beatsPerSample;
            logMessage("the 808's first step after its Play: " + juce::String(offBar, 3) + " samples off the peer's bar line");
            expectLessOrEqual(std::abs(offBar), 1.5, "its first step on the peer's bar line");
        }
        r.catchUp();
        peer->audio().stop(0);
        const double peerStopped = r.until(1.0, [&] { return ! p->isPlaying(); });
        expect(peerStopped >= 0.0 && peerStopped < 0.2, "the peer's Stop stops the 808");

        // The 808's tempo to the peer.
        p->editPlay([](PlaySettings& s) { s.tempo = 110.0; }, false);
        const double sent = r.until(1.0, [&] { return coolsynth::sync::tempoFromBpm(r.peerBlocks.back().bpm) == coolsynth::sync::tempoFromBpm(110.0); });
        expect(sent >= 0.0 && sent < 0.2, "the panel's tempo reaches the peer");

        // The peer leaves: alone, an incoming clock leads again.
        peer->setEnabled(false);
        const double alone = r.until(1.0, [&] { return p->syncPeers() == 0; });
        expect(alone >= 0.0, "alone again");
        r.in = incomingClock(100.0);
        r.runFor(0.5);
        expect(p->followingClock() && ! p->followingSync(), "alone, the MIDI clock is followed");
        r.in = {};
        r.peer = nullptr;
        rig.reset();
        p.reset();

        beginTest("joining_an_older_session_the_808_takes_its_tempo");
        {
            // The peer's session is the older; the 808's audio starts only once they have met (a device opened late):
            // its panel's tempo must not win over the session's.
            auto older = std::make_unique<coolsynth::sync::BeatSync>(97.0, options);
            older->setEnabled(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            auto q = syncedProcessor(true);
            const double joined = waitFor(3.0, [&] { return q->syncPeers() == 1; });
            expect(joined >= 0.0, "the 808 joins the older session");
            auto lateRig = std::make_unique<Rig>(*q, coolsynth::sync::hostMicrosNow());
            lateRig->peer = older.get();
            lateRig->runFor(0.5);
            q->syncTick();
            expectWithinAbsoluteError(lateRig->peerBlocks.back().bpm, 97.0, 1.0e-3, "the session keeps its tempo");
            expectWithinAbsoluteError(q->tempoNow(), 97.0, 1.0e-3, "the 808 plays at it");
            expectWithinAbsoluteError(q->project().play.tempo, 97.0, 1.0e-3, "and its panel takes it");
        }
    }
};

static BeatSyncChoiceTests beatSyncChoiceTests;
static BeatSyncAloneTests beatSyncAloneTests;
static BeatSyncNetworkProcessorTests beatSyncNetworkProcessorTests;
