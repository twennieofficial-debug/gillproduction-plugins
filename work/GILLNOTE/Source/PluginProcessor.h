#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "NoteEngine.h"
#include "../../GILLCommon/QualityBus.h"
class GillNoteProcessor final:public juce::AudioProcessor {
public:
 GillNoteProcessor();~GillNoteProcessor()override=default;
 void prepareToPlay(double,int)override;void releaseResources()override{engine.stop();}
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 const juce::String getName()const override{return "GILLNOTE";}bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}double getTailLengthSeconds()const override{return 0;}
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor*createEditor()override;
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return "ORIGINAL TAKE";}void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
 float value(const char*)const;void setValue(const char*,float);void audition(double);
 static juce::AudioProcessorValueTreeState::ParameterLayout layout();
 juce::AudioProcessorValueTreeState apvts;gill::QualityClient quality{*this,apvts};gill::note::Engine engine;
private:
 std::atomic<float>*bypass=nullptr;void process(juce::AudioBuffer<float>&,bool);
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillNoteProcessor)
};
