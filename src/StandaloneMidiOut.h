#pragma once

#include <memory>

#include <juce_events/juce_events.h>

/*
    MIDI clock out on time in the standalone app (the review of D267). JUCE's standalone player sends a block's MIDI
    all at once from the audio thread unless the output runs its own sending thread; then each message leaves at its
    own sample. This keeps that thread running on whatever output is chosen in the audio and MIDI settings. Nothing
    in a host or the tests.
*/
namespace cs808
{
    [[nodiscard]] std::unique_ptr<juce::Timer> makeStandaloneMidiOutKeeper();
}
