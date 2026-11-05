#pragma once

#include <algorithm>

namespace swan::dsp
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
} // namespace swan::dsp
