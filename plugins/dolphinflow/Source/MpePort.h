#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>

namespace dolphinflow
{
/**
 * A virtual MIDI port ("Animal Audio - Dolphin Flow") carrying the same notes and glides as the plugin's MIDI out.
 *
 * Why: hosts like Live take only notes from a VST3 plugin's MIDI out and drop its pitch bends, but they record a
 * MIDI input port with MPE turned on (Live: Settings > Link, Tempo & MIDI > the port's Track + MPE) as real MPE
 * notes, every glide shown in the clip's MPE editor. Not every system can make one (Windows can't): then this
 * does nothing and isOpen() is false.
 *
 * The audio thread only queues messages (lock and allocation free); a background thread sends each one when its
 * time comes, so the port's timing follows the audio blocks to about a millisecond.
 */
class MpePort : private juce::Thread
{
public:
    MpePort();
    ~MpePort() override;

    /** Message thread: creates the port (once; later calls do nothing). `name` overrides the numbered default. */
    void open(const juce::String& name = {});
    bool isOpen() const noexcept { return output != nullptr; }
    juce::String getPortName() const;

    /** Audio thread: queues a block's MIDI out, each event at blockStartMs plus its sample offset. */
    void queue(const juce::MidiBuffer& buffer, double blockStartMs, double sampleRate) noexcept;

private:
    struct Event
    {
        double timeMs;
        juce::uint8 bytes[3];
        int size;
    };

    void run() override;

    static constexpr int kCapacity = 4096;
    std::unique_ptr<juce::MidiOutput> output;
    std::array<Event, kCapacity> events{};
    juce::AbstractFifo fifo{kCapacity};
    std::atomic<bool> ready{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MpePort)
};
} // namespace dolphinflow
