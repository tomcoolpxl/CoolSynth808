#pragma once

#include <atomic>
#include <utility>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Project.h"

/*
    The WAV export (D331): the pattern shown or the chain, played from its start a number of times and rendered
    offline by a processor of its own, as the app plays it (the kit, swing, accents, chance, the hats' choke). The mix,
    and as an option one file per drum heard, all of the same length: each drum's file is the same render with only
    that drum heard (the engine's mask on its generators' outputs): every drum plays, rings and rests as in the mix, so
    what one does to another (the closed hat choking the open one, the voice the metal oscillators run in) stays, and
    the files add up to the mix.

    The end: cut where the last time through ends; the ring-out after it; or a seamless loop, the ring-out in its
    start: played as a loop until what rang before has rung out, the last time through kept, so its start holds the
    end's ring-out as a loop plays it, chokes and all. 24-bit WAV at the app's sample rate.
*/
namespace cs808
{
    enum class ExportEnd { cut = 0, ring = 1, loop = 2 };

    struct ExportJob
    {
        Project project;
        std::vector<int> patterns { 0 }; // in a row: the pattern shown, or the chain (indices in the bank)
        int times = 1;
        ExportEnd end = ExportEnd::cut;
        bool stems = false;
        double sampleRate = 48000.0;
        double bpm = 120.0;
    };

    struct ExportResult
    {
        juce::AudioBuffer<float> mix;
        std::vector<std::pair<int, juce::AudioBuffer<float>>> stems; // by lane, each the mix's length
    };

    /** The lanes heard in the patterns rendered: a step on in the variation played, not muted, not silenced by a
        solo. */
    [[nodiscard]] std::vector<int> lanesHeard(const ExportJob& job);

    /** The length of the times played through, in samples: where the next time through would begin. */
    [[nodiscard]] std::int64_t loopSamples(const ExportJob& job);

    /** Renders the mix and, if asked, each drum heard. Any thread; the renders share the computer's cores. `done`
        counts up to 1; `cancel` stops it, returning false. */
    bool renderExport(const ExportJob& job, ExportResult& result, std::atomic<float>& done, const std::atomic<bool>& cancel);

    /** Writes a 24-bit WAV; false if it could not. */
    bool writeWav24(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate);

    /** A drum's file beside the mix's: "Name - BD.wav" (the switched instrument's code when it is switched). */
    [[nodiscard]] juce::File stemFile(const juce::File& mixFile, int lane, const KitValues& kit);
}
