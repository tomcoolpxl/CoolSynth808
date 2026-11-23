// Renders the built-in beats to WAV files for a listening pass beside their records: each beat on the stock kit,
// at its own tempo, its chain played until at least eight seconds have gone, then its tails.
//
//     CoolSynth808Render <folder> [id ...]      (no ids: every beat)

#include <cstdio>
#include <iostream>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Library.h"
#include "Processor.h"

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    bool render(const cs808::LibraryEntry& e, const juce::File& folder)
    {
        using namespace cs808;
        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails(0, 2, rate, block);
        p->prepareToPlay(rate, block);
        Project project;
        project.play = e.pattern.play;
        project.kit.alt = e.pattern.alt;
        project.currentBank().pattern[0] = e.pattern.pattern;
        for (std::size_t i = 0; i < e.pattern.moreParts.size() && i + 1 < SequencerBank::patterns; ++i)
            project.currentBank().pattern[i + 1] = e.pattern.moreParts[i].pattern;
        p->replaceProject(project, false);
        std::vector<int> chain;
        for (int i = 0; i <= static_cast<int>(e.pattern.moreParts.size()) && i < SequencerBank::patterns; ++i)
            chain.push_back(i);
        p->setChain(chain);

        // Whole passes of the chain, at least eight seconds of them, then a second and a half of tails.
        const double passBeats = 4.0 * static_cast<double>(chain.size()) * (e.pattern.play.variation == SequencerVariation::ab ? 2 : 1);
        const double passSeconds = passBeats * 60.0 / e.pattern.play.tempo;
        const int passes = std::max(1, static_cast<int>(std::ceil(8.0 / passSeconds - 1.0e-9)));
        const auto playing = static_cast<int>(std::llround(passSeconds * passes * rate));
        const int total = playing + static_cast<int>(1.5 * rate);

        juce::AudioBuffer<float> out(2, total);
        juce::AudioBuffer<float> buffer(2, block);
        p->setPlaying(true);
        for (int pos = 0; pos < total; pos += block)
        {
            if (pos >= playing)
                p->setPlaying(false);
            juce::MidiBuffer midi;
            p->processBlock(buffer, midi);
            const int n = std::min(block, total - pos);
            for (int c = 0; c < 2; ++c)
                out.copyFrom(c, pos, buffer, c, 0, n);
        }

        const auto file = folder.getChildFile(e.id + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
        if (writer == nullptr)
            return false;
        const bool ok = writer->writeFromAudioSampleBuffer(out, 0, total);
        std::cout << e.id << ": " << juce::String(total / rate, 1) << " s, peak " << juce::String(out.getMagnitude(0, total), 3)
                  << (ok ? "" : " (not written)") << std::endl;
        return ok;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    if (argc < 2)
    {
        std::fputs("CoolSynth808Render <folder> [id ...]\n", stderr);
        return 2;
    }
    const juce::File folder(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])));
    folder.createDirectory();
    juce::StringArray ids;
    for (int i = 2; i < argc; ++i)
        ids.add(juce::String::fromUTF8(argv[i]));

    int failures = 0, done = 0;
    for (const auto& e : cs808::builtInLibrary())
        if (ids.isEmpty() || ids.contains(e.id))
        {
            failures += render(e, folder) ? 0 : 1;
            ++done;
        }
    if (done == 0)
        std::fputs("no such beat\n", stderr);
    return failures == 0 && done > 0 ? 0 : 1;
}
