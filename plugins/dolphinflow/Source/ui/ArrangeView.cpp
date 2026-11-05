#include "ArrangeView.h"

#include <algorithm>

namespace dolphinflow::ui
{
namespace
{
using animalaudio::ui::makeFont;

constexpr float kStepperW = 24.0f;
constexpr float kLengthRowH = 22.0f;

juce::String chordName(const dsp::Slot& slot)
{
    return juce::String(dsp::pitchClassName(slot.root)) + dsp::chordShape(slot.shape).symbol;
}

void sign(juce::Graphics& g, juce::Rectangle<float> area, bool plus)
{
    const auto box = area.withSizeKeepingCentre(8.0f, 8.0f);
    g.fillRect(box.withSizeKeepingCentre(8.0f, 1.6f));
    if (plus)
        g.fillRect(box.withSizeKeepingCentre(1.6f, 8.0f));
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------

SlotCell::SlotCell(DolphinFlowProcessor& p, const animalaudio::ui::Palette& palette, int i)
    : processor(p)
    , colours(palette)
    , index(i)
{
    setMouseClickGrabsKeyboardFocus(false);
    setTooltip("Click to select, then play a root (with the CHORD quality) to write it here. - / + sets how long it "
               "plays. Right-click empties it.");
}

juce::Rectangle<float> SlotCell::lengthDownArea() const
{
    auto area = getLocalBounds().toFloat().reduced(1.5f);
    return area.removeFromBottom(kLengthRowH).removeFromLeft(kStepperW);
}

juce::Rectangle<float> SlotCell::lengthUpArea() const
{
    auto area = getLocalBounds().toFloat().reduced(1.5f);
    return area.removeFromBottom(kLengthRowH).removeFromRight(kStepperW);
}

void SlotCell::setSelected(bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void SlotCell::stepLength(int delta)
{
    auto slot = processor.getSlot(index);
    slot.lengthIndex = std::clamp(slot.lengthIndex + delta, 0, dsp::kNumSlotLengths - 1);
    processor.setSlot(index, slot);
    repaint();
}

void SlotCell::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        auto slot = processor.getSlot(index);
        slot.root = -1;
        processor.setSlot(index, slot);
        repaint();
        return;
    }
    if (lengthDownArea().contains(e.position))
        stepLength(-1);
    else if (lengthUpArea().contains(e.position))
        stepLength(1);
    if (onSelect)
        onSelect(index);
}

void SlotCell::paint(juce::Graphics& g)
{
    const auto slot = processor.getSlot(index);
    const bool playing = processor.getPlayingSlot() == index;
    auto area = getLocalBounds().toFloat().reduced(1.5f);
    constexpr float radius = 8.0f;

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);
    const auto face = playing ? colours.accent.withMultipliedBrightness(0.42f) : colours.panelRaised;
    g.setGradientFill(juce::ColourGradient(face.brighter(0.06f), 0.0f, area.getY(), face.darker(0.12f), 0.0f,
                                           area.getBottom(), false));
    g.fillRoundedRectangle(area, radius);

    if (playing)
    {
        // Progress through the square fills from the left.
        juce::Graphics::ScopedSaveState clip(g);
        juce::Path shape;
        shape.addRoundedRectangle(area, radius);
        g.reduceClipRegion(shape);
        g.setColour(colours.accent.withAlpha(0.22f));
        g.fillRect(area.withWidth(area.getWidth() * processor.getSlotProgress()));
    }

    g.setColour(selected ? colours.accent : playing ? colours.accent.withAlpha(0.7f) : colours.outline);
    g.drawRoundedRectangle(area, radius, selected ? 2.2f : 1.0f);

    g.setColour(colours.textDim);
    g.setFont(makeFont(10.0f, true, 0.1f));
    g.drawText(juce::String(index + 1), area.reduced(9.0f, 6.0f).withHeight(12.0f), juce::Justification::topLeft,
               false);

    auto body = area.withTrimmedBottom(kLengthRowH).withTrimmedTop(8.0f);
    if (slot.isEmpty())
    {
        g.setColour(colours.textDim.withAlpha(0.6f));
        g.setFont(makeFont(11.0f, true, 0.2f));
        g.drawText(selected ? "PLAY A ROOT" : "EMPTY", body, juce::Justification::centred, false);
    }
    else
    {
        g.setColour(playing ? colours.text : colours.accent);
        g.setFont(makeFont(22.0f, true, 0.02f));
        g.drawFittedText(chordName(slot), body.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    const auto lengthRow = area.removeFromBottom(kLengthRowH);
    g.setColour(colours.background.withAlpha(0.35f));
    g.fillRect(lengthRow.reduced(0.0f, 0.0f).withTrimmedTop(0.5f));
    g.setColour(slot.isEmpty() ? colours.textDim.withAlpha(0.5f) : colours.textDim);
    g.setFont(makeFont(9.5f, true, 0.14f));
    g.drawText(dsp::slotLengthLabel(slot.lengthIndex), lengthRow, juce::Justification::centred, false);
    g.setColour(colours.text.withAlpha(slot.lengthIndex > 0 ? 0.85f : 0.25f));
    sign(g, lengthDownArea(), false);
    g.setColour(colours.text.withAlpha(slot.lengthIndex < dsp::kNumSlotLengths - 1 ? 0.85f : 0.25f));
    sign(g, lengthUpArea(), true);
}

// ---------------------------------------------------------------------------------------------------------------

ClipButton::ClipButton(DolphinFlowProcessor& p, const animalaudio::ui::Palette& palette)
    : processor(p)
    , colours(palette)
{
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("Drag onto a track to drop the loop as a MIDI clip. Click to save the .mid file and copy its path.");
}

juce::File ClipButton::save()
{
    const auto file = processor.defaultClipFile();
    if (processor.writeMidiClip(file))
        return file;
    return {};
}

void ClipButton::mouseDown(const juce::MouseEvent&)
{
    dragging = false;
    down = true;
    repaint();
}

void ClipButton::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    const auto file = save();
    if (!file.existsAsFile())
    {
        status = "ADD A CHORD FIRST";
    }
    else
    {
        status = "DROP IT ON A MIDI TRACK";
        juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
    }
    if (onStatusChanged)
        onStatusChanged();
}

void ClipButton::mouseUp(const juce::MouseEvent&)
{
    down = false;
    repaint();
    if (dragging)
        return;
    const auto file = save();
    if (file.existsAsFile())
    {
        juce::SystemClipboard::copyTextToClipboard(file.getFullPathName());
        status = "SAVED TO MUSIC / ANIMAL AUDIO. DRAG THE BUTTON TO A TRACK";
    }
    else
    {
        status = "ADD A CHORD FIRST";
    }
    if (onStatusChanged)
        onStatusChanged();
}

void ClipButton::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const float radius = area.getHeight() * 0.5f;
    g.setColour(down ? colours.accent : colours.panelRaised);
    g.fillRoundedRectangle(area, radius);
    g.setColour(down ? colours.accent : colours.accent.withAlpha(0.7f));
    g.drawRoundedRectangle(area, radius, 1.2f);

    // A little clip glyph: three note bars.
    const auto glyph = area.withWidth(area.getHeight()).reduced(area.getHeight() * 0.28f).translated(8.0f, 0.0f);
    g.setColour(down ? colours.onAccent : colours.accent);
    for (int i = 0; i < 3; ++i)
    {
        const float y = glyph.getY() + glyph.getHeight() * (0.15f + 0.3f * static_cast<float>(i));
        const float x = glyph.getX() + glyph.getWidth() * (0.1f * static_cast<float>(i % 2));
        g.fillRoundedRectangle(x, y, glyph.getWidth() * 0.8f, 2.6f, 1.2f);
    }

    g.setColour(down ? colours.onAccent : colours.text);
    g.setFont(makeFont(11.5f, true, 0.12f));
    g.drawText("COPY MIDI CLIP", area.withTrimmedLeft(area.getHeight() * 0.9f), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------------------------------------------

ArrangeView::ArrangeView(DolphinFlowProcessor& p, const animalaudio::ui::Theme& themeToUse)
    : processor(p)
    , theme(themeToUse)
    , loopButton("LOOP", themeToUse.palette)
    , clipButton(p, themeToUse.palette)
{
    setInterceptsMouseClicks(false, true);
    for (int i = 0; i < dsp::kNumSlots; ++i)
    {
        auto cell = std::make_unique<SlotCell>(processor, theme.palette, i);
        cell->onSelect = [this](int index) { select(index); };
        addAndMakeVisible(*cell);
        cells[static_cast<std::size_t>(i)] = std::move(cell);
    }
    loopAttachment = std::make_unique<juce::ButtonParameterAttachment>(
        *processor.apvts.getParameter(ids::loop.getParamID()), loopButton);
    loopButton.setClickingTogglesState(true);
    loopButton.setMouseClickGrabsKeyboardFocus(false);
    loopButton.setTooltip("Play the squares in order, round and round. Follows the host's beat while it plays.");
    addAndMakeVisible(loopButton);
    clipButton.onStatusChanged = [this] { repaint(); };
    addAndMakeVisible(clipButton);
    select(0);
}

void ArrangeView::select(int index)
{
    selected = std::clamp(index, -1, dsp::kNumSlots - 1);
    for (int i = 0; i < dsp::kNumSlots; ++i)
        cells[static_cast<std::size_t>(i)]->setSelected(i == selected);
}

bool ArrangeView::writeSelected(int rootPitchClass, int shape)
{
    if (selected < 0)
        return false;
    auto slot = processor.getSlot(selected);
    slot.root = dsp::pitchClassOf(rootPitchClass);
    slot.shape = std::clamp(shape, 0, dsp::kNumChordShapes - 1);
    processor.setSlot(selected, slot);
    select((selected + 1) % dsp::kNumSlots);
    refresh();
    return true;
}

void ArrangeView::clearSelected()
{
    if (selected < 0)
        return;
    auto slot = processor.getSlot(selected);
    slot.root = -1;
    processor.setSlot(selected, slot);
    refresh();
}

void ArrangeView::refresh()
{
    for (auto& cell : cells)
        cell->repaint();
    repaint();
}

void ArrangeView::paint(juce::Graphics& g)
{
    const auto status = clipButton.getStatus();
    const auto line = juce::Rectangle<float>(
        static_cast<float>(clipButton.getRight() + 14), static_cast<float>(clipButton.getY()),
        static_cast<float>(getWidth() - clipButton.getRight() - 14), static_cast<float>(clipButton.getHeight()));
    g.setColour(theme.palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.1f));
    g.drawFittedText(status.isNotEmpty() ? status : "SELECT A SQUARE, PICK A CHORD (1-0), PLAY A ROOT",
                     line.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
}

void ArrangeView::resized()
{
    constexpr int gap = 8;
    constexpr int controlsH = 30;
    auto area = getLocalBounds();
    auto controls = area.removeFromBottom(controlsH);
    area.removeFromBottom(10);
    const int cellW = (area.getWidth() - gap * 3) / 4;
    const int cellH = (area.getHeight() - gap) / 2;
    for (int i = 0; i < dsp::kNumSlots; ++i)
    {
        const int col = i % 4;
        const int row = i / 4;
        cells[static_cast<std::size_t>(i)]->setBounds(area.getX() + col * (cellW + gap),
                                                      area.getY() + row * (cellH + gap), cellW, cellH);
    }
    loopButton.setBounds(controls.removeFromLeft(86));
    controls.removeFromLeft(10);
    clipButton.setBounds(controls.removeFromLeft(178));
}
} // namespace dolphinflow::ui
