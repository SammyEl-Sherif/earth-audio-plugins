#pragma once

#include "CosmosProcessor.h"
#include "ui/CosmosTheme.h"
#include "ui/CosmosWidgets.h"

#include <animalaudio/ui/BrandBadge.h>
#include <animalaudio/ui/ChoicePillRow.h>
#include <animalaudio/ui/PillButton.h>

#include <memory>
#include <vector>

namespace cosmos
{
class CosmosEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 980;
    static constexpr int kHeight = 660;

    explicit CosmosEditor(CosmosProcessor& owner);
    ~CosmosEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::Button& getModeButton(dsp::Mode mode) noexcept { return *modeButtons[static_cast<std::size_t>(mode)]; }
    animalaudio::ui::ChoicePillRow& getVariationPills() noexcept { return *variationPills; }
    juce::Slider& getActivityKnob() noexcept { return activityKnob; }
    juce::Slider& getTimeKnob() noexcept { return timeKnob; }
    juce::Slider& getMixKnob() noexcept { return mixKnob; }
    juce::Button& getHoldButton() noexcept { return holdButton; }
    juce::Button& getSyncButton() noexcept { return syncButton; }
    juce::Button& getTapButton() noexcept { return tapButton; }
    juce::Button& getRecordButton() noexcept { return recordButton; }
    juce::Button& getStopButton() noexcept { return stopButton; }
    juce::Button& getClearButton() noexcept { return clearButton; }

    /** Advances the display as the timer would (tests call it without a message loop). */
    void refresh();

private:
    void timerCallback() override { refresh(); }

    float value(const juce::ParameterID& id) const;
    juce::String valueText(const juce::ParameterID& id) const;
    void attach(juce::Slider& slider, const juce::ParameterID& id, const juce::String& tooltip);
    void attach(juce::Button& button, const juce::ParameterID& id, const juce::String& tooltip);
    void showMode(int index);
    void tapTempo();

    void paintHeader(juce::Graphics& g) const;
    void paintPanel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, juce::Colour key) const;
    void paintEffectPanel(juce::Graphics& g) const;
    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintLooperPanel(juce::Graphics& g, juce::Rectangle<float> area) const;
    void paintStrip(juce::Graphics& g, juce::Rectangle<float> area) const;

    CosmosProcessor& owner;
    animalaudio::ui::Theme theme;
    animalaudio::ui::BrandBadge brand;

    ui::CosmosKnob activityKnob, shapeKnob, filterKnob, mixKnob, timeKnob, repeatsKnob, spaceKnob;
    ui::CosmosKnob modFreqKnob, resonanceKnob, effectVolumeKnob, modDepthKnob, reverbTimeKnob, loopLevelKnob;
    ui::HoldButton holdButton;
    animalaudio::ui::PillButton syncButton, tapButton;
    animalaudio::ui::PillButton recordButton, stopButton, clearButton, reverseButton, preFxButton;
    std::vector<std::unique_ptr<ui::ModeButton>> modeButtons;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<animalaudio::ui::ChoicePillRow> variationPills;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    std::vector<double> tapTimes;
    int shownMode = 0;
    int frame = 0;
    juce::int64 shownState = 0;

    juce::TooltipWindow tooltipWindow{this, 400};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CosmosEditor)
};
} // namespace cosmos
