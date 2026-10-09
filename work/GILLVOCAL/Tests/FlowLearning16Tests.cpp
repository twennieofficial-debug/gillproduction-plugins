#include "PluginProcessor.h"
#include "../../GILLCommon/SongLearningTests.h"
#include <cstdio>
#include <vector>
#include <memory>
#include <fstream>
constexpr double rate=48000,pi=3.14159265358979323846;int checks=0,fails=0;
void check(bool ok,const char*what,double value=0){++checks;fails+=!ok;printf("%s %s %.9g\n",ok?"PASS":"FAIL",what,value);}
struct Run {gill::testing::SongTestHead head;GillVocalProcessor p{GillKind::Flow};juce::MidiBuffer midi;Run(){head.rate=rate;p.setPlayHead(&head);p.setRateAndBufferSizeDetails(rate,127);p.setValue("amount",70);p.setValue("autogain",0);p.prepareToPlay(rate,127);}~Run(){p.setPlayHead(nullptr);}
 std::vector<float> render(double seconds,double amplitude,bool dynamic=false){std::vector<float>out;int total=int(seconds*rate);out.reserve(total);for(int at=0;at<total;at+=127){int n=std::min(127,total-at);juce::AudioBuffer<float>b(2,n);for(int i=0;i<n;++i){double t=head.seconds+i/rate;double a=amplitude;if(dynamic)a*=((int(t*8)%4)==0?.1:(int(t*8)%4)==1?.35:1);float x=float(a*sin(2*pi*220*t));b.setSample(0,i,x);b.setSample(1,i,x*.8f);}p.processBlock(b,midi);for(int i=0;i<n;++i)out.push_back(b.getSample(0,i));if(head.playing)head.seconds+=n/rate;}return out;}
 gill::LearnProfile learn(double amp,bool dynamic=false){p.requestLearning(true);render(.01,0);head.playing=true;render(4.1,amp,dynamic);head.playing=false;render(.01,0);return p.savedProfile();}
};
double power(const std::vector<float>&x){double p=0;for(size_t i=x.size()/2;i<x.size();++i)p+=x[i]*x[i];return p/(x.size()-x.size()/2);}
int main(){juce::ScopedJuceInitialiser_GUI gui;Run quiet,loud,moving;
 auto q=quiet.learn(.025),l=loud.learn(.25),m=moving.learn(.25,true);
 check(q.valid&&l.valid&&m.valid&&quiet.p.learnState.load()==2,"FLOW three actual DAW captures apply on STOP");
 check(l.thresholdDb-q.thresholdDb>18,"FLOW learned threshold responds to20dB input difference",l.thresholdDb-q.thresholdDb);
 check(m.dynamicRangeDb>12&&m.releaseMs>l.releaseMs+20,"FLOW dynamic passage changes learned release",m.releaseMs-l.releaseMs);
 printf("PROFILE quiet rms=%.2f threshold=%.2f attack=%.2f release=%.2f range=%.2f motion=%.2f\n",q.rmsDb,q.thresholdDb,q.attackMs,q.releaseMs,q.dynamicRangeDb,q.motionDb);
 printf("PROFILE loud rms=%.2f threshold=%.2f attack=%.2f release=%.2f range=%.2f motion=%.2f\n",l.rmsDb,l.thresholdDb,l.attackMs,l.releaseMs,l.dynamicRangeDb,l.motionDb);
 printf("PROFILE moving rms=%.2f threshold=%.2f attack=%.2f release=%.2f range=%.2f motion=%.2f\n",m.rmsDb,m.thresholdDb,m.attackMs,m.releaseMs,m.dynamicRangeDb,m.motionDb);
 quiet.head.playing=loud.head.playing=true;quiet.head.seconds=loud.head.seconds=0;auto qa=quiet.render(2,.12),la=loud.render(2,.12);double difference=10*log10(power(la)/power(qa));
 check(difference>5&&quiet.p.reduction.load()>loud.p.reduction.load()+5,"FLOW profiles cause different actual attenuation and GR",difference);
 printf("REDUCTION quiet=%.5f loud=%.5f\n",quiet.p.reduction.load(),loud.p.reduction.load());
 check(quiet.p.value("amount")==70&&loud.p.value("amount")==70,"FLOW learning preserves user AMOUNT while adapting threshold");
 juce::MemoryBlock state;quiet.p.getStateInformation(state);Run recalled;recalled.p.setStateInformation(state.getData(),int(state.getSize()));recalled.p.prepareToPlay(rate,127);quiet.p.prepareToPlay(rate,127);recalled.head.seconds=quiet.head.seconds=0;recalled.head.playing=quiet.head.playing=true;auto a=quiet.render(2,.12),b=recalled.render(2,.12);double err=0;for(size_t i=0;i<a.size();++i)err=std::max(err,std::abs(double(a[i])-b[i]));check(err<1e-7&&recalled.p.savedProfile().thresholdDb==q.thresholdDb,"FLOW saved profile recalls bit-identical actual audio",err);
 quiet.head.playing=false;quiet.p.requestLearning(true);quiet.render(.01,0);check(quiet.p.learnState.load()==4,"FLOW stopped LEARN arms");quiet.p.requestLearning(false);quiet.render(.01,0);check(quiet.p.learnState.load()==2&&quiet.p.savedProfile().thresholdDb==q.thresholdDb,"FLOW cancelling arm preserves old profile");
 quiet.p.requestLearning(true);quiet.head.playing=true;quiet.render(.3,.3);quiet.p.requestLearning(false);quiet.render(.01,.3);check(quiet.p.learnState.load()==3&&quiet.p.savedProfile().thresholdDb==q.thresholdDb,"FLOW short FINISH reports insufficient and retains old profile");
 quiet.head.playing=false;auto newq=quiet.learn(.25);check(newq.thresholdDb-q.thresholdDb>18,"FLOW relearning replaces previous profile");
 Run manual;manual.p.setPlayHead(nullptr);manual.p.requestLearning(true);manual.render(3,.08);manual.p.requestLearning(false);manual.render(.01,0);check(manual.p.savedProfile().valid&&manual.p.learnState.load()==2,"FLOW no-transport manual LEARN FINISH applies");
 Run empty;empty.p.requestLearning(true);empty.head.playing=true;empty.render(.5,0);empty.head.playing=false;empty.render(.01,0);check(empty.p.learnState.load()==3&&!empty.p.savedProfile().valid,"FLOW silent STOP gives actionable insufficient feedback");
 check(quiet.p.getLatencySamples()==0,"FLOW learning remains zero latency");
 std::ofstream json("flow16-results.json");json<<"{\"checks\":"<<checks<<",\"failures\":"<<fails<<",\"quiet_threshold_db\":"<<q.thresholdDb<<",\"loud_threshold_db\":"<<l.thresholdDb<<",\"audio_attenuation_difference_db\":"<<difference<<",\"recall_max_error\":"<<err<<"}";
 printf("RESULT %d checks %d failures\n",checks,fails);return fails?1:0;
}
