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
    REQUIRE(h.processor.getPlayingSlot() == 2);
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

// ---- arrangement --------------------------------------------------------------------------------------------

namespace
{
using dolphinflow::dsp::Slot;

void processSamples(Harness& h, int samples, int blockSize = 512)
{
    for (int done = 0; done < samples; done += blockSize)
        processBlock(h, std::min(blockSize, samples - done));
}

constexpr int kBeat = 24000; // samples per beat at 120 BPM, 48 kHz
} // namespace

TEST_CASE("dolphinflow: slots pack, locate and default to a progression")
{
    for (int root = -1; root < 12; ++root)
        for (int shape = 0; shape < dolphinflow::dsp::kNumChordShapes; ++shape)
            for (int length = 0; length < dolphinflow::dsp::kNumSlotLengths; ++length)
            {
                const Slot slot{root, shape, length};
                REQUIRE(Slot::unpack(slot.pack()) == slot);
            }

    dolphinflow::DolphinFlowProcessor p;
    REQUIRE(p.getSlot(0) == Slot{A, 4, 3});
    REQUIRE(p.getSlot(3) == Slot{G, kMaj, 3});
    REQUIRE(p.getSlot(4).isEmpty());

    Slot slots[dolphinflow::dsp::kNumSlots] = {{C, 0, 3}, {}, {F, 0, 1}, {G, 0, 0}};
    REQUIRE(dolphinflow::dsp::loopBeats(slots) == Catch::Approx(7.0));
    REQUIRE(dolphinflow::dsp::locate(slots, 3.5).slot == 0);
    REQUIRE(dolphinflow::dsp::locate(slots, 4.5).slot == 2); // empty square skipped
    REQUIRE(dolphinflow::dsp::locate(slots, 6.25).slot == 3);
    REQUIRE(dolphinflow::dsp::locate(slots, 7.5).slot == 0); // wraps
    REQUIRE(dolphinflow::dsp::locate(slots, 6.25).left == Catch::Approx(0.75));
}

TEST_CASE("dolphinflow: LOOP plays the squares in order and comes round")
{
    Harness h(kRate, 512); // no host tempo: 120 BPM
    h.setParam(dolphinflow::ids::loop, 1.0f);
    const auto& engine = h.processor.getEngine();

    processSamples(h, kBeat);
    REQUIRE(h.processor.getPlayingSlot() == 0);
    REQUIRE(engine.getRoot() == A);
    REQUIRE(engine.getShape() == 4);
    const auto firstPass = voicedNotes(h.processor);

    processSamples(h, 4 * kBeat);
    REQUIRE(h.processor.getPlayingSlot() == 1);
    REQUIRE(engine.getRoot() == F);
    processSamples(h, 8 * kBeat);
    REQUIRE(h.processor.getPlayingSlot() == 3);
    REQUIRE(engine.getRoot() == G);
    processSamples(h, 4 * kBeat);
    REQUIRE(h.processor.getPlayingSlot() == 0);
    REQUIRE(voicedNotes(h.processor) == firstPass); // the first pass already sounds like every later one

    h.setParam(dolphinflow::ids::loop, 0.0f);
    processBlock(h);
    REQUIRE(h.processor.getPlayingSlot() == -1);
    REQUIRE_FALSE(engine.isHeld());
}

TEST_CASE("dolphinflow: chord changes land on the beat, at each square's length")
{
    Harness h(kRate, 512);
    h.processor.setSlot(0, {C, kMaj, 1}); // 2 beats
    h.processor.setSlot(1, {F, kMaj, 0}); // 1 beat
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
    position.setPpqPosition(9.0); // bar 3: the third square
    h.playHead.position = position;
    h.setParam(dolphinflow::ids::loop, 1.0f);
    processBlock(h);
    REQUIRE(h.processor.getPlayingSlot() == 2);
    REQUIRE(h.processor.getEngine().getRoot() == C);
    REQUIRE(h.processor.getTempo() == Catch::Approx(124.0));
}

TEST_CASE("dolphinflow: an empty arrangement stays silent, and the loop's notes come out as MIDI")
{
    Harness h(kRate, 512);
    for (int i = 0; i < dolphinflow::dsp::kNumSlots; ++i)
        h.processor.setSlot(i, {});
    h.setParam(dolphinflow::ids::loop, 1.0f);
    processBlock(h);
    REQUIRE(h.processor.getPlayingSlot() == -1);
    REQUIRE_FALSE(h.processor.getEngine().isHeld());

    h.processor.setSlot(5, {E, kMin, 3});
    const auto out = processAndCollect(h);
    REQUIRE(h.processor.getPlayingSlot() == 5);
    REQUIRE(notesWhere(out, true) == voicedNotes(h.processor));
}

TEST_CASE("dolphinflow: the MIDI clip is one pass of the loop, voiced as it plays")
{
    dolphinflow::DolphinFlowProcessor p;
    const auto file = juce::File::createTempFile(".mid");
    REQUIRE(p.writeMidiClip(file));
    REQUIRE(p.defaultClipFile().getFileName() == "Dolphin Flow - Am7 Fmaj7 C G.mid");

    juce::FileInputStream in(file);
    juce::MidiFile midiFile;
    REQUIRE(midiFile.readFrom(in));
    REQUIRE(midiFile.getTimeFormat() == 960);
    const auto* track = midiFile.getTrack(0);
    REQUIRE(track != nullptr);

    std::vector<int> firstChord;
    double lastOff = 0.0;
    int ons = 0;
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        const auto& m = track->getEventPointer(i)->message;
        if (m.isNoteOn())
        {
            ++ons;
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

    dolphinflow::dsp::Slot slots[dolphinflow::dsp::kNumSlots];
    for (int i = 0; i < dolphinflow::dsp::kNumSlots; ++i)
        slots[i] = p.getSlot(i);
    const auto settled = dolphinflow::dsp::settledVoicing(slots, 0, 4, 60);
    std::vector<int> expected(settled.notes, settled.notes + settled.count);
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
    REQUIRE(firstChord == expected);
    file.deleteFile();

    for (int i = 0; i < dolphinflow::dsp::kNumSlots; ++i)
        p.setSlot(i, {});
    REQUIRE_FALSE(p.writeMidiClip(file));
}

TEST_CASE("dolphinflow: the arrangement is saved with the plugin state")
{
    dolphinflow::DolphinFlowProcessor a;
    a.setSlot(0, {D, kMin9, 5});
    a.setSlot(6, {B_, kDom7, 0});
    a.setSlot(2, {});
    juce::MemoryBlock state;
    a.getStateInformation(state);

    dolphinflow::DolphinFlowProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(b.getSlot(0) == Slot{D, kMin9, 5});
    REQUIRE(b.getSlot(6) == Slot{B_, kDom7, 0});
    REQUIRE(b.getSlot(2).isEmpty());
    REQUIRE(b.getSlot(1) == a.getSlot(1));
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

TEST_CASE("dolphinflow: arrange view writes chords into squares")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    std::unique_ptr<dolphinflow::DolphinFlowEditor> editor(
        static_cast<dolphinflow::DolphinFlowEditor*>(h.processor.createEditor()));
    animalaudio::testing::click(editor->getArrangeTab());
    REQUIRE(editor->getView() == dolphinflow::DolphinFlowEditor::View::Arrange);
    auto& arrange = editor->getArrangeView();
    REQUIRE(arrange.isVisible());

    arrange.select(4);
    REQUIRE(editor->keyPressed(juce::KeyPress('2', {}, '2'))); // MIN
    REQUIRE(editor->keyPressed(juce::KeyPress('d', {}, 'd'))); // E
    REQUIRE(h.processor.getSlot(4) == Slot{E, kMin, dolphinflow::dsp::kDefaultLengthIndex});
    REQUIRE(arrange.getSelected() == 5);
    editor->keyStateChanged(false);

    REQUIRE(editor->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    REQUIRE(arrange.getSelected() == 4);
    auto& cell = arrange.getCell(4);
    const auto up = cell.lengthUpArea().getCentre();
    cell.mouseDown(animalaudio::testing::makeMouseEvent(cell, up));
    REQUIRE(h.processor.getSlot(4).lengthIndex == dolphinflow::dsp::kDefaultLengthIndex + 1);
    REQUIRE(editor->keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    REQUIRE(h.processor.getSlot(4).isEmpty());

    animalaudio::testing::click(arrange.getLoopButton());
    REQUIRE(h.processor.getParameterValue(dolphinflow::ids::loop) > 0.5f);

    // Clicking a root on screen writes too.
    arrange.select(7);
    h.setParam(dolphinflow::ids::chord, 3.0f);
    editor->getRootKey(F).mouseDown(animalaudio::testing::makeMouseEvent(editor->getRootKey(F), {5.0f, 5.0f}));
    editor->getRootKey(F).mouseUp(animalaudio::testing::makeMouseEvent(editor->getRootKey(F), {5.0f, 5.0f}));
    REQUIRE(h.processor.getSlot(7).root == F);
    REQUIRE(h.processor.getSlot(7).shape == 3);

    animalaudio::testing::click(editor->getKeysTab());
    REQUIRE_FALSE(arrange.isVisible());
}

TEST_CASE("dolphinflow: arrange view renders headlessly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    Harness h(kRate, 512);
    h.processor.setSlot(4, {D, kMin9, 5});
    h.processor.setSlot(5, {E, kMin, 1});
    h.setParam(dolphinflow::ids::loop, 1.0f);
    std::unique_ptr<dolphinflow::DolphinFlowEditor> editor(
        static_cast<dolphinflow::DolphinFlowEditor*>(h.processor.createEditor()));
    editor->setView(dolphinflow::DolphinFlowEditor::View::Arrange);
    editor->getArrangeView().select(6);
    for (int frame = 0; frame < 120; ++frame) // 4 s: into the second square
    {
        processBlock(h, 1600);
        editor->refresh();
    }
    REQUIRE(h.processor.getPlayingSlot() == 1);
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    int bright = 0;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            bright += image.getPixelAt(x, y).getBrightness() > 0.55f ? 1 : 0;
    REQUIRE(bright > 400);
    writePngIfRequested(image, "dolphinflow-arrange.png");
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
