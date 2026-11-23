#pragma once

#include <array>
#include <map>
#include <optional>

#include <juce_events/juce_events.h>

#include "midi/ControllerSetup.h"

/*
    What a controller's places do in CoolSynth 808 (D268): a first scheme after the user's sketch and the way small
    drum machines work (the TR-6S, Circuit Tracks: one set of knobs follows the chosen drum; pads play drums or
    enter steps), to be settled with the user. The places are CoolSynth's (src/midi, ported), so a controller set
    up once in CoolSynth 808 knows the same places.

    The main knob chooses the drum (the accent row is the first), left and right step through them, and its press
    switches the pads between playing drums and entering the chosen drum's steps. Back starts and stops.
    Knobs 1 to 3 are the chosen drum's own knobs in the panel's order, knob 4 its lane's length. Knob 5 chooses the
    pattern. Knobs 6, 7 and 8 are tempo, swing and volume. The four faders are the levels of the bass drum, the
    snare, the closed hat and the open hat.
    Playing, the eight pads strike the bass drum, snare, clap, closed hat, open hat, low tom, rim shot and cowbell;
    with Shift held, the mid tom, high tom and cymbal. A pad hit hard (velocity 100 or more) is accented. Entering
    steps, the pads are steps 1 to 8, with Shift 9 to 16.

    The audio thread strikes the pads' drums itself (Processor::setPadNotes), so a pad is heard without the message
    thread's delay; the rest comes here. Message thread.
*/
namespace cs808
{
    class Editor;

    /** The drum each pad strikes, by lane (-1 none): [shift][pad]. */
    inline constexpr std::array<std::array<int, 8>, 2> padLanes { { { 0, 1, 6, 10, 9, 2, 5, 7 }, { 3, 4, 8, -1, -1, -1, -1, -1 } } };

    /** A place's name ("Knob 5") and what it does in CoolSynth 808 ("Chooses the pattern"). */
    [[nodiscard]] juce::String placeTitle(coolsynth::midi::Place place);
    [[nodiscard]] juce::String placeUse(coolsynth::midi::Place place);

    class Controls final : private juce::Timer
    {
    public:
        explicit Controls(Editor& editor);
        ~Controls() override;

        /** Acts on one control moved, as the computer's controller setup links it to a place. */
        void handle(const coolsynth::midi::ControllerEvent& event);
        [[nodiscard]] bool padsEnterSteps() const noexcept { return stepMode; }
        [[nodiscard]] bool shiftHeld() const noexcept { return shift; }

    private:
        void timerCallback() override;
        void play(const coolsynth::midi::PlaceLink& link, const coolsynth::midi::ControllerEvent& event,
                  coolsynth::midi::Encoding encoding);
        /** Whether a move starts an undo step: a control moved without a pause is one, as a knob dragged. */
        [[nodiscard]] bool touch(coolsynth::midi::Place place);
        void closeGesture();
        /** Tells the audio thread which note each pad strikes now. */
        void publishPads();

        Editor& editor;
        bool shift = false;
        bool stepMode = false;
        coolsynth::midi::Place lastPlace = coolsynth::midi::Place::count;
        double lastMoveMs = 0.0;
        int seenRevision = -1;
        std::uint32_t padsKit = 0; // the switches the pads' notes were made for

        struct Last
        {
            int value = -1;
            int repeats = 0;
            std::optional<coolsynth::midi::Encoding> turns; // an endless knob found sending turns as positions
        };
        std::map<int, Last> lastValues;
    };
}
