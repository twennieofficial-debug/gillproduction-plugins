#include "PluginProcessor.h"
#include "../../GILLCommon/SongLearningTests.h"
#include <iostream>
#include <cstring>
namespace {
int failures=0, checks=0;
void check(bool value,const char* label){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;}
bool same(const gill::LearnProfile&a,const gill::LearnProfile&b){return a.valid==b.valid&&a.version==b.version&&a.rmsDb==b.rmsDb&&a.peakDb==b.peakDb&&a.crestDb==b.crestDb&&a.thresholdDb==b.thresholdDb&&a.dynamicRangeDb==b.dynamicRangeDb&&a.motionDb==b.motionDb&&a.attackMs==b.attackMs&&a.releaseMs==b.releaseMs;}
struct Run {
 GillVocalProcessor p{GillKind::Flow};gill::testing::SongTestHead head;juce::MidiBuffer midi;
 Run(){head.rate=16000;p.setRateAndBufferSizeDetails(16000,128);p.prepareToPlay(16000,128);}
 ~Run(){p.setPlayHead(nullptr);}
 void render(double seconds,float level=.1f){for(int at=0;at<int(seconds*16000);at+=128){juce::AudioBuffer<float>b(2,128);for(int c=0;c<2;++c)for(int i=0;i<128;++i)b.setSample(c,i,level*std::sin(float(at+i)*.08f));p.processBlock(b,midi);if(head.playing)head.seconds+=128./16000;}}
};
}
int main(){juce::ScopedJuceInitialiser_GUI gui;
 for(bool prior:{false,true})for(bool transport:{false,true})for(double duration:{.1,3.}){
  Run r;if(transport){r.p.setPlayHead(&r.head);r.head.playing=true;}
  if(prior){r.p.requestLearning(true);r.render(3.);r.p.requestLearning(false);r.render(.008);}
  const auto old=r.p.savedProfile();check(old.valid==prior,"profile fixture is independently established");
  r.p.requestLearning(true);r.render(duration,.4f);check(r.p.learnState.load()==1,"cancellation fixture is actively learning");
  r.p.requestCancelLearning();r.render(.008);check(same(old,r.p.savedProfile())&&r.p.learnState.load()==(prior?2:0),"explicit CANCEL discards short or sufficient capture and preserves entire profile");
  r.render(.1);check(same(old,r.p.savedProfile())&&r.p.learnState.load()==(prior?2:0),"cancelled transport cannot restart capture on following blocks");
  r.p.requestLearning(true);r.render(.1);r.p.requestLearning(false);r.render(.008);check(r.p.learnState.load()==3&&same(old,r.p.savedProfile()),"short explicit FINISH stays insufficient and preserves prior profile");
 }
 Run pending;pending.p.requestLearning(true);pending.p.requestCancelLearning();pending.render(.008);check(pending.p.learnState.load()==0,"CANCEL before first audio callback supersedes pending LEARN");
 Run armed;armed.p.setPlayHead(&armed.head);armed.p.requestLearning(true);armed.render(.008);check(armed.p.learnState.load()==4,"stopped transport arms capture");armed.head.playing=true;armed.p.requestCancelLearning();armed.render(.008);check(armed.p.learnState.load()==0,"CANCEL wins when armed transport starts in same block");
 std::cout<<"FLOW_CANCEL_RESULT "<<checks<<" checks "<<failures<<" failures"<<std::endl;return failures?1:0;
}
