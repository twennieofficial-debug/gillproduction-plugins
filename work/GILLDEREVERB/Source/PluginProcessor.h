#pragma once
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/QualityBus.h"
#include <JuceHeader.h>
#include "DereverbDSP.h"
#include <array>
#include <atomic>

class GillDereverbAudioProcessor final : public juce::AudioProcessor {
public:
    GillDereverbAudioProcessor();
    ~GillDereverbAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "GILLDEREVERB"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4096.0 / (getSampleRate() > 0 ? getSampleRate() : 48000.0); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "DEFAULT"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    bool readSpectrum(std::array<float,2048>& pre, std::array<float,2048>& post);
    float getInputDb() const { return inputDb.load(std::memory_order_relaxed); }
    float getOutputDb() const { return outputDb.load(std::memory_order_relaxed); }
    float getReductionDb() const { return reductionDb.load(std::memory_order_relaxed); }
    void copyAtoB();
    void swapAB();
    void resetAllParameters();
    // A real edit of the new single control opts a restored legacy instance
    // into the automatic model. Ordinary host automation never does this.
    void beginUserAmountGesture();
    bool isAutomaticMode() const { return automaticMode.load(std::memory_order_relaxed); }
    void setAnalyzerEnabled(bool enabled) { analyzerEnabled.store(enabled, std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
private:
    gill::ModeTransition qualityTransition;
    template<class T> void process(juce::AudioBuffer<T>&, bool hostBypassed);
    void feedAnalyzer(float pre, float post);
    std::atomic<float> *amountParam{}, *roomParam{}, *preserveParam{}, *lowParam{}, *highParam{}, *mixParam{}, *outputParam{}, *removedParam{}, *bypassParam{};
    gilldereverb::DereverbEngine engine;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gainSmooth, bypassSmooth, removedSmooth, mixSmooth;
    std::atomic<float> inputDb{-100.f}, outputDb{-100.f}, reductionDb{0.f};
    std::atomic<bool> automaticMode{true};
    std::atomic<bool> analyzerEnabled{false}, spectrumReady{false};
    std::array<float,2048> inputAccumulator{}, outputAccumulator{}, publishedPre{}, publishedPost{};
    int analyzerIndex = 0;
    juce::ValueTree alternateState;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillDereverbAudioProcessor)
};
