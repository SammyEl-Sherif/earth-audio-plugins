#include "DolphinFlowTheme.h"

#include "DolphinFlowMotif.h"

namespace dolphinflow::ui
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
    // Open water at dusk: deep ocean blue, a bright aqua glow, a silver-grey dolphin and a warm coral highlight.
    animalaudio::ui::Palette p;
    p.background = hsv(kThemeHue, 0.55f, 0.09f);
    p.panel = hsv(kThemeHue, 0.48f, 0.14f);
    p.panelRaised = hsv(kThemeHue, 0.42f, 0.21f);
    p.outline = hsv(kThemeHue, 0.36f, 0.32f);
    p.text = hsv(kThemeHue, 0.06f, 0.98f);
    p.textDim = hsv(kThemeHue, 0.20f, 0.72f);
    p.accent = hsv(198.0f, 0.62f, 1.0f);
    p.accentAlt = hsv(238.0f, 0.48f, 1.0f);
    p.onAccent = hsv(kThemeHue, 0.70f, 0.13f);
    p.detail = hsv(12.0f, 0.62f, 1.0f);
    p.ambient = hsv(176.0f, 0.55f, 0.85f);
    return p;
}

animalaudio::ui::Theme makeTheme()
{
    animalaudio::ui::Theme theme;
    theme.palette = makePalette();
    theme.name = "DOLPHIN FLOW";
    theme.tagline = "CHORD GLIDER";
    theme.motif = std::make_shared<DolphinFlowMotif>();
    return theme;
}
} // namespace dolphinflow::ui
