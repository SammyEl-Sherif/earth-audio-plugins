#pragma once

#include "ComposeContext.h"

#include <animalaudio/ui/PillButton.h>

#include <array>
#include <functional>
#include <memory>

namespace dolphinflow::ui
{
/**
 * One chord circle. Held, it plays (the CHORD parameter follows it); dragged onto the timeline it is inserted there;
 * double-clicked it goes after the selected chord (or at the end). Filled when every note is in the home key, hollow
 * when it borrows; aqua major, violet minor, grey other.
 */
class PaletteCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    PaletteCell(ComposeContext& context, int row, int degree);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    void setChord(const dsp::PaletteChord& newChord);
    const dsp::PaletteChord& getChord() const noexcept { return chord; }
    bool isEmpty() const noexcept { return chord.shape < 0; }
    void setLit(bool shouldBeLit);

    /** Puts this chord into the song after the selection. */
    void write();

    /** Drag description: "chord:<root>:<shape>". */
    static juce::var describe(int root, int shape);
    static bool parse(const juce::var& description, int& root, int& shape);

private:
    int source() const noexcept;
    void stop();

    ComposeContext& ctx;
    int row;
    int degree;
    dsp::PaletteChord chord;
    bool lit = false;
    bool playing = false;
    bool dragging = false;
};

/**
 * The chord palette: the key's chords by scale degree (columns) and colour (rows: dominant-of, ninth, add9, seventh,
 * sixth, suspended, triad), with a bar to borrow from neighbouring keys (circle of fifths) or a parallel mode, and
 * CHORD RELATIONS, which rings the chords that follow on strongly (a fifth down, a leading tone) or smoothly (two
 * shared notes) from the chord last clicked. Melody notes sent here with "find chords" dim every chord that does not
 * contain them all.
 */
class PaletteView : public juce::Component
{
public:
    static constexpr int kBarH = 40;

    explicit PaletteView(ComposeContext& context);
    ~PaletteView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refresh();
    void borrow(int steps);
    void goHome();
    void setPaletteScale(int scale);

    PaletteCell& getCell(int row, int degree) noexcept;
    juce::Button& getPrevButton() noexcept { return prevButton; }
    juce::Button& getNextButton() noexcept { return nextButton; }
    juce::Button& getHomeButton() noexcept { return homeButton; }
    juce::Button& getRelationsButton() noexcept { return relationsButton; }
    juce::Button& getMatchButton() noexcept { return matchButton; }

private:
    void showScaleMenu();

    ComposeContext& ctx;
    std::array<std::unique_ptr<PaletteCell>, dsp::kCircleRows * dsp::kDegrees> cells;
    animalaudio::ui::PillButton homeButton;
    animalaudio::ui::PillButton prevButton;
    animalaudio::ui::PillButton borrowButton;
    animalaudio::ui::PillButton nextButton;
    animalaudio::ui::PillButton relationsButton;
    animalaudio::ui::PillButton matchButton;
    juce::LookAndFeel_V4 menuLook;
    juce::Rectangle<int> grid;
    dsp::PaletteKey shown;
    bool filled = false;
    bool shownNumerals = false;
};
} // namespace dolphinflow::ui
