#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "RiseDSP.h"
#include "../../GILLCommon/QualityBus.h"
#include <mutex>

class GillRiseProcessor final:public juce::AudioProcessor,private juce::Thread {
public:
 GillRiseProcessor();~GillRiseProcessor()override;
 void prepareToPlay(double,int)override;void releaseResources()override;
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 const juce::String getName()const override{return "GILLRISE";}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 0;}bool hasEditor()const override{return true;}juce::AudioProcessorEditor*createEditor()override;
 int getNumPrograms()override{return 6;}int getCurrentProgram()override{return program.load();}void setCurrentProgram(int)override;const juce::String getProgramName(int)override;void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
 float value(const char*)const;void setValue(const char*,float);void arm();void finishCapture();void audition(bool);
 std::shared_ptr<const gill::rise::Render> result()const;std::shared_ptr<const gill::rise::Capture> captured()const;
 juce::File exportWav(juce::String&error,const juce::File&folderOverride={});juce::String statusText()const;
 static juce::AudioProcessorValueTreeState::ParameterLayout layout();
 juce::AudioProcessorValueTreeState apvts;gill::QualityClient quality{*this,apvts};gill::rise::CaptureEngine capture;
 std::atomic<bool>rendering{false},previewing{false};std::atomic<double>sampleRateView{48000};std::atomic<unsigned>revision{0};
private:
 gill::rise::Settings settings()const;void run()override;void process(juce::AudioBuffer<float>&,bool);bool publishPreview(const gill::rise::Render&);
 mutable std::mutex resultMutex;std::shared_ptr<const gill::rise::Render>rendered;std::shared_ptr<const gill::rise::Capture>source,pendingRestore;
 // 0 empty, 1 worker writing, 2 ready, 3 audio playing. CAS grants exclusive
 // preview-buffer ownership; no mutex or reference-count destruction on audio.
 std::atomic<int>previewState{0},previewCommand{0};std::array<std::vector<float>,2>preview;int previewCount=0,previewPosition=0,previewStop=0;
 std::atomic<int>program{0};std::atomic<unsigned>generation{0};std::weak_ptr<const gill::rise::Render>exportedRender;juce::File exported;bool supported=true;std::atomic<bool>renderFailed{false};
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillRiseProcessor)
};
