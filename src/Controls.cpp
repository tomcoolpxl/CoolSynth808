#include "Controls.h"

#include <cmath>

#include "Editor.h"

namespace cs808
{
    namespace midi = coolsynth::midi;

    namespace
    {
        int keyOf(const midi::ControlId& c) noexcept
        {
            return ((c.kind == midi::ControlId::Kind::note ? 1 : 0) * 17 + c.channel) * 128 + c.number;
        }

        /** A value's new place: the control's position for an absolute one, the old value moved by its turn otherwise. */
        float moved(float old, midi::Encoding encoding, int value, float step = 1.0f / 100.0f)
        {
            if (encoding == midi::Encoding::absolute)
                return static_cast<float>(value) / 127.0f;
            return juce::jlimit(0.0f, 1.0f, old + static_cast<float>(midi::relativeSteps(encoding, value)) * step);
        }

        /** A count's new value: its position across the range for an absolute control, a step a click otherwise. */
        int movedCount(int old, int low, int high, midi::Encoding encoding, int value)
        {
            if (encoding == midi::Encoding::absolute)
                return low + juce::roundToInt(value / 127.0 * (high - low));
            return juce::jlimit(low, high, old + midi::relativeSteps(encoding, value));
        }

        /** The panel's knobs on a drum's column, in their order (the accent row: the Accent). */
        std::vector<int> knobsOf(int panelLane)
        {
            std::vector<int> list;
            for (int k = 0; k < knobCount; ++k)
                if (knobs[static_cast<std::size_t>(k)].lane + 1 == panelLane)
                    list.push_back(k);
            return list;
        }

        int knobIndex(const char* id)
        {
            for (int k = 0; k < knobCount; ++k)
                if (juce::String(knobs[static_cast<std::size_t>(k)].id) == id)
                    return k;
            return -1;
        }

        std::uint32_t switchesOf(const KitValues& kit) noexcept
        {
            std::uint32_t bits = 0;
            for (int l = 0; l < laneCount; ++l)
                if (kit.alt[static_cast<std::size_t>(l)])
                    bits |= 1u << l;
            return bits;
        }
    }

    juce::String placeTitle(midi::Place place)
    {
        using P = midi::Place;
        switch (place)
        {
            case P::mainTurn: return "Main knob";
            case P::mainLeft: return "Main knob left";
            case P::mainRight: return "Main knob right";
            case P::mainPress: return "Main knob press";
            case P::back: return "Back";
            case P::shift: return "Shift";
            default: break;
        }
        if (place >= P::knob1 && place <= P::knob8)
            return "Knob " + juce::String(midi::knobOf(place));
        if (place >= P::fader1 && place <= P::fader4)
            return "Fader " + juce::String(midi::faderOf(place) + 1);
        if (place >= P::pad1 && place <= P::pad8)
            return "Pad " + juce::String(midi::padOf(place) + 1);
        return {};
    }

    juce::String placeUse(midi::Place place)
    {
        using P = midi::Place;
        if (place >= P::pad1 && place <= P::pad8)
        {
            const auto pad = static_cast<std::size_t>(midi::padOf(place));
            juce::String text = "Strikes the " + juce::String(instruments[static_cast<std::size_t>(padLanes[0][pad])].name).toLowerCase();
            if (padLanes[1][pad] >= 0)
                text << " (Shift: " << juce::String(instruments[static_cast<std::size_t>(padLanes[1][pad])].name).toLowerCase() << ")";
            return text << "; step " << static_cast<int>(pad + 1) << " (Shift: " << static_cast<int>(pad + 9) << ") entering steps";
        }
        switch (place)
        {
            case P::mainTurn: return "Chooses the drum";
            case P::mainLeft: return "The drum before";
            case P::mainRight: return "The next drum";
            case P::mainPress: return "Pads: play drums or enter the drum's steps";
            case P::back: return "Play and stop";
            case P::shift: return "The pads' second drums and steps 9 to 16";
            case P::knob1: return "The chosen drum's first knob";
            case P::knob2: return "The chosen drum's second knob";
            case P::knob3: return "The chosen drum's third knob";
            case P::knob4: return "The chosen drum's lane length";
            case P::knob5: return "Chooses the pattern";
            case P::knob6: return "Tempo";
            case P::knob7: return "Swing";
            case P::knob8: return "Volume";
            case P::fader1: return "Bass drum level";
            case P::fader2: return "Snare drum level";
            case P::fader3: return "Closed hat level";
            case P::fader4: return "Open hat level";
            default: return {};
        }
    }

    Controls::Controls(Editor& e) : editor(e)
    {
        publishPads();
        startTimerHz(100);
    }

    Controls::~Controls()
    {
        closeGesture();
        editor.processor.setPadNotes({});
    }

    void Controls::timerCallback()
    {
        auto& setup = midi::ControllerSetup::instance();
        if (setup.revision() != seenRevision || switchesOf(editor.processor.project().kit) != padsKit)
        {
            seenRevision = setup.revision();
            lastValues.clear();
            publishPads();
        }
        // A control left still for a moment ends its undo step.
        if (gestureOpen && juce::Time::getMillisecondCounterHiRes() - lastMoveMs > 600.0)
            closeGesture();

        bool any = false;
        editor.processor.takeControllerEvents([&](const midi::ControllerEvent& event)
        {
            any = true;
            handle(event);
        });
        if (any)
            editor.refresh();
    }

    void Controls::closeGesture()
    {
        if (gestureOpen)
            editor.processor.endGesture();
        gestureOpen = false;
        lastPlace = midi::Place::count;
    }

    void Controls::touch(midi::Place place)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (place != lastPlace || ! gestureOpen)
        {
            closeGesture();
            editor.processor.beginGesture();
            gestureOpen = true;
        }
        lastPlace = place;
        lastMoveMs = now;
    }

    void Controls::publishPads()
    {
        padsKit = switchesOf(editor.processor.project().kit);
        Processor::PadNotes notes;
        notes.fill(-1);
        if (! stepMode)
        {
            // A place may have more than one control (alsoLink): every one of them.
            for (const auto& link : midi::ControllerSetup::instance().map().places)
                if (link.place >= midi::Place::pad1 && link.place <= midi::Place::pad8)
                {
                    const int lane = padLanes[shift ? 1u : 0u][static_cast<std::size_t>(midi::padOf(link.place))];
                    if (const int index = Processor::padIndex(link.control); index >= 0)
                        notes[static_cast<std::size_t>(index)] = static_cast<std::int8_t>(lane >= 0 ? laneNote(lane, editor.processor.project().kit) : -1);
                }
        }
        editor.processor.setPadNotes(notes);
    }

    void Controls::handle(const midi::ControllerEvent& event)
    {
        auto& setup = midi::ControllerSetup::instance();
        if (setup.isLearning())
        {
            setup.hear(event, this);
            return;
        }
        const auto* link = setup.map().placeFor(event.control);
        if (link == nullptr)
            return;

        // As CoolSynth's ControllerInput (D211): an endless knob that sends positions around the middle is found by
        // the same value three times, and taken as sending turns from then on.
        auto& last = lastValues[keyOf(event.control)];
        const int previous = last.value;
        last.value = event.value;
        auto encoding = link->encoding;
        if (encoding == midi::Encoding::absolute && ! event.note)
        {
            const bool turnLike = (event.value >= 61 && event.value <= 67 && event.value != 64)
                               || (event.value >= 1 && event.value <= 3) || (event.value >= 125 && event.value <= 126);
            last.repeats = previous == event.value && turnLike ? last.repeats + 1 : 0;
            if (! last.turns && last.repeats >= 2)
                last.turns = midi::guessEncoding({ event.value, event.value, event.value });
            if (last.turns)
                encoding = *last.turns;
        }
        play(*link, event, encoding);
    }

    void Controls::play(const midi::PlaceLink& link, const midi::ControllerEvent& event, midi::Encoding encoding)
    {
        auto& p = editor.processor;
        const auto place = link.place;
        const int value = event.value;
        // Pressed: a note-on, or a controller in its upper half (some buttons send 127 a press and nothing on release).
        const bool down = event.value >= 64 || (event.note && event.value > 0);
        constexpr int panelLanes = laneCount + 1;

        if (midi::isButtonPlace(place))
        {
            if (place == midi::Place::shift)
            {
                if (shift != down)
                {
                    shift = down;
                    publishPads();
                }
                return;
            }
            if (! down)
                return;
            closeGesture();
            if (place >= midi::Place::pad1 && place <= midi::Place::pad8)
            {
                const int pad = midi::padOf(place);
                if (stepMode)
                {
                    const int step = pad + (shift ? 8 : 0);
                    const bool b = editor.editB;
                    editor.editLane(editor.selectedLane, [&](SequencerLane& l)
                    {
                        auto& s = (b ? l.b : l.a)[static_cast<std::size_t>(step)];
                        s.on = ! s.on;
                        if (s.on)
                        {
                            s.chance = 1.0f;
                            s.hits = 1;
                        }
                    });
                    editor.selectedStep = step;
                }
                else if (const int lane = padLanes[shift ? 1u : 0u][static_cast<std::size_t>(pad)]; lane >= 0)
                    editor.selectedLane = lane + 1; // the audio thread has struck it already
                return;
            }
            switch (place)
            {
                case midi::Place::mainPress:
                    stepMode = ! stepMode;
                    publishPads();
                    break;
                case midi::Place::mainLeft: editor.selectedLane = (editor.selectedLane + panelLanes - 1) % panelLanes; break;
                case midi::Place::mainRight: editor.selectedLane = (editor.selectedLane + 1) % panelLanes; break;
                case midi::Place::back: p.setPlaying(! p.isPlaying()); break;
                default: break;
            }
            return;
        }

        if (place != lastPlace)
            closeGesture(); // another control: the last one's turn is one undo step
        switch (place)
        {
            case midi::Place::mainTurn:
                editor.selectedLane = movedCount(editor.selectedLane, 0, panelLanes - 1, encoding, value);
                return;
            case midi::Place::knob5:
            {
                const int pattern = movedCount(std::max(0, editor.editPattern), 0, SequencerBank::patterns - 1, encoding, value);
                if (pattern != editor.editPattern)
                    editor.selectPatternKey(pattern, false);
                return;
            }
            default: break;
        }

        touch(place);
        switch (place)
        {
            case midi::Place::knob1: case midi::Place::knob2: case midi::Place::knob3:
            {
                const auto list = knobsOf(editor.selectedLane);
                const auto i = static_cast<std::size_t>(midi::knobOf(place) - 1);
                if (i < list.size())
                {
                    const int k = list[i];
                    const float v = moved(p.project().kit.knob[static_cast<std::size_t>(k)], encoding, value);
                    p.editKit([k, v](KitValues& kit) { kit.knob[static_cast<std::size_t>(k)] = v; });
                }
                break;
            }
            case midi::Place::knob4:
            {
                const int length = movedCount(editor.laneShown(editor.selectedLane).length, 1, SequencerLane::steps, encoding, value);
                editor.editLane(editor.selectedLane, [length](SequencerLane& l) { l.length = length; });
                break;
            }
            case midi::Place::knob6:
            {
                const double tempo = encoding == midi::Encoding::absolute ? 40.0 + value / 127.0 * 200.0
                                                                          : p.project().play.tempo + 0.5 * midi::relativeSteps(encoding, value);
                p.editPlay([tempo](PlaySettings& s) { s.tempo = juce::jlimit(30.0, 300.0, std::round(tempo * 2.0) / 2.0); });
                break;
            }
            case midi::Place::knob7:
            {
                const float swing = 0.5f + 0.25f * moved((p.project().play.swing - 0.5f) / 0.25f, encoding, value, 1.0f / 25.0f);
                p.editPlay([swing](PlaySettings& s) { s.swing = std::round(swing * 100.0f) / 100.0f; });
                break;
            }
            case midi::Place::knob8:
            {
                // An audio taper, as CoolSynth's faders (D184): the cube of the position, the top 0 dB.
                const float old = std::cbrt(std::pow(10.0f, p.project().kit.volumeDb / 20.0f));
                const float position = moved(old, encoding, value);
                const float db = position <= 0.001f ? -60.0f : std::max(-60.0f, 60.0f * std::log10(position));
                p.editKit([db](KitValues& kit) { kit.volumeDb = db; });
                break;
            }
            case midi::Place::fader1: case midi::Place::fader2: case midi::Place::fader3: case midi::Place::fader4:
            {
                static constexpr std::array<const char*, 4> levels { "bdLevel", "sdLevel", "chLevel", "ohLevel" };
                const int k = knobIndex(levels[static_cast<std::size_t>(midi::faderOf(place))]);
                if (k >= 0)
                {
                    const float v = moved(p.project().kit.knob[static_cast<std::size_t>(k)], encoding, value);
                    p.editKit([k, v](KitValues& kit) { kit.knob[static_cast<std::size_t>(k)] = v; });
                }
                break;
            }
            default: break;
        }
    }
}
