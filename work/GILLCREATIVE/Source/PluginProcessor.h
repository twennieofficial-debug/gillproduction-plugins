#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "CreativeDSP.h"
#include "CreativeRender.h"
#include "../../GILLCommon/QualityBus.h"
#include <mutex>
using CreativeKind=gill::creative::Kind;
class GillCreativeProcessor final:public juce::AudioProcessor{
public:
    explicit GillCreativeProcessor(CreativeKind);
    void prepareToPlay(double,int)override;
    void releaseResources()override;
    bool isBusesLayoutSupported(const BusesLayout&)const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    juce::AudioProcessorEditor*createEditor()override;
    bool hasEditor()const override{return true;}
    const juce::String getName()const override;
    bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
    double getTailLengthSeconds()const override{return kind==CreativeKind::Reply?6.0:12.0;}
    int getNumPrograms()override{return 6;}int getCurrentProgram()override{return currentProgram.load();}
    void setCurrentProgram(int index)override{selectPreset(index,false);}const juce::String getProgramName(int)override;void changeProgramName(int,const juce::String&)override{}
    juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
    void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
    void selectPreset(int,bool gesture=true);bool presetMatches()const;
    float value(const juce::String&)const;void setValue(const juce::String&,float,bool gesture=true);
    void startLearn(){engine.request(1);}void stopLearn(){engine.request(2);}void applyLearn(){engine.request(3);}void undoLearn(){engine.request(4);}
    void audition(bool enable){engine.request(enable?5:6);}void editPlan(const gill::creative::Plan&,bool commit=true);
    bool renderEffects(bool effectsOnly=true);
    bool canRender()const {const auto p=plan();return p.valid()&&archive.ready(p.sourceRevision)&&!renderer.busy();}
    gill::creative::Plan plan()const{return engine.snapshot();}
    static juce::AudioProcessorValueTreeState::ParameterLayout layout(CreativeKind);
    const CreativeKind kind;juce::AudioProcessorValueTreeState apvts;gill::QualityClient quality{*this,apvts};gill::creative::Engine engine;
    CreativeCaptureArchive archive;CreativeRender renderer;
    std::atomic<bool>rateSupported{true};std::atomic<double>sampleRateView{48000};std::atomic<float>tempo{120};
private:
    void process(juce::AudioBuffer<float>&,bool);
    std::array<std::atomic<float>*,10>raw{};std::atomic<int>currentProgram{0};
    mutable std::mutex modelProducer;bool prepared=false;juce::MemoryBlock pendingState;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillCreativeProcessor)
};
