#pragma once

#include <animalaudio/ui/Theme.h>

namespace cosmos::ui
{
/**
 * Retro space: a ringed planet striped like a 70s sunset (mascot), machined knob caps with a cream index line,
 * a starfield over a synthwave horizon grid (ambient) and faint stars across the panels (decoration).
 */
class CosmosMotif : public animalaudio::ui::Motif
{
public:
    void drawMascot(juce::Graphics&, juce::Rectangle<float> area, const animalaudio::ui::Palette&) const override;
    void drawKnobCap(juce::Graphics&, juce::Point<float> centre, float radius, float angle,
                     const animalaudio::ui::Palette&) const override;
    void drawAmbient(juce::Graphics&, juce::Rectangle<float> area, double timeSeconds,
                     const animalaudio::ui::Palette&) const override;
    void drawDecoration(juce::Graphics&, juce::Rectangle<float> area, const animalaudio::ui::Palette&) const override;
};

/** Text as a path, filled with the sunset ramp top to bottom and cut by thin horizontal stripes. */
void drawSunsetText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
                    juce::Colour cutColour);
} // namespace cosmos::ui
