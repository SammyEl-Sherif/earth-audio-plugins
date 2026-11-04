#include "CosmosEditor.h"

#include "ui/CosmosMotif.h"

#include <animalaudio/core/BuildStamp.h>

#include <algorithm>
#include <cmath>

namespace cosmos
{
namespace
{
using animalaudio::ui::makeFont;
using Command = dsp::PhraseLooper::Command;
using LoopState = dsp::PhraseLooper::State;

constexpr float kHeaderH = 74.0f;
const juce::Rectangle<float> kEffectPanel{20.0f, 90.0f, 310.0f, 300.0f};
const juce::Rectangle<float> kDisplay{342.0f, 90.0f, 396.0f, 300.0f};
const juce::Rectangle<float> kLooperPanel{750.0f, 90.0f, 210.0f, 300.0f};
const juce::Rectangle<float> kStrip{20.0f, 404.0f, 940.0f, 226.0f};
constexpr int kColumns = 8;
constexpr float kColumnW = 940.0f / kColumns;
constexpr float kFieldSeconds = static_cast<float>(dsp::CosmosEngine::kOverviewSeconds);
constexpr float kFieldSemitones = 24.0f; // +-24 semitones top to bottom of the grain field

enum Column
{
    ActivityCol = 0,
    ShapeCol,
    FilterCol,
    MixCol,
    TimeCol,
    RepeatsCol,
    SpaceCol,
    HoldCol
};

constexpr const char* kColumnNames[kColumns] = {"ACTIVITY", "SHAPE",   "FILTER", "MIX",
                                                "TIME",     "REPEATS", "SPACE",  "HOLD"};

/** Each knob column walks along the sunset ramp. */
float columnHue(int column)
{
    static constexpr float hues[kColumns] = {0.0f, 0.0f, 0.2f, 0.42f, 0.55f, 0.72f, 0.92f, 1.0f};
    return hues[column];
}

float columnCentre(int column)
{
    return kStrip.getX() + kColumnW * (static_cast<float>(column) + 0.5f);
}

juce::Rectangle<float> familyBox(dsp::Family family)
{
    const int index = static_cast<int>(family);
    const int row = index / 2;
    const int col = index % 2;
    return {kEffectPanel.getX() + 18.0f + static_cast<float>(col) * 142.0f,
            kEffectPanel.getY() + 34.0f + static_cast<float>(row) * 108.0f, 132.0f, 100.0f};
}

int indexInFamily(dsp::Mode mode)
{
    int index = 0;
    for (int i = 0; i < static_cast<int>(mode); ++i)
        index += dsp::familyOf(dsp::modeFromIndex(i)) == dsp::familyOf(mode) ? 1 : 0;
    return index;
}

juce::Font monoFont(float height, bool bold = true)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height,
                                        bold ? juce::Font::bold : juce::Font::plain));
}

juce::String withDots(std::initializer_list<juce::String> parts)
{
    static const juce::String separator(juce::CharPointer_UTF8("  \xc2\xb7  "));
    juce::StringArray all;
    for (const auto& part : parts)
        if (part.isNotEmpty())
            all.add(part);
    return all.joinIntoString(separator);
}

const char* shapeName(float shape)
{
    static constexpr const char* names[] = {"SQUARE", "SWELL", "TRIANGLE", "DECAY"};
    return names[std::clamp(juce::roundToInt(shape * 3.0f), 0, 3)];
}

const char* looperStateName(LoopState state)
{
    switch (state)
    {
    case LoopState::Recording:
        return "REC";
    case LoopState::Playing:
        return "PLAY";
    case LoopState::Overdubbing:
        return "DUB";
    case LoopState::Stopped:
        return "STOP";
    case LoopState::Empty:
        break;
    }
    return "EMPTY";
}

/** The four SHAPE envelopes as tiny icons: square, swell, triangle, decay. */
juce::Path shapeIcon(int index, juce::Rectangle<float> box)
{
    juce::Path path;
    const float l = box.getX(), r = box.getRight(), t = box.getY(), b = box.getBottom(), m = box.getCentreX();
    path.startNewSubPath(l, b);
    switch (index)
    {
    case 0:
        path.lineTo(l, t);
        path.lineTo(r, t);
        path.lineTo(r, b);
        break;
    case 1:
        path.lineTo(r, t);
        path.lineTo(r, b);
        break;
    case 2:
        path.lineTo(m, t);
        path.lineTo(r, b);
        break;
    default:
        path.lineTo(l, t);
        path.lineTo(r, b);
        break;
    }
    return path;
}
} // namespace

CosmosEditor::CosmosEditor(CosmosProcessor& plugin)
    : juce::AudioProcessorEditor(plugin)
    , owner(plugin)
    , theme(ui::makeTheme())
    , brand(theme.palette)
    , activityKnob(theme, ui::familyColour(dsp::familyOf(plugin.getMode())))
    , shapeKnob(theme, ui::sunset(columnHue(ShapeCol)))
    , filterKnob(theme, ui::sunset(columnHue(FilterCol)))
    , mixKnob(theme, ui::sunset(columnHue(MixCol)))
    , timeKnob(theme, ui::sunset(columnHue(TimeCol)), dsp::kNumTimes)
    , repeatsKnob(theme, ui::sunset(columnHue(RepeatsCol)))
    , spaceKnob(theme, ui::sunset(columnHue(SpaceCol)))
    , modFreqKnob(theme, ui::sunset(columnHue(ShapeCol)), 15)
    , resonanceKnob(theme, ui::sunset(columnHue(FilterCol)), 15)
    , effectVolumeKnob(theme, ui::sunset(columnHue(MixCol)), 15)
    , modDepthKnob(theme, ui::sunset(columnHue(RepeatsCol)), 15)
    , reverbTimeKnob(theme, ui::sunset(columnHue(SpaceCol)), 15)
    , loopLevelKnob(theme, theme.palette.text, 15)
    , holdButton(theme.palette)
    , syncButton("SYNC", theme.palette)
    , tapButton("TAP", theme.palette)
    , recordButton("REC", theme.palette)
    , stopButton("STOP", theme.palette)
    , clearButton("CLEAR", theme.palette)
    , reverseButton("REVERSE", theme.palette)
    , preFxButton("PRE-FX", theme.palette)
{
    setOpaque(true);
    brand.setTooltip(animalaudio::core::buildStampLong(theme.name));

    attach(activityKnob, ids::activity, "How busy the effect is: layers, density, speed or taps, by mode.");
    attach(shapeKnob, ids::shape, "Grain envelope: square, swell, triangle, decay.");
    attach(filterKnob, ids::filter, "Resonant lowpass on the effect.");
    attach(mixKnob, ids::mix, "Dry / wet.");
    attach(timeKnob, ids::time, "Clock speed against the tempo: 1/4 to 8 steps per beat.");
    attach(repeatsKnob, ids::repeats, "Feeds the effect back into itself: cascading loops and echoes.");
    attach(spaceKnob, ids::space, "Reverb on the effect.");
    attach(modFreqKnob, ids::modFreq, "Pitch modulation rate.");
    attach(resonanceKnob, ids::resonance, "Filter resonance.");
    attach(effectVolumeKnob, ids::effectVolume, "Level of the effect before the mix.");
    attach(modDepthKnob, ids::modDepth, "Pitch modulation depth, up to a semitone.");
    attach(reverbTimeKnob, ids::reverbTime, "Reverb decay time.");
    attach(loopLevelKnob, ids::loopLevel, "Phrase loop playback level.");
    attach(holdButton, ids::hold, "Freezes what the effect is listening to: it keeps playing the held moment.");
    attach(syncButton, ids::sync, "Follow the host tempo. Off (or no host tempo): TAP / own tempo.");
    attach(reverseButton, ids::loopReverse, "Plays the phrase loop backwards.");
    attach(preFxButton, ids::loopPreFx, "On: the loop plays through the effect. Off: the loop records the output.");

    auto& modeParam = *owner.apvts.getParameter(ids::mode.getParamID());
    for (int i = 0; i < dsp::kNumModes; ++i)
    {
        const auto mode = dsp::modeFromIndex(i);
        auto button =
            std::make_unique<ui::ModeButton>(dsp::modeName(mode), theme.palette, ui::familyColour(dsp::familyOf(mode)));
        button->setTooltip(dsp::modeBlurb(mode));
        button->onClick = [this, i]
        {
            showMode(i);
            modeAttachment->setValueAsCompleteGesture(static_cast<float>(i));
        };
        addAndMakeVisible(*button);
        modeButtons.push_back(std::move(button));
    }
    modeAttachment =
        std::make_unique<juce::ParameterAttachment>(modeParam, [this](float v) { showMode(juce::roundToInt(v)); });
    modeAttachment->sendInitialUpdate();

    auto& variationChoice =
        *dynamic_cast<juce::AudioParameterChoice*>(owner.apvts.getParameter(ids::variation.getParamID()));
    variationPills =
        std::make_unique<animalaudio::ui::ChoicePillRow>(variationChoice, theme.palette, "Four takes on each effect.");
    addAndMakeVisible(*variationPills);

    tapButton.setTooltip("Tap the tempo (turns SYNC off).");
    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible(tapButton);

    recordButton.setTooltip("Record, then play, then overdub.");
    recordButton.onClick = [this] { owner.sendLooperCommand(Command::RecPlayDub); };
    stopButton.setTooltip("Stop the loop.");
    stopButton.onClick = [this] { owner.sendLooperCommand(Command::Stop); };
    clearButton.setTooltip("Erase the loop.");
    clearButton.onClick = [this] { owner.sendLooperCommand(Command::Clear); };
    for (auto* button : {&recordButton, &stopButton, &clearButton})
        addAndMakeVisible(*button);
    addAndMakeVisible(brand);

    setSize(kWidth, kHeight);
    refresh();
    startTimerHz(30);
}

CosmosEditor::~CosmosEditor()
{
    stopTimer();
}

void CosmosEditor::attach(juce::Slider& slider, const juce::ParameterID& id, const juce::String& tooltip)
{
    auto* param = owner.apvts.getParameter(id.getParamID());
    slider.setTooltip(tooltip);
    slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(*param, slider));
    addAndMakeVisible(slider);
}

void CosmosEditor::attach(juce::Button& button, const juce::ParameterID& id, const juce::String& tooltip)
{
    button.setClickingTogglesState(true);
    button.setTooltip(tooltip);
    buttonAttachments.push_back(
        std::make_unique<juce::ButtonParameterAttachment>(*owner.apvts.getParameter(id.getParamID()), button));
    addAndMakeVisible(button);
}

float CosmosEditor::value(const juce::ParameterID& id) const
{
    const auto* param = owner.apvts.getParameter(id.getParamID());
    return param->convertFrom0to1(param->getValue());
}

juce::String CosmosEditor::valueText(const juce::ParameterID& id) const
{
    return owner.apvts.getParameter(id.getParamID())->getCurrentValueAsText();
}

void CosmosEditor::showMode(int index)
{
    shownMode = std::clamp(index, 0, dsp::kNumModes - 1);
    for (int i = 0; i < static_cast<int>(modeButtons.size()); ++i)
        modeButtons[static_cast<std::size_t>(i)]->setToggleState(i == shownMode, juce::dontSendNotification);
    activityKnob.setColour(ui::familyColour(dsp::familyOf(dsp::modeFromIndex(shownMode))));
    repaint();
}

void CosmosEditor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (!tapTimes.empty() && now - tapTimes.back() > 2000.0)
        tapTimes.clear();
    tapTimes.push_back(now);
    if (tapTimes.size() > 5)
        tapTimes.erase(tapTimes.begin());
    if (tapTimes.size() < 2)
        return;
    const double interval = (tapTimes.back() - tapTimes.front()) / static_cast<double>(tapTimes.size() - 1);
    const auto bpm = static_cast<float>(std::clamp(60000.0 / interval, 40.0, 240.0));
    for (const auto& [id, real] : {std::pair<const juce::ParameterID*, float>{&ids::tempo, bpm}, {&ids::sync, 0.0f}})
    {
        auto* param = owner.apvts.getParameter(id->getParamID());
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(real));
        param->endChangeGesture();
    }
    repaint();
}

void CosmosEditor::refresh()
{
    ++frame;
    const auto& engine = owner.getEngine();
    const auto state = engine.getLooperState();
    const char* label = state == LoopState::Empty ? "REC" : state == LoopState::Playing ? "DUB" : "PLAY";
    if (recordButton.getButtonText() != label)
        recordButton.setButtonText(label);
    recordButton.setToggleState(state == LoopState::Recording || state == LoopState::Overdubbing,
                                juce::dontSendNotification);

    juce::int64 hash = static_cast<int>(state) + (engine.isFrozen() ? 7 : 0) + (engine.isHostTempo() ? 13 : 0);
    hash = hash * 1000003 + juce::roundToInt(engine.getBpm() * 10.0);
    for (const auto* id : {&ids::mode,       &ids::variation, &ids::activity, &ids::shape,        &ids::modFreq,
                           &ids::filter,     &ids::resonance, &ids::mix,      &ids::effectVolume, &ids::time,
                           &ids::sync,       &ids::tempo,     &ids::repeats,  &ids::modDepth,     &ids::space,
                           &ids::reverbTime, &ids::loopLevel, &ids::hold,     &ids::loopReverse,  &ids::loopPreFx})
        hash = hash * 1000003 + juce::roundToInt(value(*id) * 1000.0f);
    if (hash != shownState)
    {
        shownState = hash;
        repaint();
        return;
    }
    repaint(kDisplay.expanded(4.0f).getSmallestIntegerContainer());
    repaint(kLooperPanel.withHeight(150.0f).getSmallestIntegerContainer());
}

void CosmosEditor::paintHeader(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    const auto header = getLocalBounds().toFloat().removeFromTop(kHeaderH);
    g.setGradientFill(juce::ColourGradient(palette.panel, 0.0f, 0.0f, palette.background, 0.0f, kHeaderH, false));
    g.fillRect(header);
    theme.motif->drawDecoration(g, header, palette);

    // Retro racing stripes along the top edge.
    for (int i = 0; i < 4; ++i)
    {
        g.setColour(ui::sunset(0.08f + 0.28f * static_cast<float>(i)));
        g.fillRect(0.0f, 2.0f * static_cast<float>(i), header.getWidth(), 1.6f);
    }
    g.setColour(palette.outline);
    g.fillRect(header.withTop(kHeaderH - 1.0f));

    theme.motif->drawMascot(g, {16.0f, 12.0f, 58.0f, 58.0f}, palette);
    ui::drawSunsetText(g, theme.name, {86.0f, 14.0f, 300.0f, 32.0f}, makeFont(31.0f, true, 0.32f), palette.panel);
    g.setColour(palette.textDim);
    g.setFont(makeFont(10.5f, true, 0.34f));
    g.drawText(theme.tagline, juce::Rectangle<float>(88.0f, 48.0f, 360.0f, 14.0f), juce::Justification::centredLeft,
               false);
}

void CosmosEditor::paintPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                              juce::Colour key) const
{
    const auto& palette = theme.palette;
    constexpr float radius = 10.0f;
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(area.translated(0.0f, 3.0f), radius);
    g.setGradientFill(juce::ColourGradient(palette.panel.brighter(0.06f), 0.0f, area.getY(),
                                           palette.panel.darker(0.15f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle(area, radius);
    g.setColour(palette.outline);
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
    g.setColour(palette.text.withAlpha(0.06f));
    g.fillRect(area.getX() + radius, area.getY() + 1.0f, area.getWidth() - 2.0f * radius, 1.0f);

    if (title.isEmpty())
        return;
    const auto row = juce::Rectangle<float>(area.getX() + 18.0f, area.getY() + 12.0f, area.getWidth() - 36.0f, 14.0f);
    g.setColour(key);
    g.fillRoundedRectangle(juce::Rectangle<float>(10.0f, 3.0f).withCentre({row.getX() + 5.0f, row.getCentreY()}), 1.5f);
    g.setColour(palette.textDim);
    g.setFont(makeFont(10.5f, true, 0.3f));
    g.drawText(title, row.withTrimmedLeft(18.0f), juce::Justification::centredLeft, false);
}

void CosmosEditor::paintEffectPanel(juce::Graphics& g) const
{
    const auto& palette = theme.palette;
    paintPanel(g, kEffectPanel, "EFFECT", palette.accent);
    const auto selected = dsp::familyOf(dsp::modeFromIndex(shownMode));
    for (int f = 0; f < dsp::kNumFamilies; ++f)
    {
        const auto family = static_cast<dsp::Family>(f);
        const auto box = familyBox(family);
        const auto colour = ui::familyColour(family);
        const bool active = family == selected;
        g.setColour(palette.background.withAlpha(0.55f));
        g.fillRoundedRectangle(box, 7.0f);
        g.setColour(active ? colour.withAlpha(0.55f) : palette.outline);
        g.drawRoundedRectangle(box.reduced(0.5f), 7.0f, 1.0f);
        g.setColour(active ? colour : colour.withAlpha(0.7f));
        g.setFont(makeFont(9.5f, true, 0.26f));
        g.drawText(dsp::familyName(family), box.reduced(10.0f, 0.0f).withY(box.getY() + 6.0f).withHeight(14.0f),
                   juce::Justification::centredLeft, false);
    }
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.5f, true, 0.26f));
    g.drawText("VARIATION",
               juce::Rectangle<float>(kEffectPanel.getX() + 18.0f, kEffectPanel.getY() + 250.0f, 200.0f, 12.0f),
               juce::Justification::centredLeft, false);
}

void CosmosEditor::paintDisplay(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    const auto& engine = owner.getEngine();
    const auto mode = dsp::modeFromIndex(shownMode);
    const auto family = dsp::familyOf(mode);
    const auto colour = ui::familyColour(family);
    const bool frozen = engine.isFrozen();
    constexpr float radius = 12.0f;
    const double time = frame / 30.0;

    g.setColour(colour.withAlpha(0.10f));
    g.fillRoundedRectangle(area.expanded(3.0f), radius + 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(area.translated(0.0f, 3.0f), radius);

    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;
    clip.addRoundedRectangle(area, radius);
    g.reduceClipRegion(clip);
    g.setColour(palette.ambient);
    g.fillRect(area);
    theme.motif->drawAmbient(g, area, time, palette);

    // Header: family, effect and variation; tempo and beat.
    const float left = area.getX() + 18.0f;
    const float top = area.getY() + 14.0f;
    g.setColour(colour);
    g.setFont(makeFont(9.5f, true, 0.3f));
    g.drawText(dsp::familyName(family), juce::Rectangle<float>(left, top, 200.0f, 12.0f),
               juce::Justification::centredLeft, false);
    const auto nameFont = makeFont(27.0f, true, 0.16f);
    const juce::String name = juce::String(dsp::modeName(mode)).toUpperCase();
    g.setColour(palette.text);
    g.setFont(nameFont);
    g.drawText(name, juce::Rectangle<float>(left - 1.0f, top + 13.0f, 260.0f, 32.0f), juce::Justification::centredLeft,
               false);
    const float nameWidth = juce::GlyphArrangement::getStringWidth(nameFont, name);
    const auto badge = juce::Rectangle<float>(left + nameWidth + 8.0f, top + 19.0f, 20.0f, 20.0f);
    g.setColour(colour);
    g.drawRoundedRectangle(badge, 4.0f, 1.2f);
    g.setFont(monoFont(13.0f));
    g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + juce::roundToInt(value(ids::variation)))),
               badge, juce::Justification::centred, false);

    const float right = area.getRight() - 18.0f;
    g.setColour(palette.text);
    g.setFont(monoFont(18.0f));
    g.drawText(juce::String(engine.getBpm(), 1), juce::Rectangle<float>(right - 120.0f, top - 2.0f, 92.0f, 22.0f),
               juce::Justification::centredRight, false);
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.0f, true, 0.2f));
    g.drawText("BPM", juce::Rectangle<float>(right - 26.0f, top + 3.0f, 26.0f, 14.0f),
               juce::Justification::centredRight, false);
    g.drawText(withDots({engine.isHostTempo() ? "HOST" : "OWN",
                         dsp::kTimeLabels[std::clamp(juce::roundToInt(value(ids::time)), 0, 5)]}),
               juce::Rectangle<float>(right - 140.0f, top + 22.0f, 140.0f, 12.0f), juce::Justification::centredRight,
               false);
    const auto step = static_cast<long long>(std::floor(engine.getStepPosition()));
    for (int i = 0; i < 4; ++i)
    {
        const bool lit = static_cast<int>(((step % 4) + 4) % 4) == i;
        const auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre({right - 3.5f - 13.0f * (3 - i), top + 44.0f});
        if (lit)
        {
            g.setColour(colour.withAlpha(0.35f));
            g.fillEllipse(dot.expanded(3.0f));
        }
        g.setColour(lit ? colour : palette.outline.brighter(0.2f));
        g.fillEllipse(dot);
    }

    // Grain field: time runs right (now) to left (4 s ago), pitch up/down from the unison line.
    const auto field = juce::Rectangle<float>(left, area.getY() + 82.0f, area.getWidth() - 36.0f, 166.0f);
    const float centreY = field.getCentreY();
    const auto yOf = [&](float semitones)
    {
        return centreY -
               std::clamp(semitones, -kFieldSemitones, kFieldSemitones) * field.getHeight() * 0.5f / kFieldSemitones;
    };
    const auto xOf = [&](float secondsAgo)
    { return field.getRight() - std::clamp(secondsAgo / kFieldSeconds, 0.0f, 1.0f) * field.getWidth(); };

    g.setColour(palette.background.withAlpha(0.45f));
    g.fillRoundedRectangle(field, 6.0f);
    for (const float semis : {-24.0f, -12.0f, 12.0f, 24.0f})
    {
        g.setColour(palette.text.withAlpha(std::abs(semis) > 13.0f ? 0.04f : 0.08f));
        g.fillRect(field.getX(), yOf(semis), field.getWidth(), 1.0f);
    }
    for (int s = 1; s < static_cast<int>(kFieldSeconds); ++s)
    {
        g.setColour(palette.text.withAlpha(0.05f));
        g.fillRect(xOf(static_cast<float>(s)), field.getY(), 1.0f, field.getHeight());
    }
    g.setColour(palette.textDim.withAlpha(0.6f));
    g.setFont(monoFont(8.5f, false));
    g.drawText("+12", juce::Rectangle<float>(field.getX() + 4.0f, yOf(12.0f) - 11.0f, 30.0f, 10.0f),
               juce::Justification::centredLeft, false);
    g.drawText("-12", juce::Rectangle<float>(field.getX() + 4.0f, yOf(-12.0f) + 1.0f, 30.0f, 10.0f),
               juce::Justification::centredLeft, false);

    // What the effect is listening to (frozen while held).
    const int bins = dsp::CosmosEngine::kOverviewBins;
    const float binW = field.getWidth() / static_cast<float>(bins);
    const auto wave = frozen ? palette.accent : palette.text;
    for (int age = 0; age < bins; ++age)
    {
        const float level = std::sqrt(std::clamp(engine.overview(age), 0.0f, 1.0f));
        if (level < 0.005f)
            continue;
        const float h = level * field.getHeight() * 0.45f;
        const float x = field.getRight() - (static_cast<float>(age) + 1.0f) * binW;
        g.setColour(wave.withAlpha(frozen ? 0.22f : 0.13f));
        g.fillRect(x + 0.3f, centreY - h, std::max(0.8f, binW - 0.6f), h * 2.0f);
    }
    g.setColour(palette.text.withAlpha(0.18f));
    g.fillRect(field.getX(), centreY, field.getWidth(), 1.0f);

    for (int k = 0; k < engine.getTapCount(); ++k)
    {
        const float x = xOf(engine.tapSecondsAgo(k));
        const float dashes[] = {3.0f, 3.0f};
        g.setColour(colour.withAlpha(0.7f));
        g.drawDashedLine(juce::Line<float>(x, field.getY() + 6.0f, x, field.getBottom() - 6.0f), dashes, 2, 1.2f);
        juce::Path diamond;
        diamond.addTriangle(x - 5.0f, centreY, x, centreY - 6.0f, x + 5.0f, centreY);
        diamond.addTriangle(x - 5.0f, centreY, x, centreY + 6.0f, x + 5.0f, centreY);
        g.setColour(colour);
        g.fillPath(diamond);
    }

    for (int k = 0; k < dsp::CosmosEngine::kMaxGrains; ++k)
    {
        const float ago = engine.grainSecondsAgo(k);
        if (ago < 0.0f)
            continue;
        const float level = std::clamp(engine.grainLevel(k), 0.0f, 1.0f);
        const juce::Point<float> p(xOf(ago), yOf(engine.grainSemitones(k)));
        const float r = 2.0f + 5.0f * std::sqrt(level);
        g.setColour(colour.withAlpha(0.18f));
        g.fillEllipse(juce::Rectangle<float>(r * 4.0f, r * 4.0f).withCentre(p));
        g.setColour(colour.interpolatedWith(palette.text, 0.25f));
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(p));
    }
    g.setColour(palette.text.withAlpha(0.7f));
    g.fillRect(field.getRight() - 1.0f, field.getY(), 1.5f, field.getHeight());
    g.setColour(palette.textDim.withAlpha(0.7f));
    g.setFont(makeFont(8.5f, true, 0.2f));
    g.drawText("NOW", juce::Rectangle<float>(field.getRight() - 40.0f, field.getBottom() + 2.0f, 40.0f, 10.0f),
               juce::Justification::centredRight, false);
    g.drawText("4 S AGO", juce::Rectangle<float>(field.getX(), field.getBottom() + 2.0f, 60.0f, 10.0f),
               juce::Justification::centredLeft, false);

    if (frozen)
    {
        const auto chip = juce::Rectangle<float>(field.getCentreX() - 34.0f, field.getY() + 8.0f, 68.0f, 18.0f);
        g.setColour(palette.accent);
        g.fillRoundedRectangle(chip, 9.0f);
        g.setColour(palette.onAccent);
        g.setFont(makeFont(10.0f, true, 0.3f));
        g.drawText("HOLD", chip, juce::Justification::centred, false);
    }

    // Footer: what the effect does, and the output meter.
    const auto footer = juce::Rectangle<float>(left, area.getBottom() - 30.0f, area.getWidth() - 36.0f, 14.0f);
    g.setColour(colour);
    g.setFont(makeFont(9.5f, true, 0.16f));
    g.drawText(juce::String(dsp::modeBlurb(mode)).toUpperCase(), footer, juce::Justification::centredLeft, false);
    const float peak = engine.getOutputPeak();
    const int lit = juce::roundToInt(
        std::clamp((juce::Decibels::gainToDecibels(peak, -60.0f) + 48.0f) / 48.0f, 0.0f, 1.0f) * 12.0f);
    for (int i = 0; i < 12; ++i)
    {
        const auto cell = juce::Rectangle<float>(footer.getRight() - 4.0f * static_cast<float>(12 - i) - 2.0f,
                                                 footer.getY() + 3.0f, 3.0f, 8.0f);
        g.setColour(i < lit ? ui::sunset(1.0f - static_cast<float>(i) / 11.0f) : palette.outline);
        g.fillRect(cell);
    }

    // CRT: scanlines, glass sheen and a vignette.
    for (float y = area.getY(); y < area.getBottom(); y += 3.0f)
    {
        g.setColour(juce::Colours::black.withAlpha(0.16f));
        g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
    }
    g.setGradientFill(juce::ColourGradient(palette.text.withAlpha(0.05f), 0.0f, area.getY(),
                                           palette.text.withAlpha(0.0f), 0.0f, area.getY() + area.getHeight() * 0.4f,
                                           false));
    g.fillRect(area.withHeight(area.getHeight() * 0.4f));
    g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, area.getCentreX(), area.getCentreY(),
                                           juce::Colours::black.withAlpha(0.45f), area.getX(), area.getY(), true));
    g.fillRect(area);
    g.setColour(colour.withAlpha(0.35f));
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
}

void CosmosEditor::paintLooperPanel(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    const auto& engine = owner.getEngine();
    paintPanel(g, area, "LOOPER", palette.detail);

    const auto state = engine.getLooperState();
    const auto colour = state == LoopState::Recording     ? palette.detail
                        : state == LoopState::Overdubbing ? ui::sunset(0.0f)
                        : state == LoopState::Playing     ? palette.accent
                                                          : palette.textDim;
    const juce::Point<float> centre(area.getX() + 66.0f, area.getY() + 94.0f);
    const float radius = 42.0f;
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    g.setColour(palette.background);
    g.strokePath(track, juce::PathStrokeType(7.0f));
    g.setColour(palette.outline);
    g.strokePath(track, juce::PathStrokeType(1.0f));
    const float progress = std::clamp(engine.getLooperProgress(), 0.0f, 1.0f);
    if (state != LoopState::Empty && progress > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * progress,
                          true);
        g.setColour(colour.withAlpha(0.25f));
        g.strokePath(arc, juce::PathStrokeType(10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour);
        g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(state == LoopState::Empty ? palette.textDim : colour);
    g.setFont(makeFont(14.0f, true, 0.24f));
    g.drawText(looperStateName(state), juce::Rectangle<float>(centre.x - 40.0f, centre.y - 14.0f, 80.0f, 16.0f),
               juce::Justification::centred, false);
    g.setColour(palette.text);
    g.setFont(monoFont(11.0f, false));
    g.drawText(juce::String(engine.getLooperSeconds(), 1) + " s",
               juce::Rectangle<float>(centre.x - 40.0f, centre.y + 4.0f, 80.0f, 14.0f), juce::Justification::centred,
               false);

    const auto knob = loopLevelKnob.getBounds().toFloat();
    g.setColour(palette.textDim);
    g.setFont(makeFont(9.0f, true, 0.24f));
    g.drawText("LEVEL", knob.withY(knob.getBottom() + 2.0f).withHeight(11.0f), juce::Justification::centred, false);
    g.setColour(palette.text);
    g.setFont(makeFont(10.5f, true, 0.04f));
    g.drawText(valueText(ids::loopLevel), knob.withY(knob.getBottom() + 14.0f).withHeight(12.0f),
               juce::Justification::centred, false);

    g.setColour(palette.textDim.withAlpha(0.7f));
    g.setFont(makeFont(9.0f, true, 0.16f));
    g.drawText(withDots({"UP TO 60 S", "LOOP NOT SAVED"}),
               juce::Rectangle<float>(area.getX() + 12.0f, area.getBottom() - 26.0f, area.getWidth() - 24.0f, 12.0f),
               juce::Justification::centred, false);
}

void CosmosEditor::paintStrip(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto& palette = theme.palette;
    paintPanel(g, area, {}, palette.text);
    const auto mode = dsp::modeFromIndex(shownMode);
    const auto modeColour = ui::familyColour(dsp::familyOf(mode));

    for (int c = 0; c < kColumns; ++c)
    {
        const float cx = columnCentre(c);
        const auto colour = c == ActivityCol ? modeColour : c == HoldCol ? palette.accent : ui::sunset(columnHue(c));
        if (c > 0)
        {
            g.setColour(palette.outline.withAlpha(0.6f));
            g.fillRect(area.getX() + kColumnW * static_cast<float>(c), area.getY() + 16.0f, 1.0f,
                       area.getHeight() - 32.0f);
        }
        g.setColour(palette.text);
        g.setFont(makeFont(10.5f, true, 0.26f));
        g.drawText(kColumnNames[c], juce::Rectangle<float>(cx - 55.0f, area.getY() + 12.0f, 110.0f, 14.0f),
                   juce::Justification::centred, false);
        g.setColour(colour);
        g.fillRoundedRectangle(juce::Rectangle<float>(16.0f, 2.0f).withCentre({cx, area.getY() + 30.0f}), 1.0f);

        juce::String readout;
        switch (c)
        {
        case ActivityCol:
            readout = valueText(ids::activity);
            break;
        case ShapeCol:
            readout = shapeName(value(ids::shape));
            break;
        case FilterCol:
            readout = valueText(ids::filter);
            break;
        case MixCol:
            readout = valueText(ids::mix);
            break;
        case TimeCol:
            readout = valueText(ids::time);
            break;
        case RepeatsCol:
            readout = valueText(ids::repeats);
            break;
        case SpaceCol:
            readout = valueText(ids::space);
            break;
        default:
            readout = owner.getEngine().isFrozen() || value(ids::hold) > 0.5f ? "HELD" : "LIVE";
            break;
        }
        g.setColour(c == HoldCol && value(ids::hold) > 0.5f ? palette.accent : palette.text);
        g.setFont(makeFont(12.5f, true, 0.06f));
        g.drawText(readout, juce::Rectangle<float>(cx - 55.0f, area.getY() + 128.0f, 110.0f, 16.0f),
                   juce::Justification::centred, false);
    }

    // SHAPE: the four envelopes around the knob.
    {
        const auto knob = shapeKnob.getBounds().toFloat();
        const float r = knob.getWidth() * 0.5f + 6.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float proportion = static_cast<float>(i) / 3.0f;
            const float a = juce::MathConstants<float>::pi * (1.2f + 1.6f * proportion);
            const juce::Point<float> p(knob.getCentreX() + r * std::sin(a), knob.getCentreY() - r * std::cos(a));
            const bool near = juce::roundToInt(value(ids::shape) * 3.0f) == i;
            g.setColour(near ? ui::sunset(columnHue(ShapeCol)) : palette.textDim.withAlpha(0.7f));
            g.strokePath(shapeIcon(i, juce::Rectangle<float>(9.0f, 7.0f).withCentre(p)),
                         juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        }
    }

    // Secondary controls.
    const auto secondary = [&](const juce::Slider& knob, const juce::String& label, const juce::ParameterID& id)
    {
        const auto b = knob.getBounds().toFloat();
        g.setColour(palette.textDim);
        g.setFont(makeFont(8.5f, true, 0.2f));
        g.drawText(label, b.withY(b.getBottom() + 2.0f).withHeight(11.0f).expanded(30.0f, 0.0f),
                   juce::Justification::centred, false);
        g.setColour(palette.text);
        g.setFont(makeFont(10.0f, true, 0.04f));
        g.drawText(valueText(id), b.withY(b.getBottom() + 13.0f).withHeight(12.0f).expanded(30.0f, 0.0f),
                   juce::Justification::centred, false);
    };
    secondary(modFreqKnob, "MOD FREQ", ids::modFreq);
    secondary(resonanceKnob, "RESONANCE", ids::resonance);
    secondary(effectVolumeKnob, "EFFECT VOL", ids::effectVolume);
    secondary(modDepthKnob, "MOD DEPTH", ids::modDepth);
    secondary(reverbTimeKnob, "REVERB TIME", ids::reverbTime);

    // ACTIVITY: what it means in this mode.
    {
        const float cx = columnCentre(ActivityCol);
        const float y = area.getY() + 158.0f;
        g.setColour(palette.textDim);
        g.setFont(makeFont(8.5f, true, 0.2f));
        g.drawText("CONTROLS", juce::Rectangle<float>(cx - 55.0f, y, 110.0f, 11.0f), juce::Justification::centred,
                   false);
        g.setColour(modeColour);
        g.setFont(makeFont(10.5f, true, 0.14f));
        g.drawText(dsp::activityMeaning(mode), juce::Rectangle<float>(cx - 56.0f, y + 14.0f, 112.0f, 14.0f),
                   juce::Justification::centred, false);
        g.setColour(palette.textDim.withAlpha(0.7f));
        g.setFont(makeFont(8.5f, true, 0.2f));
        g.drawText("IN " + juce::String(dsp::modeName(mode)).toUpperCase(),
                   juce::Rectangle<float>(cx - 55.0f, y + 30.0f, 110.0f, 11.0f), juce::Justification::centred, false);
    }

    // TIME: tempo source.
    {
        const float cx = columnCentre(TimeCol);
        const auto& engine = owner.getEngine();
        g.setColour(palette.text);
        g.setFont(monoFont(12.0f));
        g.drawText(juce::String(engine.getBpm(), 1) + " BPM",
                   juce::Rectangle<float>(cx - 55.0f, area.getY() + 186.0f, 110.0f, 14.0f),
                   juce::Justification::centred, false);
        g.setColour(palette.textDim);
        g.setFont(makeFont(8.5f, true, 0.2f));
        g.drawText(engine.isHostTempo() ? "FROM HOST" : "OWN TEMPO",
                   juce::Rectangle<float>(cx - 55.0f, area.getY() + 202.0f, 110.0f, 11.0f),
                   juce::Justification::centred, false);
    }

    // HOLD: a hint.
    {
        const float cx = columnCentre(HoldCol);
        g.setColour(palette.textDim);
        g.setFont(makeFont(8.5f, true, 0.16f));
        g.drawText("FREEZES THE", juce::Rectangle<float>(cx - 55.0f, area.getY() + 164.0f, 110.0f, 11.0f),
                   juce::Justification::centred, false);
        g.drawText("EFFECT'S INPUT", juce::Rectangle<float>(cx - 55.0f, area.getY() + 177.0f, 110.0f, 11.0f),
                   juce::Justification::centred, false);
        g.drawText("PATTERNS PLAY ON", juce::Rectangle<float>(cx - 55.0f, area.getY() + 190.0f, 110.0f, 11.0f),
                   juce::Justification::centred, false);
    }
}

void CosmosEditor::paint(juce::Graphics& g)
{
    const auto& palette = theme.palette;
    g.setGradientFill(juce::ColourGradient(palette.background.brighter(0.06f), 0.0f, kHeaderH, palette.background, 0.0f,
                                           static_cast<float>(getHeight()), false));
    g.fillAll();
    theme.motif->drawDecoration(g, getLocalBounds().toFloat().withTrimmedTop(kHeaderH), palette);
    paintHeader(g);
    paintEffectPanel(g);
    paintDisplay(g, kDisplay);
    paintLooperPanel(g, kLooperPanel);
    paintStrip(g, kStrip);

    g.setColour(palette.textDim.withAlpha(0.6f));
    g.setFont(makeFont(10.5f, false));
    const float y = static_cast<float>(getHeight()) - 24.0f;
    g.drawText(animalaudio::core::buildStampShort(), juce::Rectangle<float>(20.0f, y, 400.0f, 14.0f),
               juce::Justification::centredLeft, false);
    g.drawText(juce::String(juce::CharPointer_UTF8("11 EFFECTS  \xc2\xb7  4 VARIATIONS EACH  \xc2\xb7  HOST PROGRAMS")),
               juce::Rectangle<float>(static_cast<float>(getWidth()) - 420.0f, y, 400.0f, 14.0f),
               juce::Justification::centredRight, false);
}

void CosmosEditor::resized()
{
    brand.setBounds(getWidth() - 20 - 150, 28, 150, 18);

    for (int i = 0; i < dsp::kNumModes; ++i)
    {
        const auto mode = dsp::modeFromIndex(i);
        const auto box = familyBox(dsp::familyOf(mode));
        modeButtons[static_cast<std::size_t>(i)]->setBounds(
            juce::Rectangle<float>(box.getX() + 6.0f,
                                   box.getY() + 24.0f + 24.0f * static_cast<float>(indexInFamily(mode)),
                                   box.getWidth() - 12.0f, 22.0f)
                .getSmallestIntegerContainer());
    }
    variationPills->setBounds(static_cast<int>(kEffectPanel.getX()) + 18, static_cast<int>(kEffectPanel.getY()) + 266,
                              animalaudio::ui::ChoicePillRow::widthFor(dsp::kNumVariations), 24);

    const int top = static_cast<int>(kStrip.getY());
    const auto big = [&](juce::Component& c, int column)
    { c.setBounds(juce::roundToInt(columnCentre(column)) - 44, top + 36, 88, 88); };
    const auto small = [&](juce::Component& c, int column)
    { c.setBounds(juce::roundToInt(columnCentre(column)) - 24, top + 150, 48, 48); };
    big(activityKnob, ActivityCol);
    big(shapeKnob, ShapeCol);
    big(filterKnob, FilterCol);
    big(mixKnob, MixCol);
    big(timeKnob, TimeCol);
    big(repeatsKnob, RepeatsCol);
    big(spaceKnob, SpaceCol);
    big(holdButton, HoldCol);
    small(modFreqKnob, ShapeCol);
    small(resonanceKnob, FilterCol);
    small(effectVolumeKnob, MixCol);
    small(modDepthKnob, RepeatsCol);
    small(reverbTimeKnob, SpaceCol);
    const int timeX = juce::roundToInt(columnCentre(TimeCol));
    syncButton.setBounds(timeX - 50, top + 154, 48, 24);
    tapButton.setBounds(timeX + 2, top + 154, 48, 24);

    const auto looper = kLooperPanel.getSmallestIntegerContainer();
    loopLevelKnob.setBounds(looper.getX() + 128, looper.getY() + 56, 62, 62);
    recordButton.setBounds(looper.getX() + 18, looper.getY() + 160, 174, 30);
    stopButton.setBounds(looper.getX() + 18, looper.getY() + 198, 83, 26);
    clearButton.setBounds(looper.getX() + 109, looper.getY() + 198, 83, 26);
    reverseButton.setBounds(looper.getX() + 18, looper.getY() + 232, 83, 26);
    preFxButton.setBounds(looper.getX() + 109, looper.getY() + 232, 83, 26);
}
} // namespace cosmos
