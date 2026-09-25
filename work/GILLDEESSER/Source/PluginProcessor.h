#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DeEsserDSP.h"
#include <array>
#include <atomic>

class GillDeEsserAudioProcessor final : public juce::AudioProcessor {
public:
    GillDeEsserAudioProcessor();
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&,juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return "GILL-DE-ESSER";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return tailSeconds.load(std::memory_order_relaxed);}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "DEFAULT";}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float getReductionDb() const {return reductionDb.load(std::memory_order_relaxed);}
    float getSibilanceDb() const {return sibilanceDb.load(std::memory_order_relaxed);}
    double getUiSampleRate() const {return uiSampleRate.load(std::memory_order_relaxed);}
    bool readSpectrum(std::array<float,2048>&,std::array<float,2048>&);
    juce::AudioProcessorValueTreeState apvts;
private:
    template<class T> void process(juce::AudioBuffer<T>&,bool);
    void feedAnalyzer(float,float);
    gilldeesser::DeEsserEngine engine;
    std::atomic<float> *amount{},*frequency{},*listen{},*bypass{};
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> bypassSmooth,listenSmooth;
    std::atomic<float> reductionDb{0},sibilanceDb{-160};
    std::atomic<double> uiSampleRate{48000},tailSeconds{0.25};
    std::atomic<bool> spectrumReady{false};
    std::array<float,2048> accumulatorPre{},accumulatorPost{},publishedPre{},publishedPost{};
    int spectrumIndex=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillDeEsserAudioProcessor)
};
