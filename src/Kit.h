#pragma once

#include <array>

#include "synth/v3/SoundUnit.h"

/*
    The kit: the TR-808's eleven circuits as the engine's three 808 generators in one layer played in the Kit mode,
    set as CoolSynth's stock 808 Kit is (D260): every knob at 12 o'clock and every Level full, as page 14 of the
    service notes measures each voice. The panel's knobs are the 808's own, plus the Accent and the Volume.

    Each lane strikes one instrument by its General MIDI key. Three lanes have a switch, as on the machine: the toms or
    the congas, the rim shot or the claves, the hand clap or the maracas.
*/
namespace cs808
{
    inline constexpr int laneCount = 11;

    struct Instrument
    {
        const char* code;
        const char* name;
        const char* altCode; // the switched instrument, or nullptr
        const char* altName;
        int note;
        int altNote;
    };

    inline constexpr std::array<Instrument, laneCount> instruments { {
        { "BD", "Bass drum", nullptr, nullptr, 36, -1 },
        { "SD", "Snare drum", nullptr, nullptr, 38, -1 },
        { "LT", "Low tom", "LC", "Low conga", 43, 64 },
        { "MT", "Mid tom", "MC", "Mid conga", 47, 63 },
        { "HT", "High tom", "HC", "High conga", 50, 62 },
        { "RS", "Rim shot", "CL", "Claves", 37, 75 },
        { "CP", "Hand clap", "MA", "Maracas", 39, 70 },
        { "CB", "Cowbell", nullptr, nullptr, 56, -1 },
        { "CY", "Cymbal", nullptr, nullptr, 49, -1 },
        { "OH", "Open hat", nullptr, nullptr, 46, -1 },
        { "CH", "Closed hat", nullptr, nullptr, 42, -1 },
    } };

    /** The lane an instrument code names (either of a pair's), or -1; `alt` says it is the pair's other one. */
    [[nodiscard]] int laneOfCode(const char* code, bool& alt) noexcept;
    /** The lane a key strikes, or -1; `alt` as above. */
    [[nodiscard]] int laneOfNote(int note, bool& alt) noexcept;

    enum Generator { drums = 0, snareClap = 1, metal = 2 };

    /** One knob of the panel: the lane it sits on (-1 the accent column) and the generator parameter it sets. */
    struct KnobSpec
    {
        const char* id;
        const char* label;
        int lane;
        int generator; // -1: the Accent, on all three generators
        int param;     // index in that generator's descriptor
    };

    inline constexpr int knobCount = 22;
    inline constexpr std::array<KnobSpec, knobCount> knobs { {
        { "accent", "Accent", -1, -1, -1 },
        { "bdLevel", "Level", 0, drums, 0 },
        { "bdTone", "Tone", 0, drums, 1 },
        { "bdDecay", "Decay", 0, drums, 2 },
        { "sdLevel", "Level", 1, snareClap, 0 },
        { "sdTone", "Tone", 1, snareClap, 1 },
        { "sdSnappy", "Snappy", 1, snareClap, 2 },
        { "ltLevel", "Level", 2, drums, 3 },
        { "ltTune", "Tuning", 2, drums, 4 },
        { "mtLevel", "Level", 3, drums, 5 },
        { "mtTune", "Tuning", 3, drums, 6 },
        { "htLevel", "Level", 4, drums, 7 },
        { "htTune", "Tuning", 4, drums, 8 },
        { "rsLevel", "Level", 5, drums, 9 },
        { "cpLevel", "Level", 6, snareClap, 3 },
        { "cbLevel", "Level", 7, metal, 0 },
        { "cyLevel", "Level", 8, metal, 1 },
        { "cyTone", "Tone", 8, metal, 2 },
        { "cyDecay", "Decay", 8, metal, 3 },
        { "ohLevel", "Level", 9, metal, 4 },
        { "ohDecay", "Decay", 9, metal, 5 },
        { "chLevel", "Level", 10, metal, 6 },
    } };
    // The Accent's index in each generator's descriptor: the 808 Drums', the Snare & Clap's, the Metal's.
    inline constexpr std::array<int, 3> accentParam { 10, 4, 7 };

    /** A knob's value in the stock kit: Levels full, everything else at 12 o'clock. */
    [[nodiscard]] float knobDefault(int index) noexcept;

    /** Every knob, 0..1 as the panel turns it, the pair switches and the volume. */
    struct KitValues
    {
        std::array<float, knobCount> knob {};
        std::array<bool, laneCount> alt {}; // conga, claves, maracas
        float volumeDb = 0.0f;
        KitValues() noexcept;
    };

    /** The key a lane strikes with its switch as set. */
    [[nodiscard]] int laneNote(int lane, const KitValues& kit) noexcept;

    /** The engine's settings for the kit: layer A the three generators in the Kit mode, layer B silent. */
    void fillEngineSettings(const KitValues& kit, coolsynth::synth::v3::EngineSettings& out) noexcept;
}
