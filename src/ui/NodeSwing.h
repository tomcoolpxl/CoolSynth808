#pragma once

#include <algorithm>
#include <array>
#include <cmath>
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
    };

    /** Made from the recordings the sheets play (LidView.cpp). */
    PanelSwings panelSwings();
}
