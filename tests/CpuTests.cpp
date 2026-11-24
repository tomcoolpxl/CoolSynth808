// What each drum costs to render (TODO: the cowbell heavy on the CPU), measured only when COOLSYNTH808_MEASURE is
// set: each drum struck on every step for 8 s at 120 BPM, rendered offline, the time it took as a share of real time.

#include <chrono>
#include <memory>

#include "AudioExport.h"

namespace
{
    using namespace cs808;

    class CpuTests final : public juce::UnitTest
    {
    public:
        CpuTests() : juce::UnitTest("Cpu", "CoolSynth808") {}

        void runTest() override
        {
            beginTest("each_drums_cost");
            if (juce::SystemStats::getEnvironmentVariable("COOLSYNTH808_MEASURE", {}).isEmpty())
                return;
            static constexpr std::array<const char*, laneCount> names { "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH" };
            const auto run = [this](int lane, int accuracy = 0)
            {
                ExportJob job;
                job.accuracy = accuracy;
                job.project.play.tempo = 120.0;
                job.bpm = 120.0;
                job.sampleRate = 48000.0;
                job.times = 4; // 8 s
                auto& p = job.project.currentBank().pattern[0];
                p = SequencerPattern {};
                if (lane >= 0)
                    for (int s = 0; s < 16; ++s)
                        p.lanes[static_cast<std::size_t>(lane)].a[static_cast<std::size_t>(s)].on = true;
                ExportResult r;
                std::atomic<float> done { 0.0f };
                const std::atomic<bool> cancel { false };
                const auto t0 = std::chrono::steady_clock::now();
                expect(renderExport(job, r, done, cancel));
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                return 100.0 * seconds / 8.0;
            };
            const double idle = run(-1);
            logMessage("nothing struck: " + juce::String(idle, 2) + " % of real time");
            for (int lane = 0; lane < laneCount; ++lane)
                logMessage(juce::String(names[static_cast<std::size_t>(lane)]) + " on every step: " + juce::String(run(lane), 2) + " % of real time");
            // The accuracy's lower levels (D339): the metal drums, and the snare for the rest of the kit.
            for (const int level : { 1, 2 })
                for (const int lane : { 1, 7, 8, 9, 10 })
                    logMessage(juce::String(level == 1 ? "Lighter, " : "Lightest, ") + names[static_cast<std::size_t>(lane)] + " on every step: "
                               + juce::String(run(lane, level), 2) + " % of real time");

            // For listening (COOLSYNTH808_MEASURE_WAVS names a folder): the metal drums in a beat, rung out.
            if (const auto folder = juce::SystemStats::getEnvironmentVariable("COOLSYNTH808_MEASURE_WAVS", {}); folder.isNotEmpty())
            {
                const juce::File out(folder);
                (void) out.createDirectory();
                const auto write = [&](const juce::String& name, const std::function<void(SequencerPattern&)>& steps)
                {
                    ExportJob job;
                    job.accuracy = juce::SystemStats::getEnvironmentVariable("COOLSYNTH808_MEASURE_ACCURACY", "0").getIntValue();
                    job.project.play.tempo = 120.0;
                    job.bpm = 120.0;
                    job.sampleRate = 48000.0;
                    job.times = 2;
                    job.end = ExportEnd::ring;
                    auto& p = job.project.currentBank().pattern[0];
                    p = SequencerPattern {};
                    steps(p);
                    ExportResult r;
                    std::atomic<float> done { 0.0f };
                    const std::atomic<bool> cancel { false };
                    if (renderExport(job, r, done, cancel))
                        expect(writeWav24(out.getChildFile(name + ".wav"), r.mix, job.sampleRate));
                };
                write("cowbell", [](SequencerPattern& p) { for (const int s : { 0, 3, 6, 10, 12 }) p.lanes[7].a[static_cast<std::size_t>(s)].on = true; });
                write("hats", [](SequencerPattern& p)
                {
                    for (int s = 0; s < 16; s += 2)
                        p.lanes[10].a[static_cast<std::size_t>(s)].on = true;
                    for (const int s : { 3, 11, 15 })
                        p.lanes[9].a[static_cast<std::size_t>(s)].on = true;
                });
                write("cymbal", [](SequencerPattern& p) { p.lanes[8].a[0].on = true; });
            }
        }
    };

    static CpuTests cpuTests;
}
