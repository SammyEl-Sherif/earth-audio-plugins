#include "PianoRoll.h"

#include <algorithm>
#include <cmath>

namespace dolphinflow::ui
{
namespace
{
using animalaudio::ui::makeFont;

constexpr float kEdgeGrab = 6.0f;

bool isBlack(int pitch)
{
    const int pc = dsp::pitchClassOf(pitch);
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

juce::String noteName(int pitch)
{
    return juce::String(dsp::pitchClassName(pitch)) + juce::String(pitch / 12 - 1);
}
} // namespace

PianoRoll::PianoRoll(ComposeContext& context)
    : ctx(context)
    , fitButton("FIT", context.theme.palette)
    , velocityButton("VELOCITY", context.theme.palette)
    , findButton("FIND CHORDS FOR NOTES", context.theme.palette)
{
    setMouseClickGrabsKeyboardFocus(false);
    fitButton.setTooltip("Zoom to show the whole song and every note.");
    velocityButton.setTooltip("Drag notes up or down to change how hard they play.");
    findButton.setTooltip("Light up the palette chords that contain every selected note (or every note under the "
                          "chord at the playhead).");
    fitButton.onClick = [this] { fit(); };
    velocityButton.onClick = [this]
    {
        ctx.velocityMode = !ctx.velocityMode;
        ctx.changed();
    };
    findButton.onClick = [this] { findChordsForSelection(); };
    for (auto* b : {&fitButton, &velocityButton, &findButton})
    {
        b->setClickingTogglesState(false);
        b->setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    const auto& p = ctx.theme.palette;
    menuLook.setColour(juce::PopupMenu::backgroundColourId, p.panel);
    menuLook.setColour(juce::PopupMenu::textColourId, p.text);
    menuLook.setColour(juce::PopupMenu::headerTextColourId, p.textDim);
    menuLook.setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    menuLook.setColour(juce::PopupMenu::highlightedTextColourId, p.onAccent);
}

PianoRoll::~PianoRoll()
{
    juce::PopupMenu::dismissAllActiveMenus();
}

juce::Rectangle<float> PianoRoll::rollArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom(static_cast<float>(kBarH));
}

float PianoRoll::yOfPitch(int pitch) const noexcept
{
    return static_cast<float>((topPitch - pitch) * rowH);
}

int PianoRoll::pitchAt(float y) const noexcept
{
    return std::clamp(static_cast<int>(std::ceil(topPitch - y / rowH)), 0, 127);
}

juce::Rectangle<float> PianoRoll::noteBounds(const dsp::MelodyNote& note) const
{
    const float x0 = ctx.axis.xOf(note.start);
    const float x1 = ctx.axis.xOf(note.end());
    return {x0, yOfPitch(note.pitch), std::max(3.0f, x1 - x0), static_cast<float>(rowH)};
}

const dsp::MelodyTrack* PianoRoll::track() const
{
    const auto& song = ctx.processor.getSong();
    if (ctx.selectedTrack < 0 || ctx.selectedTrack >= static_cast<int>(song.tracks.size()))
        return nullptr;
    return &song.tracks[static_cast<std::size_t>(ctx.selectedTrack)];
}

int PianoRoll::noteAt(juce::Point<float> p) const
{
    const auto* t = track();
    if (t == nullptr || p.x < TimeAxis::kGutter)
        return -1;
    // Last drawn is on top.
    for (int i = static_cast<int>(t->notes.size()) - 1; i >= 0; --i)
        if (noteBounds(t->notes[static_cast<std::size_t>(i)]).contains(p))
            return i;
    return -1;
}

std::vector<PianoRoll::Marked> PianoRoll::marked() const
{
    std::vector<Marked> out;
    if (const auto* t = track())
        for (int i = 0; i < static_cast<int>(t->notes.size()); ++i)
            out.push_back(
                {t->notes[static_cast<std::size_t>(i)],
                 std::find(ctx.selectedNotes.begin(), ctx.selectedNotes.end(), i) != ctx.selectedNotes.end()});
    return out;
}

void PianoRoll::commit(std::vector<Marked> notes, bool undoable)
{
    if (track() == nullptr)
        return;
    for (auto& m : notes)
    {
        m.note.start = std::max(0.0, m.note.start);
        m.note.pitch = std::clamp(m.note.pitch, 0, 127);
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const Marked& a, const Marked& b) {
                         return a.note.start < b.note.start ||
                                (a.note.start == b.note.start && a.note.pitch < b.note.pitch);
                     });
    if (notes.size() > static_cast<std::size_t>(dsp::kMaxTrackNotes))
        notes.resize(static_cast<std::size_t>(dsp::kMaxTrackNotes));
    auto song = ctx.processor.getSong();
    auto& target = song.tracks[static_cast<std::size_t>(ctx.selectedTrack)].notes;
    target.clear();
    ctx.selectedNotes.clear();
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        target.push_back(notes[static_cast<std::size_t>(i)].note);
        if (notes[static_cast<std::size_t>(i)].selected)
            ctx.selectedNotes.push_back(i);
    }
    ctx.processor.setSong(song, undoable);
    ctx.changed();
}

void PianoRoll::preview(int pitch, int velocity)
{
    if (ctx.selectedTrack >= 0)
        ctx.processor.previewNote(ctx.selectedTrack, pitch, velocity);
}

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& palette = ctx.theme.palette;
    const auto area = rollArea();
    const auto& song = ctx.processor.getSong();
    const auto chords = dsp::placeChords(song);
    const auto key = ctx.key();
    const unsigned home = dsp::scaleMask(key.homeTonic, key.homeScale);
    const int lowest = std::max(0, pitchAt(area.getBottom()));
    const int highest = std::min(127, static_cast<int>(std::ceil(topPitch)) + 1);
    const float left = static_cast<float>(TimeAxis::kGutter);
    const float right = area.getRight();

    g.setColour(palette.background);
    g.fillRect(area);
    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(area.withTrimmedLeft(left).toNearestInt());
        // Rows: key shading, then the harmonic guide under each chord.
        for (int pitch = lowest; pitch <= highest; ++pitch)
        {
            const float y = yOfPitch(pitch);
            const bool inKey = ((home >> dsp::pitchClassOf(pitch)) & 1u) != 0;
            g.setColour(inKey ? palette.panel : palette.background.darker(0.25f));
            g.fillRect(left, y, right - left, static_cast<float>(rowH));
        }
        for (const auto& c : chords)
        {
            const float x0 = std::max(left, ctx.axis.xOf(c.start));
            const float x1 = std::min(right, ctx.axis.xOf(c.end()));
            if (x1 <= x0)
                continue;
            const auto colour = ctx.chordColour(c.shape);
            for (int pitch = lowest; pitch <= highest; ++pitch)
            {
                float alpha = 0.0f;
                switch (dsp::guideFor(pitch, c.root, c.shape, home))
                {
                case dsp::Guide::Root:
                    alpha = 0.5f;
                    break;
                case dsp::Guide::ChordTone:
                    alpha = 0.3f;
                    break;
                case dsp::Guide::Extension:
                    alpha = 0.14f;
                    break;
                case dsp::Guide::InKey:
                case dsp::Guide::OutOfKey:
                    break;
                }
                if (alpha > 0.0f)
                {
                    g.setColour(colour.withAlpha(alpha));
                    g.fillRect(x0, yOfPitch(pitch) + 0.5f, x1 - x0, static_cast<float>(rowH) - 1.0f);
                }
            }
            g.setColour(palette.outline);
            g.fillRect(ctx.axis.xOf(c.start), area.getY(), 1.0f, area.getHeight());
        }
        // Grid: snap lines faint, beats, bars.
        const double first = std::floor(ctx.axis.scrollBeats);
        const double last = ctx.axis.beatAt(right);
        const double step =
            ctx.snap > 0.0 && ctx.snap < 1.0 && ctx.snap * ctx.axis.pixelsPerBeat > 7.0 ? ctx.snap : 1.0;
        for (double b = first; b <= last; b += step)
        {
            const bool bar = std::fmod(b, 4.0) < 1.0e-9;
            const bool beat = std::fmod(b, 1.0) < 1.0e-9;
            g.setColour(palette.outline.withAlpha(bar ? 0.9f : beat ? 0.45f : 0.2f));
            g.fillRect(ctx.axis.xOf(b), area.getY(), 1.0f, area.getHeight());
        }
        for (int pitch = lowest; pitch <= highest; ++pitch)
            if (dsp::pitchClassOf(pitch) == 0)
            {
                g.setColour(palette.outline.withAlpha(0.7f));
                g.fillRect(left, yOfPitch(pitch) + static_cast<float>(rowH) - 1.0f, right - left, 1.0f);
            }

        // Ghost notes: the other tracks, or (voicing view) the chord voicing.
        if (isReadOnly())
        {
            const auto v = ctx.processor.clipVoicing();
            for (const auto& n : dsp::voiceChordTrack(song, v.voices, v.centre, 0.0, 1.0e9, false))
            {
                const auto chordIndex = dsp::chordIndexAt(chords, n.start);
                const auto colour = chordIndex >= 0
                                        ? ctx.chordColour(chords[static_cast<std::size_t>(chordIndex)].shape)
                                        : palette.accent;
                const auto r = noteBounds({n.note, n.start, n.end - n.start, 100}).reduced(0.5f, 1.0f);
                g.setColour(colour.brighter(0.2f));
                g.fillRoundedRectangle(r, 3.0f);
                g.setColour(palette.text.withAlpha(0.7f));
                g.drawRoundedRectangle(r, 3.0f, 1.0f);
            }
        }
        else
        {
            for (int t = 0; t < static_cast<int>(song.tracks.size()); ++t)
            {
                if (t == ctx.selectedTrack)
                    continue;
                g.setColour(ctx.trackColour(t).withAlpha(0.35f));
                for (const auto& n : song.tracks[static_cast<std::size_t>(t)].notes)
                    g.drawRoundedRectangle(noteBounds(n).reduced(0.5f, 1.5f), 3.0f, 1.0f);
            }
            if (const auto* t = track())
            {
                const auto colour = ctx.trackColour(ctx.selectedTrack);
                for (int i = 0; i < static_cast<int>(t->notes.size()); ++i)
                {
                    const auto& n = t->notes[static_cast<std::size_t>(i)];
                    const bool selected =
                        std::find(ctx.selectedNotes.begin(), ctx.selectedNotes.end(), i) != ctx.selectedNotes.end();
                    const auto r = noteBounds(n).reduced(0.5f, 1.0f);
                    const float strength = 0.45f + 0.55f * static_cast<float>(n.velocity) / 127.0f;
                    g.setColour((selected ? colour.brighter(0.35f) : colour).withAlpha(strength));
                    g.fillRoundedRectangle(r, 3.0f);
                    g.setColour(selected ? palette.text : colour.brighter(0.5f).withAlpha(0.8f));
                    g.drawRoundedRectangle(r, 3.0f, selected ? 1.6f : 1.0f);
                    if (ctx.velocityMode)
                    {
                        g.setColour(palette.text.withAlpha(0.8f));
                        g.fillRect(r.getX() + 2.0f, r.getBottom() - 2.5f,
                                   (r.getWidth() - 4.0f) * static_cast<float>(n.velocity) / 127.0f, 1.5f);
                    }
                    if (r.getWidth() > 26.0f && rowH >= 11.0)
                    {
                        g.setColour(palette.onAccent.withAlpha(0.85f));
                        g.setFont(makeFont(8.5f, true, 0.0f));
                        g.drawText(noteName(n.pitch), r.reduced(4.0f, 0.0f), juce::Justification::centredLeft, false);
                    }
                }
            }
        }

        // Playhead and rubber band.
        const float x = ctx.axis.xOf(ctx.processor.getPlayheadBeats());
        g.setColour(ctx.processor.isSequencing() ? palette.text : palette.textDim.withAlpha(0.6f));
        g.fillRect(x - 0.5f, area.getY(), 1.5f, area.getHeight());
        if (drag == Drag::Select)
        {
            g.setColour(palette.accent.withAlpha(0.15f));
            g.fillRect(rubber);
            g.setColour(palette.accent);
            g.drawRect(rubber, 1.0f);
        }
    }

    // Keys down the left: white and black, C named.
    {
        juce::Graphics::ScopedSaveState keysClip(g);
        g.reduceClipRegion(area.withWidth(left).toNearestInt());
        for (int pitch = lowest; pitch <= highest; ++pitch)
        {
            const float y = yOfPitch(pitch);
            const auto r = juce::Rectangle<float>(0.0f, y, left - 2.0f, static_cast<float>(rowH));
            g.setColour(isBlack(pitch) ? palette.background.darker(0.3f) : palette.panelRaised);
            g.fillRect(r.reduced(0.0f, 0.5f));
            if (rowH >= 10.0 && (dsp::pitchClassOf(pitch) == 0 || rowH >= 15.0))
            {
                g.setColour(dsp::pitchClassOf(pitch) == 0 ? palette.text : palette.textDim);
                g.setFont(makeFont(std::min(10.0f, static_cast<float>(rowH) - 2.0f), true, 0.04f));
                g.drawText(noteName(pitch), r.reduced(6.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    // The bar under the roll.
    g.setColour(palette.outline.withAlpha(0.7f));
    g.fillRect(0.0f, area.getBottom(), static_cast<float>(getWidth()), 1.0f);
    if (isReadOnly())
    {
        g.setColour(palette.textDim);
        g.setFont(makeFont(9.5f, true, 0.14f));
        g.drawText("CHORD VOICINGS AS THEY PLAY  /  VOICES AND OCTAVE ON THE KEYS PAGE",
                   getLocalBounds().removeFromBottom(kBarH).withTrimmedLeft(110), juce::Justification::centredLeft,
                   false);
    }
}

void PianoRoll::resized()
{
    auto bar = getLocalBounds().removeFromBottom(kBarH).reduced(0, 6);
    bar.removeFromTop(2);
    fitButton.setBounds(bar.removeFromLeft(54));
    bar.removeFromLeft(8);
    velocityButton.setBounds(bar.removeFromLeft(92));
    bar.removeFromLeft(8);
    findButton.setBounds(bar.removeFromLeft(196));
    const bool editable = !isReadOnly();
    velocityButton.setVisible(editable);
    findButton.setVisible(editable);
}

void PianoRoll::centreOn(int pitch)
{
    topPitch = pitch + rollArea().getHeight() / rowH * 0.5;
    repaint();
}

void PianoRoll::fit()
{
    const auto& song = ctx.processor.getSong();
    const double length = std::max(4.0, dsp::songBeats(song));
    const float width = rollArea().getWidth() - TimeAxis::kGutter - 8.0f;
    ctx.axis.pixelsPerBeat = std::clamp(width / length, 4.0, 160.0);
    ctx.axis.scrollBeats = 0.0;
    int low = 127, high = 0;
    if (const auto* t = track())
        for (const auto& n : t->notes)
        {
            low = std::min(low, n.pitch);
            high = std::max(high, n.pitch);
        }
    if (low <= high)
    {
        const double rows = std::max(12, high - low + 5);
        rowH = std::clamp(rollArea().getHeight() / rows, 7.0, 24.0);
        topPitch = high + 2.5;
    }
    ctx.changed();
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    mouseBeat = ctx.axis.beatAt(e.position.x);
    const int i = noteAt(e.position);
    auto cursor = juce::MouseCursor::NormalCursor;
    if (i >= 0 && !ctx.velocityMode)
    {
        const auto r = noteBounds(track()->notes[static_cast<std::size_t>(i)]);
        if (r.getRight() - e.position.x < kEdgeGrab || (e.position.x - r.getX() < kEdgeGrab && r.getWidth() > 14.0f))
            cursor = juce::MouseCursor::LeftRightResizeCursor;
        else
            cursor = juce::MouseCursor::DraggingHandCursor;
    }
    else if (i >= 0)
    {
        cursor = juce::MouseCursor::UpDownResizeCursor;
    }
    setMouseCursor(cursor);
}

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    drag = Drag::None;
    copied = false;
    downAt = e.position;
    mouseBeat = ctx.axis.beatAt(e.position.x);
    before = ctx.processor.getSong();
    if (e.position.y >= rollArea().getBottom())
        return;
    if (e.position.x < TimeAxis::kGutter)
    {
        preview(pitchAt(e.position.y)); // the keys play
        return;
    }
    if (isReadOnly())
        return;

    const int i = noteAt(e.position);
    if (e.mods.isPopupMenu())
    {
        if (i >= 0 && std::find(ctx.selectedNotes.begin(), ctx.selectedNotes.end(), i) == ctx.selectedNotes.end())
            ctx.selectedNotes = {i};
        ctx.changed();
        showNoteMenu(mouseBeat);
        return;
    }
    if (i < 0)
    {
        drag = Drag::Pending;
        return;
    }
    auto& sel = ctx.selectedNotes;
    const auto found = std::find(sel.begin(), sel.end(), i);
    if (e.mods.isShiftDown())
    {
        if (found != sel.end())
        {
            sel.erase(found);
            ctx.changed();
            return;
        }
        sel.push_back(i);
    }
    else if (found == sel.end())
    {
        sel = {i};
    }
    dragStart = marked();
    anchor = i;
    const auto& n = track()->notes[static_cast<std::size_t>(i)];
    const auto r = noteBounds(n);
    if (ctx.velocityMode)
        drag = Drag::Velocity;
    else if (r.getRight() - e.position.x < kEdgeGrab)
        drag = Drag::ResizeEnd;
    else if (e.position.x - r.getX() < kEdgeGrab && r.getWidth() > 14.0f)
        drag = Drag::ResizeStart;
    else
        drag = Drag::Move;
    lastLength = n.length;
    preview(n.pitch, n.velocity);
    ctx.changed();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    mouseBeat = ctx.axis.beatAt(e.position.x);
    const double grid = ctx.snap > 0.0 ? ctx.snap : 1.0 / 64.0;
    if (drag == Drag::Pending && e.getDistanceFromDragStart() > 4)
        drag = Drag::Select;
    if (drag == Drag::Select)
    {
        rubber = juce::Rectangle<float>(downAt, e.position);
        auto selection = e.mods.isShiftDown() ? ctx.selectedNotes : std::vector<int>{};
        if (const auto* t = track())
            for (int i = 0; i < static_cast<int>(t->notes.size()); ++i)
                if (rubber.intersects(noteBounds(t->notes[static_cast<std::size_t>(i)])) &&
                    std::find(selection.begin(), selection.end(), i) == selection.end())
                    selection.push_back(i);
        ctx.selectedNotes = selection;
        ctx.changed();
        return;
    }
    if (anchor < 0 || dragStart.empty())
        return;
    const auto& anchorNote = dragStart[static_cast<std::size_t>(anchor)].note;
    auto notes = dragStart;
    if (drag == Drag::Move)
    {
        const double rawStart = anchorNote.start + e.getDistanceFromDragStartX() / ctx.axis.pixelsPerBeat;
        const double deltaBeats = dsp::snapBeats(rawStart, grid) - anchorNote.start;
        const int deltaPitch = juce::roundToInt(-e.getDistanceFromDragStartY() / rowH);
        if (e.mods.isAltDown())
        {
            // Alt-drag copies: the originals stay put, unselected; the moved copies are the selection.
            copied = true;
            for (auto& m : dragStart)
                if (m.selected)
                    notes.push_back({m.note, true});
            for (std::size_t i = 0; i < dragStart.size(); ++i)
                notes[i].selected = false;
        }
        for (auto& m : notes)
            if (m.selected)
            {
                m.note.start += deltaBeats;
                m.note.pitch = std::clamp(m.note.pitch + deltaPitch, 0, 127);
            }
    }
    else if (drag == Drag::ResizeEnd || drag == Drag::ResizeStart)
    {
        const double delta = e.getDistanceFromDragStartX() / ctx.axis.pixelsPerBeat;
        for (auto& m : notes)
            if (m.selected)
            {
                if (drag == Drag::ResizeEnd)
                {
                    const double end = dsp::snapBeats(m.note.end() + delta, grid);
                    m.note.length = std::max(grid, end - m.note.start);
                }
                else
                {
                    const double end = m.note.end();
                    m.note.start = std::min(end - grid, std::max(0.0, dsp::snapBeats(m.note.start + delta, grid)));
                    m.note.length = end - m.note.start;
                }
                lastLength = m.note.length;
            }
    }
    else if (drag == Drag::Velocity)
    {
        const int delta = juce::roundToInt(-static_cast<float>(e.getDistanceFromDragStartY()) * 0.8f);
        for (auto& m : notes)
            if (m.selected)
                m.note.velocity = std::clamp(m.note.velocity + delta, 1, 127);
    }
    commit(notes, false);
}

void PianoRoll::mouseUp(const juce::MouseEvent& e)
{
    const auto finished = drag;
    drag = Drag::None;
    rubber = {};
    if (finished == Drag::Pending && !isReadOnly())
    {
        // A click on empty space: a new note, the grid's length (or the last one drawn), selected and heard.
        const auto* t = track();
        if (t == nullptr || static_cast<int>(t->notes.size()) >= dsp::kMaxTrackNotes)
            return;
        const double grid = ctx.snap > 0.0 ? ctx.snap : 0.25;
        const double start = std::max(0.0, std::floor(ctx.axis.beatAt(e.position.x) / grid) * grid);
        auto notes = marked();
        for (auto& m : notes)
            m.selected = false;
        const int pitch = pitchAt(e.position.y);
        notes.push_back({{pitch, start, std::max(grid, lastLength), 100}, true});
        commit(notes, true);
        preview(pitch);
        return;
    }
    if (finished == Drag::Move || finished == Drag::ResizeEnd || finished == Drag::ResizeStart ||
        finished == Drag::Velocity)
    {
        if (e.getDistanceFromDragStart() > 0)
            ctx.processor.pushUndo(before);
        if (finished == Drag::Move || finished == Drag::Velocity)
            if (const auto* t = track(); t != nullptr && !ctx.selectedNotes.empty())
            {
                const auto& n = t->notes[static_cast<std::size_t>(ctx.selectedNotes.front())];
                preview(n.pitch, n.velocity);
            }
    }
    ctx.changed();
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (isReadOnly())
        return;
    const int i = noteAt(e.position);
    if (i < 0)
        return;
    auto notes = marked();
    notes.erase(notes.begin() + i);
    for (auto& m : notes)
        m.selected = false;
    commit(notes, true);
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const bool horizontal = e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (e.mods.isCommandDown() || (e.mods.isAltDown() && e.mods.isShiftDown()))
    {
        ctx.axis.zoom(wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15, ctx.axis.beatAt(e.position.x));
    }
    else if (e.mods.isAltDown())
    {
        const int pitch = pitchAt(e.position.y);
        const float offset = e.position.y;
        rowH = std::clamp(rowH * (wheel.deltaY > 0 ? 1.12 : 1.0 / 1.12), 6.0, 28.0);
        topPitch = pitch + offset / rowH;
    }
    else if (horizontal)
    {
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        ctx.axis.scrollBeats = std::max(0.0, ctx.axis.scrollBeats - delta * 200.0 / ctx.axis.pixelsPerBeat);
    }
    else
    {
        topPitch = std::clamp(topPitch + wheel.deltaY * 30.0, 12.0, 127.0);
    }
    ctx.changed();
}

bool PianoRoll::handleKey(const juce::KeyPress& key)
{
    if (isReadOnly())
        return false;
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    if (mods.isCommandDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(code));
        if (c == 'a')
            selectAll();
        else if (c == 'c')
            copySelection(false);
        else if (c == 'x')
            copySelection(true);
        else if (c == 'v')
            paste(dsp::snapBeats(std::max(0.0, mouseBeat), ctx.snap > 0.0 ? ctx.snap : 0.25));
        else if (c == 'd')
            duplicateSelection();
        else
            return false;
        return true;
    }
    if (ctx.selectedNotes.empty())
        return false;
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        nudgeSelection(0.0, (key == juce::KeyPress::upKey ? 1 : -1) * (mods.isShiftDown() ? 12 : 1));
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        nudgeSelection((key == juce::KeyPress::rightKey ? 1.0 : -1.0) * (ctx.snap > 0.0 ? ctx.snap : 0.25), 0);
        return true;
    }
    return false;
}

void PianoRoll::deleteSelection()
{
    if (ctx.selectedNotes.empty())
        return;
    auto notes = marked();
    notes.erase(std::remove_if(notes.begin(), notes.end(), [](const Marked& m) { return m.selected; }), notes.end());
    commit(notes, true);
}

void PianoRoll::selectAll()
{
    ctx.selectedNotes.clear();
    if (const auto* t = track())
        for (int i = 0; i < static_cast<int>(t->notes.size()); ++i)
            ctx.selectedNotes.push_back(i);
    ctx.changed();
}

void PianoRoll::copySelection(bool cut)
{
    clipboard.clear();
    for (const auto& m : marked())
        if (m.selected)
            clipboard.push_back(m.note);
    if (cut)
        deleteSelection();
}

void PianoRoll::paste(double atBeat)
{
    if (clipboard.empty() || track() == nullptr)
        return;
    double first = clipboard.front().start;
    for (const auto& n : clipboard)
        first = std::min(first, n.start);
    auto notes = marked();
    for (auto& m : notes)
        m.selected = false;
    for (auto n : clipboard)
    {
        n.start = atBeat + (n.start - first);
        notes.push_back({n, true});
    }
    commit(notes, true);
}

void PianoRoll::duplicateSelection()
{
    auto notes = marked();
    double from = 1.0e9, to = 0.0;
    for (const auto& m : notes)
        if (m.selected)
        {
            from = std::min(from, m.note.start);
            to = std::max(to, m.note.end());
        }
    if (to <= from)
        return;
    const double span =
        std::ceil((to - from) / (ctx.snap > 0.0 ? ctx.snap : 0.25) - 1.0e-9) * (ctx.snap > 0.0 ? ctx.snap : 0.25);
    const auto count = notes.size();
    for (std::size_t i = 0; i < count; ++i)
        if (notes[i].selected)
        {
            auto copy = notes[i].note;
            copy.start += span;
            notes[i].selected = false;
            notes.push_back({copy, true});
        }
    commit(notes, true);
}

void PianoRoll::nudgeSelection(double beats, int semitones)
{
    auto notes = marked();
    for (auto& m : notes)
        if (m.selected)
        {
            m.note.start = std::max(0.0, m.note.start + beats);
            m.note.pitch = std::clamp(m.note.pitch + semitones, 0, 127);
        }
    commit(notes, true);
    if (semitones != 0)
        for (const auto& m : notes)
            if (m.selected)
            {
                preview(std::clamp(m.note.pitch, 0, 127), m.note.velocity);
                break;
            }
}

void PianoRoll::findChordsForSelection()
{
    std::vector<int> pitches;
    for (const auto& m : marked())
        if (m.selected)
            pitches.push_back(m.note.pitch);
    if (pitches.empty())
    {
        // Nothing selected: the notes sounding under the chord at the playhead.
        const auto chords = dsp::placeChords(ctx.processor.getSong());
        const int c = dsp::chordIndexAt(chords, ctx.processor.getPlayheadBeats());
        if (c >= 0)
            for (const auto& m : marked())
                if (m.note.start < chords[static_cast<std::size_t>(c)].end() &&
                    m.note.end() > chords[static_cast<std::size_t>(c)].start)
                    pitches.push_back(m.note.pitch);
    }
    ctx.matchMask = dsp::pitchClassMask(pitches);
    ctx.selectedTrack = -1;
    ctx.voicing = false;
    ctx.selectedNotes.clear();
    ctx.changed();
}

bool PianoRoll::setChordFromSelection(double beat)
{
    std::vector<int> pitches;
    for (const auto& m : marked())
        if (m.selected)
            pitches.push_back(m.note.pitch);
    int root = 0, shape = 0;
    if (pitches.empty() || !dsp::identifyChord(pitches.data(), static_cast<int>(pitches.size()), root, shape))
        return false;
    const auto chords = dsp::placeChords(ctx.processor.getSong());
    const int c = dsp::chordIndexAt(chords, beat);
    ctx.edit(
        [&](dsp::Song& song)
        {
            if (c >= 0)
            {
                auto& chord = song.sections[static_cast<std::size_t>(chords[static_cast<std::size_t>(c)].section)]
                                  .chords[static_cast<std::size_t>(chords[static_cast<std::size_t>(c)].index)];
                chord.root = root;
                chord.shape = shape;
            }
            else
            {
                dsp::insertChord(song, dsp::insertPointAt(song, beat), {root, shape, 4.0});
            }
        });
    return true;
}

void PianoRoll::showNoteMenu(double beat)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    std::vector<int> pitches;
    for (const auto& m : marked())
        if (m.selected)
            pitches.push_back(m.note.pitch);
    int root = 0, shape = 0;
    const bool spells =
        !pitches.empty() && dsp::identifyChord(pitches.data(), static_cast<int>(pitches.size()), root, shape);
    const auto chordText = spells ? ctx.chordLabel(root, shape) : juce::String();
    menu.addItem(1, spells ? "Set the chord here to " + chordText : "Selected notes spell no chord", spells);
    menu.addItem(2, "Find chords for these notes", !pitches.empty());
    menu.addSeparator();
    menu.addItem(3, "Duplicate", !pitches.empty());
    menu.addItem(4, "Delete", !pitches.empty());
    menu.addItem(5, "Paste here", !clipboard.empty());
    menu.addItem(6, "Select all");
    juce::PopupMenu velocity;
    for (int v : {40, 64, 90, 110, 127})
        velocity.addItem(100 + v, "Velocity " + juce::String(v), !pitches.empty());
    menu.addSubMenu("Velocity", velocity);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                       [safe, beat](int result)
                       {
                           if (safe == nullptr || result <= 0)
                               return;
                           switch (result)
                           {
                           case 1:
                               safe->setChordFromSelection(beat);
                               break;
                           case 2:
                               safe->findChordsForSelection();
                               break;
                           case 3:
                               safe->duplicateSelection();
                               break;
                           case 4:
                               safe->deleteSelection();
                               break;
                           case 5:
                               safe->paste(dsp::snapBeats(beat, safe->ctx.snap > 0.0 ? safe->ctx.snap : 0.25));
                               break;
                           case 6:
                               safe->selectAll();
                               break;
                           default:
                               if (result > 100)
                               {
                                   auto notes = safe->marked();
                                   for (auto& m : notes)
                                       if (m.selected)
                                           m.note.velocity = result - 100;
                                   safe->commit(notes, true);
                               }
                               break;
                           }
                       });
}
} // namespace dolphinflow::ui
