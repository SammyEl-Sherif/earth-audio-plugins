#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace animalaudio::dsp
{
/**
 * Stereo algorithmic reverb: two input allpass diffusers per channel into an 8-line feedback delay network
 * with a Hadamard mix, per-line damping and slowly modulated line lengths (no metallic ringing on long
 * tails). Output is 100% wet. Decay is RT60 in seconds; `damping` 0 = bright, 1 = dark.
 * `prepare` allocates; everything else is allocation free and safe on the audio thread.
 */
class FdnReverb
{
public:
    static constexpr int kLines = 8;

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        const double scale = sampleRate / 1000.0;
        for (int i = 0; i < kLines; ++i)
        {
            lineLength[i] = std::max(8.0, kLineMs[i] * scale);
            lines[i].assign(static_cast<std::size_t>(lineLength[i] + 2.0 * kModSamples * scale / 48.0 + 8.0), 0.0f);
        }
        for (int c = 0; c < 2; ++c)
            for (int d = 0; d < kDiffusers; ++d)
            {
                const auto length = static_cast<std::size_t>(std::max(4.0, kDiffuserMs[c][d] * scale));
                diffusers[c][d].buffer.assign(length, 0.0f);
                diffusers[c][d].index = 0;
            }
        modDepth = kModSamples * scale / 48.0;
        reset();
        updateGains();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kLines; ++i)
        {
            std::fill(lines[i].begin(), lines[i].end(), 0.0f);
            writeIndex[i] = 0;
            lowState[i] = 0.0f;
        }
        for (auto& channel : diffusers)
            for (auto& d : channel)
                std::fill(d.buffer.begin(), d.buffer.end(), 0.0f);
        lfoPhase = 0.0;
        modCounter = 0;
        for (int i = 0; i < kLines; ++i)
            lineDelay[i] = lineLength[i];
    }

    void setDecaySeconds(float seconds) noexcept
    {
        const float clamped = std::clamp(seconds, 0.1f, 60.0f);
        if (std::abs(clamped - decaySeconds) < 1.0e-4f)
            return;
        decaySeconds = clamped;
        updateGains();
    }

    void setDamping(float amount) noexcept
    {
        const float clamped = std::clamp(amount, 0.0f, 0.98f);
        if (std::abs(clamped - damping) < 1.0e-4f)
            return;
        damping = clamped;
        updateGains();
    }

    float getDecaySeconds() const noexcept { return decaySeconds; }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        if (lines[0].empty())
        {
            outL = outR = 0.0f;
            return;
        }
        const float left = diffuse(0, inL);
        const float right = diffuse(1, inR);

        // The modulation is slow (well under 1 Hz), so line lengths are refreshed every 16 samples.
        if ((modCounter++ & 15) == 0)
        {
            lfoPhase += 16.0 * kModHz / sampleRate;
            if (lfoPhase >= 1.0)
                lfoPhase -= 1.0;
            for (int i = 0; i < kLines; ++i)
            {
                const double lfo = std::sin(6.283185307179586 * (lfoPhase + 0.125 * i));
                lineDelay[i] = lineLength[i] + modDepth * (1.0 + lfo) * ((i & 1) ? 1.0 : 0.7);
            }
        }

        std::array<float, kLines> taps{};
        for (int i = 0; i < kLines; ++i)
            taps[static_cast<std::size_t>(i)] = readLine(i, lineDelay[i]);

        outL = 0.0f;
        outR = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float sign = (i & 2) ? -1.0f : 1.0f;
            if (i & 1)
                outR += sign * taps[static_cast<std::size_t>(i)];
            else
                outL += sign * taps[static_cast<std::size_t>(i)];
        }
        outL *= 0.5f;
        outR *= 0.5f;

        hadamard(taps);
        for (int i = 0; i < kLines; ++i)
        {
            float v = taps[static_cast<std::size_t>(i)] * lineGain[i];
            lowState[i] = v + lowCoeff * (lowState[i] - v);
            v = lowState[i] + ((i & 1) ? right : left) * 0.5f;
            auto& line = lines[i];
            line[static_cast<std::size_t>(writeIndex[i])] = v;
            if (++writeIndex[i] >= static_cast<int>(line.size()))
                writeIndex[i] = 0;
        }
    }

private:
    static constexpr int kDiffusers = 2;
    static constexpr double kLineMs[kLines] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1};
    static constexpr double kDiffuserMs[2][kDiffusers] = {{4.77, 3.59}, {5.13, 3.31}};
    static constexpr double kModSamples = 12.0; // at 48 kHz
    static constexpr double kModHz = 0.37;

    struct Allpass
    {
        std::vector<float> buffer;
        int index = 0;
    };

    float diffuse(int channel, float x) noexcept
    {
        for (auto& d : diffusers[channel])
        {
            if (d.buffer.empty())
                continue;
            const float delayed = d.buffer[static_cast<std::size_t>(d.index)];
            const float v = x + 0.6f * delayed;
            d.buffer[static_cast<std::size_t>(d.index)] = v;
            if (++d.index >= static_cast<int>(d.buffer.size()))
                d.index = 0;
            x = delayed - 0.6f * v;
        }
        return x;
    }

    float readLine(int i, double delay) const noexcept
    {
        const auto& line = lines[i];
        const int size = static_cast<int>(line.size());
        double position = static_cast<double>(writeIndex[i]) - delay;
        while (position < 0.0)
            position += size;
        const int a = static_cast<int>(position);
        const float t = static_cast<float>(position - a);
        const int b = (a + 1) % size;
        return line[static_cast<std::size_t>(a % size)] * (1.0f - t) + line[static_cast<std::size_t>(b)] * t;
    }

    static void hadamard(std::array<float, kLines>& v) noexcept
    {
        for (int span = 1; span < kLines; span *= 2)
            for (int i = 0; i < kLines; i += 2 * span)
                for (int j = i; j < i + span; ++j)
                {
                    const float a = v[static_cast<std::size_t>(j)];
                    const float b = v[static_cast<std::size_t>(j + span)];
                    v[static_cast<std::size_t>(j)] = a + b;
                    v[static_cast<std::size_t>(j + span)] = a - b;
                }
        constexpr float norm = 0.35355339f; // 1 / sqrt(8)
        for (auto& x : v)
            x *= norm;
    }

    void updateGains() noexcept
    {
        for (int i = 0; i < kLines; ++i)
            lineGain[i] = static_cast<float>(std::pow(10.0, -3.0 * lineLength[i] / (decaySeconds * sampleRate)));
        lowCoeff = damping;
    }

    double sampleRate = 44100.0;
    std::vector<float> lines[kLines];
    double lineLength[kLines] = {};
    int writeIndex[kLines] = {};
    float lineGain[kLines] = {};
    float lowState[kLines] = {};
    Allpass diffusers[2][kDiffusers];
    double modDepth = 0.0;
    double lfoPhase = 0.0;
    double lineDelay[kLines] = {};
    unsigned modCounter = 0;
    float decaySeconds = 2.0f;
    float damping = 0.3f;
    float lowCoeff = 0.3f;
};
} // namespace animalaudio::dsp
