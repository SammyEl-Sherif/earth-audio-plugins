#include "PaletteView.h"

#include <algorithm>

namespace dolphinflow::ui
{
namespace
{
using animalaudio::ui::makeFont;

constexpr int kLabelW = 52;

void arrow(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool right)
{
    const auto box = area.withSizeKeepingCentre(8.0f, 10.0f);
    juce::Path p;
    if (right)
        p.addTriangle(box.getX(), box.getY(), box.getRight(), box.getCentreY(), box.getX(), box.getBottom());
    else
        p.addTriangle(box.getRight(), box.getY(), box.getX(), box.getCentreY(), box.getRight(), box.getBottom());
    g.setColour(colour);
    g.fillPath(p);
}

void house(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto box = area.withSizeKeepingCentre(12.0f, 11.0f);
    juce::Path p;
    p.startNewSubPath(box.getX(), box.getY() + 5.0f);
    p.lineTo(box.getCentreX(), box.getY());
    p.lineTo(box.getRight(), box.getY() + 5.0f);
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillRect(box.getX() + 2.0f, box.getY() + 5.0f, box.getWidth() - 4.0f, box.getHeight() - 5.0f);
}

juce::String keyName(int tonic, int scale)
{
    return juce::String(dsp::pitchClassName(tonic, dsp::prefersFlats(tonic, scale))) + " " + dsp::scaleAt(scale).label;
}
} // namespace

void drawChordText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size,
                   juce::Colour colour)
{
    // Base: a leading accidental (numerals), the note letter or roman numeral, its accidental, a minor "m" (not
    // "maj"). The rest (7, add9, 7/ii...) is set small and raised, with maj as a triangle.
    const int length = text.length();
    int split = 0;
    if (split < length && (text[split] == 'b' || text[split] == '#') && length > 1 &&
        juce::String("IViv").containsChar(text[1]))
        ++split;
    if (split < length && juce::String("ABCDEFG").containsChar(text[split]))
    {
        ++split;
        if (split < length && (text[split] == '#' || text[split] == 'b'))
            ++split;
        if (split < length && text[split] == 'm' && !text.substring(split).startsWith("maj"))
            ++split;
    }
    else
    {
        while (split < length && juce::String("IViv").containsChar(text[split]))
            ++split;
    }
    split = std::max(1, std::min(split, length));
    const auto base = text.substring(0, split);
    const auto raised = text.substring(split).replace("maj", juce::String::fromUTF8("\xce\x94"));

    const auto baseFont = makeFont(size, true, 0.0f);
    const auto raisedFont = makeFont(size * 0.66f, true, 0.0f);
    const float baseW = juce::GlyphArrangement::getStringWidth(baseFont, base);
    const float raisedW = juce::GlyphArrangement::getStringWidth(raisedFont, raised);
    const float total = baseW + raisedW + 1.0f;
    juce::Graphics::ScopedSaveState state(g);
    if (total > area.getWidth() && total > 0.0f)
    {
        const float scale = area.getWidth() / total;
        g.addTransform(juce::AffineTransform::scale(scale, scale, area.getCentreX(), area.getCentreY()));
    }
    const float x = area.getCentreX() - total * 0.5f;
    const float baseline = area.getCentreY() + size * 0.36f;
    g.setColour(colour);
    g.setFont(baseFont);
    g.drawSingleLineText(base, juce::roundToInt(x), juce::roundToInt(baseline));
    g.setFont(raisedFont);
    g.drawSingleLineText(raised, juce::roundToInt(x + baseW + 1.0f), juce::roundToInt(baseline - size * 0.42f));
}

// ---------------------------------------------------------------------------------------------------------------

PaletteCell::PaletteCell(ComposeContext& context, int r, int d)
    : ctx(context)
    , row(r)
    , degree(d)
{
    setMouseClickGrabsKeyboardFocus(false);
}

int PaletteCell::source() const noexcept
{
    return DolphinFlowProcessor::kKeySourceBase + 200 + row * dsp::kDegrees + degree;
}

juce::var PaletteCell::describe(int root, int shape)
{
    return "chord:" + juce::String(root) + ":" + juce::String(shape);
}

bool PaletteCell::parse(const juce::var& description, int& root, int& shape)
{
    const auto text = description.toString();
    if (!text.startsWith("chord:"))
        return false;
    const auto parts = juce::StringArray::fromTokens(text.fromFirstOccurrenceOf("chord:", false, false), ":", {});
    if (parts.size() != 2)
        return false;
    root = std::clamp(parts[0].getIntValue(), 0, 11);
    shape = std::clamp(parts[1].getIntValue(), 0, dsp::kNumChordShapes - 1);
    return true;
}

void PaletteCell::setChord(const dsp::PaletteChord& newChord)
{
    chord = newChord;
    if (isEmpty())
    {
        setTooltip({});
    }
    else
    {
        const auto k = ctx.key();
        setTooltip(juce::String::fromUTF8(dsp::chordNameInKey(chord.root, chord.shape, k.homeTonic, k.homeScale).text) +
                   "  (" + juce::String::fromUTF8(chord.numeral.text) + ")" +
                   (chord.inKey ? "" : ", borrows a note from outside the key") +
                   ". Hold to play, drag onto the timeline, or double-click to add it after the selection.");
    }
    repaint();
}

void PaletteCell::setLit(bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void PaletteCell::mouseDown(const juce::MouseEvent& e)
{
    dragging = false;
    if (isEmpty() || e.mods.isPopupMenu())
        return;
    ctx.processor.setParameterFromUi(ids::chord, static_cast<float>(chord.shape));
    ctx.processor.pressRoot(source(), chord.root);
    ctx.focusRoot = chord.root;
    ctx.focusShape = chord.shape;
    playing = true;
    ctx.changed();
}

void PaletteCell::stop()
{
    if (!playing)
        return;
    playing = false;
    ctx.processor.releaseRoot(source());
    repaint();
}

void PaletteCell::mouseDrag(const juce::MouseEvent& e)
{
    if (isEmpty() || dragging || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        dragging = true;
        stop();
        juce::Image image(juce::Image::ARGB, getWidth(), getHeight(), true);
        {
            juce::Graphics g(image);
            paint(g);
        }
        container->startDragging(describe(chord.root, chord.shape), this, juce::ScaledImage(image));
    }
}

void PaletteCell::mouseUp(const juce::MouseEvent&)
{
    stop();
}

void PaletteCell::mouseDoubleClick(const juce::MouseEvent&)
{
    write();
}

void PaletteCell::write()
{
    if (isEmpty())
        return;
    const int root = chord.root;
    const int shape = chord.shape;
    int after = -1;
    for (int i : ctx.selectedChords)
        after = std::max(after, i);
    ctx.edit(
        [&](dsp::Song& song)
        {
            const auto chords = dsp::placeChords(song);
            dsp::InsertPoint at;
            if (after >= 0 && after < static_cast<int>(chords.size()))
                at = {chords[static_cast<std::size_t>(after)].section,
                      chords[static_cast<std::size_t>(after)].index + 1};
            else
                at = {static_cast<int>(song.sections.size()) - 1,
                      static_cast<int>(song.sections.empty() ? 0 : song.sections.back().chords.size())};
            dsp::insertChord(song, at, {root, shape, 4.0});
        });
    // The new chord becomes the selection, so the next double-click follows it.
    ctx.selectedChords = {after >= 0 ? after + 1 : dsp::countChords(ctx.processor.getSong()) - 1};
    ctx.changed();
}

void PaletteCell::paint(juce::Graphics& g)
{
    if (isEmpty())
        return;
    const auto& palette = ctx.theme.palette;
    const auto bounds = getLocalBounds().toFloat();
    const float d = std::min(bounds.getWidth(), bounds.getHeight()) - 6.0f;
    const auto circle = bounds.withSizeKeepingCentre(d, d);
    const auto colour = ctx.chordColour(chord.shape);
    const bool on = lit || playing;
    const bool hover = isMouseOver(true);

    // Relations ring what follows the focused chord; matching dims what lacks the melody's notes.
    auto relation = dsp::Relation::None;
    const bool focused = chord.root == ctx.focusRoot && chord.shape == ctx.focusShape;
    if (ctx.relations && ctx.focusRoot >= 0)
        relation = dsp::relationBetween(ctx.focusRoot, ctx.focusShape, chord.root, chord.shape);
    const bool dimmed = (ctx.matchMask != 0 && !dsp::chordContains(chord.root, chord.shape, ctx.matchMask)) ||
                        (ctx.relations && ctx.focusRoot >= 0 && relation == dsp::Relation::None && !focused);
    const float alpha = dimmed ? 0.28f : 1.0f;

    if (on)
    {
        g.setColour(colour.withAlpha(0.3f));
        g.fillEllipse(circle.expanded(3.0f));
    }
    if (chord.inKey)
    {
        const auto face = on ? colour.brighter(0.15f) : colour.withMultipliedBrightness(hover ? 0.92f : 0.72f);
        g.setGradientFill(juce::ColourGradient(face.brighter(0.1f).withMultipliedAlpha(alpha), circle.getCentreX(),
                                               circle.getY(), face.darker(0.25f).withMultipliedAlpha(alpha),
                                               circle.getCentreX(), circle.getBottom(), false));
        g.fillEllipse(circle);
    }
    else
    {
        g.setColour((on ? colour.withAlpha(0.35f) : palette.panelRaised.withAlpha(hover ? 0.9f : 0.55f))
                        .withMultipliedAlpha(alpha));
        g.fillEllipse(circle);
        g.setColour(colour.withAlpha(0.9f * alpha));
        g.drawEllipse(circle.reduced(0.8f), 1.6f);
    }
    if (relation != dsp::Relation::None || (ctx.relations && focused))
    {
        g.setColour(relation == dsp::Relation::Smooth ? palette.text.withAlpha(0.55f) : palette.text);
        if (relation == dsp::Relation::Smooth)
        {
            juce::Path ring;
            ring.addEllipse(circle.expanded(2.5f));
            juce::Path dashed;
            const float dashes[] = {3.0f, 3.0f};
            juce::PathStrokeType(1.5f).createDashedStroke(dashed, ring, dashes, 2);
            g.fillPath(dashed);
        }
        else
        {
            g.drawEllipse(circle.expanded(2.5f), focused ? 1.0f : 2.2f);
        }
    }
    const auto ink = (chord.inKey ? palette.onAccent : colour.brighter(0.25f)).withMultipliedAlpha(alpha);
    const auto label =
        ctx.numerals ? juce::String::fromUTF8(chord.numeral.text) : ctx.chordLabel(chord.root, chord.shape);
    drawChordText(g, label, circle.reduced(d * 0.12f, 0.0f), std::clamp(d * 0.27f, 10.0f, 16.0f), ink);
}

// ---------------------------------------------------------------------------------------------------------------

PaletteView::PaletteView(ComposeContext& context)
    : ctx(context)
    , homeButton({}, context.theme.palette)
    , prevButton({}, context.theme.palette)
    , borrowButton({}, context.theme.palette)
    , nextButton({}, context.theme.palette)
    , relationsButton("CHORD RELATIONS", context.theme.palette)
    , matchButton("MATCHING NOTES", context.theme.palette)
{
    for (int row = 0; row < dsp::kCircleRows; ++row)
        for (int degree = 0; degree < dsp::kDegrees; ++degree)
        {
            auto cell = std::make_unique<PaletteCell>(ctx, row, degree);
            addAndMakeVisible(*cell);
            cells[static_cast<std::size_t>(row * dsp::kDegrees + degree)] = std::move(cell);
        }
    homeButton.setTooltip("Back to the home key.");
    prevButton.setTooltip("Borrow from the key a fifth down (one more flat). Further from home is more exotic.");
    nextButton.setTooltip("Borrow from the key a fifth up (one more sharp).");
    borrowButton.setTooltip("The key the palette shows. Click to switch to a parallel mode.");
    relationsButton.setTooltip("Ring the chords that follow the one you last clicked: solid = strong pull (a fifth "
                               "down, a leading tone), dashed = smooth (two shared notes).");
    matchButton.setTooltip("Showing only chords that contain the melody notes you sent here. Click to clear.");
    prevButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { arrow(g, r, c, false); };
    nextButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { arrow(g, r, c, true); };
    homeButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { house(g, r, c); };
    prevButton.onClick = [this] { borrow(-1); };
    nextButton.onClick = [this] { borrow(1); };
    homeButton.onClick = [this] { goHome(); };
    borrowButton.onClick = [this] { showScaleMenu(); };
    relationsButton.onClick = [this]
    {
        ctx.relations = !ctx.relations;
        ctx.changed();
    };
    matchButton.onClick = [this]
    {
        ctx.matchMask = 0;
        ctx.changed();
    };
    for (auto* b : {&homeButton, &prevButton, &borrowButton, &nextButton, &relationsButton, &matchButton})
    {
        b->setClickingTogglesState(false);
        b->setMouseClickGrabsKeyboardFocus(false);
        addChildComponent(*b);
        b->setVisible(b != &matchButton);
    }

    const auto& p = ctx.theme.palette;
    menuLook.setColour(juce::PopupMenu::backgroundColourId, p.panel);
    menuLook.setColour(juce::PopupMenu::textColourId, p.text);
    menuLook.setColour(juce::PopupMenu::headerTextColourId, p.textDim);
    menuLook.setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    menuLook.setColour(juce::PopupMenu::highlightedTextColourId, p.onAccent);
    refresh();
}

PaletteView::~PaletteView()
{
    juce::PopupMenu::dismissAllActiveMenus();
}

PaletteCell& PaletteView::getCell(int row, int degree) noexcept
{
    return *cells[static_cast<std::size_t>(std::clamp(row, 0, dsp::kCircleRows - 1) * dsp::kDegrees +
                                           std::clamp(degree, 0, dsp::kDegrees - 1))];
}

void PaletteView::borrow(int steps)
{
    const auto key = ctx.key();
    ctx.processor.setPaletteBorrow(key.shift + steps, key.scale);
    ctx.changed();
}

void PaletteView::goHome()
{
    ctx.processor.setPaletteBorrow(0, ctx.key().homeScale);
    ctx.changed();
}

void PaletteView::setPaletteScale(int scale)
{
    ctx.processor.setPaletteBorrow(ctx.key().shift, scale);
    ctx.changed();
}

void PaletteView::showScaleMenu()
{
    const auto key = ctx.key();
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader("BORROW FROM A PARALLEL MODE");
    for (int s = 0; s < dsp::kNumScales; ++s)
        menu.addItem(s + 1, keyName(key.tonic(), s), true, s == key.scale);
    juce::Component::SafePointer<PaletteView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&borrowButton),
                       [safe](int result)
                       {
                           if (safe != nullptr && result > 0)
                               safe->setPaletteScale(result - 1);
                       });
}

void PaletteView::refresh()
{
    const auto key = ctx.key();
    const bool keyChanged = key.homeTonic != shown.homeTonic || key.homeScale != shown.homeScale ||
                            key.shift != shown.shift || key.scale != shown.scale;
    if (keyChanged || !filled || shownNumerals != ctx.numerals)
    {
        filled = true;
        shown = key;
        shownNumerals = ctx.numerals;
        const auto shift = key.shift == 0 ? juce::String() : (key.shift > 0 ? "  +" : "  ") + juce::String(key.shift);
        borrowButton.setButtonText(keyName(key.tonic(), key.scale) + shift);
        borrowButton.setToggleState(!key.isHome(), juce::dontSendNotification);
        homeButton.setEnabled(!key.isHome());
        prevButton.setEnabled(key.shift > -dsp::kMaxPaletteShift);
        nextButton.setEnabled(key.shift < dsp::kMaxPaletteShift);
        for (int row = 0; row < dsp::kCircleRows; ++row)
            for (int degree = 0; degree < dsp::kDegrees; ++degree)
                getCell(row, degree).setChord(dsp::circleChord(key, row, degree));
    }
    relationsButton.setToggleState(ctx.relations, juce::dontSendNotification);
    int matched = 0;
    for (unsigned m = ctx.matchMask; m != 0; m &= m - 1)
        ++matched;
    matchButton.setVisible(matched > 0);
    matchButton.setToggleState(true, juce::dontSendNotification);
    matchButton.setButtonText("MATCHING " + juce::String(matched) + (matched == 1 ? " NOTE  X" : " NOTES  X"));

    const auto& engine = ctx.processor.getEngine();
    const bool audible = engine.getRoot() >= 0 && (engine.getEnvelopeLevel() > 0.002f || engine.isGateOn());
    for (auto& cell : cells)
    {
        cell->setLit(audible && !cell->isEmpty() && cell->getChord().root == engine.getRoot() &&
                     cell->getChord().shape == engine.getShape());
        cell->repaint();
    }
    repaint();
}

void PaletteView::paint(juce::Graphics& g)
{
    const auto& palette = ctx.theme.palette;
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.0f, true, 0.16f));
    const int rowH = grid.getHeight() / dsp::kCircleRows;
    for (int row = 0; row < dsp::kCircleRows; ++row)
        g.drawText(dsp::circleRowLabel(row), juce::Rectangle<int>(0, grid.getY() + row * rowH, kLabelW, rowH),
                   juce::Justification::centredLeft, false);
    g.setColour(palette.outline.withAlpha(0.7f));
    g.fillRect(0, getHeight() - kBarH, getWidth(), 1);
}

void PaletteView::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromBottom(kBarH).reduced(0, 6);
    bar.removeFromTop(2);
    homeButton.setBounds(bar.removeFromLeft(34));
    bar.removeFromLeft(6);
    prevButton.setBounds(bar.removeFromLeft(30));
    bar.removeFromLeft(4);
    borrowButton.setBounds(bar.removeFromLeft(150));
    bar.removeFromLeft(4);
    nextButton.setBounds(bar.removeFromLeft(30));
    bar.removeFromLeft(12);
    relationsButton.setBounds(bar.removeFromLeft(140));
    bar.removeFromLeft(8);
    matchButton.setBounds(bar.removeFromLeft(150));

    area.removeFromBottom(4);
    grid = area.withTrimmedLeft(kLabelW);
    const int rowH = grid.getHeight() / dsp::kCircleRows;
    const int colW = grid.getWidth() / dsp::kDegrees;
    for (int row = 0; row < dsp::kCircleRows; ++row)
        for (int degree = 0; degree < dsp::kDegrees; ++degree)
            getCell(row, degree).setBounds(grid.getX() + degree * colW, grid.getY() + row * rowH, colW, rowH);
}
} // namespace dolphinflow::ui
