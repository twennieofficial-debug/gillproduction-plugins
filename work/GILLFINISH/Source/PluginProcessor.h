#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Presets.h"
#include "CharacterDSP.h"
#include "SpectralDSP.h"
#include <atomic>
#include <array>
class GillFinishProcessor final:public juce::AudioProcessor{
public:
    explicit GillFinishProcessor(FinishKind);
    void prepareToPlay(double,int)override;
    void releaseResources()override;
    bool isBusesLayoutSupported(const BusesLayout&)const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    juce::AudioProcessorEditor* createEditor()override;
    bool hasEditor()const override{return true;}
    const juce::String getName()const override;
    bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
    double getTailLengthSeconds()const override{return kind==FinishKind::Strip?30.:getLatencySamples()/uiRate.load()+.2;}
    int getNumPrograms()override{return static_cast<int>(programs.size());}
    int getCurrentProgram()override{return currentProgram.load();}
    void setCurrentProgram(int i)override{selectPreset(i,false);}
    const juce::String getProgramName(int i)override{return programs[static_cast<size_t>(juce::jlimit(0,getNumPrograms()-1,i))].name;}
    void changeProgramName(int,const juce::String&)override{}
    void selectPreset(int,bool gesture=true);
    bool presetMatches()const;
    void getStateInformation(juce::MemoryBlock&)override;
    void setStateInformation(const void*,int)override;
    juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
    float value(const juce::String&)const;
    void setValue(const juce::String&,float,bool gesture=true);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(FinishKind);
    std::array<float,128> graph()const;
    const FinishKind kind;
    const std::vector<gillfinish::ParamSpec> definitions;
    const std::vector<gillfinish::Preset> programs;
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inputPeak{0},outputPeak{0},reduction{0},cutoff{1500},tempo{120};
    std::atomic<double> uiRate{48000};std::atomic<bool> rateSupported{true},hostTempo{false};
private:
    void process(juce::AudioBuffer<float>&,bool);
    void update(bool queryHost=false);
    float at(const char*)const;
    gillfinish::SilkDSP silk;gillfinish::SparkDSP spark;gillfinish::GoldDSP gold;gillfinish::DiveDSP dive;gillfinish::StripDSP strip;
    std::array<std::atomic<float>*,24> values{};
    std::array<std::vector<float>,2>dry;size_t dryPosition=0;int latency=0;
    juce::SmoothedValue<float> bypassFade,deltaFade;
    mutable juce::SpinLock graphLock;std::array<float,128> display{};
    std::atomic<int>currentProgram{0};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillFinishProcessor)
};
