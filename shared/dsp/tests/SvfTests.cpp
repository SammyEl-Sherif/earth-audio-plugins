#include <animalaudio/dsp/Svf.h>
#include <animalaudio/testing/SignalProbe.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using animalaudio::dsp::Svf;
using animalaudio::testing::rms;
using animalaudio::testing::sineWave;

namespace
{
double lowpassGain(double hz, double cutoff, double q)
{
    constexpr double rate = 48000.0;
    Svf filter;
    filter.setCoefficients(cutoff, q, rate);
    const auto in = sineWave(hz, rate, 48000);
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
        out[i] = filter.lowpass(0, in[i]);
    return rms(out, 24000, 48000) / rms(in, 24000, 48000);
}
} // namespace

TEST_CASE("Svf lowpass passes the band below cutoff and cuts above")
{
    REQUIRE(std::abs(lowpassGain(100.0, 2000.0, 0.707) - 1.0) < 0.02);
    REQUIRE(std::abs(lowpassGain(2000.0, 2000.0, 0.707) - 0.707) < 0.03);
    REQUIRE(lowpassGain(16000.0, 2000.0, 0.707) < 0.03); // 12 dB / octave
}

TEST_CASE("Svf resonance peaks at the cutoff")
{
    REQUIRE(lowpassGain(1000.0, 1000.0, 8.0) > 6.0);
}

TEST_CASE("Svf stays bounded while the cutoff sweeps fast")
{
    constexpr double rate = 44100.0;
    Svf filter;
    const auto in = sineWave(220.0, rate, 44100, 0.8f);
    float peak = 0.0f;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        if (i % 8 == 0)
            filter.setCoefficients(20000.0 * std::pow(0.001, 0.5 + 0.5 * std::sin(i * 0.01)), 4.0, rate);
        const float y = filter.lowpass(1, in[i]);
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::abs(y));
    }
    REQUIRE(peak < 8.0f);
}
