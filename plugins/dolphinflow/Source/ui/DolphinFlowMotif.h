#pragma once

#include <animalaudio/ui/Theme.h>

namespace dolphinflow::ui
{
/** A dolphin mid-leap, arcing up and to the left, fitted into `area` (fins and fluke included). */
juce::Path makeDolphinBody(juce::Rectangle<float> area);

/** Bottlenose dolphin leaping from a wave, a dorsal-fin knob pointer, rolling swell. */
class DolphinFlowMotif : public animalaudio::ui::Motif
{
public:
    void drawMascot(juce::Graphics&, juce::Rectangle<float> area, const animalaudio::ui::Palette&) const override;
    void drawKnobCap(juce::Graphics&, juce::Point<float> centre, float radius, float angle,
                     const animalaudio::ui::Palette&) const override;
    void drawAmbient(juce::Graphics&, juce::Rectangle<float> area, double timeSeconds,
                     const animalaudio::ui::Palette&) const override;
};
} // namespace dolphinflow::ui
