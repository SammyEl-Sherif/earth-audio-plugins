#pragma once

#include "Chords.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dolphinflow::dsp
{
/** A seven-note scale: semitones above the tonic for each degree. */
struct Scale
{
    const char* name;  // parameter text
    const char* label; // upper-case display text
    int steps[7];
};

inline constexpr Scale kScales[] = {
    {"Major", "MAJOR", {0, 2, 4, 5, 7, 9, 11}},
    {"Minor", "MINOR", {0, 2, 3, 5, 7, 8, 10}},
    {"Dorian", "DORIAN", {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian", "PHRYGIAN", {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian", "LYDIAN", {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian", "MIXOLYDIAN", {0, 2, 4, 5, 7, 9, 10}},
    {"Locrian", "LOCRIAN", {0, 1, 3, 5, 6, 8, 10}},
    {"Harmonic Minor", "HARMONIC MINOR", {0, 2, 3, 5, 7, 8, 11}},
    {"Melodic Minor", "MELODIC MINOR", {0, 2, 3, 5, 7, 9, 11}},
};
inline constexpr int kNumScales = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
inline constexpr int kDegrees = 7;

inline const Scale& scaleAt(int index) noexcept
{
    return kScales[std::clamp(index, 0, kNumScales - 1)];
}

/** Bit i set for each pitch class i in the key. */
inline unsigned scaleMask(int tonic, int scaleIndex) noexcept
{
    unsigned mask = 0;
    for (int step : scaleAt(scaleIndex).steps)
        mask |= 1u << pitchClassOf(tonic + step);
    return mask;
}

inline bool chordInKey(int root, int shape, unsigned keyMask) noexcept
{
    return (chordMask(root, shape) & ~keyMask) == 0;
}

inline int degreeRoot(int tonic, int scaleIndex, int degree) noexcept
{
    return pitchClassOf(tonic + scaleAt(scaleIndex).steps[((degree % kDegrees) + kDegrees) % kDegrees]);
}

/** The shape whose tones, measured from its root, are exactly `relativeMask` (bit 0 = root). -1 if none. */
inline int shapeForIntervals(unsigned relativeMask) noexcept
{
    for (int s = 0; s < kNumChordShapes; ++s)
        if (chordMask(0, s) == relativeMask)
            return s;
    return -1;
}

/** The chord built by stacking thirds inside the scale on `degree`: 3 = triad, 4 = seventh, 5 = ninth. -1 if the
 * result is a chord Dolphin Flow has no shape for. */
inline int stackedChord(int tonic, int scaleIndex, int degree, int tones) noexcept
{
    const int root = degreeRoot(tonic, scaleIndex, degree);
    unsigned relative = 0;
    for (int i = 0; i < tones; ++i)
        relative |= 1u << pitchClassOf(degreeRoot(tonic, scaleIndex, degree + 2 * i) - root);
    return shapeForIntervals(relative);
}

enum class Quality
{
    Major,
    Minor,
    Other // diminished, augmented, suspended
};

/** Per shape, in kChordShapes order: quality, and the text after the numeral (UTF-8). */
struct ShapeTheory
{
    Quality quality;
    bool lowerCase; // minor third above the root: written in lower case (ii, vii°)
    const char* numeralSuffix;
};

inline constexpr ShapeTheory kShapeTheory[] = {
    {Quality::Major, false, ""},
    {Quality::Minor, true, ""},
    {Quality::Major, false, "7"},
    {Quality::Major, false, "maj7"},
    {Quality::Minor, true, "7"},
    {Quality::Other, false, "sus2"},
    {Quality::Other, false, "sus4"},
    {Quality::Other, true, "\xc2\xb0"}, // °
    {Quality::Major, false, "add9"},
    {Quality::Minor, true, "9"},
    {Quality::Other, false, "+"},
    {Quality::Major, false, "6"},
    {Quality::Minor, true, "6"},
    {Quality::Other, true, "\xc2\xb0" "7"},
    {Quality::Other, true, "\xc3\xb8" "7"}, // ø7
    {Quality::Major, false, "9"},
    {Quality::Major, false, "maj9"},
    {Quality::Other, false, "7sus4"},
    {Quality::Minor, true, "11"},
    {Quality::Minor, true, "(maj7)"},
};
static_assert(sizeof(kShapeTheory) / sizeof(kShapeTheory[0]) == static_cast<std::size_t>(kNumChordShapes),
              "every chord shape needs its theory entry");

inline const ShapeTheory& shapeTheory(int shape) noexcept
{
    return kShapeTheory[std::clamp(shape, 0, kNumChordShapes - 1)];
}

/** Fixed-size text, so naming chords never allocates. UTF-8. */
struct Label
{
    char text[24] = {};
};

/**
 * Roman numeral of a chord in a key: "ii7", "V", "bVII", "#iv°". The degree is the scale step the root lands
 * on; a root between steps is written as the step above flattened (or, failing that, the step below sharpened).
 */
inline Label romanNumeral(int root, int shape, int tonic, int scaleIndex) noexcept
{
    static constexpr const char* upper[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static constexpr const char* lower[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    const auto& steps = scaleAt(scaleIndex).steps;
    const int offset = pitchClassOf(root - tonic);
    const char* accidental = "";
    int degree = -1;
    for (int d = 0; d < kDegrees && degree < 0; ++d)
        if (steps[d] == offset)
            degree = d;
    for (int d = 0; d < kDegrees && degree < 0; ++d)
        if (steps[d] == pitchClassOf(offset + 1))
        {
            degree = d;
            accidental = "b";
        }
    for (int d = 0; d < kDegrees && degree < 0; ++d)
        if (steps[d] == pitchClassOf(offset - 1))
        {
            degree = d;
            accidental = "#";
        }
    Label label;
    if (degree < 0)
        return label;
    const auto& theory = shapeTheory(shape);
    std::snprintf(label.text, sizeof(label.text), "%s%s%s", accidental,
                  theory.lowerCase ? lower[degree] : upper[degree], theory.numeralSuffix);
    return label;
}

/** "C", "Am7", "F#m7b5". */
inline Label chordName(int root, int shape, bool flats = false) noexcept
{
    Label label;
    std::snprintf(label.text, sizeof(label.text), "%s%s", pitchClassName(root, flats), chordShape(shape).symbol);
    return label;
}

/** Whether a key is written with flats: its key signature (the relative major's) is F, Bb, Eb, Ab or Db. The two
 * minor-scale variants take the natural minor's signature. */
inline bool prefersFlats(int tonic, int scaleIndex) noexcept
{
    // Where each mode's tonic sits in its relative major: Major, Minor, Dorian, Phrygian, Lydian, Mixolydian,
    // Locrian, then harmonic and melodic minor as Minor.
    static constexpr int offsetInMajor[] = {0, 9, 2, 4, 5, 7, 11, 9, 9};
    const int relativeMajor = pitchClassOf(tonic - offsetInMajor[std::clamp(scaleIndex, 0, kNumScales - 1)]);
    return relativeMajor == 5 || relativeMajor == 10 || relativeMajor == 3 || relativeMajor == 8 || relativeMajor == 1;
}

/** A chord named as it is written in a key: flats in flat keys, and for any root the numeral flattens (bVII). */
inline Label chordNameInKey(int root, int shape, int tonic, int scaleIndex) noexcept
{
    const bool flats = prefersFlats(tonic, scaleIndex) || romanNumeral(root, shape, tonic, scaleIndex).text[0] == 'b';
    return chordName(root, shape, flats);
}

// ---- the chord palette -----------------------------------------------------------------------------------------

inline constexpr int kPaletteRows = 4;
inline constexpr int kPaletteColumns = kDegrees;

enum class PaletteRow
{
    Triad = 0, // the scale's own triad on each degree
    Seventh,   // ...with its seventh
    Colour,    // ninth chord when the scale has one, else a suspension
    LeadsTo    // the dominant seventh that resolves onto that degree (V7/x)
};

inline const char* paletteRowLabel(int row) noexcept
{
    static constexpr const char* labels[] = {"TRIADS", "SEVENTHS", "COLOUR", "LEADS TO"};
    return labels[std::clamp(row, 0, kPaletteRows - 1)];
}

/** Where the palette is looking: home key, borrowed `shift` steps round the circle of fifths, in `scale`. */
struct PaletteKey
{
    int homeTonic = 0;
    int homeScale = 0;
    int shift = 0;      // -6..6 fifths from home
    int scale = 0;      // mode of the palette (a parallel mode of home when it differs from homeScale)

    int tonic() const noexcept { return pitchClassOf(homeTonic + 7 * shift); }
    bool isHome() const noexcept { return shift == 0 && scale == homeScale; }
};

inline constexpr int kMaxPaletteShift = 6;

struct PaletteChord
{
    int root = 0;
    int shape = 0;
    bool inKey = true; // every note in the home key: drawn filled, else hollow
    Label numeral;     // relative to the home key
};

inline PaletteChord paletteChord(const PaletteKey& key, int row, int degree) noexcept
{
    const int tonic = key.tonic();
    const int triad = std::max(0, stackedChord(tonic, key.scale, degree, 3));
    PaletteChord chord;
    chord.root = degreeRoot(tonic, key.scale, degree);
    chord.shape = triad;
    const unsigned paletteMask = scaleMask(tonic, key.scale);

    switch (static_cast<PaletteRow>(std::clamp(row, 0, kPaletteRows - 1)))
    {
    case PaletteRow::Triad:
        break;
    case PaletteRow::Seventh:
        if (const int seventh = stackedChord(tonic, key.scale, degree, 4); seventh >= 0)
            chord.shape = seventh;
        break;
    case PaletteRow::Colour:
    {
        static constexpr int sus4 = 6, sus2 = 5, add9 = 8;
        if (const int ninth = stackedChord(tonic, key.scale, degree, 5); ninth >= 0)
            chord.shape = ninth;
        else if (chordInKey(chord.root, sus4, paletteMask))
            chord.shape = sus4;
        else if (chordInKey(chord.root, sus2, paletteMask))
            chord.shape = sus2;
        else if (chordInKey(chord.root, add9, paletteMask))
            chord.shape = add9;
        break;
    }
    case PaletteRow::LeadsTo:
    {
        static constexpr int dominant7 = 2;
        const auto target = romanNumeral(chord.root, triad, key.homeTonic, key.homeScale);
        chord.root = pitchClassOf(chord.root + 7);
        chord.shape = dominant7;
        chord.inKey = chordInKey(chord.root, chord.shape, scaleMask(key.homeTonic, key.homeScale));
        std::snprintf(chord.numeral.text, sizeof(chord.numeral.text), "V7/%s", target.text);
        return chord;
    }
    }
    chord.inKey = chordInKey(chord.root, chord.shape, scaleMask(key.homeTonic, key.homeScale));
    chord.numeral = romanNumeral(chord.root, chord.shape, key.homeTonic, key.homeScale);
    return chord;
}
} // namespace dolphinflow::dsp
