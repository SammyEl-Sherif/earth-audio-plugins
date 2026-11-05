#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace animalaudio::core
{
/** "42 %" for a 0..1 value. */
inline juce::String percentText(float value)
{
    return juce::String(juce::roundToInt(value * 100.0f)) + " %";
}

/** 0..1 float shown and typed as a percentage. */
inline std::unique_ptr<juce::AudioParameterFloat> percentParameter(const juce::ParameterID& id,
                                                                   const juce::String& name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat>(
        id, name, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), defaultValue,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float value, int) { return percentText(value); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue() * 0.01f; }));
}

/** Float in [low, high] skewed so `centre` sits mid-travel, with a unit label and custom read-out. */
inline std::unique_ptr<juce::AudioParameterFloat> unitParameter(const juce::ParameterID& id, const juce::String& name,
                                                                float low, float high, float centre, float defaultValue,
                                                                const juce::String& label,
                                                                std::function<juce::String(float)> format)
{
    juce::NormalisableRange<float> range(low, high, 0.0f);
    range.setSkewForCentre(centre);
    return std::make_unique<juce::AudioParameterFloat>(
        id, name, range, defaultValue,
        juce::AudioParameterFloatAttributes()
            .withLabel(label)
            .withStringFromValueFunction([format](float value, int) { return format(value); })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); }));
}
} // namespace animalaudio::core
