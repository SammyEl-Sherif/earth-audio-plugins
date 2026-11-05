#pragma once

#include "DolphinFlowProcessor.h"
#include "ui/DolphinFlowTheme.h"

#include <animalaudio/ui/BrandBadge.h>
#include <animalaudio/ui/ChoicePillRow.h>
#include <animalaudio/ui/PillButton.h>
#include <animalaudio/ui/ThemedKnob.h>

#include <array>
#include <memory>
#include <vector>

namespace dolphinflow
{
/** Computer key for each root, Ableton's own layout: A W S E D F T G Y H U J = C .. B. */
inline constexpr char kRootKeys[12] = {'a', 'w', 's', 'e', 'd', 'f', 't', 'g', 'y', 'h', 'u', 'j'};
/** Computer key for each chord quality, in CHORD parameter order. */
inline constexpr char kChordKeys[dsp::kNumChordShapes] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};

/** One root on the on-screen keyboard: shows the note and its computer key, plays while held. */
class RootKey : public juce::Component, public juce::SettableTooltipClient
{
public:
    RootKey(DolphinFlowProcessor& processor, const animalaudio::ui::Palette& palette, int pitchClass);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isBlack() const noexcept;
    void setLit(bool shouldBeLit);

private:
    DolphinFlowProcessor& processor;
    animalaudio::ui::Palette colours;
    int pitchClass;
    bool lit = false;
    bool down = false;
};

class DolphinFlowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 960;
    static constexpr int kHeight = 600;
    static constexpr int kHistory = 150; // ribbon points, 5 s at the 30 Hz refresh

    explicit DolphinFlowEditor(DolphinFlowProcessor& owner);
    ~DolphinFlowEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType) override;
    void mouseDown(const juce::MouseEvent&) override;

    juce::Slider& getKnob(const juce::ParameterID& id);
    animalaudio::ui::ChoicePillRow& getChordPills() noexcept { return *chordPills; }
    animalaudio::ui::ChoicePillRow& getModePills() noexcept { return *modePills; }
    animalaudio::ui::ChoicePillRow& getVoicePills() noexcept { return *voicePills; }
    juce::Button& getLatchButton() noexcept { return latchButton; }
    juce::Button& getOctaveDownButton() noexcept { return octaveDown; }
    juce::Button& getOctaveUpButton() noexcept { return octaveUp; }
    RootKey& getRootKey(int pitchClass) noexcept { return *rootKeys[static_cast<std::size_t>(pitchClass)]; }

    /** Advances the display as the timer would (tests call it without a message loop). */
    void refresh();

private:
    struct KnobSlot
    {
        juce::ParameterID id;
        juce::String label;
        std::unique_ptr<animalaudio::ui::ThemedKnob> knob;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    void timerCallback() override { refresh(); }
    void stepOctave(int delta);
    void releaseHeldKeys();
    std::unique_ptr<animalaudio::ui::ChoicePillRow> makePills(const juce::ParameterID& id, const juce::String& tooltip,
                                                              bool upperCase);

    void paintHeader(juce::Graphics& g) const;
    void paintPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                    const juce::String& hint) const;
    void paintRibbons(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintKnobLabels(juce::Graphics& g) const;
    void paintChordKeys(juce::Graphics& g) const;

    DolphinFlowProcessor& owner;
    animalaudio::ui::Theme theme;
    animalaudio::ui::BrandBadge brand;
    std::vector<KnobSlot> knobs;
    std::unique_ptr<animalaudio::ui::ChoicePillRow> chordPills;
    std::unique_ptr<animalaudio::ui::ChoicePillRow> modePills;
    std::unique_ptr<animalaudio::ui::ChoicePillRow> voicePills;
    animalaudio::ui::PillButton latchButton;
    animalaudio::ui::PillButton octaveDown;
    animalaudio::ui::PillButton octaveUp;
    std::unique_ptr<juce::ButtonParameterAttachment> latchAttachment;
    std::array<std::unique_ptr<RootKey>, 12> rootKeys;

    std::array<int, 12> heldKeyCodes{}; // computer key held for each root, 0 when up
    std::array<std::array<float, dsp::kMaxVoices>, kHistory> history{};
    int historyHead = 0;
    float viewCentre = static_cast<float>(dsp::kDefaultCentre);
    double time = 0.0;
    bool focusGrabbed = false;

    juce::TooltipWindow tooltipWindow{this, 400};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DolphinFlowEditor)
};
} // namespace dolphinflow
