#pragma once

#include "dsp/CosmosEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace cosmos
{
namespace ids
{
inline const juce::ParameterID mode{"mode", 1};
inline const juce::ParameterID variation{"variation", 1};
inline const juce::ParameterID activity{"activity", 1};
inline const juce::ParameterID shape{"shape", 1};
inline const juce::ParameterID modFreq{"modFreq", 1};
inline const juce::ParameterID filter{"filter", 1};
inline const juce::ParameterID resonance{"resonance", 1};
inline const juce::ParameterID mix{"mix", 1};
inline const juce::ParameterID effectVolume{"effectVolume", 1};
inline const juce::ParameterID time{"time", 1};
inline const juce::ParameterID sync{"sync", 1};
inline const juce::ParameterID tempo{"tempo", 1};
inline const juce::ParameterID repeats{"repeats", 1};
inline const juce::ParameterID modDepth{"modDepth", 1};
inline const juce::ParameterID space{"space", 1};
inline const juce::ParameterID reverbTime{"reverbTime", 1};
inline const juce::ParameterID loopLevel{"loopLevel", 1};
inline const juce::ParameterID hold{"hold", 1};
inline const juce::ParameterID loopReverse{"loopReverse", 1};
inline const juce::ParameterID loopPreFx{"loopPreFx", 1};
} // namespace ids

/** "Mosaic A" ... "Warp D": mode x variation, also the host program list. */
juce::String programName(int index);

class CosmosProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumPrograms = dsp::kNumModes * dsp::kNumVariations;

    CosmosProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cosmos"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return kNumPrograms; }
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override { return programName(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    const dsp::CosmosEngine& getEngine() const noexcept { return engine; }
    dsp::Mode getMode() const noexcept;

    /** From the UI: REC/PLAY/DUB, STOP, CLEAR. Applied at the start of the next block. */
    void sendLooperCommand(dsp::PhraseLooper::Command command) noexcept;

private:
    dsp::Settings readSettings() const noexcept;

    dsp::CosmosEngine engine;
    double currentRate = 44100.0;
    std::atomic<int> pendingCommand{0};

    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* variationParam = nullptr;
    std::atomic<float>* activityParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* modFreqParam = nullptr;
    std::atomic<float>* filterParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* effectVolumeParam = nullptr;
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* tempoParam = nullptr;
    std::atomic<float>* repeatsParam = nullptr;
    std::atomic<float>* modDepthParam = nullptr;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* reverbTimeParam = nullptr;
    std::atomic<float>* loopLevelParam = nullptr;
    std::atomic<float>* holdParam = nullptr;
    std::atomic<float>* loopReverseParam = nullptr;
    std::atomic<float>* loopPreFxParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CosmosProcessor)
};
} // namespace cosmos
