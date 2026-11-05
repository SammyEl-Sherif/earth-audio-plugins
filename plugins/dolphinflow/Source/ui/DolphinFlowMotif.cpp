#include "SwanMotif.h"

#include <animalaudio/ui/Drawing.h>

#include <cmath>

namespace swan::ui
{
namespace
{
using P = juce::Point<float>;

/** Maps the 1x1 drawing box onto `area`, keeping the aspect square and centred. */
juce::AffineTransform unitToArea(juce::Rectangle<float> area)
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre(size, size);
    return juce::AffineTransform::scale(size).translated(box.getX(), box.getY());
}
} // namespace

juce::Path makeSwanBody(juce::Rectangle<float> area)
{
    juce::Path swan;
    swan.startNewSubPath(0.36f, 0.62f);                     // chest, where the neck meets it
    swan.cubicTo(0.30f, 0.74f, 0.38f, 0.88f, 0.56f, 0.88f); // breast to keel
    swan.cubicTo(0.78f, 0.89f, 0.93f, 0.80f, 0.98f, 0.56f); // keel to raised tail
    swan.cubicTo(0.88f, 0.64f, 0.74f, 0.57f, 0.58f, 0.60f); // back
    swan.cubicTo(0.50f, 0.52f, 0.62f, 0.36f, 0.52f, 0.22f); // back of the neck, the S
    swan.cubicTo(0.48f, 0.11f, 0.37f, 0.07f, 0.30f, 0.10f); // crown
    swan.cubicTo(0.26f, 0.11f, 0.24f, 0.12f, 0.22f, 0.13f); // forehead to bill
    swan.lineTo(0.22f, 0.18f);                              // bill base
    swan.cubicTo(0.30f, 0.19f, 0.38f, 0.18f, 0.42f, 0.25f); // throat
    swan.cubicTo(0.47f, 0.35f, 0.35f, 0.47f, 0.36f, 0.62f); // front of the neck
    swan.closeSubPath();
    swan.applyTransform(unitToArea(area));
    return swan;
}

void SwanMotif::drawMascot(juce::Graphics& g, juce::Rectangle<float> area,
                           const animalaudio::ui::Palette& palette) const
{
    const auto toArea = unitToArea(area);
    const auto at = [&](float x, float y) { return P(x, y).transformedBy(toArea); };
    const float size = std::min(area.getWidth(), area.getHeight());

    // Reflection ripples under the keel.
    g.setColour(palette.ambient.withAlpha(0.55f));
    for (int i = 0; i < 2; ++i)
    {
        const float inset = 0.18f + 0.12f * static_cast<float>(i);
        const auto a = at(inset, 0.93f + 0.05f * static_cast<float>(i));
        const auto b = at(1.06f - inset, 0.93f + 0.05f * static_cast<float>(i));
        g.drawLine(a.x, a.y, b.x, b.y, std::max(1.0f, size * 0.025f));
    }

    const auto body = makeSwanBody(area);
    g.setColour(palette.text);
    g.fillPath(body);

    juce::Path wing;
    wing.startNewSubPath(at(0.50f, 0.70f));
    wing.cubicTo(at(0.62f, 0.62f), at(0.80f, 0.62f), at(0.92f, 0.62f));
    wing.startNewSubPath(at(0.54f, 0.77f));
    wing.cubicTo(at(0.66f, 0.70f), at(0.78f, 0.70f), at(0.88f, 0.70f));
    g.setColour(palette.textDim);
    g.strokePath(wing, juce::PathStrokeType(std::max(1.0f, size * 0.022f), juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    juce::Path bill;
    bill.startNewSubPath(at(0.23f, 0.125f));
    bill.lineTo(at(0.08f, 0.185f));
    bill.lineTo(at(0.23f, 0.185f));
    bill.closeSubPath();
    g.setColour(palette.detail);
    g.fillPath(bill);

    const float knob = size * 0.055f;
    const auto knobCentre = at(0.235f, 0.135f);
    g.setColour(palette.background);
    g.fillEllipse(knobCentre.x - knob, knobCentre.y - knob, knob * 2.0f, knob * 1.6f);
    const float eye = size * 0.028f;
    const auto eyeCentre = at(0.30f, 0.14f);
    g.fillEllipse(eyeCentre.x - eye, eyeCentre.y - eye, eye * 2.0f, eye * 2.0f);
}

void SwanMotif::drawKnobCap(juce::Graphics& g, P centre, float radius, float angle,
                            const animalaudio::ui::Palette& palette) const
{
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.15f), centre.x, centre.y - radius,
                                           palette.panel, centre.x, centre.y + radius, false));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(palette.outline);
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Pointer: a little swan neck curving out from the middle, with the orange bill at its tip.
    juce::Path neck;
    neck.startNewSubPath(0.0f, radius * 0.15f);
    neck.cubicTo(radius * 0.32f, -radius * 0.20f, -radius * 0.28f, -radius * 0.42f, 0.0f, -radius * 0.70f);
    const auto rotate = juce::AffineTransform::rotation(angle).translated(centre);
    g.setColour(palette.text);
    g.strokePath(neck,
                 juce::PathStrokeType(std::max(1.5f, radius * 0.11f), juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded),
                 rotate);

    const auto tip = P(0.0f, -radius * 0.72f).transformedBy(rotate);
    const float dot = std::max(2.0f, radius * 0.11f);
    g.setColour(palette.detail);
    g.fillEllipse(tip.x - dot, tip.y - dot, dot * 2.0f, dot * 2.0f);
}

void SwanMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                            const animalaudio::ui::Palette& palette) const
{
    animalaudio::ui::drawRipples(g, area, palette.ambient.withAlpha(0.35f), timeSeconds);
}
} // namespace swan::ui
