#pragma once

#include "ArrangeView.h"
#include "DolphinFlowProcessor.h" // Source/ is on the include path

#include <animalaudio/ui/PillButton.h>
#include <animalaudio/ui/Theme.h>

#include <array>
#include <functional>
#include <memory>

namespace dolphinflow::ui
{
/** Colour a chord is drawn in on the palette and the strip: major, minor or other (dim / aug / sus). */
juce::Colour qualityColour(const animalaudio::ui::Palette& palette, int shape);

/**
 * One chord on the palette. Held, it plays (the CHORD parameter follows it). Dragged onto a square of the strip it
 * is written there; double-clicked it goes into the selected square. Filled when every note is in the home key,
 * hollow when it borrows a note from outside.
 */
class PaletteCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    PaletteCell(DolphinFlowProcessor& processor, const animalaudio::ui::Palette& palette, int row, int degree);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    void setChord(const dsp::PaletteChord& newChord);
    const dsp::PaletteChord& getChord() const noexcept { return chord; }
    void setLit(bool shouldBeLit);

    /** Asked to write this chord into the arrangement's selected square. */
    std::function<void(const dsp::PaletteChord&)> onWrite;

    /** Drag description: "chord:<root>:<shape>". */
    static juce::var describe(int root, int shape);
    static bool parse(const juce::var& description, int& root, int& shape);

private:
    int source() const noexcept;
    void stop();

    DolphinFlowProcessor& processor;
    animalaudio::ui::Palette colours;
    int row;
    int degree;
    dsp::PaletteChord chord;
    bool lit = false;
    bool playing = false;
    bool dragging = false;
};

/** A square of the arrangement, small: click selects it, drop a palette chord on it to write it. */
class StripCell : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    StripCell(DolphinFlowProcessor& processor, const animalaudio::ui::Palette& palette, int index);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails& details) override;

    void setSelected(bool shouldBeSelected);
    std::function<void(int)> onSelect;
    std::function<void(int)> onDropped; // after a chord was written into this square

private:
    DolphinFlowProcessor& processor;
    animalaudio::ui::Palette colours;
    int index;
    bool selected = false;
    bool hovered = false;
};

/**
 * Chords that belong together. The palette lays out the home key's chords by scale degree (columns) and colour
 * (rows: triads, sevenths, ninths / suspensions, and the dominant that leads into each degree), names each with its
 * roman numeral, and can borrow from neighbouring keys round the circle of fifths or from a parallel mode. A strip of
 * the eight arrangement squares sits underneath to build a progression from it.
 */
class PaletteView : public juce::Component, public juce::DragAndDropContainer
{
public:
    PaletteView(DolphinFlowProcessor& processor, const animalaudio::ui::Theme& theme, ArrangeView& arrange);
    ~PaletteView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Re-reads key, borrow and arrangement; lights the sounding chord. */
    void refresh();
    /** Moves the palette `steps` fifths round the circle (clamped to six either way). */
    void borrow(int steps);
    void goHome();
    void setPaletteScale(int scale);

    PaletteCell& getCell(int row, int degree) noexcept;
    StripCell& getStripCell(int index) noexcept { return *strip[static_cast<std::size_t>(index)]; }
    juce::Button& getPrevButton() noexcept { return prevButton; }
    juce::Button& getNextButton() noexcept { return nextButton; }
    juce::Button& getHomeButton() noexcept { return homeButton; }

private:
    void showKeyMenu();
    void showScaleMenu(bool home);
    void select(int index);

    DolphinFlowProcessor& processor;
    animalaudio::ui::Theme theme;
    ArrangeView& arrange;
    std::array<std::unique_ptr<PaletteCell>, dsp::kPaletteRows * dsp::kPaletteColumns> cells;
    std::array<std::unique_ptr<StripCell>, dsp::kNumSlots> strip;
    animalaudio::ui::PillButton keyButton;
    animalaudio::ui::PillButton scaleButton;
    animalaudio::ui::PillButton prevButton;
    animalaudio::ui::PillButton borrowButton;
    animalaudio::ui::PillButton nextButton;
    animalaudio::ui::PillButton homeButton;
    animalaudio::ui::PillButton loopButton;
    std::unique_ptr<juce::ButtonParameterAttachment> loopAttachment;
    juce::LookAndFeel_V4 menuLook;
    juce::Rectangle<int> gridArea;
    dsp::PaletteKey shown;
    bool filled = false; // cells hold chords for `shown`
};
} // namespace dolphinflow::ui
