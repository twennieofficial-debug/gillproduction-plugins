#pragma once
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/QualityBus.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "Presets.h"
#include <array>
#include <atomic>
class GillNextProcessor final:public juce::AudioProcessor {
public:
 explicit GillNextProcessor(NextKind);~GillNextProcessor()override;
 void prepareToPlay(double,int)override;void releaseResources()override;
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 juce::AudioProcessorEditor*createEditor()override;bool hasEditor()const override{return true;}
 const juce::String getName()const override;
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override;
 int getNumPrograms()override{return static_cast<int>(programs.size());}int getCurrentProgram()override{return currentProgram.load();}
 void setCurrentProgram(int i)override{selectPreset(i,false);}const juce::String getProgramName(int i)override{return programs[static_cast<size_t>(juce::jlimit(0,getNumPrograms()-1,i))].name;}
 void changeProgramName(int,const juce::String&)override{}void selectPreset(int,bool gesture=true);bool presetMatches()const;
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
 float value(const juce::String&)const;void setValue(const juce::String&,float,bool gesture=true);
 static juce::AudioProcessorValueTreeState::ParameterLayout layout(NextKind);
 std::array<float,128> graph()const;
 std::array<float,256> waveform(int lane)const; // lane0 GUIDE, lane1 DOUBLE/aligned
 void capture(int lane); // toggles capture, 0 GUIDE external sidechain, 1 DOUBLE main
 void alignTakes();void undoAlignment();void clearCaptures();
 void startLearn();void stopLearn();void applyLearn();void revertLearn();void resetMeters();
 void resetForm();juce::String statusText()const;
 const NextKind kind;const std::vector<gillnext::ParamSpec> definitions;const std::vector<gillnext::Preset> programs;
 juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
 std::atomic<float>inputPeak{0},outputPeak{0},inputRms{0},outputRms{0},reduction{0};
 std::atomic<double>uiRate{48000};std::atomic<bool>rateSupported{true};
 std::atomic<float>appliedGain{0},voiceActivity{0};std::atomic<bool>sidechainActive{false};
 std::array<std::atomic<float>,3>cleanReduction{};
 std::atomic<int>captureState{0}; // 0 stopped, 1 GUIDE, 2 DOUBLE
 std::atomic<float>guideSeconds{0},doubleSeconds{0},alignConfidence{0};
 std::atomic<int>alignState{0}; // 0 waiting,1 captured,2 analysing,3 ready,4 error
 std::atomic<int>learnState{0}; // 0 idle,1 learning,2 suggestion ready,3 applied,4 armed,5 insufficient audio
 std::atomic<float>learnProgress{0},learnDrive{0},learnLow{0},learnMid{0},learnHigh{0},learnComp{0};
 std::atomic<float>momentaryLufs{-100},integratedLufs{-100},truePeakDb{-100},compressorReduction{0},limiterReduction{0};
private:
    gill::ModeTransition qualityTransition;
 static BusesProperties buses(NextKind);
 void process(juce::AudioBuffer<float>&,bool);
 struct Impl;std::unique_ptr<Impl> impl;
 std::atomic<int>currentProgram{0};
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillNextProcessor)
};
