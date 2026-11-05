#include "CosmosProcessor.h"

#include "CosmosEditor.h"

#include <animalaudio/core/ParameterFactory.h>
#include <animalaudio/core/TransportReader.h>

#include <algorithm>
#include <cmath>

namespace cosmos
{
juce::String programName(int index)
{
    const int clamped = std::clamp(index, 0, CosmosProcessor::kNumPrograms - 1);
    return juce::String(dsp::modeName(dsp::modeFromIndex(clamped / dsp::kNumVariations))) + " " +
           juce::String::charToString(static_cast<juce::juce_wchar>('A' + clamped % dsp::kNumVariations));
}

namespace
{
using animalaudio::core::percentParameter;
using animalaudio::core::unitParameter;

juce::String hzText(float hz)
{
    return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray modes;
    for (int i = 0; i < dsp::kNumModes; ++i)
        modes.add(dsp::modeName(dsp::modeFromIndex(i)));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::mode, "Mode", modes, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::variation, "Variation",
                                                            juce::StringArray{"A", "B", "C", "D"}, 0));

    layout.add(percentParameter(ids::activity, "Activity", 0.5f));
    layout.add(percentParameter(ids::shape, "Shape", 0.6f));
    layout.add(unitParameter(ids::modFreq, "Mod Freq", 0.05f, 12.0f, 1.5f, 1.0f, "Hz",
                             [](float v) { return juce::String(v, v < 1.0f ? 2 : 1) + " Hz"; }));
    layout.add(unitParameter(ids::filter, "Filter", 60.0f, 20000.0f, 1200.0f, 20000.0f, "Hz", hzText));
    layout.add(percentParameter(ids::resonance, "Resonance", 0.15f));
    layout.add(percentParameter(ids::mix, "Mix", 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ids::effectVolume, "Effect Volume", juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int)
                                         { return (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));

    juce::StringArray times;
    for (const auto* label : dsp::kTimeLabels)
        times.add(label);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::time, "Time", times, dsp::kDefaultTime));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::sync, "Host Sync", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ids::tempo, "Tempo", juce::NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("BPM")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " BPM"; })
            .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));

    layout.add(percentParameter(ids::repeats, "Repeats", 0.3f));
    layout.add(percentParameter(ids::modDepth, "Mod Depth", 0.0f));
    layout.add(percentParameter(ids::space, "Space", 0.3f));
    layout.add(unitParameter(ids::reverbTime, "Reverb Time", 0.4f, 20.0f, 3.0f, 3.0f, "s",
                             [](float v) { return juce::String(v, 1) + " s"; }));
    layout.add(percentParameter(ids::loopLevel, "Loop Level", 0.8f));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::hold, "Hold", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::loopReverse, "Loop Reverse", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::loopPreFx, "Loop Pre-FX", true));
    return layout;
}
} // namespace

CosmosProcessor::CosmosProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "CosmosState", createParameterLayout())
{
    const auto raw = [this](const juce::ParameterID& id) { return apvts.getRawParameterValue(id.getParamID()); };
    modeParam = raw(ids::mode);
    variationParam = raw(ids::variation);
    activityParam = raw(ids::activity);
    shapeParam = raw(ids::shape);
    modFreqParam = raw(ids::modFreq);
    filterParam = raw(ids::filter);
    resonanceParam = raw(ids::resonance);
    mixParam = raw(ids::mix);
    effectVolumeParam = raw(ids::effectVolume);
    timeParam = raw(ids::time);
    syncParam = raw(ids::sync);
    tempoParam = raw(ids::tempo);
    repeatsParam = raw(ids::repeats);
    modDepthParam = raw(ids::modDepth);
    spaceParam = raw(ids::space);
    reverbTimeParam = raw(ids::reverbTime);
    loopLevelParam = raw(ids::loopLevel);
    holdParam = raw(ids::hold);
    loopReverseParam = raw(ids::loopReverse);
    loopPreFxParam = raw(ids::loopPreFx);

    engine.setSettings(readSettings());
    engine.prepare(currentRate);
}

bool CosmosProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return output == layouts.getMainInputChannelSet();
}

dsp::Mode CosmosProcessor::getMode() const noexcept
{
    return dsp::modeFromIndex(juce::roundToInt(modeParam->load()));
}

int CosmosProcessor::getCurrentProgram()
{
    return static_cast<int>(getMode()) * dsp::kNumVariations +
           std::clamp(juce::roundToInt(variationParam->load()), 0, dsp::kNumVariations - 1);
}

void CosmosProcessor::setCurrentProgram(int index)
{
    const int clamped = std::clamp(index, 0, kNumPrograms - 1);
    const auto set = [this](const juce::ParameterID& id, int value)
    {
        auto* param = apvts.getParameter(id.getParamID());
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(value)));
    };
    set(ids::mode, clamped / dsp::kNumVariations);
    set(ids::variation, clamped % dsp::kNumVariations);
}

dsp::Settings CosmosProcessor::readSettings() const noexcept
{
    dsp::Settings s;
    s.mode = getMode();
    s.variation = std::clamp(juce::roundToInt(variationParam->load()), 0, dsp::kNumVariations - 1);
    s.activity = activityParam->load();
    s.shape = shapeParam->load();
    s.modHz = modFreqParam->load();
    s.modDepth = modDepthParam->load();
    s.cutoffHz = filterParam->load();
    s.resonance = resonanceParam->load();
    s.mix = mixParam->load();
    s.effectGain = juce::Decibels::decibelsToGain(effectVolumeParam->load(), -60.0f);
    s.time = std::clamp(juce::roundToInt(timeParam->load()), 0, dsp::kNumTimes - 1);
    s.sync = syncParam->load() > 0.5f;
    s.tempo = tempoParam->load();
    s.repeats = repeatsParam->load();
    s.space = spaceParam->load();
    s.reverbSeconds = reverbTimeParam->load();
    s.loopLevel = loopLevelParam->load();
    s.hold = holdParam->load() > 0.5f;
    s.loopReverse = loopReverseParam->load() > 0.5f;
    s.loopPreFx = loopPreFxParam->load() > 0.5f;
    return s;
}

void CosmosProcessor::prepareToPlay(double sampleRate, int)
{
    currentRate = sampleRate;
    engine.setSettings(readSettings());
    engine.prepare(sampleRate);
}

double CosmosProcessor::getTailLengthSeconds() const
{
    return 20.0;
}

void CosmosProcessor::sendLooperCommand(dsp::PhraseLooper::Command command) noexcept
{
    pendingCommand.store(static_cast<int>(command), std::memory_order_release);
}

void CosmosProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    const int command = pendingCommand.exchange(0, std::memory_order_acquire);
    if (command != 0)
        engine.looperCommand(static_cast<dsp::PhraseLooper::Command>(command));

    engine.setSettings(readSettings());
    const auto transport = animalaudio::core::readTransport(getPlayHead());
    const int channels = std::min(buffer.getNumChannels(), dsp::CosmosEngine::kMaxChannels);
    float* pointers[dsp::CosmosEngine::kMaxChannels] = {};
    for (int channel = 0; channel < channels; ++channel)
        pointers[channel] = buffer.getWritePointer(channel);
    engine.process(pointers, channels, buffer.getNumSamples(), transport);
}

void CosmosProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void CosmosProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* CosmosProcessor::createEditor()
{
    return new CosmosEditor(*this);
}
} // namespace cosmos

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cosmos::CosmosProcessor();
}
