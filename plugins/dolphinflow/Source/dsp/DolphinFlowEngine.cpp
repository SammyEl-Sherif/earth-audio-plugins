#include "DolphinFlowEngine.h"

#include <algorithm>
#include <cmath>

namespace dolphinflow::dsp
{
namespace
{
constexpr float kPi = 3.14159265358979f;

/** Band-limiting correction for a saw's reset at phase 0 (two-sample polynomial BLEP). */
inline float polyBlep(float phase, float increment) noexcept
{
    if (phase < increment)
    {
        const float t = phase / increment;
        return t + t - t * t - 1.0f;
    }
    if (phase > 1.0f - increment)
    {
        const float t = (phase - 1.0f) / increment;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

inline float saw(float& phase, float increment) noexcept
{
    const float out = 2.0f * phase - 1.0f - polyBlep(phase, increment);
    phase += increment;
    if (phase >= 1.0f)
        phase -= 1.0f;
    return out;
}

/** Glide shape: 0 is an even slide, 1 a fast start that eases into the note. */
inline float glideShape(float t, float curve) noexcept
{
    const float u = 1.0f - t;
    return t + curve * ((1.0f - u * u * u) - t);
}

/** Per-sample coefficient that gets an exponential segment to ~1% in `seconds`. */
inline float decayCoefficient(double seconds, double sampleRate) noexcept
{
    const double samples = std::max(1.0, seconds * sampleRate);
    return static_cast<float>(std::exp(-4.6 / samples));
}
} // namespace

void DolphinFlowEngine::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reverb.prepare(sampleRate);
    const double controlRate = sampleRate / kControlInterval;
    toneSmoother.prepare(controlRate, 30.0);
    widthSmoother.prepare(controlRate, 40.0);
    spaceSmoother.prepare(controlRate, 40.0);
    gainSmoother.prepare(sampleRate, 20.0);
    setSettings(settings);
    reset();
}

void DolphinFlowEngine::reset() noexcept
{
    stage = Stage::Idle;
    level = 0.0f;
    filterEnv = 0.0f;
    gate = false;
    hasChord = false;
    controlCounter = 0;
    for (auto& voice : voices)
        voice = Voice{};
    filter.reset();
    reverb.reset();
    toneSmoother.reset(settings.tone);
    widthSmoother.reset(settings.width);
    spaceSmoother.reset(settings.space);
    gainSmoother.reset(settings.level * settings.level * 1.4f);
    space = settings.space;
    updateControl();
    controlCounter = 0;
    publish();
}

DolphinFlowEngine::Envelope DolphinFlowEngine::envelopeFor(Mode mode) const noexcept
{
    struct Times
    {
        double attack, decay;
        float sustain;
        double release;
        float filterOctaves;
        double filterDecay;
        float q;
        bool restrike;
    };
    static constexpr Times times[kNumModes] = {
        {0.45, 0.5, 1.0f, 1.8, 0.0f, 0.5, 0.8f, false},    // Pad
        {0.003, 0.28, 0.0f, 0.15, 3.0f, 0.16, 1.3f, true}, // Stab
        {0.004, 1.4, 0.3f, 0.6, 1.5f, 0.5, 0.9f, true},    // Keys
    };
    const auto& t = times[std::clamp(static_cast<int>(mode), 0, kNumModes - 1)];
    return {static_cast<float>(1.0 / std::max(1.0, t.attack * sampleRate)),
            decayCoefficient(t.decay, sampleRate),
            t.sustain,
            decayCoefficient(t.release, sampleRate),
            t.filterOctaves,
            decayCoefficient(t.filterDecay, sampleRate),
            t.q,
            t.restrike};
}

void DolphinFlowEngine::setSettings(const EngineSettings& next) noexcept
{
    EngineSettings clamped = next;
    clamped.voices = std::clamp(next.voices, kMinVoices, kMaxVoices);
    clamped.glideMs = std::clamp(next.glideMs, 0.0f, kMaxGlideMs);
    clamped.curve = std::clamp(next.curve, 0.0f, 1.0f);

    // A played voicing (shape -1) keeps its own notes; only named chords are re-voiced.
    const bool revoice =
        hasChord && gate && shape >= 0 && (clamped.voices != settings.voices || clamped.centre != settings.centre);
    registerShift += clamped.centre - settings.centre;
    settings = clamped;
    envelope = envelopeFor(settings.mode);
    const double glideSamples = settings.glideMs * 0.001 * sampleRate;
    glideStep = glideSamples >= 1.0 ? static_cast<float>(1.0 / glideSamples) : 1.0f;

    toneSmoother.setTarget(settings.tone);
    widthSmoother.setTarget(settings.width);
    spaceSmoother.setTarget(settings.space);
    gainSmoother.setTarget(settings.level * settings.level * 1.4f);
    reverb.setDecaySeconds(1.2f + 4.0f * settings.space);
    reverb.setDamping(0.45f);

    if (revoice)
        applyVoicing(VoiceLeader::lead(leadFrom(), root, shape, settings.voices, settings.centre), true);
}

Voicing DolphinFlowEngine::leadFrom() noexcept
{
    // An octave change moves the whole chord by that octave, rather than leading to the nearest notes in range.
    Voicing from = voicing;
    for (int v = 0; v < from.count; ++v)
        from.notes[v] = std::clamp(from.notes[v] + registerShift, 0, 127);
    registerShift = 0;
    return from;
}

void DolphinFlowEngine::playChord(int rootPitchClass, int shapeIndex) noexcept
{
    root = pitchClassOf(rootPitchClass);
    shape = std::clamp(shapeIndex, 0, kNumChordShapes - 1);

    // Glide only from notes still sounding; after silence the chord simply starts on its notes.
    const bool sounding = hasChord && stage != Stage::Idle;
    const Voicing next = sounding ? VoiceLeader::lead(leadFrom(), root, shape, settings.voices, settings.centre)
                                  : VoiceLeader::initial(root, shape, settings.voices, settings.centre);
    applyVoicing(next, sounding);
    gateOn();
}

void DolphinFlowEngine::playChordVoiced(const Voicing& next, int rootPitchClass, int shapeIndex) noexcept
{
    if (next.count < kMinVoices)
    {
        playChord(rootPitchClass, shapeIndex);
        return;
    }
    root = pitchClassOf(rootPitchClass);
    shape = std::clamp(shapeIndex, 0, kNumChordShapes - 1);
    applyVoicing(next, hasChord && stage != Stage::Idle);
    gateOn();
}

void DolphinFlowEngine::playVoicing(const Voicing& notes) noexcept
{
    Voicing next;
    next.count = std::clamp(notes.count, 1, kMaxVoices);
    std::copy(notes.notes, notes.notes + next.count, next.notes);
    std::sort(next.notes, next.notes + next.count);
    identifyChord(next.notes, next.count, root, shape);

    const bool sounding = hasChord && stage != Stage::Idle;
    if (sounding && voicing.count == next.count)
    {
        // Rank the voices by where they are now and hand them the new notes in the same order: no crossing.
        int order[kMaxVoices] = {};
        for (int v = 0; v < next.count; ++v)
            order[v] = v;
        std::sort(order, order + next.count, [this](int a, int b) { return voices[a].pitch < voices[b].pitch; });
        Voicing ranked;
        ranked.count = next.count;
        for (int i = 0; i < next.count; ++i)
            ranked.notes[order[i]] = next.notes[i];
        next = ranked;
    }
    applyVoicing(next, sounding);
    gateOn();
}

void DolphinFlowEngine::gateOn() noexcept
{
    hasChord = true;
    if (!gate || envelope.restrike)
        strike();
    gate = true;
}

void DolphinFlowEngine::release() noexcept
{
    gate = false;
    if (stage != Stage::Idle)
        stage = Stage::Release;
}

void DolphinFlowEngine::kill() noexcept
{
    gate = false;
    stage = Stage::Idle;
    level = 0.0f;
    filterEnv = 0.0f;
    hasChord = false;
}

void DolphinFlowEngine::strike() noexcept
{
    // Attack rises from wherever the level is, so re-striking a ringing chord does not click.
    stage = Stage::Attack;
    filterEnv = 1.0f;
}

void DolphinFlowEngine::applyVoicing(const Voicing& next, bool glide) noexcept
{
    for (int v = 0; v < next.count; ++v)
    {
        auto& voice = voices[v];
        const auto target = static_cast<float>(next.notes[v]);
        const bool wasPlaying = glide && v < voicing.count;
        voice.start = wasPlaying ? voice.pitch : target;
        voice.pitch = voice.start;
        voice.target = target;
        voice.progress = wasPlaying && glideStep < 1.0f ? 0.0f : 1.0f;
        if (voice.progress >= 1.0f)
            voice.pitch = target;
        if (!wasPlaying)
        {
            // Spread the starting phases so a fresh chord does not start with every saw in step.
            voice.phaseA = 0.13f * static_cast<float>(v);
            voice.phaseB = 0.57f + 0.21f * static_cast<float>(v);
            voice.phaseB -= std::floor(voice.phaseB);
        }
    }
    voicing = next;
    registerShift = 0;
    voiceGain = 0.32f / std::sqrt(static_cast<float>(voicing.count));
}

void DolphinFlowEngine::updateControl() noexcept
{
    const float tone = toneSmoother.next();
    const float width = widthSmoother.next();
    space = spaceSmoother.next();

    const double up = std::exp2((0.03 + 0.22 * width) * 0.5 / 12.0); // half the A/B detune, in pitch ratio
    const float advance = glideStep * static_cast<float>(kControlInterval);
    for (int v = 0; v < voicing.count; ++v)
    {
        auto& voice = voices[v];
        if (voice.progress < 1.0f)
        {
            voice.progress = std::min(1.0f, voice.progress + advance);
            voice.pitch = voice.start + (voice.target - voice.start) * glideShape(voice.progress, settings.curve);
        }
        const double hz = 440.0 * std::exp2((static_cast<double>(voice.pitch) - 69.0) / 12.0);
        voice.incA = static_cast<float>(std::min(0.45, hz * up / sampleRate));
        voice.incB = static_cast<float>(std::min(0.45, hz / up / sampleRate));

        // Equal-power pan, voices fanned across the field by WIDTH; unity gain at centre.
        const float spread =
            voicing.count > 1 ? 2.0f * static_cast<float>(v) / static_cast<float>(voicing.count - 1) - 1.0f : 0.0f;
        const float angle = kPi * (0.25f + 0.25f * spread * width);
        panLeft[v] = std::cos(angle) * 1.41421356f;
        panRight[v] = std::sin(angle) * 1.41421356f;
    }

    const double cutoff = 80.0 * std::exp2(8.0 * tone + envelope.filterOctaves * filterEnv);
    filter.setCoefficients(cutoff, envelope.q, sampleRate);
}

float DolphinFlowEngine::nextEnvelope() noexcept
{
    switch (stage)
    {
    case Stage::Attack:
        level += envelope.attackStep;
        if (level >= 1.0f)
        {
            level = 1.0f;
            stage = Stage::Decay;
        }
        break;
    case Stage::Decay:
        level = envelope.sustain + (level - envelope.sustain) * envelope.decayCoeff;
        break;
    case Stage::Release:
        level *= envelope.releaseCoeff;
        if (level < 1.0e-4f) // -80 dB
        {
            level = 0.0f;
            stage = Stage::Idle;
        }
        break;
    case Stage::Idle:
        level = 0.0f;
        break;
    }
    filterEnv *= envelope.filterCoeff;
    return level;
}

void DolphinFlowEngine::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp(numChannels, 0, kMaxChannels);
    if (numChannels == 0)
        return;
    float* left = channels[0];
    float* right = numChannels > 1 ? channels[1] : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        if (controlCounter++ % kControlInterval == 0)
            updateControl();

        float dryL = 0.0f;
        float dryR = 0.0f;
        const float env = nextEnvelope();
        if (stage != Stage::Idle || env > 0.0f)
        {
            float sumL = 0.0f;
            float sumR = 0.0f;
            for (int v = 0; v < voicing.count; ++v)
            {
                auto& voice = voices[v];
                const float s = 0.5f * (saw(voice.phaseA, voice.incA) + saw(voice.phaseB, voice.incB));
                sumL += s * panLeft[v];
                sumR += s * panRight[v];
            }
            const float gain = env * voiceGain;
            dryL = filter.lowpass(0, sumL * gain);
            dryR = filter.lowpass(1, sumR * gain);
        }

        float wetL = 0.0f;
        float wetR = 0.0f;
        reverb.process(dryL, dryR, wetL, wetR);
        const float master = gainSmoother.next();
        const float outL = (dryL * (1.0f - 0.3f * space) + wetL * space * 0.9f) * master;
        const float outR = (dryR * (1.0f - 0.3f * space) + wetR * space * 0.9f) * master;

        if (right != nullptr)
        {
            left[i] = outL;
            right[i] = outR;
        }
        else
        {
            left[i] = 0.5f * (outL + outR);
        }
    }
    publish();
}

float DolphinFlowEngine::getVoicePitch(int voice) const noexcept
{
    if (voice < 0 || voice >= kMaxVoices)
        return -1.0f;
    return pitchShown[voice].load(std::memory_order_relaxed);
}

void DolphinFlowEngine::publish() noexcept
{
    const bool audible = stage != Stage::Idle;
    gateShown.store(gate, std::memory_order_relaxed);
    rootShown.store(hasChord ? root : -1, std::memory_order_relaxed);
    shapeShown.store(shape, std::memory_order_relaxed);
    voiceCountShown.store(voicing.count, std::memory_order_relaxed);
    levelShown.store(level, std::memory_order_relaxed);
    for (int v = 0; v < kMaxVoices; ++v)
        pitchShown[v].store(audible && v < voicing.count ? voices[v].pitch : -1.0f, std::memory_order_relaxed);
}
} // namespace dolphinflow::dsp
