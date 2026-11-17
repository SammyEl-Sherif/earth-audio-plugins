#include "TimelineView.h"

#include "PaletteView.h"

#include <algorithm>
#include <cmath>

namespace dolphinflow::ui
{
namespace
{
using animalaudio::ui::makeFont;

constexpr float kEdgeGrab = 7.0f;
constexpr int kPreviewSource = DolphinFlowProcessor::kKeySourceBase + 500;
constexpr double kEmptySectionBeats = 4.0;

double snapOr(double beats, double grid, double minimum)
{
    return std::max(minimum, dsp::snapBeats(beats, grid > 0.0 ? grid : 0.25));
}
} // namespace

TimelineView::TimelineView(ComposeContext& context)
    : ctx(context)
{
    setMouseClickGrabsKeyboardFocus(false);
    const auto& p = ctx.theme.palette;
    menuLook.setColour(juce::PopupMenu::backgroundColourId, p.panel);
    menuLook.setColour(juce::PopupMenu::textColourId, p.text);
    menuLook.setColour(juce::PopupMenu::headerTextColourId, p.textDim);
    menuLook.setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    menuLook.setColour(juce::PopupMenu::highlightedTextColourId, p.onAccent);
}

TimelineView::~TimelineView()
{
    juce::PopupMenu::dismissAllActiveMenus();
    stopPreview();
}

juce::Rectangle<float> TimelineView::chordBounds(int flat) const
{
    const auto chords = dsp::placeChords(ctx.processor.getSong());
    if (flat < 0 || flat >= static_cast<int>(chords.size()))
        return {};
    const auto& c = chords[static_cast<std::size_t>(flat)];
    const float x0 = ctx.axis.xOf(c.start);
    const float x1 = ctx.axis.xOf(c.end());
    return {x0, static_cast<float>(kChordTop), x1 - x0, static_cast<float>(kChordH)};
}

juce::Rectangle<float> TimelineView::sectionBounds(int section) const
{
    const auto& song = ctx.processor.getSong();
    if (section < 0 || section >= static_cast<int>(song.sections.size()))
        return {};
    const double start = dsp::sectionStart(song, section);
    double length = dsp::sectionBeats(song.sections[static_cast<std::size_t>(section)]);
    if (length <= 0.0)
        length = kEmptySectionBeats; // the empty last section: room to drop the first chord
    const float x0 = ctx.axis.xOf(start);
    return {x0, static_cast<float>(kSectionTop), ctx.axis.xOf(start + length) - x0, static_cast<float>(kSectionH)};
}

juce::Rectangle<float> TimelineView::addSectionBounds() const
{
    const auto& song = ctx.processor.getSong();
    const auto last = sectionBounds(static_cast<int>(song.sections.size()) - 1);
    return {last.getRight() + 6.0f, static_cast<float>(kSectionTop), 24.0f, static_cast<float>(kSectionH)};
}

int TimelineView::chordAt(juce::Point<float> p) const
{
    if (p.y < kChordTop || p.y > kChordTop + kChordH || p.x < TimeAxis::kGutter)
        return -1;
    const double beat = ctx.axis.beatAt(p.x);
    return dsp::chordIndexAt(dsp::placeChords(ctx.processor.getSong()), beat);
}

int TimelineView::sectionAt(juce::Point<float> p) const
{
    for (int s = 0; s < static_cast<int>(ctx.processor.getSong().sections.size()); ++s)
        if (sectionBounds(s).contains(p))
            return s;
    return -1;
}

void TimelineView::paint(juce::Graphics& g)
{
    const auto& palette = ctx.theme.palette;
    const auto& song = ctx.processor.getSong();
    const auto chords = dsp::placeChords(song);
    const auto bounds = getLocalBounds().toFloat();
    const auto key = ctx.key();
    const unsigned home = dsp::scaleMask(key.homeTonic, key.homeScale);

    g.setColour(palette.textDim);
    g.setFont(makeFont(9.0f, true, 0.2f));
    g.drawText("SECTION", juce::Rectangle<float>(0.0f, kSectionTop, TimeAxis::kGutter - 6.0f, kSectionH),
               juce::Justification::centredLeft, false);
    g.drawText("CHORDS", juce::Rectangle<float>(0.0f, kChordTop, TimeAxis::kGutter - 6.0f, kChordH),
               juce::Justification::centredLeft, false);

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(getLocalBounds().withTrimmedLeft(TimeAxis::kGutter));

    // Lane backgrounds.
    g.setColour(palette.background.withAlpha(0.55f));
    g.fillRoundedRectangle(bounds.withTrimmedLeft(TimeAxis::kGutter).withY(kChordTop).withHeight(kChordH), 6.0f);

    // Sections.
    for (int s = 0; s < static_cast<int>(song.sections.size()); ++s)
    {
        const auto& section = song.sections[static_cast<std::size_t>(s)];
        const auto r = sectionBounds(s).reduced(1.0f, 0.0f);
        const bool empty = section.chords.empty();
        const auto colour = ctx.sectionColour(section.colour);
        if (empty)
        {
            g.setColour(colour.withAlpha(0.35f));
            const float dashes[] = {4.0f, 3.0f};
            juce::Path p;
            p.addRoundedRectangle(r.reduced(0.5f), 5.0f);
            juce::Path dashed;
            juce::PathStrokeType(1.2f).createDashedStroke(dashed, p, dashes, 2);
            g.fillPath(dashed);
            // The empty section's chord lane: where the first chord goes.
            const auto lane = r.withY(kChordTop).withHeight(kChordH);
            g.setColour(palette.textDim.withAlpha(0.5f));
            g.setFont(makeFont(9.0f, true, 0.16f));
            g.drawFittedText("DROP CHORDS", lane.toNearestInt(), juce::Justification::centred, 2, 0.8f);
        }
        else
        {
            g.setColour(colour);
            g.fillRoundedRectangle(r, 5.0f);
        }
        g.setColour(palette.text.withAlpha(empty ? 0.6f : 0.95f));
        g.setFont(makeFont(10.5f, true, 0.06f));
        g.drawFittedText(juce::String(section.name), r.reduced(8.0f, 0.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.7f);
    }
    {
        const auto plus = addSectionBounds();
        g.setColour(palette.panelRaised);
        g.fillRoundedRectangle(plus, 5.0f);
        g.setColour(palette.text);
        const auto c = plus.getCentre();
        g.fillRect(c.x - 5.0f, c.y - 1.0f, 10.0f, 2.0f);
        g.fillRect(c.x - 1.0f, c.y - 5.0f, 2.0f, 10.0f);
    }

    // Chords.
    const int playing = ctx.processor.isSequencing() ? ctx.processor.getPlayingChord() : -1;
    const double playhead = ctx.processor.getPlayheadBeats();
    for (const auto& c : chords)
    {
        const auto r = chordBounds(c.flat).reduced(1.0f, 2.0f);
        const auto colour = ctx.chordColour(c.shape);
        const bool selected =
            std::find(ctx.selectedChords.begin(), ctx.selectedChords.end(), c.flat) != ctx.selectedChords.end();
        const bool inKey = dsp::chordInKey(c.root, c.shape, home);
        const bool on = c.flat == playing;
        if (inKey)
        {
            g.setColour(colour.withMultipliedBrightness(on ? 1.0f : 0.78f));
            g.fillRoundedRectangle(r, 5.0f);
        }
        else
        {
            g.setColour(colour.withAlpha(on ? 0.4f : 0.16f));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(colour);
            g.drawRoundedRectangle(r.reduced(0.7f), 5.0f, 1.4f);
        }
        if (on)
        {
            juce::Graphics::ScopedSaveState inner(g);
            g.reduceClipRegion(r.toNearestInt());
            g.setColour(palette.text.withAlpha(0.18f));
            g.fillRect(r.withWidth(static_cast<float>((playhead - c.start) / c.beats) * r.getWidth()));
        }
        if (selected)
        {
            g.setColour(palette.text);
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 2.0f);
        }
        drawChordText(g, ctx.chordLabel(c.root, c.shape), r.reduced(6.0f, 2.0f), 14.0f,
                      inKey ? palette.onAccent : colour.brighter(0.3f));
        // Resize grip.
        g.setColour((inKey ? palette.onAccent : colour).withAlpha(0.45f));
        g.fillRoundedRectangle(r.getRight() - 4.0f, r.getCentreY() - 7.0f, 2.0f, 14.0f, 1.0f);
    }

    // Insert caret for a chord being dragged in.
    if (dropBeat >= 0.0)
    {
        const auto at = dsp::insertPointAt(song, dropBeat);
        double beat = dsp::sectionStart(song, at.section);
        for (int i = 0; i < at.index; ++i)
            beat += song.sections[static_cast<std::size_t>(at.section)].chords[static_cast<std::size_t>(i)].beats;
        const float x = ctx.axis.xOf(beat);
        g.setColour(palette.text);
        g.fillRoundedRectangle(x - 1.5f, kChordTop - 2.0f, 3.0f, kChordH + 4.0f, 1.5f);
    }

    // Ruler: beats, bars numbered.
    const double first = std::floor(ctx.axis.scrollBeats);
    const double last = ctx.axis.beatAt(bounds.getRight());
    g.setFont(makeFont(9.0f, true, 0.06f));
    for (double b = first; b <= last; b += 1.0)
    {
        const float x = ctx.axis.xOf(b);
        const bool bar = std::fmod(b, 4.0) < 1.0e-9;
        g.setColour(bar ? palette.textDim : palette.outline);
        g.fillRect(x, static_cast<float>(kRulerTop + (bar ? 2 : 9)), 1.0f, static_cast<float>(kRulerH - (bar ? 2 : 9)));
        if (bar && ctx.axis.pixelsPerBeat * 4.0 > 26.0)
            g.drawText(juce::String(static_cast<int>(b / 4.0) + 1),
                       juce::Rectangle<float>(x + 3.0f, kRulerTop, 30.0f, 12.0f), juce::Justification::centredLeft,
                       false);
    }
    const double songLength = dsp::songBeats(song);
    g.setColour(palette.outline);
    g.fillRect(ctx.axis.xOf(songLength), static_cast<float>(kRulerTop), 2.0f, static_cast<float>(kRulerH));

    // Playhead.
    const float x = ctx.axis.xOf(playhead);
    g.setColour(ctx.processor.isSequencing() ? palette.text : palette.textDim);
    g.fillRect(x - 0.5f, static_cast<float>(kChordTop - 2), 1.5f, static_cast<float>(kRulerTop + kRulerH - kChordTop));
    juce::Path head;
    head.addTriangle(x - 5.0f, kRulerTop + kRulerH - 8.0f, x + 5.0f, kRulerTop + kRulerH - 8.0f, x,
                     static_cast<float>(kRulerTop + kRulerH));
    g.fillPath(head);
}

void TimelineView::stopPreview()
{
    if (previewing)
    {
        previewing = false;
        ctx.processor.releaseRoot(kPreviewSource);
    }
}

void TimelineView::mouseMove(const juce::MouseEvent& e)
{
    const int c = chordAt(e.position);
    const bool edge = c >= 0 && chordBounds(c).getRight() - e.position.x < kEdgeGrab;
    setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void TimelineView::mouseDown(const juce::MouseEvent& e)
{
    drag = Drag::None;
    dragChord = -1;
    before = ctx.processor.getSong();
    const auto& song = before;

    if (addSectionBounds().contains(e.position))
    {
        addSection();
        return;
    }
    if (e.position.y >= kRulerTop)
    {
        drag = Drag::Seek;
        ctx.processor.seek(std::max(0.0, dsp::snapBeats(ctx.axis.beatAt(e.position.x), ctx.snap)));
        ctx.changed();
        return;
    }
    if (const int s = sectionAt(e.position); s >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            showSectionMenu(s);
            return;
        }
        // A section click selects its chords.
        ctx.selectedChords.clear();
        for (const auto& c : dsp::placeChords(song))
            if (c.section == s)
                ctx.selectedChords.push_back(c.flat);
        ctx.selectedTrack = -1;
        ctx.changed();
        return;
    }
    const int c = chordAt(e.position);
    if (c < 0)
    {
        if (!e.mods.isShiftDown())
            ctx.selectedChords.clear();
        ctx.changed();
        return;
    }
    if (e.mods.isPopupMenu())
    {
        if (std::find(ctx.selectedChords.begin(), ctx.selectedChords.end(), c) == ctx.selectedChords.end())
            ctx.selectedChords = {c};
        ctx.changed();
        showChordMenu(c);
        return;
    }
    ctx.selectedTrack = -1;
    auto& sel = ctx.selectedChords;
    const auto found = std::find(sel.begin(), sel.end(), c);
    if (e.mods.isShiftDown())
    {
        if (found != sel.end())
            sel.erase(found);
        else
            sel.push_back(c);
    }
    else if (found == sel.end())
    {
        sel = {c};
    }
    dragChord = c;
    const auto chords = dsp::placeChords(song);
    const auto& placed = chords[static_cast<std::size_t>(c)];
    ctx.focusRoot = placed.root;
    ctx.focusShape = placed.shape;
    if (chordBounds(c).getRight() - e.position.x < kEdgeGrab)
    {
        drag = Drag::Resize;
        dragStartBeats = placed.beats;
    }
    else
    {
        // Hear it while held, as on the palette.
        ctx.processor.setParameterFromUi(ids::chord, static_cast<float>(placed.shape));
        ctx.processor.pressRoot(kPreviewSource, placed.root);
        previewing = true;
    }
    ctx.changed();
}

void TimelineView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag == Drag::Seek)
    {
        ctx.processor.seek(std::max(0.0, dsp::snapBeats(ctx.axis.beatAt(e.position.x), ctx.snap)));
        ctx.changed();
        return;
    }
    if (dragChord < 0)
        return;
    if (drag == Drag::Resize)
    {
        const double delta = e.getDistanceFromDragStartX() / ctx.axis.pixelsPerBeat;
        const double beats = std::min(dsp::kMaxChordBeats, snapOr(dragStartBeats + delta, ctx.snap,
                                                                  ctx.snap > 0.0 ? ctx.snap : dsp::kMinChordBeats));
        auto song = before;
        const auto chords = dsp::placeChords(song);
        const auto& placed = chords[static_cast<std::size_t>(dragChord)];
        song.sections[static_cast<std::size_t>(placed.section)].chords[static_cast<std::size_t>(placed.index)].beats =
            beats;
        ctx.processor.setSong(song, false);
        ctx.changed();
        return;
    }
    if (drag == Drag::None && e.getDistanceFromDragStart() > 5)
    {
        drag = Drag::Move;
        stopPreview();
    }
    if (drag == Drag::Move)
    {
        dropBeat = std::max(0.0, ctx.axis.beatAt(e.position.x));
        repaint();
    }
}

void TimelineView::mouseUp(const juce::MouseEvent& e)
{
    stopPreview();
    if (drag == Drag::Resize)
    {
        ctx.processor.pushUndo(before);
    }
    else if (drag == Drag::Move && dropBeat >= 0.0)
    {
        // Move (alt: copy) the selected chords to the caret.
        auto song = before;
        const bool copy = e.mods.isAltDown();
        auto moving = copy ? std::vector<dsp::SongChord>{} : dsp::takeChords(song, ctx.selectedChords);
        if (copy)
        {
            auto copySong = before;
            moving = dsp::takeChords(copySong, ctx.selectedChords);
        }
        // Where the caret falls once the moved chords are out of the way.
        const auto chordsBefore = dsp::placeChords(before);
        double removedBefore = 0.0;
        if (!copy)
            for (int i : ctx.selectedChords)
                if (i >= 0 && i < static_cast<int>(chordsBefore.size()) &&
                    chordsBefore[static_cast<std::size_t>(i)].end() <= dropBeat + 1.0e-9)
                    removedBefore += chordsBefore[static_cast<std::size_t>(i)].beats;
        auto at = dsp::insertPointAt(song, std::max(0.0, dropBeat - removedBefore));
        ctx.selectedChords.clear();
        for (const auto& c : moving)
            if (dsp::insertChord(song, at, c))
                ++at.index;
        dsp::tidySections(song);
        ctx.processor.setSong(song, false);
        ctx.processor.pushUndo(before);
        // Select what moved: find it at its new place.
        const auto placed = dsp::placeChords(song);
        int flat = 0;
        for (int s = 0; s < at.section; ++s)
            flat += static_cast<int>(song.sections[static_cast<std::size_t>(s)].chords.size());
        for (int i = 0; i < static_cast<int>(moving.size()); ++i)
            ctx.selectedChords.push_back(flat + at.index - static_cast<int>(moving.size()) + i);
    }
    drag = Drag::None;
    dropBeat = -1.0;
    ctx.changed();
}

void TimelineView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (const int s = sectionAt(e.position); s >= 0)
        renameSection(s);
}

void TimelineView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        ctx.axis.zoom(wheel.deltaY > 0 ? 1.15 : 1.0 / 1.15, ctx.axis.beatAt(e.position.x));
    }
    else
    {
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        ctx.axis.scrollBeats = std::max(0.0, ctx.axis.scrollBeats - delta * 200.0 / ctx.axis.pixelsPerBeat);
    }
    ctx.changed();
}

bool TimelineView::isInterestedInDragSource(const SourceDetails& details)
{
    int root = 0, shape = 0;
    return PaletteCell::parse(details.description, root, shape);
}

void TimelineView::itemDragMove(const SourceDetails& details)
{
    dropBeat = std::max(0.0, ctx.axis.beatAt(static_cast<float>(details.localPosition.x)));
    repaint();
}

void TimelineView::itemDragExit(const SourceDetails&)
{
    dropBeat = -1.0;
    repaint();
}

void TimelineView::itemDropped(const SourceDetails& details)
{
    int root = 0, shape = 0;
    const double beat = std::max(0.0, ctx.axis.beatAt(static_cast<float>(details.localPosition.x)));
    dropBeat = -1.0;
    if (!PaletteCell::parse(details.description, root, shape))
        return;
    const double length = ctx.snap >= 1.0 ? 4.0 : 4.0; // a bar; drag the edge to change it
    ctx.edit([&](dsp::Song& song) { dsp::insertChord(song, dsp::insertPointAt(song, beat), {root, shape, length}); });
}

void TimelineView::addSection()
{
    ctx.edit(
        [](dsp::Song& song)
        {
            if (static_cast<int>(song.sections.size()) >= dsp::kMaxSections)
                return;
            dsp::Section section;
            section.name = dsp::nextSectionName(song);
            section.colour = static_cast<int>(song.sections.size());
            song.sections.push_back(section);
        });
}

void TimelineView::renameSection(int section)
{
    const auto r = sectionBounds(section).toNearestInt();
    nameEditor = std::make_unique<juce::TextEditor>();
    const auto& p = ctx.theme.palette;
    nameEditor->setColour(juce::TextEditor::backgroundColourId, p.panelRaised);
    nameEditor->setColour(juce::TextEditor::textColourId, p.text);
    nameEditor->setColour(juce::TextEditor::outlineColourId, p.accent);
    nameEditor->setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    nameEditor->setFont(makeFont(11.0f, true, 0.04f));
    nameEditor->setText(juce::String(ctx.processor.getSong().sections[static_cast<std::size_t>(section)].name));
    nameEditor->selectAll();
    nameEditor->setBounds(r.withWidth(std::max(r.getWidth(), 110)));
    addAndMakeVisible(*nameEditor);
    nameEditor->grabKeyboardFocus();
    const auto commit = [this, section]
    {
        if (nameEditor == nullptr)
            return;
        const auto text = nameEditor->getText().trim().substring(0, 24);
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<TimelineView>(this), section, text]
            {
                if (safe == nullptr)
                    return;
                safe->nameEditor.reset();
                if (text.isNotEmpty())
                    safe->ctx.edit(
                        [section, text](dsp::Song& song)
                        {
                            if (section < static_cast<int>(song.sections.size()))
                                song.sections[static_cast<std::size_t>(section)].name = text.toStdString();
                        });
                if (auto* top = safe->getTopLevelComponent())
                    top->grabKeyboardFocus();
            });
    };
    nameEditor->onReturnKey = commit;
    nameEditor->onFocusLost = commit;
    nameEditor->onEscapeKey = [this]
    {
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<TimelineView>(this)]
            {
                if (safe != nullptr)
                    safe->nameEditor.reset();
            });
    };
}

void TimelineView::deleteSelectedChords()
{
    if (ctx.selectedChords.empty())
        return;
    const auto selected = ctx.selectedChords;
    ctx.selectedChords.clear();
    ctx.edit([&](dsp::Song& song) { dsp::takeChords(song, selected); });
}

void TimelineView::showChordMenu(int flat)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addSectionHeader(
        ctx.chordLabel(dsp::placeChords(ctx.processor.getSong())[static_cast<std::size_t>(flat)].root,
                       dsp::placeChords(ctx.processor.getSong())[static_cast<std::size_t>(flat)].shape));
    menu.addItem(1, "Duplicate");
    menu.addItem(2, "Delete");
    juce::PopupMenu lengths;
    const double options[] = {1.0, 2.0, 4.0, 8.0, 16.0};
    const char* names[] = {"1 beat", "2 beats", "1 bar", "2 bars", "4 bars"};
    for (int i = 0; i < 5; ++i)
        lengths.addItem(10 + i, names[i]);
    menu.addSubMenu("Length", lengths);
    juce::Component::SafePointer<TimelineView> safe(this);
    const auto selected = ctx.selectedChords;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe, selected, options](int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            if (result == 2)
            {
                safe->deleteSelectedChords();
                return;
            }
            safe->ctx.edit(
                [&](dsp::Song& song)
                {
                    if (result == 1)
                    {
                        auto copy = song;
                        auto taken = dsp::takeChords(copy, selected);
                        const auto chords = dsp::placeChords(song);
                        const int lastFlat = *std::max_element(selected.begin(), selected.end());
                        const auto& last = chords[static_cast<std::size_t>(lastFlat)];
                        dsp::InsertPoint at{last.section, last.index + 1};
                        for (const auto& c : taken)
                            if (dsp::insertChord(song, at, c))
                                ++at.index;
                        return;
                    }
                    const double beats = options[result - 10];
                    const auto chords = dsp::placeChords(song);
                    for (int i : selected)
                        if (i >= 0 && i < static_cast<int>(chords.size()))
                            song.sections[static_cast<std::size_t>(chords[static_cast<std::size_t>(i)].section)]
                                .chords[static_cast<std::size_t>(chords[static_cast<std::size_t>(i)].index)]
                                .beats = beats;
                });
        });
}

void TimelineView::showSectionMenu(int section)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&menuLook);
    menu.addItem(1, "Rename");
    menu.addItem(2, "Duplicate");
    menu.addItem(3, "Delete with its chords");
    menu.addItem(4, "Add a section after");
    juce::PopupMenu colours;
    for (int i = 0; i < 6; ++i)
        colours.addColouredItem(10 + i, "Colour " + juce::String(i + 1), ctx.sectionColour(i).brighter(0.4f));
    menu.addSubMenu("Colour", colours);
    juce::Component::SafePointer<TimelineView> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe, section](int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            if (result == 1)
            {
                safe->renameSection(section);
                return;
            }
            safe->ctx.selectedChords.clear();
            safe->ctx.edit(
                [&](dsp::Song& song)
                {
                    if (section >= static_cast<int>(song.sections.size()))
                        return;
                    auto& sections = song.sections;
                    if (result == 2 && static_cast<int>(sections.size()) < dsp::kMaxSections &&
                        dsp::countChords(song) +
                                static_cast<int>(sections[static_cast<std::size_t>(section)].chords.size()) <=
                            dsp::kMaxSongChords)
                        sections.insert(sections.begin() + section + 1, sections[static_cast<std::size_t>(section)]);
                    else if (result == 3)
                        sections.erase(sections.begin() + section);
                    else if (result == 4 && static_cast<int>(sections.size()) < dsp::kMaxSections)
                    {
                        dsp::Section added;
                        added.name = dsp::nextSectionName(song);
                        added.colour = static_cast<int>(sections.size());
                        // An empty section only stays at the end; elsewhere it is a placeholder for
                        // the next drop, so give it the first chord of the one before.
                        if (section < static_cast<int>(sections.size()) - 1)
                            added.chords.push_back(sections[static_cast<std::size_t>(section)].chords.empty()
                                                       ? dsp::SongChord{}
                                                       : sections[static_cast<std::size_t>(section)].chords.back());
                        sections.insert(sections.begin() + section + 1, added);
                    }
                    else if (result >= 10)
                        sections[static_cast<std::size_t>(section)].colour = result - 10;
                });
        });
}
} // namespace dolphinflow::ui
