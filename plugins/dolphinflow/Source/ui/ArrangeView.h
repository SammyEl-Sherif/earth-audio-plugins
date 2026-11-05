#pragma once

#include "DolphinFlowProcessor.h" // Source/ is on the include path

#include <animalaudio/ui/PillButton.h>
#include <animalaudio/ui/Theme.h>

#include <array>
#include <functional>
#include <memory>

namespace dolphinflow::ui
{
/**
 * One square of the arrangement: its number, chord and length. Click selects it (the next root you play is written
 * into it), the - / + corners change its length, right-click empties it. Lit and filling while the loop plays it.
 */
class SlotCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotCell(DolphinFlowProcessor& processor, const animalaudio::ui::Palette& palette, int index);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    std::function<void(int)> onSelect;
    void setSelected(bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Hit areas of the length steppers, in local coordinates. */
    juce::Rectangle<float> lengthDownArea() const;
    juce::Rectangle<float> lengthUpArea() const;

private:
    void stepLength(int delta);

    DolphinFlowProcessor& processor;
    animalaudio::ui::Palette colours;
    int index;
    bool selected = false;
};

/**
 * Drag it onto a track in the host to drop one pass of the loop as a MIDI clip. A click saves the same .mid file
 * (to ~/Music/Animal Audio/Dolphin Flow) and copies its path to the clipboard.
 */
class ClipButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    ClipButton(DolphinFlowProcessor& processor, const animalaudio::ui::Palette& palette);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /** Writes the clip; returns the file, or a non-existent file on failure. */
    juce::File save();
    /** What the last save did, for the status line. */
    const juce::String& getStatus() const noexcept { return status; }
    std::function<void()> onStatusChanged;

private:
    DolphinFlowProcessor& processor;
    animalaudio::ui::Palette colours;
    bool dragging = false;
    bool down = false;
    juce::String status;
};

/** The arrangement view: eight chord squares in two rows, LOOP, and the MIDI clip button. */
class ArrangeView : public juce::Component
{
public:
    ArrangeView(DolphinFlowProcessor& processor, const animalaudio::ui::Theme& theme);

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getSelected() const noexcept { return selected; }
    void select(int index);
    /** Writes a chord into the selected square and moves the selection on. Returns false if none is selected. */
    bool writeSelected(int rootPitchClass, int shape);
    void clearSelected();
    void refresh();

    SlotCell& getCell(int index) noexcept { return *cells[static_cast<std::size_t>(index)]; }
    juce::Button& getLoopButton() noexcept { return loopButton; }
    ClipButton& getClipButton() noexcept { return clipButton; }

private:
    DolphinFlowProcessor& processor;
    animalaudio::ui::Theme theme;
    std::array<std::unique_ptr<SlotCell>, dsp::kNumSlots> cells;
    animalaudio::ui::PillButton loopButton;
    std::unique_ptr<juce::ButtonParameterAttachment> loopAttachment;
    ClipButton clipButton;
    int selected = 0;
};
} // namespace dolphinflow::ui
