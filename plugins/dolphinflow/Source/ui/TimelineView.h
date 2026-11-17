#pragma once

#include "ComposeContext.h"

#include <memory>

namespace dolphinflow::ui
{
/**
 * The song across time: a lane of sections (Intro, Verse, ...), a lane of chords and a bar ruler with the playhead.
 *
 * Chords: click to select (and hear it), shift adds, drag the right edge to change its length, drag the body to move
 * it (alt copies), right-click for more. Drop a palette chord anywhere to insert it there. Sections: click selects its
 * chords, double-click renames, right-click recolours / duplicates / deletes, + adds one. Click the ruler to move the
 * playhead. Wheel scrolls, alt / cmd + wheel zooms.
 */
class TimelineView : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient
{
public:
    static constexpr int kSectionTop = 0;
    static constexpr int kSectionH = 24;
    static constexpr int kChordTop = 28;
    static constexpr int kChordH = 42;
    static constexpr int kRulerTop = 74;
    static constexpr int kRulerH = 18;
    static constexpr int kHeight = 94;

    explicit TimelineView(ComposeContext& context);
    ~TimelineView() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails& details) override;

    /** Chord / section under a point (local coordinates), -1 if none. */
    int chordAt(juce::Point<float> p) const;
    int sectionAt(juce::Point<float> p) const;
    juce::Rectangle<float> chordBounds(int flat) const;
    juce::Rectangle<float> sectionBounds(int section) const;
    juce::Rectangle<float> addSectionBounds() const;

    void addSection();
    void renameSection(int section);
    void deleteSelectedChords();

private:
    enum class Drag
    {
        None,
        Resize,
        Move,
        Seek
    };

    void showChordMenu(int flat);
    void showSectionMenu(int section);
    void stopPreview();

    ComposeContext& ctx;
    Drag drag = Drag::None;
    int dragChord = -1;
    dsp::Song before; // the song when the drag began
    double dragStartBeats = 0.0;
    double dropBeat = -1.0; // insert caret while something is dragged over the chord lane
    bool previewing = false;
    std::unique_ptr<juce::TextEditor> nameEditor;
    juce::LookAndFeel_V4 menuLook;
};
} // namespace dolphinflow::ui
