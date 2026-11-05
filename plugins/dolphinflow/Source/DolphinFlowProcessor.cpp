#include "DolphinFlowProcessor.h"

#include "DolphinFlowEditor.h"

#include <animalaudio/core/ParameterFactory.h>

#include <algorithm>

namespace dolphinflow
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

DolphinFlowProcessor::DolphinFlowProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "DolphinFlowState", createParameterLayout())
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
    midiOut.ensureSize(4096);
}

bool DolphinFlowProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() &&
           (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

int DolphinFlowProcessor::currentShape() const noexcept
{
    return std::clamp(juce::roundToInt(chordParam->load()), 0, dsp::kNumChordShapes - 1);
}

void DolphinFlowProcessor::pushSettings() noexcept
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

void DolphinFlowProcessor::prepareToPlay(double sampleRate, int)
{
    heldCount = 0;
    intent = {};
    outCount = 0;
    midiOut.ensureSize(4096);
    lastShape = currentShape();
    lastLatch = latchParam->load() > 0.5f;
    pushSettings();
    engine.prepare(sampleRate);
}

void DolphinFlowProcessor::press(int source, int root, int note, int velocity) noexcept
{
    release(source);
    if (heldCount == static_cast<int>(held.size()))
    {
        std::move(held.begin() + 1, held.end(), held.begin());
        --heldCount;
    }
    held[static_cast<std::size_t>(heldCount++)] = {source, root, note};
    lastVelocity = std::clamp(velocity, 1, 127);
}

void DolphinFlowProcessor::release(int source) noexcept
{
    const auto end = held.begin() + heldCount;
    const auto it = std::find_if(held.begin(), end, [source](const Held& h) { return h.source == source; });
    if (it == end)
        return;
    std::move(it + 1, end, it);
    --heldCount;
}

void DolphinFlowProcessor::releaseKeys() noexcept
{
    const auto end = held.begin() + heldCount;
    heldCount = static_cast<int>(
        std::remove_if(held.begin(), end, [](const Held& h) { return h.source >= kKeySourceBase; }) - held.begin());
}

DolphinFlowProcessor::Intent DolphinFlowProcessor::wantedIntent() const noexcept
{
    Intent next;
    if (heldCount == 0)
        return next;
    const auto& newest = held[static_cast<std::size_t>(heldCount - 1)];
    next.root = newest.root;
    next.shape = currentShape();
    next.kind = Intent::Kind::Chord;
    if (newest.note < 0)
        return next;

    // Three or more MIDI notes at once are a voiced chord (e.g. a clip recorded from our MIDI out): play it as is.
    // Two stay roots, so overlapping legato roots on a keyboard do not turn into dyads.
    int notes[kMaxHeld] = {};
    int count = 0;
    for (int i = 0; i < heldCount; ++i)
        if (held[static_cast<std::size_t>(i)].note >= 0)
            notes[count++] = held[static_cast<std::size_t>(i)].note;
    std::sort(notes, notes + count);
    count = static_cast<int>(std::unique(notes, notes + count) - notes);
    if (count < kMinVoicedNotes)
        return next;
    next.kind = Intent::Kind::Voicing;
    next.voicing.count = std::min(count, dsp::kMaxVoices);
    std::copy(notes, notes + next.voicing.count, next.voicing.notes);
    return next;
}

void DolphinFlowProcessor::resolve() noexcept
{
    const Intent next = wantedIntent();
    if (next == intent)
        return;
    intent = next;
    switch (intent.kind)
    {
    case Intent::Kind::None:
        if (latchParam->load() < 0.5f)
            engine.release();
        break;
    case Intent::Kind::Chord:
        engine.playChord(intent.root, intent.shape);
        break;
    case Intent::Kind::Voicing:
        engine.playVoicing(intent.voicing);
        break;
    }
}

void DolphinFlowProcessor::syncMidiOut(int samplePosition) noexcept
{
    int wanted[dsp::kMaxVoices] = {};
    int wantedCount = 0;
    if (engine.isHeld())
    {
        const auto& voicing = engine.getVoicing();
        for (int v = 0; v < voicing.count; ++v)
            if (std::find(wanted, wanted + wantedCount, voicing.notes[v]) == wanted + wantedCount)
                wanted[wantedCount++] = voicing.notes[v];
    }

    // Common tones keep sounding across a chord change, so a recorded clip shows them as one long note.
    int kept = 0;
    for (int i = 0; i < outCount; ++i)
    {
        const int note = outNotes[static_cast<std::size_t>(i)];
        if (std::find(wanted, wanted + wantedCount, note) == wanted + wantedCount)
            midiOut.addEvent(juce::MidiMessage::noteOff(kMidiChannel, note), samplePosition);
        else
            outNotes[static_cast<std::size_t>(kept++)] = note;
    }
    outCount = kept;
    for (int i = 0; i < wantedCount; ++i)
    {
        const auto begin = outNotes.begin();
        if (std::find(begin, begin + outCount, wanted[i]) != begin + outCount)
            continue;
        midiOut.addEvent(juce::MidiMessage::noteOn(kMidiChannel, wanted[i], static_cast<juce::uint8>(lastVelocity)),
                         samplePosition);
        outNotes[static_cast<std::size_t>(outCount++)] = wanted[i];
    }
}

void DolphinFlowProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        press(message.getNoteNumber(), message.getNoteNumber() % 12, message.getNoteNumber(), message.getVelocity());
    else if (message.isNoteOff())
        release(message.getNoteNumber());
    else if (message.isAllSoundOff())
    {
        heldCount = 0;
        intent = {};
        engine.kill();
    }
    else if (message.isAllNotesOff())
    {
        heldCount = 0;
        intent = {};
        engine.release();
    }
}

void DolphinFlowProcessor::drainUiEvents() noexcept
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
                press(event.source, event.root, -1, kKeyVelocity);
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

void DolphinFlowProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiOut.clear();
    const int numSamples = buffer.getNumSamples();
    const int channels = std::min(buffer.getNumChannels(), dsp::DolphinFlowEngine::kMaxChannels);
    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);
    if (channels == 0)
    {
        midi.clear();
        return;
    }

    pushSettings();
    drainUiEvents();
    resolve();

    // A new chord quality re-voices a latched chord too; switching LATCH off lets a latched chord go.
    const int shape = currentShape();
    if (shape != lastShape)
    {
        lastShape = shape;
        if (intent.kind == Intent::Kind::None && engine.isHeld() && engine.getRoot() >= 0)
            engine.playChord(engine.getRoot(), shape);
    }
    const bool latch = latchParam->load() > 0.5f;
    if (lastLatch && !latch && heldCount == 0)
        engine.release();
    lastLatch = latch;
    syncMidiOut(0);

    float* pointers[dsp::DolphinFlowEngine::kMaxChannels] = {};
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

    // Messages sharing a timestamp are applied together, so a chord from a clip lands as one chord.
    for (auto it = midi.cbegin(); it != midi.cend();)
    {
        const int position = (*it).samplePosition;
        const int at = std::clamp(position, 0, numSamples);
        renderTo(at);
        for (; it != midi.cend() && (*it).samplePosition == position; ++it)
            handleMidiMessage((*it).getMessage());
        resolve();
        syncMidiOut(std::min(at, std::max(0, numSamples - 1)));
    }
    renderTo(numSamples);
    midi.swapWith(midiOut);
}

void DolphinFlowProcessor::postUiEvent(const UiEvent& event)
{
    const auto scope = uiFifo.write(1);
    if (scope.blockSize1 > 0)
        uiEvents[static_cast<std::size_t>(scope.startIndex1)] = event;
    else if (scope.blockSize2 > 0)
        uiEvents[static_cast<std::size_t>(scope.startIndex2)] = event;
}

void DolphinFlowProcessor::pressRoot(int source, int rootPitchClass)
{
    postUiEvent({UiEvent::Type::Press, source, dsp::pitchClassOf(rootPitchClass)});
}

void DolphinFlowProcessor::releaseRoot(int source)
{
    postUiEvent({UiEvent::Type::Release, source, 0});
}

void DolphinFlowProcessor::releaseAllKeys()
{
    postUiEvent({UiEvent::Type::ReleaseAllKeys, 0, 0});
}

void DolphinFlowProcessor::setParameterFromUi(const juce::ParameterID& id, float realValue)
{
    if (auto* param = apvts.getParameter(id.getParamID()))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(realValue));
        param->endChangeGesture();
    }
}

float DolphinFlowProcessor::getParameterValue(const juce::ParameterID& id) const
{
    return apvts.getRawParameterValue(id.getParamID())->load();
}

void DolphinFlowProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DolphinFlowProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* DolphinFlowProcessor::createEditor()
{
    return new DolphinFlowEditor(*this);
}
} // namespace dolphinflow

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dolphinflow::DolphinFlowProcessor();
}
