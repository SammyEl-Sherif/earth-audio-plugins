#pragma once

#include <algorithm>

namespace dolphinflow::dsp
{
inline constexpr int kMaxChordTones = 5;

/** A chord quality: semitones above the root, lowest first. */
struct ChordShape
{
    const char* label;  // pill text
    const char* symbol; // appended to the root: "C" + "m7"
    int count;
    int intervals[kMaxChordTones];
};

inline constexpr ChordShape kChordShapes[] = {
    {"MAJ", "", 3, {0, 4, 7}},          {"MIN", "m", 3, {0, 3, 7}},
    {"7", "7", 4, {0, 4, 7, 10}},       {"MAJ7", "maj7", 4, {0, 4, 7, 11}},
    {"MIN7", "m7", 4, {0, 3, 7, 10}},   {"SUS2", "sus2", 3, {0, 2, 7}},
    {"SUS4", "sus4", 3, {0, 5, 7}},     {"DIM", "dim", 3, {0, 3, 6}},
    {"ADD9", "add9", 4, {0, 4, 7, 14}}, {"MIN9", "m9", 5, {0, 3, 7, 10, 14}},
};

inline constexpr int kNumChordShapes = static_cast<int>(sizeof(kChordShapes) / sizeof(kChordShapes[0]));

inline const ChordShape& chordShape(int index) noexcept
{
    return kChordShapes[std::clamp(index, 0, kNumChordShapes - 1)];
}

/** Sharp spelling of a pitch class 0..11. */
inline const char* pitchClassName(int pitchClass) noexcept
{
    static constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((pitchClass % 12) + 12) % 12];
}

inline int pitchClassOf(int midiNote) noexcept
{
    return ((midiNote % 12) + 12) % 12;
}
/**
 * Names a set of notes: the chord shape whose pitch classes match exactly, preferring the lowest note as the root.
 * Returns false (root = lowest note's pitch class) when no shape matches.
 */
inline bool identifyChord(const int* notes, int count, int& root, int& shape) noexcept
{
    root = 0;
    shape = -1;
    if (count <= 0)
        return false;
    unsigned played = 0;
    int lowest = notes[0];
    for (int i = 0; i < count; ++i)
    {
        played |= 1u << pitchClassOf(notes[i]);
        lowest = std::min(lowest, notes[i]);
    }
    root = pitchClassOf(lowest);
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        const int candidate = (root + attempt) % 12;
        if ((played & (1u << candidate)) == 0)
            continue;
        for (int s = 0; s < kNumChordShapes; ++s)
        {
            unsigned tones = 0;
            for (int i = 0; i < kChordShapes[s].count; ++i)
                tones |= 1u << pitchClassOf(candidate + kChordShapes[s].intervals[i]);
            if (tones == played)
            {
                root = candidate;
                shape = s;
                return true;
            }
        }
    }
    return false;
}
} // namespace dolphinflow::dsp
