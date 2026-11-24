// The WAV export (D331): its lengths for each end, the seamless loop's start, the drums' files adding up to the mix
// and keeping what one drum does to another (the closed hat choking the open one), and the files written.

#include <cmath>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "AudioExport.h"
#include "ui/ExportPanel.h"

namespace
{
    using namespace cs808;

    class ExportTests final : public juce::UnitTest
    {
    public:
        ExportTests() : juce::UnitTest("Export", "CoolSynth808") {}

        static ExportJob jobWith(const std::function<void(SequencerPattern&)>& steps)
        {
            ExportJob job;
            job.project.play.tempo = 120.0;
            job.bpm = 120.0;
            job.sampleRate = 48000.0;
            auto& p = job.project.currentBank().pattern[0];
            p = SequencerPattern {};
            steps(p);
            job.patterns = { 0 };
            return job;
        }

        static float peak(const juce::AudioBuffer<float>& a, int from, int to)
        {
            float m = 0.0f;
            for (int i = std::max(0, from); i < std::min(to, a.getNumSamples()); ++i)
                m = std::max(m, std::abs(a.getSample(0, i)));
            return m;
        }

        static double energy(const juce::AudioBuffer<float>& a, int from, int to)
        {
            double e = 0.0;
            for (int i = std::max(0, from); i < std::min(to, a.getNumSamples()); ++i)
                e += static_cast<double>(a.getSample(0, i)) * a.getSample(0, i);
            return e;
        }

        bool render(const ExportJob& job, ExportResult& result)
        {
            std::atomic<float> done { 0.0f };
            const std::atomic<bool> cancel { false };
            const bool ok = renderExport(job, result, done, cancel);
            expect(ok, "rendered");
            return ok;
        }

        void runTest() override
        {
            constexpr int bar = 96000; // 120 bpm at 48 kHz

            beginTest("cut_ends_where_the_next_time_through_would_begin");
            {
                auto job = jobWith([](SequencerPattern& p) { p.lanes[0].a[0].on = true; });
                job.times = 2;
                ExportResult r;
                if (render(job, r))
                {
                    expectEquals(r.mix.getNumSamples(), 2 * bar);
                    expectEquals(r.mix.getNumChannels(), 2);
                    expectGreaterThan(peak(r.mix, 0, 2400), 0.02f, "the first bass drum");
                    expectGreaterThan(peak(r.mix, bar, bar + 2400), 0.02f, "the second");
                    expect(r.stems.empty(), "no drum files unless asked");
                }
            }

            beginTest("ring_out_goes_on_until_it_is_quiet");
            {
                // A cymbal on the last step rings past the end.
                auto job = jobWith([](SequencerPattern& p) { p.lanes[8].a[15].on = true; });
                job.end = ExportEnd::ring;
                ExportResult r;
                if (render(job, r))
                {
                    const int n = r.mix.getNumSamples();
                    expectGreaterThan(n, bar + 4800, "longer than the bar");
                    expectLessThan(n, bar + 20 * 48000 + 1, "within the longest ring");
                    expectGreaterThan(peak(r.mix, bar, bar + 4800), 0.01f, "the cymbal still ringing after the bar");
                    expectLessThan(peak(r.mix, n - 480, n), 1.0e-4f, "quiet at its end");
                }
            }

            beginTest("a_seamless_loop_starts_with_its_end_ringing");
            {
                auto job = jobWith([](SequencerPattern& p) { p.lanes[8].a[15].on = true; });
                ExportResult cut, loop;
                job.end = ExportEnd::cut;
                const bool a = render(job, cut);
                job.end = ExportEnd::loop;
                if (a && render(job, loop))
                {
                    expectEquals(loop.mix.getNumSamples(), bar, "the loop's length exactly");
                    expectLessThan(peak(cut.mix, 0, 4800), 1.0e-5f, "cut: silent before the cymbal");
                    expectGreaterThan(peak(loop.mix, 0, 4800), 0.005f, "the loop: the cymbal's ring-out in its start");
                    // The end, played on into the start, has no jump: the last sample and the first are close.
                    const float last = loop.mix.getSample(0, bar - 1), first = loop.mix.getSample(0, 0);
                    const float step = std::abs(loop.mix.getSample(0, bar - 1) - loop.mix.getSample(0, bar - 2));
                    expectLessThan(std::abs(first - last), 4.0f * step + 0.02f, "seamless where it wraps");
                }
            }

            beginTest("each_drum_has_its_file_and_they_add_up_to_the_mix");
            {
                auto job = jobWith([](SequencerPattern& p)
                {
                    p.lanes[0].a[0].on = true;   // bass drum
                    p.lanes[1].a[4].on = true;   // snare
                    p.lanes[10].a[2].on = true;  // closed hat
                    p.lanes[10].a[10].on = true;
                    p.lanes[5].a[6].on = true;   // a rim shot, muted: not heard, no file
                    p.lanes[5].mute = true;
                });
                job.stems = true;
                job.end = ExportEnd::ring;
                const auto heard = lanesHeard(job);
                expect(heard == std::vector<int> { 0, 1, 10 }, "the lanes heard");
                ExportResult r;
                if (render(job, r))
                {
                    expectEquals(static_cast<int>(r.stems.size()), 3);
                    juce::AudioBuffer<float> sum(2, r.mix.getNumSamples());
                    sum.clear();
                    for (const auto& [lane, audio] : r.stems)
                    {
                        expectEquals(audio.getNumSamples(), r.mix.getNumSamples(), "as long as the mix");
                        for (int c = 0; c < 2; ++c)
                            sum.addFrom(c, 0, audio, c, 0, audio.getNumSamples());
                    }
                    double rest = 0.0;
                    for (int i = 0; i < sum.getNumSamples(); ++i)
                    {
                        const double d = static_cast<double>(sum.getSample(0, i)) - r.mix.getSample(0, i);
                        rest += d * d;
                    }
                    const double mixEnergy = energy(r.mix, 0, r.mix.getNumSamples());
                    // Exactly, but for the float sums' rounding: each drum plays as in the mix, only it heard.
                    expectLessThan(rest, mixEnergy * 1.0e-10, "the drums' files add up to the mix");
                    // Each file holds its own drum only: the snare's is quiet where the bass drum strikes.
                    expectLessThan(peak(r.stems[1].second, 0, 2000), 1.0e-4f, "the snare's file silent at the bass drum");
                }
            }

            beginTest("the_open_hats_file_keeps_the_closed_hats_choke");
            {
                const auto openHat = [this](bool withClosed)
                {
                    auto job = jobWith([withClosed](SequencerPattern& p)
                    {
                        p.lanes[9].a[0].on = true; // open hat
                        if (withClosed)
                            p.lanes[10].a[2].on = true; // closed hat two steps later
                    });
                    job.stems = true;
                    ExportResult r;
                    juce::AudioBuffer<float> open;
                    if (render(job, r))
                        for (auto& [lane, audio] : r.stems)
                            if (lane == 9)
                                open = audio;
                    return open;
                };
                const auto alone = openHat(false), choked = openHat(true);
                const int from = 12000 + 2400, to = 12000 + 24000; // from 50 ms after the closed hat
                if (alone.getNumSamples() > 0 && choked.getNumSamples() > 0)
                    expectLessThan(energy(choked, from, to), energy(alone, from, to) * 0.5, "choked in its own file");
            }

            beginTest("the_files_are_written_beside_each_other");
            {
                auto job = jobWith([](SequencerPattern& p) { p.lanes[0].a[0].on = true; p.lanes[2].a[8].on = true; });
                job.project.kit.alt[2] = true; // the low conga
                job.stems = true;
                ExportResult r;
                const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth808ExportTest");
                (void) folder.deleteRecursively();
                (void) folder.createDirectory();
                const auto mixFile = folder.getChildFile("Beat.wav");
                if (render(job, r))
                {
                    expect(writeWav24(mixFile, r.mix, job.sampleRate));
                    for (const auto& [lane, audio] : r.stems)
                        expect(writeWav24(stemFile(mixFile, lane, job.project.kit), audio, job.sampleRate));
                    expect(folder.getChildFile("Beat - BD.wav").existsAsFile(), "the bass drum's");
                    expect(folder.getChildFile("Beat - LC.wav").existsAsFile(), "the low conga's, by its switch");
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(mixFile.createInputStream().release(), true));
                    expect(reader != nullptr);
                    if (reader != nullptr)
                    {
                        expectEquals(static_cast<int>(reader->bitsPerSample), 24);
                        expectEquals(static_cast<int>(reader->numChannels), 2);
                        expectEquals(static_cast<int>(reader->lengthInSamples), bar);
                        expectEquals(juce::roundToInt(reader->sampleRate), 48000);
                    }
                }
                (void) folder.deleteRecursively();
            }

            beginTest("the_dialog_renders_and_writes_on_its_own_thread");
            {
                const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth808ExportPanelTest");
                (void) folder.deleteRecursively();
                (void) folder.createDirectory();
                ExportPanel::Hooks hooks;
                hooks.patternName = "Pattern 01";
                hooks.chainName = "Chain 01";
                hooks.jobFor = [](bool)
                {
                    return jobWith([](SequencerPattern& p) { p.lanes[0].a[0].on = true; p.lanes[10].a[4].on = true; });
                };
                auto panel = std::make_unique<ExportPanel>(hooks);
                panel->setBounds(0, 0, 1280, 800);
                const auto mixFile = folder.getChildFile("Panel.wav");
                panel->exportToForTesting(mixFile);
                for (int i = 0; i < 3000 && panel->busy(); ++i)
                    juce::Thread::sleep(10);
                expect(! panel->busy(), "done within 30 s");
                expect(mixFile.existsAsFile(), "the mix written");
                panel = nullptr; // joins its worker
                (void) folder.deleteRecursively();
            }
        }
    };

    static ExportTests exportTests;
}
