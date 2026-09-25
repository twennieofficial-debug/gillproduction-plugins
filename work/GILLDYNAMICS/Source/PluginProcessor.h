#pragma once
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/QualityBus.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "Presets.h"
#include "VocalDynamicsDSP.h"
#include "BussDSP.h"
#include "QuadDSP.h"
#include "StageDSP.h"
#include <atomic>
#include <array>
class GillDynamicsProcessor final:public juce::AudioProcessor,private juce::AudioProcessorValueTreeState::Listener{
public:
 explicit GillDynamicsProcessor(DynKind);
 ~GillDynamicsProcessor()override;
 void prepareToPlay(double,int)override;void releaseResources()override;
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 juce::AudioProcessorEditor*createEditor()override;bool hasEditor()const override{return true;}
 const juce::String getName()const override;
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return (kind==DynKind::Stage?.25:0)+getLatencySamples()/uiRate.load();}
 int getNumPrograms()override{return static_cast<int>(programs.size());}int getCurrentProgram()override{return currentProgram.load();}
 void setCurrentProgram(int i)override{selectPreset(i,false);}const juce::String getProgramName(int i)override{return programs[static_cast<size_t>(juce::jlimit(0,getNumPrograms()-1,i))].name;}
 void changeProgramName(int,const juce::String&)override{}void selectPreset(int,bool gesture=true);bool presetMatches()const;
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
 float value(const juce::String&)const;void setValue(const juce::String&,float,bool gesture=true);
 void syncGroups();std::array<float,128> graph()const;
 static juce::AudioProcessorValueTreeState::ParameterLayout layout(DynKind);
 const DynKind kind;const std::vector<gilldyn::ParamSpec> definitions;const std::vector<gilldyn::Preset> programs;
 juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
 std::atomic<float>inputPeak{0},outputPeak{0},inputRms{0},outputRms{0},reduction{0};
 std::atomic<double>uiRate{48000};std::atomic<bool>rateSupported{true};
 std::array<std::atomic<float>,4>bandReduction{};
 std::array<std::atomic<float>,3>crossoversDisplay{};
 std::array<std::atomic<float>,2>channelOutputRms{};
 std::array<std::atomic<float>,2>channelOutputPeak{};
private:
    gill::ModeTransition qualityTransition;
 void process(juce::AudioBuffer<float>&,bool);void update();float at(const char*)const;
 void parameterChanged(const juce::String&,float)override;void updateGraph();
 gilldyn::VoxDSP vox;gilldyn::OptaDSP opta;
 gilldyn::BussDSP buss;gilldyn::QuadDSP quad;gill::StageDSP stage;
 std::array<std::atomic<float>*,128>values{};std::array<std::vector<float>,2>dry;size_t dryPosition=0;int latency=0;
 juce::SmoothedValue<float>bypassFade;double inPower=0,outPower=0;
 std::array<double,2>channelPower{};double meterAlpha=.0001;int graphCounter=0;
 std::array<float,128>display{};mutable juce::SpinLock graphLock;
 std::atomic<int>currentProgram{0};
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillDynamicsProcessor)
};
