#pragma once

#include <animalaudio/ui/Theme.h>

namespace swan::ui
{
/** The swan's body, S-curved neck and head, facing left, fitted into `area` (bill excluded). */
juce::Path makeSwanBody(juce::Rectangle<float> area);

/** Mute swan: white body with an S neck and orange bill, a neck-shaped knob pointer, lake ripples. */
class SwanMotif : public animalaudio::ui::Motif
{
public:
    void drawMascot(juce::Graphics&, juce::Rectangle<float> area, const animalaudio::ui::Palette&) const override;
    void drawKnobCap(juce::Graphics&, juce::Point<float> centre, float radius, float angle,
                     const animalaudio::ui::Palette&) const override;
    void drawAmbient(juce::Graphics&, juce::Rectangle<float> area, double timeSeconds,
                     const animalaudio::ui::Palette&) const override;
};
} // namespace swan::ui
