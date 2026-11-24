#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace cs808
{
    /**
        A node of a drum's circuit as a hit moves it (D280, D319): how far it is from rest (0..1 of its own peak, with an
        afterglow of 60 ms so a 1 ms trigger and an audio-rate ring are seen at the screen's rate) and which way, at the
        recording's times; past the recording's end the last value fades as the recording was fading.
    */
    struct NodeSwing
    {
        std::vector<float> ms, level01, sign;
        float tailTau = 60.0f;

        [[nodiscard]] bool bound() const noexcept { return ms.size() > 1; }

        /** Its magnitude at el ms after the strike; the sign of the node's swing then into `s`. */
        [[nodiscard]] float at(double el, float& s) const noexcept
        {
            s = 1.0f;
            if (! bound())
                return 0.0f;
            if (el <= ms.front())
            {
                s = sign.front();
                return level01.front();
            }
            if (el >= ms.back())
            {
                s = sign.back();
                return level01.back() * static_cast<float>(std::exp(-(el - ms.back()) / tailTau));
            }
            const auto it = std::upper_bound(ms.begin(), ms.end(), static_cast<float>(el));
            const auto i = static_cast<std::size_t>(std::distance(ms.begin(), it)) - 1;
            const float fr = static_cast<float>((el - ms[i]) / std::max(1.0e-6f, ms[i + 1] - ms[i]));
            s = sign[i];
            return level01[i] + fr * (level01[i + 1] - level01[i]);
        }

        /** From a recorded series at its times: unbound when the node hardly moves. */
        static NodeSwing of(const std::vector<double>& times, const std::vector<float>& x)
        {
            NodeSwing out;
            const auto n = std::min(times.size(), x.size());
            if (n < 2)
                return out;
            float peak = 0.0f;
            for (std::size_t i = 0; i < n; ++i)
                peak = std::max(peak, std::abs(x[i] - x[0]));
            if (peak < 1.0e-4f)
                return out;
            out.ms.resize(n);
            out.level01.resize(n);
            out.sign.resize(n);
            float level = 0.0f, sign = 1.0f;
            for (std::size_t i = 0; i < n; ++i)
            {
                const float d = x[i] - x[0];
                const float target = std::abs(d) / peak;
                const float glow = i == 0 ? 0.0f : level * std::exp(-static_cast<float>(times[i] - times[i - 1]) / 60.0f);
                if (target >= glow)
                {
                    level = target;
                    sign = d >= 0.0f ? 1.0f : -1.0f;
                }
                else
                    level = glow;
                out.ms[i] = static_cast<float>(times[i]);
                out.level01[i] = level;
                out.sign[i] = sign;
            }
            // The fade past the end: as the last 100 ms of the recording were fading, or the afterglow's.
            const float end = out.ms.back();
            std::size_t k = n - 1;
            while (k > 0 && out.ms[k] > end - 100.0f)
                --k;
            const float earlier = out.level01[k], last = out.level01.back();
            out.tailTau = earlier > last && last > 1.0e-4f ? std::clamp((end - out.ms[k]) / std::log(earlier / last), 20.0f, 3000.0f) : 60.0f;
            return out;
        }
    };

    /** The lifted lid's copper (D319): for each drum (0 bass drum to 10 closed hat), its trigger, envelope, core and
        output nodes as its recording moves them. */
    struct PanelSwings
    {
        enum Role : int { trigger = 0, envelope, core, output, roleCount };
        std::array<std::array<NodeSwing, roleCount>, 11> lanes;
        std::array<std::array<std::string, roleCount>, 11> names; // the nodes, as the recordings name them
    };

    /** Made from the recordings the sheets play (LidView.cpp). */
    PanelSwings panelSwings();

    /**
        The drums' board nodes as the engine plays them (D320): for each lane (0 bass drum to 10 closed hat) and each of
        its nodes, how far it has swung from where it stood when struck, on its recording's scale (1 its recording's
        peak), with the same 60 ms afterglow and sign as the recordings'. Live while the engine reports.
    */
    class LiveBoard
    {
    public:
        static constexpr int lanes = 11, slots = 9;
        struct Node { std::string name; float recordedRest = 0.0f, recordedPeak = 0.0f, recordedSign = 1.0f, level = 0.0f, sign = 1.0f; };

        /** Each frame: the engine's report count, and per lane its hit count and each node's largest swing since. */
        void update(unsigned int reports, const std::array<unsigned int, lanes>& hits,
                    const std::array<std::array<float, slots>, lanes>& swings, double nowMs)
        {
            const float dt = static_cast<float>(std::clamp(nowMs - lastNow, 0.0, 200.0));
            lastNow = nowMs;
            if (reports != lastReports)
            {
                lastReports = reports;
                reportedAt = nowMs;
            }
            for (std::size_t l = 0; l < static_cast<std::size_t>(lanes); ++l)
            {
                auto& lane = nodes[l];
                if (hits[l] != lastHits[l])
                {
                    lastHits[l] = hits[l];
                    for (auto& n : lane)
                        n.level = 0.0f;
                }
                for (std::size_t k = 0; k < static_cast<std::size_t>(slots); ++k)
                {
                    auto& n = lane[k];
                    if (n.recordedPeak <= 0.0f)
                        continue;
                    const float d = swings[l][k], target = std::min(1.0f, std::abs(d) / n.recordedPeak);
                    const float glow = n.level * std::exp(-dt / 60.0f);
                    if (target >= glow)
                    {
                        n.level = target;
                        n.sign = d >= 0.0f ? 1.0f : -1.0f;
                    }
                    else
                        n.level = glow;
                }
            }
        }

        /** Whether the engine is playing (it reported in the last half second). */
        [[nodiscard]] bool live(double nowMs) const noexcept { return nowMs - reportedAt < 500.0; }
        /** A node's swing now, signed, -1..1 of its recording's peak (0 where it has none). */
        [[nodiscard]] float value(int lane, int slot) const noexcept
        {
            if (lane < 0 || lane >= lanes || slot < 0 || slot >= slots)
                return 0.0f;
            const auto& n = nodes[static_cast<std::size_t>(lane)][static_cast<std::size_t>(slot)];
            return n.sign * n.level;
        }
        /** Which of a lane's nodes has this name, or -1. */
        [[nodiscard]] int slotOf(int lane, const std::string& name) const
        {
            if (lane < 0 || lane >= lanes)
                return -1;
            for (int k = 0; k < slots; ++k)
                if (! name.empty() && nodes[static_cast<std::size_t>(lane)][static_cast<std::size_t>(k)].name == name)
                    return k;
            return -1;
        }

        std::array<std::array<Node, slots>, lanes> nodes;

    private:
        std::array<unsigned int, lanes> lastHits {};
        unsigned int lastReports = 0;
        double reportedAt = -1.0e9, lastNow = 0.0;
    };

    /** The engine's board nodes by lane, their recordings' peaks (LidView.cpp). */
    LiveBoard makeLiveBoard();
}
