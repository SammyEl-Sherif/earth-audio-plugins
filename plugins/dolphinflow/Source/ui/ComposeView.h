#pragma once

#include "ComposeContext.h"
#include "PaletteView.h"
#include "PianoRoll.h"
#include "TimelineView.h"

#include <animalaudio/ui/PillButton.h>

#include <memory>
#include <vector>

namespace dolphinflow::ui
{
/** One track in the list: solo, mute, name, volume. The chord track also switches PALETTE / VOICING. */
class TrackRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    TrackRow(ComposeContext& context, int index);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void refresh();

    int getIndex() const noexcept { return index; }
    juce::Button& getSoloButton() noexcept { return solo; }
    juce::Button& getMuteButton() noexcept { return mute; }
    juce::Slider& getVolume() noexcept { return volume; }
    juce::Button& getVoicingButton() noexcept { return voicingButton; }

    std::function<void(int)> onSelect;
    std::function<void(int)> onRename;
    std::function<void(int)> onMenu;

private:
    void setMix(const std::function<void(dsp::TrackMix&)>& change, bool undoable = true);

    ComposeContext& ctx;
    int index; // -1: the chord track
    animalaudio::ui::PillButton solo;
    animalaudio::ui::PillButton mute;
    animalaudio::ui::PillButton paletteButton;
    animalaudio::ui::PillButton voicingButton;
    juce::Slider volume;
    dsp::Song volumeBefore;
};

/** Drag it onto a host track to drop the selected part as a MIDI clip; click saves the .mid and copies its path. */
class ExportButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ExportButton(ComposeContext& context);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /** What a drag exports now: the selected chords' range or the whole chord track, a melody track, or (alt) all. */
    ClipScope scope(bool everything) const;
    juce::File save(bool everything);
    const juce::String& getStatus() const noexcept { return status; }

private:
    ComposeContext& ctx;
    bool dragging = false;
    bool down = false;
    juce::String status;
};

/**
 * COMPOSE: write a song. Song key and transport across the top, the tracks down the left, sections and chords along
 * the timeline, and below it the chord palette (chord track) or the piano roll (a melody track, or the chord voicings).
 */
class ComposeView : public juce::Component, public juce::DragAndDropContainer
{
public:
    ComposeView(DolphinFlowProcessor& processor, const animalaudio::ui::Theme& theme);
    ~ComposeView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    /** Timer tick: playhead, lit chords, play button. */
    void refresh();
    /** Song edits, transport and piano roll keys; false lets the editor play roots. */
    bool handleKey(const juce::KeyPress& key);

    void selectTrack(int index);
    void addTrack(dsp::TrackRole role);

    ComposeContext& getContext() noexcept { return ctx; }
    TimelineView& getTimeline() noexcept { return timeline; }
    PaletteView& getPalette() noexcept { return palette; }
    PianoRoll& getPianoRoll() noexcept { return roll; }
    ExportButton& getExportButton() noexcept { return exportButton; }
    TrackRow& getTrackRow(int index); // -1: chord track
    juce::Button& getPlayButton() noexcept { return playButton; }
    juce::Button& getRewindButton() noexcept { return rewindButton; }
    juce::Button& getLoopButton() noexcept { return loopButton; }
    juce::Button& getLinkButton() noexcept { return linkButton; }
    juce::Button& getAddTrackButton() noexcept { return addTrackButton; }
    juce::Button& getNumeralsButton() noexcept { return numeralsButton; }
    juce::Button& getNamesButton() noexcept { return namesButton; }

private:
    void rebuildTracks();
    void showKeyMenu();
    void showScaleMenu();
    void showSnapMenu();
    void showAddTrackMenu();
    void showTrackMenu(int index);
    void renameTrack(int index);
    void layoutLower();
    void syncAll();

    ComposeContext ctx;
    TimelineView timeline;
    PaletteView palette;
    PianoRoll roll;
    ExportButton exportButton;
    std::vector<std::unique_ptr<TrackRow>> rows;
    animalaudio::ui::PillButton keyButton;
    animalaudio::ui::PillButton scaleButton;
    animalaudio::ui::PillButton rewindButton;
    animalaudio::ui::PillButton playButton;
    animalaudio::ui::PillButton loopButton;
    animalaudio::ui::PillButton linkButton;
    animalaudio::ui::PillButton snapButton;
    animalaudio::ui::PillButton namesButton;
    animalaudio::ui::PillButton numeralsButton;
    animalaudio::ui::PillButton undoButton;
    animalaudio::ui::PillButton redoButton;
    animalaudio::ui::PillButton addTrackButton;
    std::unique_ptr<juce::ButtonParameterAttachment> loopAttachment;
    std::unique_ptr<juce::TextEditor> nameEditor;
    juce::LookAndFeel_V4 menuLook;
    juce::Rectangle<int> trackPanel;
    juce::Rectangle<int> lowerArea;
    int shownTracks = -1;
    int lastSelectedTrack = -2;
    bool wasPlaying = false;
};
} // namespace dolphinflow::ui
