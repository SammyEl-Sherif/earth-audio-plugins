#include "CosmosMotif.h"

#include "CosmosTheme.h"

#include <cmath>
#include <cstdint>

namespace cosmos::ui
{
namespace
{
juce::Rectangle<float> disc(juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}

/** Deterministic 0..1 hash so the starfield is the same every frame. */
float hash01(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x & 0xffffffU) / 16777216.0f;
}

void drawStars(juce::Graphics& g, juce::Rectangle<float> area, int count, float alpha, double time,
               const animalaudio::ui::Palette& palette)
{
    for (int i = 0; i < count; ++i)
    {
        const auto seed = static_cast<std::uint32_t>(i * 3 + 11);
        const float x = area.getX() + hash01(seed) * area.getWidth();
        const float y = area.getY() + hash01(seed + 1) * area.getHeight();
        const float size = 0.6f + 1.5f * hash01(seed + 2) * hash01(seed + 2);
        const float twinkle = 0.55f + 0.45f * static_cast<float>(std::sin(time * (0.6 + hash01(seed + 5)) + i));
        const auto colour = hash01(seed + 7) < 0.15f ? sunset(hash01(seed + 9)) : palette.text;
        g.setColour(colour.withAlpha(alpha * twinkle * (0.4f + 0.6f * hash01(seed + 3))));
        g.fillEllipse(disc({x, y}, size * 0.5f));
    }
}
} // namespace

void drawSunsetText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& font,
                    juce::Colour cutColour)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, area.getX(), area.getY() + font.getAscent());
    juce::Path path;
    glyphs.createPath(path);
    const auto box = path.getBounds();
    juce::ColourGradient fill(sunset(0.0f), 0.0f, box.getY(), sunset(0.62f), 0.0f, box.getBottom(), false);
    fill.addColour(0.5, sunset(0.3f));
    g.setGradientFill(fill);
    g.fillPath(path);

    // Two thin cuts across the lower half: the 70s racing-stripe lettering.
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(path);
    g.setColour(cutColour);
    const float h = box.getHeight();
    g.fillRect(box.getX(), box.getY() + h * 0.60f, box.getWidth(), std::max(1.0f, h * 0.06f));
    g.fillRect(box.getX(), box.getY() + h * 0.78f, box.getWidth(), std::max(1.0f, h * 0.08f));
}

void CosmosMotif::drawMascot(juce::Graphics& g, juce::Rectangle<float> area,
                             const animalaudio::ui::Palette& palette) const
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const float radius = size * 0.34f;
    const auto ringBox = juce::Rectangle<float>(size * 0.98f, size * 0.30f).withCentre(centre);
    const auto tilt = juce::AffineTransform::rotation(-0.38f, centre.x, centre.y);
    const float ringWidth = std::max(1.2f, size * 0.045f);

    // Back half of the ring, behind the planet.
    juce::Path back;
    back.addCentredArc(centre.x, centre.y, ringBox.getWidth() * 0.5f, ringBox.getHeight() * 0.5f, 0.0f,
                       -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour(palette.text.withAlpha(0.5f));
    g.strokePath(back, juce::PathStrokeType(ringWidth), tilt);

    // Glow, then the planet striped like a sunset: bands thin out toward the bottom.
    g.setGradientFill(juce::ColourGradient(sunset(0.3f).withAlpha(0.35f), centre.x, centre.y,
                                           sunset(0.3f).withAlpha(0.0f), centre.x + radius * 1.7f, centre.y, true));
    g.fillEllipse(disc(centre, radius * 1.7f));

    juce::Path planet;
    planet.addEllipse(disc(centre, radius));
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(planet);
        const auto box = disc(centre, radius);
        juce::ColourGradient fill(sunset(0.0f), 0.0f, box.getY(), sunset(0.75f), 0.0f, box.getBottom(), false);
        fill.addColour(0.45, sunset(0.3f));
        g.setGradientFill(fill);
        g.fillRect(box);
        g.setColour(palette.background);
        for (int i = 0; i < 4; ++i)
        {
            const float t = static_cast<float>(i) / 4.0f;
            const float y = box.getY() + box.getHeight() * (0.5f + 0.13f * static_cast<float>(i));
            g.fillRect(box.getX(), y, box.getWidth(), box.getHeight() * (0.025f + 0.03f * t));
        }
        g.setColour(palette.text.withAlpha(0.18f));
        g.fillEllipse(disc({centre.x - radius * 0.35f, centre.y - radius * 0.4f}, radius * 0.3f));
    }

    // Front half of the ring.
    juce::Path front;
    front.addCentredArc(centre.x, centre.y, ringBox.getWidth() * 0.5f, ringBox.getHeight() * 0.5f, 0.0f,
                        juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, true);
    g.setColour(palette.background);
    g.strokePath(front, juce::PathStrokeType(ringWidth * 2.2f), tilt);
    g.setColour(palette.text);
    g.strokePath(front, juce::PathStrokeType(ringWidth), tilt);

    // A small moon.
    const auto moon = juce::Point<float>(area.getX() + size * 0.86f, area.getY() + size * 0.16f);
    g.setColour(palette.accent);
    g.fillEllipse(disc(moon, size * 0.045f));
}

void CosmosMotif::drawKnobCap(juce::Graphics& g, juce::Point<float> centre, float radius, float angle,
                              const animalaudio::ui::Palette& palette) const
{
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(disc(centre, radius * 1.02f).translated(0.0f, radius * 0.1f));

    // Knurled skirt.
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.45f), centre.x, centre.y - radius,
                                           palette.background, centre.x, centre.y + radius, false));
    g.fillEllipse(disc(centre, radius));
    g.setColour(palette.background.withAlpha(0.55f));
    const int teeth = std::max(24, static_cast<int>(radius * 1.6f));
    for (int i = 0; i < teeth; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(teeth);
        const juce::Point<float> outer(centre.x + radius * std::sin(a), centre.y - radius * std::cos(a));
        const juce::Point<float> inner(centre.x + radius * 0.88f * std::sin(a),
                                       centre.y - radius * 0.88f * std::cos(a));
        g.drawLine(inner.x, inner.y, outer.x, outer.y, 1.0f);
    }

    // Brushed face with a soft top light.
    const float face = radius * 0.8f;
    g.setGradientFill(juce::ColourGradient(palette.panelRaised.brighter(0.25f), centre.x - face * 0.4f, centre.y - face,
                                           palette.panel.darker(0.3f), centre.x + face * 0.4f, centre.y + face, false));
    g.fillEllipse(disc(centre, face));
    g.setColour(palette.text.withAlpha(0.08f));
    g.drawEllipse(disc(centre, face * 0.62f), 1.0f);
    g.setColour(palette.text.withAlpha(0.14f));
    g.drawEllipse(disc(centre, face - 0.5f), 1.0f);

    // Cream index line and a glowing tip in the knob's colour.
    const auto place = juce::AffineTransform::rotation(angle).translated(centre);
    const auto from = juce::Point<float>(0.0f, -face * 0.28f).transformedBy(place);
    const auto to = juce::Point<float>(0.0f, -face * 0.86f).transformedBy(place);
    g.setColour(palette.text);
    g.drawLine(from.x, from.y, to.x, to.y, std::max(1.6f, radius * 0.07f));
    g.setColour(palette.accent.withAlpha(0.35f));
    g.fillEllipse(disc(to, std::max(2.5f, radius * 0.16f)));
    g.setColour(palette.accent);
    g.fillEllipse(disc(to, std::max(1.4f, radius * 0.07f)));
}

void CosmosMotif::drawAmbient(juce::Graphics& g, juce::Rectangle<float> area, double timeSeconds,
                              const animalaudio::ui::Palette& palette) const
{
    drawStars(g, area, 70, 0.55f, timeSeconds, palette);

    // Synthwave horizon: a glow line and a perspective grid running toward the viewer.
    const float horizon = area.getY() + area.getHeight() * 0.72f;
    g.setGradientFill(juce::ColourGradient(sunset(0.5f).withAlpha(0.16f), 0.0f, horizon, sunset(0.5f).withAlpha(0.0f),
                                           0.0f, horizon - area.getHeight() * 0.25f, false));
    g.fillRect(area.getX(), horizon - area.getHeight() * 0.25f, area.getWidth(), area.getHeight() * 0.25f);

    const auto grid = sunset(0.62f);
    g.setColour(grid.withAlpha(0.22f));
    g.fillRect(area.getX(), horizon, area.getWidth(), 1.0f);
    const float depth = area.getBottom() - horizon;
    const float scroll = static_cast<float>(std::fmod(timeSeconds * 0.25, 1.0));
    for (int i = 0; i < 7; ++i)
    {
        const float t = (static_cast<float>(i) + scroll) / 7.0f;
        const float y = horizon + depth * t * t;
        g.setColour(grid.withAlpha(0.06f + 0.14f * t));
        g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
    }
    const float vanishing = area.getCentreX();
    for (int i = -9; i <= 9; ++i)
    {
        const float spread = static_cast<float>(i) / 9.0f;
        g.setColour(grid.withAlpha(0.13f));
        g.drawLine(vanishing + spread * area.getWidth() * 0.12f, horizon, vanishing + spread * area.getWidth() * 1.1f,
                   area.getBottom(), 1.0f);
    }
}

void CosmosMotif::drawDecoration(juce::Graphics& g, juce::Rectangle<float> area,
                                 const animalaudio::ui::Palette& palette) const
{
    drawStars(g, area, 120, 0.22f, 0.0, palette);
}
} // namespace cosmos::ui
