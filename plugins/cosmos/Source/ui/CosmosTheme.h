#pragma once

#include "../dsp/CosmosModes.h"

#include <animalaudio/ui/Theme.h>

namespace cosmos::ui
{
inline constexpr float kThemeHue = 186.0f; // read by scripts/new_plugin.py to keep plugins' hues apart

animalaudio::ui::Palette makePalette();

/** The retro sunset ramp, gold (0) -> orange -> pink -> violet -> phosphor cyan (1). Knobs walk along it. */
juce::Colour sunset(float t);

/** Each effect family's colour (its LEDs, label and grains). */
juce::Colour familyColour(dsp::Family family);

/** Palette + name/tagline + motif (ringed planet, machined caps, starfield). */
animalaudio::ui::Theme makeTheme();
} // namespace cosmos::ui
