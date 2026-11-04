#include "CosmosTheme.h"

#include "CosmosMotif.h"

#include <algorithm>

namespace cosmos::ui
{
namespace
{
const juce::Colour kGold(0xffffd25a);
const juce::Colour kOrange(0xffff8a4c);
const juce::Colour kPink(0xffff5fa2);
const juce::Colour kViolet(0xff9b7bff);
const juce::Colour kCyan(0xff52e5f2);
} // namespace

animalaudio::ui::Palette makePalette()
{
    // Deep-space navy with cream type and a phosphor-cyan accent; the sunset ramp supplies the warm colours.
    animalaudio::ui::Palette palette;
    palette.background = juce::Colour(0xff07080f);
    palette.panel = juce::Colour(0xff0e1120);
    palette.panelRaised = juce::Colour(0xff191d33);
    palette.outline = juce::Colour(0xff282e4a);
    palette.text = juce::Colour(0xfff3ecdc);
    palette.textDim = juce::Colour(0xff8e92ae);
    palette.accent = kCyan;
    palette.accentAlt = kOrange;
    palette.onAccent = juce::Colour(0xff04161a);
    palette.detail = kPink;
    palette.ambient = juce::Colour(0xff04050b);
    return palette;
}

juce::Colour sunset(float t)
{
    static const juce::Colour stops[] = {kGold, kOrange, kPink, kViolet, kCyan};
    const float scaled = std::clamp(t, 0.0f, 1.0f) * 4.0f;
    const int index = std::min(3, static_cast<int>(scaled));
    return stops[index].interpolatedWith(stops[index + 1], scaled - static_cast<float>(index));
}

juce::Colour familyColour(dsp::Family family)
{
    switch (family)
    {
    case dsp::Family::MicroLoop:
        return kOrange;
    case dsp::Family::Granules:
        return kCyan;
    case dsp::Family::Glitch:
        return kPink;
    case dsp::Family::MultiDelay:
    case dsp::Family::Count:
        break;
    }
    return kGold;
}

animalaudio::ui::Theme makeTheme()
{
    animalaudio::ui::Theme theme;
    theme.palette = makePalette();
    theme.name = "COSMOS";
    theme.tagline = juce::String(juce::CharPointer_UTF8("GRANULAR  \xc2\xb7  LOOPS  \xc2\xb7  SPACE"));
    theme.motif = std::make_shared<CosmosMotif>();
    return theme;
}
} // namespace cosmos::ui
