#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "MotionDSP.h"
#include "../../GILLCommon/QualityBus.h"
#include <mutex>
using MotionKind=gill::motion::Kind;
class GillMotionProcessor final:public juce::AudioProcessor,private juce::Thread {
public:
 explicit GillMotionProcessor(MotionKind);~GillMotionProcessor()override;
 void prepareToPlay(double,int)override;void releaseResources()override;
 bool isBusesLayoutSupported(const BusesLayout&)const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 const juce::String getName()const override{return gill::motion::name(kind);}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}bool hasEditor()const override{return true;}
 double getTailLengthSeconds()const override{return kind==MotionKind::Trail?90:0;}juce::AudioProcessorEditor*createEditor()override;
 int getNumPrograms()override{return 6;}int getCurrentProgram()override{return program.load();}void setCurrentProgram(int)override;const juce::String getProgramName(int)override;void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
 juce::AudioProcessorParameter*getBypassParameter()const override{return apvts.getParameter("bypass");}
 float value(const char*)const;void setValue(const char*,float);bool presetMatches()const;void arm();void finishCapture(){capture.finish();}void audition(bool x){previewCommand=x?1:2;}
 std::shared_ptr<const gill::motion::Render>result()const;std::shared_ptr<const gill::motion::Clip>captured()const;
 juce::File exportWav(juce::String&,const juce::File&folder={});juce::String statusText()const;
 gill::motion::Settings settings()const;static juce::AudioProcessorValueTreeState::ParameterLayout layout(MotionKind);
 const MotionKind kind;juce::AudioProcessorValueTreeState apvts;gill::QualityClient quality{*this,apvts};gill::motion::Capture capture;
 std::atomic<bool>rendering{false},previewing{false};std::atomic<double>tempo{120},sampleRateView{48000};std::atomic<bool>hostTempo{false};
private:
 void run()override;void process(juce::AudioBuffer<float>&,bool);bool publishPreview(const gill::motion::Render&);
 std::array<std::atomic<float>*,17>raw{};std::array<std::atomic<float>,17>presetValues{};gill::motion::Engine engine;mutable std::mutex mutex;std::shared_ptr<const gill::motion::Clip>source;std::shared_ptr<const gill::motion::Render>rendered;
 std::atomic<unsigned>generation{0};std::atomic<int>program{0},previewCommand{0},previewState{0};std::atomic<bool>failed{false};
 std::array<std::vector<float>,2>preview;double previewRate=48000,previewPosition=0;int previewCount=0,previewStop=0;bool supported=true,wasPlaying=false,hadPosition=false;std::int64_t nextPosition=0;
 std::weak_ptr<const gill::motion::Render>exportedRender;juce::File exported;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillMotionProcessor)
};
