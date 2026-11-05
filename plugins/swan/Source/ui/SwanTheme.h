#pragma once

#include <animalaudio/ui/Theme.h>

namespace swan::ui
{
inline constexpr float kThemeHue = 262.0f; // read by scripts/new_plugin.py to keep plugins' hues apart

animalaudio::ui::Palette makePalette();

/** Palette + name/tagline + motif. Give the plugin its animal by deriving a Motif (see shared/ui/Theme.h). */
animalaudio::ui::Theme makeTheme();
} // namespace swan::ui
