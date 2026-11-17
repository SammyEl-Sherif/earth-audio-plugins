#pragma once

#include "Song.h"

#include <atomic>
#include <memory>
#include <vector>

namespace dolphinflow::dsp
{
/** What the audio thread plays: the song flattened, mute / solo resolved, notes sorted. Built on the message thread. */
struct PlaySong
{
    struct Chord
    {
        int root;
        int shape;
        double start;
        double beats;
        double end() const noexcept { return start + beats; }
    };
    struct Note
    {
        int pitch;
        double start;
        double end;
        int velocity;
        int channel;
    };

    std::vector<Chord> chords;
    std::vector<Note> notes; // every audible melody note, sorted by start
    double chordLength = 0.0;
    double length = 0.0; // whole song, bar aligned
    bool chordsAudible = true;
    int chordVelocity = 100;

    /** Index of the chord at `beat` (inside [0, chordLength)), or -1. Allocation free. */
    int chordAt(double beat) const noexcept
    {
        for (int i = 0; i < static_cast<int>(chords.size()); ++i)
            if (beat >= chords[static_cast<std::size_t>(i)].start - 1.0e-9 &&
                beat < chords[static_cast<std::size_t>(i)].end() - 1.0e-9)
                return i;
        return -1;
    }
};

inline std::unique_ptr<PlaySong> compileSong(const Song& song)
{
    auto play = std::make_unique<PlaySong>();
    for (const auto& c : placeChords(song))
        play->chords.push_back({c.root, c.shape, c.start, c.beats});
    play->chordLength = chordBeats(song);
    play->length = songBeats(song);

    bool anySolo = song.chordMix.solo;
    for (const auto& t : song.tracks)
        anySolo = anySolo || t.mix.solo;
    const auto audible = [anySolo](const TrackMix& mix) { return !mix.mute && (!anySolo || mix.solo); };
    play->chordsAudible = audible(song.chordMix);
    play->chordVelocity = std::clamp(static_cast<int>(std::lround(127.0f * song.chordMix.volume)), 1, 127);
    for (int t = 0; t < static_cast<int>(song.tracks.size()) && t < kMaxMelodyTracks; ++t)
    {
        const auto& track = song.tracks[static_cast<std::size_t>(t)];
        if (!audible(track.mix))
            continue;
        for (const auto& n : track.notes)
        {
            const int velocity =
                std::clamp(static_cast<int>(std::lround(static_cast<float>(n.velocity) * track.mix.volume)), 1, 127);
            play->notes.push_back({n.pitch, n.start, n.end(), velocity, kFirstTrackChannel + t});
        }
    }
    std::stable_sort(play->notes.begin(), play->notes.end(),
                     [](const PlaySong::Note& a, const PlaySong::Note& b) { return a.start < b.start; });
    return play;
}

/**
 * Hands PlaySongs from the message thread to the audio thread without locks or allocation on the audio side.
 *
 * Hazard pointer: the audio thread announces the song it is about to read (`inUse`) and re-checks that it is still
 * the live one; the message thread frees a retired song only once the audio thread no longer announces it. All
 * atomics are sequentially consistent, which is what makes the announce-then-recheck safe.
 */
class SongStore
{
public:
    SongStore() { publish(std::make_unique<PlaySong>()); }
    ~SongStore()
    {
        delete live.load();
        for (auto* r : retired)
            delete r;
    }

    /** Message thread. */
    void publish(std::unique_ptr<PlaySong> song)
    {
        auto* old = live.exchange(song.release());
        if (old != nullptr)
            retired.push_back(old);
        collect();
    }

    /** Message thread: frees songs the audio thread has moved past. Called on each publish and from a timer. */
    void collect()
    {
        const auto* busy = inUse.load();
        retired.erase(std::remove_if(retired.begin(), retired.end(),
                                     [busy](PlaySong* r)
                                     {
                                         if (r == busy)
                                             return false;
                                         delete r;
                                         return true;
                                     }),
                      retired.end());
    }

    /** Audio thread: the song to use for this block (valid until the next acquire). */
    const PlaySong& acquire() noexcept
    {
        for (;;)
        {
            auto* song = live.load();
            inUse.store(song);
            if (live.load() == song)
                return *song;
        }
    }

    /** Message thread (tests, export): the latest published song. */
    const PlaySong& latest() const noexcept { return *live.load(); }

private:
    std::atomic<PlaySong*> live{nullptr};
    std::atomic<const PlaySong*> inUse{nullptr};
    std::vector<PlaySong*> retired;
};
} // namespace dolphinflow::dsp
