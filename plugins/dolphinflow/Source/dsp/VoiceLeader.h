#pragma once

#include "Chords.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dolphinflow::dsp
{
inline constexpr int kMinVoices = 3;
inline constexpr int kMaxVoices = 6;
/** Notes stay within this many semitones either side of the register centre. */
inline constexpr int kRangeHalfWidth = 16;

/** One MIDI note per voice. Voice i always keeps index i, so it can glide from its old note to its new one. */
struct Voicing
{
    int count = 0;
    int notes[kMaxVoices] = {};
};

/**
 * Picks where each voice goes for the next chord so the voices move as little as possible (voice leading).
 *
 * Every voice is assigned one chord tone; all tones the voice count allows must appear at least once, and
 * spare voices double whichever tone is nearest. Each voice takes the octave of its tone closest to where it
 * was. Among all assignments the cheapest wins: total semitones moved, plus small penalties for unisons,
 * for a bass that is not the root, for doubling a colour tone, and for drifting from the register centre.
 * Ties go to the first assignment found, so the result is deterministic.
 *
 * Allocation free: at most 5^6 assignments of 6 voices, a fraction of a millisecond, so it runs on the audio
 * thread at each chord change.
 */
class VoiceLeader
{
public:
    /** Which chord tones (indices into the shape) the voices must cover, most important first. */
    struct ToneSet
    {
        int count = 0;
        int pitchClasses[kMaxChordTones] = {};
        bool colour[kMaxChordTones] = {}; // doubling it is discouraged (3rd, 7th, 9th)
    };

    static ToneSet tonesFor(int rootPitchClass, int shapeIndex, int voices) noexcept
    {
        const auto& shape = chordShape(shapeIndex);
        // With fewer voices than tones, the fifth goes first, then the ninth.
        static constexpr int keepOrder[] = {0, 1, 3, 4, 2};
        ToneSet set;
        const int keep = std::min(voices, shape.count);
        for (int index : keepOrder)
        {
            if (set.count == keep)
                break;
            if (index >= shape.count)
                continue;
            set.pitchClasses[set.count] = pitchClassOf(rootPitchClass + shape.intervals[index]);
            set.colour[set.count] = index != 0 && index != 2;
            ++set.count;
        }
        return set;
    }

    /** First chord: lead from voices spread evenly around the centre, which gives an open, centred voicing. */
    static Voicing initial(int rootPitchClass, int shapeIndex, int voices, int centre) noexcept
    {
        Voicing spread;
        spread.count = std::clamp(voices, kMinVoices, kMaxVoices);
        for (int i = 0; i < spread.count; ++i)
            spread.notes[i] = centre - 7 + (14 * i) / (spread.count - 1);
        return lead(spread, rootPitchClass, shapeIndex, voices, centre);
    }

    static Voicing lead(const Voicing& from, int rootPitchClass, int shapeIndex, int voices, int centre) noexcept
    {
        const int count = std::clamp(voices, kMinVoices, kMaxVoices);
        if (from.count != count)
            return initial(rootPitchClass, shapeIndex, count, centre);

        const ToneSet tones = tonesFor(rootPitchClass, shapeIndex, count);
        const int low = centre - kRangeHalfWidth;
        const int high = centre + kRangeHalfWidth;
        const int root = pitchClassOf(rootPitchClass);
        const unsigned allTones = (1u << tones.count) - 1u;

        int choice[kMaxVoices] = {};
        Voicing best;
        best.count = count;
        float bestCost = 1.0e9f;

        for (;;)
        {
            unsigned covered = 0;
            for (int v = 0; v < count; ++v)
                covered |= 1u << choice[v];

            if (covered == allTones)
            {
                Voicing candidate;
                candidate.count = count;
                float cost = 0.0f;
                int sum = 0;
                int lowest = 0;
                int seen[kMaxChordTones] = {};
                for (int v = 0; v < count; ++v)
                {
                    const int note = nearest(from.notes[v], tones.pitchClasses[choice[v]], low, high);
                    candidate.notes[v] = note;
                    cost += static_cast<float>(std::abs(note - from.notes[v]));
                    sum += note;
                    if (v == 0 || note < candidate.notes[lowest])
                        lowest = v;
                    if (seen[choice[v]]++ > 0 && tones.colour[choice[v]])
                        cost += kColourDoublingCost;
                }
                for (int a = 0; a < count; ++a)
                    for (int b = a + 1; b < count; ++b)
                        if (candidate.notes[a] == candidate.notes[b])
                            cost += kUnisonCost;
                if (pitchClassOf(candidate.notes[lowest]) != root)
                    cost += kRootlessBassCost;
                cost += kCentreGravity * std::abs(static_cast<float>(sum - count * centre)) / static_cast<float>(count);

                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = candidate;
                }
            }

            int digit = 0;
            while (digit < count && ++choice[digit] == tones.count)
                choice[digit++] = 0;
            if (digit == count)
                break;
        }
        return best;
    }

    /** The note of `pitchClass` nearest `previous`, folded by octaves into [low, high]. */
    static int nearest(int previous, int pitchClass, int low, int high) noexcept
    {
        int note = previous + ((pitchClass - pitchClassOf(previous) + 18) % 12) - 6;
        while (note < low)
            note += 12;
        while (note > high)
            note -= 12;
        return note;
    }

private:
    static constexpr float kUnisonCost = 3.0f;
    static constexpr float kRootlessBassCost = 1.5f;
    static constexpr float kColourDoublingCost = 0.75f;
    static constexpr float kCentreGravity = 0.6f;
};
} // namespace dolphinflow::dsp
