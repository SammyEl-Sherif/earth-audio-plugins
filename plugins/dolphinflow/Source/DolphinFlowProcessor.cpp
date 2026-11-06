#include "DolphinFlowProcessor.h"

#include "DolphinFlowEditor.h"

#include <animalaudio/core/ParameterFactory.h>
#include <animalaudio/core/TransportReader.h>

#include <algorithm>

namespace dolphinflow
{
namespace
{
using animalaudio::core::percentParameter;
using animalaudio::core::unitParameter;

const juce::Identifier kProgression{"Progression"};
const juce::Identifier kSlot{"Slot"};
const juce::Identifier kRoot{"root"};
const juce::Identifier kShape{"shape"};
const juce::Identifier kLength{"length"};

/** First open: a deep house staple, Am7 - Fmaj7 - C - G, a bar each. */
dsp::Slot defaultSlot(int index)
{
    static constexpr int roots[] = {9, 5, 0, 7};
    static constexpr int shapes[] = {4, 3, 0, 0};
    dsp::Slot slot;
    if (index < 4)
    {
        slot.root = roots[index];
        slot.shape = shapes[index];
    }
    return slot;
}

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
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::loop, "Loop", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ids::mpe, "MPE Out", true));
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
    loopParam = apvts.getRawParameterValue(ids::loop.getParamID());
    mpeParam = apvts.getRawParameterValue(ids::mpe.getParamID());
    for (int i = 0; i < dsp::kNumSlots; ++i)
        slotState[static_cast<std::size_t>(i)].store(defaultSlot(i).pack());
    loadSlotsFromState();

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
    s.centre = dsp::kDefaultCentre + 12 * std::clamp(juce::roundToInt(octaveParam->load()) + playedOctave,
                                                     -kMaxTotalOctaves, kMaxTotalOctaves);
    s.glideMs = glideParam->load();
    s.curve = curveParam->load();
    s.tone = toneParam->load();
    s.width = widthParam->load();
    s.space = spaceParam->load();
    s.level = levelParam->load();
    settledVoices = s.voices;
    settledCentre = s.centre;
    engine.setSettings(s);
}

void DolphinFlowProcessor::prepareToPlay(double sampleRate, int)
{
    heldCount = 0;
    intent = {};
    playedOctave = 0;
    noteOut.reset();
    sent.fill({});
    lastPlaying = false;
    lastRecording = false;
    sequencing = false;
    sequenceSlot = -1;
    playingSlot.store(-1);
    midiOut.ensureSize(4096);
    lastShape = currentShape();
    lastLatch = latchParam->load() > 0.5f;
    pushSettings();
    engine.prepare(sampleRate);
    if (wrapperType != wrapperType_Undefined)
        mpePort.open(); // a host loaded us; tests open it themselves (openMpePort), so they make no stray ports
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
    next.octave = newest.note / 12 - kReferenceOctave;

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
    {
        // The octave of the played root moves the register (Live's Z / X on its computer keyboard, or simply
        // playing higher), on top of OCTAVE. Editor keys play at OCTAVE alone.
        if (intent.octave != playedOctave)
        {
            playedOctave = intent.octave;
            pushSettings();
        }
        engine.playChord(intent.root, intent.shape);
        break;
    }
    case Intent::Kind::Voicing:
        engine.playVoicing(intent.voicing);
        break;
    }
}

void DolphinFlowProcessor::syncMidiOut(int samplePosition) noexcept
{
    noteOut.sync(engine, lastVelocity, midiOut, samplePosition);
}

bool DolphinFlowProcessor::isOwnEcho(const juce::MidiMessage& message) noexcept
{
    // Our MPE port feeds every armed track listening to All Ins, this one included. A note-on matching one we sent
    // in the last second is that echo: it would turn the next root into an added note instead of a glide.
    if (message.getChannel() < NoteOut::kFirstMemberChannel)
        return false; // plain notes and keyboards on channel 1 are always played
    const auto window = static_cast<juce::int64>(getSampleRate());
    for (auto& s : sent)
        if (s.note == message.getNoteNumber() && s.channel == message.getChannel() && sampleClock - s.at <= window)
        {
            s.note = -1; // each sent note echoes once
            return true;
        }
    return false;
}

void DolphinFlowProcessor::rememberSent(const juce::MidiBuffer& buffer) noexcept
{
    for (const auto metadata : buffer)
    {
        const auto message = metadata.getMessage();
        if (!message.isNoteOn() || message.getChannel() < NoteOut::kFirstMemberChannel)
            continue;
        sent[static_cast<std::size_t>(sentHead)] = {message.getChannel(), message.getNoteNumber(),
                                                    sampleClock + metadata.samplePosition};
        sentHead = (sentHead + 1) % kMaxSent;
    }
}

void DolphinFlowProcessor::handleMidiMessage(const juce::MidiMessage& message) noexcept
{
    // A MIDI source is its channel and note, so an MPE keyboard's notes stay apart.
    const int source = (message.getChannel() - 1) * 128 + message.getNoteNumber();
    if (message.isNoteOn())
    {
        if (!isOwnEcho(message))
            press(source, message.getNoteNumber() % 12, message.getNoteNumber(), message.getVelocity());
    }
    else if (message.isNoteOff())
        release(source); // an echo's note-off finds nothing held
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

void DolphinFlowProcessor::stepSequencer(const dsp::Slots& slots, double& samplesToBoundary,
                                         double beatsPerSample) noexcept
{
    const auto at = dsp::locate(slots, sequenceBeats);
    if (at.slot < 0)
        return;
    const auto& slot = slots[at.slot];
    // A new square, an edit to the playing one, or the loop coming round on a single square: play it.
    const bool changed = at.slot != sequenceSlot || slot.root != sequenceChord.root ||
                         slot.shape != sequenceChord.shape || at.into + 1.0e-9 < sequenceInto;
    if (changed)
    {
        if (sequenceSlot < 0)
            engine.playChordVoiced(dsp::settledVoicing(slots, at.slot, settledVoices, settledCentre), slot.root,
                                   slot.shape);
        else
            engine.playChord(slot.root, slot.shape);
        sequenceSlot = at.slot;
        sequenceChord = slot;
    }
    sequenceInto = at.into;
    samplesToBoundary = std::max(1.0, std::ceil(at.left / beatsPerSample - 1.0e-6));
    slotProgress.store(static_cast<float>(at.into / slot.beats()), std::memory_order_relaxed);
}

void DolphinFlowProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const double blockStartMs = juce::Time::getMillisecondCounterHiRes();
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

    // The arrangement loop: locked to the host's beat position while it plays, else free-running from the start.
    dsp::Slot slots[dsp::kNumSlots];
    for (int i = 0; i < dsp::kNumSlots; ++i)
        slots[i] = dsp::Slot::unpack(slotState[static_cast<std::size_t>(i)].load(std::memory_order_relaxed));
    const auto transport = animalaudio::core::readTransport(getPlayHead());
    const double bpm = animalaudio::dsp::bpmOr(transport, 120.0);
    tempo.store(bpm, std::memory_order_relaxed);
    const double beatsPerSample = bpm / 60.0 / std::max(1.0, getSampleRate());
    const bool hostClock = transport.playing && transport.hasPpq;
    const bool runLoop = loopParam->load() > 0.5f && dsp::loopBeats(slots) > 0.0;
    if (runLoop && !sequencing)
    {
        sequencing = true;
        sequenceSlot = -1;
        sequenceInto = 0.0;
        sequenceBeats = 0.0;
    }
    else if (!runLoop && sequencing)
    {
        sequencing = false;
        sequenceSlot = -1;
        intent = {};
        engine.release();
    }
    if (sequencing && hostClock)
        sequenceBeats = transport.ppq;
    double toBoundary = 0.0;
    if (sequencing)
        stepSequencer(slots, toBoundary, beatsPerSample); // the square at the block start, before any note goes out

    drainUiEvents();
    if (!sequencing)
        resolve();

    // A new chord quality re-voices a latched chord too; switching LATCH off lets a latched chord go.
    const int shape = currentShape();
    if (shape != lastShape)
    {
        lastShape = shape;
        if (!sequencing && intent.kind == Intent::Kind::None && engine.isHeld() && engine.getRoot() >= 0)
            engine.playChord(engine.getRoot(), shape);
    }
    const bool latch = latchParam->load() > 0.5f;
    if (!sequencing && lastLatch && !latch && heldCount == 0)
        engine.release();
    lastLatch = latch;

    // Starting play or record sends the sounding notes again, so the clip being recorded holds them from its start.
    const bool started = (transport.playing && !lastPlaying) || (transport.recording && !lastRecording);
    lastPlaying = transport.playing;
    lastRecording = transport.recording;
    if (started)
        noteOut.releaseAll(midiOut, 0);
    noteOut.setMpe(mpeParam->load() > 0.5f, midiOut, 0);
    syncMidiOut(0);

    float* pointers[dsp::DolphinFlowEngine::kMaxChannels] = {};
    int done = 0;
    auto it = midi.cbegin();
    const auto applyMidiAt = [&](int at)
    {
        // Messages sharing a timestamp are applied together, so a chord from a clip lands as one chord.
        while (it != midi.cend() && std::clamp((*it).samplePosition, 0, numSamples) <= at)
        {
            const int position = (*it).samplePosition;
            for (; it != midi.cend() && (*it).samplePosition == position; ++it)
                handleMidiMessage((*it).getMessage());
            if (!sequencing)
                resolve(); // while the loop plays, keys still track but the loop owns the sound
            syncMidiOut(std::min(at, std::max(0, numSamples - 1)));
        }
    };

    while (done < numSamples)
    {
        applyMidiAt(done);
        int next = numSamples;
        if (sequencing)
        {
            stepSequencer(slots, toBoundary, beatsPerSample);
            syncMidiOut(done);
            next = static_cast<int>(std::min<double>(next, done + toBoundary));
        }
        // While a glide runs, its MPE pitch bends go out every few samples, read from the engine as it slides.
        noteOut.updateBends(engine, midiOut, done);
        if (noteOut.isGliding(engine))
            next = std::min(next, done + NoteOut::kBendInterval);
        if (it != midi.cend())
            next = std::min(next, std::clamp((*it).samplePosition, 0, numSamples));
        next = std::max(next, done + 1);
        for (int channel = 0; channel < channels; ++channel)
            pointers[channel] = buffer.getWritePointer(channel, done);
        engine.process(pointers, channels, next - done);
        if (sequencing)
            sequenceBeats += (next - done) * beatsPerSample;
        done = next;
    }
    applyMidiAt(numSamples);
    playingSlot.store(sequencing ? sequenceSlot : -1, std::memory_order_relaxed);
    if (noteOut.isMpe())
    {
        mpePort.queue(midiOut, blockStartMs, getSampleRate()); // MPE only: plain channel 1 notes have no glides
        rememberSent(midiOut);
    }
    sampleClock += numSamples;
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

dsp::Slot DolphinFlowProcessor::getSlot(int index) const noexcept
{
    if (index < 0 || index >= dsp::kNumSlots)
        return {};
    return dsp::Slot::unpack(slotState[static_cast<std::size_t>(index)].load(std::memory_order_relaxed));
}

void DolphinFlowProcessor::setSlot(int index, const dsp::Slot& slot)
{
    if (index < 0 || index >= dsp::kNumSlots)
        return;
    slotState[static_cast<std::size_t>(index)].store(slot.pack(), std::memory_order_relaxed);
    auto progression = apvts.state.getOrCreateChildWithName(kProgression, nullptr);
    while (progression.getNumChildren() < dsp::kNumSlots)
        progression.appendChild(juce::ValueTree(kSlot), nullptr);
    auto node = progression.getChild(index);
    node.setProperty(kRoot, slot.root, nullptr);
    node.setProperty(kShape, slot.shape, nullptr);
    node.setProperty(kLength, slot.lengthIndex, nullptr);
}

void DolphinFlowProcessor::loadSlotsFromState()
{
    const auto progression = apvts.state.getChildWithName(kProgression);
    for (int i = 0; i < dsp::kNumSlots; ++i)
    {
        dsp::Slot slot = getSlot(i);
        if (progression.isValid() && i < progression.getNumChildren())
        {
            const auto node = progression.getChild(i);
            slot.root = std::clamp(static_cast<int>(node.getProperty(kRoot, -1)), -1, 11);
            slot.shape = std::clamp(static_cast<int>(node.getProperty(kShape, 0)), 0, dsp::kNumChordShapes - 1);
            slot.lengthIndex = std::clamp(static_cast<int>(node.getProperty(kLength, dsp::kDefaultLengthIndex)), 0,
                                          dsp::kNumSlotLengths - 1);
        }
        setSlot(i, slot); // also writes the tree when the saved state had none
    }
}

bool DolphinFlowProcessor::writeMidiClip(const juce::File& file) const
{
    dsp::Slot slots[dsp::kNumSlots];
    for (int i = 0; i < dsp::kNumSlots; ++i)
        slots[i] = getSlot(i);
    const double total = dsp::loopBeats(slots);
    if (total <= 0.0)
        return false;

    const int voices =
        dsp::kMinVoices + std::clamp(juce::roundToInt(voicesParam->load()), 0, dsp::kMaxVoices - dsp::kMinVoices);
    const int centre = dsp::kDefaultCentre + 12 * juce::roundToInt(octaveParam->load());
    constexpr int ticksPerBeat = 960;
    const auto ticks = [](double beats) { return std::round(beats * ticksPerBeat); };

    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::textMetaEvent(3, "Dolphin Flow"), 0.0);
    track.addEvent(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60.0e6 / getTempo())), 0.0);
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    for (const auto& note : dsp::buildClip(slots, voices, centre))
    {
        track.addEvent(
            juce::MidiMessage::noteOn(NoteOut::kPlainChannel, note.note, static_cast<juce::uint8>(kKeyVelocity)),
            ticks(note.start));
        track.addEvent(juce::MidiMessage::noteOff(NoteOut::kPlainChannel, note.note), ticks(note.end));
    }
    track.addEvent(juce::MidiMessage::endOfTrack(), ticks(total));
    track.sort();
    track.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerBeat);
    midiFile.addTrack(track);
    if (!file.getParentDirectory().createDirectory())
        return false;
    file.deleteFile();
    juce::FileOutputStream out(file);
    return out.openedOk() && midiFile.writeTo(out, 1);
}

juce::File DolphinFlowProcessor::defaultClipFile() const
{
    juce::StringArray names;
    for (int i = 0; i < dsp::kNumSlots; ++i)
        if (const auto slot = getSlot(i); !slot.isEmpty())
            names.add(juce::String(dsp::pitchClassName(slot.root)) + dsp::chordShape(slot.shape).symbol);
    const auto name = juce::File::createLegalFileName("Dolphin Flow - " + names.joinIntoString(" ") + ".mid");
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Animal Audio")
        .getChildFile("Dolphin Flow")
        .getChildFile(name);
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
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            loadSlotsFromState();
        }
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
