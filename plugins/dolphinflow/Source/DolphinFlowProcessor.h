#pragma once

#include "EchoGuard.h"
#include "MpePort.h"
#include "NoteOut.h"
#include "dsp/DolphinFlowEngine.h"
#include "SongIO.h"
#include "dsp/SongPlayback.h"
#include "dsp/Theory.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>

namespace dolphinflow
{
namespace ids
{
inline const juce::ParameterID chord{"chord", 1};
inline const juce::ParameterID mode{"mode", 1};
inline const juce::ParameterID voices{"voices", 1};
inline const juce::ParameterID octave{"octave", 1};
inline const juce::ParameterID glide{"glide", 1};
inline const juce::ParameterID curve{"curve", 1};
inline const juce::ParameterID tone{"tone", 1};
inline const juce::ParameterID width{"width", 1};
inline const juce::ParameterID space{"space", 1};
inline const juce::ParameterID level{"level", 1};
inline const juce::ParameterID latch{"latch", 1};
inline const juce::ParameterID loop{"loop", 1};
inline const juce::ParameterID mpe{"mpe", 1};
inline const juce::ParameterID key{"key", 1};
inline const juce::ParameterID scale{"scale", 1};
} // namespace ids

/**
 * Chord instrument. A root comes from a computer key (via the editor) or a MIDI note; the chord quality is
 * the CHORD parameter. Held roots stack: letting go of the newest falls back to the one still held. Three or more
 * MIDI notes held together are played as that exact voicing, so a clip recorded from the MIDI output (which
 * carries the voiced notes being played, glides as MPE pitch bends: see NoteOut) plays back the same. Notes still
 * sounding when the host starts playing or recording are sent again then, so a latched chord or the loop is
 * captured from the first beat.
 */
class DolphinFlowProcessor : public juce::AudioProcessor
{
public:
    /** Editor key sources are numbered from here so they never collide with MIDI sources (channel * 128 + note). */
    static constexpr int kKeySourceBase = 4096;

    DolphinFlowProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dolphin Flow"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Message thread: the editor's keys and buttons. Delivered to the audio thread at the next block.
    void pressRoot(int source, int rootPitchClass);
    void releaseRoot(int source);
    void releaseAllKeys();
    /** Sets a parameter as a user gesture (so hosts record it), from a key press or click. */
    void setParameterFromUi(const juce::ParameterID& id, float realValue);
    float getParameterValue(const juce::ParameterID& id) const;

    // The song: sections of chords plus melody tracks (lead, bass...). Message thread.
    const dsp::Song& getSong() const noexcept { return song; }
    /** Replaces the song (sanitised), saves it in the state and hands it to the audio thread. `undoable` pushes the
     * previous song onto the undo stack. */
    void setSong(const dsp::Song& newSong, bool undoable = true);
    /** Pushes `before` as one undo step (a drag edits live with undoable = false, then commits once). */
    void pushUndo(const dsp::Song& before);
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return !undoStack.empty(); }
    bool canRedo() const noexcept { return !redoStack.empty(); }
    /** Frees songs the audio thread has finished with (the editor's timer calls it). */
    void collectSongs() { songStore.collect(); }
    const dsp::PlaySong& getPlaySong() const noexcept { return songStore.latest(); }

    // Transport. PLAY runs the song from the playhead (to its end, or round and round with LOOP on). LOOP alone also
    // runs it. LINK follows the host: the song plays whenever the host plays, at the host's beat position.
    void setPlaying(bool shouldPlay);
    bool isPlaying() const noexcept { return transportPlaying.load(); }
    void rewind() { seek(0.0); }
    /** Moves the playhead (the host's position wins while it plays). */
    void seek(double beats);
    void setLinked(bool shouldLink);
    bool isLinked() const noexcept { return linked.load(); }
    /** Where the song is (beats, inside the song), whether it is running, and the chord it plays (-1 none). */
    double getPlayheadBeats() const noexcept { return playhead.load(std::memory_order_relaxed); }
    bool isSequencing() const noexcept { return sequencingShown.load(std::memory_order_relaxed); }
    int getPlayingChord() const noexcept { return playingChord.load(std::memory_order_relaxed); }
    double getTempo() const noexcept { return tempo.load(std::memory_order_relaxed); }
    /** Sounds a melody track's note through MIDI out for a moment (piano roll clicks). */
    void previewNote(int track, int pitch, int velocity);

    /** A part of the song as a standard MIDI file (see buildClip). False if empty or unwritable. */
    bool writeMidiClip(const juce::File& file, const ClipScope& scope = {}) const;
    /** ~/Music/Animal Audio/Dolphin Flow/Dolphin Flow - <title>.mid */
    juce::File clipFile(const ClipScope& scope = {}) const;
    ClipVoicing clipVoicing() const;

    const dsp::DolphinFlowEngine& getEngine() const noexcept { return engine; }
    /** The virtual MIDI port mirroring MIDI out (empty name where the system can't make one). */
    juce::String getMpePortName() const { return mpePort.getPortName(); }
    /** Message thread: opens the virtual MPE port (prepareToPlay does this when a host loads the plugin). */
    void openMpePort(const juce::String& name = {}) { mpePort.open(name); }

    // Key: the KEY / SCALE parameters are home; the palette can borrow from neighbouring keys and parallel modes.
    // Message thread. The borrowed position is saved with the plugin state, not automated.
    dsp::PaletteKey getPaletteKey() const;
    void setPaletteBorrow(int shift, int scale);
    unsigned getHomeKeyMask() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    struct UiEvent
    {
        enum class Type
        {
            Press,
            Release,
            ReleaseAllKeys,
            Rewind,
            Preview // source = channel, root = pitch, velocity
        } type;
        int source;
        int root;
        int velocity = 0;
    };

    struct Held
    {
        int source;
        int root;
        int note; // MIDI note, or -1 for an editor key
    };

    /** What the held keys ask for; the engine is only told when this changes. */
    struct Intent
    {
        enum class Kind
        {
            None,
            Chord,  // newest root with the CHORD quality, voice-led
            Voicing // three or more MIDI notes held: play exactly those
        } kind = Kind::None;
        int root = 0;
        int shape = 0;
        int octave = 0; // the newest MIDI root's octave against kReferenceOctave; 0 for an editor key
        dsp::Voicing voicing;

        bool operator==(const Intent& other) const noexcept
        {
            if (kind != other.kind || root != other.root || shape != other.shape || octave != other.octave)
                return false;
            if (kind != Kind::Voicing)
                return true;
            return voicing.count == other.voicing.count &&
                   std::equal(voicing.notes, voicing.notes + voicing.count, other.voicing.notes);
        }
    };

    static constexpr int kMaxHeld = 32;
    static constexpr int kMinVoicedNotes = 3;
    static constexpr int kKeyVelocity = 100;
    static constexpr int kReferenceOctave = 5; // MIDI 60-71 (Live's C3, its computer keyboard's home) plays at OCTAVE
    static constexpr int kMaxTotalOctaves = 3; // OCTAVE plus the played octave, in either direction

    void pushSettings() noexcept;
    int currentShape() const noexcept;
    void press(int source, int root, int note, int velocity) noexcept;
    void release(int source) noexcept;
    void releaseKeys() noexcept;
    Intent wantedIntent() const noexcept;
    void resolve() noexcept;
    void syncMidiOut(int samplePosition) noexcept;
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    bool isOwnEcho(const juce::MidiMessage& message) const noexcept;
    void rememberSent(const juce::MidiBuffer& buffer) noexcept;
    void drainUiEvents() noexcept;
    void postUiEvent(const UiEvent& event);
    void loadSongFromState();
    void publishSong();
    double songPosition(const dsp::PlaySong& play) const noexcept;
    void stepSong(const dsp::PlaySong& play, double& samplesToBoundary, double beatsPerSample) noexcept;
    void emitMelody(const dsp::PlaySong& play, double from, double to, int sampleStart, double beatsPerSample,
                    int numSamples) noexcept;
    void melodyOff(int samplePosition) noexcept;
    void stopSequencing() noexcept;
    void sendPreviews(int numSamples) noexcept;

    dsp::DolphinFlowEngine engine;

    std::array<Held, kMaxHeld> held{};
    int heldCount = 0;
    Intent intent;
    int lastVelocity = kKeyVelocity;
    int playedOctave = 0; // the newest MIDI root's octave against kReferenceOctave
    juce::MidiBuffer midiOut;
    NoteOut noteOut;
    MpePort mpePort;
    std::uint32_t blockMs = 0; // wall clock at this block's start, for EchoGuard
    bool lastPlaying = false;
    bool lastRecording = false;
    int lastShape = 0;
    bool lastLatch = false;

    dsp::Song song;
    std::vector<dsp::Song> undoStack;
    std::vector<dsp::Song> redoStack;
    dsp::SongStore songStore;

    std::atomic<bool> transportPlaying{false};
    std::atomic<double> seekTarget{0.0};
    std::atomic<bool> linked{false};
    bool sequencing = false;
    double sequenceBeats = 0.0; // unwrapped song position
    int sequenceChord = -1;
    int sequenceRoot = -1;
    int sequenceShape = -1;
    double sequenceInto = 0.0;
    int settledVoices = 4;
    int settledCentre = dsp::kDefaultCentre;
    std::atomic<double> playhead{0.0};
    std::atomic<bool> sequencingShown{false};
    std::atomic<int> playingChord{-1};
    std::atomic<double> tempo{120.0};

    // Melody tracks: notes sounding now (MIDI out), and where the next note to start is.
    struct SoundingNote
    {
        int channel;
        int pitch;
        double end; // song position (wrapped)
    };
    static constexpr int kMaxSounding = 64;
    std::array<SoundingNote, kMaxSounding> sounding{};
    int soundingCount = 0;
    std::size_t melodyCursor = 0;
    double melodyPosition = -1.0; // wrapped position melody was last emitted up to, -1 = must re-seek

    struct PreviewOff
    {
        int channel;
        int pitch;
        int samplesLeft;
    };
    static constexpr int kMaxPreviews = 16;
    std::array<PreviewOff, kMaxPreviews> previews{};
    int previewCount = 0;

    std::array<UiEvent, 128> uiEvents{};
    juce::AbstractFifo uiFifo{128};

    std::atomic<float>* chordParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* octaveParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* curveParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* latchParam = nullptr;
    std::atomic<float>* loopParam = nullptr;
    std::atomic<float>* mpeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DolphinFlowProcessor)
};
} // namespace dolphinflow
