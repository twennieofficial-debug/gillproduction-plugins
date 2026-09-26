#include "PluginProcessor.h"
#include "Signals.h"
#include "../../GILLCommon/SongLearningTests.h"
#include <fstream>
int main(){juce::ScopedJuceInitialiser_GUI gui;using test::check;
 GillSmartDeEsserProcessor p;gill::testing::SongTestHead head;p.setPlayHead(&head);
 constexpr int block=512;constexpr double rate=16000;p.setRateAndBufferSizeDetails(rate,block);p.prepareToPlay(rate,block);
 const auto early=test::vocal(rate,1,6200),late=test::vocal(rate,1,4200);
 juce::AudioBuffer<float> audio(2,block);juce::MidiBuffer midi;
 const auto render=[&](int seconds,const std::vector<float>* source){
  for(int s=0;s<seconds;++s)for(int at=0;at<int(rate);at+=block){const int n=std::min(block,int(rate)-at);audio.setSize(2,n,false,false,true);
   for(int c=0;c<2;++c)for(int i=0;i<n;++i)audio.setSample(c,i,source?(*source)[static_cast<size_t>(at+i)]:0);
   p.processBlock(audio,midi);if(head.playing)head.seconds+=n/rate;
  }
 };
 p.startLearning();render(1,nullptr);check(p.learnState==GillSmartDeEsserProcessor::armed&&p.elapsedSeconds==0,"stopped transport arms without recording silence");
 head.playing=true;render(12,&early);check(p.learnState==GillSmartDeEsserProcessor::learning,"learner passes old eight-second limit");
 head.playing=false;render(1,nullptr);gillsmart::Profile first;check(p.learnedCandidate(first),"transport STOP retains actual candidate");
 check(p.applyLearned(),"stopped-transport candidate can be applied");
 p.startLearning();render(1,nullptr);p.finishLearning();render(1,nullptr);check(p.learnState==GillSmartDeEsserProcessor::applied,"cancelling armed capture retains applied profile");
 head.playing=true;head.seconds=37;p.startLearning();render(12,&early);render(108,nullptr);
 check(p.learnState==GillSmartDeEsserProcessor::learning,"long vocal pause does not end whole-song learning");
 render(180,&late);gillsmart::Profile full;check(p.elapsedSeconds==300&&p.learnedCandidate(full),"full 300-second take ends with valid learned candidate");
 check(full.frequency<first.frequency-500&&full.activeSeconds>180,"late song content contributes to learned sibilance profile");
 check(p.applyLearned()&&p.getLatencySamples()==0,"full-song learned processing retains zero sample latency");
 juce::MemoryBlock state;p.getStateInformation(state);GillSmartDeEsserProcessor restored;restored.setStateInformation(state.getData(),int(state.getSize()));restored.prepareToPlay(rate,block);
 check(restored.value("frequency")==p.value("frequency")&&restored.value("profile")==1,"full-song profile restores with original host parameter ranges");
 p.setPlayHead(nullptr);std::ofstream("smartdeesser-full-song-report.json")<<"{\"passed\":"<<(test::failed?"false":"true")<<",\"checks\":"<<test::passed+test::failed<<",\"failures\":"<<test::failed<<",\"duration_seconds\":300}";
 return test::finish();}
