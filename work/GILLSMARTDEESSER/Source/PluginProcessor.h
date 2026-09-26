#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../GILLCommon/QualityBus.h"
#include "Learning.h"

class GillSmartDeEsserProcessor final : public juce::AudioProcessor {
public:
    enum LearnState { idle, learning, ready, applied, insufficient, armed };
    GillSmartDeEsserProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void prepareToPlay(double,int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&,juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override{return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return "GILLSMARTDEESSER";}
    bool acceptsMidi() const override{return false;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return .05;}
    int getNumPrograms() override{return 5;}
    int getCurrentProgram() override{return currentProgram.load();}
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override{return apvts.getParameter("bypass");}
    float value(const char*) const noexcept;
    void setValue(const char*,float,bool gesture=true);
    void startLearning();
    void finishLearning();
    bool applyLearned();
    bool undoLearned();
    bool learnedCandidate(gillsmart::Profile&) const noexcept;
    bool canUndo() const noexcept{return undoAvailable.load();}
    std::atomic<int> learnState{idle};
    std::atomic<float> activeSeconds{0},elapsedSeconds{0},reduction{0},sibilance{-160};
    std::atomic<double> uiRate{48000};
    std::atomic<bool> supported{true};
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this,apvts};
private:
    template<class T> void process(juce::AudioBuffer<T>&,bool bypassed);
    void publish(const gillsmart::Profile&) noexcept;
    gillsmart::SmartEngine engine;
    gillsmart::Learner learner;
    std::array<std::atomic<float>*,7> realtimeParameters{};
    bool collecting=false, waitingForPlay=false, haveLearnPosition=false;
    double expectedLearnSeconds=0;
    std::atomic<int> request{0},currentProgram{0};
    std::atomic<unsigned> candidateVersion{0};
    std::array<std::atomic<float>,8> candidate{};
    std::array<float,9> undoValues{};
    std::atomic<bool> undoAvailable{false};
    juce::SmoothedValue<double> wetRamp,listenRamp;
    std::array<std::array<double,256>,2> original{};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillSmartDeEsserProcessor)
};
