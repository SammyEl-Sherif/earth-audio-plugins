#pragma once

#include <vector>

namespace cosmos::dsp
{
/**
 * Stereo phrase looper: record, play, overdub, stop, clear, reverse. One button cycles
 * REC -> PLAY -> DUB -> PLAY like a pedal looper. Starting, stopping and clearing fade over a few ms, and the
 * recorded loop gets short fades at its seam, so nothing clicks. `prepare` allocates; the rest does not.
 */
class PhraseLooper
{
public:
    enum class State : int
    {
        Empty = 0,
        Recording,
        Playing,
        Overdubbing,
        Stopped
    };

    enum class Command : int
    {
        None = 0,
        RecPlayDub,
        Stop,
        Clear
    };

    static constexpr double kMaxSeconds = 60.0;

    void prepare(double sampleRate);
    void clear() noexcept;
    void command(Command command) noexcept;
    void setReverse(bool reverse) noexcept;

    /** Records `in*` as the state requires and returns the loop's playback (silence while recording). */
    void process(float inL, float inR, float& outL, float& outR) noexcept;

    State getState() const noexcept { return state; }
    int getLength() const noexcept { return length; }
    int getHead() const noexcept { return head; }
    int getCapacity() const noexcept { return capacity; }
    bool isReversed() const noexcept { return direction < 0; }

private:
    void closeLoop() noexcept;

    std::vector<float> buffer[2];
    int capacity = 0;
    int length = 0;
    int head = 0;
    int direction = 1;
    int fadeSamples = 64;
    float gain = 0.0f;
    float gainStep = 0.01f;
    bool clearPending = false;
    State state = State::Empty;
};
} // namespace cosmos::dsp
