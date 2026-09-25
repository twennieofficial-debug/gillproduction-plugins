#pragma once
#include "../../GILLCommon/QualityBus.h"
#include <JuceHeader.h>
#include "EqDSP.h"
#include <array>
#include <atomic>

class GilleqAudioProcessor final : public juce::AudioProcessor {
public:
    GilleqAudioProcessor();
    ~GilleqAudioProcessor() override = default;
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
    const juce::String getName() const override { return "GILLEQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "DEFAULT"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    gill::Bands getBands() const;
    struct ResponseSnapshot {
        gill::Bands bands{};
        double sampleRate = 48000.0, trim = 1.0, delta = 0.0;
        bool bypassed = false, mono = false;
    };
    ResponseSnapshot getResponseSnapshot() const;
    double getResponseDb(double hz, int mode = 0) const;
    double getResponseDb(double hz, int mode, const ResponseSnapshot&) const;
    float getBandDynamicGainDb(int index) const;
    float getBandDetectorDb(int index) const;
    bool readSpectrum(std::array<float,2048>& pre, std::array<float,2048>& post);
    float getInputDb() const { return inputDb.load(std::memory_order_relaxed); }
    float getOutputDb() const { return outputDb.load(std::memory_order_relaxed); }
    void copyAtoB();
    void swapAB();
    void resetAllBands();
    void setAnalyzerEnabled(bool enabled) { analyzerEnabled.store(enabled, std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
private:
    template<class T> void process(juce::AudioBuffer<T>&, bool hostBypassed);
    void feedAnalyzer(float pre, float post);
    void publishDynamics();
    struct BandAtoms { std::atomic<float>* enabled{}, *type{}, *freq{}, *gain{}, *q{}, *channel{}, *slope{}, *dynamic{}, *threshold{}, *range{}, *attack{}, *release{}; };
    std::array<BandAtoms,8> bandAtoms{};
    std::atomic<float>* outputParam{}, *bypassParam{}, *deltaParam{};
    gill::EqEngine engine;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gainSmooth, bypassSmooth, deltaSmooth;
    std::atomic<float> inputDb{-100.f}, outputDb{-100.f};
    struct LiveBand {
        std::atomic<float> gain{0}, dynamicGain{0}, detectorDb{-160}, frequency{1000}, q{0.70710678f};
        std::atomic<int> type{-1}, channel{0};
        std::atomic<bool> active{false};
    };
    std::array<LiveBand,8> liveBands{};
    std::atomic<unsigned> liveSequence{0};
    std::atomic<bool> analyzerEnabled{true}, spectrumReady{false};
    std::array<float,2048> inputAccumulator{}, outputAccumulator{}, publishedPre{}, publishedPost{};
    int analyzerIndex = 0;
    juce::ValueTree alternateState;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GilleqAudioProcessor)
};
