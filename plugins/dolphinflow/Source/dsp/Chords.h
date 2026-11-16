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
    // Beyond the number keys: reached from the pills and the chord palette. Appended, so saved indices keep meaning.
    {"AUG", "aug", 3, {0, 4, 8}},           {"6", "6", 4, {0, 4, 7, 9}},
    {"MIN6", "m6", 4, {0, 3, 7, 9}},        {"DIM7", "dim7", 4, {0, 3, 6, 9}},
    {"M7B5", "m7b5", 4, {0, 3, 6, 10}},     {"9", "9", 5, {0, 4, 7, 10, 14}},
    {"MAJ9", "maj9", 5, {0, 4, 7, 11, 14}}, {"7SUS4", "7sus4", 4, {0, 5, 7, 10}},
    {"MIN11", "m11", 5, {0, 3, 7, 10, 17}}, {"MMAJ7", "m(maj7)", 4, {0, 3, 7, 11}},
};

inline constexpr int kNumChordShapes = static_cast<int>(sizeof(kChordShapes) / sizeof(kChordShapes[0]));
/** Shapes with a number key (1-0); the rest are picked with the mouse. */
inline constexpr int kNumKeyedShapes = 10;

inline const ChordShape& chordShape(int index) noexcept
{
    return kChordShapes[std::clamp(index, 0, kNumChordShapes - 1)];
}

/** Spelling of a pitch class 0..11: sharps unless `flats`. */
inline const char* pitchClassName(int pitchClass, bool flats = false) noexcept
{
    static constexpr const char* sharpNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static constexpr const char* flatNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    return (flats ? flatNames : sharpNames)[((pitchClass % 12) + 12) % 12];
}

inline int pitchClassOf(int midiNote) noexcept
{
    return ((midiNote % 12) + 12) % 12;
}

/** Bit i set for each pitch class i the chord contains. */
inline unsigned chordMask(int rootPitchClass, int shapeIndex) noexcept
{
    const auto& shape = chordShape(shapeIndex);
    unsigned mask = 0;
    for (int i = 0; i < shape.count; ++i)
        mask |= 1u << pitchClassOf(rootPitchClass + shape.intervals[i]);
    return mask;
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
    // The keyed shapes first, so a set two names share (Dm7 = F6) gets the commoner name: m7 over 6, m7b5 over m6.
    const int passes[][2] = {{0, kNumKeyedShapes}, {kNumKeyedShapes, kNumChordShapes}};
    for (const auto& pass : passes)
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            const int candidate = (root + attempt) % 12;
            if ((played & (1u << candidate)) == 0)
                continue;
            for (int s = pass[0]; s < pass[1]; ++s)
                if (chordMask(candidate, s) == played)
                {
                    root = candidate;
                    shape = s;
                    return true;
                }
        }
    return false;
}
} // namespace dolphinflow::dsp
