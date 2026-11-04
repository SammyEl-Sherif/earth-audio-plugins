#include <animalaudio/dsp/Interpolation.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using animalaudio::dsp::catmullRom;

TEST_CASE("catmullRom hits the inner points and reproduces a ramp")
{
    REQUIRE(catmullRom(3.0f, 1.0f, 5.0f, -2.0f, 0.0f) == Catch::Approx(1.0f));
    REQUIRE(catmullRom(3.0f, 1.0f, 5.0f, -2.0f, 1.0f) == Catch::Approx(5.0f));
    for (const float t : {0.1f, 0.25f, 0.5f, 0.9f})
        REQUIRE(catmullRom(0.0f, 1.0f, 2.0f, 3.0f, t) == Catch::Approx(1.0f + t));
}
