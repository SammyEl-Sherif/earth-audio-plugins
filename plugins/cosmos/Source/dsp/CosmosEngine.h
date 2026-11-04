#pragma once

#include "CosmosModes.h"
#include "PhraseLooper.h"

#include <animalaudio/dsp/FdnReverb.h>
#include <animalaudio/dsp/Smoothing.h>
#include <animalaudio/dsp/Svf.h>
#include <animalaudio/dsp/TempoSync.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace cosmos::dsp
{
/** Everything the engine reads from the parameters, set once per block. */
struct Settings
{
    Mode mode = Mode::Mosaic;
    int variation = 0;     // 0..3
    float activity = 0.5f; // 0..1
    float shape = 0.6f;    // grain envelope: 0 square, 1/3 swell, 2/3 triangle, 1 decay
    float modHz = 1.0f;    // pitch modulation rate
    float modDepth = 0.0f; // 0..1 (1 = +-1 semitone)
    float cutoffHz = 20000.0f;
    float resonance = 0.2f;  // 0..1
    float mix = 0.5f;        // dry / wet, equal power
    float effectGain = 1.0f; // linear wet level
    int time = kDefaultTime; // index into kTimeMultipliers
    bool sync = true;        // follow the host tempo when it reports one
    float tempo = 120.0f;    // own tempo (BPM) when not synced or the host has none
    float repeats = 0.3f;    // 0..1 feedback of the effect into its own input
    float space = 0.3f;      // reverb amount
    float reverbSeconds = 3.0f;
    float loopLevel = 0.8f;
    bool hold = false;
    bool loopReverse = false;
    bool loopPreFx = true;
};

/** Grain envelope as fractions of the grain length. */
struct Envelope
{
    float attack, release;
};
Envelope shapeEnvelope(float shape) noexcept;

/**
 * COSMOS signal path, per sample:
 *
 *   input (+ phrase loop if PRE-FX) -> history (frozen while HOLD) <- REPEATS feedback
 *   history -> grains / taps of the current mode -> EFFECT VOL -> resonant lowpass -> SPACE reverb
 *   dry/wet MIX (+ phrase loop if POST-FX)
 *
 * Grains read the history with cubic interpolation; a clock (host tempo or own tempo, times TIME) triggers
 * the rhythmic modes. Random choices come from a seeded generator and depend only on the sample count,
 * so renders are block-size independent. `prepare` allocates; `process` does not.
 */
class CosmosEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxGrains = 48;
    static constexpr int kMaxTaps = 4;
    static constexpr int kOverviewBins = 160;
    static constexpr double kOverviewSeconds = 4.0;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setSettings(const Settings& settings) noexcept { next = settings; }
    void looperCommand(PhraseLooper::Command command) noexcept { looper.command(command); }

    void process(float* const* channels, int numChannels, int numSamples,
                 const animalaudio::dsp::TransportInfo& transport) noexcept;

    // ---- UI telemetry (relaxed atomics, written once per block) ----
    float overview(int ageBins) const noexcept;
    float grainSecondsAgo(int i) const noexcept { return grainAgo[i].load(std::memory_order_relaxed); }
    float grainSemitones(int i) const noexcept { return grainPitch[i].load(std::memory_order_relaxed); }
    float grainLevel(int i) const noexcept { return grainGain[i].load(std::memory_order_relaxed); }
    int getTapCount() const noexcept { return tapCount.load(std::memory_order_relaxed); }
    float tapSecondsAgo(int i) const noexcept { return tapAgo[i].load(std::memory_order_relaxed); }
    double getBpm() const noexcept { return bpmSeen.load(std::memory_order_relaxed); }
    bool isHostTempo() const noexcept { return hostTempo.load(std::memory_order_relaxed); }
    double getStepPosition() const noexcept { return stepSeen.load(std::memory_order_relaxed); }
    bool isFrozen() const noexcept { return frozenSeen.load(std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load(std::memory_order_relaxed); }
    PhraseLooper::State getLooperState() const noexcept
    {
        return static_cast<PhraseLooper::State>(looperState.load(std::memory_order_relaxed));
    }
    double getLooperSeconds() const noexcept { return looperSeconds.load(std::memory_order_relaxed); }
    float getLooperProgress() const noexcept { return looperProgress.load(std::memory_order_relaxed); }
    double getSampleRate() const noexcept { return sampleRate; }

    /** Test hook: number of grains playing right now. */
    int activeGrains() const noexcept;

private:
    enum class Window : std::uint8_t
    {
        Shape,  // the SHAPE knob's envelope
        Smooth, // symmetric, overlap-adds to 1 at 50% (pitch-shift style grains)
        Gate    // flat with ~2 ms fades (stutters)
    };

    struct Grain
    {
        bool active = false;
        double position = 0.0; // absolute history position (samples)
        double rate = 1.0;     // read speed, negative = reverse
        double glide = 1.0;    // per-sample multiplier applied to rate
        int age = 0;
        int length = 0;
        float invAttack = 0.0f;
        float invRelease = 0.0f;
        float gainL = 0.0f;
        float gainR = 0.0f;
        float semitones = 0.0f;
        Window window = Window::Shape;
    };

    struct Pending
    {
        int countdown = -1;
        double start = 0.0;
        double rate = 1.0;
        double glide = 1.0;
        int length = 0;
        float gain = 0.0f;
        float pan = 0.0f;
        Window window = Window::Shape;
    };

    static constexpr int kMaxPending = 32;

    // events
    void onTick(std::int64_t step) noexcept;
    void onContinuous() noexcept;
    void triggerStrum() noexcept;
    bool spawn(double start, double rate, int length, float gain, float pan, Window window = Window::Shape,
               double glideRatio = 1.0) noexcept;
    void schedule(int delaySamples, double start, double rate, int length, float gain, float pan,
                  Window window = Window::Shape, double glideRatio = 1.0) noexcept;
    void runPending() noexcept;

    // helpers
    double sourceStart(double span, double rate) const noexcept;
    float readHistory(int channel, double position) const noexcept;
    void renderGrains(float& outL, float& outR, double modMultiplier) noexcept;
    void renderTaps(float& outL, float& outR, double modSamples) noexcept;
    void tapLayout(int& count, double* steps) const noexcept;
    void publishTelemetry(int numSamples) noexcept;
    double random01() noexcept;
    int randomInt(int count) noexcept;
    int samples(double seconds) const noexcept { return static_cast<int>(seconds * sampleRate); }

    double sampleRate = 44100.0;
    Settings current;
    Settings next;

    // history (power-of-two ring)
    std::vector<float> history[kMaxChannels];
    std::int64_t mask = 0;
    std::int64_t writeHead = 0; // next sample to write; the newest is writeHead - 1
    std::int64_t filled = 0;
    int maxGrainSamples = 0;

    // hold
    bool frozen = false;
    double anchor = 0.0; // "now" that grains and taps read relative to (loops inside the held region)
    double holdStart = 0.0;
    double holdLength = 0.0;
    double holdOffset = 0.0;

    // clock
    double stepPosition = 0.0;
    std::int64_t lastStep = -1;
    double stepSamples = 22050.0;
    double bpm = 120.0;
    animalaudio::dsp::OnePoleSmoother tapUnit; // delay length unit, glides when the tempo changes

    // grains and events
    Grain grains[kMaxGrains];
    Pending pending[kMaxPending];
    double continuousCountdown = 0.0;
    std::uint32_t rng = 0x9e3779b9u;
    int sequenceIndex = 0;
    int arpIndex = 0;
    bool strumUp = true;
    bool warpPanFlip = false;
    std::int64_t burstUntil = -1;
    std::int64_t lastOnset = 0;
    float envFast = 0.0f;
    float envSlow = 0.0f;
    float onsetFastRelease = 0.0f;
    float onsetSlowCoeff = 0.0f;

    // modulation
    double lfoPhase = 0.0;

    // smoothing / filters
    animalaudio::dsp::OnePoleSmoother mixSmooth, effectSmooth, cutoffSmooth, resonanceSmooth, spaceSmooth, loopSmooth,
        directSmooth, repeatsSmooth, tapsSmooth, tapDuck;
    int duckSamples = 384;
    float directTarget = 0.0f;
    animalaudio::dsp::Svf filter;
    animalaudio::dsp::FdnReverb reverb;
    float dcInL = 0.0f, dcInR = 0.0f, dcOutL = 0.0f, dcOutR = 0.0f;
    std::int64_t sampleCount = 0;

    PhraseLooper looper;

    // telemetry
    std::atomic<float> overviewBins[kOverviewBins] = {};
    std::atomic<int> overviewHead{0};
    int overviewFill = 0;
    int overviewBinSamples = 1;
    float overviewPeak = 0.0f;
    std::atomic<float> grainAgo[kMaxGrains] = {};
    std::atomic<float> grainPitch[kMaxGrains] = {};
    std::atomic<float> grainGain[kMaxGrains] = {};
    std::atomic<int> tapCount{0};
    std::atomic<float> tapAgo[kMaxTaps] = {};
    std::atomic<double> bpmSeen{120.0};
    std::atomic<bool> hostTempo{false};
    std::atomic<double> stepSeen{0.0};
    std::atomic<bool> frozenSeen{false};
    std::atomic<float> outputPeak{0.0f};
    std::atomic<int> looperState{0};
    std::atomic<double> looperSeconds{0.0};
    std::atomic<float> looperProgress{0.0f};
};
} // namespace cosmos::dsp
