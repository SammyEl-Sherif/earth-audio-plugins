#pragma once

#include <algorithm>
#include <cmath>

namespace animalaudio::dsp
{
/**
 * Trapezoidal (zero-delay feedback) state-variable filter, one per channel (Simper / Zavalishin form).
 * Stable under fast cutoff and resonance modulation, so coefficients can be updated every few samples.
 * Allocation free.
 */
class Svf
{
public:
    static constexpr int kMaxChannels = 2;

    struct Outputs
    {
        float low, band, high;
    };

    void reset() noexcept
    {
        for (auto& s : state)
            s = {0.0f, 0.0f};
    }

    /** `q` 0.5 is gentle, 0.707 Butterworth, 10+ rings. Cutoff is clamped below Nyquist. */
    void setCoefficients(double cutoffHz, double q, double sampleRate) noexcept
    {
        const double nyquistSafe = 0.49 * sampleRate;
        const double fc = std::clamp(cutoffHz, 10.0, nyquistSafe);
        const double g = std::tan(3.141592653589793 * fc / sampleRate);
        k = static_cast<float>(1.0 / std::max(0.05, q));
        a1 = static_cast<float>(1.0 / (1.0 + g * (g + k)));
        a2 = static_cast<float>(g) * a1;
        a3 = static_cast<float>(g) * a2;
    }

    Outputs process(int channel, float x) noexcept
    {
        auto& s = state[std::clamp(channel, 0, kMaxChannels - 1)];
        const float v3 = x - s.ic2;
        const float v1 = a1 * s.ic1 + a2 * v3;
        const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        return {v2, v1, x - k * v1 - v2};
    }

    float lowpass(int channel, float x) noexcept { return process(channel, x).low; }

private:
    struct State
    {
        float ic1, ic2;
    };
    State state[kMaxChannels] = {};
    float k = 1.414f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
};
} // namespace animalaudio::dsp
