#include "PaletteView.h"

#include <algorithm>

namespace dolphinflow::ui
{
namespace
{
using animalaudio::ui::makeFont;

constexpr int kNavH = 28;
constexpr int kDigitsH = 14;
constexpr int kStripH = 42;
constexpr int kLabelW = 74;
constexpr int kGap = 8;

juce::String utf8(const dsp::Label& label)
{
    return juce::String::fromUTF8(label.text);
}

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

juce::String chordText(const DolphinFlowProcessor& processor, int root, int shape)
{
    const auto key = processor.getPaletteKey();
    return utf8(dsp::chordNameInKey(root, shape, key.homeTonic, key.homeScale));
}
} // namespace

juce::Colour qualityColour(const animalaudio::ui::Palette& palette, int shape)
{
    switch (dsp::shapeTheory(shape).quality)
    {
    case dsp::Quality::Major:
        return palette.accent;
    case dsp::Quality::Minor:
        return palette.accentAlt;
    case dsp::Quality::Other:
        break;
    }
    return palette.textDim;
}

// ---------------------------------------------------------------------------------------------------------------

PaletteCell::PaletteCell(DolphinFlowProcessor& p, const animalaudio::ui::Palette& palette, int r, int d)
    : processor(p)
    , colours(palette)
    , row(r)
    , degree(d)
{
    setMouseClickGrabsKeyboardFocus(false);
}

int PaletteCell::source() const noexcept
{
    return DolphinFlowProcessor::kKeySourceBase + 200 + row * dsp::kPaletteColumns + degree;
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
    const bool changed = newChord.root != chord.root || newChord.shape != chord.shape ||
                         newChord.inKey != chord.inKey || std::strcmp(newChord.numeral.text, chord.numeral.text) != 0;
    chord = newChord;
    if (changed)
    {
        setTooltip(chordText(processor, chord.root, chord.shape) + "  (" + utf8(chord.numeral) + ")" +
                   (chord.inKey ? "" : ", borrows a note from outside the key") +
                   ". Hold to play, drag onto a square below to add it, or double-click to put it in the selected "
                   "square.");
        repaint();
    }
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
    if (e.mods.isPopupMenu())
        return;
    processor.setParameterFromUi(ids::chord, static_cast<float>(chord.shape));
    processor.pressRoot(source(), chord.root);
    playing = true;
    repaint();
}

void PaletteCell::stop()
{
    if (!playing)
        return;
    playing = false;
    processor.releaseRoot(source());
    repaint();
}

void PaletteCell::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
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
    if (onWrite)
        onWrite(chord);
}

void PaletteCell::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = bounds.withSizeKeepingCentre(std::min(bounds.getWidth() - 8.0f, 84.0f), bounds.getHeight() - 6.0f);
    const float radius = area.getHeight() * 0.5f;
    const auto colour = qualityColour(colours, chord.shape);
    const bool on = lit || playing;
    const bool hover = isMouseOver(true);

    if (on)
    {
        g.setColour(colour.withAlpha(0.28f));
        g.fillRoundedRectangle(area.expanded(3.0f), radius + 3.0f);
    }
    g.setColour(juce::Colours::black.withAlpha(0.32f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);

    juce::Colour ink;
    if (chord.inKey)
    {
        const auto face = on ? colour.brighter(0.2f) : colour.withMultipliedBrightness(hover ? 0.95f : 0.82f);
        g.setGradientFill(juce::ColourGradient(face.brighter(0.08f), 0.0f, area.getY(), face.darker(0.18f), 0.0f,
                                               area.getBottom(), false));
        g.fillRoundedRectangle(area, radius);
        ink = colours.onAccent;
    }
    else
    {
        g.setColour(on ? colour.withAlpha(0.35f) : colours.panelRaised.withAlpha(hover ? 1.0f : 0.7f));
        g.fillRoundedRectangle(area, radius);
        g.setColour(colour.withAlpha(on ? 1.0f : 0.85f));
        g.drawRoundedRectangle(area.reduced(0.8f), radius, 1.6f);
        ink = on ? colours.text : colour.brighter(0.15f);
    }

    const auto text = area.reduced(radius * 0.5f, 3.0f);
    g.setColour(ink);
    g.setFont(makeFont(13.5f, true, 0.0f));
    g.drawFittedText(chordText(processor, chord.root, chord.shape),
                     text.withTrimmedBottom(text.getHeight() * 0.42f).toNearestInt(), juce::Justification::centredBottom,
                     1, 0.75f);
    g.setColour(ink.withAlpha(0.72f));
    g.setFont(makeFont(10.0f, true, 0.04f));
    g.drawFittedText(utf8(chord.numeral), text.withTrimmedTop(text.getHeight() * 0.6f).toNearestInt(),
                     juce::Justification::centredTop, 1, 0.7f);
}

// ---------------------------------------------------------------------------------------------------------------

StripCell::StripCell(DolphinFlowProcessor& p, const animalaudio::ui::Palette& palette, int i)
    : processor(p)
    , colours(palette)
    , index(i)
{
    setMouseClickGrabsKeyboardFocus(false);
    setTooltip("Square " + juce::String(i + 1) +
               " of the loop. Drop a chord from the palette here, or select it and double-click a chord. Right-click "
               "empties it.");
}

void StripCell::setSelected(bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void StripCell::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        auto slot = processor.getSlot(index);
        slot.root = -1;
        processor.setSlot(index, slot);
        repaint();
        return;
    }
    if (onSelect)
        onSelect(index);
}

bool StripCell::isInterestedInDragSource(const SourceDetails& details)
{
    int root = 0, shape = 0;
    return PaletteCell::parse(details.description, root, shape);
}

void StripCell::itemDragEnter(const SourceDetails&)
{
    hovered = true;
    repaint();
}

void StripCell::itemDragExit(const SourceDetails&)
{
    hovered = false;
    repaint();
}

void StripCell::itemDropped(const SourceDetails& details)
{
    hovered = false;
    int root = 0, shape = 0;
    if (PaletteCell::parse(details.description, root, shape))
    {
        auto slot = processor.getSlot(index);
        slot.root = root;
        slot.shape = shape;
        processor.setSlot(index, slot);
        if (onDropped)
            onDropped(index);
    }
    repaint();
}

void StripCell::paint(juce::Graphics& g)
{
    const auto slot = processor.getSlot(index);
    const bool playing = processor.getPlayingSlot() == index;
    const auto area = getLocalBounds().toFloat().reduced(1.5f);
    constexpr float radius = 7.0f;

    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);
    g.setColour(playing ? colours.accent.withMultipliedBrightness(0.42f) : colours.panelRaised);
    g.fillRoundedRectangle(area, radius);
    if (playing)
    {
        juce::Graphics::ScopedSaveState clip(g);
        juce::Path shape;
        shape.addRoundedRectangle(area, radius);
        g.reduceClipRegion(shape);
        g.setColour(colours.accent.withAlpha(0.22f));
        g.fillRect(area.withWidth(area.getWidth() * processor.getSlotProgress()));
    }
    if (!slot.isEmpty())
    {
        // A thin bar in the chord's quality colour, like the palette.
        g.setColour(qualityColour(colours, slot.shape).withAlpha(0.9f));
        g.fillRoundedRectangle(area.getX() + 6.0f, area.getBottom() - 5.0f, area.getWidth() - 12.0f, 2.5f, 1.2f);
    }
    g.setColour(hovered ? colours.text : selected ? colours.accent : colours.outline);
    g.drawRoundedRectangle(area, radius, hovered || selected ? 2.0f : 1.0f);

    g.setColour(colours.textDim);
    g.setFont(makeFont(9.0f, true, 0.1f));
    g.drawText(juce::String(index + 1), area.reduced(6.0f, 4.0f), juce::Justification::topLeft, false);

    const auto body = area.reduced(8.0f, 4.0f).withTrimmedLeft(6.0f);
    if (slot.isEmpty())
    {
        g.setColour(colours.textDim.withAlpha(0.55f));
        g.setFont(makeFont(9.5f, true, 0.18f));
        g.drawText(hovered ? "DROP" : "EMPTY", body, juce::Justification::centred, false);
        return;
    }
    const auto key = processor.getPaletteKey();
    g.setColour(playing ? colours.text : colours.text.withAlpha(0.92f));
    g.setFont(makeFont(13.0f, true, 0.0f));
    g.drawFittedText(chordText(processor, slot.root, slot.shape), body.withTrimmedBottom(13.0f).toNearestInt(),
                     juce::Justification::centredBottom, 1, 0.7f);
    g.setColour(colours.textDim);
    g.setFont(makeFont(9.0f, true, 0.06f));
    g.drawFittedText(utf8(dsp::romanNumeral(slot.root, slot.shape, key.homeTonic, key.homeScale)),
                     body.withTrimmedTop(body.getHeight() - 15.0f).withTrimmedBottom(3.0f).toNearestInt(),
                     juce::Justification::centred, 1, 0.7f);
}

// ---------------------------------------------------------------------------------------------------------------

PaletteView::PaletteView(DolphinFlowProcessor& p, const animalaudio::ui::Theme& themeToUse, ArrangeView& arrangeView)
    : processor(p)
    , theme(themeToUse)
    , arrange(arrangeView)
    , keyButton("C", themeToUse.palette)
    , scaleButton("MAJOR", themeToUse.palette)
    , prevButton({}, themeToUse.palette)
    , borrowButton({}, themeToUse.palette)
    , nextButton({}, themeToUse.palette)
    , homeButton({}, themeToUse.palette)
    , loopButton("LOOP", themeToUse.palette)
{
    const auto& palette = theme.palette;
    for (int row = 0; row < dsp::kPaletteRows; ++row)
        for (int degree = 0; degree < dsp::kPaletteColumns; ++degree)
        {
            auto cell = std::make_unique<PaletteCell>(processor, palette, row, degree);
            cell->onWrite = [this](const dsp::PaletteChord& chord)
            {
                arrange.writeSelected(chord.root, chord.shape);
                refresh();
            };
            addAndMakeVisible(*cell);
            cells[static_cast<std::size_t>(row * dsp::kPaletteColumns + degree)] = std::move(cell);
        }
    for (int i = 0; i < dsp::kNumSlots; ++i)
    {
        auto cell = std::make_unique<StripCell>(processor, palette, i);
        cell->onSelect = [this](int index) { select(index); };
        cell->onDropped = [this](int index) { select((index + 1) % dsp::kNumSlots); };
        addAndMakeVisible(*cell);
        strip[static_cast<std::size_t>(i)] = std::move(cell);
    }

    keyButton.setTooltip("Home key of the song. The palette, the numerals and the on-screen keys follow it.");
    scaleButton.setTooltip("Home scale (mode) of the song.");
    prevButton.setTooltip("Borrow from the key a fifth down (one more flat). Further from home sounds more exotic.");
    nextButton.setTooltip("Borrow from the key a fifth up (one more sharp).");
    borrowButton.setTooltip("Where the palette is borrowing from. Click to switch to a parallel mode.");
    homeButton.setTooltip("Back to the home key.");
    loopButton.setTooltip("Play the squares in order, round and round. Follows the host's beat while it plays.");
    prevButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { arrow(g, r, c, false); };
    nextButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { arrow(g, r, c, true); };
    homeButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { house(g, r, c); };
    keyButton.onClick = [this] { showKeyMenu(); };
    scaleButton.onClick = [this] { showScaleMenu(true); };
    borrowButton.onClick = [this] { showScaleMenu(false); };
    prevButton.onClick = [this] { borrow(-1); };
    nextButton.onClick = [this] { borrow(1); };
    homeButton.onClick = [this] { goHome(); };
    for (auto* button : {&keyButton, &scaleButton, &prevButton, &borrowButton, &nextButton, &homeButton})
    {
        button->setClickingTogglesState(false);
        addAndMakeVisible(*button);
    }
    loopAttachment = std::make_unique<juce::ButtonParameterAttachment>(
        *processor.apvts.getParameter(ids::loop.getParamID()), loopButton);
    loopButton.setClickingTogglesState(true);
    addAndMakeVisible(loopButton);

    for (auto* child : getChildren())
        child->setMouseClickGrabsKeyboardFocus(false);

    menuLook.setColour(juce::PopupMenu::backgroundColourId, palette.panel);
    menuLook.setColour(juce::PopupMenu::textColourId, palette.text);
    menuLook.setColour(juce::PopupMenu::headerTextColourId, palette.textDim);
    menuLook.setColour(juce::PopupMenu::highlightedBackgroundColourId, palette.accent);
    menuLook.setColour(juce::PopupMenu::highlightedTextColourId, palette.onAccent);

    refresh();
}

PaletteView::~PaletteView()
{
    // An open key / scale menu draws with menuLook, which goes with us.
    juce::PopupMenu::dismissAllActiveMenus();
}

PaletteCell& PaletteView::getCell(int row, int degree) noexcept
{
    return *cells[static_cast<std::size_t>(std::clamp(row, 0, dsp::kPaletteRows - 1) * dsp::kPaletteColumns +
                                           std::clamp(degree, 0, dsp::kPaletteColumns - 1))];
}

void PaletteView::select(int index)
{
    arrange.select(index);
    refresh();
}

void PaletteView::borrow(int steps)
{
    const auto key = processor.getPaletteKey();
    processor.setPaletteBorrow(key.shift + steps, key.scale);
    refresh();
}

void PaletteView::goHome()
{
    const auto key = processor.getPaletteKey();
    processor.setPaletteBorrow(0, key.homeScale);
    refresh();
}

void PaletteView::setPaletteScale(int scale)
{
    processor.setPaletteBorrow(processor.getPaletteKey().shift, scale);
    refresh();
}

void PaletteView::showKeyMenu()
{
    const int current = processor.getPaletteKey().homeTonic;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader("HOME KEY");
    for (int pc = 0; pc < 12; ++pc)
        menu.addItem(pc + 1,
                     juce::String(dsp::pitchClassName(pc)) +
                         (dsp::pitchClassName(pc)[1] == '#' ? juce::String(" / ") + dsp::pitchClassName(pc, true)
                                                             : juce::String()),
                     true, pc == current);
    juce::Component::SafePointer<PaletteView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&keyButton),
                       [safe](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           safe->processor.setParameterFromUi(ids::key, static_cast<float>(result - 1));
                           safe->refresh();
                       });
}

void PaletteView::showScaleMenu(bool home)
{
    const auto key = processor.getPaletteKey();
    const int current = home ? key.homeScale : key.scale;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader(home ? "HOME SCALE" : "BORROW FROM THE PARALLEL MODE");
    for (int s = 0; s < dsp::kNumScales; ++s)
        menu.addItem(s + 1, home ? juce::String(dsp::kScales[s].name) : keyName(key.tonic(), s), true, s == current);
    juce::Component::SafePointer<PaletteView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(home ? &scaleButton : &borrowButton),
                       [safe, home](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           if (home)
                           {
                               // A new home scale brings the palette home too.
                               safe->processor.setParameterFromUi(ids::scale, static_cast<float>(result - 1));
                               safe->processor.setPaletteBorrow(0, result - 1);
                               safe->refresh();
                           }
                           else
                           {
                               safe->setPaletteScale(result - 1);
                           }
                       });
}

void PaletteView::refresh()
{
    const auto key = processor.getPaletteKey();
    const bool keyChanged = key.homeTonic != shown.homeTonic || key.homeScale != shown.homeScale ||
                            key.shift != shown.shift || key.scale != shown.scale;
    shown = key;
    if (keyChanged || !filled)
    {
        filled = true;
        keyButton.setButtonText(dsp::pitchClassName(key.homeTonic, dsp::prefersFlats(key.homeTonic, key.homeScale)));
        scaleButton.setButtonText(dsp::scaleAt(key.homeScale).label);
        const auto shift = key.shift == 0 ? juce::String() : (key.shift > 0 ? "  +" : "  ") + juce::String(key.shift);
        borrowButton.setButtonText(keyName(key.tonic(), key.scale) + shift);
        borrowButton.setToggleState(!key.isHome(), juce::dontSendNotification);
        homeButton.setEnabled(!key.isHome());
        prevButton.setEnabled(key.shift > -dsp::kMaxPaletteShift);
        nextButton.setEnabled(key.shift < dsp::kMaxPaletteShift);
        for (int row = 0; row < dsp::kPaletteRows; ++row)
            for (int degree = 0; degree < dsp::kPaletteColumns; ++degree)
                getCell(row, degree).setChord(dsp::paletteChord(key, row, degree));
        repaint();
    }

    const auto& engine = processor.getEngine();
    const bool audible = engine.getRoot() >= 0 && (engine.getEnvelopeLevel() > 0.002f || engine.isGateOn());
    for (auto& cell : cells)
        cell->setLit(audible && cell->getChord().root == engine.getRoot() &&
                     cell->getChord().shape == engine.getShape());
    for (int i = 0; i < dsp::kNumSlots; ++i)
    {
        strip[static_cast<std::size_t>(i)]->setSelected(i == arrange.getSelected());
        strip[static_cast<std::size_t>(i)]->repaint();
    }
}

void PaletteView::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.2f));
    g.drawText("HOME", juce::Rectangle<int>(0, 0, 40, kNavH), juce::Justification::centredLeft, false);

    // Row names down the left, scale degrees (and their keys) across the top.
    const int rowH = gridArea.getHeight() / dsp::kPaletteRows;
    for (int row = 0; row < dsp::kPaletteRows; ++row)
        g.drawText(dsp::paletteRowLabel(row), juce::Rectangle<int>(0, gridArea.getY() + row * rowH, kLabelW, rowH),
                   juce::Justification::centredLeft, false);
    const int colW = (gridArea.getWidth() - kLabelW) / dsp::kPaletteColumns;
    g.setFont(makeFont(9.0f, true, 0.1f));
    for (int degree = 0; degree < dsp::kPaletteColumns; ++degree)
        g.drawText("KEY " + juce::String(degree + 1),
                   juce::Rectangle<int>(gridArea.getX() + kLabelW + degree * colW, gridArea.getY() - kDigitsH, colW,
                                        kDigitsH - 2),
                   juce::Justification::centred, false);

    const auto top = strip.front()->getBounds();
    g.setColour(palette.outline.withAlpha(0.6f));
    g.drawHorizontalLine(top.getY() - kGap / 2 - 1, 0.0f, static_cast<float>(getWidth()));
}

void PaletteView::resized()
{
    auto area = getLocalBounds();
    auto nav = area.removeFromTop(kNavH);
    nav.removeFromLeft(40);
    keyButton.setBounds(nav.removeFromLeft(46));
    nav.removeFromLeft(6);
    scaleButton.setBounds(nav.removeFromLeft(136));
    homeButton.setBounds(nav.removeFromRight(32));
    nav.removeFromRight(6);
    nextButton.setBounds(nav.removeFromRight(30));
    nav.removeFromRight(6);
    borrowButton.setBounds(nav.removeFromRight(190));
    nav.removeFromRight(6);
    prevButton.setBounds(nav.removeFromRight(30));

    auto stripRow = area.removeFromBottom(kStripH);
    area.removeFromBottom(kGap + 2);
    loopButton.setBounds(stripRow.removeFromRight(70).withSizeKeepingCentre(70, 30));
    stripRow.removeFromRight(kGap);
    const int stripW = (stripRow.getWidth() - kGap * (dsp::kNumSlots - 1)) / dsp::kNumSlots;
    for (int i = 0; i < dsp::kNumSlots; ++i)
        strip[static_cast<std::size_t>(i)]->setBounds(stripRow.getX() + i * (stripW + kGap), stripRow.getY(), stripW,
                                                      stripRow.getHeight());

    area.removeFromTop(6 + kDigitsH);
    gridArea = area;
    const int rowH = area.getHeight() / dsp::kPaletteRows;
    const int colW = (area.getWidth() - kLabelW) / dsp::kPaletteColumns;
    for (int row = 0; row < dsp::kPaletteRows; ++row)
        for (int degree = 0; degree < dsp::kPaletteColumns; ++degree)
            getCell(row, degree).setBounds(area.getX() + kLabelW + degree * colW, area.getY() + row * rowH, colW,
                                           rowH);
}
} // namespace dolphinflow::ui
