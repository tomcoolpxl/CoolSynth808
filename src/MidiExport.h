#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Project.h"

/*
    A pattern as a MIDI file, for dragging into a DAW: the sequencer itself plays it (swing, rolls, chance and accent
    as heard), one bar for A or B, two for AB, at 960 ticks a beat, on channel 10 with the kit's General MIDI keys.
    The 808's velocities (the lowest for a step, full for an accent) become 100 and 127, which a DAW's drums expect.
*/
namespace cs808
{
    [[nodiscard]] juce::MidiFile patternToMidi(const SequencerPattern& pattern, const KitValues& kit, const PlaySettings& play);
    /** Writes it to a temporary file named after the pattern; returns the file, or an invalid one. */
    juce::File writePatternMidi(const SequencerPattern& pattern, const KitValues& kit, const PlaySettings& play, const juce::String& name);
}
