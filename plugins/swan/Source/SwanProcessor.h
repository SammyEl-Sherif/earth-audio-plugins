#pragma once

#include "dsp/SwanEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace swan
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
} // namespace ids

/**
 * Chord instrument. A root comes from a computer key (via the editor) or a MIDI note; the chord quality is
 * the CHORD parameter. Held roots stack: letting go of the newest falls back to the one still held.
 */
class SwanProcessor : public juce::AudioProcessor
{
public:
    /** Editor key sources are numbered from here so they never collide with MIDI note numbers. */
    static constexpr int kKeySourceBase = 1000;

    SwanProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Swan"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
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

    const dsp::SwanEngine& getEngine() const noexcept { return engine; }

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
    };

    void pushSettings() noexcept;
    int currentShape() const noexcept;
    void press(int source, int root) noexcept;
    void release(int source) noexcept;
    void releaseKeys() noexcept;
    void afterRelease() noexcept;
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    void drainUiEvents() noexcept;
    void postUiEvent(const UiEvent& event);

    dsp::SwanEngine engine;

    std::array<Held, 32> held{};
    int heldCount = 0;
    int lastShape = 0;
    bool lastLatch = false;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwanProcessor)
};
} // namespace swan
