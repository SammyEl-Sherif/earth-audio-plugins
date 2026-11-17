#pragma once

#include "dsp/Song.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace dolphinflow
{
/** The song as a "Song" child of the plugin state. */
juce::ValueTree songToTree(const dsp::Song& song);

/**
 * The song saved in `state`: its "Song" child; else an older save's eight arrangement squares ("Progression"),
 * brought over as one Verse; else the default song. Counts are clamped to the song limits.
 */
dsp::Song songFromState(const juce::ValueTree& state);

/** Keeps a song inside the limits (sections, chords, tracks, notes) and its values in range. */
void sanitise(dsp::Song& song);

/** What a MIDI clip holds. */
struct ClipScope
{
    int track = -1;         // -1: the chord track, else a melody track
    double from = 0.0;      // beats
    double to = -1.0;       // < 0: to the song's end
    bool allTracks = false; // every track, one MIDI track each (a type-1 file)
};

/** How the chord track sounds, so its clip matches what plays. */
struct ClipVoicing
{
    int voices = 4;
    int centre = 60;
    bool mpe = false;
    double bpm = 120.0;
    float glideMs = 350.0f;
    float curve = 0.5f;
};

/**
 * One standard MIDI file for `scope`, 960 ticks per beat, starting at `scope.from`. The chord track is voiced as it
 * plays (common tones tied); with MPE each chord note has its own channel, the file sets up an MPE lower zone, and a
 * note a voice glides into carries that glide as a pitch bend curve shaped like the synth's. Melody tracks are on
 * channel 1, so the clip plays on whatever instrument it is dropped on. Empty when there is nothing to export.
 */
juce::MidiFile buildClip(const dsp::Song& song, const ClipScope& scope, const ClipVoicing& voicing);

/** "Chords - Am7 F C G", "Lead", "Song": the clip's file name, without extension. */
juce::String clipTitle(const dsp::Song& song, const ClipScope& scope, int homeTonic, int homeScale);
} // namespace dolphinflow
