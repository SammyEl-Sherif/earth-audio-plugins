#include "SwanTheme.h"

#include "SwanMotif.h"

namespace swan::ui
{
namespace
{
juce::Colour hsv(float hueDegrees, float saturation, float brightness)
{
    return juce::Colour::fromHSV(hueDegrees / 360.0f, saturation, brightness, 1.0f);
}
} // namespace

animalaudio::ui::Palette makePalette()
{
    // Dusk on a lake: deep violet water, a lilac glow, a white swan with an orange bill.
    animalaudio::ui::Palette p;
    p.background = hsv(kThemeHue, 0.38f, 0.09f);
    p.panel = hsv(kThemeHue, 0.34f, 0.14f);
    p.panelRaised = hsv(kThemeHue, 0.30f, 0.21f);
    p.outline = hsv(kThemeHue, 0.27f, 0.31f);
    p.text = hsv(kThemeHue, 0.05f, 0.98f);
    p.textDim = hsv(kThemeHue, 0.17f, 0.70f);
    p.accent = hsv(kThemeHue, 0.52f, 1.0f);
    p.accentAlt = hsv(312.0f, 0.50f, 1.0f);
    p.onAccent = hsv(kThemeHue, 0.65f, 0.13f);
    p.detail = hsv(24.0f, 0.82f, 1.0f);
    p.ambient = hsv(205.0f, 0.50f, 0.85f);
    return p;
}

animalaudio::ui::Theme makeTheme()
{
    animalaudio::ui::Theme theme;
    theme.palette = makePalette();
    theme.name = "SWAN";
    theme.tagline = "CHORD GLIDER";
    theme.motif = std::make_shared<SwanMotif>();
    return theme;
}
} // namespace swan::ui
