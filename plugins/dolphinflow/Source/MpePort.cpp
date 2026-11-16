#include "MpePort.h"

namespace dolphinflow
{
namespace
{
/** Ports this process has opened, so a second instance gets "Animal Audio - Dolphin Flow 2" rather than a twin. */
std::atomic<int> portsOpened{0};
} // namespace

MpePort::MpePort()
    : juce::Thread("Dolphin Flow MPE port")
{
}

MpePort::~MpePort()
{
    stopThread(1000);
}

void MpePort::open(const juce::String& wanted)
{
    if (output != nullptr)
        return;
    const int number = wanted.isEmpty() ? ++portsOpened : 0;
    const auto name = wanted.isNotEmpty() ? wanted
                      : number == 1       ? juce::String("Animal Audio - Dolphin Flow")
                                          : "Animal Audio - Dolphin Flow " + juce::String(number);
    output = juce::MidiOutput::createNewDevice(name);
    if (output == nullptr)
        return;
    output->setName(name);
    ready.store(true);
    startThread(juce::Thread::Priority::high);
}

juce::String MpePort::getPortName() const
{
    return output != nullptr ? output->getName() : juce::String();
}

void MpePort::queue(const juce::MidiBuffer& buffer, double blockStartMs, double sampleRate) noexcept
{
    if (!ready.load(std::memory_order_acquire) || buffer.isEmpty())
        return;
    const double msPerSample = 1000.0 / std::max(1.0, sampleRate);
    for (const auto metadata : buffer)
    {
        if (metadata.numBytes > 3)
            continue; // notes and pitch bends only
        const auto scope = fifo.write(1);
        Event* slot = scope.blockSize1 > 0   ? &events[static_cast<std::size_t>(scope.startIndex1)]
                      : scope.blockSize2 > 0 ? &events[static_cast<std::size_t>(scope.startIndex2)]
                                             : nullptr;
        if (slot == nullptr)
            return; // full: the sender has stalled, drop rather than block the audio thread
        slot->timeMs = blockStartMs + metadata.samplePosition * msPerSample;
        slot->size = metadata.numBytes;
        std::copy(metadata.data, metadata.data + metadata.numBytes, slot->bytes);
        if (slot->size == 3 && (slot->bytes[0] & 0xf0) == 0x90 && slot->bytes[2] > 0)
            slot->bytes[2] = static_cast<juce::uint8>(portVelocity(slot->bytes[2]));
    }
}

void MpePort::run()
{
    while (!threadShouldExit())
    {
        while (fifo.getNumReady() > 0 && !threadShouldExit())
        {
            const auto scope = fifo.read(1);
            const auto& event =
                events[static_cast<std::size_t>(scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
            const double wait = event.timeMs - juce::Time::getMillisecondCounterHiRes();
            if (wait > 0.5)
                juce::Thread::sleep(static_cast<int>(std::min(wait, 50.0)));
            output->sendMessageNow(juce::MidiMessage(event.bytes, event.size));
        }
        wait(1);
    }
}
} // namespace dolphinflow
