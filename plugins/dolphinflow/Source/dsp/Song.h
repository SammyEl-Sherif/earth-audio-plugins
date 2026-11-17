#pragma once

#include "Progression.h"
#include "Theory.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace dolphinflow::dsp
{
// ---- the song: sections of chords, plus melody tracks (lead, bass, ...) ------------------------------------------

inline constexpr int kMaxSections = 16;
inline constexpr int kMaxSongChords = 64;
inline constexpr int kMaxMelodyTracks = 4;
inline constexpr int kMaxTrackNotes = 512;
/** Melody tracks send on their own MIDI channels, 13..16, so a host can route each to its own instrument. The chord
 * track keeps channel 1 (or MPE member channels 2..12). */
inline constexpr int kFirstTrackChannel = 13;
inline constexpr double kMinChordBeats = 0.25;
inline constexpr double kMaxChordBeats = 64.0;
inline constexpr int kLowestNote = 24;  // C1
inline constexpr int kHighestNote = 96; // C7

struct SongChord
{
    int root = 0;
    int shape = 0;
    double beats = 4.0;

    bool operator==(const SongChord& o) const noexcept
    {
        return root == o.root && shape == o.shape && std::abs(beats - o.beats) < 1.0e-9;
    }
};

struct Section
{
    std::string name = "Verse";
    int colour = 0; // index into the editor's section colours
    std::vector<SongChord> chords;
};

struct MelodyNote
{
    int pitch = 60;
    double start = 0.0;  // beats from the song start
    double length = 1.0; // beats
    int velocity = 100;

    double end() const noexcept { return start + length; }
    bool operator==(const MelodyNote& o) const noexcept
    {
        return pitch == o.pitch && std::abs(start - o.start) < 1.0e-9 && std::abs(length - o.length) < 1.0e-9 &&
               velocity == o.velocity;
    }
};

enum class TrackRole
{
    Lead = 0,
    Bass,
    Pad,
    Arp
};

inline const char* trackRoleName(TrackRole role) noexcept
{
    static constexpr const char* names[] = {"Lead", "Bass", "Pad", "Arp"};
    return names[std::clamp(static_cast<int>(role), 0, 3)];
}

/** Where a new track's piano roll opens: bass low, others around middle C. */
inline int roleCentre(TrackRole role) noexcept
{
    return role == TrackRole::Bass ? 40 : 67;
}

struct TrackMix
{
    bool mute = false;
    bool solo = false;
    float volume = 0.8f; // scales note velocity
};

struct MelodyTrack
{
    std::string name = "Lead";
    TrackRole role = TrackRole::Lead;
    TrackMix mix;
    std::vector<MelodyNote> notes; // kept sorted by start
};

struct Song
{
    std::vector<Section> sections;
    std::vector<MelodyTrack> tracks;
    TrackMix chordMix;
};

/** First open: one Verse of Am7 - Fmaj7 - C - G, a bar each, and empty Lead and Bass tracks. */
inline Song defaultSong()
{
    Song song;
    Section verse;
    verse.name = "Verse";
    verse.chords = {{9, 4, 4.0}, {5, 3, 4.0}, {0, 0, 4.0}, {7, 0, 4.0}};
    song.sections.push_back(verse);
    MelodyTrack lead;
    lead.name = "Lead";
    lead.role = TrackRole::Lead;
    MelodyTrack bass;
    bass.name = "Bass";
    bass.role = TrackRole::Bass;
    song.tracks = {lead, bass};
    return song;
}

// ---- positions -------------------------------------------------------------------------------------------------

/** A chord with its place in the song. */
struct PlacedChord
{
    int section = 0;
    int index = 0; // within its section
    int flat = 0;  // across the song
    int root = 0;
    int shape = 0;
    double start = 0.0;
    double beats = 4.0;

    double end() const noexcept { return start + beats; }
};

inline std::vector<PlacedChord> placeChords(const Song& song)
{
    std::vector<PlacedChord> out;
    double time = 0.0;
    for (int s = 0; s < static_cast<int>(song.sections.size()); ++s)
        for (int i = 0; i < static_cast<int>(song.sections[static_cast<std::size_t>(s)].chords.size()); ++i)
        {
            const auto& c = song.sections[static_cast<std::size_t>(s)].chords[static_cast<std::size_t>(i)];
            out.push_back({s, i, static_cast<int>(out.size()), c.root, c.shape, time, c.beats});
            time += c.beats;
        }
    return out;
}

inline double sectionBeats(const Section& section) noexcept
{
    double total = 0.0;
    for (const auto& c : section.chords)
        total += c.beats;
    return total;
}

inline double sectionStart(const Song& song, int sectionIndex) noexcept
{
    double start = 0.0;
    for (int s = 0; s < sectionIndex && s < static_cast<int>(song.sections.size()); ++s)
        start += sectionBeats(song.sections[static_cast<std::size_t>(s)]);
    return start;
}

inline double chordBeats(const Song& song) noexcept
{
    return sectionStart(song, static_cast<int>(song.sections.size()));
}

/** Song length: the chords, or the last melody note if that runs on, rounded up to a whole bar. */
inline double songBeats(const Song& song) noexcept
{
    double total = chordBeats(song);
    for (const auto& t : song.tracks)
        for (const auto& n : t.notes)
            total = std::max(total, n.end());
    return total <= 0.0 ? 0.0 : std::ceil(total / 4.0 - 1.0e-9) * 4.0;
}

/** Index into placeChords() of the chord sounding at `beat`, or -1. */
inline int chordIndexAt(const std::vector<PlacedChord>& chords, double beat) noexcept
{
    for (const auto& c : chords)
        if (beat >= c.start - 1.0e-9 && beat < c.end() - 1.0e-9)
            return c.flat;
    return -1;
}

// ---- harmonic guides -------------------------------------------------------------------------------------------

/** How a note sits against the chord under it: what the piano roll shades its rows with. */
enum class Guide
{
    Root,      // the chord's root: bassline anchor
    ChordTone, // third or fifth
    Extension, // sixth, seventh, ninth, eleventh
    InKey,     // a passing note in the home key
    OutOfKey
};

inline Guide guideFor(int pitch, int root, int shape, unsigned keyMask) noexcept
{
    const int pc = pitchClassOf(pitch);
    if (pc == pitchClassOf(root))
        return Guide::Root;
    const auto& s = chordShape(shape);
    for (int i = 1; i < s.count; ++i)
        if (pitchClassOf(root + s.intervals[i]) == pc)
            return i <= 2 ? Guide::ChordTone : Guide::Extension;
    return (keyMask >> pc) & 1u ? Guide::InKey : Guide::OutOfKey;
}

// ---- chord relations and matching -------------------------------------------------------------------------------

enum class Relation
{
    None = 0,
    Smooth, // shares two or more notes: glides with little movement
    Strong  // a fifth down (V -> I) or a leading tone up into its root: the strongest pull
};

/** How `to` follows on from `from`. */
inline Relation relationBetween(int fromRoot, int fromShape, int toRoot, int toShape) noexcept
{
    if (fromRoot == toRoot && fromShape == toShape)
        return Relation::None;
    const int interval = pitchClassOf(toRoot - fromRoot);
    const unsigned from = chordMask(fromRoot, fromShape);
    const unsigned to = chordMask(toRoot, toShape);
    const bool fromHasLeadingTone = ((from >> pitchClassOf(toRoot - 1)) & 1u) != 0;
    if (interval == 5 || (fromHasLeadingTone && interval == 1))
        return Relation::Strong;
    int common = 0;
    for (unsigned m = from & to; m != 0; m &= m - 1)
        ++common;
    return common >= 2 ? Relation::Smooth : Relation::None;
}

/** Bit i set for each pitch class i among `pitches`. */
inline unsigned pitchClassMask(const std::vector<int>& pitches) noexcept
{
    unsigned mask = 0;
    for (int p : pitches)
        mask |= 1u << pitchClassOf(p);
    return mask;
}

/** A chord "matches" a melody when it contains every pitch class of it. */
inline bool chordContains(int root, int shape, unsigned pitchClasses) noexcept
{
    return pitchClasses != 0 && (chordMask(root, shape) & pitchClasses) == pitchClasses;
}

// ---- the extended palette (circles) ----------------------------------------------------------------------------

/** Palette rows, top to bottom in the editor: colourful extensions above, the plain triad at the bottom. */
enum class CircleRow
{
    LeadsTo = 0, // V7 of the degree
    Ninth,       // stacked ninth (or 6/9-style add9)
    Add9,
    MajorSeventh, // the stacked seventh
    Sixth,
    Sus,
    Triad,
    Count
};
inline constexpr int kCircleRows = static_cast<int>(CircleRow::Count);

inline const char* circleRowLabel(int row) noexcept
{
    static constexpr const char* labels[] = {"V7 OF", "9", "ADD9", "7", "6", "SUS", "TRIAD"};
    return labels[std::clamp(row, 0, kCircleRows - 1)];
}

/** The palette chord at (row, degree). shape -1 = empty circle (no such chord in this scale). */
inline PaletteChord circleChord(const PaletteKey& key, int row, int degree) noexcept
{
    static constexpr int maj = 0, minor = 1, dom7 = 2, sus2 = 5, sus4 = 6, add9 = 8, six = 11, minor6 = 12;
    const int tonic = key.tonic();
    const unsigned paletteMask = scaleMask(tonic, key.scale);
    const unsigned homeMask = scaleMask(key.homeTonic, key.homeScale);
    const int triad = stackedChord(tonic, key.scale, degree, 3);
    PaletteChord chord;
    chord.root = degreeRoot(tonic, key.scale, degree);
    chord.shape = -1;
    const auto finish = [&]
    {
        if (chord.shape >= 0)
        {
            chord.inKey = chordInKey(chord.root, chord.shape, homeMask);
            chord.numeral = romanNumeral(chord.root, chord.shape, key.homeTonic, key.homeScale);
        }
        return chord;
    };
    switch (static_cast<CircleRow>(std::clamp(row, 0, kCircleRows - 1)))
    {
    case CircleRow::Triad:
        chord.shape = triad;
        return finish();
    case CircleRow::MajorSeventh:
        chord.shape = stackedChord(tonic, key.scale, degree, 4);
        return finish();
    case CircleRow::Ninth:
        chord.shape = stackedChord(tonic, key.scale, degree, 5);
        return finish();
    case CircleRow::Add9:
        if (triad == maj && chordInKey(chord.root, add9, paletteMask))
            chord.shape = add9;
        return finish();
    case CircleRow::Sixth:
        if (triad == maj && chordInKey(chord.root, six, paletteMask))
            chord.shape = six;
        else if (triad == minor && chordInKey(chord.root, minor6, paletteMask))
            chord.shape = minor6;
        return finish();
    case CircleRow::Sus:
        if (chordInKey(chord.root, sus4, paletteMask))
            chord.shape = sus4;
        else if (chordInKey(chord.root, sus2, paletteMask))
            chord.shape = sus2;
        return finish();
    case CircleRow::LeadsTo:
    {
        if (triad < 0)
            return chord;
        const auto target = romanNumeral(chord.root, triad, key.homeTonic, key.homeScale);
        chord.root = pitchClassOf(chord.root + 7);
        chord.shape = dom7;
        chord.inKey = chordInKey(chord.root, chord.shape, homeMask);
        std::snprintf(chord.numeral.text, sizeof(chord.numeral.text), "V7/%s", target.text);
        return chord;
    }
    case CircleRow::Count:
        break;
    }
    return chord;
}

// ---- voicing the chord track -----------------------------------------------------------------------------------

/** Each chord's voicing as the loop plays it: led through once to settle, then voiced on the second pass. */
inline std::vector<Voicing> settledVoicings(const std::vector<PlacedChord>& chords, int voices, int centre)
{
    std::vector<Voicing> out;
    if (chords.empty())
        return out;
    Voicing v = VoiceLeader::initial(chords.front().root, chords.front().shape, voices, centre);
    for (std::size_t i = 1; i < chords.size(); ++i)
        v = VoiceLeader::lead(v, chords[i].root, chords[i].shape, voices, centre);
    for (const auto& c : chords)
    {
        v = VoiceLeader::lead(v, c.root, c.shape, voices, centre);
        out.push_back(v);
    }
    return out;
}

/** A note of an exported chord clip. With MPE, `channel` is its member channel and `bendFrom` how many semitones
 * below (negative) or above its pitch the voice slid in from (0: no glide). */
struct VoicedNote
{
    int note;
    double start;
    double end;
    int channel;
    float bendFrom;
};

/**
 * The chord track's notes over [from, to) beats, voiced as it plays. Common tones of consecutive chords tie into one
 * note. With `mpe`, each note gets its own member channel (2..12, round robin) and a note a voice glides into
 * remembers where it slid from, so the clip can carry the glide as that note's pitch bend.
 */
inline std::vector<VoicedNote> voiceChordTrack(const Song& song, int voices, int centre, double from, double to,
                                               bool mpe)
{
    std::vector<VoicedNote> notes;
    const auto chords = placeChords(song);
    const auto voicings = settledVoicings(chords, voices, centre);
    struct Open
    {
        std::size_t index;
        int voice;
    };
    std::vector<Open> open;
    int nextChannel = 2;
    constexpr int lastMember = kFirstTrackChannel - 1;
    Voicing previous;
    bool havePrevious = false;
    for (std::size_t c = 0; c < chords.size(); ++c)
    {
        const double start = std::max(chords[c].start, from);
        const double end = std::min(chords[c].end(), to);
        if (end <= start + 1.0e-9)
        {
            havePrevious = false;
            continue;
        }
        if (!havePrevious && !open.empty())
        {
            for (const auto& o : open)
                notes[o.index].end = std::max(notes[o.index].start, start);
            open.clear();
        }
        const auto& voicing = voicings[c];
        std::vector<Open> stillOpen;
        std::vector<bool> taken(static_cast<std::size_t>(voicing.count), false);
        for (const auto& o : open)
        {
            bool kept = false;
            for (int v = 0; v < voicing.count && !kept; ++v)
                if (!taken[static_cast<std::size_t>(v)] && voicing.notes[v] == notes[o.index].note)
                {
                    taken[static_cast<std::size_t>(v)] = true;
                    stillOpen.push_back({o.index, v});
                    kept = true;
                }
            if (!kept)
                notes[o.index].end = start;
        }
        for (int v = 0; v < voicing.count; ++v)
        {
            if (taken[static_cast<std::size_t>(v)])
                continue;
            // A doubled note (two voices on one pitch) sounds once.
            const bool doubled = std::any_of(stillOpen.begin(), stillOpen.end(),
                                             [&](const Open& o) { return notes[o.index].note == voicing.notes[v]; });
            if (doubled)
                continue;
            int channel = 1;
            if (mpe)
            {
                for (int tries = 0; tries < lastMember - 1; ++tries)
                {
                    channel = nextChannel;
                    nextChannel = channel == lastMember ? 2 : channel + 1;
                    if (std::none_of(stillOpen.begin(), stillOpen.end(),
                                     [&](const Open& o) { return notes[o.index].channel == channel; }))
                        break;
                }
            }
            float bendFrom = 0.0f;
            if (mpe && havePrevious && v < previous.count && previous.notes[v] != voicing.notes[v])
                bendFrom = static_cast<float>(previous.notes[v] - voicing.notes[v]);
            stillOpen.push_back({notes.size(), v});
            notes.push_back({voicing.notes[v], start, end, channel, bendFrom});
        }
        for (const auto& o : stillOpen)
            notes[o.index].end = end;
        open = stillOpen;
        previous = voicing;
        havePrevious = true;
    }
    return notes;
}

// ---- edits (message thread) ------------------------------------------------------------------------------------

inline double snapBeats(double beats, double grid) noexcept
{
    return grid > 0.0 ? std::round(beats / grid) * grid : beats;
}

inline void sortNotes(MelodyTrack& track)
{
    std::stable_sort(track.notes.begin(), track.notes.end(),
                     [](const MelodyNote& a, const MelodyNote& b)
                     { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
}

inline int countChords(const Song& song) noexcept
{
    int n = 0;
    for (const auto& s : song.sections)
        n += static_cast<int>(s.chords.size());
    return n;
}

/** Where a drop at `beat` lands: the section it falls in (or the last) and the index before which to insert. */
struct InsertPoint
{
    int section = 0;
    int index = 0;
};

inline InsertPoint insertPointAt(const Song& song, double beat) noexcept
{
    InsertPoint at;
    if (song.sections.empty())
        return at;
    double start = 0.0;
    for (int s = 0; s < static_cast<int>(song.sections.size()); ++s)
    {
        const auto& section = song.sections[static_cast<std::size_t>(s)];
        const double length = sectionBeats(section);
        const bool last = s == static_cast<int>(song.sections.size()) - 1;
        if (beat < start + length || last || (length <= 0.0 && beat < start + 4.0))
        {
            at.section = s;
            at.index = static_cast<int>(section.chords.size());
            double t = start;
            for (int i = 0; i < static_cast<int>(section.chords.size()); ++i)
            {
                const double mid = t + section.chords[static_cast<std::size_t>(i)].beats * 0.5;
                if (beat < mid)
                {
                    at.index = i;
                    break;
                }
                t += section.chords[static_cast<std::size_t>(i)].beats;
            }
            return at;
        }
        start += length;
    }
    return at;
}

/** Inserts a chord; false when the song is full. */
inline bool insertChord(Song& song, const InsertPoint& at, const SongChord& chord)
{
    if (countChords(song) >= kMaxSongChords)
        return false;
    if (song.sections.empty())
        song.sections.push_back({});
    const int s = std::clamp(at.section, 0, static_cast<int>(song.sections.size()) - 1);
    auto& chords = song.sections[static_cast<std::size_t>(s)].chords;
    const int i = std::clamp(at.index, 0, static_cast<int>(chords.size()));
    chords.insert(chords.begin() + i, chord);
    return true;
}

/** Empty sections go, except the last (where new chords are dropped); a song always keeps one section. */
inline void tidySections(Song& song)
{
    for (int s = static_cast<int>(song.sections.size()) - 2; s >= 0; --s)
        if (song.sections[static_cast<std::size_t>(s)].chords.empty())
            song.sections.erase(song.sections.begin() + s);
    if (song.sections.empty())
        song.sections.push_back({});
}

/** Removes the chords at these flat indices; returns them in song order. */
inline std::vector<SongChord> takeChords(Song& song, std::vector<int> flat)
{
    std::vector<SongChord> taken;
    std::sort(flat.begin(), flat.end());
    flat.erase(std::unique(flat.begin(), flat.end()), flat.end());
    int index = 0;
    for (auto& section : song.sections)
        for (auto it = section.chords.begin(); it != section.chords.end();)
        {
            if (std::binary_search(flat.begin(), flat.end(), index++))
            {
                taken.push_back(*it);
                it = section.chords.erase(it);
            }
            else
            {
                ++it;
            }
        }
    return taken;
}

/** The names new sections cycle through. */
inline const char* nextSectionName(const Song& song) noexcept
{
    static constexpr const char* names[] = {"Intro", "Verse", "Pre", "Chorus", "Bridge", "Drop", "Outro"};
    return names[song.sections.size() % 7];
}
} // namespace dolphinflow::dsp
