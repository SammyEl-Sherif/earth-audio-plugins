#pragma once

#include "dsp/DolphinFlowEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace dolphinflow
{
/**
 * Turns what the engine plays into MIDI out, so a track recording it gets the full chord. Common tones stay one
 * long note across a chord change.
 *
 * MPE (lower zone: manager channel 1, one member channel 2-16 per note, 48 semitone bend range, the MPE default):
 * a voice that glides ends its old note and starts the new one on a fresh channel with a pitch bend that begins
 * at the pitch it slid from and follows the engine's glide to zero, so the host's MPE editor shows each glide as
 * that note's pitch curve. Without MPE every note is on channel 1 and glides are not sent.
 *
 * Audio thread only; allocation free.
 */
class NoteOut
{
public:
    static constexpr int kPlainChannel = 1;
    static constexpr int kFirstMemberChannel = 2;
    static constexpr int kLastMemberChannel = 16;
    static constexpr float kBendRangeSemitones = 48.0f;
    static constexpr int kBendInterval = 64; // samples between pitch bend updates while a glide runs

    /** Forgets every note without sending anything (after prepare). */
    void reset() noexcept;
    /** Switching mode ends every sounding note first; the next sync starts them again in the new mode. */
    void setMpe(bool on, juce::MidiBuffer& out, int samplePosition) noexcept;
    bool isMpe() const noexcept { return mpe; }

    /** Notes on and off to match the engine's held voicing. Call after the engine was told the change. */
    void sync(const dsp::DolphinFlowEngine& engine, int velocity, juce::MidiBuffer& out, int samplePosition) noexcept;
    /** MPE: sends each note's pitch bend where the engine's glide has moved it. */
    void updateBends(const dsp::DolphinFlowEngine& engine, juce::MidiBuffer& out, int samplePosition) noexcept;
    /** Ends every note (the next sync starts the held ones again, e.g. when the host starts recording). */
    void releaseAll(juce::MidiBuffer& out, int samplePosition) noexcept;

    /** True while a sent bend is not yet at rest, i.e. bends still need sending. */
    bool isGliding(const dsp::DolphinFlowEngine& engine) const noexcept;
    int getNumNotes() const noexcept { return count; }

    /** Pitch bend value (0..16383, centre 8192) for an offset in semitones over the MPE range. */
    static int bendFor(float semitones) noexcept;

private:
    struct Note
    {
        int note;
        int channel;
        int voice;
        int bend;
    };

    int allocateChannel() noexcept;
    int wantedBend(const dsp::DolphinFlowEngine& engine, const Note& n) const noexcept;

    std::array<Note, dsp::kMaxVoices> notes{};
    int count = 0;
    int nextChannel = kFirstMemberChannel;
    bool mpe = true;
};
} // namespace dolphinflow
