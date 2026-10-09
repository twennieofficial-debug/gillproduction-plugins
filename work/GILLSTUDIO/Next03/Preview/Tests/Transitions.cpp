#include "MacTestSupport.h"
#include "Processor.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
using preview::Processor;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Head final:juce::AudioPlayHead{
 std::atomic<bool> playing{true},recording{false},known{true};
 juce::Optional<PositionInfo> getPosition()const override{if(!known.load())return {};PositionInfo p;p.setIsPlaying(playing.load());p.setIsRecording(recording.load());return p;}
};
void run(Processor& p,juce::AudioBuffer<float>& b){const juce::ScopedLock guard(p.getCallbackLock());juce::MidiBuffer m;p.processBlock(b,m);}
void zeros(Processor& p,int n=5000){juce::AudioBuffer<float>b(2,n);b.clear();run(p,b);}
int impulse(Processor& p){zeros(p);juce::AudioBuffer<float>b(2,5000);b.clear();b.setSample(0,0,1);b.setSample(1,0,1);run(p,b);int peak=0;for(int i=1;i<b.getNumSamples();++i)if(std::abs(b.getSample(0,i))>std::abs(b.getSample(0,peak)))peak=i;require(std::abs(b.getSample(0,peak)-1)<2e-5,"neutral impulse amplitude");return peak;}
void pump(){
 // A fixed sleep does not prove JUCE's timer thread has dispatched under load.
 // Require observable timer service before making any transition assertions.
 struct Probe final:juce::Timer{int calls=0;void timerCallback()override{++calls;}}probe;
 probe.startTimer(1);const auto start=juce::Time::getMillisecondCounterHiRes();
 do{juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}
 while((probe.calls<3||juce::Time::getMillisecondCounterHiRes()-start<90)&&juce::Time::getMillisecondCounterHiRes()-start<2000);
 require(probe.calls>=3,"test harness could not service JUCE timers");
}
struct Listener final:juce::AudioProcessorListener{
 std::thread::id main=std::this_thread::get_id();int count=0,mismatches=0;bool wrongThread=false;
 void audioProcessorParameterChanged(juce::AudioProcessor*,int,float)override{}
 void audioProcessorChanged(juce::AudioProcessor* p,const ChangeDetails& d)override{
  if(!d.latencyChanged)return;++count;if(std::this_thread::get_id()!=main)wrongThread=true;
  auto& q=static_cast<Processor&>(*p);const int actual=impulse(q);
  if(actual!=q.getLatencySamples()||actual!=(q.effectiveMode()?0:2048))++mismatches;
 }
};
void negativeControl(){Processor p;p.prepareToPlay(48000,127);Listener l;p.addListener(&l);p.setLatencySamples(0);require(l.count==1&&l.mismatches==1,"ordering oracle failed to detect premature latency announcement");p.removeListener(&l);std::cout<<"premature host latency negative control detected\n";}
void headless(){for(double rate:{44100.,48000.,96000.})for(int mode=0;mode<2;++mode){Processor p;p.set("mode",float(mode));p.prepareToPlay(rate,127);Listener l;p.addListener(&l);require(impulse(p)==(mode?0:2048),"initial headless delay");p.set("mode",float(1-mode));pump();require(p.modePending()&&p.effectiveMode()==mode&&l.count==0,"headless mode changed without prepare");require(impulse(p)==(mode?0:2048),"pending request changes headless audio delay");p.prepareToPlay(rate,31);require(!p.modePending()&&p.effectiveMode()==1-mode,"prepare did not commit request");require(impulse(p)==(mode?2048:0),"prepared request actual delay");require(l.mismatches==0&&!l.wrongThread&&l.count==1,"prepare notification order");p.removeListener(&l);}std::cout<<"headless pending/reprepare at three rates both directions passed\n";}
void transport(){for(int mode=0;mode<2;++mode){Processor p;Head head;p.setPlayHead(&head);p.set("mode",float(mode));p.prepareToPlay(48000,127);zeros(p);Listener l;p.addListener(&l);p.set("mode",float(1-mode));pump();require(p.modePending()&&l.count==0,"playing request applied");require(impulse(p)==(mode?0:2048),"playing request changes audio delay");head.playing=false;head.recording=true;zeros(p);pump();require(p.modePending()&&l.count==0,"recording request applied");head.recording=false;zeros(p);pump();require(!p.modePending()&&l.count==1&&!l.wrongThread&&l.mismatches==0,"stopped commit/notification order");require(impulse(p)==(mode?2048:0),"stopped commit delay");
  // Cancelled requests must not trigger a needless reset or host notification.
  head.playing=true;zeros(p);p.set("mode",float(mode));p.set("mode",float(1-mode));pump();require(l.count==1&&!p.modePending(),"cancelled mode request committed");
  head.known=false;zeros(p);p.set("mode",float(mode));pump();require(p.modePending()&&l.count==1,"unknown transport switched");
  head.known=true;head.playing=false;zeros(p);pump();require(!p.modePending()&&l.count==2&&l.mismatches==0,"known stopped transport did not recover");p.removeListener(&l);
 }std::cout<<"play/record/stop/cancel/unknown mode contract passed\n";}
void threaded(){Processor p;Head head;p.setPlayHead(&head);p.prepareToPlay(48000,31);zeros(p);Listener l;p.addListener(&l);std::thread audio([&]{juce::AudioBuffer<float>b(2,31);for(int i=0;i<160;++i){p.set("mode",float(i%2));b.clear();run(p,b);}p.set("mode",1);});pump();audio.join();pump();require(l.count==0&&p.modePending(),"audio parameter automation notified/switched while playing");head.playing=false;zeros(p);pump();require(l.count==1&&l.mismatches==0&&!l.wrongThread,"control-thread commit violated notification contract");p.removeListener(&l);std::cout<<"threaded mode automation defers notifications correctly\n";}
juce::MemoryBlock encode(const juce::ValueTree& tree){juce::MemoryBlock b;juce::MemoryOutputStream out(b,false);tree.writeToStream(out);return b;}
void versions(){Processor p;p.set("denoise",.35f);p.set("n0_freq",4321);juce::MemoryBlock bytes;p.getStateInformation(bytes);auto original=juce::ValueTree::readFromData(bytes.getData(),bytes.getSize());require(int(original.getProperty("stateSchema"))==2,"missing version envelope");auto legacy=original.createCopy();legacy.removeProperty("stateSchema",nullptr);auto old=encode(legacy);Processor recalled;recalled.setStateInformation(old.getData(),int(old.getSize()));require(std::abs(recalled.value("denoise")-.35f)<1e-6&&std::abs(recalled.value("n0_freq")-4321)<.1f,"legacy parameter migration");for(auto value:{juce::var(-1),juce::var(3),juce::var(1.5),juce::var("1"),juce::var(std::numeric_limits<juce::int64>::max())}){auto bad=original.createCopy();bad.setProperty("stateSchema",value,nullptr);for(auto child:bad)if(child.getProperty("id").toString()=="denoise")child.setProperty("value",.99f,nullptr);auto data=encode(bad);p.setStateInformation(data.getData(),int(data.getSize()));require(std::abs(p.value("denoise")-.35f)<1e-6,"invalid/future schema partially mutates state");}std::cout<<"legacy/current/future/malformed state schema passed\n";}
int main(){gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;try{negativeControl();headless();transport();threaded();versions();std::cout<<"SPECTRA_TRANSITION_CONTRACT_PASS\n";}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
