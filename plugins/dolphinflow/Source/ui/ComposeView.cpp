#include "ComposeView.h"

#include <algorithm>
#include <cmath>

namespace dolphinflow::ui
{
namespace
{
using animalaudio::ui::makeFont;

constexpr int kTopH = 40;
constexpr int kPanelW = 248;
constexpr int kMargin = 16;
constexpr int kChordRowH = 92;
constexpr int kTrackRowH = 74;
constexpr int kExportW = 216;

void setupMenu(juce::LookAndFeel_V4& look, const animalaudio::ui::Palette& p)
{
    look.setColour(juce::PopupMenu::backgroundColourId, p.panel);
    look.setColour(juce::PopupMenu::textColourId, p.text);
    look.setColour(juce::PopupMenu::headerTextColourId, p.textDim);
    look.setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    look.setColour(juce::PopupMenu::highlightedTextColourId, p.onAccent);
}

void glyphRewind(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre(11.0f, 10.0f);
    g.setColour(c);
    g.fillRect(b.getX(), b.getY(), 2.0f, b.getHeight());
    juce::Path p;
    p.addTriangle(b.getRight(), b.getY(), b.getX() + 2.5f, b.getCentreY(), b.getRight(), b.getBottom());
    g.fillPath(p);
}

void glyphPlay(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool pause)
{
    const auto b = r.withSizeKeepingCentre(10.0f, 11.0f);
    g.setColour(c);
    if (pause)
    {
        g.fillRect(b.getX() + 1.0f, b.getY(), 3.0f, b.getHeight());
        g.fillRect(b.getRight() - 4.0f, b.getY(), 3.0f, b.getHeight());
        return;
    }
    juce::Path p;
    p.addTriangle(b.getX(), b.getY(), b.getRight(), b.getCentreY(), b.getX(), b.getBottom());
    g.fillPath(p);
}

void glyphLoop(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre(14.0f, 10.0f);
    juce::Path p;
    p.addRoundedRectangle(b, 4.5f);
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(1.6f));
    juce::Path head;
    head.addTriangle(b.getRight() - 4.0f, b.getY() - 3.0f, b.getRight() - 4.0f, b.getY() + 3.0f, b.getRight() + 1.0f,
                     b.getY());
    g.fillPath(head);
}

void glyphLink(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto b = r.withSizeKeepingCentre(16.0f, 8.0f);
    g.setColour(c);
    g.drawRoundedRectangle(b.withWidth(10.0f), 4.0f, 1.6f);
    g.drawRoundedRectangle(b.withTrimmedLeft(6.0f), 4.0f, 1.6f);
}

void glyphUndo(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool redo)
{
    const auto b = r.withSizeKeepingCentre(12.0f, 10.0f);
    juce::Path p;
    if (redo)
    {
        p.startNewSubPath(b.getX(), b.getBottom());
        p.quadraticTo(b.getX(), b.getY() + 3.0f, b.getRight() - 2.0f, b.getY() + 3.0f);
    }
    else
    {
        p.startNewSubPath(b.getRight(), b.getBottom());
        p.quadraticTo(b.getRight(), b.getY() + 3.0f, b.getX() + 2.0f, b.getY() + 3.0f);
    }
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path head;
    const float hx = redo ? b.getRight() : b.getX();
    const float dir = redo ? -1.0f : 1.0f;
    head.addTriangle(hx, b.getY() + 3.0f, hx + dir * 4.0f, b.getY(), hx + dir * 4.0f, b.getY() + 6.0f);
    g.fillPath(head);
}

juce::String snapName(double snap)
{
    if (snap <= 0.0)
        return "OFF";
    if (snap >= 4.0)
        return "1 BAR";
    return "1/" + juce::String(juce::roundToInt(4.0 / snap));
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------

TrackRow::TrackRow(ComposeContext& context, int i)
    : ctx(context)
    , index(i)
    , solo("S", context.theme.palette)
    , mute("M", context.theme.palette)
    , paletteButton("CHORDS", context.theme.palette)
    , voicingButton("VOICING", context.theme.palette)
{
    setMouseClickGrabsKeyboardFocus(false);
    solo.setTooltip("Solo: only soloed tracks play.");
    mute.setTooltip("Mute this track.");
    solo.onClick = [this] { setMix([](dsp::TrackMix& m) { m.solo = !m.solo; }); };
    mute.onClick = [this] { setMix([](dsp::TrackMix& m) { m.mute = !m.mute; }); };
    for (auto* b : {&solo, &mute, &paletteButton, &voicingButton})
    {
        b->setClickingTogglesState(false);
        b->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    paletteButton.setVisible(index < 0);
    voicingButton.setVisible(index < 0);
    paletteButton.setTooltip("The chord palette under the timeline.");
    voicingButton.setTooltip("The chord notes as they are voiced, in the piano roll.");
    paletteButton.onClick = [this]
    {
        ctx.voicing = false;
        if (onSelect)
            onSelect(index);
    };
    voicingButton.onClick = [this]
    {
        ctx.voicing = true;
        if (onSelect)
            onSelect(index);
    };

    const auto& p = ctx.theme.palette;
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange(0.0, 1.0, 0.0);
    volume.setColour(juce::Slider::trackColourId, p.accent.withAlpha(0.8f));
    volume.setColour(juce::Slider::backgroundColourId, p.background);
    volume.setColour(juce::Slider::thumbColourId, p.text);
    volume.setMouseClickGrabsKeyboardFocus(false);
    volume.setTooltip(index < 0 ? "Chord track volume (MIDI velocity)." : "Track volume (MIDI velocity).");
    volume.onDragStart = [this] { volumeBefore = ctx.processor.getSong(); };
    volume.onValueChange = [this]
    {
        const auto v = static_cast<float>(volume.getValue());
        setMix([v](dsp::TrackMix& m) { m.volume = v; }, false);
    };
    volume.onDragEnd = [this] { ctx.processor.pushUndo(volumeBefore); };
    addAndMakeVisible(volume);
    refresh();
}

void TrackRow::setMix(const std::function<void(dsp::TrackMix&)>& change, bool undoable)
{
    auto song = ctx.processor.getSong();
    if (index < 0)
        change(song.chordMix);
    else if (index < static_cast<int>(song.tracks.size()))
        change(song.tracks[static_cast<std::size_t>(index)].mix);
    ctx.processor.setSong(song, undoable);
    ctx.changed();
}

void TrackRow::refresh()
{
    const auto& song = ctx.processor.getSong();
    const dsp::TrackMix* mix = index < 0 ? &song.chordMix
                               : index < static_cast<int>(song.tracks.size())
                                   ? &song.tracks[static_cast<std::size_t>(index)].mix
                                   : nullptr;
    if (mix == nullptr)
        return;
    solo.setToggleState(mix->solo, juce::dontSendNotification);
    mute.setToggleState(mix->mute, juce::dontSendNotification);
    if (!volume.isMouseButtonDown())
        volume.setValue(mix->volume, juce::dontSendNotification);
    paletteButton.setToggleState(ctx.selectedTrack < 0 && !ctx.voicing, juce::dontSendNotification);
    voicingButton.setToggleState(ctx.selectedTrack < 0 && ctx.voicing, juce::dontSendNotification);
    repaint();
}

void TrackRow::paint(juce::Graphics& g)
{
    const auto& p = ctx.theme.palette;
    const auto& song = ctx.processor.getSong();
    const bool selected = ctx.selectedTrack == index;
    const auto area = getLocalBounds().toFloat().reduced(0.0f, 2.0f);
    g.setColour(selected ? p.panelRaised : p.panel.withAlpha(0.6f));
    g.fillRoundedRectangle(area, 7.0f);
    const auto colour = index < 0 ? p.accent : ctx.trackColour(index);
    if (selected)
    {
        g.setColour(colour);
        g.fillRoundedRectangle(area.withWidth(4.0f), 2.0f);
    }
    const juce::String name =
        index < 0 ? juce::String("Chords") : juce::String(song.tracks[static_cast<std::size_t>(index)].name);
    g.setColour(p.text);
    g.setFont(makeFont(14.0f, true, 0.02f));
    g.drawText(name, juce::Rectangle<float>(68.0f, area.getY() + 6.0f, area.getWidth() - 76.0f, 22.0f),
               juce::Justification::centredLeft, false);
    g.setColour(p.textDim);
    g.setFont(makeFont(8.5f, true, 0.16f));
    const juce::String detail =
        index < 0 ? juce::String("SYNTH + MIDI CH 1")
                  : juce::String(dsp::trackRoleName(song.tracks[static_cast<std::size_t>(index)].role)).toUpperCase() +
                        "  /  MIDI CH " + juce::String(dsp::kFirstTrackChannel + index);
    g.drawText(detail, juce::Rectangle<float>(68.0f, area.getY() + 26.0f, area.getWidth() - 76.0f, 12.0f),
               juce::Justification::centredLeft, false);
}

void TrackRow::resized()
{
    auto area = getLocalBounds().reduced(8, 6);
    auto left = area.removeFromLeft(52);
    auto top = left.removeFromTop(28);
    solo.setBounds(top.removeFromLeft(24).reduced(0, 2));
    top.removeFromLeft(4);
    mute.setBounds(top.removeFromLeft(24).reduced(0, 2));
    area.removeFromLeft(8);
    auto bottom = getLocalBounds().reduced(8, 6).removeFromBottom(index < 0 ? 46 : 22);
    if (index < 0)
    {
        auto modes = bottom.removeFromTop(22);
        modes.removeFromLeft(60);
        paletteButton.setBounds(modes.removeFromLeft(78));
        modes.removeFromLeft(6);
        voicingButton.setBounds(modes.removeFromLeft(78));
        bottom.removeFromTop(4);
    }
    volume.setBounds(bottom.withTrimmedLeft(4));
}

void TrackRow::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && index >= 0)
    {
        if (onMenu)
            onMenu(index);
        return;
    }
    if (onSelect)
        onSelect(index);
}

void TrackRow::mouseDoubleClick(const juce::MouseEvent&)
{
    if (index >= 0 && onRename)
        onRename(index);
}

// ---------------------------------------------------------------------------------------------------------------

ExportButton::ExportButton(ComposeContext& context)
    : ctx(context)
{
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("Drag onto a track to drop this part as a MIDI clip (alt / right-click: the whole song, every track). "
               "Click saves the .mid to Music / Animal Audio and copies its path.");
}

ClipScope ExportButton::scope(bool everything) const
{
    ClipScope s;
    s.allTracks = everything;
    s.track = everything ? -1 : ctx.selectedTrack;
    double from = 0.0, to = 0.0;
    if (!everything && ctx.selectedTrack < 0 && ctx.selectedRange(from, to))
    {
        s.from = from;
        s.to = to;
    }
    return s;
}

juce::File ExportButton::save(bool everything)
{
    const auto s = scope(everything);
    const auto file = ctx.processor.clipFile(s);
    if (ctx.processor.writeMidiClip(file, s))
        return file;
    return {};
}

void ExportButton::mouseDown(const juce::MouseEvent&)
{
    dragging = false;
    down = true;
    repaint();
}

void ExportButton::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    const auto file = save(e.mods.isAltDown() || e.mods.isPopupMenu());
    if (!file.existsAsFile())
    {
        status = "NOTHING TO EXPORT YET";
    }
    else
    {
        status = "DROP IT ON A MIDI TRACK";
        juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
    }
    ctx.changed();
}

void ExportButton::mouseUp(const juce::MouseEvent& e)
{
    down = false;
    repaint();
    if (dragging)
        return;
    const auto file = save(e.mods.isAltDown() || e.mods.isPopupMenu());
    if (file.existsAsFile())
    {
        juce::SystemClipboard::copyTextToClipboard(file.getFullPathName());
        status = "SAVED: " + file.getFileNameWithoutExtension().fromFirstOccurrenceOf("Dolphin Flow - ", false, false);
    }
    else
    {
        status = "NOTHING TO EXPORT YET";
    }
    ctx.changed();
}

void ExportButton::paint(juce::Graphics& g)
{
    const auto& p = ctx.theme.palette;
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(down ? p.accent : p.panelRaised);
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(p.accent.withAlpha(down ? 1.0f : 0.7f));
    g.drawRoundedRectangle(area, 8.0f, 1.2f);

    // Note glyph.
    const auto glyph = area.withWidth(area.getHeight()).reduced(area.getHeight() * 0.27f).translated(6.0f, 0.0f);
    g.setColour(down ? p.onAccent : p.accent);
    g.fillEllipse(glyph.getX(), glyph.getBottom() - 6.0f, 7.0f, 6.0f);
    g.fillRect(glyph.getX() + 5.5f, glyph.getY(), 1.8f, glyph.getHeight() - 3.0f);
    g.fillRect(glyph.getX() + 5.5f, glyph.getY(), 6.0f, 2.0f);

    const auto& song = ctx.processor.getSong();
    juce::String part = "Chords";
    double from = 0.0, to = 0.0;
    if (ctx.selectedTrack >= 0 && ctx.selectedTrack < static_cast<int>(song.tracks.size()))
        part = juce::String(song.tracks[static_cast<std::size_t>(ctx.selectedTrack)].name);
    else if (ctx.selectedRange(from, to))
        part = "Chords (" + juce::String(static_cast<int>(ctx.selectedChords.size())) + " selected)";
    auto text = area.withTrimmedLeft(area.getHeight() * 0.95f).reduced(4.0f, 3.0f);
    g.setColour(down ? p.onAccent : p.text);
    g.setFont(makeFont(12.0f, true, 0.04f));
    g.drawFittedText(part, text.removeFromTop(text.getHeight() * 0.55f).toNearestInt(),
                     juce::Justification::centredLeft, 1, 0.8f);
    g.setColour(down ? p.onAccent.withAlpha(0.8f) : p.textDim);
    g.setFont(makeFont(8.5f, true, 0.1f));
    g.drawFittedText(status.isNotEmpty() ? status : juce::String("DRAG TO EXPORT MIDI CLIP"), text.toNearestInt(),
                     juce::Justification::centredLeft, 1, 0.8f);
}

// ---------------------------------------------------------------------------------------------------------------

ComposeView::ComposeView(DolphinFlowProcessor& processor, const animalaudio::ui::Theme& theme)
    : ctx(processor, theme)
    , timeline(ctx)
    , palette(ctx)
    , roll(ctx)
    , exportButton(ctx)
    , keyButton("C", theme.palette)
    , scaleButton("MAJOR", theme.palette)
    , rewindButton({}, theme.palette)
    , playButton({}, theme.palette)
    , loopButton({}, theme.palette)
    , linkButton({}, theme.palette)
    , snapButton("1/4", theme.palette)
    , namesButton("Am", theme.palette)
    , numeralsButton("vi", theme.palette)
    , undoButton({}, theme.palette)
    , redoButton({}, theme.palette)
    , addTrackButton("+  ADD NEW TRACK", theme.palette)
{
    setupMenu(menuLook, theme.palette);
    ctx.onChanged = [this] { syncAll(); };

    addAndMakeVisible(timeline);
    addChildComponent(palette);
    addChildComponent(roll);
    addAndMakeVisible(exportButton);

    rewindButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { glyphRewind(g, r, c); };
    playButton.glyph = [this](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    { glyphPlay(g, r, c, ctx.processor.isPlaying()); };
    loopButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { glyphLoop(g, r, c); };
    linkButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { glyphLink(g, r, c); };
    undoButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { glyphUndo(g, r, c, false); };
    redoButton.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { glyphUndo(g, r, c, true); };

    keyButton.setTooltip("The song's key. Palette, numerals, piano roll guides and keyboard dots follow it.");
    scaleButton.setTooltip("The song's scale (mode).");
    rewindButton.setTooltip("Back to the start.");
    playButton.setTooltip("Play the song from the playhead (to its end, or round and round with LOOP).");
    loopButton.setTooltip("Loop the song. On its own it also plays it; follows the host's beat while the host plays.");
    linkButton.setTooltip("Link to the host: the song plays whenever the host plays, at the host's position.");
    snapButton.setTooltip("Grid for chords and notes.");
    namesButton.setTooltip("Show chords by name (Am).");
    numeralsButton.setTooltip("Show chords as numerals in the key (vi).");
    undoButton.setTooltip("Undo (cmd-Z).");
    redoButton.setTooltip("Redo (shift-cmd-Z).");
    addTrackButton.setTooltip("A melody track: lead, bass, pad or arp. Each sends MIDI on its own channel.");

    keyButton.onClick = [this] { showKeyMenu(); };
    scaleButton.onClick = [this] { showScaleMenu(); };
    rewindButton.onClick = [this]
    {
        ctx.processor.rewind();
        ctx.axis.scrollBeats = 0.0;
        ctx.changed();
    };
    playButton.onClick = [this]
    {
        ctx.processor.setPlaying(!ctx.processor.isPlaying());
        ctx.changed();
    };
    linkButton.onClick = [this]
    {
        ctx.processor.setLinked(!ctx.processor.isLinked());
        ctx.changed();
    };
    snapButton.onClick = [this] { showSnapMenu(); };
    namesButton.onClick = [this]
    {
        ctx.numerals = false;
        ctx.changed();
    };
    numeralsButton.onClick = [this]
    {
        ctx.numerals = true;
        ctx.changed();
    };
    undoButton.onClick = [this]
    {
        ctx.clearSelection();
        ctx.processor.undo();
        ctx.changed();
    };
    redoButton.onClick = [this]
    {
        ctx.clearSelection();
        ctx.processor.redo();
        ctx.changed();
    };
    addTrackButton.onClick = [this] { showAddTrackMenu(); };
    loopAttachment = std::make_unique<juce::ButtonParameterAttachment>(
        *processor.apvts.getParameter(ids::loop.getParamID()), loopButton);
    loopButton.setClickingTogglesState(true);
    for (auto* b : {&keyButton, &scaleButton, &rewindButton, &playButton, &linkButton, &snapButton, &namesButton,
                    &numeralsButton, &undoButton, &redoButton, &addTrackButton})
        b->setClickingTogglesState(false);
    for (auto* b : {&keyButton, &scaleButton, &rewindButton, &playButton, &loopButton, &linkButton, &snapButton,
                    &namesButton, &numeralsButton, &undoButton, &redoButton, &addTrackButton})
    {
        b->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    rebuildTracks();
    selectTrack(-1);
}

ComposeView::~ComposeView()
{
    juce::PopupMenu::dismissAllActiveMenus();
}

TrackRow& ComposeView::getTrackRow(int index)
{
    return *rows[static_cast<std::size_t>(std::clamp(index + 1, 0, static_cast<int>(rows.size()) - 1))];
}

void ComposeView::rebuildTracks()
{
    for (auto& row : rows)
        removeChildComponent(row.get());
    rows.clear();
    const int count = static_cast<int>(ctx.processor.getSong().tracks.size());
    for (int i = -1; i < count; ++i)
    {
        auto row = std::make_unique<TrackRow>(ctx, i);
        row->onSelect = [this](int index) { selectTrack(index); };
        row->onRename = [this](int index) { renameTrack(index); };
        row->onMenu = [this](int index) { showTrackMenu(index); };
        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }
    shownTracks = count;
    addTrackButton.setEnabled(count < dsp::kMaxMelodyTracks);
    resized();
}

void ComposeView::selectTrack(int index)
{
    const int count = static_cast<int>(ctx.processor.getSong().tracks.size());
    index = std::clamp(index, -1, count - 1);
    if (index != ctx.selectedTrack)
    {
        ctx.selectedNotes.clear();
        if (index >= 0)
            roll.centreOn(dsp::roleCentre(ctx.processor.getSong().tracks[static_cast<std::size_t>(index)].role));
    }
    if (index >= 0)
        ctx.selectedChords.clear();
    ctx.selectedTrack = index;
    ctx.changed();
}

void ComposeView::addTrack(dsp::TrackRole role)
{
    const int count = static_cast<int>(ctx.processor.getSong().tracks.size());
    if (count >= dsp::kMaxMelodyTracks)
        return;
    ctx.edit(
        [role](dsp::Song& song)
        {
            dsp::MelodyTrack track;
            track.role = role;
            int same = 0;
            for (const auto& t : song.tracks)
                same += t.role == role ? 1 : 0;
            track.name = std::string(dsp::trackRoleName(role)) + (same > 0 ? " " + std::to_string(same + 1) : "");
            song.tracks.push_back(track);
        });
    rebuildTracks();
    selectTrack(count);
}

void ComposeView::layoutLower()
{
    const bool showRoll = ctx.selectedTrack >= 0 || ctx.voicing;
    palette.setVisible(!showRoll);
    roll.setVisible(showRoll);
    palette.setBounds(lowerArea);
    roll.setBounds(lowerArea);
    roll.resized();
    exportButton.setBounds(lowerArea.getRight() - kExportW, lowerArea.getBottom() - PaletteView::kBarH + 4, kExportW,
                           PaletteView::kBarH - 8);
    exportButton.toFront(false);
}

void ComposeView::syncAll()
{
    if (static_cast<int>(ctx.processor.getSong().tracks.size()) != shownTracks)
        rebuildTracks();
    if (ctx.selectedTrack >= static_cast<int>(ctx.processor.getSong().tracks.size()))
        ctx.selectedTrack = -1;
    // Indices die when the song changes underneath them (undo, a deleted track).
    const auto chordCount = dsp::countChords(ctx.processor.getSong());
    ctx.selectedChords.erase(std::remove_if(ctx.selectedChords.begin(), ctx.selectedChords.end(),
                                            [chordCount](int i) { return i < 0 || i >= chordCount; }),
                             ctx.selectedChords.end());
    if (ctx.selectedTrack >= 0)
    {
        const auto n =
            static_cast<int>(ctx.processor.getSong().tracks[static_cast<std::size_t>(ctx.selectedTrack)].notes.size());
        ctx.selectedNotes.erase(
            std::remove_if(ctx.selectedNotes.begin(), ctx.selectedNotes.end(), [n](int i) { return i < 0 || i >= n; }),
            ctx.selectedNotes.end());
    }
    for (auto& row : rows)
        row->refresh();
    const auto key = ctx.key();
    keyButton.setButtonText(dsp::pitchClassName(key.homeTonic, dsp::prefersFlats(key.homeTonic, key.homeScale)));
    scaleButton.setButtonText(dsp::scaleAt(key.homeScale).label);
    linkButton.setToggleState(ctx.processor.isLinked(), juce::dontSendNotification);
    playButton.setToggleState(ctx.processor.isPlaying(), juce::dontSendNotification);
    snapButton.setButtonText(snapName(ctx.snap));
    namesButton.setToggleState(!ctx.numerals, juce::dontSendNotification);
    numeralsButton.setToggleState(ctx.numerals, juce::dontSendNotification);
    undoButton.setEnabled(ctx.processor.canUndo());
    redoButton.setEnabled(ctx.processor.canRedo());
    if (lastSelectedTrack != ctx.selectedTrack || palette.isVisible() == (ctx.selectedTrack >= 0 || ctx.voicing))
        layoutLower();
    lastSelectedTrack = ctx.selectedTrack;
    palette.refresh();
    timeline.repaint();
    roll.repaint();
    exportButton.repaint();
    repaint();
}

void ComposeView::refresh()
{
    ctx.processor.collectSongs();
    const bool playing = ctx.processor.isPlaying();
    if (playing != wasPlaying)
    {
        wasPlaying = playing;
        playButton.setToggleState(playing, juce::dontSendNotification);
        playButton.repaint();
    }
    // Follow the playhead off the right edge.
    if (ctx.processor.isSequencing())
    {
        const double beat = ctx.processor.getPlayheadBeats();
        const double visible = (timeline.getWidth() - TimeAxis::kGutter) / ctx.axis.pixelsPerBeat;
        if (beat > ctx.axis.scrollBeats + visible * 0.92 || beat < ctx.axis.scrollBeats)
            ctx.axis.scrollBeats = std::max(0.0, beat - visible * 0.1);
    }
    palette.refresh();
    timeline.repaint();
    if (roll.isVisible())
        roll.repaint();
    repaint(0, 0, getWidth(), kTopH + 8);
}

bool ComposeView::handleKey(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() &&
        juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode())) == 'z')
    {
        ctx.clearSelection();
        if (mods.isShiftDown())
            ctx.processor.redo();
        else
            ctx.processor.undo();
        ctx.changed();
        return true;
    }
    if (ctx.selectedTrack >= 0 && roll.handleKey(key))
        return true;
    if (ctx.selectedTrack < 0 && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        timeline.deleteSelectedChords();
        return true;
    }
    if (ctx.selectedTrack < 0 && !ctx.voicing && !mods.isAnyModifierKeyDown() &&
        (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey))
    {
        palette.borrow(key == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }
    if (mods.isCommandDown() &&
        (key.getTextCharacter() == '=' || key.getTextCharacter() == '+' || key.getTextCharacter() == '-'))
    {
        ctx.axis.zoom(key.getTextCharacter() == '-' ? 1.0 / 1.25 : 1.25, ctx.axis.scrollBeats);
        ctx.changed();
        return true;
    }
    return false;
}

void ComposeView::showKeyMenu()
{
    const int current = ctx.key().homeTonic;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader("SONG KEY");
    for (int pc = 0; pc < 12; ++pc)
        menu.addItem(pc + 1,
                     juce::String(dsp::pitchClassName(pc)) + (dsp::pitchClassName(pc)[1] == '#'
                                                                  ? juce::String(" / ") + dsp::pitchClassName(pc, true)
                                                                  : juce::String()),
                     true, pc == current);
    juce::Component::SafePointer<ComposeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&keyButton),
                       [safe](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           safe->ctx.processor.setParameterFromUi(ids::key, static_cast<float>(result - 1));
                           safe->ctx.changed();
                       });
}

void ComposeView::showScaleMenu()
{
    const int current = ctx.key().homeScale;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader("SONG SCALE");
    for (int s = 0; s < dsp::kNumScales; ++s)
        menu.addItem(s + 1, dsp::kScales[s].name, true, s == current);
    juce::Component::SafePointer<ComposeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&scaleButton),
                       [safe](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           safe->ctx.processor.setParameterFromUi(ids::scale, static_cast<float>(result - 1));
                           safe->ctx.processor.setPaletteBorrow(0, result - 1);
                           safe->ctx.changed();
                       });
}

void ComposeView::showSnapMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader("GRID");
    const double grids[] = {4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0};
    for (int i = 0; i < 7; ++i)
        menu.addItem(i + 1, i == 1 ? juce::String("1/2") : snapName(grids[i]), true,
                     std::abs(grids[i] - ctx.snap) < 1.0e-9);
    juce::Component::SafePointer<ComposeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&snapButton),
                       [safe, grids](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           safe->ctx.snap = grids[result - 1];
                           safe->ctx.changed();
                       });
}

void ComposeView::showAddTrackMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader("NEW TRACK");
    for (int r = 0; r < 4; ++r)
        menu.addItem(r + 1, dsp::trackRoleName(static_cast<dsp::TrackRole>(r)));
    juce::Component::SafePointer<ComposeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addTrackButton),
                       [safe](int result)
                       {
                           if (safe != nullptr && result > 0)
                               safe->addTrack(static_cast<dsp::TrackRole>(result - 1));
                       });
}

void ComposeView::showTrackMenu(int index)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addItem(1, "Rename");
    menu.addItem(2, "Clear notes");
    menu.addItem(3, "Delete track");
    juce::PopupMenu roles;
    for (int r = 0; r < 4; ++r)
        roles.addItem(10 + r, dsp::trackRoleName(static_cast<dsp::TrackRole>(r)));
    menu.addSubMenu("Role", roles);
    juce::Component::SafePointer<ComposeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                       [safe, index](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           if (result == 1)
                           {
                               safe->renameTrack(index);
                               return;
                           }
                           safe->ctx.selectedNotes.clear();
                           if (result == 3)
                               safe->ctx.selectedTrack = -1;
                           safe->ctx.edit(
                               [index, result](dsp::Song& song)
                               {
                                   if (index >= static_cast<int>(song.tracks.size()))
                                       return;
                                   auto& track = song.tracks[static_cast<std::size_t>(index)];
                                   if (result == 2)
                                       track.notes.clear();
                                   else if (result == 3)
                                       song.tracks.erase(song.tracks.begin() + index);
                                   else if (result >= 10)
                                       track.role = static_cast<dsp::TrackRole>(result - 10);
                               });
                       });
}

void ComposeView::renameTrack(int index)
{
    auto& row = getTrackRow(index);
    nameEditor = std::make_unique<juce::TextEditor>();
    const auto& p = ctx.theme.palette;
    nameEditor->setColour(juce::TextEditor::backgroundColourId, p.panelRaised);
    nameEditor->setColour(juce::TextEditor::textColourId, p.text);
    nameEditor->setColour(juce::TextEditor::outlineColourId, p.accent);
    nameEditor->setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    nameEditor->setFont(makeFont(13.0f, true, 0.02f));
    nameEditor->setText(juce::String(ctx.processor.getSong().tracks[static_cast<std::size_t>(index)].name));
    nameEditor->selectAll();
    nameEditor->setBounds(row.getBounds().withTrimmedLeft(66).withHeight(26).translated(0, 6).withTrimmedRight(8));
    addAndMakeVisible(*nameEditor);
    nameEditor->grabKeyboardFocus();
    const auto commit = [this, index]
    {
        if (nameEditor == nullptr)
            return;
        const auto text = nameEditor->getText().trim().substring(0, 24);
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<ComposeView>(this), index, text]
            {
                if (safe == nullptr)
                    return;
                safe->nameEditor.reset();
                if (text.isNotEmpty())
                    safe->ctx.edit(
                        [index, text](dsp::Song& song)
                        {
                            if (index < static_cast<int>(song.tracks.size()))
                                song.tracks[static_cast<std::size_t>(index)].name = text.toStdString();
                        });
                if (auto* top = safe->getTopLevelComponent())
                    top->grabKeyboardFocus();
            });
    };
    nameEditor->onReturnKey = commit;
    nameEditor->onFocusLost = commit;
}

void ComposeView::paint(juce::Graphics& g)
{
    const auto& p = ctx.theme.palette;
    // Panels: the track list, the main area.
    const auto panel = trackPanel.toFloat();
    g.setColour(p.panel.withAlpha(0.55f));
    g.fillRoundedRectangle(panel, 8.0f);
    const auto main =
        juce::Rectangle<float>(panel.getRight() + 12.0f, panel.getY(),
                               static_cast<float>(getWidth() - kMargin) - panel.getRight() - 12.0f, panel.getHeight());
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(main.translated(0.0f, 2.0f), 8.0f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(main, 8.0f);
    g.setColour(p.outline);
    g.drawRoundedRectangle(main.reduced(0.5f), 8.0f, 1.0f);

    // Top bar labels.
    g.setColour(p.textDim);
    g.setFont(makeFont(9.5f, true, 0.22f));
    g.drawText("SONG KEY", juce::Rectangle<int>(kMargin, 6, 70, kTopH - 6), juce::Justification::centredLeft, false);
    const bool hostTempo = ctx.processor.isLinked();
    g.setColour(p.text);
    g.setFont(makeFont(15.0f, true, 0.02f));
    const auto bpmArea = juce::Rectangle<int>(linkButton.getRight() + 16, 6, 110, kTopH - 6);
    g.drawText(juce::String(juce::roundToInt(ctx.processor.getTempo())), bpmArea.withWidth(36),
               juce::Justification::centredRight, false);
    g.setColour(p.textDim);
    g.setFont(makeFont(8.5f, true, 0.16f));
    g.drawText(hostTempo ? "BPM  /  LINKED" : "BPM  /  HOST", bpmArea.withTrimmedLeft(42),
               juce::Justification::centredLeft, false);
    const double beat = ctx.processor.getPlayheadBeats();
    g.drawText("BAR " + juce::String(static_cast<int>(beat / 4.0) + 1) + "." +
                   juce::String(static_cast<int>(std::fmod(beat, 4.0)) + 1),
               juce::Rectangle<int>(bpmArea.getRight() + 4, 6, 70, kTopH - 6), juce::Justification::centredLeft, false);
    g.drawText("GRID", juce::Rectangle<int>(snapButton.getX() - 40, 6, 36, kTopH - 6),
               juce::Justification::centredRight, false);
}

void ComposeView::resized()
{
    auto top = juce::Rectangle<int>(kMargin, 10, getWidth() - kMargin * 2, kTopH - 12);
    top.removeFromLeft(70);
    keyButton.setBounds(top.removeFromLeft(46));
    top.removeFromLeft(6);
    scaleButton.setBounds(top.removeFromLeft(124));
    top.removeFromLeft(26);
    rewindButton.setBounds(top.removeFromLeft(36));
    top.removeFromLeft(4);
    playButton.setBounds(top.removeFromLeft(40));
    top.removeFromLeft(4);
    loopButton.setBounds(top.removeFromLeft(36));
    top.removeFromLeft(4);
    linkButton.setBounds(top.removeFromLeft(36));

    redoButton.setBounds(top.removeFromRight(34));
    top.removeFromRight(4);
    undoButton.setBounds(top.removeFromRight(34));
    top.removeFromRight(14);
    numeralsButton.setBounds(top.removeFromRight(40));
    top.removeFromRight(4);
    namesButton.setBounds(top.removeFromRight(44));
    top.removeFromRight(14);
    snapButton.setBounds(top.removeFromRight(62));

    trackPanel = juce::Rectangle<int>(kMargin, kTopH + 8, kPanelW, getHeight() - kTopH - 8 - kMargin);
    auto list = trackPanel.reduced(8);
    addTrackButton.setBounds(list.removeFromBottom(34));
    list.removeFromBottom(8);
    for (auto& row : rows)
        row->setBounds(list.removeFromTop(row->getIndex() < 0 ? kChordRowH : kTrackRowH));

    const auto main = juce::Rectangle<int>(trackPanel.getRight() + 12, trackPanel.getY(),
                                           getWidth() - kMargin - trackPanel.getRight() - 12, trackPanel.getHeight())
                          .reduced(10, 8);
    timeline.setBounds(main.withHeight(TimelineView::kHeight));
    lowerArea = main.withTrimmedTop(TimelineView::kHeight + 6);
    layoutLower();
}
} // namespace dolphinflow::ui
