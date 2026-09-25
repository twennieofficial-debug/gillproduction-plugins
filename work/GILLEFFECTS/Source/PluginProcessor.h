#pragma once
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/QualityBus.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "AirDSP.h"
#include "SpaceDSP.h"
#include "EchoDSP.h"
#include "BalanceDSP.h"
#include <array>
#include <atomic>

enum class GillKind { Air, Space, Echo, Balance };
struct BalanceView {
    std::array<float,8> gains{},pre{},post{};
    std::array<bool,8> active{};
    gill::FineBalanceView fine;
};
class GillEffectProcessor final : public juce::AudioProcessor {
public:
    explicit GillEffectProcessor(GillKind);
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
    int getNumPrograms() override { return kind==GillKind::Space||kind==GillKind::Echo?12:1; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int i) override { selectPreset(i,false); }
    void selectPreset(int,bool gesture=true);
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    bool presetMatches() const;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(GillKind);
    float value(const juce::String&) const;
    void setValue(const juce::String&,float,bool gesture=true);
    void requestLearning(bool start) { learnCommand.store(start?1:2); }
    gill::LearnBalanceProfile savedProfile() const;
    BalanceView balanceView() const;
    static constexpr std::array<float,8> divisionBeats{.25f,1.f/3,.5f,.75f,1,1.5f,2,4};
    const GillKind kind;
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
    std::atomic<float> inputPeak{0},outputPeak{0},learnProgress{0},tempoBpm{120},delayMs{500};
    std::atomic<int> learnState{0};
    std::atomic<double> uiRate{48000};
    std::atomic<bool> rateSupported{true},hostTempoAvailable{false},delayLimited{false};
private:
    gill::ModeTransition qualityTransition;
    void process(juce::AudioBuffer<float>&,bool);
    void updateParameters(bool queryHost=false);
    void exchangeProfile();
    gill::AirDSP air;
    gill::SpaceDSP space;
    gill::EchoDSP echo;
    gill::BalanceDSP balance;
    std::array<std::vector<float>,2> dryRing;
    size_t dryPosition=0;
    int latency=0,bypassIndex=0;
    std::array<std::atomic<float>*,12> parameterValues{};
    float parameter(size_t i) const noexcept;
    juce::SmoothedValue<float> bypassFade;
    std::atomic<int> learnCommand{0},currentProgram{0};
    std::atomic<double> tail{0};
    mutable juce::SpinLock profileLock;
    gill::LearnBalanceProfile profile;
    BalanceView view;
    bool profilePending=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillEffectProcessor)
};
