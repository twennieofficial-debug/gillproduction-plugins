#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "HarmonyDSP.h"
#include "ReferenceEngine.h"
#include "RescueDSP.h"
#include "../../GILLCommon/QualityBus.h"
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/SongLearnTransport.h"

enum class ToolsKind { Harmony, Reference, Rescue };
class GillToolsProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    explicit GillToolsProcessor(ToolsKind);
    ~GillToolsProcessor() override;
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
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 6; }
    int getCurrentProgram() override { return program.load(); }
    void setCurrentProgram(int index) override { selectPreset(index,false); }
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    void selectPreset(int,bool gesture=true);
    void setValue(const juce::String&,float,bool gesture=true);
    float value(const juce::String&) const;
    void loadReference(int,const juce::File&);
    void requestRescueLearn(bool finish=false){rescueLearnCommand.store(finish?2:1);}
    void cancelRescueLearn(){rescueResultSuppressed=true;rescueLearnCommand=3;rescueLearnState=0;}
    std::atomic<int> rescueLearnState{0};
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(ToolsKind);
    const ToolsKind kind;
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient quality {*this,apvts};
    std::unique_ptr<gill::tools::HarmonyDSP> harmony;
    std::unique_ptr<gill::tools::ReferenceEngine> reference;
    std::unique_ptr<gill::tools::RescueDSP> rescue;
    std::atomic<double> rateView {48000};
    std::atomic<bool> supported {true};
    std::atomic<float> inputPeak {0},outputPeak {0};
private:
    void process(juce::AudioBuffer<float>&,bool);
    void timerCallback() override;
    float parameter(size_t index) const noexcept;
    gill::tools::HarmonyParameters harmonyParameters(bool hostBypass) const noexcept;
    gill::tools::RescueParameters rescueParameters(bool hostBypass) const noexcept;
    std::vector<std::atomic<float>*> raw;
    std::atomic<int> program {0};
    gill::ModeTransition transition;
    std::atomic<unsigned> learnSeen{0};std::atomic<bool>rescueResultSuppressed{false};
    std::atomic<int> rescueLearnCommand{0};gill::SongLearnTransport rescueLearnTransport;
    int lastSlot=0;
    std::int64_t nextPosition=0;
    bool hadPosition=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillToolsProcessor)
};
