#include "DolphinFlowProcessor.h"

#include "DolphinFlowEditor.h"

#include <animalaudio/core/ParameterFactory.h>
#include <animalaudio/core/TransportReader.h>

#include <algorithm>
#include <cmath>

namespace dolphinflow
{
namespace
{
using animalaudio::core::percentParameter;
using animalaudio::core::unitParameter;

const juce::Identifier kPaletteShift{"paletteShift"};
const juce::Identifier kPaletteScale{"paletteScale"};
const juce::Identifier kSongNode{"Song"};
const juce::Identifier kLegacyProgression{"Progression"};
const juce::Identifier kLink{"link"};
constexpr std::size_t kUndoDepth = 100;
constexpr double kPreviewSeconds = 0.35;

/** The voicing chord `target` has once the song has gone round once: so the first pass sounds like every later one
 * (and like the exported clip). Allocation free. */
dsp::Voicing settledVoicingAt(const dsp::PlaySong& play, int target, int voices, int centre) noexcept
{
    const auto& chords = play.chords;
    if (chords.empty() || target < 0 || target >= static_cast<int>(chords.size()))
        return {};
    auto v = dsp::VoiceLeader::initial(chords.front().root, chords.front().shape, voices, centre);
    for (std::size_t i = 1; i < chords.size(); ++i)
        v = dsp::VoiceLeader::lead(v, chords[i].root, chords[i].shape, voices, centre);
    for (int i = 0; i <= target; ++i)
        v = dsp::VoiceLeader::lead(v, chords[static_cast<std::size_t>(i)].root,
                                   chords[static_cast<std::size_t>(i)].shape, voices, centre);
    return v;
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
    juce::StringArray keys;
    for (int pc = 0; pc < 12; ++pc)
        keys.add(dsp::pitchClassName(pc));
    juce::StringArray scales;
    for (const auto& scale : dsp::kScales)
        scales.add(scale.name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::key, "Key", keys, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ids::scale, "Scale", scales, 0));
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
    loadSongFromState();

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
    lastPlaying = false;
    lastRecording = false;
    sequencing = false;
    sequenceChord = -1;
    soundingCount = 0;
    previewCount = 0;
    melodyPosition = -1.0;
    playingChord.store(-1);
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

bool DolphinFlowProcessor::isOwnEcho(const juce::MidiMessage& message) const noexcept
{
    // Our MPE port feeds every armed track listening to All Ins, this one included (on channel 1, as Live gives it to
    // us). Played again, each echo would add notes that echo again, until the notes pile up and never stop.
    return EchoGuard::isEcho(message.getNoteNumber(), message.getVelocity(), blockMs);
}

void DolphinFlowProcessor::rememberSent(const juce::MidiBuffer& buffer) noexcept
{
    const double msPerSample = 1000.0 / std::max(1.0, getSampleRate());
    for (const auto metadata : buffer)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            EchoGuard::remember(message.getNoteNumber(), MpePort::portVelocity(message.getVelocity()),
                                blockMs + static_cast<std::uint32_t>(metadata.samplePosition * msPerSample));
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
            case UiEvent::Type::Rewind:
                sequenceBeats = seekTarget.load();
                sequenceChord = -1; // the first chord starts on its settled voicing
                melodyPosition = -1.0;
                break;
            case UiEvent::Type::Preview:
                if (previewCount < kMaxPreviews)
                {
                    midiOut.addEvent(
                        juce::MidiMessage::noteOn(event.source, event.root,
                                                  static_cast<juce::uint8>(std::clamp(event.velocity, 1, 127))),
                        0);
                    previews[static_cast<std::size_t>(previewCount++)] = {
                        event.source, event.root, static_cast<int>(kPreviewSeconds * getSampleRate())};
                }
                break;
            }
        }
    };
    handle(scope.startIndex1, scope.blockSize1);
    handle(scope.startIndex2, scope.blockSize2);
}

double DolphinFlowProcessor::songPosition(const dsp::PlaySong& play) const noexcept
{
    if (loopParam->load() > 0.5f && play.length > 0.0)
    {
        const double wrapped = std::fmod(sequenceBeats, play.length);
        return wrapped < 0.0 ? wrapped + play.length : wrapped;
    }
    return std::max(0.0, sequenceBeats);
}

void DolphinFlowProcessor::stepSong(const dsp::PlaySong& play, double& samplesToBoundary,
                                    double beatsPerSample) noexcept
{
    const bool loop = loopParam->load() > 0.5f;
    const double position = songPosition(play);
    const int index = position < play.chordLength ? play.chordAt(position) : -1;
    double left = 0.0;
    if (index < 0)
    {
        // Past the chords (a melody running on) or past the end: the chord track is silent.
        if (sequenceChord >= 0)
            engine.release();
        sequenceChord = -1;
        sequenceRoot = -1;
        left = position < play.length ? play.length - position : 1.0e6;
    }
    else
    {
        const auto& chord = play.chords[static_cast<std::size_t>(index)];
        // A new chord, an edit to the playing one, or the loop coming round on a single chord: play it.
        const bool changed = index != sequenceChord || chord.root != sequenceRoot || chord.shape != sequenceShape ||
                             position + 1.0e-9 < sequenceInto;
        if (changed)
        {
            lastVelocity = play.chordVelocity;
            if (!play.chordsAudible)
                engine.release();
            else if (sequenceChord < 0)
                engine.playChordVoiced(settledVoicingAt(play, index, settledVoices, settledCentre), chord.root,
                                       chord.shape);
            else
                engine.playChord(chord.root, chord.shape);
            sequenceChord = index;
            sequenceRoot = chord.root;
            sequenceShape = chord.shape;
        }
        left = chord.end() - position;
    }
    if (loop)
        left = std::min(left, play.length - position);
    sequenceInto = position;
    samplesToBoundary = std::max(1.0, std::ceil(left / beatsPerSample - 1.0e-6));
}

void DolphinFlowProcessor::melodyOff(int samplePosition) noexcept
{
    for (int i = 0; i < soundingCount; ++i)
        midiOut.addEvent(juce::MidiMessage::noteOff(sounding[static_cast<std::size_t>(i)].channel,
                                                    sounding[static_cast<std::size_t>(i)].pitch),
                         samplePosition);
    soundingCount = 0;
}

void DolphinFlowProcessor::emitMelody(const dsp::PlaySong& play, double from, double to, int sampleStart,
                                      double beatsPerSample, int numSamples) noexcept
{
    const auto& notes = play.notes;
    const bool loop = loopParam->load() > 0.5f;
    if (melodyPosition < 0.0 || std::abs(from - melodyPosition) > 1.0e-6)
    {
        // A jump (start, rewind, the host moving): end what sounds and find the first note from here.
        melodyOff(sampleStart);
        melodyCursor = static_cast<std::size_t>(std::lower_bound(notes.begin(), notes.end(), from - 1.0e-9,
                                                                 [](const dsp::PlaySong::Note& n, double beat)
                                                                 { return n.start < beat; }) -
                                                notes.begin());
    }
    const auto sampleAt = [&](double beat)
    {
        return std::clamp(sampleStart + static_cast<int>(std::lround((beat - from) / beatsPerSample)), 0,
                          std::max(0, numSamples - 1));
    };

    for (; melodyCursor < notes.size() && notes[melodyCursor].start < to - 1.0e-9; ++melodyCursor)
    {
        const auto& n = notes[melodyCursor];
        const int at = sampleAt(n.start);
        for (int i = 0; i < soundingCount; ++i)
            if (sounding[static_cast<std::size_t>(i)].channel == n.channel &&
                sounding[static_cast<std::size_t>(i)].pitch == n.pitch)
            {
                // Same pitch again on the same track: end the old one first so nothing hangs.
                midiOut.addEvent(juce::MidiMessage::noteOff(n.channel, n.pitch), at);
                sounding[static_cast<std::size_t>(i)] = sounding[static_cast<std::size_t>(--soundingCount)];
                break;
            }
        if (soundingCount == kMaxSounding)
            continue;
        midiOut.addEvent(juce::MidiMessage::noteOn(n.channel, n.pitch, static_cast<juce::uint8>(n.velocity)), at);
        sounding[static_cast<std::size_t>(soundingCount++)] = {n.channel, n.pitch,
                                                               loop ? std::min(n.end, play.length) : n.end};
    }
    // A note ending exactly at `to` ends at the start of the next stretch (the next block's first sample).
    for (int i = 0; i < soundingCount;)
    {
        auto& s = sounding[static_cast<std::size_t>(i)];
        if (s.end < to - 1.0e-9)
        {
            midiOut.addEvent(juce::MidiMessage::noteOff(s.channel, s.pitch), sampleAt(s.end));
            s = sounding[static_cast<std::size_t>(--soundingCount)];
        }
        else
        {
            ++i;
        }
    }
    melodyPosition = to;
    if (loop && to >= play.length - 1.0e-9)
    {
        // Round again: what still sounds ends as the loop restarts.
        for (int i = 0; i < soundingCount; ++i)
            sounding[static_cast<std::size_t>(i)].end -= play.length;
        melodyPosition = 0.0;
        melodyCursor = 0;
    }
}

void DolphinFlowProcessor::stopSequencing() noexcept
{
    sequencing = false;
    sequenceChord = -1;
    intent = {};
    engine.release();
    melodyOff(0);
    melodyPosition = -1.0;
}

void DolphinFlowProcessor::sendPreviews(int numSamples) noexcept
{
    for (int i = 0; i < previewCount;)
    {
        auto& p = previews[static_cast<std::size_t>(i)];
        if (p.samplesLeft < numSamples)
        {
            midiOut.addEvent(juce::MidiMessage::noteOff(p.channel, p.pitch), std::max(0, p.samplesLeft));
            p = previews[static_cast<std::size_t>(--previewCount)];
        }
        else
        {
            p.samplesLeft -= numSamples;
            ++i;
        }
    }
}

void DolphinFlowProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const double blockStartMs = juce::Time::getMillisecondCounterHiRes();
    blockMs = static_cast<std::uint32_t>(static_cast<juce::int64>(blockStartMs));
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

    // The song: locked to the host's beat position while the host plays, else running from the playhead.
    const auto& play = songStore.acquire();
    const auto transport = animalaudio::core::readTransport(getPlayHead());
    const double bpm = animalaudio::dsp::bpmOr(transport, 120.0);
    tempo.store(bpm, std::memory_order_relaxed);
    const double beatsPerSample = bpm / 60.0 / std::max(1.0, getSampleRate());
    const bool hostClock = transport.playing && transport.hasPpq;
    const bool loop = loopParam->load() > 0.5f;
    sendPreviews(numSamples);
    drainUiEvents();
    const bool runSong = play.length > 0.0 && (transportPlaying.load() || loop || (linked.load() && hostClock));
    if (runSong && !sequencing)
    {
        sequencing = true;
        sequenceChord = -1;
        sequenceInto = 0.0;
        melodyPosition = -1.0;
        if (!transportPlaying.load())
            sequenceBeats = 0.0; // LOOP (or the host) starts the song from its top
    }
    else if (!runSong && sequencing)
    {
        stopSequencing();
    }
    if (sequencing && hostClock)
        sequenceBeats = transport.ppq;
    if (sequencing && !loop && !hostClock && sequenceBeats >= play.length)
    {
        // PLAY ran to the end of the song: stop, back at the top.
        transportPlaying.store(false);
        stopSequencing();
        sequenceBeats = 0.0;
    }
    double toBoundary = 0.0;
    if (sequencing)
        stepSong(play, toBoundary, beatsPerSample); // the chord at the block start, before any note goes out
    else
        melodyOff(0);
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
            stepSong(play, toBoundary, beatsPerSample);
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
        {
            const double from = songPosition(play);
            emitMelody(play, from, from + (next - done) * beatsPerSample, done, beatsPerSample, numSamples);
            sequenceBeats += (next - done) * beatsPerSample;
        }
        done = next;
    }
    applyMidiAt(numSamples);
    playingChord.store(sequencing ? sequenceChord : -1, std::memory_order_relaxed);
    playhead.store(songPosition(play), std::memory_order_relaxed);
    sequencingShown.store(sequencing, std::memory_order_relaxed);
    if (noteOut.isMpe())
    {
        mpePort.queue(midiOut, blockStartMs, getSampleRate()); // MPE only: plain channel 1 notes have no glides
        rememberSent(midiOut);
    }
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

void DolphinFlowProcessor::publishSong()
{
    auto node = apvts.state.getChildWithName(kSongNode);
    if (node.isValid())
        apvts.state.removeChild(node, nullptr);
    if (auto legacy = apvts.state.getChildWithName(kLegacyProgression); legacy.isValid())
        apvts.state.removeChild(legacy, nullptr);
    apvts.state.appendChild(songToTree(song), nullptr);
    songStore.publish(dsp::compileSong(song));
}

void DolphinFlowProcessor::loadSongFromState()
{
    song = songFromState(apvts.state);
    undoStack.clear();
    redoStack.clear();
    linked.store(static_cast<bool>(apvts.state.getProperty(kLink, false)));
    publishSong();
}

void DolphinFlowProcessor::setSong(const dsp::Song& newSong, bool undoable)
{
    if (undoable)
    {
        undoStack.push_back(song);
        if (undoStack.size() > kUndoDepth)
            undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    song = newSong;
    sanitise(song);
    publishSong();
}

bool DolphinFlowProcessor::undo()
{
    if (undoStack.empty())
        return false;
    redoStack.push_back(song);
    song = undoStack.back();
    undoStack.pop_back();
    publishSong();
    return true;
}

bool DolphinFlowProcessor::redo()
{
    if (redoStack.empty())
        return false;
    undoStack.push_back(song);
    song = redoStack.back();
    redoStack.pop_back();
    publishSong();
    return true;
}

void DolphinFlowProcessor::setPlaying(bool shouldPlay)
{
    transportPlaying.store(shouldPlay);
}

void DolphinFlowProcessor::seek(double beats)
{
    seekTarget.store(std::max(0.0, beats));
    playhead.store(std::max(0.0, beats));
    postUiEvent({UiEvent::Type::Rewind, 0, 0});
}

void DolphinFlowProcessor::pushUndo(const dsp::Song& before)
{
    undoStack.push_back(before);
    if (undoStack.size() > kUndoDepth)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void DolphinFlowProcessor::setLinked(bool shouldLink)
{
    linked.store(shouldLink);
    apvts.state.setProperty(kLink, shouldLink, nullptr);
}

void DolphinFlowProcessor::previewNote(int track, int pitch, int velocity)
{
    postUiEvent({UiEvent::Type::Preview, dsp::kFirstTrackChannel + std::clamp(track, 0, dsp::kMaxMelodyTracks - 1),
                 std::clamp(pitch, 0, 127), velocity});
}

ClipVoicing DolphinFlowProcessor::clipVoicing() const
{
    ClipVoicing v;
    v.voices =
        dsp::kMinVoices + std::clamp(juce::roundToInt(voicesParam->load()), 0, dsp::kMaxVoices - dsp::kMinVoices);
    v.centre = dsp::kDefaultCentre + 12 * juce::roundToInt(octaveParam->load());
    v.mpe = mpeParam->load() > 0.5f;
    v.bpm = getTempo();
    v.glideMs = glideParam->load();
    v.curve = curveParam->load();
    return v;
}

bool DolphinFlowProcessor::writeMidiClip(const juce::File& file, const ClipScope& scope) const
{
    const auto midiFile = buildClip(song, scope, clipVoicing());
    if (midiFile.getNumTracks() == 0)
        return false;
    if (!file.getParentDirectory().createDirectory())
        return false;
    file.deleteFile();
    juce::FileOutputStream out(file);
    return out.openedOk() && midiFile.writeTo(out, midiFile.getNumTracks() > 1 ? 1 : 0);
}

dsp::PaletteKey DolphinFlowProcessor::getPaletteKey() const
{
    dsp::PaletteKey key;
    key.homeTonic = std::clamp(juce::roundToInt(getParameterValue(ids::key)), 0, 11);
    key.homeScale = std::clamp(juce::roundToInt(getParameterValue(ids::scale)), 0, dsp::kNumScales - 1);
    key.shift = std::clamp(static_cast<int>(apvts.state.getProperty(kPaletteShift, 0)), -dsp::kMaxPaletteShift,
                           dsp::kMaxPaletteShift);
    // -1 (or nothing saved) follows the home scale, so changing SCALE moves the palette with it.
    const int scale = static_cast<int>(apvts.state.getProperty(kPaletteScale, -1));
    key.scale = scale < 0 ? key.homeScale : std::clamp(scale, 0, dsp::kNumScales - 1);
    return key;
}

void DolphinFlowProcessor::setPaletteBorrow(int shift, int scale)
{
    const int homeScale = std::clamp(juce::roundToInt(getParameterValue(ids::scale)), 0, dsp::kNumScales - 1);
    apvts.state.setProperty(kPaletteShift, std::clamp(shift, -dsp::kMaxPaletteShift, dsp::kMaxPaletteShift), nullptr);
    apvts.state.setProperty(kPaletteScale, scale == homeScale ? -1 : std::clamp(scale, 0, dsp::kNumScales - 1),
                            nullptr);
}

unsigned DolphinFlowProcessor::getHomeKeyMask() const
{
    const auto key = getPaletteKey();
    return dsp::scaleMask(key.homeTonic, key.homeScale);
}

juce::File DolphinFlowProcessor::clipFile(const ClipScope& scope) const
{
    const auto key = getPaletteKey();
    const auto name = juce::File::createLegalFileName("Dolphin Flow - " +
                                                      clipTitle(song, scope, key.homeTonic, key.homeScale) + ".mid");
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
            loadSongFromState();
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
