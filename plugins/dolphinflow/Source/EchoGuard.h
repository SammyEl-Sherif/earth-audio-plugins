#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace dolphinflow
{
namespace detail
{
struct EchoRing
{
    static constexpr std::uint32_t kSize = 1024;
    std::array<std::atomic<std::uint64_t>, kSize> slots{};
    std::atomic<std::uint32_t> head{0};
};
inline EchoRing echoRing; // one per process, shared by every instance
} // namespace detail

/**
 * Remembers the note-ons every Dolphin Flow in this process sent to its MPE port, so one coming back in is not
 * played again. Without this, an armed track on All Ins hears the port and feeds itself (each echo a new press,
 * each press more notes) until the notes pile up and never stop.
 *
 * Live gives a plugin that is not MPE enabled all its input on channel 1, so echoes are matched by note and the
 * port's marked velocity (MpePort::portVelocity, never the played velocity), not by channel. Shared by every
 * instance, so two instances on All Ins do not ping-pong. Lock and allocation free; any thread.
 */
class EchoGuard
{
public:
    static constexpr std::uint32_t kWindowMs = 250; // how late an echo may come back

    static void remember(int note, int velocity, std::uint32_t timeMs) noexcept
    {
        const auto index = ring().head.fetch_add(1, std::memory_order_relaxed) % detail::EchoRing::kSize;
        ring().slots[index].store(pack(note, velocity, timeMs), std::memory_order_relaxed);
    }

    static bool isEcho(int note, int velocity, std::uint32_t nowMs) noexcept
    {
        const auto wanted = key(note, velocity);
        for (const auto& slot : ring().slots)
        {
            const auto entry = slot.load(std::memory_order_relaxed);
            if (entry == 0 || (entry & 0xffffu) != wanted)
                continue;
            const auto sentMs = static_cast<std::uint32_t>(entry >> 32);
            if (nowMs - sentMs <= kWindowMs || sentMs - nowMs <= kWindowMs) // either order: queued slightly ahead
                return true;
        }
        return false;
    }

private:
    static detail::EchoRing& ring() noexcept { return detail::echoRing; }

    static std::uint64_t key(int note, int velocity) noexcept
    {
        return 0x8000u | (static_cast<std::uint64_t>(note & 0x7f) << 7) | static_cast<std::uint64_t>(velocity & 0x7f);
    }

    static std::uint64_t pack(int note, int velocity, std::uint32_t timeMs) noexcept
    {
        return (static_cast<std::uint64_t>(timeMs) << 32) | key(note, velocity);
    }
};
} // namespace dolphinflow
