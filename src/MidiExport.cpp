#include "MidiExport.h"

#include <memory>

namespace cs808
{
    juce::MidiFile patternToMidi(const SequencerPattern& pattern, const KitValues& kit, const PlaySettings& play)
    {
        constexpr int ticksPerBeat = 960;
        // One sample a tick at 120 BPM: the sequencer's sample offsets are the file's ticks.
        constexpr double bpm = 120.0;
        constexpr double rate = ticksPerBeat * bpm / 60.0;

        auto bank = std::make_unique<SequencerBank>();
        bank->pattern[0] = pattern;
        applyNotes(*bank, kit);
        auto sequencer = std::make_unique<coolsynth::synth::StepSequencer>();
        sequencer->setBank(*bank);
        coolsynth::synth::SequencerSettings s;
        s.swing = play.swing;
        s.variation = play.variation;
        sequencer->setSettings(s);

        const int bars = play.variation == SequencerVariation::ab ? 2 : 1;
        const int ticks = bars * 4 * ticksPerBeat;
        std::vector<coolsynth::synth::EngineMidiEvent> events(4096);
        const int count = sequencer->process(0.0, bpm, true, ticks, rate, events.data(), static_cast<int>(events.size()));
        std::vector<coolsynth::synth::EngineMidiEvent> tail(512);
        const int tailCount = sequencer->process(static_cast<double>(bars) * 4.0, bpm, false, 1, rate, tail.data(), static_cast<int>(tail.size()));

        juce::MidiMessageSequence track;
        track.addEvent(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / play.tempo)), 0.0);
        track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
        for (int i = 0; i < count; ++i)
        {
            const auto& e = events[static_cast<std::size_t>(i)];
            if (e.type == coolsynth::synth::EngineMidiEventType::noteOn)
                track.addEvent(juce::MidiMessage::noteOn(10, e.noteNumber, static_cast<juce::uint8>(e.value > 0.5f ? 127 : 100)), e.sampleOffset);
            else
                track.addEvent(juce::MidiMessage::noteOff(10, e.noteNumber), e.sampleOffset);
        }
        for (int i = 0; i < tailCount; ++i)
            track.addEvent(juce::MidiMessage::noteOff(10, tail[static_cast<std::size_t>(i)].noteNumber), ticks);
        track.updateMatchedPairs();
        track.addEvent(juce::MidiMessage::endOfTrack(), ticks);

        juce::MidiFile file;
        file.setTicksPerQuarterNote(ticksPerBeat);
        file.addTrack(track);
        return file;
    }

    juce::File writePatternMidi(const SequencerPattern& pattern, const KitValues& kit, const PlaySettings& play, const juce::String& name)
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CoolSynth808");
        if (! folder.createDirectory())
            return {};
        const auto file = folder.getChildFile(juce::File::createLegalFileName(name.isEmpty() ? "Pattern" : name) + ".mid");
        // Written from its start whatever was there: a file still held open elsewhere (the last one dragged into a
        // DAW) is not deleted, and the stream would append to it (the review of D267).
        juce::FileOutputStream out(file);
        if (! out.openedOk() || ! out.setPosition(0) || out.truncate().failed())
            return {};
        auto midi = patternToMidi(pattern, kit, play);
        if (! midi.writeTo(out, 1))
            return {};
        return file;
    }
}
