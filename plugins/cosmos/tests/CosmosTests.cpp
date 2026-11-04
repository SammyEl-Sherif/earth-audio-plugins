#include "CosmosEditor.h"
#include "CosmosProcessor.h"
#include "dsp/CosmosEngine.h"
#include "dsp/PhraseLooper.h"

#include <animalaudio/core/BuildStamp.h>
#include <animalaudio/testing/RealMouseSuite.h>
#include <animalaudio/testing/SignalProbe.h>
#include <animalaudio/testing/TestKit.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using animalaudio::testing::kPi;
using animalaudio::testing::rms;
using animalaudio::testing::writePngIfRequested;
using cosmos::dsp::CosmosEngine;
using cosmos::dsp::Mode;
using cosmos::dsp::PhraseLooper;
using cosmos::dsp::Settings;

namespace
{
constexpr double kRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

/** A plucked chord: decaying harmonic notes every 0.5 s, so the effects have attacks and pitch to work on. */
Stereo plucks(double seconds, double rate = kRate)
{
    const auto count = static_cast<std::size_t>(seconds * rate);
    Stereo out{std::vector<float>(count), std::vector<float>(count)};
    static constexpr double notes[] = {220.0, 277.18, 329.63, 440.0};
    for (std::size_t i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const int index = static_cast<int>(t / 0.5);
        const double local = t - index * 0.5;
        const double f = notes[index % 4];
        const double env = std::exp(-local * 6.0) * std::min(1.0, local * 400.0);
        const double v = env * (std::sin(2.0 * kPi * f * t) + 0.3 * std::sin(4.0 * kPi * f * t));
        out.left[i] = static_cast<float>(0.4 * v);
        out.right[i] = static_cast<float>(0.4 * v * 0.9);
    }
    return out;
}

Stereo silence(double seconds, double rate = kRate)
{
    const auto count = static_cast<std::size_t>(seconds * rate);
    return {std::vector<float>(count), std::vector<float>(count)};
}

Stereo render(CosmosEngine& engine, const Settings& settings, Stereo in, int blockSize = 512,
              animalaudio::dsp::TransportInfo transport = {})
{
    engine.setSettings(settings);
    const auto count = static_cast<int>(in.left.size());
    for (int done = 0; done < count; done += blockSize)
    {
        const int n = std::min(blockSize, count - done);
        float* channels[2] = {in.left.data() + done, in.right.data() + done};
        engine.process(channels, 2, n, transport);
    }
    return in;
}

Settings wetSettings(Mode mode, int variation = 0)
{
    Settings s;
    s.mode = mode;
    s.variation = variation;
    s.mix = 1.0f;
    s.space = 0.0f;
    s.repeats = 0.0f;
    s.sync = false;
    s.tempo = 120.0f;
    return s;
}

float peakOf(const Stereo& s)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < s.left.size(); ++i)
        peak = std::max({peak, std::abs(s.left[i]), std::abs(s.right[i])});
    return peak;
}

bool allFinite(const Stereo& s)
{
    for (std::size_t i = 0; i < s.left.size(); ++i)
        if (!std::isfinite(s.left[i]) || !std::isfinite(s.right[i]))
            return false;
    return true;
}
} // namespace

TEST_CASE("cosmos: shape envelopes run square, swell, triangle, decay")
{
    const auto square = cosmos::dsp::shapeEnvelope(0.0f);
    const auto swell = cosmos::dsp::shapeEnvelope(1.0f / 3.0f);
    const auto triangle = cosmos::dsp::shapeEnvelope(2.0f / 3.0f);
    const auto decay = cosmos::dsp::shapeEnvelope(1.0f);
    REQUIRE(square.attack < 0.05f);
    REQUIRE(square.release < 0.05f);
    REQUIRE(swell.attack > 0.85f);
    REQUIRE(triangle.attack == Catch::Approx(0.5f).margin(0.01));
    REQUIRE(triangle.release == Catch::Approx(0.5f).margin(0.01));
    REQUIRE(decay.release > 0.85f);
}

TEST_CASE("cosmos: every effect and variation makes sound and stays bounded, even with feedback flat out")
{
    const int mode = GENERATE(Catch::Generators::range(0, cosmos::dsp::kNumModes));
    const int variation = GENERATE(Catch::Generators::range(0, cosmos::dsp::kNumVariations));
    INFO(cosmos::dsp::modeName(cosmos::dsp::modeFromIndex(mode)) << " " << variation);

    CosmosEngine engine;
    auto quiet = wetSettings(cosmos::dsp::modeFromIndex(mode), variation);
    quiet.activity = 1.0f;
    engine.setSettings(quiet);
    engine.prepare(kRate);
    const auto wet = render(engine, quiet, plucks(3.0));
    REQUIRE(allFinite(wet));
    REQUIRE(rms(wet.left, 48000, wet.left.size()) > 0.01);

    auto loud = quiet;
    loud.repeats = 1.0f;
    loud.effectGain = 4.0f;
    loud.resonance = 1.0f;
    loud.cutoffHz = 800.0f;
    loud.space = 1.0f;
    loud.reverbSeconds = 20.0f;
    loud.modDepth = 1.0f;
    loud.modHz = 7.0f;
    const auto hot = render(engine, loud, plucks(6.0));
    REQUIRE(allFinite(hot));
    REQUIRE(peakOf(hot) < 3.0f); // input peaks at 0.4; the wet path tops out near +6 dB
}

TEST_CASE("cosmos: mix 0 passes the input untouched")
{
    CosmosEngine engine;
    auto s = wetSettings(Mode::Haze);
    s.mix = 0.0f;
    s.repeats = 0.8f;
    s.space = 0.8f;
    engine.setSettings(s);
    engine.prepare(kRate);
    const auto in = plucks(1.5);
    const auto out = render(engine, s, in);
    for (std::size_t i = 0; i < in.left.size(); ++i)
    {
        REQUIRE(out.left[i] == in.left[i]);
        REQUIRE(out.right[i] == in.right[i]);
    }
}

TEST_CASE("cosmos: silence in, silence out")
{
    CosmosEngine engine;
    auto s = wetSettings(Mode::Mosaic);
    s.space = 0.6f;
    s.repeats = 0.9f;
    engine.setSettings(s);
    engine.prepare(kRate);
    const auto out = render(engine, s, silence(2.0));
    REQUIRE(peakOf(out) == 0.0f);
}

TEST_CASE("cosmos: block size does not change the render")
{
    const auto mode =
        GENERATE(Mode::Mosaic, Mode::Sequence, Mode::Haze, Mode::Strum, Mode::Interrupt, Mode::Pattern, Mode::Warp);
    INFO(cosmos::dsp::modeName(mode));
    auto s = wetSettings(mode, 1);
    s.activity = 0.8f;
    s.repeats = 0.5f;
    s.space = 0.4f;
    s.modDepth = 0.5f;
    s.cutoffHz = 3000.0f;
    s.resonance = 0.5f;

    const auto in = plucks(2.5);
    CosmosEngine reference;
    reference.setSettings(s);
    reference.prepare(kRate);
    const auto expected = render(reference, s, in, 512);

    for (const int block : {1, 37, 256, 1024})
    {
        CosmosEngine engine;
        engine.setSettings(s);
        engine.prepare(kRate);
        const auto out = render(engine, s, in, block);
        float worst = 0.0f;
        for (std::size_t i = 0; i < in.left.size(); ++i)
            worst = std::max(worst, std::abs(out.left[i] - expected.left[i]));
        INFO("block " << block);
        REQUIRE(worst < 1.0e-5f);
    }
}

TEST_CASE("cosmos: PATTERN echoes land on the tap times")
{
    // 120 BPM, 1X: one step is 0.5 s. Variation B with ACTIVITY 0 is a single tap at half a step.
    CosmosEngine engine;
    auto s = wetSettings(Mode::Pattern, 1);
    s.activity = 0.0f;
    engine.setSettings(s);
    engine.prepare(kRate);
    auto in = silence(1.0);
    for (int i = 0; i < 48; ++i)
        in.left[static_cast<std::size_t>(9600 + i)] = in.right[static_cast<std::size_t>(9600 + i)] = 0.5f;
    const auto out = render(engine, s, in);

    std::size_t loudest = 0;
    for (std::size_t i = 0; i < out.left.size(); ++i)
        if (std::abs(out.left[i]) > std::abs(out.left[loudest]))
            loudest = i;
    const double delaySeconds = (static_cast<double>(loudest) - 9600.0) / kRate;
    REQUIRE(delaySeconds == Catch::Approx(0.25).margin(0.003));
    REQUIRE(engine.getTapCount() == 1);
}

TEST_CASE("cosmos: REPEATS make echoes regenerate")
{
    const auto tail = [](float repeats)
    {
        CosmosEngine engine;
        auto s = wetSettings(Mode::Pattern, 1);
        s.activity = 0.0f;
        s.repeats = repeats;
        engine.setSettings(s);
        engine.prepare(kRate);
        auto in = plucks(0.4);
        in.left.resize(static_cast<std::size_t>(kRate * 3.0));
        in.right.resize(in.left.size());
        const auto out = render(engine, s, in);
        return rms(out.left, static_cast<std::size_t>(kRate * 2.0), out.left.size());
    };
    REQUIRE(tail(0.0f) < 1.0e-4);
    REQUIRE(tail(0.8f) > 1.0e-3);
}

TEST_CASE("cosmos: HOLD keeps the effect playing after the input stops")
{
    const auto lastSecond = [](bool hold)
    {
        CosmosEngine engine;
        auto s = wetSettings(Mode::Haze);
        s.activity = 0.7f;
        engine.setSettings(s);
        engine.prepare(kRate);
        render(engine, s, plucks(1.5));
        s.hold = hold;
        const auto out = render(engine, s, silence(3.0));
        REQUIRE(engine.isFrozen() == hold);
        return rms(out.left, static_cast<std::size_t>(kRate * 2.0), out.left.size());
    };
    REQUIRE(lastSecond(false) < 1.0e-4);
    REQUIRE(lastSecond(true) > 0.01);
}

TEST_CASE("cosmos: the clock follows the host tempo only while synced")
{
    CosmosEngine engine;
    auto s = wetSettings(Mode::Sequence);
    s.tempo = 90.0f;
    engine.setSettings(s);
    engine.prepare(kRate);
    animalaudio::dsp::TransportInfo host;
    host.hasBpm = true;
    host.bpm = 140.0;
    render(engine, s, silence(0.1), 512, host);
    REQUIRE(engine.getBpm() == Catch::Approx(90.0));
    REQUIRE_FALSE(engine.isHostTempo());

    s.sync = true;
    render(engine, s, silence(0.1), 512, host);
    REQUIRE(engine.getBpm() == Catch::Approx(140.0));
    REQUIRE(engine.isHostTempo());

    render(engine, s, silence(0.1), 512, {});
    REQUIRE(engine.getBpm() == Catch::Approx(90.0));
}

TEST_CASE("cosmos: the clock locks to the host position while playing")
{
    CosmosEngine engine;
    auto s = wetSettings(Mode::Sequence);
    s.sync = true;
    s.time = 3; // 2X: two steps per beat
    engine.setSettings(s);
    engine.prepare(kRate);
    animalaudio::dsp::TransportInfo host;
    host.playing = true;
    host.hasBpm = true;
    host.bpm = 120.0;
    host.hasPpq = true;
    host.ppq = 8.25;
    render(engine, s, silence(512.0 / kRate), 512, host);
    const double expected = 8.25 * 2.0 + 512.0 * 2.0 * 2.0 / kRate;
    REQUIRE(engine.getStepPosition() == Catch::Approx(expected).margin(1.0e-6));
}

TEST_CASE("cosmos: phrase looper records, plays, overdubs, reverses and clears")
{
    PhraseLooper looper;
    looper.prepare(kRate);
    const auto play = [&](float in, float& out)
    {
        float l = 0.0f, r = 0.0f;
        looper.process(in, -in, l, r);
        REQUIRE(r == Catch::Approx(-l).margin(1.0e-6));
        out = l;
    };
    float out = 0.0f;
    play(1.0f, out);
    REQUIRE(out == 0.0f); // empty: nothing to play

    constexpr int length = 24000;
    looper.command(PhraseLooper::Command::RecPlayDub);
    REQUIRE(looper.getState() == PhraseLooper::State::Recording);
    for (int i = 0; i < length; ++i)
    {
        play(static_cast<float>(std::sin(i * 0.01)), out);
        REQUIRE(out == 0.0f);
    }
    looper.command(PhraseLooper::Command::RecPlayDub);
    REQUIRE(looper.getState() == PhraseLooper::State::Playing);
    REQUIRE(looper.getLength() == length);

    // Second pass (fade-in of the gain is over): the loop plays back what was recorded.
    for (int i = 0; i < length; ++i)
        play(0.0f, out);
    for (int i = 0; i < length; ++i)
    {
        play(0.0f, out);
        if (i > 400 && i < length - 400)
            REQUIRE(out == Catch::Approx(std::sin(i * 0.01)).margin(1.0e-5));
    }

    looper.command(PhraseLooper::Command::RecPlayDub);
    REQUIRE(looper.getState() == PhraseLooper::State::Overdubbing);
    for (int i = 0; i < length; ++i)
        play(0.25f, out);
    looper.command(PhraseLooper::Command::RecPlayDub);
    for (int i = 0; i < length; ++i)
    {
        play(0.0f, out);
        if (i > 400 && i < length - 400)
            REQUIRE(out == Catch::Approx(std::sin(i * 0.01) + 0.25).margin(1.0e-5));
    }

    looper.setReverse(true);
    float previous = 0.0f;
    play(0.0f, previous);
    for (int i = 0; i < 200; ++i)
    {
        play(0.0f, out);
        REQUIRE(std::abs(out - previous) < 0.03f); // reversing keeps the read position: no jump
        previous = out;
    }

    looper.command(PhraseLooper::Command::Stop);
    float largestStep = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        play(0.0f, out);
        largestStep = std::max(largestStep, std::abs(out - previous));
        previous = out;
    }
    REQUIRE(out == 0.0f);
    REQUIRE(largestStep < 0.05f);

    looper.command(PhraseLooper::Command::Clear);
    REQUIRE(looper.getState() == PhraseLooper::State::Empty);
}

TEST_CASE("cosmos: looper pre-FX feeds the effect, post-FX adds to the output")
{
    for (const bool preFx : {true, false})
    {
        INFO("pre-fx " << preFx);
        CosmosEngine engine;
        auto s = wetSettings(Mode::Haze);
        s.mix = 0.0f; // dry only: a pre-FX loop is heard through the dry path, a post-FX one on top
        s.loopPreFx = preFx;
        s.loopLevel = 1.0f;
        engine.setSettings(s);
        engine.prepare(kRate);
        engine.looperCommand(PhraseLooper::Command::RecPlayDub);
        render(engine, s, plucks(1.0));
        engine.looperCommand(PhraseLooper::Command::RecPlayDub);
        const auto out = render(engine, s, silence(1.5));
        REQUIRE(engine.getLooperState() == PhraseLooper::State::Playing);
        REQUIRE(engine.getLooperSeconds() == Catch::Approx(1.0).margin(0.02));
        REQUIRE(rms(out.left, 0, out.left.size()) > 0.02);
    }
}

TEST_CASE("cosmos: parameters exist with their defaults")
{
    cosmos::CosmosProcessor processor;
    const auto get = [&](const juce::ParameterID& id)
    {
        const auto* param = processor.apvts.getParameter(id.getParamID());
        REQUIRE(param != nullptr);
        return param->convertFrom0to1(param->getValue());
    };
    REQUIRE(get(cosmos::ids::mode) == 0.0f);
    REQUIRE(get(cosmos::ids::variation) == 0.0f);
    REQUIRE(get(cosmos::ids::mix) == Catch::Approx(0.5f));
    REQUIRE(get(cosmos::ids::time) == 2.0f);
    REQUIRE(get(cosmos::ids::sync) == 1.0f);
    REQUIRE(get(cosmos::ids::filter) == Catch::Approx(20000.0f));
    REQUIRE(get(cosmos::ids::hold) == 0.0f);
    REQUIRE(get(cosmos::ids::loopPreFx) == 1.0f);
    REQUIRE(processor.getNumPrograms() == 44);
    REQUIRE(processor.getProgramName(0) == "Mosaic A");
    REQUIRE(processor.getProgramName(43) == "Warp D");
}

TEST_CASE("cosmos: host programs pick effect and variation")
{
    cosmos::CosmosProcessor processor;
    processor.setCurrentProgram(4 * 4 + 2); // Tunnel C
    REQUIRE(processor.getMode() == Mode::Tunnel);
    REQUIRE(processor.getCurrentProgram() == 18);
    REQUIRE(processor.getProgramName(processor.getCurrentProgram()) == "Tunnel C");
}

TEST_CASE("cosmos: state round trip")
{
    juce::MemoryBlock saved;
    {
        cosmos::CosmosProcessor processor;
        const auto set = [&](const juce::ParameterID& id, float v)
        {
            auto* p = processor.apvts.getParameter(id.getParamID());
            p->setValueNotifyingHost(p->convertTo0to1(v));
        };
        set(cosmos::ids::mode, 9.0f);
        set(cosmos::ids::variation, 3.0f);
        set(cosmos::ids::activity, 0.2f);
        set(cosmos::ids::tempo, 97.5f);
        set(cosmos::ids::hold, 1.0f);
        set(cosmos::ids::reverbTime, 7.0f);
        processor.getStateInformation(saved);
    }
    cosmos::CosmosProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const auto get = [&](const juce::ParameterID& id)
    {
        const auto* p = restored.apvts.getParameter(id.getParamID());
        return p->convertFrom0to1(p->getValue());
    };
    REQUIRE(restored.getMode() == Mode::Pattern);
    REQUIRE(get(cosmos::ids::variation) == 3.0f);
    REQUIRE(get(cosmos::ids::activity) == Catch::Approx(0.2f).margin(0.001));
    REQUIRE(get(cosmos::ids::tempo) == Catch::Approx(97.5f).margin(0.05));
    REQUIRE(get(cosmos::ids::hold) == 1.0f);
    REQUIRE(get(cosmos::ids::reverbTime) == Catch::Approx(7.0f).margin(0.01));

    const char garbage[] = "not a state";
    restored.setStateInformation(garbage, sizeof(garbage));
    REQUIRE(restored.getMode() == Mode::Pattern);
}

TEST_CASE("cosmos: processor runs mono and odd block sizes")
{
    cosmos::CosmosProcessor processor;
    processor.setPlayConfigDetails(1, 1, kRate, 128);
    processor.prepareToPlay(kRate, 128);
    juce::MidiBuffer midi;
    const auto in = plucks(0.5);
    for (int done = 0, n = 1; done + n <= static_cast<int>(in.left.size()); done += n, n = n % 300 + 7)
    {
        juce::AudioBuffer<float> block(1, n);
        block.copyFrom(0, 0, in.left.data() + done, n);
        processor.processBlock(block, midi);
        for (int i = 0; i < n; ++i)
            REQUIRE(std::isfinite(block.getSample(0, i)));
    }
}

TEST_CASE("cosmos: editor renders headlessly")
{
    juce::ScopedJuceInitialiser_GUI gui;
    animalaudio::testing::ProcessorHarness<cosmos::CosmosProcessor> harness(kRate, 512);
    harness.setParam(cosmos::ids::mode, 0.0f);
    harness.setParam(cosmos::ids::variation, 1.0f);
    harness.setParam(cosmos::ids::activity, 0.85f);
    harness.setParam(cosmos::ids::repeats, 0.45f);
    harness.setParam(cosmos::ids::space, 0.4f);
    harness.setParam(cosmos::ids::filter, 9000.0f);
    harness.setParam(cosmos::ids::modDepth, 0.2f);
    harness.setParam(cosmos::ids::time, 3.0f);

    const auto in = plucks(4.2);
    harness.processor.sendLooperCommand(PhraseLooper::Command::RecPlayDub);
    juce::AudioBuffer<float> block(2, 512);
    for (std::size_t done = 0; done + 512 <= in.left.size(); done += 512)
    {
        if (done == 512 * 140)
            harness.processor.sendLooperCommand(PhraseLooper::Command::RecPlayDub);
        block.copyFrom(0, 0, in.left.data() + done, 512);
        block.copyFrom(1, 0, in.right.data() + done, 512);
        harness.processor.processBlock(block, harness.midi);
    }

    std::unique_ptr<cosmos::CosmosEditor> editor(static_cast<cosmos::CosmosEditor*>(harness.processor.createEditor()));
    for (int i = 0; i < 40; ++i)
        editor->refresh();
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    REQUIRE(image.getWidth() == cosmos::CosmosEditor::kWidth * 2);
    REQUIRE(image.getHeight() == cosmos::CosmosEditor::kHeight * 2);

    int bright = 0;
    int grainPixels = 0;
    const auto orange = cosmos::ui::familyColour(cosmos::dsp::Family::MicroLoop);
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
        {
            const auto pixel = image.getPixelAt(x, y);
            bright += pixel.getBrightness() > 0.6f ? 1 : 0;
            const bool inField = x > 2 * 360 && x < 2 * 720 && y > 2 * 172 && y < 2 * 338;
            grainPixels += inField && animalaudio::testing::realmouse::colourDistance(pixel, orange) < 0.35f ? 1 : 0;
        }
    REQUIRE(bright > 400);
    REQUIRE(grainPixels > 20); // micro-loop grains drawn in the field
    writePngIfRequested(image, "cosmos-ui.png");
}

TEST_CASE("cosmos: editor controls drive their parameters")
{
    juce::ScopedJuceInitialiser_GUI gui;
    cosmos::CosmosProcessor processor;
    processor.setPlayConfigDetails(2, 2, kRate, 512);
    processor.prepareToPlay(kRate, 512);
    std::unique_ptr<cosmos::CosmosEditor> editor(static_cast<cosmos::CosmosEditor*>(processor.createEditor()));
    const auto get = [&](const juce::ParameterID& id)
    {
        const auto* param = processor.apvts.getParameter(id.getParamID());
        return param->convertFrom0to1(param->getValue());
    };

    animalaudio::testing::click(editor->getModeButton(Mode::Strum));
    REQUIRE(processor.getMode() == Mode::Strum);
    REQUIRE(editor->getModeButton(Mode::Strum).getToggleState());
    REQUIRE_FALSE(editor->getModeButton(Mode::Mosaic).getToggleState());

    animalaudio::testing::click(editor->getVariationPills().getPill(2));
    REQUIRE(get(cosmos::ids::variation) == 2.0f);

    animalaudio::testing::click(editor->getHoldButton());
    REQUIRE(get(cosmos::ids::hold) == 1.0f);
    animalaudio::testing::click(editor->getHoldButton());
    REQUIRE(get(cosmos::ids::hold) == 0.0f);

    editor->getTimeKnob().setValue(5.0, juce::sendNotificationSync);
    REQUIRE(get(cosmos::ids::time) == 5.0f);

    animalaudio::testing::click(editor->getTapButton());
    juce::Thread::sleep(400);
    animalaudio::testing::click(editor->getTapButton());
    REQUIRE(get(cosmos::ids::sync) == 0.0f);
    REQUIRE(get(cosmos::ids::tempo) > 60.0f);
    REQUIRE(get(cosmos::ids::tempo) < 240.0f);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block(2, 512);
    block.clear();
    animalaudio::testing::click(editor->getRecordButton());
    for (int i = 0; i < 20; ++i)
        processor.processBlock(block, midi);
    REQUIRE(processor.getEngine().getLooperState() == PhraseLooper::State::Recording);
    editor->refresh();
    REQUIRE(editor->getRecordButton().getButtonText() == "PLAY");
    animalaudio::testing::click(editor->getRecordButton());
    for (int i = 0; i < 10; ++i)
        processor.processBlock(block, midi);
    REQUIRE(processor.getEngine().getLooperState() == PhraseLooper::State::Playing);
    animalaudio::testing::click(editor->getClearButton());
    processor.processBlock(block, midi);
    for (int i = 0; i < 10; ++i)
        processor.processBlock(block, midi);
    REQUIRE(processor.getEngine().getLooperState() == PhraseLooper::State::Empty);

    auto* mode = processor.apvts.getParameter(cosmos::ids::mode.getParamID());
    mode->setValueNotifyingHost(mode->convertTo0to1(9.0f));
    REQUIRE(editor->getModeButton(Mode::Pattern).getToggleState());
}

TEST_CASE("cosmos: real mouse: effect buttons and HOLD respond to clicks at any host scale")
{
    using namespace animalaudio::testing::realmouse;
    if (!animalaudio::testing::prepareNativeWindows())
        SKIP("no display available");
    juce::ScopedJuceInitialiser_GUI gui;

    for (const auto& scenario : scenarios())
    {
        INFO(describe(scenario).toStdString());
        cosmos::CosmosProcessor processor;
        DesktopEditor<cosmos::CosmosEditor> window(processor, scenario.kind, scenario.scale);
        auto& editor = *window.editor;
        for (const auto mode : {Mode::Haze, Mode::Warp, Mode::Blocks, Mode::Mosaic, Mode::Arp})
        {
            window.clickCentreOf(editor.getModeButton(mode));
            window.parkMouse();
            REQUIRE(processor.getMode() == mode);
            REQUIRE(editor.getModeButton(mode).getToggleState());
        }
        window.clickCentreOf(editor.getHoldButton());
        window.parkMouse();
        REQUIRE(processor.apvts.getRawParameterValue("hold")->load() > 0.5f);
        window.clickCentreOf(editor.getHoldButton());
        window.parkMouse();
        REQUIRE(processor.apvts.getRawParameterValue("hold")->load() < 0.5f);
    }
}

TEST_CASE("cosmos: build stamp is baked in")
{
    REQUIRE(animalaudio::core::buildStampShort().isNotEmpty());
}

TEST_CASE("cosmos: cpu cost", "[.bench]")
{
    for (const auto mode : {Mode::Haze, Mode::Tunnel, Mode::Pattern, Mode::Mosaic})
    {
        CosmosEngine engine;
        auto s = wetSettings(mode, 3);
        s.activity = 1.0f;
        s.space = 0.5f;
        s.modDepth = 0.5f;
        engine.setSettings(s);
        engine.prepare(kRate);
        const auto in = plucks(10.0);
        const auto start = std::chrono::steady_clock::now();
        render(engine, s, in);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        WARN(cosmos::dsp::modeName(mode) << ": " << 100.0 * seconds / 10.0 << "% of one core, " << engine.activeGrains()
                                         << " grains");
    }
}
