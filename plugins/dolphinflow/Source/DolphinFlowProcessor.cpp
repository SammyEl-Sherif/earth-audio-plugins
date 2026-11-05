#include "SwanProcessor.h"

#include "SwanEditor.h"

#include <animalaudio/core/ParameterFactory.h>

#include <algorithm>

namespace swan
{
namespace
{
using animalaudio::core::percentParameter;
using animalaudio::core::unitParameter;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray chords;
    for (const auto& shape : dsp::kChordShapes)
        chords.add(shape.label);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::chord, "Chord", chords, 0));
    layout.add(
        std::make_unique<juce::AudioParameterChoice>(ids::mode, "Mode", juce::StringArray{"Pad", "Stab", "Keys"}, 0));
    layout.add(
        std::make_unique<juce::AudioParameterChoice>(ids::voices, "Voices", juce::StringArray{"3", "4", "5", "6"}, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(ids::octave, "Octave", -dsp::kMaxOctaveShift,
                                                         dsp::kMaxOctaveShift, 0));
    layout.add(unitParameter(ids::glide, "Glide", 0.0f, dsp::kMaxGlideMs, 300.0f, 350.0f, "ms",
                             [](float ms) { return juce::String(juce::roundToInt(ms)) + " ms"; }));
    layout.add(percentParameter(ids::curve, "Curve", 0.5f));
    layout.add(percentParameter(ids::tone, "Tone", 0.55f));
    layout.add(percentParameter(ids::width, "Width", 0.35f));
    layout.add(percentParameter(ids::space, "Space", 0.25f));
    layout.add(percentParameter(ids::level, "Level", 0.7f));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::latch, "Latch", false));
    return layout;
}
} // namespace

SwanProcessor::SwanProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "SwanState", createParameterLayout())
{
    chordParam = apvts.getRawParameterValue(ids::chord.getParamID());
    modeParam = apvts.getRawParameterValue(ids::mode.getParamID());
    voicesParam = apvts.getRawParameterValue(ids::voices.getParamID());
    octaveParam = apvts.getRawParameterValue(ids::octave.getParamID());
    glideParam = apvts.getRawParameterValue(ids::glide.getParamID());
    curveParam = apvts.getRawParameterValue(ids::curve.getParamID());
    toneParam = apvts.getRawParameterValue(ids::tone.getParamID());
    widthParam = apvts.getRawParameterValue(ids::width.getParamID());
    spaceParam = apvts.getRawParameterValue(ids::space.getParamID());
    levelParam = apvts.getRawParameterValue(ids::level.getParamID());
    latchParam = apvts.getRawParameterValue(ids::latch.getParamID());

    pushSettings();
    engine.prepare(44100.0);
}

bool SwanProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

int SwanProcessor::currentShape() const noexcept
{
    return std::clamp(juce::roundToInt(chordParam->load()), 0, dsp::kNumChordShapes - 1);
}

void SwanProcessor::pushSettings() noexcept
{
    dsp::EngineSettings s;
    s.mode = static_cast<dsp::Mode>(std::clamp(juce::roundToInt(modeParam->load()), 0, dsp::kNumModes - 1));
    s.voices =
        dsp::kMinVoices + std::clamp(juce::roundToInt(voicesParam->load()), 0, dsp::kMaxVoices - dsp::kMinVoices);
    s.centre = dsp::kDefaultCentre + 12 * juce::roundToInt(octaveParam->load());
    s.glideMs = glideParam->load();
    s.curve = curveParam->load();
    s.tone = toneParam->load();
    s.width = widthParam->load();
    s.space = spaceParam->load();
    s.level = levelParam->load();
    engine.setSettings(s);
}

void SwanProcessor::prepareToPlay(double sampleRate, int)
{
    heldCount = 0;
    lastShape = currentShape();
    lastLatch = latchParam->load() > 0.5f;
    pushSettings();
    engine.prepare(sampleRate);
}

void SwanProcessor::press(int source, int root) noexcept
{
    release(source);
    if (heldCount == static_cast<int>(held.size()))
    {
        std::move(held.begin() + 1, held.end(), held.begin());
        --heldCount;
    }
    held[static_cast<std::size_t>(heldCount++)] = {source, root};
    engine.playChord(root, currentShape());
}

void SwanProcessor::release(int source) noexcept
{
    const auto end = held.begin() + heldCount;
    const auto it = std::find_if(held.begin(), end, [source](const Held& h) { return h.source == source; });
    if (it == end)
        return;
    const bool wasNewest = it == end - 1;
    std::move(it + 1, end, it);
    --heldCount;
    if (wasNewest)
        afterRelease();
}

void SwanProcessor::releaseKeys() noexcept
{
    const auto end = held.begin() + heldCount;
    const bool newestWasKey = heldCount > 0 && held[static_cast<std::size_t>(heldCount - 1)].source >= kKeySourceBase;
    heldCount = static_cast<int>(
        std::remove_if(held.begin(), end, [](const Held& h) { return h.source >= kKeySourceBase; }) - held.begin());
    if (newestWasKey)
        afterRelease();
}

void SwanProcessor::afterRelease() noexcept
{
    if (heldCount > 0)
        engine.playChord(held[static_cast<std::size_t>(heldCount - 1)].root, currentShape());
    else if (latchParam->load() < 0.5f)
        engine.release();
}

void SwanProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        press(message.getNoteNumber(), message.getNoteNumber() % 12);
    else if (message.isNoteOff())
        release(message.getNoteNumber());
    else if (message.isAllSoundOff())
    {
        heldCount = 0;
        engine.kill();
    }
    else if (message.isAllNotesOff())
    {
        heldCount = 0;
        engine.release();
    }
}

void SwanProcessor::drainUiEvents() noexcept
{
    const auto scope = uiFifo.read(uiFifo.getNumReady());
    const auto handle = [this](int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& event = uiEvents[static_cast<std::size_t>(i)];
            switch (event.type)
            {
            case UiEvent::Type::Press:
                press(event.source, event.root);
                break;
            case UiEvent::Type::Release:
                release(event.source);
                break;
            case UiEvent::Type::ReleaseAllKeys:
                releaseKeys();
                break;
            }
        }
    };
    handle(scope.startIndex1, scope.blockSize1);
    handle(scope.startIndex2, scope.blockSize2);
}

void SwanProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int channels = std::min(buffer.getNumChannels(), dsp::SwanEngine::kMaxChannels);
    if (channels == 0)
        return;
    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);

    pushSettings();
    drainUiEvents();

    // A new chord quality re-voices what is sounding; switching LATCH off lets a latched chord go.
    const int shape = currentShape();
    if (shape != lastShape)
    {
        lastShape = shape;
        if (engine.isGateOn() && engine.getRoot() >= 0)
            engine.playChord(engine.getRoot(), shape);
    }
    const bool latch = latchParam->load() > 0.5f;
    if (lastLatch && !latch && heldCount == 0)
        engine.release();
    lastLatch = latch;

    float* pointers[dsp::SwanEngine::kMaxChannels] = {};
    int done = 0;
    const auto renderTo = [&](int end)
    {
        if (end <= done)
            return;
        for (int channel = 0; channel < channels; ++channel)
            pointers[channel] = buffer.getWritePointer(channel, done);
        engine.process(pointers, channels, end - done);
        done = end;
    };

    for (const auto metadata : midi)
    {
        renderTo(std::clamp(metadata.samplePosition, 0, numSamples));
        handleMidiMessage(metadata.getMessage());
    }
    renderTo(numSamples);
}

void SwanProcessor::postUiEvent(const UiEvent& event)
{
    const auto scope = uiFifo.write(1);
    if (scope.blockSize1 > 0)
        uiEvents[static_cast<std::size_t>(scope.startIndex1)] = event;
    else if (scope.blockSize2 > 0)
        uiEvents[static_cast<std::size_t>(scope.startIndex2)] = event;
}

void SwanProcessor::pressRoot(int source, int rootPitchClass)
{
    postUiEvent({UiEvent::Type::Press, source, dsp::pitchClassOf(rootPitchClass)});
}

void SwanProcessor::releaseRoot(int source)
{
    postUiEvent({UiEvent::Type::Release, source, 0});
}

void SwanProcessor::releaseAllKeys()
{
    postUiEvent({UiEvent::Type::ReleaseAllKeys, 0, 0});
}

void SwanProcessor::setParameterFromUi(const juce::ParameterID& id, float realValue)
{
    if (auto* param = apvts.getParameter(id.getParamID()))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(realValue));
        param->endChangeGesture();
    }
}

float SwanProcessor::getParameterValue(const juce::ParameterID& id) const
{
    return apvts.getRawParameterValue(id.getParamID())->load();
}

void SwanProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SwanProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* SwanProcessor::createEditor()
{
    return new SwanEditor(*this);
}
} // namespace swan

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new swan::SwanProcessor();
}
