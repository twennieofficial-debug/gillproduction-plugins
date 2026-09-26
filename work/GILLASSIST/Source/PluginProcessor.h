#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AssistEngine.h"
#include "../../GILLCommon/QualityBus.h"

class GillAssistProcessor final:public juce::AudioProcessor{
public:
    GillAssistProcessor();~GillAssistProcessor()override=default;
    const juce::String getName()const override{return "GILLASSIST";}
    void prepareToPlay(double,int)override;void releaseResources()override{}
    bool isBusesLayoutSupported(const BusesLayout&)const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
    bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
    double getTailLengthSeconds()const override{return 0;}bool hasEditor()const override{return true;}
    juce::AudioProcessorEditor*createEditor()override;
    int getNumPrograms()override{return 6;}int getCurrentProgram()override{return preset.load();}
    void setCurrentProgram(int)override;const juce::String getProgramName(int)override;void changeProgramName(int,const juce::String&)override{}
    juce::AudioProcessorParameter*getBypassParameter()const override{return state.getParameter("bypass");}
    void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
    float value(const char*)const;void setValue(const char*,float);
    gill::assist::Settings settings()const;void swapAB();
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState state;gill::QualityClient quality{*this,state};
    gill::assist::Engine engine;
    std::atomic<float>inputPeak{0},outputPeak{0};std::atomic<bool>playingView{false};
private:
    void process(juce::AudioBuffer<float>&,bool);
    std::array<std::atomic<float>*,13>raw{};
    std::atomic<int>preset{0};juce::ValueTree ab;bool abSet=false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillAssistProcessor)
};
