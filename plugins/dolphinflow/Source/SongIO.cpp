#include "SongIO.h"

#include "dsp/DolphinFlowEngine.h"

#include <algorithm>
#include <cmath>

namespace dolphinflow
{
namespace
{
const juce::Identifier kSong{"Song"};
const juce::Identifier kSection{"Section"};
const juce::Identifier kChord{"Chord"};
const juce::Identifier kTrack{"Track"};
const juce::Identifier kName{"name"};
const juce::Identifier kColour{"colour"};
const juce::Identifier kRoot{"root"};
const juce::Identifier kShape{"shape"};
const juce::Identifier kBeats{"beats"};
const juce::Identifier kRole{"role"};
const juce::Identifier kMute{"mute"};
const juce::Identifier kSolo{"solo"};
const juce::Identifier kVolume{"volume"};
const juce::Identifier kNotes{"notes"};
const juce::Identifier kChordMute{"chordMute"};
const juce::Identifier kChordSolo{"chordSolo"};
const juce::Identifier kChordVolume{"chordVolume"};
// The eight squares of earlier versions.
const juce::Identifier kProgression{"Progression"};
const juce::Identifier kLength{"length"};

constexpr int kTicksPerBeat = 960;
constexpr int kKeyVelocity = 100;
constexpr int kMpeLastMember = dsp::kFirstTrackChannel - 1;
constexpr double kBendStepBeats = 1.0 / 64.0;
constexpr float kBendRangeSemitones = 48.0f;

double ticks(double beats)
{
    return std::round(beats * kTicksPerBeat);
}

void writeMix(juce::ValueTree& node, const dsp::TrackMix& mix, const juce::Identifier& mute,
              const juce::Identifier& solo, const juce::Identifier& volume)
{
    node.setProperty(mute, mix.mute, nullptr);
    node.setProperty(solo, mix.solo, nullptr);
    node.setProperty(volume, mix.volume, nullptr);
}

dsp::TrackMix readMix(const juce::ValueTree& node, const juce::Identifier& mute, const juce::Identifier& solo,
                      const juce::Identifier& volume)
{
    dsp::TrackMix mix;
    mix.mute = node.getProperty(mute, false);
    mix.solo = node.getProperty(solo, false);
    mix.volume = static_cast<float>(static_cast<double>(node.getProperty(volume, 0.8)));
    return mix;
}

int bendFor(float semitones)
{
    return std::clamp(8192 + static_cast<int>(std::lround(semitones / kBendRangeSemitones * 8192.0f)), 0, 16383);
}

/** RPN on `channel`: 101/100 select it, 6 sets its value. */
void addRpn(juce::MidiMessageSequence& seq, int channel, int rpn, int value)
{
    seq.addEvent(juce::MidiMessage::controllerEvent(channel, 101, 0), 0.0);
    seq.addEvent(juce::MidiMessage::controllerEvent(channel, 100, rpn), 0.0);
    seq.addEvent(juce::MidiMessage::controllerEvent(channel, 6, value), 0.0);
}

void addHeader(juce::MidiMessageSequence& seq, const juce::String& name, double bpm)
{
    seq.addEvent(juce::MidiMessage::textMetaEvent(3, name), 0.0);
    seq.addEvent(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60.0e6 / std::max(1.0, bpm))), 0.0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
}

juce::MidiMessageSequence chordTrack(const dsp::Song& song, double from, double to, const ClipVoicing& voicing)
{
    juce::MidiMessageSequence seq;
    addHeader(seq, "Dolphin Flow Chords", voicing.bpm);
    if (voicing.mpe)
    {
        // MPE configuration: a lower zone on manager channel 1 with the chord's member channels, 48 semitone bends.
        addRpn(seq, 1, 6, kMpeLastMember - 1);
        for (int channel = 2; channel <= kMpeLastMember; ++channel)
            addRpn(seq, channel, 0, static_cast<int>(kBendRangeSemitones));
    }
    const double glideBeats = voicing.glideMs / 1000.0 * voicing.bpm / 60.0;
    const auto notes = dsp::voiceChordTrack(song, voicing.voices, voicing.centre, from, to, voicing.mpe);
    for (const auto& n : notes)
    {
        const double start = n.start - from;
        const double end = n.end - from;
        if (voicing.mpe)
        {
            // The bend goes first so the note starts where its voice slides from, then follows the glide home.
            seq.addEvent(juce::MidiMessage::pitchWheel(n.channel, bendFor(n.bendFrom)), ticks(start));
            if (n.bendFrom != 0.0f && glideBeats > 0.0)
            {
                const double span = std::min(glideBeats, end - start);
                for (double t = kBendStepBeats; t < span + 1.0e-9; t += kBendStepBeats)
                {
                    const float progress =
                        dsp::glideShape(static_cast<float>(std::min(1.0, t / glideBeats)), voicing.curve);
                    seq.addEvent(juce::MidiMessage::pitchWheel(n.channel, bendFor(n.bendFrom * (1.0f - progress))),
                                 ticks(start + t));
                }
                seq.addEvent(juce::MidiMessage::pitchWheel(n.channel, 8192), ticks(start + span));
            }
        }
        seq.addEvent(juce::MidiMessage::noteOn(n.channel, n.note, static_cast<juce::uint8>(kKeyVelocity)),
                     ticks(start));
        seq.addEvent(juce::MidiMessage::noteOff(n.channel, n.note), ticks(end));
    }
    return seq;
}

juce::MidiMessageSequence melodyTrack(const dsp::MelodyTrack& track, double from, double to, double bpm)
{
    juce::MidiMessageSequence seq;
    addHeader(seq, "Dolphin Flow " + juce::String(track.name), bpm);
    for (const auto& n : track.notes)
    {
        const double start = std::max(n.start, from);
        const double end = std::min(n.end(), to);
        if (end <= start + 1.0e-9 || n.start < from - 1.0e-9)
            continue; // a note that began before the clip belongs to the previous one
        seq.addEvent(juce::MidiMessage::noteOn(1, n.pitch, static_cast<juce::uint8>(std::clamp(n.velocity, 1, 127))),
                     ticks(start - from));
        seq.addEvent(juce::MidiMessage::noteOff(1, n.pitch), ticks(end - from));
    }
    return seq;
}

void finish(juce::MidiMessageSequence& seq, double length)
{
    seq.addEvent(juce::MidiMessage::endOfTrack(), ticks(length));
    seq.sort();
    seq.updateMatchedPairs();
}
} // namespace

juce::ValueTree songToTree(const dsp::Song& song)
{
    juce::ValueTree root(kSong);
    root.setProperty(kChordMute, song.chordMix.mute, nullptr);
    root.setProperty(kChordSolo, song.chordMix.solo, nullptr);
    root.setProperty(kChordVolume, song.chordMix.volume, nullptr);
    for (const auto& section : song.sections)
    {
        juce::ValueTree node(kSection);
        node.setProperty(kName, juce::String(section.name), nullptr);
        node.setProperty(kColour, section.colour, nullptr);
        for (const auto& c : section.chords)
        {
            juce::ValueTree chord(kChord);
            chord.setProperty(kRoot, c.root, nullptr);
            chord.setProperty(kShape, c.shape, nullptr);
            chord.setProperty(kBeats, c.beats, nullptr);
            node.appendChild(chord, nullptr);
        }
        root.appendChild(node, nullptr);
    }
    for (const auto& track : song.tracks)
    {
        juce::ValueTree node(kTrack);
        node.setProperty(kName, juce::String(track.name), nullptr);
        node.setProperty(kRole, static_cast<int>(track.role), nullptr);
        writeMix(node, track.mix, kMute, kSolo, kVolume);
        // Compact: "pitch start length velocity" per note, notes separated by ';'.
        juce::StringArray notes;
        for (const auto& n : track.notes)
            notes.add(juce::String(n.pitch) + " " + juce::String(n.start, 4) + " " + juce::String(n.length, 4) + " " +
                      juce::String(n.velocity));
        node.setProperty(kNotes, notes.joinIntoString(";"), nullptr);
        root.appendChild(node, nullptr);
    }
    return root;
}

void sanitise(dsp::Song& song)
{
    if (song.sections.size() > static_cast<std::size_t>(dsp::kMaxSections))
        song.sections.resize(static_cast<std::size_t>(dsp::kMaxSections));
    int chords = 0;
    for (auto& section : song.sections)
    {
        section.colour = std::clamp(section.colour, 0, 15);
        if (section.name.size() > 24)
            section.name.resize(24);
        for (auto& c : section.chords)
        {
            c.root = std::clamp(c.root, 0, 11);
            c.shape = std::clamp(c.shape, 0, dsp::kNumChordShapes - 1);
            c.beats = std::clamp(c.beats, dsp::kMinChordBeats, dsp::kMaxChordBeats);
        }
        const auto room = static_cast<std::size_t>(std::max(0, dsp::kMaxSongChords - chords));
        if (section.chords.size() > room)
            section.chords.resize(room);
        chords += static_cast<int>(section.chords.size());
    }
    if (song.tracks.size() > static_cast<std::size_t>(dsp::kMaxMelodyTracks))
        song.tracks.resize(static_cast<std::size_t>(dsp::kMaxMelodyTracks));
    const auto clampMix = [](dsp::TrackMix& mix) { mix.volume = std::clamp(mix.volume, 0.0f, 1.0f); };
    clampMix(song.chordMix);
    for (auto& track : song.tracks)
    {
        clampMix(track.mix);
        if (track.name.size() > 24)
            track.name.resize(24);
        track.role = static_cast<dsp::TrackRole>(std::clamp(static_cast<int>(track.role), 0, 3));
        if (track.notes.size() > static_cast<std::size_t>(dsp::kMaxTrackNotes))
            track.notes.resize(static_cast<std::size_t>(dsp::kMaxTrackNotes));
        for (auto& n : track.notes)
        {
            n.pitch = std::clamp(n.pitch, 0, 127);
            n.start = std::max(0.0, n.start);
            n.length = std::clamp(n.length, 1.0 / 64.0, 256.0);
            n.velocity = std::clamp(n.velocity, 1, 127);
        }
        dsp::sortNotes(track);
    }
}

dsp::Song songFromState(const juce::ValueTree& state)
{
    const auto node = state.getChildWithName(kSong);
    if (node.isValid())
    {
        dsp::Song song;
        song.chordMix = readMix(node, kChordMute, kChordSolo, kChordVolume);
        for (const auto& child : node)
        {
            if (child.hasType(kSection))
            {
                dsp::Section section;
                section.name = child.getProperty(kName, "Verse").toString().toStdString();
                section.colour = child.getProperty(kColour, 0);
                for (const auto& c : child)
                    if (c.hasType(kChord))
                        section.chords.push_back({static_cast<int>(c.getProperty(kRoot, 0)),
                                                  static_cast<int>(c.getProperty(kShape, 0)),
                                                  static_cast<double>(c.getProperty(kBeats, 4.0))});
                song.sections.push_back(section);
            }
            else if (child.hasType(kTrack))
            {
                dsp::MelodyTrack track;
                track.name = child.getProperty(kName, "Lead").toString().toStdString();
                track.role = static_cast<dsp::TrackRole>(static_cast<int>(child.getProperty(kRole, 0)));
                track.mix = readMix(child, kMute, kSolo, kVolume);
                for (const auto& text : juce::StringArray::fromTokens(child.getProperty(kNotes).toString(), ";", {}))
                {
                    const auto parts = juce::StringArray::fromTokens(text, " ", {});
                    if (parts.size() == 4)
                        track.notes.push_back({parts[0].getIntValue(), parts[1].getDoubleValue(),
                                               parts[2].getDoubleValue(), parts[3].getIntValue()});
                }
                song.tracks.push_back(track);
            }
        }
        sanitise(song);
        return song;
    }

    auto song = dsp::defaultSong();
    const auto progression = state.getChildWithName(kProgression);
    if (progression.isValid() && progression.getNumChildren() > 0)
    {
        auto& verse = song.sections.front();
        verse.chords.clear();
        for (const auto& slot : progression)
        {
            const int root = slot.getProperty(kRoot, -1);
            if (root < 0)
                continue;
            const int length = std::clamp(static_cast<int>(slot.getProperty(kLength, dsp::kDefaultLengthIndex)), 0,
                                          dsp::kNumSlotLengths - 1);
            verse.chords.push_back({root, static_cast<int>(slot.getProperty(kShape, 0)), dsp::kSlotBeats[length]});
        }
    }
    sanitise(song);
    return song;
}

juce::MidiFile buildClip(const dsp::Song& song, const ClipScope& scope, const ClipVoicing& voicing)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote(kTicksPerBeat);
    const double songLength = dsp::songBeats(song);
    const double from = std::max(0.0, scope.from);
    const double to = scope.to < 0.0 ? songLength : std::min(scope.to, std::max(songLength, scope.to));
    if (to <= from + 1.0e-9)
        return file;
    const double length = to - from;

    bool any = false;
    if (scope.allTracks || scope.track < 0)
    {
        auto seq = chordTrack(song, from, to, voicing);
        any = any || seq.getNumEvents() > 3;
        finish(seq, length);
        file.addTrack(seq);
    }
    for (int t = 0; t < static_cast<int>(song.tracks.size()); ++t)
        if (scope.allTracks || scope.track == t)
        {
            auto seq = melodyTrack(song.tracks[static_cast<std::size_t>(t)], from, to, voicing.bpm);
            any = any || seq.getNumEvents() > 3;
            finish(seq, length);
            file.addTrack(seq);
        }
    if (!any)
        return {};
    return file;
}

juce::String clipTitle(const dsp::Song& song, const ClipScope& scope, int homeTonic, int homeScale)
{
    if (scope.allTracks)
        return "Song";
    if (scope.track >= 0 && scope.track < static_cast<int>(song.tracks.size()))
        return juce::String(song.tracks[static_cast<std::size_t>(scope.track)].name);
    juce::StringArray names;
    const double to = scope.to < 0.0 ? 1.0e9 : scope.to;
    for (const auto& c : dsp::placeChords(song))
        if (c.end() > scope.from + 1.0e-9 && c.start < to - 1.0e-9 && names.size() < 8)
            names.add(dsp::chordNameInKey(c.root, c.shape, homeTonic, homeScale).text);
    return "Chords - " + names.joinIntoString(" ");
}
} // namespace dolphinflow
