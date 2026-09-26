#pragma once
#include "../../GILLCommon/SongLearnTransport.h"
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/QualityBus.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "FlowDSP.h"
#include "HeatDSP.h"
#include "TuneDSP.h"
#include <array>
#include <atomic>
enum class GillKind { Flow, Heat, Tune };
class GillVocalProcessor final : public juce::AudioProcessor {
public:
    explicit GillVocalProcessor(GillKind, bool liveTune = false);
    ~GillVocalProcessor() override = default;
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tail.load(); }
    int getNumPrograms() override { return kind==GillKind::Tune?5:1; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    bool presetMatches() const;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(GillKind, int qualityDefault=1);
    float value(const juce::String&) const;
    void setValue(const juce::String&,float,bool gesture=true);
    void requestLearning(bool start) { learnCommand.store(start?1:2); }
    gill::LearnProfile savedProfile() const;
    const GillKind kind;
    const bool isLiveTune;
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
    std::atomic<float> inputPeak{0},outputPeak{0},reduction{0},learnProgress{0},pitchHz{0},targetHz{0},pitchConfidence{0};
    std::atomic<int> learnState{0};
    std::atomic<double> uiRate{48000};
    std::atomic<bool> rateSupported{true};
private:
    gill::SongLearnTransport songLearn;
    gill::ModeTransition qualityTransition;
    void process(juce::AudioBuffer<float>&,bool);
    void updateParameters();
    void exchangeProfile();
    gill::FlowDSP flow;
    gill::HeatDSP heat;
    gill::TuneDSP tune;
    std::array<std::vector<float>,2> dryRing;
    size_t dryPosition=0;
    int latency=0;
    std::array<std::atomic<float>*,7> parameterValues{};
    float parameter(size_t i) const noexcept;
    juce::SmoothedValue<float> bypassFade;
    std::atomic<int> learnCommand{0},currentProgram{0};
    std::atomic<double> tail{0};
    mutable juce::SpinLock profileLock;
    gill::LearnProfile profile;
    bool profilePending=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillVocalProcessor)
};
