#include "DolphinFlowMotif.h"

#include <animalaudio/ui/Drawing.h>

#include <cmath>

namespace dolphinflow::ui
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

juce::Path makeDolphinBody(juce::Rectangle<float> area)
{
    juce::Path d;
    d.startNewSubPath(0.10f, 0.40f);                     // tip of the beak
    d.cubicTo(0.12f, 0.33f, 0.16f, 0.29f, 0.22f, 0.28f); // melon
    d.cubicTo(0.36f, 0.14f, 0.62f, 0.12f, 0.78f, 0.36f); // arched back
    d.cubicTo(0.82f, 0.42f, 0.85f, 0.50f, 0.87f, 0.57f); // tail stock
    d.lineTo(0.99f, 0.50f);                              // upper fluke
    d.cubicTo(0.95f, 0.60f, 0.92f, 0.64f, 0.90f, 0.66f);
    d.lineTo(0.86f, 0.78f); // lower fluke
    d.cubicTo(0.84f, 0.70f, 0.82f, 0.64f, 0.80f, 0.60f);
    d.cubicTo(0.70f, 0.42f, 0.52f, 0.32f, 0.30f, 0.40f); // belly
    d.cubicTo(0.24f, 0.42f, 0.18f, 0.43f, 0.12f, 0.42f); // throat to jaw
    d.closeSubPath();

    d.startNewSubPath(0.44f, 0.19f); // dorsal fin
    d.cubicTo(0.48f, 0.12f, 0.52f, 0.06f, 0.58f, 0.04f);
    d.cubicTo(0.56f, 0.10f, 0.57f, 0.15f, 0.60f, 0.19f);
    d.closeSubPath();

    d.startNewSubPath(0.30f, 0.40f); // flipper
    d.cubicTo(0.28f, 0.46f, 0.27f, 0.50f, 0.24f, 0.54f);
    d.cubicTo(0.32f, 0.50f, 0.36f, 0.46f, 0.38f, 0.39f);
    d.closeSubPath();

    d.applyTransform(unitToArea(area));
    return d;
}

void DolphinFlowMotif::drawMascot(juce::Graphics& g, juce::Rectangle<float> area,
                                  const animalaudio::ui::Palette& palette) const
{
    const auto toArea = unitToArea(area);
    const auto at = [&](float x, float y) { return P(x, y).transformedBy(toArea); };
    const float size = std::min(area.getWidth(), area.getHeight());

    // The wave it leaps from.
    juce::Path wave;
    wave.startNewSubPath(at(0.02f, 0.86f));
    wave.cubicTo(at(0.22f, 0.74f), at(0.38f, 0.98f), at(0.56f, 0.84f));
    wave.cubicTo(at(0.70f, 0.74f), at(0.84f, 0.96f), at(1.02f, 0.84f));
    g.setColour(palette.ambient.withAlpha(0.8f));
    g.strokePath(wave, juce::PathStrokeType(std::max(1.5f, size * 0.045f), juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(palette.ambient.withAlpha(0.6f));
    for (const auto& drop : {P(0.80f, 0.70f), P(0.92f, 0.76f), P(0.74f, 0.80f)})
    {
        const auto c = at(drop.x, drop.y);
        const float r = size * 0.025f;
        g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    const auto body = makeDolphinBody(area);
    const auto top = at(0.5f, 0.10f);
    const auto bottom = at(0.5f, 0.50f);
    g.setGradientFill(juce::ColourGradient(palette.textDim, top.x, top.y, palette.text, bottom.x, bottom.y, false));
    g.fillPath(body);

    const float eye = size * 0.026f;
    const auto eyeCentre = at(0.205f, 0.335f);
    g.setColour(palette.background);
    g.fillEllipse(eyeCentre.x - eye, eyeCentre.y - eye, eye * 2.0f, eye * 2.0f);

    // A small coral smile line along the jaw.
    juce::Path smile;
    smile.startNewSubPath(at(0.12f, 0.405f));
    smile.quadraticTo(at(0.17f, 0.395f), at(0.21f, 0.375f));
    g.setColour(palette.detail);
    g.strokePath(smile, juce::PathStrokeType(std::max(1.0f, size * 0.018f), juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void DolphinFlowMotif::drawKnobCap(juce::Graphics& g, P centre, float radius, float angle,
                                   const animalaudio::ui::Palette& palette) const
{
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.15f), centre.x, centre.y - radius,
                                           palette.panel, centre.x, centre.y + radius, false));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(palette.outline);
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Pointer: a dorsal fin cutting outward, with a coral dot at its tip.
    const float r = radius;
    juce::Path fin;
    fin.startNewSubPath(-0.20f * r, -0.05f * r);
    fin.cubicTo(-0.12f * r, -0.40f * r, 0.02f * r, -0.66f * r, 0.16f * r, -0.80f * r);
    fin.cubicTo(0.10f * r, -0.52f * r, 0.12f * r, -0.28f * r, 0.22f * r, -0.05f * r);
    fin.closeSubPath();
    const auto rotate = juce::AffineTransform::rotation(angle).translated(centre);
    g.setColour(palette.text);
    g.fillPath(fin, rotate);

    const auto tip = P(0.14f * r, -0.76f * r).transformedBy(rotate);
    const float dot = std::max(2.0f, r * 0.10f);
    g.setColour(palette.detail);
    g.fillEllipse(tip.x - dot, tip.y - dot, dot * 2.0f, dot * 2.0f);
}

void DolphinFlowMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                                   const animalaudio::ui::Palette& palette) const
{
    animalaudio::ui::drawRipples(g, area, palette.ambient.withAlpha(0.35f), timeSeconds);
}
} // namespace dolphinflow::ui
