#include "AudioExport.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Processor.h"

namespace cs808
{
    namespace
    {
        constexpr int block = 512;
        constexpr float quietLevel = 1.0e-5f;  // -100 dB, the engine's own quiet
        constexpr double quietSeconds = 0.25;  // that long under it, and a drum has rung out
        constexpr double longestRing = 20.0;   // seconds after the end at most
        constexpr double ringBefore = 10.0;    // a seamless loop plays this long before the time kept

        /** The engine's light for each lane: what its mask hears (a tom's is its conga's too, the rim shot's the claves'). */
        using Watch = coolsynth::synth::v3::CircuitWatch;
        constexpr std::array<int, laneCount> lightOfLane { Watch::bassDrum, Watch::snare, Watch::lowTom, Watch::midTom, Watch::highTom,
                                                           Watch::rimShot, Watch::clap, Watch::cowbell, Watch::cymbal, Watch::openHat,
                                                           Watch::closedHat };

        double beatsPerPass(const ExportJob& job) noexcept
        {
            const int bars = job.project.play.variation == SequencerVariation::ab ? 2 : 1;
            return 4.0 * bars * static_cast<double>(job.patterns.size());
        }

        double samplesPerBeat(const ExportJob& job) noexcept { return job.sampleRate * 60.0 / job.bpm; }

        /** The first sample at or after a beat position, as the sequencer places a note. */
        std::int64_t sampleAt(const ExportJob& job, double beats) noexcept
        {
            return static_cast<std::int64_t>(std::ceil(beats * samplesPerBeat(job) - 1.0e-6));
        }

        /** One render: the mix (lane -1) or one drum, by a processor of its own. */
        class OneRender
        {
        public:
            OneRender(const ExportJob& j, int lane, std::atomic<std::int64_t>& counter, const std::atomic<bool>& stop)
                : job(j), rendered(counter), cancel(stop)
            {
                Project project = job.project;
                project.play.loop = true;
                project.play.tempo = job.bpm;
                processor->replaceProject(project, false);
                if (lane >= 0)
                    processor->circuitWatch().hearOnly(1u << lightOfLane[static_cast<std::size_t>(lane)]);
                processor->setChain(job.patterns);
                processor->setAccuracy(job.accuracy);
                processor->setPlayConfigDetails(0, 2, job.sampleRate, block);
                processor->prepareToPlay(job.sampleRate, block);
            }

            /** The whole render into `out`; the last sample above the quiet in `lastLoud`. False if cancelled. */
            bool run(juce::AudioBuffer<float>& out, std::int64_t& lastLoud)
            {
                const auto loop = loopSamples(job);
                const double totalBeats = beatsPerPass(job) * job.times;
                lastLoud = -1;
                if (job.end == ExportEnd::loop)
                {
                    // Played as a loop until what rang before has rung out: from a bar far enough before the end, or
                    // round and round when the loop is shorter than that; then from the start, the time kept.
                    const double before = 4.0 * std::ceil(ringBefore * job.bpm / 60.0 / 4.0);
                    processor->setPlaying(true);
                    if (before < totalBeats)
                    {
                        processor->jumpTo(totalBeats - before);
                        if (! play(sampleAt(job, before), nullptr))
                            return false;
                    }
                    else
                        for (int round = 0; round < static_cast<int>(std::ceil(before / totalBeats)); ++round)
                        {
                            if (round > 0)
                                processor->jumpTo(0.0);
                            if (! play(loop, nullptr))
                                return false;
                        }
                    processor->jumpTo(0.0);
                    out.setSize(2, static_cast<int>(loop));
                    return play(loop, &out, 0, &lastLoud);
                }

                const auto room = job.end == ExportEnd::ring ? loop + static_cast<std::int64_t>(longestRing * job.sampleRate) : loop;
                out.setSize(2, static_cast<int>(room));
                out.clear();
                processor->setPlaying(true);
                if (! play(loop, &out, 0, &lastLoud))
                    return false;
                if (job.end == ExportEnd::cut)
                    return true;
                // The ring-out: stopped, until it has been quiet a while.
                processor->setPlaying(false);
                const auto hold = static_cast<std::int64_t>(quietSeconds * job.sampleRate);
                std::int64_t at = loop;
                while (at < room && (at - std::max(lastLoud, loop - 1)) < hold)
                {
                    const auto n = std::min<std::int64_t>(block, room - at);
                    if (! play(n, &out, at, &lastLoud))
                        return false;
                    at += n;
                }
                return true;
            }

        private:
            bool play(std::int64_t samples, juce::AudioBuffer<float>* out, std::int64_t at = 0, std::int64_t* lastLoud = nullptr)
            {
                for (std::int64_t done = 0; done < samples;)
                {
                    if (cancel.load(std::memory_order_relaxed))
                        return false;
                    const int n = static_cast<int>(std::min<std::int64_t>(block, samples - done));
                    buffer.setSize(2, n, false, false, true);
                    midi.clear();
                    processor->processBlock(buffer, midi);
                    if (out != nullptr)
                        for (int c = 0; c < 2; ++c)
                        {
                            const float* from = buffer.getReadPointer(c);
                            out->copyFrom(c, static_cast<int>(at + done), from, n);
                            if (lastLoud != nullptr)
                                for (int i = 0; i < n; ++i)
                                    if (std::abs(from[i]) > quietLevel)
                                        *lastLoud = std::max(*lastLoud, at + done + i);
                        }
                    done += n;
                    rendered.fetch_add(n, std::memory_order_relaxed);
                }
                return true;
            }

            const ExportJob& job;
            std::atomic<std::int64_t>& rendered;
            const std::atomic<bool>& cancel;
            std::unique_ptr<Processor> processor = std::make_unique<Processor>(Processor::Offline {});
            juce::AudioBuffer<float> buffer { 2, block };
            juce::MidiBuffer midi;
        };
    }

    std::vector<int> lanesHeard(const ExportJob& job)
    {
        const auto variation = job.project.play.variation;
        std::array<bool, laneCount> heard {};
        for (const int index : job.patterns)
        {
            if (index < 0 || index >= SequencerBank::patterns)
                continue;
            const auto& pattern = job.project.currentBank().pattern[static_cast<std::size_t>(index)];
            bool anySolo = false;
            for (int l = 0; l < laneCount; ++l)
                anySolo = anySolo || pattern.lanes[static_cast<std::size_t>(l)].solo;
            for (int l = 0; l < laneCount; ++l)
            {
                const auto& lane = pattern.lanes[static_cast<std::size_t>(l)];
                if (lane.mute || (anySolo && ! lane.solo))
                    continue;
                const int length = std::clamp(lane.length, 1, SequencerLane::steps);
                for (int s = 0; s < length; ++s)
                {
                    const auto& a = lane.a[static_cast<std::size_t>(s)];
                    const auto& b = lane.b[static_cast<std::size_t>(s)];
                    if ((variation != SequencerVariation::b && a.on && a.chance > 0.0f)
                        || (variation != SequencerVariation::a && b.on && b.chance > 0.0f))
                        heard[static_cast<std::size_t>(l)] = true;
                }
            }
        }
        std::vector<int> lanes;
        for (int l = 0; l < laneCount; ++l)
            if (heard[static_cast<std::size_t>(l)])
                lanes.push_back(l);
        return lanes;
    }

    std::int64_t loopSamples(const ExportJob& job)
    {
        return sampleAt(job, beatsPerPass(job) * std::max(1, job.times));
    }

    bool renderExport(const ExportJob& job, ExportResult& result, std::atomic<float>& done, const std::atomic<bool>& cancel)
    {
        std::vector<int> renders { -1 };
        if (job.stems)
            for (const int lane : lanesHeard(job))
                renders.push_back(lane);
        const auto count = renders.size();
        std::vector<juce::AudioBuffer<float>> audio(count);
        std::vector<std::int64_t> lastLoud(count, -1);
        std::vector<char> finished(count, 0);

        // The work to do, for the progress: the time kept, plus what plays before it or rings after.
        const auto loop = loopSamples(job);
        const auto extra = job.end == ExportEnd::loop ? static_cast<std::int64_t>(ringBefore * job.sampleRate)
                         : job.end == ExportEnd::ring ? static_cast<std::int64_t>(2.0 * job.sampleRate) : 0;
        const double total = static_cast<double>((loop + extra) * static_cast<std::int64_t>(count));
        std::atomic<std::int64_t> rendered { 0 };

        {
            // Each render on a core of its own, below the audio thread's priority: the app keeps playing.
            const int cores = std::max(1, juce::SystemStats::getNumCpus() - 1);
            juce::ThreadPool pool(juce::ThreadPoolOptions {}
                                      .withThreadName("CoolSynth 808 export")
                                      .withNumberOfThreads(std::min(cores, static_cast<int>(count)))
                                      .withThreadStackSizeBytes(4 * 1024 * 1024)
                                      .withDesiredThreadPriority(juce::Thread::Priority::low));
            for (std::size_t i = 0; i < count; ++i)
                pool.addJob([&, i]
                {
                    OneRender render(job, renders[i], rendered, cancel);
                    finished[i] = render.run(audio[i], lastLoud[i]) ? 1 : 0;
                });
            while (pool.getNumJobs() > 0)
            {
                done.store(static_cast<float>(std::min(0.99, static_cast<double>(rendered.load()) / std::max(1.0, total))));
                juce::Thread::sleep(20);
            }
        }
        if (cancel.load() || std::find(finished.begin(), finished.end(), 0) != finished.end())
            return false;

        // Rung out: every file as long as the longest ring-out, so they line up.
        if (job.end == ExportEnd::ring)
        {
            std::int64_t length = loop;
            for (const auto last : lastLoud)
                length = std::max(length, last + 1);
            for (auto& a : audio)
                a.setSize(2, static_cast<int>(length), true, true, false);
        }
        result.mix = std::move(audio[0]);
        result.stems.clear();
        for (std::size_t i = 1; i < count; ++i)
            result.stems.emplace_back(renders[i], std::move(audio[i]));
        done.store(1.0f);
        return true;
    }

    bool writeWav24(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        (void) file.deleteFile();
        auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                      .withSampleRate(sampleRate)
                                                      .withNumChannels(audio.getNumChannels())
                                                      .withBitsPerSample(24));
        if (writer == nullptr)
            return false;
        return writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    }

    juce::File stemFile(const juce::File& mixFile, int lane, const KitValues& kit)
    {
        const auto& instrument = instruments[static_cast<std::size_t>(lane)];
        const bool switched = instrument.altCode != nullptr && kit.alt[static_cast<std::size_t>(lane)];
        const juce::String code = switched ? instrument.altCode : instrument.code;
        return mixFile.getSiblingFile(mixFile.getFileNameWithoutExtension() + " - " + code + ".wav");
    }
}
