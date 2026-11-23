#pragma once

#include <memory>

#include <juce_events/juce_events.h>

/*
    The controller's input apart from the sound's, in the standalone app (CoolSynth's D271). JUCE's standalone player
    takes every open MIDI input as one stream, so a drum note from a DAW on channel 10 and a pad of the controller on
    the same note could not be told apart. This stands between the inputs and the player: a linked control from the
    controller's input (ControllerSetup::controllerInput) goes to the processor's controller queue, its note-offs
    and everything else to the player; the other inputs go to the player only. While learning, every input may be
    heard, unless one device is chosen as the controller. With no controller input (Any input, or Auto finding none) every message goes to the player as before.
    It opens the controller's input when it is closed. Nothing in a host or the tests.
*/
namespace cs808
{
    class Processor;

    [[nodiscard]] std::unique_ptr<juce::Timer> makeStandaloneMidiInRouter(Processor& processor);
}
