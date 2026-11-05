#pragma once

#include "VoiceLeader.h"

#include <animalaudio/dsp/FdnReverb.h>
#include <animalaudio/dsp/Smoothing.h>
#include <animalaudio/dsp/Svf.h>

#include <atomic>

namespace dolphinflow::dsp
{
/** How a chord change sounds. Every mode glides; they differ in envelope and whether a change re-strikes. */
enum class Mode : int
{
    Pad = 0, // slow swell, legato: a new chord only glides, the envelope keeps going
    Stab,    // short plucked hit with a filter blip, struck again on every chord
    Keys,    // piano-like decay to a low sustain, struck again on every chord
    Count
};

inline constexpr int kNumModes = static_cast<int>(Mode::Count);
inline constexpr float kMaxGlideMs = 2000.0f;
inline constexpr int kDefaultCentre = 60; // C4, the register the voicings sit around
inline constexpr int kMaxOctaveShift = 2;

struct EngineSettings
{
    Mode mode = Mode::Pad;
    int voices = 4;
    int centre = kDefaultCentre;
    float glideMs = 350.0f;
    float curve = 0.5f;  // 0 = even slide, 1 = fast start that eases into the note
    float tone = 0.55f;  // filter cutoff, 0..1 over 80 Hz..20 kHz
    float width = 0.35f; // detune + stereo spread
    float space = 0.25f; // reverb amount
    float level = 0.7f;
};

/**
 * Paraphonic chord synth. Up to six voices of two detuned band-limited saws each, one shared envelope and
 * low-pass filter, then reverb. On a chord change each voice glides from its note to the note the
 * VoiceLeader picked for it, all voices arriving together after the glide time.
 *
 * `prepare` allocates (reverb lines); everything else is allocation free and realtime safe.
 */
class DolphinFlowEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kControlInterval = 8; // samples between pitch / filter updates

    void prepare(double sampleRate);
    void reset() noexcept;
    void setSettings(const EngineSettings& settings) noexcept;

    /** Starts the chord, or glides to it if one is already sounding. Shape -1 means unnamed (a played voicing). */
    void playChord(int rootPitchClass, int shapeIndex) noexcept;
    /** Plays exactly these notes (lowest first), each voice gliding from its nearest-ranked old note. */
    void playVoicing(const Voicing& notes) noexcept;
    /** Lets the chord ring out with the mode's release. */
    void release() noexcept;
    /** Silences at once (host "all sound off"). */
    void kill() noexcept;

    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    // Read by the UI (any thread).
    bool isGateOn() const noexcept { return gateShown.load(std::memory_order_relaxed); }
    int getRoot() const noexcept { return rootShown.load(std::memory_order_relaxed); }
    int getShape() const noexcept { return shapeShown.load(std::memory_order_relaxed); }
    int getVoiceCount() const noexcept { return voiceCountShown.load(std::memory_order_relaxed); }
    /** Current (gliding) pitch of a voice as a fractional MIDI note, or -1 when silent. */
    float getVoicePitch(int voice) const noexcept;
    float getEnvelopeLevel() const noexcept { return levelShown.load(std::memory_order_relaxed); }

    // Audio-thread view (processor and tests).
    bool isHeld() const noexcept { return gate; }
    const Voicing& getVoicing() const noexcept { return voicing; }
    float currentPitch(int voice) const noexcept { return voices[voice].pitch; }

private:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Release
    };

    struct Voice
    {
        float pitch = 60.0f;
        float start = 60.0f;
        float target = 60.0f;
        float progress = 1.0f;
        float phaseA = 0.0f;
        float phaseB = 0.0f;
        float incA = 0.0f;
        float incB = 0.0f;
    };

    struct Envelope
    {
        float attackStep, decayCoeff, sustain, releaseCoeff, filterOctaves, filterCoeff, q;
        bool restrike;
    };

    void applyVoicing(const Voicing& next, bool glide) noexcept;
    void gateOn() noexcept;
    void strike() noexcept;
    void updateControl() noexcept;
    float nextEnvelope() noexcept;
    void publish() noexcept;
    Envelope envelopeFor(Mode mode) const noexcept;

    double sampleRate = 44100.0;
    EngineSettings settings;
    Envelope envelope{};
    Voice voices[kMaxVoices];
    Voicing voicing;
    int root = 0;
    int shape = 0;
    bool gate = false;
    bool hasChord = false;

    Stage stage = Stage::Idle;
    float level = 0.0f;
    float filterEnv = 0.0f;
    float glideStep = 1.0f; // progress per sample
    int controlCounter = 0;

    animalaudio::dsp::OnePoleSmoother toneSmoother;
    animalaudio::dsp::OnePoleSmoother widthSmoother;
    animalaudio::dsp::OnePoleSmoother spaceSmoother;
    animalaudio::dsp::OnePoleSmoother gainSmoother;
    float panLeft[kMaxVoices] = {};
    float panRight[kMaxVoices] = {};
    float voiceGain = 0.0f;
    float space = 0.0f;
    animalaudio::dsp::Svf filter;
    animalaudio::dsp::FdnReverb reverb;

    std::atomic<bool> gateShown{false};
    std::atomic<int> rootShown{-1};
    std::atomic<int> shapeShown{0};
    std::atomic<int> voiceCountShown{0};
    std::atomic<float> levelShown{0.0f};
    std::atomic<float> pitchShown[kMaxVoices] = {};
};
} // namespace dolphinflow::dsp
