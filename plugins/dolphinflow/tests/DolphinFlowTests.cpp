#include "DolphinFlowEditor.h"
#include "DolphinFlowProcessor.h"
#include "dsp/DolphinFlowEngine.h"
#include "dsp/VoiceLeader.h"

#include <animalaudio/core/BuildStamp.h>
#include <animalaudio/testing/SignalProbe.h>
#include <animalaudio/testing/TestKit.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <map>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using animalaudio::testing::largestStep;
using animalaudio::testing::rms;
using animalaudio::testing::writePngIfRequested;
using dolphinflow::dsp::DolphinFlowEngine;
using dolphinflow::dsp::EngineSettings;
using dolphinflow::dsp::Mode;
using dolphinflow::dsp::VoiceLeader;
using dolphinflow::dsp::Voicing;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kMaj = 0, kMin = 1, kDom7 = 2, kMin9 = 9;
constexpr int C = 0, D = 2, E = 4, F = 5, G = 7, A = 9, B_ = 11;

Voicing voicing(std::initializer_list<int> notes)
{
    Voicing v;
    for (int n : notes)
        v.notes[v.count++] = n;
    return v;
}

int totalMovement(const Voicing& from, const Voicing& to)
{
    int sum = 0;
    for (int i = 0; i < from.count; ++i)
        sum += std::abs(to.notes[i] - from.notes[i]);
    return sum;
}

bool containsPitchClass(const Voicing& v, int pc)
{
    for (int i = 0; i < v.count; ++i)
        if (dolphinflow::dsp::pitchClassOf(v.notes[i]) == dolphinflow::dsp::pitchClassOf(pc))
            return true;
    return false;
}

struct Render
{
    std::vector<float> left, right;
};

void renderInto(DolphinFlowEngine& engine, Render& out, int samples, int blockSize = 256)
{
    std::vector<float> l(static_cast<std::size_t>(blockSize)), r(static_cast<std::size_t>(blockSize));
    for (int done = 0; done < samples; done += blockSize)
    {
        const int n = std::min(blockSize, samples - done);
        float* channels[] = {l.data(), r.data()};
        engine.process(channels, 2, n);
        out.left.insert(out.left.end(), l.begin(), l.begin() + n);
        out.right.insert(out.right.end(), r.begin(), r.begin() + n);
    }
}

struct EngineRig
{
    DolphinFlowEngine engine;
    Render out;

    explicit EngineRig(EngineSettings settings = {})
    {
        engine.prepare(kRate);
        engine.setSettings(settings);
        engine.reset();
    }

    void run(double seconds, int blockSize = 256)
    {
        renderInto(engine, out, static_cast<int>(seconds * kRate), blockSize);
    }
};

int samplesFor(double seconds)
{
    return static_cast<int>(seconds * kRate);
}
} // namespace

// ---- chords and voice leading ---------------------------------------------------------------------------------

TEST_CASE("dolphinflow: chord shapes are well formed")
{
    for (const auto& shape : dolphinflow::dsp::kChordShapes)
    {
        REQUIRE(shape.count >= 3);
        REQUIRE(shape.count <= dolphinflow::dsp::kMaxChordTones);
        REQUIRE(shape.intervals[0] == 0);
        for (int i = 1; i < shape.count; ++i)
            REQUIRE(shape.intervals[i] > shape.intervals[i - 1]);
    }
    REQUIRE(juce::String(dolphinflow::dsp::pitchClassName(9)) + dolphinflow::dsp::chordShape(4).symbol == "Am7");
}

TEST_CASE("dolphinflow: C to Am moves one voice up a whole tone")
{
    const auto c = voicing({60, 64, 67, 72});
    const auto am = VoiceLeader::lead(c, A, kMin, 4, 64);
    REQUIRE(totalMovement(c, am) == 2);
    int moved = 0;
    for (int i = 0; i < 4; ++i)
        if (am.notes[i] != c.notes[i])
        {
            ++moved;
            REQUIRE(c.notes[i] == 67);
            REQUIRE(am.notes[i] == 69);
        }
    REQUIRE(moved == 1);
}

TEST_CASE("dolphinflow: C to F holds the common tone")
{
    const auto c = voicing({60, 64, 67, 72});
    const auto f = VoiceLeader::lead(c, F, kMaj, 4, 64);
    REQUIRE(f.notes[0] == 60);
    REQUIRE(f.notes[3] == 72);
    REQUIRE(totalMovement(c, f) <= 3);
}

TEST_CASE("dolphinflow: G7 resolves to C by small steps")
{
    const auto g7 = VoiceLeader::initial(G, kDom7, 4, 60);
    const auto c = VoiceLeader::lead(g7, C, kMaj, 4, 60);
    for (int i = 0; i < 4; ++i)
        REQUIRE(std::abs(c.notes[i] - g7.notes[i]) <= 4);
    REQUIRE(totalMovement(g7, c) <= 7);
}

TEST_CASE("dolphinflow: every chord tone sounds, and thin voicings drop the fifth first")
{
    const int voices = GENERATE(3, 4, 5, 6);
    const int shape = GENERATE(Catch::Generators::range(0, dolphinflow::dsp::kNumChordShapes));
    const int root = GENERATE(C, E, A);
    const auto chord = VoiceLeader::initial(root, shape, voices, 60);
    REQUIRE(chord.count == voices);
    const auto& tones = dolphinflow::dsp::chordShape(shape);
    for (int i = 0; i < tones.count; ++i)
    {
        const bool dropped = voices < tones.count && (i == 2 || (voices == 3 && i == 4));
        if (!dropped)
            REQUIRE(containsPitchClass(chord, root + tones.intervals[i]));
    }
}

TEST_CASE("dolphinflow: a minor nine on three voices keeps root, third and seventh")
{
    const auto chord = VoiceLeader::initial(D, kMin9, 3, 60);
    REQUIRE(containsPitchClass(chord, D));
    REQUIRE(containsPitchClass(chord, F));
    REQUIRE(containsPitchClass(chord, C));
}

TEST_CASE("dolphinflow: long progressions stay in range, close and repeatable")
{
    const int voices = GENERATE(3, 4, 6);
    const int centre = GENERATE(48, 60, 72);
    std::srand(7);
    auto current = VoiceLeader::initial(C, kMaj, voices, centre);
    auto replay = current;
    for (int step = 0; step < 400; ++step)
    {
        const int root = std::rand() % 12;
        const int shape = std::rand() % dolphinflow::dsp::kNumChordShapes;
        const auto next = VoiceLeader::lead(current, root, shape, voices, centre);
        REQUIRE(VoiceLeader::lead(replay, root, shape, voices, centre).notes[0] == next.notes[0]);
        for (int v = 0; v < voices; ++v)
        {
            REQUIRE(next.notes[v] >= centre - dolphinflow::dsp::kRangeHalfWidth);
            REQUIRE(next.notes[v] <= centre + dolphinflow::dsp::kRangeHalfWidth);
            REQUIRE(std::abs(next.notes[v] - current.notes[v]) <= 12);
        }
        current = replay = next;
    }
}

// ---- engine ------------------------------------------------------------------------------------------------

TEST_CASE("dolphinflow: every voice glides and they land together")
{
    EngineSettings settings;
    settings.glideMs = 200.0f;
    settings.curve = 0.0f;
    EngineRig rig(settings);
    rig.engine.playChord(C, kMaj);
    rig.run(0.5);
    const auto before = rig.engine.getVoicing();

    rig.engine.playChord(A, kMin);
    const auto after = rig.engine.getVoicing();
    rig.run(0.1);
    bool anyMidway = false;
    for (int v = 0; v < after.count; ++v)
    {
        const float pitch = rig.engine.currentPitch(v);
        const float lo = static_cast<float>(std::min(before.notes[v], after.notes[v]));
        const float hi = static_cast<float>(std::max(before.notes[v], after.notes[v]));
        REQUIRE(pitch >= lo - 1.0e-4f);
        REQUIRE(pitch <= hi + 1.0e-4f);
        if (before.notes[v] != after.notes[v])
        {
            // Linear curve: halfway in time is halfway in pitch.
            REQUIRE(pitch == Catch::Approx((lo + hi) * 0.5f).margin(0.1));
            anyMidway = true;
        }
    }
    REQUIRE(anyMidway);

    rig.run(0.11);
    for (int v = 0; v < after.count; ++v)
        REQUIRE(rig.engine.currentPitch(v) == Catch::Approx(static_cast<float>(after.notes[v])));
}

TEST_CASE("dolphinflow: zero glide jumps, and a chord after silence starts on its notes")
{
    EngineSettings settings;
    settings.glideMs = 0.0f;
    EngineRig rig(settings);
    rig.engine.playChord(C, kMaj);
    rig.run(0.2);
    rig.engine.playChord(F, kMaj);
    rig.run(0.001);
    for (int v = 0; v < rig.engine.getVoicing().count; ++v)
        REQUIRE(rig.engine.currentPitch(v) == static_cast<float>(rig.engine.getVoicing().notes[v]));

    settings.glideMs = 500.0f;
    rig.engine.setSettings(settings);
    rig.engine.release();
    rig.run(4.0);
    REQUIRE(rig.engine.getEnvelopeLevel() == 0.0f);
    rig.engine.playChord(G, kMaj);
    rig.run(0.001);
    for (int v = 0; v < rig.engine.getVoicing().count; ++v)
        REQUIRE(rig.engine.currentPitch(v) == static_cast<float>(rig.engine.getVoicing().notes[v]));
}

TEST_CASE("dolphinflow: block size does not change the render")
{
    const int blockSize = GENERATE(1, 37, 512);
    const auto play = [](int block)
    {
        EngineRig rig;
        rig.engine.playChord(C, kMaj);
        rig.run(0.512 * 1.0, block); // 24576 samples: a multiple of 512, so both runs change chord on the same sample
        rig.engine.playChord(A, kMin);
        rig.run(0.512, block);
        rig.engine.release();
        rig.run(0.512, block);
        return rig.out;
    };
    const auto reference = play(512);
    const auto test = play(blockSize);
    REQUIRE(test.left.size() == reference.left.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < reference.left.size(); ++i)
        worst =
            std::max({worst, std::abs(test.left[i] - reference.left[i]), std::abs(test.right[i] - reference.right[i])});
    REQUIRE(worst < 1.0e-5f);
}

TEST_CASE("dolphinflow: chord changes glide without clicks")
{
    const auto mode = GENERATE(Mode::Pad, Mode::Stab, Mode::Keys);
    EngineSettings settings;
    settings.mode = mode;
    settings.space = 0.0f;
    EngineRig rig(settings);
    rig.engine.playChord(C, kMaj);
    rig.run(0.6);
    const auto window = static_cast<std::size_t>(samplesFor(0.02));
    const double firstStrike = largestStep(rig.out.left, 0, static_cast<std::size_t>(samplesFor(0.3)));
    const auto changeAt = rig.out.left.size();
    rig.engine.playChord(E, kMin);
    rig.run(0.6);

    const double steady = largestStep(rig.out.left, changeAt - static_cast<std::size_t>(samplesFor(0.2)), changeAt);
    const double atChange = largestStep(rig.out.left, changeAt, changeAt + window);
    if (mode == Mode::Pad)
        REQUIRE(atChange < steady * 1.5 + 1.0e-4); // legato: the change is just a glide
    else
        REQUIRE(atChange < firstStrike * 1.5 + 1.0e-4); // a re-strike is about as sharp as the first note, no jump
    for (float s : rig.out.left)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("dolphinflow: sound is in a sensible range and goes silent after release")
{
    EngineSettings settings;
    settings.space = 0.0f;
    EngineRig rig(settings);
    rig.run(0.1);
    for (float s : rig.out.left)
        REQUIRE(s == 0.0f); // idle is exact silence

    const int voices = GENERATE(3, 6);
    settings.voices = voices;
    rig.engine.setSettings(settings);
    rig.engine.playChord(D, kMin9);
    rig.run(1.0);
    const double level =
        rms(rig.out.left, rig.out.left.size() - static_cast<std::size_t>(samplesFor(0.3)), rig.out.left.size());
    REQUIRE(level > 0.03);
    REQUIRE(level < 0.5);
    for (float s : rig.out.left)
        REQUIRE(std::abs(s) < 1.0f);

    rig.engine.release();
    rig.run(5.0);
    REQUIRE(rms(rig.out.left, rig.out.left.size() - 1000, rig.out.left.size()) < 1.0e-5);
}

TEST_CASE("dolphinflow: stab mode strikes again on every chord")
{
    EngineSettings settings;
    settings.mode = Mode::Stab;
    settings.space = 0.0f;
    EngineRig rig(settings);
    rig.engine.playChord(C, kMaj);
    rig.run(1.0);
    REQUIRE(rig.engine.getEnvelopeLevel() < 0.01f);
    rig.engine.playChord(F, kMaj);
    rig.run(0.01);
    REQUIRE(rig.engine.getEnvelopeLevel() > 0.5f);
}

TEST_CASE("dolphinflow: octave and voice count changes re-voice a held chord")
{
    EngineRig rig;
    rig.engine.playChord(C, kMaj);
    rig.run(0.2);
    EngineSettings settings;
    settings.centre = 72;
    rig.engine.setSettings(settings);
    rig.run(1.0);
    for (int v = 0; v < rig.engine.getVoicing().count; ++v)
        REQUIRE(rig.engine.getVoicing().notes[v] >= 72 - dolphinflow::dsp::kRangeHalfWidth);
    settings.voices = 6;
    rig.engine.setSettings(settings);
    rig.run(0.01);
    REQUIRE(rig.engine.getVoicing().count == 6);
}

// ---- processor ---------------------------------------------------------------------------------------------

namespace
{
using Harness = animalaudio::testing::ProcessorHarness<dolphinflow::DolphinFlowProcessor>;

void processBlock(Harness& h, int samples = 512)
{
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    h.processor.processBlock(buffer, h.midi);
    h.midi.clear();
}

float paramValue(dolphinflow::DolphinFlowProcessor& p, const juce::ParameterID& id)
{
    return p.getParameterValue(id);
}
} // namespace

TEST_CASE("dolphinflow: parameters exist with their defaults")
{
    dolphinflow::DolphinFlowProcessor processor;
    REQUIRE(paramValue(processor, dolphinflow::ids::chord) == 0.0f);
    REQUIRE(paramValue(processor, dolphinflow::ids::mode) == 0.0f);
    REQUIRE(paramValue(processor, dolphinflow::ids::voices) == 1.0f);
    REQUIRE(paramValue(processor, dolphinflow::ids::octave) == 0.0f);
    REQUIRE(paramValue(processor, dolphinflow::ids::glide) == Catch::Approx(350.0f));
    REQUIRE(paramValue(processor, dolphinflow::ids::latch) == 0.0f);
    REQUIRE(processor.apvts.getParameter(dolphinflow::ids::level.getParamID())->getCurrentValueAsText() == "70 %");
    REQUIRE(processor.acceptsMidi());
    REQUIRE(processor.getTotalNumInputChannels() == 0);
    REQUIRE(processor.getTotalNumOutputChannels() == 2);
}

TEST_CASE("dolphinflow: MIDI notes pick the root, CHORD picks the quality")
{
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::chord, 4.0f); // min7
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 100);
    processBlock(h);
    const auto& engine = h.processor.getEngine();
    REQUIRE(engine.getRoot() == A);
    REQUIRE(engine.getShape() == 4);
    REQUIRE(engine.isGateOn());

    h.midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    processBlock(h);
    REQUIRE(engine.getRoot() == E);
    h.midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
    processBlock(h);
    REQUIRE(engine.getRoot() == A); // falls back to the key still held
    REQUIRE(engine.isGateOn());
    h.midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
    processBlock(h);
    REQUIRE_FALSE(engine.isGateOn());
}

TEST_CASE("dolphinflow: the played root's octave moves the register, like Live's Z / X")
{
    // Live's Z / X shift its computer keyboard's notes an octave, with the plugin window closed: the chord follows.
    Harness h(kRate, 512);
    const auto& engine = h.processor.getEngine();
    const auto mean = [&engine]
    {
        const auto& v = engine.getVoicing();
        float sum = 0.0f;
        for (int i = 0; i < v.count; ++i)
            sum += static_cast<float>(v.notes[i]);
        return sum / static_cast<float>(v.count);
    };
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0); // C3, Live's home octave
    processBlock(h);
    const float home = mean();
    REQUIRE(std::abs(home - 60.0f) < 6.0f);

    h.midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.8f), 0); // X: same key, an octave up
    processBlock(h);
    REQUIRE(engine.getRoot() == C);
    REQUIRE(mean() == Catch::Approx(home + 12.0f).margin(2.0));

    h.midi.addEvent(juce::MidiMessage::noteOff(1, 72), 0);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0); // Z twice from there
    processBlock(h);
    REQUIRE(mean() == Catch::Approx(home - 12.0f).margin(2.0));

    // OCTAVE adds on top; together they stop at three octaves either way.
    h.setParam(dolphinflow::ids::octave, -2.0f);
    h.midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 24, 0.8f), 0);
    processBlock(h);
    REQUIRE(mean() == Catch::Approx(home - 36.0f).margin(2.0));
}

TEST_CASE("dolphinflow: changing CHORD re-voices the held chord; LATCH holds after release")
{
    Harness h(kRate, 512);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    processBlock(h);
    h.setParam(dolphinflow::ids::chord, 1.0f);
    processBlock(h);
    REQUIRE(h.processor.getEngine().getShape() == 1);

    h.setParam(dolphinflow::ids::latch, 1.0f);
    h.midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    processBlock(h);
    REQUIRE(h.processor.getEngine().isGateOn());
    h.setParam(dolphinflow::ids::latch, 0.0f);
    processBlock(h);
    REQUIRE_FALSE(h.processor.getEngine().isGateOn());
}

TEST_CASE("dolphinflow: editor keys and buttons reach the audio thread")
{
    Harness h(kRate, 512);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + 7, 7);
    processBlock(h);
    REQUIRE(h.processor.getEngine().getRoot() == G);
    h.processor.releaseAllKeys();
    processBlock(h);
    REQUIRE_FALSE(h.processor.getEngine().isGateOn());
}

namespace
{
struct MidiEvent
{
    bool on;
    int note;
    int position;
};

std::vector<MidiEvent> processAndCollect(Harness& h, int samples = 512)
{
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    h.processor.processBlock(buffer, h.midi);
    std::vector<MidiEvent> out;
    for (const auto metadata : h.midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOnOrOff())
            out.push_back({message.isNoteOn(), message.getNoteNumber(), metadata.samplePosition});
    }
    h.midi.clear();
    return out;
}

std::vector<int> notesWhere(const std::vector<MidiEvent>& events, bool on)
{
    std::vector<int> notes;
    for (const auto& e : events)
        if (e.on == on)
            notes.push_back(e.note);
    std::sort(notes.begin(), notes.end());
    return notes;
}

std::vector<int> voicedNotes(const dolphinflow::DolphinFlowProcessor& p)
{
    const auto& v = p.getEngine().getVoicing();
    std::vector<int> notes(v.notes, v.notes + v.count);
    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return notes;
}
} // namespace

TEST_CASE("dolphinflow: chords are named from their notes")
{
    int root = 0, shape = 0;
    const int am7[] = {57, 60, 64, 67};
    REQUIRE(dolphinflow::dsp::identifyChord(am7, 4, root, shape));
    REQUIRE(root == A);
    REQUIRE(shape == 4);
    const int c6inversion[] = {64, 67, 72}; // C major, E in the bass
    REQUIRE(dolphinflow::dsp::identifyChord(c6inversion, 3, root, shape));
    REQUIRE(root == C);
    REQUIRE(shape == kMaj);
    const int cluster[] = {60, 61, 62};
    REQUIRE_FALSE(dolphinflow::dsp::identifyChord(cluster, 3, root, shape));
    REQUIRE(shape == -1);
}

TEST_CASE("dolphinflow: the played notes come out as MIDI, common tones held across changes")
{
    Harness h(kRate, 512);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + C, C);
    const auto first = processAndCollect(h);
    REQUIRE(notesWhere(first, false).empty());
    REQUIRE(notesWhere(first, true) == voicedNotes(h.processor));
    REQUIRE(notesWhere(first, true).size() >= 3);

    const auto before = voicedNotes(h.processor);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + A, A);
    const auto change = processAndCollect(h);
    const auto after = voicedNotes(h.processor);
    for (int note : notesWhere(change, false))
        REQUIRE(std::find(after.begin(), after.end(), note) == after.end()); // only notes that left
    for (int note : notesWhere(change, true))
        REQUIRE(std::find(before.begin(), before.end(), note) == before.end()); // only notes that arrived
    REQUIRE_FALSE(notesWhere(change, true).empty());
    REQUIRE(notesWhere(change, true).size() < after.size()); // C to Am shares tones

    h.processor.releaseAllKeys();
    const auto release = processAndCollect(h);
    REQUIRE(notesWhere(release, false) == after);
    REQUIRE(notesWhere(release, true).empty());
}

TEST_CASE("dolphinflow: MIDI out lands on the input's sample")
{
    Harness h(kRate, 512);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 62, 0.8f), 200);
    const auto out = processAndCollect(h);
    REQUIRE_FALSE(out.empty());
    for (const auto& e : out)
        REQUIRE(e.position == 200);
}

TEST_CASE("dolphinflow: a clip recorded from MIDI out plays back the same voicing")
{
    Harness recorder(kRate, 512);
    recorder.setParam(dolphinflow::ids::chord, 4.0f); // min7
    recorder.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + D, D);
    const auto recorded = processAndCollect(recorder);
    const auto voiced = voicedNotes(recorder.processor);

    Harness player(kRate, 512); // CHORD left at MAJ: the clip, not the parameter, decides
    for (int note : notesWhere(recorded, true))
        player.midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 64);
    const auto echoed = processAndCollect(player);
    REQUIRE(voicedNotes(player.processor) == voiced);
    REQUIRE(player.processor.getEngine().getRoot() == D);
    REQUIRE(player.processor.getEngine().getShape() == 4);
    REQUIRE(notesWhere(echoed, true) == voiced);

    // The next chord in the clip: offs and ons on one sample glide as one change.
    const auto& engine = player.processor.getEngine();
    std::vector<int> next = voiced;
    next.back() += 2;
    for (int note : voiced)
        player.midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    for (int note : next)
        player.midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
    processAndCollect(player, 64);
    REQUIRE(voicedNotes(player.processor) == next);
    REQUIRE(engine.isHeld());
}

namespace
{
struct RawEvent
{
    juce::MidiMessage message;
    int position;
};

std::vector<RawEvent> processRaw(Harness& h, int samples = 512)
{
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    h.processor.processBlock(buffer, h.midi);
    std::vector<RawEvent> out;
    for (const auto metadata : h.midi)
        out.push_back({metadata.getMessage(), metadata.samplePosition});
    h.midi.clear();
    return out;
}
} // namespace

TEST_CASE("dolphinflow: MPE out puts each note on its own channel and sends glides as pitch bends")
{
    using dolphinflow::NoteOut;
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::glide, 300.0f);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + C, C);
    std::map<int, int> channelOf; // sounding note -> member channel
    for (const auto& e : processRaw(h))
        if (e.message.isNoteOn())
        {
            REQUIRE(e.message.getChannel() >= NoteOut::kFirstMemberChannel);
            for (const auto& [note, channel] : channelOf)
                REQUIRE(channel != e.message.getChannel());
            channelOf[e.message.getNoteNumber()] = e.message.getChannel();
        }
    REQUIRE(channelOf.size() == voicedNotes(h.processor).size());

    // C to A: the voices that move end their notes and start the new ones on fresh channels, each with a bend
    // from where it slides from (G up to A is -2 semitones) that runs to 0 with the glide. Common tones never bend.
    const auto before = voicedNotes(h.processor);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + A, A);
    const auto change = processRaw(h);
    std::map<int, std::vector<int>> bends; // channel -> every bend sent on it
    std::map<int, int> arrivedOn;          // channel -> the note started on it
    for (std::size_t i = 0; i < change.size(); ++i)
        if (change[i].message.isNoteOn())
        {
            const int ch = change[i].message.getChannel();
            REQUIRE(std::find(before.begin(), before.end(), change[i].message.getNoteNumber()) == before.end());
            REQUIRE(i > 0);
            REQUIRE(change[i - 1].message.isPitchWheel()); // the bend comes before the note
            REQUIRE(change[i - 1].message.getChannel() == ch);
            REQUIRE(change[i - 1].message.getPitchWheelValue() != 8192);
            arrivedOn[ch] = change[i].message.getNoteNumber();
        }
    REQUIRE_FALSE(arrivedOn.empty());
    std::vector<RawEvent> all = change;
    for (int block = 0; block < 40; ++block) // ~0.43 s, past the 300 ms glide
        for (const auto& e : processRaw(h))
        {
            REQUIRE_FALSE(e.message.isNoteOnOrOff());
            all.push_back(e);
        }
    for (const auto& e : all)
        if (e.message.isPitchWheel())
        {
            REQUIRE(arrivedOn.count(e.message.getChannel()) == 1);
            bends[e.message.getChannel()].push_back(e.message.getPitchWheelValue());
        }
    bool sawG = false;
    for (const auto& [ch, curve] : bends)
    {
        REQUIRE(curve.size() > 20); // a smooth curve, not a jump
        const bool rising = curve.front() < 8192;
        REQUIRE((rising ? std::is_sorted(curve.begin(), curve.end()) : std::is_sorted(curve.rbegin(), curve.rend())));
        REQUIRE(curve.back() == 8192);
        if (dolphinflow::dsp::pitchClassOf(arrivedOn[ch]) == A &&
            std::find(before.begin(), before.end(), arrivedOn[ch] - 2) != before.end())
        {
            sawG = true;
            REQUIRE(curve.front() == NoteOut::bendFor(-2.0f));
        }
    }
    REQUIRE(sawG);
}

#if JUCE_MAC
TEST_CASE("dolphinflow: the virtual MPE port carries the notes and glide bends")
{
    struct Listener : juce::MidiInputCallback
    {
        std::atomic<int> notesOn{0}, bends{0};
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
        {
            if (m.isNoteOn())
                ++notesOn;
            if (m.isPitchWheel())
                ++bends;
        }
    } listener;

    Harness h(kRate, 512);
    // Its own name: a running Live may already have a Dolphin Flow making "Animal Audio - Dolphin Flow".
    h.processor.openMpePort("Animal Audio - Dolphin Flow Test " +
                            juce::String(juce::Random::getSystemRandom().nextInt()));
    const auto name = h.processor.getMpePortName();
    REQUIRE(name.startsWith("Animal Audio - Dolphin Flow"));
    std::unique_ptr<juce::MidiInput> input;
    for (int attempt = 0; attempt < 50 && input == nullptr; ++attempt)
    {
        for (const auto& device : juce::MidiInput::getAvailableDevices())
            if (device.name == name)
                input = juce::MidiInput::openDevice(device.identifier, &listener);
        if (input == nullptr) // the device list updates on the message thread
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    REQUIRE(input != nullptr);
    input->start();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200); // CoreMIDI connects the port asynchronously

    h.setParam(dolphinflow::ids::glide, 300.0f);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + C, C);
    processRaw(h);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + A, A);
    for (int block = 0; block < 10; ++block)
        processRaw(h);
    for (int wait = 0; wait < 200 && listener.bends < 10; ++wait)
        juce::Thread::sleep(10);
    input->stop();
    REQUIRE(listener.notesOn >= static_cast<int>(voicedNotes(h.processor).size()));
    REQUIRE(listener.bends >= 10);
}
#endif

TEST_CASE("dolphinflow: its own MPE notes coming back in are ignored, so roots still glide")
{
    // Live hands a plugin that is not MPE enabled every note on channel 1; an MPE enabled one keeps the channels.
    const bool collapsed = GENERATE(true, false);
    Harness h(kRate, 512);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0); // C root from a keyboard
    std::vector<RawEvent> echo;
    for (const auto& e : processRaw(h))
        if (e.message.isNoteOn())
            echo.push_back(e);
    REQUIRE(echo.size() >= 3);
    const auto cChord = voicedNotes(h.processor);

    // The port's notes arrive back a block later, as on a track armed on All Ins: the chord must not change.
    for (const auto& e : echo)
        h.midi.addEvent(juce::MidiMessage::noteOn(collapsed ? 1 : e.message.getChannel(), e.message.getNoteNumber(),
                                                  static_cast<juce::uint8>(dolphinflow::MpePort::portVelocity(
                                                      e.message.getVelocity()))), // as the port sends it
                        0);
    processRaw(h);
    REQUIRE(voicedNotes(h.processor) == cChord);
    REQUIRE(h.processor.getEngine().getShape() == kMaj);

    // The next root is a chord change (a glide), not a note added to the echo.
    h.midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
    processRaw(h);
    REQUIRE(h.processor.getEngine().getRoot() == A);
    REQUIRE(h.processor.getEngine().getVoicing().count == 4);

    // A real MPE keyboard on member channels still plays a voicing.
    Harness k(kRate, 512);
    for (int i = 0; i < 3; ++i)
        k.midi.addEvent(juce::MidiMessage::noteOn(2 + i, 60 + 4 * i, 0.8f), 0);
    processRaw(k);
    REQUIRE(voicedNotes(k.processor) == std::vector<int>{60, 64, 68});
}

TEST_CASE("dolphinflow: with MPE off the notes are plain channel 1 notes")
{
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::mpe, 0.0f);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + C, C);
    processRaw(h);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + A, A);
    for (int block = 0; block < 20; ++block)
        for (const auto& e : processRaw(h))
        {
            REQUIRE_FALSE(e.message.isPitchWheel());
            if (e.message.isNoteOnOrOff())
                REQUIRE(e.message.getChannel() == 1);
        }
}

TEST_CASE("dolphinflow: starting play or record sends a latched chord again, so the recording holds it")
{
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::latch, 1.0f);
    h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + D, D);
    processAndCollect(h);
    h.processor.releaseAllKeys();
    processAndCollect(h);
    const auto latched = voicedNotes(h.processor);
    REQUIRE(h.processor.getEngine().isHeld());

    juce::AudioPlayHead::PositionInfo position;
    position.setIsPlaying(true);
    position.setPpqPosition(0.0);
    h.playHead.position = position;
    const auto started = processAndCollect(h);
    REQUIRE(notesWhere(started, false) == latched);
    REQUIRE(notesWhere(started, true) == latched);
    for (const auto& e : started)
        REQUIRE(e.position == 0);
    REQUIRE(processAndCollect(h).empty()); // only once

    position.setIsRecording(true);
    h.playHead.position = position;
    REQUIRE(notesWhere(processAndCollect(h), true) == latched);
}

TEST_CASE("dolphinflow: the loop starting with the host sends each chord once, no empty notes")
{
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::loop, 1.0f);
    for (int block = 0; block < 4; ++block) // free-running on the first square
        processBlock(h);
    juce::AudioPlayHead::PositionInfo position;
    position.setIsPlaying(true);
    position.setBpm(120.0);
    position.setPpqPosition(8.0); // the third square
    h.playHead.position = position;
    const auto started = processAndCollect(h);
    REQUIRE(h.processor.getPlayingChord() == 2);
    REQUIRE(notesWhere(started, true) == voicedNotes(h.processor));
    for (auto on = started.begin(); on != started.end(); ++on) // a note started is not ended: no empty notes
        if (on->on)
            REQUIRE(
                std::none_of(on + 1, started.end(), [&](const MidiEvent& e) { return !e.on && e.note == on->note; }));
}

TEST_CASE("dolphinflow: two overlapping MIDI roots stay roots")
{
    Harness h(kRate, 512);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
    h.midi.addEvent(juce::MidiMessage::noteOn(1, 53, 0.8f), 10);
    processAndCollect(h);
    REQUIRE(h.processor.getEngine().getRoot() == F);
    REQUIRE(h.processor.getEngine().getVoicing().count == 4);
}

TEST_CASE("dolphinflow: state round trip")
{
    dolphinflow::DolphinFlowProcessor a;
    const auto set = [](dolphinflow::DolphinFlowProcessor& p, const juce::ParameterID& id, float value)
    {
        auto* param = p.apvts.getParameter(id.getParamID());
        param->setValueNotifyingHost(param->convertTo0to1(value));
    };
    set(a, dolphinflow::ids::chord, 6.0f);
    set(a, dolphinflow::ids::mode, 2.0f);
    set(a, dolphinflow::ids::glide, 900.0f);
    set(a, dolphinflow::ids::octave, -1.0f);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    dolphinflow::DolphinFlowProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(paramValue(b, dolphinflow::ids::chord) == 6.0f);
    REQUIRE(paramValue(b, dolphinflow::ids::mode) == 2.0f);
    REQUIRE(paramValue(b, dolphinflow::ids::glide) == Catch::Approx(900.0f).margin(0.5));
    REQUIRE(paramValue(b, dolphinflow::ids::octave) == -1.0f);
}

// ---- the song: sections, chords, melody tracks ----------------------------------------------------------------

namespace
{
using dolphinflow::dsp::MelodyNote;
using dolphinflow::dsp::Song;
using dolphinflow::dsp::SongChord;

void processSamples(Harness& h, int samples, int blockSize = 512)
{
    for (int done = 0; done < samples; done += blockSize)
        processBlock(h, std::min(blockSize, samples - done));
}

constexpr int kBeat = 24000; // samples per beat at 120 BPM, 48 kHz

/** The default song (Lead and Bass tracks) with these chords in one Verse. */
Song songWith(std::initializer_list<SongChord> chords)
{
    auto song = dolphinflow::dsp::defaultSong();
    song.sections.front().chords = chords;
    return song;
}

struct ChannelEvent
{
    bool on;
    int note;
    int position;
    int channel;
};

/** Every note on / off a block sends, with its channel and sample, over `blocks` blocks. */
std::vector<ChannelEvent> collect(Harness& h, int blocks, int blockSize = 512)
{
    std::vector<ChannelEvent> out;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        buffer.clear();
        h.processor.processBlock(buffer, h.midi);
        for (const auto metadata : h.midi)
        {
            const auto m = metadata.getMessage();
            if (m.isNoteOnOrOff())
                out.push_back(
                    {m.isNoteOn(), m.getNoteNumber(), b * blockSize + metadata.samplePosition, m.getChannel()});
        }
        h.midi.clear();
    }
    return out;
}
} // namespace

TEST_CASE("dolphinflow: the song places chords, finds where drops land and tidies empty sections")
{
    using namespace dolphinflow::dsp;
    auto song = defaultSong();
    REQUIRE(song.tracks.size() == 2);
    REQUIRE(song.tracks[1].role == TrackRole::Bass);
    const auto chords = placeChords(song);
    REQUIRE(chords.size() == 4);
    REQUIRE(chords[2].start == Catch::Approx(8.0));
    REQUIRE(chordBeats(song) == Catch::Approx(16.0));
    REQUIRE(songBeats(song) == Catch::Approx(16.0));
    REQUIRE(chordIndexAt(chords, 13.0) == 3);

    // A drop before a chord's middle goes before it, after it goes after it.
    REQUIRE(insertPointAt(song, 5.0).index == 1);
    REQUIRE(insertPointAt(song, 7.0).index == 2);
    REQUIRE(insertPointAt(song, 99.0).index == 4);

    Section chorus;
    chorus.name = "Chorus";
    chorus.chords = {{C, kMaj, 2.0}};
    song.sections.push_back(chorus);
    song.sections.insert(song.sections.begin() + 1, Section{}); // an empty middle section
    song.sections.push_back(Section{});                         // and an empty last one
    tidySections(song);
    REQUIRE(song.sections.size() == 3);
    REQUIRE(song.sections[1].name == "Chorus");
    REQUIRE(song.sections[2].chords.empty()); // the last stays: where the next chord is dropped
    REQUIRE(insertPointAt(song, 17.0).section == 1);

    const auto taken = takeChords(song, {1, 4});
    REQUIRE(taken.size() == 2);
    REQUIRE(taken[0].root == F);
    REQUIRE(taken[1].root == C);
    REQUIRE(countChords(song) == 3);

    // A melody note past the chords makes the song longer, to the bar.
    song.tracks[0].notes = {{72, 13.0, 6.0, 100}};
    REQUIRE(songBeats(song) == Catch::Approx(20.0));
}

TEST_CASE("dolphinflow: LOOP plays the song's chords in order and comes round")
{
    Harness h(kRate, 512); // no host tempo: 120 BPM
    h.setParam(dolphinflow::ids::loop, 1.0f);
    const auto& engine = h.processor.getEngine();

    processSamples(h, kBeat);
    REQUIRE(h.processor.getPlayingChord() == 0);
    REQUIRE(engine.getRoot() == A);
    REQUIRE(engine.getShape() == 4);
    const auto firstPass = voicedNotes(h.processor);

    processSamples(h, 4 * kBeat);
    REQUIRE(h.processor.getPlayingChord() == 1);
    REQUIRE(engine.getRoot() == F);
    processSamples(h, 8 * kBeat);
    REQUIRE(h.processor.getPlayingChord() == 3);
    REQUIRE(engine.getRoot() == G);
    processSamples(h, 4 * kBeat);
    REQUIRE(h.processor.getPlayingChord() == 0);
    REQUIRE(voicedNotes(h.processor) == firstPass); // the first pass already sounds like every later one

    h.setParam(dolphinflow::ids::loop, 0.0f);
    processBlock(h);
    REQUIRE(h.processor.getPlayingChord() == -1);
    REQUIRE_FALSE(engine.isHeld());
}

TEST_CASE("dolphinflow: chord changes land on the beat, at each chord's length")
{
    Harness h(kRate, 512);
    h.processor.setSong(songWith({{C, kMaj, 2.0}, {F, kMaj, 1.0}}));
    h.setParam(dolphinflow::ids::loop, 1.0f);
    processSamples(h, 2 * kBeat, 1); // samples 0 .. 47999: beats 0 to 2
    REQUIRE(h.processor.getEngine().getRoot() == C);
    processSamples(h, 1, 1); // sample 48000 is beat 2
    REQUIRE(h.processor.getEngine().getRoot() == F);
}

TEST_CASE("dolphinflow: LOOP follows the host's beat position while it plays")
{
    Harness h(kRate, 512);
    juce::AudioPlayHead::PositionInfo position;
    position.setIsPlaying(true);
    position.setBpm(124.0);
    position.setPpqPosition(9.0); // bar 3: the third chord
    h.playHead.position = position;
    h.setParam(dolphinflow::ids::loop, 1.0f);
    processBlock(h);
    REQUIRE(h.processor.getPlayingChord() == 2);
    REQUIRE(h.processor.getEngine().getRoot() == C);
    REQUIRE(h.processor.getTempo() == Catch::Approx(124.0));
}

TEST_CASE("dolphinflow: an empty song stays silent, and a chord put in plays")
{
    Harness h(kRate, 512);
    h.processor.setSong(songWith({}));
    h.setParam(dolphinflow::ids::loop, 1.0f);
    processBlock(h);
    REQUIRE_FALSE(h.processor.isSequencing());
    REQUIRE_FALSE(h.processor.getEngine().isHeld());

    h.processor.setSong(songWith({{E, kMin, 4.0}}));
    const auto out = processAndCollect(h);
    REQUIRE(h.processor.getPlayingChord() == 0);
    REQUIRE(notesWhere(out, true) == voicedNotes(h.processor));
}

TEST_CASE("dolphinflow: PLAY runs the song once and stops at its end; seek moves the playhead")
{
    Harness h(kRate, 512);
    h.processor.setSong(songWith({{C, kMaj, 2.0}, {G, kMaj, 2.0}}));
    h.processor.setPlaying(true);
    processSamples(h, kBeat);
    REQUIRE(h.processor.isSequencing());
    REQUIRE(h.processor.getEngine().getRoot() == C);
    // The song is a bar long (4 beats): past it, PLAY stops and the playhead goes home.
    processSamples(h, 4 * kBeat);
    REQUIRE_FALSE(h.processor.isPlaying());
    REQUIRE_FALSE(h.processor.isSequencing());
    REQUIRE(h.processor.getPlayheadBeats() == Catch::Approx(0.0));

    h.processor.seek(2.0);
    h.processor.setPlaying(true);
    processBlock(h);
    REQUIRE(h.processor.getEngine().getRoot() == G);
    REQUIRE(h.processor.getPlayheadBeats() == Catch::Approx(2.0).margin(0.05));

    // Pausing keeps the place.
    h.processor.setPlaying(false);
    processBlock(h);
    const double paused = h.processor.getPlayheadBeats();
    REQUIRE_FALSE(h.processor.isSequencing());
    h.processor.setPlaying(true);
    processBlock(h);
    REQUIRE(h.processor.getPlayheadBeats() >= paused);
}

TEST_CASE("dolphinflow: LINK plays the song whenever the host plays, at the host's position")
{
    Harness h(kRate, 512);
    h.processor.setLinked(true);
    processBlock(h);
    REQUIRE_FALSE(h.processor.isSequencing()); // the host is stopped

    juce::AudioPlayHead::PositionInfo position;
    position.setIsPlaying(true);
    position.setBpm(120.0);
    position.setPpqPosition(4.5);
    h.playHead.position = position;
    processBlock(h);
    REQUIRE(h.processor.isSequencing());
    REQUIRE(h.processor.getPlayingChord() == 1);

    position.setIsPlaying(false);
    h.playHead.position = position;
    processBlock(h);
    REQUIRE_FALSE(h.processor.isSequencing());

    juce::MemoryBlock state;
    h.processor.getStateInformation(state);
    dolphinflow::DolphinFlowProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(b.isLinked());
}

TEST_CASE("dolphinflow: melody tracks play as MIDI on their own channels, on the beat")
{
    Harness h(kRate, 512);
    auto song = songWith({{C, kMaj, 4.0}});
    song.tracks[0].notes = {{72, 1.0, 1.0, 100}, {74, 2.5, 0.5, 64}}; // Lead
    song.tracks[1].notes = {{36, 0.0, 4.0, 110}};                     // Bass
    h.processor.setSong(song);
    h.setParam(dolphinflow::ids::loop, 1.0f);
    const auto events = collect(h, (4 * kBeat) / 512 + 2);

    const auto find = [&](int channel, int note, bool on)
    {
        for (const auto& e : events)
            if (e.channel == channel && e.note == note && e.on == on)
                return e.position;
        return -1;
    };
    REQUIRE(find(13, 72, true) == kBeat);      // Lead on channel 13, at beat 1
    REQUIRE(find(13, 72, false) == 2 * kBeat); // and off at beat 2
    REQUIRE(find(13, 74, true) == kBeat * 5 / 2);
    REQUIRE(find(14, 36, true) == 0); // Bass on channel 14
    // The loop comes round: the bass note ends at the loop's end and starts again.
    REQUIRE(find(14, 36, false) >= 4 * kBeat - 1);
    // Chord notes stay on channel 1 (MPE off) or 2-12 (MPE on), never on the melody channels.
    for (const auto& e : events)
        if (e.note != 72 && e.note != 74 && e.note != 36)
            REQUIRE(e.channel <= 12);
}

TEST_CASE("dolphinflow: mute and solo decide which tracks play")
{
    Harness h(kRate, 512);
    auto song = songWith({{C, kMaj, 4.0}});
    song.tracks[0].notes = {{72, 0.0, 1.0, 100}};
    song.tracks[1].notes = {{36, 0.0, 1.0, 100}};
    song.tracks[1].mix.solo = true;
    h.processor.setSong(song);
    h.setParam(dolphinflow::ids::loop, 1.0f);
    auto events = collect(h, 4);
    const auto has = [&](int channel)
    {
        return std::any_of(events.begin(), events.end(),
                           [&](const ChannelEvent& e) { return e.on && e.channel == channel; });
    };
    REQUIRE(has(14));
    REQUIRE_FALSE(has(13));                          // the lead is not soloed
    REQUIRE_FALSE(h.processor.getEngine().isHeld()); // nor is the chord track

    song.tracks[1].mix.solo = false;
    song.tracks[1].mix.mute = true;
    h.setParam(dolphinflow::ids::loop, 0.0f);
    processBlock(h);
    h.processor.setSong(song);
    h.setParam(dolphinflow::ids::loop, 1.0f);
    events = collect(h, 4);
    REQUIRE(has(13));
    REQUIRE_FALSE(has(14));
    REQUIRE(h.processor.getEngine().isHeld());
}

TEST_CASE("dolphinflow: a piano roll click sounds its note briefly on the track's channel")
{
    Harness h(kRate, 512);
    h.processor.previewNote(1, 43, 90);
    const auto events = collect(h, 40); // 0.43 s
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].on);
    REQUIRE(events[0].channel == 14);
    REQUIRE(events[0].note == 43);
    REQUIRE_FALSE(events[1].on);
    REQUIRE(events[1].position > static_cast<int>(0.3 * kRate));
}

TEST_CASE("dolphinflow: undo and redo walk the song's edits")
{
    dolphinflow::DolphinFlowProcessor p;
    const auto original = p.getSong();
    REQUIRE_FALSE(p.canUndo());
    p.setSong(songWith({{D, kMin, 4.0}}));
    auto drag = p.getSong();
    drag.sections.front().chords.front().beats = 8.0;
    p.setSong(drag, false); // a drag in progress
    p.pushUndo(songWith({{D, kMin, 4.0}}));
    REQUIRE(p.getSong().sections.front().chords.front().beats == Catch::Approx(8.0));
    REQUIRE(p.undo());
    REQUIRE(p.getSong().sections.front().chords.front().beats == Catch::Approx(4.0));
    REQUIRE(p.undo());
    REQUIRE(dolphinflow::dsp::countChords(p.getSong()) == dolphinflow::dsp::countChords(original));
    REQUIRE_FALSE(p.undo());
    REQUIRE(p.redo());
    REQUIRE(p.getSong().sections.front().chords.front().root == D);
}

namespace
{
juce::MidiFile readClip(const juce::File& file)
{
    juce::FileInputStream in(file);
    juce::MidiFile midiFile;
    REQUIRE(midiFile.readFrom(in));
    return midiFile;
}
} // namespace

TEST_CASE("dolphinflow: the chord clip is the song voiced as it plays")
{
    dolphinflow::DolphinFlowProcessor p;
    p.setParameterFromUi(dolphinflow::ids::mpe, 0.0f); // plain notes on channel 1
    const auto file = juce::File::createTempFile(".mid");
    REQUIRE(p.writeMidiClip(file));
    REQUIRE(p.clipFile().getFileName() == "Dolphin Flow - Chords - Am7 Fmaj7 C G.mid");

    const auto midiFile = readClip(file);
    REQUIRE(midiFile.getTimeFormat() == 960);
    REQUIRE(midiFile.getNumTracks() == 1);
    const auto* track = midiFile.getTrack(0);
    std::vector<int> firstChord;
    double lastOff = 0.0;
    int ons = 0;
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        const auto& m = track->getEventPointer(i)->message;
        if (m.isNoteOn())
        {
            ++ons;
            REQUIRE(m.getChannel() == 1);
            if (m.getTimeStamp() == 0.0)
                firstChord.push_back(m.getNoteNumber());
        }
        if (m.isNoteOff())
            lastOff = std::max(lastOff, m.getTimeStamp());
    }
    std::sort(firstChord.begin(), firstChord.end());
    REQUIRE(lastOff == Catch::Approx(16.0 * 960.0)); // four bars
    REQUIRE(firstChord.size() >= 3);
    REQUIRE(ons < 16); // common tones are tied across chords, not re-struck

    const auto voicings = dolphinflow::dsp::settledVoicings(dolphinflow::dsp::placeChords(p.getSong()), 4, 60);
    std::vector<int> expected(voicings[0].notes, voicings[0].notes + voicings[0].count);
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
    REQUIRE(firstChord == expected);

    // A selection: just its chords, starting at zero.
    dolphinflow::ClipScope scope;
    scope.from = 8.0;
    scope.to = 12.0;
    REQUIRE(p.clipFile(scope).getFileName() == "Dolphin Flow - Chords - C.mid");
    REQUIRE(p.writeMidiClip(file, scope));
    const auto part = readClip(file);
    double end = 0.0;
    for (int i = 0; i < part.getTrack(0)->getNumEvents(); ++i)
        if (part.getTrack(0)->getEventPointer(i)->message.isNoteOff())
            end = std::max(end, part.getTrack(0)->getEventPointer(i)->message.getTimeStamp());
    REQUIRE(end == Catch::Approx(4.0 * 960.0));
    file.deleteFile();

    p.setSong(songWith({}));
    REQUIRE_FALSE(p.writeMidiClip(file));
}

TEST_CASE("dolphinflow: with MPE the chord clip carries each glide as its note's pitch bend")
{
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::mpe, 1.0f);
    h.setParam(dolphinflow::ids::glide, 500.0f);
    h.processor.setSong(songWith({{C, kMaj, 4.0}, {A, kMin, 4.0}})); // C -> Am: G steps up to A
    const auto file = juce::File::createTempFile(".mid");
    REQUIRE(h.processor.writeMidiClip(file));
    const auto midiFile = readClip(file);
    const auto* track = midiFile.getTrack(0);
    bool zone = false;
    int bends = 0;
    std::vector<int> channels;
    double glidingStart = -1.0;
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        const auto& m = track->getEventPointer(i)->message;
        if (m.isController() && m.getChannel() == 1 && m.getControllerNumber() == 6)
            zone = true; // the MPE zone set-up on the manager channel
        if (m.isPitchWheel() && m.getPitchWheelValue() != 8192)
            ++bends;
        if (m.isNoteOn())
        {
            REQUIRE(m.getChannel() >= 2);
            REQUIRE(m.getChannel() <= 12);
            channels.push_back(m.getChannel());
            if (m.getNoteNumber() % 12 == A && m.getTimeStamp() > 0.0)
                glidingStart = m.getTimeStamp();
        }
    }
    REQUIRE(zone);
    REQUIRE(bends > 4); // a curve, not a jump
    REQUIRE(glidingStart == Catch::Approx(4.0 * 960.0));
    // Notes that sound together are on different channels.
    std::sort(channels.begin(), channels.begin() + 3);
    REQUIRE(std::unique(channels.begin(), channels.begin() + 3) == channels.begin() + 3);
    file.deleteFile();
}

TEST_CASE("dolphinflow: melody tracks and the whole song export as clips too")
{
    dolphinflow::DolphinFlowProcessor p;
    auto song = p.getSong();
    song.tracks[0].notes = {{72, 1.0, 1.0, 100}, {76, 6.0, 2.0, 80}};
    song.tracks[1].notes = {{33, 0.0, 4.0, 100}};
    p.setSong(song);
    const auto file = juce::File::createTempFile(".mid");

    dolphinflow::ClipScope lead;
    lead.track = 0;
    REQUIRE(p.clipFile(lead).getFileName() == "Dolphin Flow - Lead.mid");
    REQUIRE(p.writeMidiClip(file, lead));
    const auto leadFile = readClip(file);
    std::vector<std::pair<double, int>> ons;
    for (int i = 0; i < leadFile.getTrack(0)->getNumEvents(); ++i)
    {
        const auto& m = leadFile.getTrack(0)->getEventPointer(i)->message;
        if (m.isNoteOn())
        {
            REQUIRE(m.getChannel() == 1); // plays on whatever it is dropped on
            ons.push_back({m.getTimeStamp(), m.getNoteNumber()});
        }
    }
    REQUIRE(ons == std::vector<std::pair<double, int>>{{960.0, 72}, {6.0 * 960.0, 76}});

    dolphinflow::ClipScope all;
    all.allTracks = true;
    REQUIRE(p.clipFile(all).getFileName() == "Dolphin Flow - Song.mid");
    REQUIRE(p.writeMidiClip(file, all));
    REQUIRE(readClip(file).getNumTracks() == 3); // chords, lead, bass
    file.deleteFile();
}

TEST_CASE("dolphinflow: the song is saved with the plugin state, and older saves' squares come over")
{
    dolphinflow::DolphinFlowProcessor a;
    auto song = songWith({{D, kMin9, 8.0}, {B_, kDom7, 1.0}});
    song.sections.front().name = "Intro";
    dolphinflow::dsp::Section chorus;
    chorus.name = "Chorus";
    chorus.colour = 3;
    chorus.chords = {{F, 18, 2.5}};
    song.sections.push_back(chorus);
    song.tracks[0].notes = {{72, 1.5, 0.25, 77}};
    song.tracks[0].mix.solo = true;
    song.tracks[1].name = "Sub";
    song.tracks[1].mix.volume = 0.4f;
    song.chordMix.mute = true;
    a.setSong(song);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    dolphinflow::DolphinFlowProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto& loaded = b.getSong();
    REQUIRE(loaded.sections.size() == 2);
    REQUIRE(loaded.sections[0].name == "Intro");
    REQUIRE(loaded.sections[0].chords[0] == SongChord{D, kMin9, 8.0});
    REQUIRE(loaded.sections[1].colour == 3);
    REQUIRE(loaded.sections[1].chords[0] == SongChord{F, 18, 2.5});
    REQUIRE(loaded.tracks[0].notes.front() == MelodyNote{72, 1.5, 0.25, 77});
    REQUIRE(loaded.tracks[0].mix.solo);
    REQUIRE(loaded.tracks[1].name == "Sub");
    REQUIRE(loaded.tracks[1].mix.volume == Catch::Approx(0.4f));
    REQUIRE(loaded.chordMix.mute);

    // A save from before the song: its eight squares become one Verse.
    juce::ValueTree old("DolphinFlowState");
    juce::ValueTree progression("Progression");
    const int roots[] = {D, -1, G, C};
    const int lengths[] = {3, 3, 1, 7};
    for (int i = 0; i < 4; ++i)
    {
        juce::ValueTree slot("Slot");
        slot.setProperty("root", roots[i], nullptr);
        slot.setProperty("shape", kMin, nullptr);
        slot.setProperty("length", lengths[i], nullptr);
        progression.appendChild(slot, nullptr);
    }
    old.appendChild(progression, nullptr);
    const auto migrated = dolphinflow::songFromState(old);
    REQUIRE(migrated.sections.size() == 1);
    REQUIRE(migrated.sections[0].chords.size() == 3);
    REQUIRE(migrated.sections[0].chords[1] == SongChord{G, kMin, 2.0});
    REQUIRE(migrated.sections[0].chords[2] == SongChord{C, kMin, 16.0});
}

TEST_CASE("dolphinflow: harmonic guides, chord relations and the circle palette")
{
    using namespace dolphinflow::dsp;
    const unsigned cMajor = scaleMask(C, 0);
    // Under Am7: A is the root, C and E chord tones, G an extension, D in key, D# out.
    REQUIRE(guideFor(57, A, 4, cMajor) == Guide::Root);
    REQUIRE(guideFor(60, A, 4, cMajor) == Guide::ChordTone);
    REQUIRE(guideFor(76, A, 4, cMajor) == Guide::ChordTone);
    REQUIRE(guideFor(67, A, 4, cMajor) == Guide::Extension);
    REQUIRE(guideFor(62, A, 4, cMajor) == Guide::InKey);
    REQUIRE(guideFor(63, A, 4, cMajor) == Guide::OutOfKey);

    REQUIRE(relationBetween(G, kDom7, C, kMaj) == Relation::Strong); // V7 -> I
    REQUIRE(relationBetween(C, kMaj, A, kMin) == Relation::Smooth);  // two shared notes
    REQUIRE(relationBetween(C, kMaj, F, 7) != Relation::None);       // a fifth down
    REQUIRE(relationBetween(C, kMaj, D, 1) == Relation::None);
    REQUIRE(chordContains(A, 4, pitchClassMask({60, 64})));
    REQUIRE_FALSE(chordContains(A, 4, pitchClassMask({60, 65})));

    PaletteKey key;
    const auto at = [&](CircleRow row, int degree)
    {
        const auto c = circleChord(key, static_cast<int>(row), degree);
        return c.shape < 0 ? std::string("-") : std::string(chordName(c.root, c.shape).text);
    };
    REQUIRE(at(CircleRow::Triad, 0) == "C");
    REQUIRE(at(CircleRow::Sixth, 0) == "C6");
    REQUIRE(at(CircleRow::Sixth, 1) == "Dm6");
    REQUIRE(at(CircleRow::Add9, 0) == "Cadd9");
    REQUIRE(at(CircleRow::Add9, 1) == "-"); // only major triads get add9
    REQUIRE(at(CircleRow::MajorSeventh, 4) == "G7");
    REQUIRE(at(CircleRow::Ninth, 1) == "Dm9");
    REQUIRE(at(CircleRow::LeadsTo, 1) == "A7");
    REQUIRE(std::string(circleChord(key, static_cast<int>(CircleRow::LeadsTo), 1).numeral.text) == "V7/ii");
}

// ---- editor ------------------------------------------------------------------------------------------------

TEST_CASE("dolphinflow: editor renders headlessly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    h.setParam(dolphinflow::ids::chord, 4.0f);
    std::unique_ptr<dolphinflow::DolphinFlowEditor> editor(
        static_cast<dolphinflow::DolphinFlowEditor*>(h.processor.createEditor()));

    // A short progression (Am7, Fmaj7, C, G) so the ribbons show voices gliding between chords.
    const struct
    {
        int note, shape;
    } progression[] = {{57, 4}, {53, 3}, {60, 0}, {55, 0}};
    for (const auto& chord : progression)
    {
        h.setParam(dolphinflow::ids::chord, static_cast<float>(chord.shape));
        h.midi.addEvent(juce::MidiMessage::noteOn(1, chord.note, 0.8f), 0);
        for (int frame = 0; frame < 36; ++frame)
        {
            processBlock(h, 1600); // one 30 Hz frame of audio
            editor->refresh();
        }
        h.midi.addEvent(juce::MidiMessage::noteOff(1, chord.note), 0);
    }
    h.midi.clear();

    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    REQUIRE(image.getWidth() == dolphinflow::DolphinFlowEditor::kWidth * 2);
    REQUIRE(image.getHeight() == dolphinflow::DolphinFlowEditor::kHeight * 2);
    int bright = 0;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            bright += image.getPixelAt(x, y).getBrightness() > 0.55f ? 1 : 0;
    REQUIRE(bright > 400);
    writePngIfRequested(image, "dolphinflow-ui.png");
}

TEST_CASE("dolphinflow: computer keys play roots and pick chords")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    std::unique_ptr<dolphinflow::DolphinFlowEditor> editor(
        static_cast<dolphinflow::DolphinFlowEditor*>(h.processor.createEditor()));

    REQUIRE(editor->keyPressed(juce::KeyPress('5', {}, '5')));
    REQUIRE(paramValue(h.processor, dolphinflow::ids::chord) == 4.0f);
    REQUIRE(editor->keyPressed(juce::KeyPress('h', {}, 'h')));
    REQUIRE(editor->keyPressed(juce::KeyPress('h', {}, 'h'))); // auto-repeat is ignored
    processBlock(h);
    REQUIRE(h.processor.getEngine().getRoot() == A);
    REQUIRE(h.processor.getEngine().isGateOn());

    editor->keyStateChanged(false); // no key is physically down in a test, so 'h' reads as released
    processBlock(h);
    REQUIRE_FALSE(h.processor.getEngine().isGateOn());

    REQUIRE(editor->keyPressed(juce::KeyPress('x', {}, 'x')));
    REQUIRE(paramValue(h.processor, dolphinflow::ids::octave) == 1.0f);
    REQUIRE(editor->keyPressed(juce::KeyPress('l', {}, 'l')));
    REQUIRE(paramValue(h.processor, dolphinflow::ids::latch) == 1.0f);
    REQUIRE_FALSE(editor->keyPressed(juce::KeyPress('a', juce::ModifierKeys::commandModifier, 'a')));
    REQUIRE_FALSE(editor->keyPressed(juce::KeyPress('q', {}, 'q')));
}

TEST_CASE("dolphinflow: editor controls drive their parameters")
{
    juce::ScopedJuceInitialiser_GUI gui;
    dolphinflow::DolphinFlowProcessor processor;
    std::unique_ptr<dolphinflow::DolphinFlowEditor> editor(
        static_cast<dolphinflow::DolphinFlowEditor*>(processor.createEditor()));

    animalaudio::testing::click(editor->getChordPills().getPill(8));
    REQUIRE(paramValue(processor, dolphinflow::ids::chord) == 8.0f);
    animalaudio::testing::click(editor->getModePills().getPill(1));
    REQUIRE(paramValue(processor, dolphinflow::ids::mode) == 1.0f);
    animalaudio::testing::click(editor->getVoicePills().getPill(3));
    REQUIRE(paramValue(processor, dolphinflow::ids::voices) == 3.0f);
    animalaudio::testing::click(editor->getOctaveDownButton());
    REQUIRE(paramValue(processor, dolphinflow::ids::octave) == -1.0f);
    animalaudio::testing::click(editor->getLatchButton());
    REQUIRE(paramValue(processor, dolphinflow::ids::latch) == 1.0f);

    editor->getKnob(dolphinflow::ids::glide).setValue(1200.0, juce::sendNotificationSync);
    REQUIRE(paramValue(processor, dolphinflow::ids::glide) == Catch::Approx(1200.0f).margin(1.0));
    auto* tone = processor.apvts.getParameter(dolphinflow::ids::tone.getParamID());
    tone->setValueNotifyingHost(0.2f);
    REQUIRE(editor->getKnob(dolphinflow::ids::tone).getValue() == Catch::Approx(0.2).margin(0.002));
}

// ---- keys, numerals and the chord palette -------------------------------------------------------------------

namespace
{
std::string name(int root, int shape)
{
    return dolphinflow::dsp::chordName(root, shape).text;
}

std::string numeral(int root, int shape, int tonic, int scale)
{
    return dolphinflow::dsp::romanNumeral(root, shape, tonic, scale).text;
}

constexpr int kMajorScale = 0, kMinorScale = 1, kHarmonicMinor = 7;
} // namespace

TEST_CASE("dolphinflow: every chord shape has a distinct set of notes")
{
    using namespace dolphinflow::dsp;
    for (int a = 0; a < kNumChordShapes; ++a)
        for (int b = a + 1; b < kNumChordShapes; ++b)
            REQUIRE(chordMask(0, a) != chordMask(0, b));
    REQUIRE(name(C, 14) == "Cm7b5");
    REQUIRE(name(G, 17) == "G7sus4");
}

TEST_CASE("dolphinflow: a key's own chords and their numerals")
{
    using namespace dolphinflow::dsp;
    const char* triads[] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
    const char* sevenths[] = {"Cmaj7", "Dm7", "Em7", "Fmaj7", "G7", "Am7", "Bm7b5"};
    const char* numerals[] = {"I", "ii", "iii", "IV", "V", "vi", "vii\xc2\xb0"};
    for (int d = 0; d < kDegrees; ++d)
    {
        const int root = degreeRoot(C, kMajorScale, d);
        REQUIRE(name(root, stackedChord(C, kMajorScale, d, 3)) == triads[d]);
        REQUIRE(name(root, stackedChord(C, kMajorScale, d, 4)) == sevenths[d]);
        REQUIRE(numeral(root, stackedChord(C, kMajorScale, d, 3), C, kMajorScale) == numerals[d]);
    }
    // Harmonic minor raises the seventh: a dominant V7 and a diminished seventh on the leading tone.
    REQUIRE(name(E, stackedChord(A, kHarmonicMinor, 4, 4)) == "E7");
    REQUIRE(name(9 + 11 - 12, stackedChord(A, kHarmonicMinor, 6, 4)) == "G#dim7");
    // Chords from outside the key are written against it.
    REQUIRE(numeral(10, kMaj, C, kMajorScale) == "bVII");
    REQUIRE(numeral(F, kMin, C, kMajorScale) == "iv");
    REQUIRE(numeral(G, kDom7, C, kMinorScale) == "V7");
    REQUIRE(numeral(D, 4, C, kMajorScale) == "ii7");
}

TEST_CASE("dolphinflow: the palette borrows round the circle of fifths and from parallel modes")
{
    using namespace dolphinflow::dsp;
    PaletteKey home;
    for (int d = 0; d < kDegrees; ++d)
        for (int row = 0; row < kPaletteRows; ++row)
            if (row != static_cast<int>(PaletteRow::LeadsTo))
                REQUIRE(paletteChord(home, row, d).inKey);
    REQUIRE(name(paletteChord(home, 2, 1).root, paletteChord(home, 2, 1).shape) == "Dm9");
    REQUIRE(name(paletteChord(home, 2, 2).root, paletteChord(home, 2, 2).shape) == "Esus4");

    const auto leadsToIi = paletteChord(home, static_cast<int>(PaletteRow::LeadsTo), 1);
    REQUIRE(name(leadsToIi.root, leadsToIi.shape) == "A7");
    REQUIRE(std::string(leadsToIi.numeral.text) == "V7/ii");
    REQUIRE_FALSE(leadsToIi.inKey);

    PaletteKey flatSide = home;
    flatSide.shift = -1; // F major: its IV is Bb, borrowed into C as bVII
    const auto bb = paletteChord(flatSide, 0, 3);
    REQUIRE(name(bb.root, bb.shape) == "A#");
    REQUIRE(std::string(chordNameInKey(bb.root, bb.shape, C, kMajorScale).text) == "Bb");
    REQUIRE(std::string(chordNameInKey(3, 3, F, kMinorScale).text) == "Ebmaj7"); // F minor is a flat key
    REQUIRE(std::string(chordNameInKey(6, 0, D, kMajorScale).text) == "F#");
    REQUIRE_FALSE(bb.inKey);
    REQUIRE(std::string(bb.numeral.text) == "bVII");
    REQUIRE(paletteChord(flatSide, 0, 0).inKey); // F itself is still in C

    PaletteKey parallel = home;
    parallel.scale = kMinorScale; // C minor: the iv and bVI are classic borrowed chords
    const auto fm = paletteChord(parallel, 0, 3);
    REQUIRE(name(fm.root, fm.shape) == "Fm");
    REQUIRE(std::string(fm.numeral.text) == "iv");
    REQUIRE_FALSE(fm.inKey);
    REQUIRE(std::string(paletteChord(parallel, 0, 5).numeral.text) == "bVI");
}

TEST_CASE("dolphinflow: key, scale and the borrowed palette are saved")
{
    dolphinflow::DolphinFlowProcessor a;
    REQUIRE(paramValue(a, dolphinflow::ids::key) == 0.0f);
    REQUIRE(paramValue(a, dolphinflow::ids::scale) == 0.0f);
    REQUIRE(a.getPaletteKey().isHome());
    a.setParameterFromUi(dolphinflow::ids::key, static_cast<float>(A));
    a.setParameterFromUi(dolphinflow::ids::scale, static_cast<float>(kMinorScale));
    a.setPaletteBorrow(2, 2); // two fifths up, dorian
    auto song = a.getSong();
    song.sections.front().chords.front() = {C, 18, 2.0}; // a chord shape past the number keys
    a.setSong(song);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    dolphinflow::DolphinFlowProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto key = b.getPaletteKey();
    REQUIRE(key.homeTonic == A);
    REQUIRE(key.homeScale == kMinorScale);
    REQUIRE(key.shift == 2);
    REQUIRE(key.scale == 2);
    REQUIRE(key.tonic() == 11);
    REQUIRE(b.getSong().sections.front().chords.front() == dolphinflow::dsp::SongChord{C, 18, 2.0});
    REQUIRE(b.getHomeKeyMask() == dolphinflow::dsp::scaleMask(A, kMinorScale));
}

namespace
{
/** A mouse event at `pos` for a press that began at `down` (so drags have a distance). */
juce::MouseEvent dragEvent(juce::Component& c, juce::Point<float> down, juce::Point<float> pos,
                           juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
{
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
}

void press(juce::Component& c, juce::Point<float> at)
{
    c.mouseDown(animalaudio::testing::makeMouseEvent(c, at));
    c.mouseUp(animalaudio::testing::makeMouseEvent(c, at));
}

std::unique_ptr<dolphinflow::DolphinFlowEditor> composeEditor(Harness& h)
{
    std::unique_ptr<dolphinflow::DolphinFlowEditor> editor(
        static_cast<dolphinflow::DolphinFlowEditor*>(h.processor.createEditor()));
    animalaudio::testing::click(editor->getComposeTab());
    return editor;
}

int countImageBright(const juce::Image& image)
{
    int bright = 0;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            bright += image.getPixelAt(x, y).getBrightness() > 0.55f ? 1 : 0;
    return bright;
}
} // namespace

TEST_CASE("dolphinflow: COMPOSE swaps the keys page for the song workspace, and remembers it")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    auto editor = composeEditor(h);
    REQUIRE(editor->getView() == dolphinflow::DolphinFlowEditor::View::Compose);
    REQUIRE(editor->getComposeView().isVisible());
    REQUIRE_FALSE(editor->getChordPills().isVisible());
    REQUIRE_FALSE(editor->getRootKey(C).isVisible());
    REQUIRE(editor->getComposeView().getPalette().isVisible()); // the chord track starts selected

    editor.reset();
    auto again = std::unique_ptr<dolphinflow::DolphinFlowEditor>(
        static_cast<dolphinflow::DolphinFlowEditor*>(h.processor.createEditor()));
    REQUIRE(again->getView() == dolphinflow::DolphinFlowEditor::View::Compose);
    animalaudio::testing::click(again->getKeysTab());
    REQUIRE(again->getChordPills().isVisible());
    REQUIRE_FALSE(again->getComposeView().isVisible());
}

TEST_CASE("dolphinflow: COMPOSE palette plays chords and puts them in the song")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    auto editor = composeEditor(h);
    auto& compose = editor->getComposeView();
    auto& palette = compose.getPalette();
    using dolphinflow::dsp::CircleRow;

    // Holding a circle plays it with its own quality.
    auto& g7 = palette.getCell(static_cast<int>(CircleRow::MajorSeventh), 4);
    g7.mouseDown(animalaudio::testing::makeMouseEvent(g7, {5.0f, 5.0f}));
    processBlock(h);
    REQUIRE(h.processor.getEngine().getRoot() == G);
    REQUIRE(h.processor.getEngine().getShape() == kDom7);
    g7.mouseUp(animalaudio::testing::makeMouseEvent(g7, {5.0f, 5.0f}));
    processBlock(h);
    REQUIRE_FALSE(h.processor.getEngine().isGateOn());

    // Double-click goes in after the selected chord, and becomes the selection.
    compose.getContext().selectedChords = {1};
    auto& dm9 = palette.getCell(static_cast<int>(CircleRow::Ninth), 1);
    dm9.mouseDoubleClick(animalaudio::testing::makeMouseEvent(dm9, {5.0f, 5.0f}));
    auto chords = dolphinflow::dsp::placeChords(h.processor.getSong());
    REQUIRE(chords.size() == 5);
    REQUIRE(chords[2].root == D);
    REQUIRE(chords[2].shape == kMin9);
    REQUIRE(compose.getContext().selectedChords == std::vector<int>{2});

    // Dropping a circle on the timeline inserts it where it lands.
    auto& timeline = compose.getTimeline();
    const auto x = static_cast<int>(timeline.chordBounds(0).getX() + 2.0f);
    juce::DragAndDropTarget::SourceDetails drop(dolphinflow::ui::PaletteCell::describe(E, kMin), &dm9,
                                                {x, dolphinflow::ui::TimelineView::kChordTop + 10});
    REQUIRE(timeline.isInterestedInDragSource(drop));
    timeline.itemDropped(drop);
    chords = dolphinflow::dsp::placeChords(h.processor.getSong());
    REQUIRE(chords.size() == 6);
    REQUIRE(chords[0].root == E);

    // Number keys play the triads; arrows borrow.
    REQUIRE(editor->keyPressed(juce::KeyPress('6', {}, '6')));
    processBlock(h);
    REQUIRE(h.processor.getEngine().getRoot() == A);
    editor->keyStateChanged(false);
    REQUIRE(editor->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    REQUIRE(h.processor.getPaletteKey().shift == -1);
    animalaudio::testing::click(palette.getHomeButton());
    REQUIRE(h.processor.getPaletteKey().isHome());

    // Chord relations and melody matching dim what does not fit, without changing the song.
    animalaudio::testing::click(palette.getRelationsButton());
    REQUIRE(compose.getContext().relations);
    compose.getContext().matchMask = dolphinflow::dsp::pitchClassMask({64, 67});
    compose.getContext().changed();
    REQUIRE(palette.getMatchButton().isVisible());
    animalaudio::testing::click(palette.getMatchButton());
    REQUIRE(compose.getContext().matchMask == 0u);
}

TEST_CASE("dolphinflow: COMPOSE timeline selects, resizes, moves and deletes chords, and adds sections")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    auto editor = composeEditor(h);
    auto& compose = editor->getComposeView();
    auto& ctx = compose.getContext();
    auto& timeline = compose.getTimeline();

    press(timeline, timeline.chordBounds(1).getCentre());
    REQUIRE(ctx.selectedChords == std::vector<int>{1});

    // Drag the right edge two beats on.
    const auto edge = timeline.chordBounds(1).getCentre().withX(timeline.chordBounds(1).getRight() - 2.0f);
    const auto further = edge.translated(static_cast<float>(2.0 * ctx.axis.pixelsPerBeat), 0.0f);
    timeline.mouseDown(dragEvent(timeline, edge, edge));
    timeline.mouseDrag(dragEvent(timeline, edge, further));
    timeline.mouseUp(dragEvent(timeline, edge, further));
    REQUIRE(h.processor.getSong().sections[0].chords[1].beats == Catch::Approx(6.0));
    REQUIRE(h.processor.canUndo());

    // Drag the first chord to the end.
    const auto first = timeline.chordBounds(0).getCentre();
    const auto end = juce::Point<float>(timeline.chordBounds(3).getRight() - 4.0f, first.y);
    press(timeline, first);
    timeline.mouseDown(dragEvent(timeline, first, first));
    timeline.mouseDrag(dragEvent(timeline, first, end));
    timeline.mouseUp(dragEvent(timeline, first, end));
    auto chords = dolphinflow::dsp::placeChords(h.processor.getSong());
    REQUIRE(chords.size() == 4);
    REQUIRE(chords[3].root == A);
    REQUIRE(chords[0].root == F);

    // Delete removes the selection; cmd-Z brings it back.
    ctx.selectedChords = {0};
    REQUIRE(editor->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    REQUIRE(dolphinflow::dsp::countChords(h.processor.getSong()) == 3);
    REQUIRE(editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    REQUIRE(dolphinflow::dsp::countChords(h.processor.getSong()) == 4);

    // + adds a section after the last, ready for the next drop.
    press(timeline, timeline.addSectionBounds().getCentre());
    REQUIRE(h.processor.getSong().sections.size() == 2);
    REQUIRE(h.processor.getSong().sections[1].chords.empty());

    // The ruler moves the playhead.
    press(timeline, {timeline.chordBounds(2).getX() + 1.0f, dolphinflow::ui::TimelineView::kRulerTop + 6.0f});
    REQUIRE(h.processor.getPlayheadBeats() ==
            Catch::Approx(dolphinflow::dsp::placeChords(h.processor.getSong())[2].start));
}

TEST_CASE("dolphinflow: COMPOSE piano roll draws, moves, deletes notes and finds chords for them")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    auto editor = composeEditor(h);
    auto& compose = editor->getComposeView();
    auto& ctx = compose.getContext();
    compose.selectTrack(0);
    auto& roll = compose.getPianoRoll();
    REQUIRE(roll.isVisible());
    REQUIRE_FALSE(compose.getPalette().isVisible());
    roll.centreOn(72);

    // A click on empty space draws a note (and sounds it on the lead's channel).
    const auto at = juce::Point<float>(ctx.axis.xOf(2.0) + 3.0f, roll.yOfPitch(72) + 3.0f);
    press(roll, at);
    const auto& notes = [&]() -> const std::vector<dolphinflow::dsp::MelodyNote>&
    { return h.processor.getSong().tracks[0].notes; };
    REQUIRE(notes().size() == 1);
    REQUIRE(notes()[0] == dolphinflow::dsp::MelodyNote{72, 2.0, 1.0, 100});
    REQUIRE(ctx.selectedNotes == std::vector<int>{0});

    // Drag it a beat later and two semitones up.
    const auto from = roll.noteBounds(notes()[0]).getCentre();
    const auto to = from.translated(static_cast<float>(ctx.axis.pixelsPerBeat), -2.0f * 13.0f);
    roll.mouseDown(dragEvent(roll, from, from));
    roll.mouseDrag(dragEvent(roll, from, to));
    roll.mouseUp(dragEvent(roll, from, to));
    REQUIRE(notes()[0].start == Catch::Approx(3.0));
    REQUIRE(notes()[0].pitch == 74);

    // Keys: up a semitone, then undo it.
    REQUIRE(editor->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    REQUIRE(notes()[0].pitch == 75);
    REQUIRE(editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z')));
    REQUIRE(notes()[0].pitch == 74);

    // Notes spelling a chord set the chord above them.
    auto song = h.processor.getSong();
    song.tracks[0].notes = {{60, 8.0, 1.0, 100}, {64, 8.0, 1.0, 100}, {67, 9.0, 1.0, 100}};
    h.processor.setSong(song);
    ctx.selectedNotes = {0, 1, 2};
    REQUIRE(roll.setChordFromSelection(8.5));
    REQUIRE(dolphinflow::dsp::placeChords(h.processor.getSong())[2].root == C);
    REQUIRE(dolphinflow::dsp::placeChords(h.processor.getSong())[2].shape == kMaj);

    // ...or light up the palette chords that contain them.
    ctx.selectedNotes = {0, 1};
    roll.findChordsForSelection();
    REQUIRE(ctx.matchMask == dolphinflow::dsp::pitchClassMask({60, 64}));
    REQUIRE(ctx.selectedTrack == -1);
    REQUIRE(compose.getPalette().isVisible());

    // Double-click deletes.
    compose.selectTrack(0);
    roll.centreOn(64);
    const auto target = roll.noteBounds(h.processor.getSong().tracks[0].notes[1]).getCentre();
    roll.mouseDoubleClick(animalaudio::testing::makeMouseEvent(roll, target));
    REQUIRE(h.processor.getSong().tracks[0].notes.size() == 2);
}

TEST_CASE("dolphinflow: COMPOSE tracks add, solo and mute, and export the selected part")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    auto editor = composeEditor(h);
    auto& compose = editor->getComposeView();
    compose.addTrack(dolphinflow::dsp::TrackRole::Pad);
    REQUIRE(h.processor.getSong().tracks.size() == 3);
    REQUIRE(h.processor.getSong().tracks[2].name == "Pad");
    REQUIRE(compose.getContext().selectedTrack == 2);

    animalaudio::testing::click(compose.getTrackRow(0).getSoloButton());
    REQUIRE(h.processor.getSong().tracks[0].mix.solo);
    animalaudio::testing::click(compose.getTrackRow(-1).getMuteButton());
    REQUIRE(h.processor.getSong().chordMix.mute);

    REQUIRE(compose.getExportButton().scope(false).track == 2);
    compose.selectTrack(-1);
    compose.getContext().selectedChords = {1, 2};
    const auto scope = compose.getExportButton().scope(false);
    REQUIRE(scope.track == -1);
    REQUIRE(scope.from == Catch::Approx(4.0));
    REQUIRE(scope.to == Catch::Approx(12.0));
    REQUIRE(compose.getExportButton().scope(true).allTracks);

    animalaudio::testing::click(compose.getTrackRow(-1).getVoicingButton());
    REQUIRE(compose.getContext().voicing);
    REQUIRE(compose.getPianoRoll().isVisible());
    REQUIRE(compose.getPianoRoll().isReadOnly());

    animalaudio::testing::click(compose.getLinkButton());
    REQUIRE(h.processor.isLinked());
    animalaudio::testing::click(compose.getPlayButton());
    REQUIRE(h.processor.isPlaying());
    animalaudio::testing::click(compose.getNumeralsButton());
    REQUIRE(compose.getContext().numerals);
    REQUIRE(compose.getContext().chordLabel(A, 4) == "vi7");
}

TEST_CASE("dolphinflow: COMPOSE renders headlessly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    auto song = h.processor.getSong();
    song.sections.front().name = "Verse";
    dolphinflow::dsp::Section chorus;
    chorus.name = "Chorus";
    chorus.colour = 1;
    chorus.chords = {{F, 3, 4.0}, {G, 0, 4.0}, {E, kMin, 2.0}, {A, kMin, 2.0}};
    song.sections.push_back(chorus);
    // A lead line and a bassline on the roots.
    const int lead[] = {76, 79, 81, 79, 77, 76, 74, 72, 76, 77, 79, 84};
    for (int i = 0; i < 12; ++i)
        song.tracks[0].notes.push_back({lead[i], i * 2.0, 1.5, 90 + (i % 3) * 10});
    const int roots[] = {45, 41, 36, 43, 41, 43, 40, 45};
    double t = 0.0;
    const double beats[] = {4, 4, 4, 4, 4, 4, 2, 2};
    for (int i = 0; i < 8; ++i)
    {
        song.tracks[1].notes.push_back({roots[i], t, beats[i] - 0.5, 110});
        t += beats[i];
    }
    h.processor.setSong(song);
    h.setParam(dolphinflow::ids::loop, 1.0f);
    auto editor = composeEditor(h);
    auto& compose = editor->getComposeView();
    compose.getContext().selectedChords = {4};
    compose.getContext().relations = true;
    compose.getContext().focusRoot = G;
    compose.getContext().focusShape = 2;
    for (int frame = 0; frame < 90; ++frame) // 3 s in: the second bar
    {
        processBlock(h, 1600);
        editor->refresh();
    }
    auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    REQUIRE(countImageBright(image) > 400);
    writePngIfRequested(image, "dolphinflow-compose.png");

    compose.selectTrack(0);
    compose.getPianoRoll().fit();
    editor->refresh();
    image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    REQUIRE(countImageBright(image) > 400);
    writePngIfRequested(image, "dolphinflow-pianoroll.png");
}

TEST_CASE("dolphinflow: build stamp is baked in")
{
    REQUIRE(animalaudio::core::buildStampShort().isNotEmpty());
}

TEST_CASE("dolphinflow: spamming keys and buttons keeps the sound and MIDI out sane")
{
    Harness h(kRate, 128);
    juce::Random rng(99);
    std::map<std::pair<int, int>, int> active; // (channel, note) -> count
    int worstActive = 0, badOffs = 0, doubleOns = 0;
    float peak = 0.0f;
    bool finite = true;
    for (int block = 0; block < 20000; ++block)
    {
        const int r = rng.nextInt(10);
        if (r == 0)
            h.setParam(dolphinflow::ids::mpe, rng.nextBool() ? 1.0f : 0.0f);
        if (r == 1)
            h.setParam(dolphinflow::ids::loop, rng.nextBool() ? 1.0f : 0.0f);
        if (r == 2)
            h.setParam(dolphinflow::ids::latch, rng.nextBool() ? 1.0f : 0.0f);
        if (r == 3)
            h.setParam(dolphinflow::ids::mode, static_cast<float>(rng.nextInt(3)));
        if (r == 4)
            h.setParam(dolphinflow::ids::chord, static_cast<float>(rng.nextInt(10)));
        if (r == 5)
            h.setParam(dolphinflow::ids::voices, static_cast<float>(rng.nextInt(4)));
        if (r == 6)
            h.setParam(dolphinflow::ids::octave, static_cast<float>(rng.nextInt(5) - 2));
        if (r == 7)
            h.processor.pressRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + rng.nextInt(12), rng.nextInt(12));
        if (r == 8)
            h.processor.releaseRoot(dolphinflow::DolphinFlowProcessor::kKeySourceBase + rng.nextInt(12));
        if (r == 9)
        {
            const int note = 40 + rng.nextInt(40);
            h.midi.addEvent(rng.nextBool() ? juce::MidiMessage::noteOn(1, note, 0.8f)
                                           : juce::MidiMessage::noteOff(1, note),
                            rng.nextInt(128));
        }
        juce::AudioBuffer<float> buffer(2, 128);
        buffer.clear();
        h.processor.processBlock(buffer, h.midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 128; ++i)
            {
                finite = finite && std::isfinite(buffer.getSample(c, i));
                peak = std::max(peak, std::abs(buffer.getSample(c, i)));
            }
        std::vector<RawEvent> out;
        for (const auto metadata : h.midi)
            out.push_back({metadata.getMessage(), metadata.samplePosition});
        h.midi.clear();
        for (const auto& e : out)
        {
            const auto key = std::make_pair(e.message.getChannel(), e.message.getNoteNumber());
            if (e.message.isNoteOn())
            {
                if (active[key] > 0)
                    ++doubleOns;
                ++active[key];
            }
            else if (e.message.isNoteOff())
            {
                if (active[key] == 0)
                    ++badOffs;
                else
                    --active[key];
            }
        }
        int total = 0;
        for (const auto& [k, n] : active)
            total += n;
        worstActive = std::max(worstActive, total);
    }
    REQUIRE(finite);
    REQUIRE(peak < 1.0f);
    REQUIRE(worstActive <= dolphinflow::dsp::kMaxVoices); // never more notes out than voices
    REQUIRE(badOffs == 0);
    REQUIRE(doubleOns == 0);
}

TEST_CASE("dolphinflow: its own port notes echoing back on channel 1 never feed themselves")
{
    // Live hands a non-MPE plugin all its input on channel 1: our port's notes come back there, a few blocks late.
    Harness h(kRate, 256);
    juce::Random rng(5);
    std::vector<std::vector<RawEvent>> pipe(3);
    int lateEvents = 0;
    for (int block = 0; block < 3000; ++block)
    {
        if (block < 1500 && rng.nextInt(3) == 0)
        {
            const int note = 48 + rng.nextInt(24);
            h.midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), rng.nextInt(256));
            h.midi.addEvent(juce::MidiMessage::noteOff(1, note), 255);
        }
        for (const auto& e : pipe[0])
        {
            auto m = e.message;
            m.setChannel(1);
            h.midi.addEvent(m, e.position);
        }
        pipe.erase(pipe.begin());
        auto out = processRaw(h, 256);
        std::vector<RawEvent> echo;
        for (const auto& e : out)
            if (e.message.isNoteOnOrOff())
            {
                auto m = e.message;
                if (m.isNoteOn()) // as the port sends it
                    m = juce::MidiMessage::noteOn(
                        m.getChannel(), m.getNoteNumber(),
                        static_cast<juce::uint8>(dolphinflow::MpePort::portVelocity(m.getVelocity())));
                echo.push_back({m, e.position});
            }
        if (block >= 2000)
            lateEvents += static_cast<int>(echo.size());
        pipe.push_back(echo);
    }
    REQUIRE(lateEvents == 0); // once the player stops, nothing keeps feeding itself
}
