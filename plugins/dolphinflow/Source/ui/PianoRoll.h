#pragma once

#include "ComposeContext.h"

#include <animalaudio/ui/PillButton.h>

#include <vector>

namespace dolphinflow::ui
{
/**
 * The piano roll of a melody track (lead, bass...), lined up under the timeline. Every row is shaded by how its note
 * sits against the chord above: the root strongest, then chord tones, then extensions, in-key passing notes plain,
 * out-of-key notes darkened. Other tracks show as ghost notes.
 *
 * Click an empty spot to add a note (it plays), drag one to move it (alt copies), drag its ends to change its
 * length, double-click deletes. Drag over empty space to select a group, shift adds. VELOCITY mode: drag up / down
 * on a note. Keys: delete, cmd-A / C / X / V / D, arrows move (shift: an octave), cmd-Z undo. Right-click a selection
 * to set the chord above from it, or to find palette chords that fit it.
 *
 * With the chord track selected in VOICING mode it shows the chord notes as they are voiced, read only.
 */
class PianoRoll : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kBarH = 40;

    explicit PianoRoll(ComposeContext& context);
    ~PianoRoll() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    /** Editing keys, forwarded by the editor (which holds keyboard focus). */
    bool handleKey(const juce::KeyPress& key);

    /** Scrolls and zooms so the whole song and every note of the track show. */
    void fit();
    /** Centres the rows on a pitch (a new track: bass low, lead high). */
    void centreOn(int pitch);

    bool isReadOnly() const noexcept { return ctx.selectedTrack < 0; }
    int noteAt(juce::Point<float> p) const;
    juce::Rectangle<float> noteBounds(const dsp::MelodyNote& note) const;
    float yOfPitch(int pitch) const noexcept;
    int pitchAt(float y) const noexcept;
    juce::Rectangle<float> rollArea() const;

    /** Pitch classes of the selected notes become the palette's match filter; the palette opens. */
    void findChordsForSelection();
    /** The chord sounding at `beat` becomes the chord the selected notes spell. False if they spell none. */
    bool setChordFromSelection(double beat);
    void deleteSelection();
    void selectAll();
    void copySelection(bool cut);
    void paste(double atBeat);
    void duplicateSelection();
    void nudgeSelection(double beats, int semitones);

    juce::Button& getFitButton() noexcept { return fitButton; }
    juce::Button& getVelocityButton() noexcept { return velocityButton; }
    juce::Button& getFindButton() noexcept { return findButton; }

private:
    enum class Drag
    {
        None,
        Pending, // pressed on empty space: a click adds a note, a drag selects
        Select,
        Move,
        ResizeEnd,
        ResizeStart,
        Velocity
    };

    struct Marked
    {
        dsp::MelodyNote note;
        bool selected;
    };

    const dsp::MelodyTrack* track() const;
    std::vector<Marked> marked() const;
    /** Writes the track's notes back (sorted, selection kept), undoable or not. */
    void commit(std::vector<Marked> notes, bool undoable);
    void showNoteMenu(double beat);
    void preview(int pitch, int velocity = 100);

    ComposeContext& ctx;
    double rowH = 13.0;
    double topPitch = 79.0;
    Drag drag = Drag::None;
    dsp::Song before;
    std::vector<Marked> dragStart;
    int anchor = -1; // the note the drag started on (index into dragStart)
    bool copied = false;
    juce::Point<float> downAt;
    juce::Rectangle<float> rubber;
    double lastLength = 1.0;
    double mouseBeat = 0.0;
    std::vector<dsp::MelodyNote> clipboard;
    animalaudio::ui::PillButton fitButton;
    animalaudio::ui::PillButton velocityButton;
    animalaudio::ui::PillButton findButton;
    juce::LookAndFeel_V4 menuLook;
};
} // namespace dolphinflow::ui
