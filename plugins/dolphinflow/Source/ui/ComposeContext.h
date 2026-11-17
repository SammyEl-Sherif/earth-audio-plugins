#pragma once

#include "DolphinFlowProcessor.h" // Source/ is on the include path

#include <animalaudio/ui/Theme.h>

#include <functional>
#include <vector>

namespace dolphinflow::ui
{
/** Beats <-> pixels, shared by the timeline and the piano roll so their columns line up. */
struct TimeAxis
{
    static constexpr int kGutter = 56; // left column: lane names / piano keys
    double pixelsPerBeat = 22.0;
    double scrollBeats = 0.0;

    float xOf(double beat) const noexcept { return static_cast<float>(kGutter + (beat - scrollBeats) * pixelsPerBeat); }
    double beatAt(float x) const noexcept { return scrollBeats + (x - kGutter) / pixelsPerBeat; }
    void zoom(double factor, double aroundBeat) noexcept
    {
        const double offset = aroundBeat - scrollBeats;
        pixelsPerBeat = std::clamp(pixelsPerBeat * factor, 4.0, 160.0);
        scrollBeats = std::max(0.0, aroundBeat - offset / factor);
    }
};

/**
 * What the COMPOSE view's parts share: which track is selected (the chord track, or a melody track), the selection
 * inside it, the grid, and how chords are named. Edits go through edit(), which hands the song to the processor
 * (one undo step) and repaints everything.
 */
class ComposeContext
{
public:
    ComposeContext(DolphinFlowProcessor& p, const animalaudio::ui::Theme& t)
        : processor(p)
        , theme(t)
    {
    }

    DolphinFlowProcessor& processor;
    animalaudio::ui::Theme theme;
    TimeAxis axis;

    int selectedTrack = -1;             // -1: the chord track
    std::vector<int> selectedChords;    // flat chord indices (placeChords order)
    std::vector<int> selectedNotes;     // indices into the selected melody track's notes
    double snap = 1.0;                  // beats; 0 = off
    bool numerals = false;              // chords shown as "vi" instead of "Am"
    bool relations = false;             // palette rings the chords that follow the focused one
    bool voicing = false;               // chord track shows its voicings in the piano roll
    bool velocityMode = false;          // piano roll drags change velocity
    unsigned matchMask = 0;             // palette dims chords that do not contain these pitch classes
    int focusRoot = -1, focusShape = 0; // the chord relations are measured from

    std::function<void()> onChanged; // repaint every part

    void changed()
    {
        if (onChanged)
            onChanged();
    }

    /** One undoable edit of the song. */
    void edit(const std::function<void(dsp::Song&)>& change)
    {
        auto song = processor.getSong();
        change(song);
        dsp::tidySections(song);
        processor.setSong(song);
        changed();
    }

    dsp::PaletteKey key() const { return processor.getPaletteKey(); }

    /** "Am7" or "vi7" (UTF-8), as the Am | vi switch says. */
    juce::String chordLabel(int root, int shape) const
    {
        const auto k = key();
        return juce::String::fromUTF8(numerals ? dsp::romanNumeral(root, shape, k.homeTonic, k.homeScale).text
                                               : dsp::chordNameInKey(root, shape, k.homeTonic, k.homeScale).text);
    }

    /** Major family aqua, minor violet, the rest grey: the palette's colours. */
    juce::Colour chordColour(int shape) const
    {
        switch (dsp::shapeTheory(shape).quality)
        {
        case dsp::Quality::Major:
            return theme.palette.accent;
        case dsp::Quality::Minor:
            return theme.palette.accentAlt;
        case dsp::Quality::Other:
            break;
        }
        return theme.palette.textDim;
    }

    /** Section colours: Dolphin's own hues. */
    juce::Colour sectionColour(int index) const
    {
        const auto& p = theme.palette;
        const juce::Colour colours[] = {p.accent.withMultipliedSaturation(0.55f).withMultipliedBrightness(0.62f),
                                        p.accentAlt.withMultipliedSaturation(0.6f).withMultipliedBrightness(0.6f),
                                        p.ambient.withMultipliedSaturation(0.6f).withMultipliedBrightness(0.6f),
                                        p.detail.withMultipliedSaturation(0.55f).withMultipliedBrightness(0.6f),
                                        juce::Colour::fromHSV(0.8f, 0.4f, 0.55f, 1.0f),
                                        juce::Colour::fromHSV(0.14f, 0.35f, 0.55f, 1.0f)};
        return colours[static_cast<std::size_t>(((index % 6) + 6) % 6)];
    }

    /** Colour a melody track draws its notes in. */
    juce::Colour trackColour(int track) const
    {
        const auto& p = theme.palette;
        const juce::Colour colours[] = {p.detail, p.ambient, p.accentAlt.brighter(0.2f), p.accent.brighter(0.3f)};
        return colours[static_cast<std::size_t>(((track % 4) + 4) % 4)];
    }

    void clearSelection()
    {
        selectedChords.clear();
        selectedNotes.clear();
    }

    /** Selected chords as a [from, to) range in beats, or false when none. */
    bool selectedRange(double& from, double& to) const
    {
        if (selectedChords.empty())
            return false;
        const auto chords = dsp::placeChords(processor.getSong());
        from = 1.0e9;
        to = 0.0;
        for (int i : selectedChords)
            if (i >= 0 && i < static_cast<int>(chords.size()))
            {
                from = std::min(from, chords[static_cast<std::size_t>(i)].start);
                to = std::max(to, chords[static_cast<std::size_t>(i)].end());
            }
        return to > from;
    }
};

/** Draws "Dm" + raised "7/9": the palette's and timeline's chord text, extension set small and high. */
void drawChordText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size,
                   juce::Colour colour);
} // namespace dolphinflow::ui
