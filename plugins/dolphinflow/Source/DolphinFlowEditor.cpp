#include "DolphinFlowEditor.h"

#include <animalaudio/core/BuildStamp.h>

#include <algorithm>
#include <cmath>

namespace dolphinflow
{
namespace
{
using animalaudio::ui::makeFont;

constexpr float kHeaderH = 72.0f;
const juce::Rectangle<float> kDisplay{20.0f, 88.0f, 600.0f, 248.0f};
const juce::Rectangle<float> kKnobPanel{636.0f, 88.0f, 304.0f, 248.0f};
const juce::Rectangle<float> kKeysPanel{20.0f, 352.0f, 600.0f, 124.0f};
const juce::Rectangle<float> kSoundPanel{636.0f, 352.0f, 304.0f, 124.0f};
const juce::Rectangle<float> kChordPanel{20.0f, 492.0f, 920.0f, 92.0f};
constexpr float kViewSpan = 18.0f; // semitones above and below the ribbon view's centre
constexpr int kKnobColumns = 3;

juce::String noteName(int midiNote)
{
    return juce::String(dsp::pitchClassName(midiNote)) + juce::String(midiNote / 12 - 1);
}

bool keyIsDown(int keyCode)
{
    return juce::KeyPress::isKeyCurrentlyDown(keyCode) ||
           juce::KeyPress::isKeyCurrentlyDown(
               static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(keyCode)))) ||
           juce::KeyPress::isKeyCurrentlyDown(
               static_cast<int>(juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(keyCode))));
}

void signGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool plus)
{
    const auto box = area.withSizeKeepingCentre(10.0f, 10.0f);
    g.setColour(colour);
    g.fillRect(box.withSizeKeepingCentre(10.0f, 2.0f));
    if (plus)
        g.fillRect(box.withSizeKeepingCentre(2.0f, 10.0f));
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------

RootKey::RootKey(DolphinFlowProcessor& p, const animalaudio::ui::Palette& palette, int pc)
    : processor(p)
    , colours(palette)
    , pitchClass(pc)
{
    setMouseClickGrabsKeyboardFocus(false);
    setTooltip(juce::String("Play ") + dsp::pitchClassName(pc) + " (computer key " +
               juce::String::charToString(static_cast<juce::juce_wchar>(kRootKeys[pc])).toUpperCase() + ")");
}

bool RootKey::isBlack() const noexcept
{
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

void RootKey::setLit(bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void RootKey::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(1.5f);
    const bool on = lit || down;
    const auto face = on ? colours.accent : isBlack() ? colours.background : colours.panelRaised;
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), 6.0f);
    g.setGradientFill(juce::ColourGradient(face.brighter(on ? 0.1f : 0.06f), 0.0f, area.getY(), face.darker(0.15f),
                                           0.0f, area.getBottom(), false));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(on ? colours.accent.brighter(0.3f) : colours.outline);
    g.drawRoundedRectangle(area, 6.0f, 1.0f);

    const auto ink = on ? colours.onAccent : colours.text;
    g.setColour(ink);
    g.setFont(makeFont(isBlack() ? 11.5f : 14.0f, true, 0.04f));
    g.drawText(dsp::pitchClassName(pitchClass), area.withTrimmedBottom(area.getHeight() * 0.45f),
               juce::Justification::centredBottom, false);
    g.setColour(on ? colours.onAccent.withAlpha(0.75f) : colours.textDim);
    g.setFont(makeFont(10.0f, true, 0.1f));
    g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(kRootKeys[pitchClass])).toUpperCase(),
               area.withTrimmedTop(area.getHeight() * 0.6f).withTrimmedBottom(4.0f), juce::Justification::centred,
               false);
}

void RootKey::mouseDown(const juce::MouseEvent&)
{
    down = true;
    repaint();
    processor.pressRoot(DolphinFlowProcessor::kKeySourceBase + 100 + pitchClass, pitchClass);
}

void RootKey::mouseUp(const juce::MouseEvent&)
{
    down = false;
    repaint();
    processor.releaseRoot(DolphinFlowProcessor::kKeySourceBase + 100 + pitchClass);
}

// ---------------------------------------------------------------------------------------------------------------

DolphinFlowEditor::DolphinFlowEditor(DolphinFlowProcessor& plugin)
    : juce::AudioProcessorEditor(plugin)
    , owner(plugin)
    , theme(ui::makeTheme())
    , brand(theme.palette)
    , latchButton("LATCH", theme.palette)
    , octaveDown({}, theme.palette)
    , octaveUp({}, theme.palette)
{
    setOpaque(true);
    brand.setTooltip(animalaudio::core::buildStampLong(theme.name));

    const struct
    {
        const juce::ParameterID& id;
        const char* label;
        const char* tooltip;
    } knobSpecs[] = {
        {ids::glide, "GLIDE", "How long each note takes to slide into the next chord. 0 jumps."},
        {ids::curve, "CURVE", "Shape of the slide: low is an even glide, high swoops in fast and eases onto the note."},
        {ids::tone, "TONE", "Low-pass filter: dark and warm to bright and open."},
        {ids::width, "WIDTH", "Detunes each voice's two saws and fans the voices across the stereo field."},
        {ids::space, "SPACE", "Reverb amount and size."},
        {ids::level, "LEVEL", "Output level."},
    };
    for (const auto& spec : knobSpecs)
    {
        KnobSlot slot{spec.id, spec.label, std::make_unique<animalaudio::ui::ThemedKnob>(theme), nullptr};
        slot.knob->setTooltip(spec.tooltip);
        slot.attachment = std::make_unique<juce::SliderParameterAttachment>(
            *owner.apvts.getParameter(spec.id.getParamID()), *slot.knob);
        addAndMakeVisible(*slot.knob);
        knobs.push_back(std::move(slot));
    }

    chordPills =
        makePills(ids::chord, "Chord quality (number keys 1-0). Changing it while a chord plays glides to it.", false);
    modePills = makePills(
        ids::mode, "PAD swells and glides legato. STAB and KEYS strike again on every chord, and still glide.", true);
    voicePills = makePills(ids::voices, "How many voices the chord is spread across.", false);

    latchAttachment = std::make_unique<juce::ButtonParameterAttachment>(
        *owner.apvts.getParameter(ids::latch.getParamID()), latchButton);
    latchButton.setClickingTogglesState(true);
    latchButton.setTooltip("Chords keep ringing after you let go (key L). Turn off to release.");
    addAndMakeVisible(latchButton);

    octaveDown.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { signGlyph(g, r, c, false); };
    octaveUp.glyph = [](juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { signGlyph(g, r, c, true); };
    octaveDown.setTooltip("Octave down (key Z)");
    octaveUp.setTooltip("Octave up (key X)");
    octaveDown.onClick = [this] { stepOctave(-1); };
    octaveUp.onClick = [this] { stepOctave(1); };
    addAndMakeVisible(octaveDown);
    addAndMakeVisible(octaveUp);

    for (int pc = 0; pc < 12; ++pc)
    {
        rootKeys[static_cast<std::size_t>(pc)] = std::make_unique<RootKey>(owner, theme.palette, pc);
        addAndMakeVisible(*rootKeys[static_cast<std::size_t>(pc)]);
    }
    for (int pc = 0; pc < 12; ++pc)
        if (rootKeys[static_cast<std::size_t>(pc)]->isBlack())
            rootKeys[static_cast<std::size_t>(pc)]->toFront(false);

    addAndMakeVisible(brand);

    // Keys must reach the editor, so clicking a control never takes keyboard focus away from it.
    for (auto* child : getChildren())
        child->setMouseClickGrabsKeyboardFocus(false);
    for (auto* row : {chordPills.get(), modePills.get(), voicePills.get()})
        for (int i = 0; i < row->getNumPills(); ++i)
            row->getPill(i).setMouseClickGrabsKeyboardFocus(false);
    setWantsKeyboardFocus(true);

    for (auto& column : history)
        column.fill(-1.0f);

    setSize(kWidth, kHeight);
    refresh();
    startTimerHz(30);
}

DolphinFlowEditor::~DolphinFlowEditor()
{
    stopTimer();
    releaseHeldKeys();
}

std::unique_ptr<animalaudio::ui::ChoicePillRow>
DolphinFlowEditor::makePills(const juce::ParameterID& id, const juce::String& tooltip, bool upperCase)
{
    auto& choice = *dynamic_cast<juce::AudioParameterChoice*>(owner.apvts.getParameter(id.getParamID()));
    auto row = std::make_unique<animalaudio::ui::ChoicePillRow>(choice, theme.palette, tooltip);
    if (upperCase)
        for (int i = 0; i < row->getNumPills(); ++i)
            row->getPill(i).setButtonText(choice.choices[i].toUpperCase());
    addAndMakeVisible(*row);
    return row;
}

juce::Slider& DolphinFlowEditor::getKnob(const juce::ParameterID& id)
{
    for (auto& slot : knobs)
        if (slot.id.getParamID() == id.getParamID())
            return *slot.knob;
    jassertfalse;
    return *knobs.front().knob;
}

void DolphinFlowEditor::stepOctave(int delta)
{
    const int octave = juce::roundToInt(owner.getParameterValue(ids::octave));
    owner.setParameterFromUi(
        ids::octave, static_cast<float>(std::clamp(octave + delta, -dsp::kMaxOctaveShift, dsp::kMaxOctaveShift)));
}

void DolphinFlowEditor::releaseHeldKeys()
{
    heldKeyCodes.fill(0);
    owner.releaseAllKeys();
}

bool DolphinFlowEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false; // leave host shortcuts alone

    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    for (int pc = 0; pc < 12; ++pc)
        if (c == static_cast<juce::juce_wchar>(kRootKeys[pc]))
        {
            auto& code = heldKeyCodes[static_cast<std::size_t>(pc)];
            if (code == 0) // ignore auto-repeat
            {
                code = key.getKeyCode();
                owner.pressRoot(DolphinFlowProcessor::kKeySourceBase + pc, pc);
            }
            return true;
        }
    for (int i = 0; i < dsp::kNumChordShapes; ++i)
        if (c == static_cast<juce::juce_wchar>(kChordKeys[i]))
        {
            owner.setParameterFromUi(ids::chord, static_cast<float>(i));
            return true;
        }
    if (c == 'z' || c == 'x')
    {
        stepOctave(c == 'z' ? -1 : 1);
        return true;
    }
    if (c == 'l')
    {
        owner.setParameterFromUi(ids::latch, owner.getParameterValue(ids::latch) > 0.5f ? 0.0f : 1.0f);
        return true;
    }
    return false;
}

bool DolphinFlowEditor::keyStateChanged(bool)
{
    bool handled = false;
    for (int pc = 0; pc < 12; ++pc)
    {
        auto& code = heldKeyCodes[static_cast<std::size_t>(pc)];
        if (code != 0 && !keyIsDown(code))
        {
            code = 0;
            owner.releaseRoot(DolphinFlowProcessor::kKeySourceBase + pc);
            handled = true;
        }
    }
    return handled;
}

void DolphinFlowEditor::focusLost(FocusChangeType)
{
    // A key let go while another window has focus never reaches us; release rather than leave a chord stuck.
    if (std::any_of(heldKeyCodes.begin(), heldKeyCodes.end(), [](int code) { return code != 0; }))
        releaseHeldKeys();
}

void DolphinFlowEditor::mouseDown(const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void DolphinFlowEditor::refresh()
{
    if (!focusGrabbed && isShowing())
    {
        grabKeyboardFocus();
        focusGrabbed = true;
    }

    const auto& engine = owner.getEngine();
    historyHead = (historyHead + 1) % kHistory;
    auto& column = history[static_cast<std::size_t>(historyHead)];
    float sum = 0.0f;
    int sounding = 0;
    for (int v = 0; v < dsp::kMaxVoices; ++v)
    {
        column[static_cast<std::size_t>(v)] = engine.getVoicePitch(v);
        if (column[static_cast<std::size_t>(v)] >= 0.0f)
        {
            sum += column[static_cast<std::size_t>(v)];
            ++sounding;
        }
    }
    const float centre = static_cast<float>(dsp::kDefaultCentre) + 12.0f * owner.getParameterValue(ids::octave);
    const float target = sounding > 0 ? sum / static_cast<float>(sounding) : centre;
    viewCentre += 0.08f * (target - viewCentre);
    time += 1.0 / 30.0;

    const int root = engine.getRoot();
    const bool audible = engine.getEnvelopeLevel() > 0.002f || engine.isGateOn();
    for (int pc = 0; pc < 12; ++pc)
        rootKeys[static_cast<std::size_t>(pc)]->setLit(audible && pc == root);
    repaint();
}

// ---------------------------------------------------------------------------------------------------------------

void DolphinFlowEditor::paintHeader(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    const auto header = getLocalBounds().toFloat().removeFromTop(kHeaderH);
    g.setGradientFill(juce::ColourGradient(palette.panel, 0.0f, 0.0f, palette.background, 0.0f, kHeaderH, false));
    g.fillRect(header);
    juce::ColourGradient stripe(palette.accent, 0.0f, 0.0f, palette.ambient, header.getWidth(), 0.0f, false);
    stripe.addColour(0.5, palette.accentAlt);
    g.setGradientFill(stripe);
    g.fillRect(header.withHeight(3.0f));
    g.setColour(palette.outline);
    g.fillRect(header.withTop(kHeaderH - 1.0f));

    theme.motif->drawMascot(g, {16.0f, 7.0f, 58.0f, 58.0f}, palette);
    g.setColour(palette.text);
    g.setFont(makeFont(24.0f, true, 0.12f));
    g.drawText(theme.name, juce::Rectangle<float>(84.0f, 12.0f, 250.0f, 32.0f), juce::Justification::centredLeft,
               false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(10.5f, true, 0.3f));
    g.drawText(theme.tagline, juce::Rectangle<float>(85.0f, 44.0f, 240.0f, 14.0f), juce::Justification::centredLeft,
               false);

    // Now playing: chord symbol and the voiced notes.
    const auto& engine = owner.getEngine();
    const int root = engine.getRoot();
    const bool audible = root >= 0 && (engine.getEnvelopeLevel() > 0.002f || engine.isGateOn());
    const auto readout = juce::Rectangle<float>(340.0f, 8.0f, 290.0f, 58.0f);
    if (audible)
    {
        // A played voicing that matches no chord shape is shown by its pitch classes.
        juce::String symbol = juce::String(dsp::pitchClassName(root)) + dsp::chordShape(engine.getShape()).symbol;
        if (engine.getShape() < 0)
        {
            juce::StringArray classes;
            for (int v = 0; v < engine.getVoiceCount(); ++v)
                if (const float pitch = engine.getVoicePitch(v); pitch >= 0.0f)
                    classes.addIfNotAlreadyThere(dsp::pitchClassName(juce::roundToInt(pitch)));
            symbol = classes.joinIntoString(" ");
        }
        g.setColour(palette.accent);
        g.setFont(makeFont(30.0f, true, 0.04f));
        g.drawText(symbol, readout.withHeight(36.0f), juce::Justification::centred, false);
        juce::StringArray notes;
        for (int v = 0; v < engine.getVoiceCount(); ++v)
            if (const float pitch = engine.getVoicePitch(v); pitch >= 0.0f)
                notes.add(noteName(juce::roundToInt(pitch)));
        g.setColour(palette.textDim);
        g.setFont(makeFont(11.0f, true, 0.12f));
        g.drawText(notes.joinIntoString("  "), readout.withTrimmedTop(38.0f), juce::Justification::centred, false);
    }
    else
    {
        g.setColour(palette.textDim);
        g.setFont(makeFont(12.0f, true, 0.2f));
        g.drawText("PRESS A  W  S  E  D ...  TO PLAY", readout, juce::Justification::centred, false);
    }
}

void DolphinFlowEditor::paintPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                                   const juce::String& hint) const
{
    const auto& palette = theme.palette;
    constexpr float radius = 8.0f;
    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillRoundedRectangle(area.translated(0.0f, 2.0f), radius);
    g.setGradientFill(juce::ColourGradient(palette.panel.brighter(0.05f), 0.0f, area.getY(), palette.panel.darker(0.1f),
                                           0.0f, area.getBottom(), false));
    g.fillRoundedRectangle(area, radius);
    g.setColour(palette.outline);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);

    if (title.isNotEmpty())
    {
        g.setColour(palette.text);
        g.setFont(makeFont(11.0f, true, 0.28f));
        g.drawText(title, area.reduced(16.0f, 0.0f).withY(area.getY() + 9.0f).withHeight(14.0f),
                   juce::Justification::centredLeft, false);
    }
    if (hint.isNotEmpty())
    {
        g.setColour(palette.textDim);
        g.setFont(makeFont(10.0f, true, 0.12f));
        g.drawText(hint, area.reduced(16.0f, 0.0f).withY(area.getY() + 9.0f).withHeight(14.0f),
                   juce::Justification::centredRight, false);
    }
}

void DolphinFlowEditor::paintRibbons(juce::Graphics& g, juce::Rectangle<float> panel) const
{
    const auto& palette = theme.palette;
    const auto area = panel.reduced(14.0f).withTrimmedTop(20.0f);
    const float low = viewCentre - kViewSpan;
    const float high = viewCentre + kViewSpan;
    const auto yOf = [&](float pitch) { return area.getBottom() - (pitch - low) / (high - low) * area.getHeight(); };

    theme.motif->drawAmbient(g, area.withTop(area.getBottom() - 46.0f), time, palette);

    g.setFont(makeFont(9.5f, true, 0.1f));
    for (int note = static_cast<int>(std::ceil(low)); note <= static_cast<int>(high); ++note)
    {
        if (dsp::pitchClassOf(note) != 0)
            continue;
        const float y = yOf(static_cast<float>(note));
        g.setColour(palette.outline.withAlpha(0.6f));
        g.drawHorizontalLine(juce::roundToInt(y), area.getX() + 26.0f, area.getRight());
        g.setColour(palette.textDim);
        g.drawText(noteName(note), juce::Rectangle<float>(area.getX(), y - 7.0f, 26.0f, 14.0f),
                   juce::Justification::centredLeft, false);
    }

    const auto plot = area.withTrimmedLeft(30.0f).withTrimmedRight(30.0f);
    const float step = plot.getWidth() / static_cast<float>(kHistory - 1);
    const int voiceCount = std::max(1, owner.getEngine().getVoiceCount());
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(area.toNearestInt());
    for (int v = 0; v < dsp::kMaxVoices; ++v)
    {
        const float mixAmount = voiceCount > 1 ? static_cast<float>(v) / static_cast<float>(voiceCount - 1) : 0.0f;
        const auto colour = palette.accent.interpolatedWith(palette.accentAlt, std::min(1.0f, mixAmount));
        juce::Path ribbon;
        bool drawing = false;
        juce::Point<float> head;
        bool hasHead = false;
        for (int i = 0; i < kHistory; ++i)
        {
            const auto index = static_cast<std::size_t>((historyHead + 1 + i) % kHistory);
            const float pitch = history[index][static_cast<std::size_t>(v)];
            if (pitch < 0.0f)
            {
                drawing = false;
                hasHead = false;
                continue;
            }
            const juce::Point<float> point(plot.getX() + step * static_cast<float>(i), yOf(pitch));
            if (drawing)
                ribbon.lineTo(point);
            else
                ribbon.startNewSubPath(point);
            drawing = true;
            head = point;
            hasHead = true;
        }
        if (ribbon.isEmpty())
            continue;
        g.setColour(colour.withAlpha(0.16f));
        g.strokePath(ribbon, juce::PathStrokeType(10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour);
        g.strokePath(ribbon, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (hasHead)
        {
            g.setColour(palette.text);
            g.fillEllipse(head.x - 5.0f, head.y - 5.0f, 10.0f, 10.0f);
            g.setColour(palette.detail);
            g.fillEllipse(head.x + 3.0f, head.y - 2.0f, 6.0f, 4.0f);
            const float pitch = history[static_cast<std::size_t>(historyHead)][static_cast<std::size_t>(v)];
            g.setColour(palette.textDim);
            g.setFont(makeFont(9.5f, true, 0.06f));
            g.drawText(noteName(juce::roundToInt(pitch)),
                       juce::Rectangle<float>(head.x + 10.0f, head.y - 7.0f, 30.0f, 14.0f),
                       juce::Justification::centredLeft, false);
        }
    }
}

void DolphinFlowEditor::paintKnobLabels(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    for (const auto& slot : knobs)
    {
        const auto bounds = slot.knob->getBounds().toFloat();
        g.setColour(palette.text);
        g.setFont(makeFont(10.5f, true, 0.22f));
        g.drawText(slot.label, bounds.withY(bounds.getBottom() + 2.0f).withHeight(13.0f).expanded(14.0f, 0.0f),
                   juce::Justification::centred, false);
        g.setColour(palette.textDim);
        g.setFont(makeFont(10.0f, true, 0.04f));
        const auto* param = owner.apvts.getParameter(slot.id.getParamID());
        g.drawText(param->getCurrentValueAsText(),
                   bounds.withY(bounds.getBottom() + 15.0f).withHeight(13.0f).expanded(14.0f, 0.0f),
                   juce::Justification::centred, false);
    }
}

void DolphinFlowEditor::paintChordKeys(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    const auto row = chordPills->getBounds().toFloat();
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.1f));
    for (int i = 0; i < chordPills->getNumPills(); ++i)
    {
        const auto pill = chordPills->getPill(i).getBounds().toFloat().translated(row.getX(), row.getY());
        g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(kChordKeys[i])),
                   pill.withY(row.getY() - 15.0f).withHeight(12.0f), juce::Justification::centred, false);
    }

    const auto octave = juce::roundToInt(owner.getParameterValue(ids::octave));
    const auto down = octaveDown.getBounds().toFloat();
    const auto up = octaveUp.getBounds().toFloat();
    g.setColour(palette.text);
    g.setFont(makeFont(15.0f, true, 0.0f));
    g.drawText((octave > 0 ? "+" : "") + juce::String(octave),
               juce::Rectangle<float>(down.getRight(), down.getY(), up.getX() - down.getRight(), down.getHeight()),
               juce::Justification::centred, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.18f));
    g.drawText("OCTAVE  Z / X", juce::Rectangle<float>(down.getX() - 4.0f, row.getY() - 15.0f, 110.0f, 12.0f),
               juce::Justification::centredLeft, false);
    const auto latch = latchButton.getBounds().toFloat();
    g.drawText("KEY L", latch.withY(row.getY() - 15.0f).withHeight(12.0f), juce::Justification::centred, false);
}

void DolphinFlowEditor::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    g.fillAll(palette.background);
    paintHeader(g);

    paintPanel(g, kDisplay, "GLIDE", owner.apvts.getParameter(ids::glide.getParamID())->getCurrentValueAsText());
    paintRibbons(g, kDisplay);
    paintPanel(g, kKnobPanel, "SOUND", {});
    paintKnobLabels(g);
    paintPanel(g, kKeysPanel, "ROOT", "COMPUTER KEYS  A W S E D F T G Y H U J");
    paintPanel(g, kSoundPanel, {}, {});

    g.setColour(palette.text);
    g.setFont(makeFont(11.0f, true, 0.28f));
    g.drawText("MODE", modePills->getBounds().toFloat().translated(0.0f, -17.0f).withHeight(14.0f),
               juce::Justification::centredLeft, false);
    g.drawText("VOICES", voicePills->getBounds().toFloat().translated(0.0f, -17.0f).withHeight(14.0f),
               juce::Justification::centredLeft, false);

    paintPanel(g, kChordPanel, "CHORD", {});
    paintChordKeys(g);
}

void DolphinFlowEditor::resized()
{
    brand.setBounds(getWidth() - 20 - 150, 27, 150, 18);

    const auto knobArea = kKnobPanel.toNearestInt().withTrimmedTop(32).reduced(10, 4);
    const int cellW = knobArea.getWidth() / kKnobColumns;
    const int cellH = knobArea.getHeight() / 2;
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        const int col = static_cast<int>(i) % kKnobColumns;
        const int row = static_cast<int>(i) / kKnobColumns;
        const auto cell =
            juce::Rectangle<int>(knobArea.getX() + col * cellW, knobArea.getY() + row * cellH, cellW, cellH);
        knobs[i].knob->setBounds(cell.withSizeKeepingCentre(66, 66).withY(cell.getY() + 2));
    }

    // Piano layout: seven naturals across, sharps raised between them.
    const auto keys = kKeysPanel.toNearestInt().withTrimmedTop(32).reduced(16, 0).withTrimmedBottom(12);
    const float whiteW = static_cast<float>(keys.getWidth()) / 7.0f;
    static constexpr int whiteOf[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    for (int pc = 0; pc < 12; ++pc)
    {
        auto& key = *rootKeys[static_cast<std::size_t>(pc)];
        const float x = static_cast<float>(keys.getX()) + whiteW * static_cast<float>(whiteOf[pc]);
        if (key.isBlack())
            key.setBounds(juce::Rectangle<float>(x + whiteW - 25.0f, static_cast<float>(keys.getY()), 50.0f,
                                                 static_cast<float>(keys.getHeight()) * 0.58f)
                              .toNearestInt());
        else
            key.setBounds(
                juce::Rectangle<float>(x, static_cast<float>(keys.getY()), whiteW, static_cast<float>(keys.getHeight()))
                    .toNearestInt());
    }

    const auto sound = kSoundPanel.toNearestInt();
    modePills->setBounds(sound.getX() + 16, sound.getY() + 30, animalaudio::ui::ChoicePillRow::widthFor(3), 26);
    voicePills->setBounds(sound.getX() + 16, sound.getY() + 84, animalaudio::ui::ChoicePillRow::widthFor(4), 26);

    const auto chord = kChordPanel.toNearestInt();
    chordPills->setBounds(chord.getX() + 16, chord.getY() + 46, animalaudio::ui::ChoicePillRow::widthFor(10), 30);
    octaveDown.setBounds(744, chord.getY() + 46, 28, 30);
    octaveUp.setBounds(812, chord.getY() + 46, 28, 30);
    latchButton.setBounds(852, chord.getY() + 46, 72, 30);
}
} // namespace dolphinflow
