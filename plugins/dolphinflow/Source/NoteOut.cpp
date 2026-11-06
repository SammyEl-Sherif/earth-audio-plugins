#include "NoteOut.h"

#include <algorithm>
#include <cmath>

namespace dolphinflow
{
namespace
{
constexpr int kBendCentre = 8192;
} // namespace

int NoteOut::bendFor(float semitones) noexcept
{
    const auto offset = std::lround(semitones / kBendRangeSemitones * static_cast<float>(kBendCentre));
    return std::clamp(kBendCentre + static_cast<int>(offset), 0, 16383);
}

void NoteOut::reset() noexcept
{
    count = 0;
    nextChannel = kFirstMemberChannel;
}

void NoteOut::setMpe(bool on, juce::MidiBuffer& out, int samplePosition) noexcept
{
    if (on == mpe)
        return;
    releaseAll(out, samplePosition);
    mpe = on;
}

void NoteOut::releaseAll(juce::MidiBuffer& out, int samplePosition) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const auto& n = notes[static_cast<std::size_t>(i)];
        out.addEvent(juce::MidiMessage::noteOff(n.channel, n.note), samplePosition);
    }
    count = 0;
}

int NoteOut::allocateChannel() noexcept
{
    // Round robin over the member channels, skipping busy ones, so a released note's channel rests a while.
    constexpr int members = kLastMemberChannel - kFirstMemberChannel + 1;
    for (int tries = 0; tries < members; ++tries)
    {
        const int channel = nextChannel;
        nextChannel = channel == kLastMemberChannel ? kFirstMemberChannel : channel + 1;
        const auto end = notes.begin() + count;
        if (std::none_of(notes.begin(), end, [channel](const Note& n) { return n.channel == channel; }))
            return channel;
    }
    return kFirstMemberChannel; // unreachable: at most six notes on fifteen channels
}

int NoteOut::wantedBend(const dsp::DolphinFlowEngine& engine, const Note& n) const noexcept
{
    if (!mpe)
        return kBendCentre;
    return bendFor(engine.currentPitch(n.voice) - static_cast<float>(n.note));
}

void NoteOut::sync(const dsp::DolphinFlowEngine& engine, int velocity, juce::MidiBuffer& out,
                   int samplePosition) noexcept
{
    struct Wanted
    {
        int note, voice;
    };
    Wanted wanted[dsp::kMaxVoices] = {};
    int wantedCount = 0;
    if (engine.isHeld())
    {
        const auto& voicing = engine.getVoicing();
        for (int v = 0; v < voicing.count; ++v)
            if (std::none_of(wanted, wanted + wantedCount, [&](const Wanted& w) { return w.note == voicing.notes[v]; }))
                wanted[wantedCount++] = {voicing.notes[v], v};
    }

    // Notes that left end first; common tones keep sounding (and follow whichever voice now plays them).
    int kept = 0;
    for (int i = 0; i < count; ++i)
    {
        auto n = notes[static_cast<std::size_t>(i)];
        const auto it = std::find_if(wanted, wanted + wantedCount, [&](const Wanted& w) { return w.note == n.note; });
        if (it == wanted + wantedCount)
        {
            out.addEvent(juce::MidiMessage::noteOff(n.channel, n.note), samplePosition);
            continue;
        }
        n.voice = it->voice;
        notes[static_cast<std::size_t>(kept++)] = n;
    }
    count = kept;

    const auto noteVelocity = static_cast<juce::uint8>(std::clamp(velocity, 1, 127));
    for (int i = 0; i < wantedCount; ++i)
    {
        const auto end = notes.begin() + count;
        if (std::any_of(notes.begin(), end, [&](const Note& n) { return n.note == wanted[i].note; }))
            continue;
        Note n{wanted[i].note, mpe ? allocateChannel() : kPlainChannel, wanted[i].voice, kBendCentre};
        if (mpe)
        {
            // The bend goes first so the note starts where the voice slides from.
            n.bend = wantedBend(engine, n);
            out.addEvent(juce::MidiMessage::pitchWheel(n.channel, n.bend), samplePosition);
        }
        out.addEvent(juce::MidiMessage::noteOn(n.channel, n.note, noteVelocity), samplePosition);
        notes[static_cast<std::size_t>(count++)] = n;
    }
}

void NoteOut::updateBends(const dsp::DolphinFlowEngine& engine, juce::MidiBuffer& out, int samplePosition) noexcept
{
    if (!mpe)
        return;
    for (int i = 0; i < count; ++i)
    {
        auto& n = notes[static_cast<std::size_t>(i)];
        const int bend = wantedBend(engine, n);
        if (bend == n.bend)
            continue;
        n.bend = bend;
        out.addEvent(juce::MidiMessage::pitchWheel(n.channel, bend), samplePosition);
    }
}

bool NoteOut::isGliding(const dsp::DolphinFlowEngine& engine) const noexcept
{
    if (!mpe)
        return false;
    for (int i = 0; i < count; ++i)
    {
        const auto& n = notes[static_cast<std::size_t>(i)];
        if (n.bend != kBendCentre || wantedBend(engine, n) != kBendCentre)
            return true;
    }
    return false;
}
} // namespace dolphinflow
