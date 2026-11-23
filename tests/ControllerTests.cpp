#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "Controls.h"
#include "Editor.h"
#include "Processor.h"

/*
    A MIDI controller on CoolSynth 808 (D268): a pad strikes its drum on the audio thread and is no longer a note
    for the sound; the main knob chooses the drum; knobs 1 to 3 turn its knobs, one undo step a turn; knob 5 chooses
    the pattern; the main knob's press makes the pads enter steps; Shift reaches the second drums and steps 9 to 16.
*/
using namespace cs808;
namespace midi = coolsynth::midi;

class ControllerTests final : public juce::UnitTest
{
public:
    ControllerTests() : juce::UnitTest("Controller", "CoolSynth808") {}

    void runTest() override
    {
        midi::ControllerMap map;
        map.name = "Test controller";
        const auto cc = [](int number) { return midi::ControlId { midi::ControlId::Kind::cc, 1, number }; };
        const auto note = [](int number) { return midi::ControlId { midi::ControlId::Kind::note, 10, number }; };
        map.link(midi::Place::mainTurn, cc(28), midi::Encoding::binaryOffset);
        map.link(midi::Place::mainPress, cc(118), midi::Encoding::absolute);
        map.link(midi::Place::shift, cc(27), midi::Encoding::absolute);
        map.link(midi::Place::knob1, cc(74), midi::Encoding::absolute);
        map.link(midi::Place::knob5, cc(77), midi::Encoding::absolute);
        for (int pad = 0; pad < 8; ++pad)
            map.link(static_cast<midi::Place>(static_cast<int>(midi::Place::pad1) + pad), note(36 + pad), midi::Encoding::absolute);
        midi::ControllerSetup::instance().useForTesting(map);

        auto p = std::make_unique<Processor>();
        p->setPlayConfigDetails(0, 2, 48000.0, 480);
        p->prepareToPlay(48000.0, 480);
        std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
        auto& editor = *dynamic_cast<Editor*>(e.get());
        auto& controls = editor.controlsForTesting();

        juce::AudioBuffer<float> buffer(2, 480);
        const auto block = [&](const juce::MidiMessage& m)
        {
            juce::MidiBuffer in;
            in.addEvent(m, 10);
            p->processBlock(buffer, in);
            return in;
        };
        const auto send = [&](const juce::MidiMessage& m)
        {
            (void) block(m);
            p->takeControllerEvents([&](const midi::ControllerEvent& ev) { controls.handle(ev); });
        };

        beginTest("a_pad_strikes_its_drum_at_once_and_is_no_note_for_the_sound");
        {
            (void) p->takeStruckLanes();
            // Pad 2's note is 37, the rim shot's: the snare struck and not the rim shot says the pad's own note
            // went to the panel and the snare's to the sound.
            (void) block(juce::MidiMessage::noteOn(10, 37, static_cast<juce::uint8>(80)));
            const auto bits = p->takeStruckLanes();
            expect((bits & (1u << 1)) != 0, "the snare was struck");
            expect((bits & (1u << 5)) == 0, "the rim shot was not");
            p->takeControllerEvents([&](const midi::ControllerEvent& ev) { controls.handle(ev); });
            expectEquals(editor.selectedLane, 2, "the pad chose its drum");
        }

        beginTest("the_main_knob_chooses_the_drum_and_knob_1_turns_its_first_knob");
        {
            editor.selectedLane = 1;
            send(juce::MidiMessage::controllerEvent(1, 28, 66)); // two steps right
            expectEquals(editor.selectedLane, 3);
            send(juce::MidiMessage::controllerEvent(1, 28, 63)); // one left
            expectEquals(editor.selectedLane, 2, "the snare");
            while (p->undo()) {}
            send(juce::MidiMessage::controllerEvent(1, 74, 0));
            send(juce::MidiMessage::controllerEvent(1, 74, 127));
            int sdLevel = -1;
            for (int k = 0; k < knobCount; ++k)
                if (juce::String(knobs[static_cast<std::size_t>(k)].id) == "sdLevel")
                    sdLevel = k;
            expectWithinAbsoluteError(p->project().kit.knob[static_cast<std::size_t>(sdLevel)], 1.0f, 1.0e-6f);
            // The turn ends: one undo step takes it all back.
            controls.handle({ cc(28), 64, false }); // another control: the knob's step closes
            expect(p->undo(), "one step to undo");
            expect(! p->canUndo(), "and only one");
        }

        beginTest("knob_5_chooses_the_pattern");
        {
            send(juce::MidiMessage::controllerEvent(1, 77, 127));
            expectEquals(editor.editPattern, 15);
            expect(p->chain() == std::vector<int> { 15 });
            send(juce::MidiMessage::controllerEvent(1, 77, 0));
            expectEquals(editor.editPattern, 0);
        }

        beginTest("pressing_the_main_knob_makes_the_pads_enter_steps");
        {
            editor.selectedLane = 1; // the bass drum
            send(juce::MidiMessage::controllerEvent(1, 118, 127));
            expect(controls.padsEnterSteps());
            (void) p->takeStruckLanes();
            const bool before = editor.laneShown(1).a[2].on;
            send(juce::MidiMessage::noteOn(10, 38, static_cast<juce::uint8>(90))); // pad 3: step 3
            expect(editor.laneShown(1).a[2].on != before, "step 3 turned");
            expectEquals(static_cast<int>(p->takeStruckLanes() & ((1u << laneCount) - 1u)), 0, "and nothing struck");
            send(juce::MidiMessage::controllerEvent(1, 27, 127)); // Shift held
            const bool before12 = editor.laneShown(1).a[11].on;
            send(juce::MidiMessage::noteOn(10, 39, static_cast<juce::uint8>(90))); // pad 4 with Shift: step 12
            expect(editor.laneShown(1).a[11].on != before12, "step 12 turned");
            send(juce::MidiMessage::controllerEvent(1, 27, 0));
            send(juce::MidiMessage::controllerEvent(1, 118, 127));
            expect(! controls.padsEnterSteps(), "back to playing");
        }

        beginTest("with_shift_a_pad_strikes_its_second_drum");
        {
            send(juce::MidiMessage::controllerEvent(1, 27, 127));
            (void) p->takeStruckLanes();
            (void) block(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(120))); // pad 1 with Shift: mid tom
            const auto bits = p->takeStruckLanes();
            expect((bits & (1u << 3)) != 0, "the mid tom");
            send(juce::MidiMessage::controllerEvent(1, 27, 0));
        }

        beginTest("a_pad_struck_in_the_same_block_as_shift_takes_its_second_drum");
        {
            // Shift and the pad in one block, before the message thread has seen either (the review of D267).
            (void) p->takeStruckLanes();
            juce::MidiBuffer in;
            in.addEvent(juce::MidiMessage::controllerEvent(1, 27, 127), 5);
            in.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(120)), 6);
            p->processBlock(buffer, in);
            auto bits = p->takeStruckLanes();
            expect((bits & (1u << 3)) != 0, "the mid tom");
            expect((bits & (1u << 0)) == 0, "not the bass drum");
            juce::MidiBuffer up;
            up.addEvent(juce::MidiMessage::controllerEvent(1, 27, 0), 5);
            up.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(120)), 6);
            p->processBlock(buffer, up);
            bits = p->takeStruckLanes();
            expect((bits & (1u << 0)) != 0, "Shift let go: the bass drum");
            p->takeControllerEvents([&](const midi::ControllerEvent& ev) { controls.handle(ev); });
            expect(! controls.shiftHeld());
        }

        // D271: the standalone sorts its inputs; a pad's note from the controller strikes the pad's drum, the same note
        // from another input (a DAW's General MIDI drums) plays its own.
        beginTest("sorted_inputs_a_pad_from_the_controller_and_the_same_note_from_elsewhere");
        {
            p->setInputsRouted(true);
            (void) p->takeStruckLanes();
            (void) block(juce::MidiMessage::noteOn(10, 37, static_cast<juce::uint8>(80))); // from another input
            auto bits = p->takeStruckLanes();
            expect((bits & (1u << 5)) != 0, "the rim shot, its General MIDI drum");
            expect((bits & (1u << 1)) == 0, "not the pad's snare");
            p->takeControllerMessage(juce::MidiMessage::noteOn(10, 37, static_cast<juce::uint8>(80))); // from the controller
            (void) block(juce::MidiMessage::controllerEvent(1, 7, 100)); // an unlinked control: the sound's
            bits = p->takeStruckLanes();
            expect((bits & (1u << 1)) != 0, "the pad's snare");
            expect((bits & (1u << 5)) == 0, "and not the rim shot");
            p->takeControllerEvents([&](const midi::ControllerEvent& ev) { controls.handle(ev); });
            p->setInputsRouted(false);
        }

        beginTest("the_controller_input_auto_takes_the_ready_setups_port_then_the_input_learned_from");
        {
            using Choice = midi::ControllerSetup::InputChoice;
            const juce::MidiDeviceInfo miniLab { "Minilab3 MIDI", "id-minilab" }, loop { "loopMIDI Port", "id-loop" },
                                       gone { "Old keyboard", "id-gone" };
            const juce::Array<juce::MidiDeviceInfo> plugged { loop, miniLab };
            const auto resolve = [&](Choice how, const juce::MidiDeviceInfo& chosen, const juce::MidiDeviceInfo& learned,
                                     const juce::String& profile) { return midi::ControllerSetup::resolveInput(how, chosen, learned, profile, plugged); };
            expect(! resolve(Choice::any, {}, loop, "arturia.minilab3").has_value(), "Any input: none");
            expect(resolve(Choice::automatic, {}, loop, "arturia.minilab3") == miniLab, "the ready setup's port first");
            expect(resolve(Choice::automatic, {}, loop, {}) == loop, "then the input learned from");
            expect(resolve(Choice::automatic, {}, { "loopMIDI Port", "id-changed" }, {}) == loop, "by name when its id moved");
            expect(! resolve(Choice::automatic, {}, gone, {}).has_value(), "unplugged: none, every input may be it");
            expect(resolve(Choice::device, loop, {}, "arturia.minilab3") == loop, "a chosen device");
            expect(resolve(Choice::device, gone, {}, {}) == gone, "even unplugged");
        }

        e.reset();
        p.reset();
        midi::ControllerSetup::instance().useForTesting({});
    }
};

static ControllerTests controllerTests;
