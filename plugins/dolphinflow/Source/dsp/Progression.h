#pragma once

#include "VoiceLeader.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dolphinflow::dsp
{
inline constexpr int kNumSlots = 8;

/** Lengths a slot can take, in quarter-note beats. */
inline constexpr double kSlotBeats[] = {1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0};
inline constexpr int kNumSlotLengths = static_cast<int>(sizeof(kSlotBeats) / sizeof(kSlotBeats[0]));
inline constexpr int kDefaultLengthIndex = 3; // one 4/4 bar

/** One square of the arrangement: a chord and how long it plays. root -1 = empty (skipped). */
struct Slot
{
    int root = -1;
    int shape = 0;
    int lengthIndex = kDefaultLengthIndex;

    bool isEmpty() const noexcept { return root < 0; }
    double beats() const noexcept { return kSlotBeats[std::clamp(lengthIndex, 0, kNumSlotLengths - 1)]; }
    bool operator==(const Slot& o) const noexcept
    {
        return root == o.root && shape == o.shape && lengthIndex == o.lengthIndex;
    }
    bool operator!=(const Slot& o) const noexcept { return !(*this == o); }

    /** Packed into one int so the audio thread can read a slot through a single atomic. */
    int pack() const noexcept { return (root < 0 ? 15 : root) | (shape << 4) | (lengthIndex << 8); }
    static Slot unpack(int packed) noexcept
    {
        Slot s;
        const int r = packed & 15;
        s.root = r > 11 ? -1 : r;
        s.shape = std::clamp((packed >> 4) & 15, 0, kNumChordShapes - 1);
        s.lengthIndex = std::clamp((packed >> 8) & 15, 0, kNumSlotLengths - 1);
        return s;
    }
};

using Slots = Slot[kNumSlots];

/** "1 BEAT", "2 BEATS", "1 BAR", "1.5 BARS", "4 BARS" (4/4). */
inline const char* slotLengthLabel(int lengthIndex) noexcept
{
    static constexpr const char* labels[] = {"1 BEAT",   "2 BEATS", "3 BEATS", "1 BAR",
                                             "1.5 BARS", "2 BARS",  "3 BARS",  "4 BARS"};
    return labels[std::clamp(lengthIndex, 0, kNumSlotLengths - 1)];
}

inline double loopBeats(const Slots& slots) noexcept
{
    double total = 0.0;
    for (const auto& s : slots)
        if (!s.isEmpty())
            total += s.beats();
    return total;
}

/** Where `beats` (any value, wraps) falls in the loop: which slot, and beats into / left in it. */
struct LoopPosition
{
    int slot = -1;
    double into = 0.0;
    double left = 0.0;
};

inline LoopPosition locate(const Slots& slots, double beats) noexcept
{
    const double total = loopBeats(slots);
    LoopPosition at;
    if (total <= 0.0)
        return at;
    double local = std::fmod(beats, total);
    if (local < 0.0)
        local += total;
    double start = 0.0;
    for (int i = 0; i < kNumSlots; ++i)
    {
        if (slots[i].isEmpty())
            continue;
        const double end = start + slots[i].beats();
        if (local < end)
        {
            at.slot = i;
            at.into = local - start;
            at.left = end - local;
            return at;
        }
        start = end;
    }
    return at; // only reached through rounding at the very end of the loop
}

/**
 * The voicing slot `target` has once the loop has gone round: led through the whole progression from a fresh first
 * chord, then on to `target`. Starting playback here makes the first pass sound like every later pass (and like the
 * exported clip). Allocation free.
 */
inline Voicing settledVoicing(const Slots& slots, int target, int voices, int centre) noexcept
{
    int first = -1;
    for (int i = 0; i < kNumSlots && first < 0; ++i)
        if (!slots[i].isEmpty())
            first = i;
    if (first < 0 || target < 0 || target >= kNumSlots || slots[target].isEmpty())
        return {};
    Voicing voicing = VoiceLeader::initial(slots[first].root, slots[first].shape, voices, centre);
    for (int i = first + 1; i < kNumSlots; ++i)
        if (!slots[i].isEmpty())
            voicing = VoiceLeader::lead(voicing, slots[i].root, slots[i].shape, voices, centre);
    for (int i = 0; i <= target; ++i)
        if (!slots[i].isEmpty())
            voicing = VoiceLeader::lead(voicing, slots[i].root, slots[i].shape, voices, centre);
    return voicing;
}

/** A note of an exported clip, in beats from the clip start. */
struct ClipNote
{
    int note;
    double start;
    double end;
};

/**
 * The notes one pass of the loop plays, voiced exactly as the engine voices them while looping: led through the
 * progression once to settle, then recorded on the second pass, so the clip's first chord follows on from its last.
 * Notes shared by consecutive chords are tied into one. Message thread only (allocates).
 */
inline std::vector<ClipNote> buildClip(const Slots& slots, int voices, int centre)
{
    std::vector<ClipNote> notes;
    int order[kNumSlots] = {};
    int count = 0;
    for (int i = 0; i < kNumSlots; ++i)
        if (!slots[i].isEmpty())
            order[count++] = i;
    if (count == 0)
        return notes;

    Voicing voicing = VoiceLeader::initial(slots[order[0]].root, slots[order[0]].shape, voices, centre);
    for (int i = 1; i < count; ++i)
        voicing = VoiceLeader::lead(voicing, slots[order[i]].root, slots[order[i]].shape, voices, centre);

    std::vector<std::size_t> open; // indices into `notes` still sounding
    double time = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto& slot = slots[order[i]];
        voicing = VoiceLeader::lead(voicing, slot.root, slot.shape, voices, centre);
        std::vector<int> wanted(voicing.notes, voicing.notes + voicing.count);
        std::sort(wanted.begin(), wanted.end());
        wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

        std::vector<std::size_t> stillOpen;
        for (auto index : open)
        {
            auto& n = notes[index];
            if (std::find(wanted.begin(), wanted.end(), n.note) != wanted.end())
            {
                stillOpen.push_back(index);
                wanted.erase(std::find(wanted.begin(), wanted.end(), n.note));
            }
            else
            {
                n.end = time;
            }
        }
        for (int note : wanted)
        {
            stillOpen.push_back(notes.size());
            notes.push_back({note, time, time});
        }
        open = stillOpen;
        time += slot.beats();
    }
    for (auto index : open)
        notes[index].end = time;
    return notes;
}
} // namespace dolphinflow::dsp
