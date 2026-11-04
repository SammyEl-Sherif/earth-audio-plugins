#include "PhraseLooper.h"

#include <algorithm>
#include <cmath>

namespace cosmos::dsp
{
namespace
{
constexpr int kMaxCapacity = 1 << 23; // ~87 s at 96 kHz; caps memory at very high rates
}

void PhraseLooper::prepare(double sampleRate)
{
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    capacity = std::min(kMaxCapacity, static_cast<int>(rate * kMaxSeconds));
    for (auto& channel : buffer)
        channel.assign(static_cast<std::size_t>(capacity), 0.0f);
    fadeSamples = std::max(16, static_cast<int>(rate * 0.004));
    gainStep = 1.0f / static_cast<float>(std::max(1, static_cast<int>(rate * 0.006)));
    clear();
}

void PhraseLooper::clear() noexcept
{
    state = State::Empty;
    length = 0;
    head = 0;
    gain = 0.0f;
    clearPending = false;
}

void PhraseLooper::setReverse(bool reverse) noexcept
{
    direction = reverse ? -1 : 1;
}

void PhraseLooper::closeLoop() noexcept
{
    length = head;
    if (length < 4 * fadeSamples)
    {
        clear();
        return;
    }
    // Short fades at the seam so the wrap is click-free in either direction.
    for (int i = 0; i < fadeSamples; ++i)
    {
        const float g = static_cast<float>(i) / static_cast<float>(fadeSamples);
        for (auto& channel : buffer)
        {
            channel[static_cast<std::size_t>(i)] *= g;
            channel[static_cast<std::size_t>(length - 1 - i)] *= g;
        }
    }
    head = direction > 0 ? 0 : length - 1;
    state = State::Playing;
}

void PhraseLooper::command(Command command) noexcept
{
    if (capacity <= 0)
        return;
    switch (command)
    {
    case Command::RecPlayDub:
        clearPending = false;
        if (state == State::Empty)
        {
            head = 0;
            state = State::Recording;
        }
        else if (state == State::Recording)
            closeLoop();
        else if (state == State::Playing)
            state = State::Overdubbing;
        else if (state == State::Overdubbing)
            state = State::Playing;
        else if (state == State::Stopped)
        {
            head = direction > 0 ? 0 : length - 1;
            state = State::Playing;
        }
        break;
    case Command::Stop:
        if (state == State::Recording)
            closeLoop();
        if (state != State::Empty)
            state = State::Stopped;
        break;
    case Command::Clear:
        if (state == State::Recording || gain <= 0.0f)
            clear();
        else
        {
            clearPending = true;
            state = State::Stopped;
        }
        break;
    case Command::None:
        break;
    }
}

void PhraseLooper::process(float inL, float inR, float& outL, float& outR) noexcept
{
    outL = outR = 0.0f;
    if (capacity <= 0 || state == State::Empty)
        return;

    if (state == State::Recording)
    {
        buffer[0][static_cast<std::size_t>(head)] = inL;
        buffer[1][static_cast<std::size_t>(head)] = inR;
        if (++head >= capacity)
            closeLoop();
        return;
    }

    const bool running = state == State::Playing || state == State::Overdubbing;
    gain = running ? std::min(1.0f, gain + gainStep) : std::max(0.0f, gain - gainStep);
    if (!running && gain <= 0.0f)
    {
        if (clearPending)
            clear();
        return;
    }
    if (length <= 0)
        return;

    head = std::clamp(head, 0, length - 1);
    const auto index = static_cast<std::size_t>(head);
    outL = buffer[0][index] * gain;
    outR = buffer[1][index] * gain;
    if (state == State::Overdubbing)
    {
        buffer[0][index] += inL;
        buffer[1][index] += inR;
    }
    head += direction;
    if (head >= length)
        head = 0;
    else if (head < 0)
        head = length - 1;
}
} // namespace cosmos::dsp
