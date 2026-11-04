#pragma once

#include <animalaudio/ui/RotaryKnob.h>
#include <animalaudio/ui/Theme.h>

namespace cosmos::ui
{
/** Knob with a ring of LED segments lit up to the value in the knob's colour, and the motif's machined cap. */
class CosmosKnob : public animalaudio::ui::RotaryKnob
{
public:
    CosmosKnob(const animalaudio::ui::Theme& theme, juce::Colour colour, int segments = 25);

    void setColour(juce::Colour newColour);
    void paint(juce::Graphics& g) override;

private:
    animalaudio::ui::Theme theme;
    juce::Colour colour;
    int segments;
};

/** An effect selector: an LED and a label. Fires on mouse down (see AGENTS.md "Gotchas"). */
class ModeButton : public juce::Button
{
public:
    ModeButton(const juce::String& label, const animalaudio::ui::Palette& palette, juce::Colour led);
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    animalaudio::ui::Palette colours;
    juce::Colour led;
};

/** The big HOLD switch: a chrome bezel around a dome that glows while held. Toggles on mouse down. */
class HoldButton : public juce::Button
{
public:
    HoldButton(const animalaudio::ui::Palette& palette);
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    animalaudio::ui::Palette colours;
};
} // namespace cosmos::ui
