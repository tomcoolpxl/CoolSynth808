#include "Kit.h"

#include <cmath>
#include <cstring>

#include "synth/v3/Modulation.h"
#include "synth/v3/Voice.h"

namespace cs808
{
    namespace v3 = coolsynth::synth::v3;

    namespace
    {
        // The stock kit's own settings (CoolSynth's Studio 1982 bank, "808 Kit", D260): each generator at 0.8, the
        // amplifier open from the first sample and never released, the master at +3.5 dB.
        constexpr float generatorLevel = 0.8f;
        constexpr float stockMasterDb = 3.5f;

        v3::GeneratorSettings generatorOf(int type)
        {
            v3::GeneratorSettings g;
            g.type = type;
            const auto& d = v3::GeneratorSlot::typeTable().descriptor(type);
            for (int i = 0; i < d.usedParams; ++i)
                g.p[static_cast<std::size_t>(i)] = v3::unitsToNormalised(d.params[static_cast<std::size_t>(i)], d.params[static_cast<std::size_t>(i)].def);
            g.level = generatorLevel;
            return g;
        }

        void setUnits(v3::GeneratorSettings& g, int param, float units)
        {
            const auto& d = v3::GeneratorSlot::typeTable().descriptor(g.type);
            g.p[static_cast<std::size_t>(param)] = v3::unitsToNormalised(d.params[static_cast<std::size_t>(param)], units);
        }
    }

    int laneOfCode(const char* code, bool& alt) noexcept
    {
        alt = false;
        if (code == nullptr)
            return -1;
        for (int i = 0; i < laneCount; ++i)
        {
            const auto& inst = instruments[static_cast<std::size_t>(i)];
            if (std::strcmp(inst.code, code) == 0)
                return i;
            if (inst.altCode != nullptr && std::strcmp(inst.altCode, code) == 0)
            {
                alt = true;
                return i;
            }
        }
        return -1;
    }

    int laneOfNote(int note, bool& alt) noexcept
    {
        alt = false;
        for (int i = 0; i < laneCount; ++i)
        {
            const auto& inst = instruments[static_cast<std::size_t>(i)];
            if (inst.note == note)
                return i;
            if (inst.altNote == note)
            {
                alt = true;
                return i;
            }
        }
        // The engine's other keys for the same instruments (General MIDI's and the TR-8S's alternates).
        switch (note)
        {
            case 35: return 0;
            case 40: return 1;
            case 41: return 2;
            case 45: return 3;
            case 48: return 4;
            case 44: return 10;
            case 51: case 57: return 8;
            default: return -1;
        }
    }

    float knobDefault(int index) noexcept
    {
        const auto& k = knobs[static_cast<std::size_t>(index)];
        return std::strcmp(k.label, "Level") == 0 ? 1.0f : 0.5f;
    }

    KitValues::KitValues() noexcept
    {
        for (int i = 0; i < knobCount; ++i)
            knob[static_cast<std::size_t>(i)] = knobDefault(i);
    }

    int laneNote(int lane, const KitValues& kit) noexcept
    {
        if (lane < 0 || lane >= laneCount)
            return -1;
        const auto& inst = instruments[static_cast<std::size_t>(lane)];
        return kit.alt[static_cast<std::size_t>(lane)] && inst.altNote >= 0 ? inst.altNote : inst.note;
    }

    void fillEngineSettings(const KitValues& kit, v3::EngineSettings& out) noexcept
    {
        out = {};
        auto& part = out.parts[0];
        part.playMode = v3::PlayMode::kit;
        std::array<v3::GeneratorSettings, 3> g { generatorOf(v3::generatorType::drums808),
                                                 generatorOf(v3::generatorType::snareClap808),
                                                 generatorOf(v3::generatorType::metal808) };
        for (int i = 0; i < knobCount; ++i)
        {
            const auto& k = knobs[static_cast<std::size_t>(i)];
            const float value = std::clamp(kit.knob[static_cast<std::size_t>(i)], 0.0f, 1.0f);
            if (k.generator < 0)
            {
                for (int gen = 0; gen < 3; ++gen)
                    setUnits(g[static_cast<std::size_t>(gen)], accentParam[static_cast<std::size_t>(gen)], value);
            }
            else
                setUnits(g[static_cast<std::size_t>(k.generator)], k.param, value);
        }
        for (int i = 0; i < 3; ++i)
            part.voice.generators[static_cast<std::size_t>(i)] = g[static_cast<std::size_t>(i)];

        v3::Envelope::Parameters amp;
        amp.attack = 0.0f;
        amp.decay = 1.0f;
        amp.sustain = 1.0f;
        amp.release = 10.0f;
        amp.velocitySensitivity = 0.0f;
        part.voice.envelopes[0] = amp;

        out.masterGainLinear = std::pow(10.0f, (stockMasterDb + std::clamp(kit.volumeDb, -60.0f, 6.0f)) / 20.0f);
        if (kit.volumeDb <= -60.0f)
            out.masterGainLinear = 0.0f;
    }
}
