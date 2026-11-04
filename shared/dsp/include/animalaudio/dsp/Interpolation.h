#pragma once

namespace animalaudio::dsp
{
/** 4-point cubic (Catmull-Rom) between y1 (t = 0) and y2 (t = 1). Reproduces linear ramps, so slowed or
    sped-up reads stay in tune without a harsh image. */
inline float catmullRom(float y0, float y1, float y2, float y3, float t) noexcept
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * y1) + (-y0 + y2) * t + (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2 +
                   (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3);
}
} // namespace animalaudio::dsp
