#include <animalaudio/dsp/FdnReverb.h>
#include <animalaudio/testing/SignalProbe.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using animalaudio::dsp::FdnReverb;

namespace
{
/** Energy of an impulse response in 100 ms windows. */
std::vector<double> decayCurve(float seconds, double rate, int windows)
{
    FdnReverb reverb;
    reverb.prepare(rate);
    reverb.setDecaySeconds(seconds);
    reverb.setDamping(0.0f);
    const int window = static_cast<int>(rate * 0.1);
    std::vector<double> energy(static_cast<std::size_t>(windows), 0.0);
    for (int i = 0; i < window * windows; ++i)
    {
        float l = 0.0f, r = 0.0f;
        const float x = i == 0 ? 1.0f : 0.0f;
        reverb.process(x, x, l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        energy[static_cast<std::size_t>(i / window)] += double(l) * l + double(r) * r;
    }
    return energy;
}
} // namespace

TEST_CASE("FdnReverb tail decays at the requested RT60")
{
    for (const float rt60 : {0.8f, 2.5f})
    {
        const auto energy = decayCurve(rt60, 48000.0, 20);
        // Compare two windows well inside the tail: 60 dB per rt60 seconds.
        const double measuredDb = 10.0 * std::log10(energy[4] / energy[14]);
        const double expectedDb = 60.0 * 1.0 / rt60;
        REQUIRE(std::abs(measuredDb - expectedDb) < expectedDb * 0.25 + 1.0);
    }
}

TEST_CASE("FdnReverb is wet only, silent on silence and survives a reset")
{
    FdnReverb reverb;
    reverb.prepare(44100.0);
    reverb.setDecaySeconds(4.0f);
    float l = 1.0f, r = 1.0f;
    for (int i = 0; i < 1000; ++i)
        reverb.process(0.0f, 0.0f, l, r);
    REQUIRE(l == 0.0f);
    REQUIRE(r == 0.0f);

    for (int i = 0; i < 4410; ++i)
        reverb.process(0.5f, -0.5f, l, r);
    reverb.reset();
    reverb.process(0.0f, 0.0f, l, r);
    REQUIRE(l == 0.0f);
    REQUIRE(r == 0.0f);
}

TEST_CASE("FdnReverb spreads a mono input into a decorrelated stereo tail")
{
    FdnReverb reverb;
    reverb.prepare(48000.0);
    reverb.setDecaySeconds(2.0f);
    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (int i = 0; i < 48000; ++i)
    {
        float l = 0.0f, r = 0.0f;
        const float x = i < 4800 ? std::sin(i * 0.05f) * 0.5f : 0.0f;
        reverb.process(x, x, l, r);
        if (i > 4800)
        {
            ll += double(l) * l;
            rr += double(r) * r;
            lr += double(l) * r;
        }
    }
    REQUIRE(ll > 1.0e-3);
    REQUIRE(rr > 1.0e-3);
    REQUIRE(std::abs(lr) / std::sqrt(ll * rr) < 0.6);
}
