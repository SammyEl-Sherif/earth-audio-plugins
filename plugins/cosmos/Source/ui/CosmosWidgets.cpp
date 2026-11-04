#include "CosmosWidgets.h"

#include <cmath>

namespace cosmos::ui
{
namespace
{
juce::Rectangle<float> disc(juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}
} // namespace

CosmosKnob::CosmosKnob(const animalaudio::ui::Theme& themeToUse, juce::Colour knobColour, int segmentCount)
    : animalaudio::ui::RotaryKnob(themeToUse.palette)
    , theme(themeToUse)
    , colour(knobColour)
    , segments(std::max(2, segmentCount))
{
}

void CosmosKnob::setColour(juce::Colour newColour)
{
    if (newColour == colour)
        return;
    colour = newColour;
    repaint();
}

void CosmosKnob::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    const float outer = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float proportion = static_cast<float>(valueToProportionOfLength(getValue()));
    const float angle = kStartAngle + proportion * (kEndAngle - kStartAngle);
    const bool small = outer < 34.0f;
    const float ledLength = std::max(3.0f, outer * (small ? 0.16f : 0.13f));
    const float ledRadius = outer - ledLength * 0.5f - 1.0f;
    const float ledWidth = std::max(1.6f, outer * (small ? 0.07f : 0.05f));
    const bool enabled = isEnabled();

    for (int i = 0; i < segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments - 1);
        const float a = kStartAngle + t * (kEndAngle - kStartAngle);
        const bool lit = t <= proportion + 1.0e-4f;
        const float s = std::sin(a);
        const float c = -std::cos(a);
        const juce::Point<float> from(centre.x + (ledRadius - ledLength * 0.5f) * s,
                                      centre.y + (ledRadius - ledLength * 0.5f) * c);
        const juce::Point<float> to(centre.x + (ledRadius + ledLength * 0.5f) * s,
                                    centre.y + (ledRadius + ledLength * 0.5f) * c);
        juce::Path tick;
        tick.startNewSubPath(from);
        tick.lineTo(to);
        const juce::PathStrokeType stroke(ledWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (lit && enabled)
        {
            g.setColour(colour.withAlpha(0.22f));
            g.strokePath(tick, juce::PathStrokeType(ledWidth * 3.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            g.setColour(colour.interpolatedWith(colours.text, 0.12f * t));
        }
        else
        {
            g.setColour(lit ? colour.withSaturation(0.15f).withAlpha(0.5f) : colours.outline.withAlpha(0.9f));
        }
        g.strokePath(tick, stroke);
    }

    auto palette = theme.palette;
    palette.accent = enabled ? colour : colour.withSaturation(0.2f);
    theme.motif->drawKnobCap(g, centre, outer - ledLength - (small ? 3.5f : 6.0f), angle, palette);

    if (hasKeyboardFocus(false))
    {
        g.setColour(colour.withAlpha(0.45f));
        g.drawEllipse(bounds.reduced(0.5f), 1.0f);
    }
}

ModeButton::ModeButton(const juce::String& label, const animalaudio::ui::Palette& palette, juce::Colour ledColour)
    : juce::Button(label)
    , colours(palette)
    , led(ledColour)
{
    setTriggeredOnMouseDown(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ModeButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (on || isMouseOver)
    {
        g.setColour((on ? led.withAlpha(0.12f) : colours.text.withAlpha(0.04f)));
        g.fillRoundedRectangle(bounds.reduced(0.5f), bounds.getHeight() * 0.5f);
    }

    const float r = std::min(4.5f, bounds.getHeight() * 0.22f);
    const juce::Point<float> centre(bounds.getX() + 11.0f, bounds.getCentreY());
    if (on)
    {
        g.setGradientFill(juce::ColourGradient(led.withAlpha(0.55f), centre.x, centre.y, led.withAlpha(0.0f),
                                               centre.x + r * 3.2f, centre.y, true));
        g.fillEllipse(disc(centre, r * 3.2f));
    }
    g.setColour(on ? led : colours.background);
    g.fillEllipse(disc(centre, r));
    g.setColour(on ? colours.text.withAlpha(0.8f) : colours.outline.brighter(0.3f));
    g.drawEllipse(disc(centre, r), 1.0f);
    if (on)
    {
        g.setColour(colours.text.withAlpha(0.85f));
        g.fillEllipse(disc({centre.x - r * 0.3f, centre.y - r * 0.3f}, r * 0.3f));
    }

    g.setColour(on ? colours.text : isMouseOver ? colours.text.withAlpha(0.85f) : colours.textDim);
    g.setFont(animalaudio::ui::makeFont(10.5f, true, 0.14f));
    g.drawText(getButtonText().toUpperCase(), bounds.withTrimmedLeft(22.0f), juce::Justification::centredLeft, false);
    if (isButtonDown)
    {
        g.setColour(colours.text.withAlpha(0.06f));
        g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
    }
}

HoldButton::HoldButton(const animalaudio::ui::Palette& palette)
    : juce::Button("HOLD")
    , colours(palette)
{
    setClickingTogglesState(true);
    setTriggeredOnMouseDown(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void HoldButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(3.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const bool on = getToggleState();
    const auto glow = colours.accent;

    if (on)
    {
        g.setGradientFill(juce::ColourGradient(glow.withAlpha(0.45f), centre.x, centre.y, glow.withAlpha(0.0f),
                                               centre.x + radius, centre.y, true));
        g.fillEllipse(disc(centre, radius));
    }

    // Hex-nut chrome bezel.
    const float bezel = radius * 0.86f;
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const float a =
            juce::MathConstants<float>::pi / 3.0f * static_cast<float>(i) + juce::MathConstants<float>::pi / 6.0f;
        const juce::Point<float> p(centre.x + bezel * std::cos(a), centre.y + bezel * std::sin(a));
        if (i == 0)
            hex.startNewSubPath(p);
        else
            hex.lineTo(p);
    }
    hex.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillPath(hex, juce::AffineTransform::translation(0.0f, 3.0f));
    g.setGradientFill(juce::ColourGradient(colours.text.withAlpha(0.85f), centre.x - bezel, centre.y - bezel,
                                           colours.panelRaised, centre.x + bezel, centre.y + bezel, false));
    g.fillPath(hex);
    g.setColour(colours.background.withAlpha(0.5f));
    g.strokePath(hex, juce::PathStrokeType(1.0f));

    // Dome.
    const float dome = radius * (isButtonDown ? 0.54f : 0.57f);
    g.setGradientFill(juce::ColourGradient(colours.panelRaised.brighter(0.5f), centre.x, centre.y - dome,
                                           colours.background, centre.x, centre.y + dome, false));
    g.fillEllipse(disc(centre, dome));
    const float lens = dome * 0.62f;
    if (on)
    {
        g.setGradientFill(juce::ColourGradient(colours.text, centre.x, centre.y - lens * 0.3f, glow, centre.x,
                                               centre.y + lens, true));
        g.fillEllipse(disc(centre, lens));
    }
    else
    {
        g.setColour(isMouseOver ? colours.outline.brighter(0.4f) : colours.outline);
        g.fillEllipse(disc(centre, lens));
    }
    g.setColour(colours.text.withAlpha(on ? 0.7f : 0.25f));
    g.drawEllipse(disc(centre, lens), 1.0f);
    g.setColour(colours.text.withAlpha(0.35f));
    g.fillEllipse(disc({centre.x - lens * 0.35f, centre.y - lens * 0.4f}, lens * 0.22f));
}
} // namespace cosmos::ui
