#pragma once
#include "MasterInfo.h"
#include "MasterDSP.h"
#include "WeightDSP.h"
#include "DeltaEngine.h"
#include "DeliverMeter.h"
#include "Foundation/FinishDSP.h"
#include "../../GILLCommon/QualityBus.h"
#include "../../GILLCommon/ModeTransition.h"

class GillMasterProcessor final:public juce::AudioProcessor {
public:
    explicit GillMasterProcessor(MasterKind);
    ~GillMasterProcessor() override=default;
    void prepareToPlay(double,int)override;
    void releaseResources()override;
    bool isBusesLayoutSupported(const BusesLayout&)const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    const juce::String getName()const override{return masterInfo(kind).name;}
    bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
    double getTailLengthSeconds()const override{return kind==MasterKind::Ceiling?.6:kind==MasterKind::Weight?.1:0;}
    bool hasEditor()const override{return true;}juce::AudioProcessorEditor*createEditor()override;
    int getNumPrograms()override{return 6;}int getCurrentProgram()override{return program.load();}
    void setCurrentProgram(int i)override{selectPreset(i,false);}const juce::String getProgramName(int)override;
    void changeProgramName(int,const juce::String&)override{}
    juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
    void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
    void selectPreset(int,bool gesture=true);void setValue(const juce::String&,float,bool gesture=true);float value(const juce::String&)const;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(MasterKind);
    juce::String deliveryReport()const;
    const MasterKind kind;const std::vector<MasterParam>specs;
    juce::AudioProcessorValueTreeState apvts;gill::QualityClient quality{*this,apvts};
    std::unique_ptr<gill::master::DeltaEngine>delta;std::unique_ptr<gill::master::DeliverMeter>deliver;
    std::atomic<float>inputPeak{0},outputPeak{0},reduction{0},correlation{0},integrated{-100},momentary{-100},peakDb{-160};
    std::array<std::atomic<float>,3>bandMeter{};
    std::array<std::atomic<float>,256>historyIn{},historyOut{};std::atomic<unsigned>historyPosition{0};
    std::array<std::atomic<float>,256>scopeL{},scopeR{};std::atomic<unsigned>scopePosition{0};
    std::atomic<double>rateView{48000};std::atomic<bool>supported{true};
    std::atomic<bool>resetMeterRequested{false};
private:
    float param(std::size_t i)const noexcept;void updateParameters();void process(juce::AudioBuffer<float>&,bool);
    std::vector<std::atomic<float>*>raw;std::atomic<int>program{0};
    std::unique_ptr<gillnext::FinishDSP>ceiling;std::unique_ptr<gill::master::LowDSP>low;
    std::unique_ptr<gill::master::GlueDSP>glue;std::unique_ptr<gill::master::WidthDSP>width;
    std::unique_ptr<gill::master::PunchDSP>punch;std::unique_ptr<gill::master::WeightDSP>weight;
    std::unique_ptr<gillnext::Loudness>loudness;gill::master::Correlation stereoMeter;
    std::unique_ptr<gillnext::OutputPeakMeter>finalPeak;double finalMaximum=0;
    std::array<gill::master::LowPass,2>auditionLow,speakerLow;
    std::array<std::array<double,1024>,2>dryDelay{};int delayPosition=0,lastMode=1;
    gill::master::Smooth mix,outputGain,bypassBlend,matchBlend,monitor;gill::ModeTransition transition;
    double fs=48000,detectorAlpha=0,inPower=0,outPower=0,inputHold=0,outputHold=0,holdDecay=1,historyInput=0,historyOutput=0;
    int historyClock=0,historyHop=960;unsigned historyWrite=0;
    unsigned scopeClock=0,scopeWrite=0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillMasterProcessor)
};
