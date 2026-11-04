#include "CosmosEngine.h"

#include <animalaudio/dsp/Interpolation.h>

#include <algorithm>
#include <cmath>

namespace cosmos::dsp
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr double kHistorySeconds = 16.0; // 8 s of echoes or a held loop, plus grain reach
constexpr double kMaxGrainSeconds = 6.0;
constexpr double kMaxHoldSeconds = 8.0;
constexpr double kSemitoneDepth = 0.0595; // MOD DEPTH 1 = +-1 semitone
constexpr double kTapModSeconds = 0.003;
constexpr float kTapGains[] = {0.8f, 0.62f, 0.5f, 0.4f};

double semitonesToRate(double semitones) noexcept
{
    return std::exp2(semitones / 12.0);
}

float softClip(float x) noexcept
{
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

float lerp(float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

std::int64_t nextPowerOfTwo(std::int64_t value) noexcept
{
    std::int64_t power = 1;
    while (power < value)
        power <<= 1;
    return power;
}
} // namespace

Envelope shapeEnvelope(float shape) noexcept
{
    // Square -> swell (ramp up) -> triangle -> decay (ramp down), the four icons around the SHAPE knob.
    static constexpr Envelope points[] = {{0.03f, 0.03f}, {0.92f, 0.04f}, {0.5f, 0.5f}, {0.04f, 0.92f}};
    const float scaled = std::clamp(shape, 0.0f, 1.0f) * 3.0f;
    const int index = std::min(2, static_cast<int>(scaled));
    const float t = scaled - static_cast<float>(index);
    return {lerp(points[index].attack, points[index + 1].attack, t),
            lerp(points[index].release, points[index + 1].release, t)};
}

void CosmosEngine::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const std::int64_t capacity = nextPowerOfTwo(static_cast<std::int64_t>(sampleRate * kHistorySeconds));
    mask = capacity - 1;
    for (auto& channel : history)
        channel.assign(static_cast<std::size_t>(capacity), 0.0f);
    maxGrainSamples = static_cast<int>(std::min(sampleRate * kMaxGrainSeconds, static_cast<double>(capacity) / 3.0));

    reverb.prepare(sampleRate);
    looper.prepare(sampleRate);

    mixSmooth.prepare(sampleRate, 15.0);
    effectSmooth.prepare(sampleRate, 15.0);
    cutoffSmooth.prepare(sampleRate, 25.0);
    resonanceSmooth.prepare(sampleRate, 25.0);
    spaceSmooth.prepare(sampleRate, 30.0);
    loopSmooth.prepare(sampleRate, 15.0);
    directSmooth.prepare(sampleRate, 3.0);
    repeatsSmooth.prepare(sampleRate, 30.0);
    tapsSmooth.prepare(sampleRate, 10.0);
    tapDuck.prepare(sampleRate, 1.0);
    tapUnit.prepare(sampleRate, 80.0);

    onsetFastRelease = static_cast<float>(std::exp(-1.0 / (0.030 * sampleRate)));
    onsetSlowCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.120 * sampleRate)));
    overviewBinSamples = std::max(1, static_cast<int>(sampleRate * kOverviewSeconds / kOverviewBins));
    duckSamples = std::max(8, samples(0.008));
    reset();
}

void CosmosEngine::reset() noexcept
{
    current = next;
    for (auto& channel : history)
        std::fill(channel.begin(), channel.end(), 0.0f);
    writeHead = 0;
    filled = 0;
    frozen = false;
    anchor = 0.0;
    holdStart = holdLength = holdOffset = 0.0;
    stepPosition = 0.0;
    lastStep = -1;
    for (auto& grain : grains)
        grain.active = false;
    for (auto& event : pending)
        event.countdown = -1;
    continuousCountdown = 0.0;
    rng = 0x9e3779b9u;
    sequenceIndex = 0;
    arpIndex = 0;
    strumUp = true;
    lastOnset = -(1 << 30);
    burstUntil = -1;
    envFast = envSlow = 0.0f;
    lfoPhase = 0.0;
    filter.reset();
    reverb.reset();
    looper.clear();
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
    sampleCount = 0;

    mixSmooth.reset(current.mix);
    effectSmooth.reset(current.effectGain);
    cutoffSmooth.reset(std::log2(std::max(20.0f, current.cutoffHz)));
    resonanceSmooth.reset(current.resonance);
    spaceSmooth.reset(current.space);
    loopSmooth.reset(current.loopLevel);
    directSmooth.reset(0.0f);
    repeatsSmooth.reset(current.repeats);
    tapsSmooth.reset(current.mode == Mode::Pattern ? 1.0f : 0.0f);
    tapDuck.reset(1.0f);
    stepSamples = sampleRate * 0.5;
    tapUnit.reset(static_cast<float>(stepSamples));

    for (auto& bin : overviewBins)
        bin.store(0.0f, std::memory_order_relaxed);
    overviewHead.store(0, std::memory_order_relaxed);
    overviewFill = 0;
    overviewPeak = 0.0f;
    for (int i = 0; i < kMaxGrains; ++i)
        grainAgo[i].store(-1.0f, std::memory_order_relaxed);
    tapCount.store(0, std::memory_order_relaxed);
}

double CosmosEngine::random01() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<double>(rng) / 4294967296.0;
}

int CosmosEngine::randomInt(int count) noexcept
{
    return count <= 1 ? 0 : std::min(count - 1, static_cast<int>(random01() * count));
}

float CosmosEngine::readHistory(int channel, double position) const noexcept
{
    const auto& buffer = history[channel];
    const double floor = std::floor(position);
    const auto i = static_cast<std::int64_t>(floor);
    const float t = static_cast<float>(position - floor);
    const auto at = [&](std::int64_t index) { return buffer[static_cast<std::size_t>(index & mask)]; };
    return animalaudio::dsp::catmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), t);
}

double CosmosEngine::sourceStart(double span, double rate) const noexcept
{
    return rate >= 0.0 ? anchor - span - 2.0 : anchor - 2.0;
}

bool CosmosEngine::spawn(double start, double rate, int length, float gain, float pan, Window window,
                         double glideRatio) noexcept
{
    if (filled < 64 || !std::isfinite(start) || !std::isfinite(rate))
        return false;
    length = std::clamp(length, 32, maxGrainSamples);

    double mean = 1.0;
    if (std::abs(glideRatio - 1.0) > 1.0e-6)
        mean = (glideRatio - 1.0) / std::log(glideRatio);
    const double travel = rate * static_cast<double>(length) * mean * (1.0 + 2.0 * kSemitoneDepth);
    double low = std::min(start, start + travel);
    double high = std::max(start, start + travel);

    const auto head = static_cast<double>(writeHead);
    const double newest = head - 4.0;
    const double capacity = static_cast<double>(mask + 1);
    const double oldest =
        std::max(head - static_cast<double>(filled) + 4.0, head - capacity + static_cast<double>(length) + 64.0);
    if (high > newest)
    {
        const double shift = high - newest;
        start -= shift;
        low -= shift;
        high -= shift;
    }
    if (low < oldest)
    {
        const double shift = oldest - low;
        if (high + shift > newest)
            return false;
        start += shift;
    }

    for (auto& grain : grains)
    {
        if (grain.active)
            continue;
        grain.active = true;
        grain.position = start;
        grain.rate = rate;
        grain.glide = std::abs(glideRatio - 1.0) > 1.0e-6 ? std::pow(glideRatio, 1.0 / length) : 1.0;
        grain.age = 0;
        grain.length = length;
        grain.window = window;
        float attack = 0.0f, release = 0.0f;
        if (window == Window::Gate)
            attack = release = std::min(static_cast<float>(samples(0.002)), length * 0.25f);
        else
        {
            const auto envelope = shapeEnvelope(current.shape);
            attack = envelope.attack * static_cast<float>(length);
            release = envelope.release * static_cast<float>(length);
        }
        grain.invAttack = 1.0f / std::max(1.0f, attack);
        grain.invRelease = 1.0f / std::max(1.0f, release);
        const float p = std::clamp(pan, -1.0f, 1.0f);
        grain.gainL = gain * std::min(1.0f, 1.0f - p);
        grain.gainR = gain * std::min(1.0f, 1.0f + p);
        grain.semitones = static_cast<float>(12.0 * std::log2(std::max(1.0e-3, std::abs(rate))));
        return true;
    }
    return false;
}

void CosmosEngine::schedule(int delaySamples, double start, double rate, int length, float gain, float pan,
                            Window window, double glideRatio) noexcept
{
    if (delaySamples <= 0)
    {
        spawn(start, rate, length, gain, pan, window, glideRatio);
        return;
    }
    for (auto& event : pending)
    {
        if (event.countdown >= 0)
            continue;
        event = {delaySamples, start, rate, glideRatio, length, gain, pan, window};
        return;
    }
}

void CosmosEngine::runPending() noexcept
{
    for (auto& event : pending)
    {
        if (event.countdown < 0)
            continue;
        if (event.countdown-- == 0)
            spawn(event.start, event.rate, event.length, event.gain, event.pan, event.window, event.glide);
    }
}

void CosmosEngine::triggerStrum() noexcept
{
    static constexpr double chords[kNumVariations][4] = {
        {0.0, 4.0, 7.0, 12.0}, {0.0, 3.0, 7.0, 10.0}, {0.0, 7.0, 12.0, 19.0}, {0.0, 5.0, 10.0, 15.0}};
    const auto& chord = chords[current.variation];
    const int stagger = samples(lerp(0.09f, 0.012f, current.activity));
    const int length = std::clamp(static_cast<int>(stepSamples * 1.5), samples(0.15), samples(3.0));
    for (int k = 0; k < 4; ++k)
    {
        const int note = strumUp ? k : 3 - k;
        const double rate = semitonesToRate(chord[note]);
        const float pan = -0.6f + 0.4f * static_cast<float>(note);
        schedule(k * stagger, sourceStart(length * rate, rate), rate, length, 0.42f, pan);
    }
    strumUp = !strumUp;
}

void CosmosEngine::onTick(std::int64_t step) noexcept
{
    const double s = stepSamples;
    const float a = current.activity;
    const int v = current.variation;

    switch (current.mode)
    {
    case Mode::Mosaic:
    {
        static constexpr double sets[kNumVariations][3] = {
            {1.0, 2.0, 1.0}, {0.5, 1.0, 2.0}, {1.0, 1.5, 2.0}, {-1.0, 1.0, 2.0}};
        const int layers = 1 + static_cast<int>(a * 2.999f);
        const float gain = 0.55f / std::sqrt(static_cast<float>(layers));
        const int length = static_cast<int>(2.0 * s);
        for (int layer = 0; layer < layers; ++layer)
        {
            if (layer > 0 && random01() > 0.5 + 0.5 * a)
                continue;
            const double rate = sets[v][randomInt(3)];
            spawn(sourceStart(length * std::abs(rate), rate), rate, length, gain,
                  static_cast<float>(random01() * 1.4 - 0.7));
        }
        break;
    }
    case Mode::Sequence:
    {
        static constexpr double patterns[kNumVariations][8] = {{0, 12, 7, 12, 0, 12, 7, 12},
                                                               {0, 7, 3, 10, 0, 5, 7, 12},
                                                               {0, -12, 0, 12, 0, -12, 0, 12},
                                                               {0, 5, 7, 12, 7, 5, 0, -5}};
        const double rate = semitonesToRate(patterns[v][sequenceIndex & 7]);
        ++sequenceIndex;
        if (random01() > 0.45 + 0.55 * a)
            break;
        const bool ratchet = a > 0.66f && random01() < (a - 0.66) * 2.5;
        const int length = static_cast<int>(s * (ratchet ? 0.48 : 0.96));
        const double span = std::max(length * rate, s);
        const float pan = (sequenceIndex & 1) ? 0.35f : -0.35f;
        spawn(sourceStart(span, rate), rate, length, 0.8f, pan);
        if (ratchet)
            schedule(static_cast<int>(s * 0.5), sourceStart(span, rate), rate, length, 0.7f, -pan);
        break;
    }
    case Mode::Glide:
    {
        static constexpr double randomTargets[] = {-12.0, -7.0, -5.0, 5.0, 7.0, 12.0};
        double semis = 12.0;
        if (v == 1)
            semis = -12.0;
        else if (v == 2)
            semis = (step & 1) ? -12.0 : 12.0;
        else if (v == 3)
            semis = randomTargets[randomInt(6)];
        const double ratio = semitonesToRate(semis * (0.25 + 0.75 * a));
        const int length = static_cast<int>(2.0 * s);
        const double span = length * (ratio - 1.0) / std::log(ratio);
        spawn(sourceStart(span, 1.0), 1.0, length, 0.55f, static_cast<float>(random01() - 0.5), Window::Shape, ratio);
        break;
    }
    case Mode::Strum:
        if (sampleCount - lastOnset > static_cast<std::int64_t>(2.0 * s) && (step & 1) == 0)
            triggerStrum();
        break;
    case Mode::Blocks:
    {
        int back = 1;
        double rate = 1.0;
        const bool shuffle = random01() < a;
        if (shuffle)
        {
            back = 1 + randomInt(4);
            if (v == 1 && random01() < 0.5)
                rate = -1.0;
            else if (v == 2 && random01() < 0.4)
                rate = random01() < 0.5 ? 0.5 : 2.0;
        }
        const double latest = anchor - (back - 1) * s;
        if (v == 3 && shuffle)
        {
            const int pieces = random01() < 0.5 ? 2 : 4;
            const int length = static_cast<int>(s / pieces);
            for (int k = 0; k < pieces; ++k)
                schedule(k * length, latest - s, 1.0, length, 0.85f, 0.0f);
            break;
        }
        const int length = static_cast<int>(s);
        const double start =
            rate < 0.0 ? latest - 2.0 : latest - std::max(1.0, rate) * s - 2.0 + (rate < 1.0 ? s * 0.5 : 0.0);
        spawn(start, rate, length, 0.85f, 0.0f);
        break;
    }
    case Mode::Interrupt:
    {
        if (random01() > 0.15 + 0.7 * a)
            break;
        burstUntil = sampleCount + static_cast<std::int64_t>(s * 0.95);
        if (v == 3)
        {
            const double ratio = 0.15;
            const int length = static_cast<int>(s * 0.95);
            const double span = length * (ratio - 1.0) / std::log(ratio);
            spawn(sourceStart(span, 1.0), 1.0, length, 0.95f, 0.0f, Window::Gate, ratio);
            break;
        }
        const int pieces = v == 1 ? 6 : 8;
        const int length = std::max(samples(0.01), static_cast<int>(s / pieces));
        for (int k = 0; k < pieces; ++k)
        {
            const double rate = v == 1 ? semitonesToRate(2.0 * k) : v == 2 ? -1.0 : 1.0;
            const double start = rate < 0.0 ? anchor - 2.0 : anchor - length * rate - 2.0;
            schedule(k * length, start, rate, length, 0.95f, 0.0f, Window::Gate);
        }
        break;
    }
    case Mode::Arp:
    {
        static constexpr double patterns[kNumVariations][6] = {
            {0, 4, 7, 12, 4, 7}, {12, 7, 4, 0, 7, 4}, {0, 7, 12, 19, 12, 7}, {0, 3, 7, 10, 12, 15}};
        static constexpr int lengths[kNumVariations] = {4, 4, 6, 6};
        const int notes = 1 + static_cast<int>(a * 3.999f);
        const int length = static_cast<int>(s / notes * 0.95);
        for (int k = 0; k < notes; ++k)
        {
            const double semis = v == 3 ? patterns[v][randomInt(6)] : patterns[v][arpIndex % lengths[v]];
            ++arpIndex;
            const double rate = semitonesToRate(semis);
            const float pan = (arpIndex & 1) ? 0.4f : -0.4f;
            schedule(static_cast<int>(k * s / notes), sourceStart(std::max(s, length * rate), rate), rate, length,
                     0.75f, pan);
        }
        break;
    }
    case Mode::Haze:
    case Mode::Tunnel:
    case Mode::Pattern:
    case Mode::Warp:
    case Mode::Count:
        break;
    }
}

void CosmosEngine::onContinuous() noexcept
{
    const auto mode = current.mode;
    if (mode != Mode::Haze && mode != Mode::Tunnel && mode != Mode::Warp)
    {
        continuousCountdown = 0.0;
        return;
    }
    if (--continuousCountdown > 0.0)
        return;

    const float a = current.activity;
    const int v = current.variation;
    if (mode == Mode::Haze)
    {
        static constexpr float ranges[kNumVariations][2] = {{0.03f, 0.09f}, {0.08f, 0.2f}, {0.2f, 0.5f}, {0.08f, 0.2f}};
        const double interval = lerp(0.15f, 0.010f, std::pow(a, 0.7f)) * (0.5 + random01());
        const double seconds = lerp(ranges[v][0], ranges[v][1], static_cast<float>(random01()));
        double rate = semitonesToRate((random01() * 2.0 - 1.0) * 0.12);
        if (v == 3)
        {
            const double pick = random01();
            rate *= pick < 0.35 ? 2.0 : pick < 0.5 ? 0.5 : 1.0;
        }
        const int length = samples(seconds);
        const float gain = 0.9f / std::sqrt(std::max(1.0f, static_cast<float>(seconds / interval)));
        spawn(sourceStart(length * rate + samples(random01() * 1.5), rate), rate, length, gain,
              static_cast<float>(random01() * 2.0 - 1.0));
        continuousCountdown = interval * sampleRate;
    }
    else if (mode == Mode::Tunnel)
    {
        static constexpr double sets[kNumVariations][3] = {
            {1.0, 0.5, 1.0}, {0.5, 0.25, 0.5}, {1.0, 2.0, 1.0}, {0.5, 1.0, 1.5}};
        const double interval = lerp(0.45f, 0.07f, a) * (0.7 + 0.6 * random01());
        const double seconds = 0.8 + 1.2 * random01();
        const double rate = sets[v][randomInt(3)];
        const int length = samples(seconds);
        const float gain = 0.7f / std::sqrt(std::max(1.0f, static_cast<float>(seconds / interval)));
        spawn(sourceStart(length * rate + samples(0.05 + 0.55 * random01()), rate), rate, length, gain,
              static_cast<float>(random01() * 1.6 - 0.8));
        continuousCountdown = interval * sampleRate;
    }
    else
    {
        static constexpr double rates[kNumVariations] = {1.0, 2.0, 0.5, -1.0};
        const double delay = std::min(stepSamples, sampleRate * kMaxHoldSeconds);
        const int length = static_cast<int>(std::clamp(delay, sampleRate * 0.06, sampleRate * 1.0));
        const double cents = (random01() * 2.0 - 1.0) * 30.0 * a;
        const double rate = rates[v] * semitonesToRate(cents / 100.0);
        double start = anchor - delay - 2.0;
        if (rate > 1.0)
            start -= (rate - 1.0) * length;
        else if (rate < 0.0)
            start = anchor - delay + length;
        spawn(start, rate, length, 1.0f, warpPanFlip ? 0.25f : -0.25f, Window::Smooth);
        warpPanFlip = !warpPanFlip;
        continuousCountdown = length * 0.5;
    }
}

void CosmosEngine::tapLayout(int& count, double* steps) const noexcept
{
    static constexpr double layouts[kNumVariations][kMaxTaps] = {
        {0.75, 1.0, 1.75, 2.0}, {0.5, 1.0, 1.5, 2.0}, {1.0 / 3.0, 2.0 / 3.0, 1.0, 4.0 / 3.0}, {0.25, 0.75, 1.25, 2.0}};
    count = 1 + static_cast<int>(std::lround(current.activity * (kMaxTaps - 1)));
    for (int k = 0; k < kMaxTaps; ++k)
        steps[k] = layouts[current.variation][k];
}

void CosmosEngine::renderTaps(float& outL, float& outR, double modSamples) noexcept
{
    int count = 0;
    double steps[kMaxTaps] = {};
    tapLayout(count, steps);
    const double unit = tapUnit.next();
    const double longest = std::max(8.0, static_cast<double>(filled) - 8.0);
    const float duck = tapDuck.next();
    const float level = tapsSmooth.next() * duck;
    if (level <= 0.0f)
        return;
    for (int k = 0; k < count; ++k)
    {
        const double delay = std::clamp(steps[k] * unit + modSamples * (k + 1) * 0.5, 4.0, longest);
        const double position = anchor - delay;
        const float l = readHistory(0, position);
        const float r = readHistory(1, position);
        const float g = kTapGains[k] * level;
        if (k & 1)
        {
            outL += 0.35f * g * l;
            outR += g * r;
        }
        else
        {
            outL += g * l;
            outR += 0.35f * g * r;
        }
    }
}

void CosmosEngine::renderGrains(float& outL, float& outR, double modMultiplier) noexcept
{
    const double newest = static_cast<double>(writeHead) - 3.0;
    const double oldest = static_cast<double>(writeHead - (mask + 1)) + 8.0;
    for (auto& grain : grains)
    {
        if (!grain.active)
            continue;
        float envelope = 0.0f;
        if (grain.window == Window::Smooth)
        {
            const float tri =
                1.0f - std::abs(2.0f * static_cast<float>(grain.age) / static_cast<float>(grain.length) - 1.0f);
            envelope = tri * tri * (3.0f - 2.0f * tri);
        }
        else
        {
            envelope = std::min({1.0f, static_cast<float>(grain.age) * grain.invAttack,
                                 static_cast<float>(grain.length - grain.age) * grain.invRelease});
            if (grain.window == Window::Shape)
                envelope = envelope * envelope * (3.0f - 2.0f * envelope);
        }
        if (grain.position > newest)
            grain.position = newest;
        if (grain.position < oldest)
        {
            grain.active = false;
            continue;
        }
        outL += readHistory(0, grain.position) * envelope * grain.gainL;
        outR += readHistory(1, grain.position) * envelope * grain.gainR;
        grain.position += grain.rate * modMultiplier;
        grain.rate *= grain.glide;
        if (++grain.age >= grain.length)
            grain.active = false;
    }
}

int CosmosEngine::activeGrains() const noexcept
{
    int count = 0;
    for (const auto& grain : grains)
        count += grain.active ? 1 : 0;
    return count;
}

float CosmosEngine::overview(int ageBins) const noexcept
{
    const int head = overviewHead.load(std::memory_order_relaxed);
    const int index = ((head - 1 - ageBins) % kOverviewBins + kOverviewBins) % kOverviewBins;
    return overviewBins[index].load(std::memory_order_relaxed);
}

void CosmosEngine::process(float* const* channels, int numChannels, int numSamples,
                           const animalaudio::dsp::TransportInfo& transport) noexcept
{
    if (history[0].empty() || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    current = next;
    current.variation = std::clamp(current.variation, 0, kNumVariations - 1);
    current.activity = std::clamp(current.activity, 0.0f, 1.0f);
    const auto& s = current;
    const bool stereo = numChannels > 1;

    // Clock
    const bool hostBpm = s.sync && transport.hasBpm && transport.bpm > 1.0 && std::isfinite(transport.bpm);
    bpm = std::clamp(s.sync ? animalaudio::dsp::bpmOr(transport, s.tempo) : static_cast<double>(s.tempo), 20.0, 400.0);
    const double multiplier = kTimeMultipliers[std::clamp(s.time, 0, kNumTimes - 1)];
    stepSamples = std::clamp(sampleRate * 60.0 / (bpm * multiplier), sampleRate * 0.02, sampleRate * kMaxHoldSeconds);
    const double stepIncrement = 1.0 / stepSamples;
    if (s.sync && transport.playing && transport.hasPpq && std::isfinite(transport.ppq))
    {
        const double target = transport.ppq * multiplier;
        if (std::abs(target - stepPosition) > 0.01)
            stepPosition = target;
    }

    // Targets
    mixSmooth.setTarget(std::clamp(s.mix, 0.0f, 1.0f));
    effectSmooth.setTarget(std::max(0.0f, s.effectGain));
    cutoffSmooth.setTarget(std::log2(std::clamp(s.cutoffHz, 20.0f, 22000.0f)));
    resonanceSmooth.setTarget(std::clamp(s.resonance, 0.0f, 1.0f));
    spaceSmooth.setTarget(std::clamp(s.space, 0.0f, 1.0f));
    loopSmooth.setTarget(std::clamp(s.loopLevel, 0.0f, 1.0f));
    repeatsSmooth.setTarget(std::clamp(s.repeats, 0.0f, 1.0f));
    tapsSmooth.setTarget(s.mode == Mode::Pattern ? 1.0f : 0.0f);
    tapUnit.setTarget(static_cast<float>(stepSamples));
    looper.setReverse(s.loopReverse);
    reverb.setDecaySeconds(s.reverbSeconds);
    reverb.setDamping(0.35f);
    const double lfoIncrement = std::clamp(static_cast<double>(s.modHz), 0.01, 40.0) / sampleRate;
    const double modDepth = std::clamp(static_cast<double>(s.modDepth), 0.0, 1.0);
    const bool strum = s.mode == Mode::Strum;
    const bool interrupt = s.mode == Mode::Interrupt;
    const std::int64_t onsetGap = samples(0.15);
    // Taps already scale the echo; normalise so REPEATS means the same regeneration with one tap or four.
    float feedbackScale = 1.0f;
    if (s.mode == Mode::Pattern)
    {
        int count = 0;
        double steps[kMaxTaps] = {};
        tapLayout(count, steps);
        float sum = 0.0f;
        for (int k = 0; k < count; ++k)
            sum += kTapGains[k];
        feedbackScale = 1.0f / sum;
    }

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = channels[0][i];
        const float inR = stereo ? channels[1][i] : inL;

        // Phrase looper before the effect: the loop plays into it.
        float srcL = inL, srcR = inR;
        const float loopLevel = loopSmooth.next();
        if (s.loopPreFx)
        {
            float playL = 0.0f, playR = 0.0f;
            looper.process(inL, inR, playL, playR);
            srcL += loopLevel * playL;
            srcR += loopLevel * playR;
        }

        if (strum)
        {
            const float x = std::max(std::abs(srcL), std::abs(srcR));
            envFast = std::max(x, envFast * onsetFastRelease);
            envSlow += (x - envSlow) * onsetSlowCoeff;
            if (envFast > 2.2f * envSlow + 0.01f && envFast > 0.03f && sampleCount - lastOnset > onsetGap)
            {
                lastOnset = sampleCount;
                triggerStrum();
            }
        }

        // Hold: switch only once the taps have faded out, and fade them around each loop of the held region.
        const bool wantFrozen = s.hold && filled > samples(0.05);
        const bool switching = wantFrozen != frozen;
        const bool nearWrap = frozen && holdLength - holdOffset < duckSamples;
        tapDuck.setTarget(switching || nearWrap ? 0.0f : 1.0f);
        if (switching && (tapDuck.getCurrent() < 0.002f || tapsSmooth.getCurrent() < 0.002f))
        {
            frozen = wantFrozen;
            if (frozen)
            {
                holdLength = std::clamp(2.0 * stepSamples, sampleRate * 0.25,
                                        std::min(sampleRate * kMaxHoldSeconds, static_cast<double>(filled) - 16.0));
                holdStart = static_cast<double>(writeHead) - holdLength;
                holdOffset = 0.0;
            }
        }
        if (frozen)
        {
            holdOffset += 1.0;
            if (holdOffset >= holdLength)
                holdOffset -= holdLength;
            anchor = holdStart + holdOffset;
        }
        else
        {
            anchor = static_cast<double>(writeHead);
        }

        // Events
        stepPosition += stepIncrement;
        const auto step = static_cast<std::int64_t>(std::floor(stepPosition));
        if (step != lastStep)
        {
            lastStep = step;
            onTick(step);
        }
        onContinuous();
        runPending();

        // Pitch modulation
        lfoPhase += lfoIncrement;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        const double lfo = std::sin(kTwoPi * lfoPhase) * modDepth;

        float wetL = 0.0f, wetR = 0.0f;
        renderGrains(wetL, wetR, 1.0 + kSemitoneDepth * lfo);
        if (tapsSmooth.getCurrent() > 0.0f || tapsSmooth.getTarget() > 0.0f)
            renderTaps(wetL, wetR, lfo * kTapModSeconds * sampleRate);
        else
            tapDuck.next();

        directSmooth.setTarget(interrupt && sampleCount >= burstUntil ? 1.0f : 0.0f);
        const float direct = directSmooth.next();
        wetL += direct * srcL;
        wetR += direct * srcR;

        const float effect = effectSmooth.next();
        wetL *= effect;
        wetR *= effect;

        // Resonant lowpass; coefficients follow the smoothed knobs every 16 samples.
        const float cutoff = cutoffSmooth.next();
        const float resonance = resonanceSmooth.next();
        const float q = 0.6f + 9.0f * resonance * resonance;
        if ((sampleCount & 15) == 0)
            filter.setCoefficients(std::exp2(static_cast<double>(cutoff)), q, sampleRate);
        // Resonance trades passband for peak (as analog ladders do), so the peak stays near +10 dB.
        const float resonanceTrim = 1.0f / std::sqrt(std::max(1.0f, q));
        const float filteredL = filter.lowpass(0, wetL * resonanceTrim);
        const float filteredR = filter.lowpass(1, wetR * resonanceTrim);

        // REPEATS: the filtered effect feeds back into the history (DC-blocked, soft-clipped).
        const float feedback = repeatsSmooth.next() * 0.93f * feedbackScale;
        if (!frozen)
        {
            dcOutL = filteredL - dcInL + 0.995f * dcOutL;
            dcInL = filteredL;
            dcOutR = filteredR - dcInR + 0.995f * dcOutR;
            dcInR = filteredR;
            const float writeL = srcL + softClip(dcOutL * feedback);
            const float writeR = srcR + softClip(dcOutR * feedback);
            const auto index = static_cast<std::size_t>(writeHead & mask);
            history[0][index] = writeL;
            history[1][index] = writeR;
            ++writeHead;
            filled = std::min(filled + 1, mask + 1);

            overviewPeak = std::max(overviewPeak, std::max(std::abs(writeL), std::abs(writeR)));
            if (++overviewFill >= overviewBinSamples)
            {
                const int head = overviewHead.load(std::memory_order_relaxed);
                overviewBins[head].store(overviewPeak, std::memory_order_relaxed);
                overviewHead.store((head + 1) % kOverviewBins, std::memory_order_relaxed);
                overviewFill = 0;
                overviewPeak = 0.0f;
            }
        }

        // SPACE
        float roomL = 0.0f, roomR = 0.0f;
        reverb.process(filteredL, filteredR, roomL, roomR);
        const float space = spaceSmooth.next();
        // A soft ceiling at +6 dB keeps extreme settings (resonance, +12 dB, long feedback) from running away.
        const float effectL = 2.0f * softClip(0.5f * (filteredL * (1.0f - 0.4f * space) + roomL * space * 1.1f));
        const float effectR = 2.0f * softClip(0.5f * (filteredR * (1.0f - 0.4f * space) + roomR * space * 1.1f));

        const float mix = mixSmooth.next();
        const float dryGain = std::sqrt(1.0f - mix);
        const float wetGain = std::sqrt(mix);
        float outL = srcL * dryGain + effectL * wetGain;
        float outR = srcR * dryGain + effectR * wetGain;

        if (!s.loopPreFx)
        {
            float playL = 0.0f, playR = 0.0f;
            looper.process(outL, outR, playL, playR);
            outL += loopLevel * playL;
            outR += loopLevel * playR;
        }

        if (stereo)
        {
            channels[0][i] = outL;
            channels[1][i] = outR;
        }
        else
        {
            channels[0][i] = 0.5f * (outL + outR);
        }
        peak = std::max(peak, std::max(std::abs(outL), std::abs(outR)));
        ++sampleCount;
    }

    for (int c = 2; c < numChannels; ++c)
        for (int i = 0; i < numSamples; ++i)
            channels[c][i] = 0.0f;

    // Telemetry
    for (int k = 0; k < kMaxGrains; ++k)
    {
        const auto& grain = grains[k];
        if (!grain.active)
        {
            grainAgo[k].store(-1.0f, std::memory_order_relaxed);
            continue;
        }
        const float phase = static_cast<float>(grain.age) / static_cast<float>(std::max(1, grain.length));
        grainAgo[k].store(static_cast<float>((static_cast<double>(writeHead) - grain.position) / sampleRate),
                          std::memory_order_relaxed);
        grainPitch[k].store(grain.semitones, std::memory_order_relaxed);
        grainGain[k].store(0.5f * (grain.gainL + grain.gainR) * std::sin(3.14159265f * phase),
                           std::memory_order_relaxed);
    }
    if (s.mode == Mode::Pattern)
    {
        int count = 0;
        double steps[kMaxTaps] = {};
        tapLayout(count, steps);
        const double behind = (static_cast<double>(writeHead) - anchor) / sampleRate;
        for (int k = 0; k < kMaxTaps; ++k)
            tapAgo[k].store(static_cast<float>(behind + steps[k] * tapUnit.getCurrent() / sampleRate),
                            std::memory_order_relaxed);
        tapCount.store(count, std::memory_order_relaxed);
    }
    else
    {
        tapCount.store(0, std::memory_order_relaxed);
    }
    bpmSeen.store(bpm, std::memory_order_relaxed);
    hostTempo.store(hostBpm, std::memory_order_relaxed);
    stepSeen.store(stepPosition, std::memory_order_relaxed);
    frozenSeen.store(frozen, std::memory_order_relaxed);
    outputPeak.store(peak, std::memory_order_relaxed);
    const auto state = looper.getState();
    looperState.store(static_cast<int>(state), std::memory_order_relaxed);
    if (state == PhraseLooper::State::Recording)
    {
        looperSeconds.store(looper.getHead() / sampleRate, std::memory_order_relaxed);
        looperProgress.store(static_cast<float>(looper.getHead()) / static_cast<float>(looper.getCapacity()),
                             std::memory_order_relaxed);
    }
    else
    {
        const int length = looper.getLength();
        looperSeconds.store(length / sampleRate, std::memory_order_relaxed);
        looperProgress.store(length > 0 ? static_cast<float>(looper.getHead()) / static_cast<float>(length) : 0.0f,
                             std::memory_order_relaxed);
    }
}
} // namespace cosmos::dsp
