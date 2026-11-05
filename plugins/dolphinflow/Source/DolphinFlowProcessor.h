#pragma once

#include "dsp/DolphinFlowEngine.h"
#include "dsp/Progression.h"

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
} // namespace ids

/**
 * Chord instrument. A root comes from a computer key (via the editor) or a MIDI note; the chord quality is
 * the CHORD parameter. Held roots stack: letting go of the newest falls back to the one still held. Three or more
 * MIDI notes held together are played as that exact voicing, so a clip recorded from the MIDI output (which
 * carries the voiced notes being played) plays back the same.
 */
class DolphinFlowProcessor : public juce::AudioProcessor
{
public:
    /** Editor key sources are numbered from here so they never collide with MIDI note numbers. */
    static constexpr int kKeySourceBase = 1000;

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

    // Arrangement: eight chord squares that loop while LOOP is on. Message thread, except the getters marked any.
    dsp::Slot getSlot(int index) const noexcept; // any thread
    void setSlot(int index, const dsp::Slot& slot);
    /** Slot the loop is playing (-1 when not looping) and how far through it, 0..1. Any thread. */
    int getPlayingSlot() const noexcept { return playingSlot.load(std::memory_order_relaxed); }
    float getSlotProgress() const noexcept { return slotProgress.load(std::memory_order_relaxed); }
    double getTempo() const noexcept { return tempo.load(std::memory_order_relaxed); }
    /** One pass of the loop as a standard MIDI file, voiced as it plays. False if empty or unwritable. */
    bool writeMidiClip(const juce::File& file) const;
    /** ~/Music/Animal Audio/Dolphin Flow/Dolphin Flow - <chords>.mid */
    juce::File defaultClipFile() const;

    const dsp::DolphinFlowEngine& getEngine() const noexcept { return engine; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct UiEvent
    {
        enum class Type
        {
            Press,
            Release,
            ReleaseAllKeys
        } type;
        int source;
        int root;
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
        dsp::Voicing voicing;

        bool operator==(const Intent& other) const noexcept
        {
            if (kind != other.kind || root != other.root || shape != other.shape)
                return false;
            if (kind != Kind::Voicing)
                return true;
            return voicing.count == other.voicing.count &&
                   std::equal(voicing.notes, voicing.notes + voicing.count, other.voicing.notes);
        }
    };

    static constexpr int kMaxHeld = 32;
    static constexpr int kMinVoicedNotes = 3;
    static constexpr int kMidiChannel = 1;
    static constexpr int kKeyVelocity = 100;

    void pushSettings() noexcept;
    int currentShape() const noexcept;
    void press(int source, int root, int note, int velocity) noexcept;
    void release(int source) noexcept;
    void releaseKeys() noexcept;
    Intent wantedIntent() const noexcept;
    void resolve() noexcept;
    void syncMidiOut(int samplePosition) noexcept;
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    void drainUiEvents() noexcept;
    void postUiEvent(const UiEvent& event);
    void loadSlotsFromState();
    void stepSequencer(const dsp::Slots& slots, double& samplesToBoundary, double beatsPerSample) noexcept;

    dsp::DolphinFlowEngine engine;

    std::array<Held, kMaxHeld> held{};
    int heldCount = 0;
    Intent intent;
    int lastVelocity = kKeyVelocity;
    juce::MidiBuffer midiOut;
    std::array<int, dsp::kMaxVoices> outNotes{};
    int outCount = 0;
    int lastShape = 0;
    bool lastLatch = false;

    std::array<std::atomic<int>, dsp::kNumSlots> slotState{};
    bool sequencing = false;
    double sequenceBeats = 0.0;
    int sequenceSlot = -1;
    dsp::Slot sequenceChord;
    double sequenceInto = 0.0;
    int settledVoices = 4;
    int settledCentre = dsp::kDefaultCentre;
    std::atomic<int> playingSlot{-1};
    std::atomic<float> slotProgress{0.0f};
    std::atomic<double> tempo{120.0};

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DolphinFlowProcessor)
};
} // namespace dolphinflow
