#include "MacTestSupport.h"
#include "Processor.h"
#include "QualityBus.h"
#include <iostream>
#include <stdexcept>
using preview::Processor;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Head final:juce::AudioPlayHead {
 bool playing=false,recording=false,known=true;
 juce::Optional<PositionInfo> getPosition()const override{if(!known)return {};PositionInfo p;p.setIsPlaying(playing);p.setIsRecording(recording);return p;}
};
void pump(){const auto start=juce::Time::getMillisecondCounterHiRes();do{juce::Thread::sleep(5);juce::Timer::callPendingTimersSynchronously();}while(juce::Time::getMillisecondCounterHiRes()-start<200);}
void process(Processor& p,juce::AudioBuffer<float>& b){juce::MidiBuffer m;const juce::ScopedLock lock(p.getCallbackLock());p.processBlock(b,m);}
void settle(Processor& p){juce::AudioBuffer<float>b(2,128);for(int i=0;i<50;++i){b.clear();process(p,b);}pump();for(int i=0;i<50;++i){b.clear();process(p,b);}}
juce::MemoryBlock encode(juce::ValueTree tree){juce::MemoryBlock result;juce::MemoryOutputStream out(result,false);tree.writeToStream(out);return result;}
juce::ValueTree saved(Processor& p){juce::MemoryBlock b;p.getStateInformation(b);return juce::ValueTree::readFromData(b.getData(),b.getSize());}
void impulse(Processor& p,int delay){juce::AudioBuffer<float>b(2,5000);b.clear();process(p,b);b.clear();b.setSample(0,37,.75f);b.setSample(1,37,-.625f);process(p,b);double error=0;for(int c=0;c<2;++c)for(int n=0;n<5000;++n)error=std::max(error,std::abs(double(b.getSample(c,n))-(n==37+delay?(c==0?.75:-.625):0.)));require(error<2e-6,"measured normal-process impulse delay/amplitude");}
void quality(double rate){
 gill::quality_detail::Bus controller(true);
 Processor p,other;Head head;p.setPlayHead(&head);other.setPlayHead(&head);
 auto* q=p.state.getParameter("gillQuality");
 require(q&&q->getName(128)=="QUALITY"&&q->getNumSteps()==2,"PREVIEW must expose native two-mode QUALITY");
 require(q->getText(0,32)=="LIVE"&&q->getText(1,32)=="PRO","common LIVE=0 PRO=1 ordering");
 require(p.getBypassParameter()==p.state.getParameter("bypass")&&p.getBypassParameter()!=nullptr,"native APVTS bypass identity");
 p.prepareToPlay(rate,128);other.prepareToPlay(rate,128);settle(p);settle(other);
 require(p.getLatencySamples()==2048,"default PRO latency retained");
 require(controller.broadcast(gill::liveQuality),"controller bus broadcast");pump();settle(p);settle(other);
 require(p.value("gillQuality")==0&&p.value("mode")==1&&p.effectiveMode()==1&&p.getLatencySamples()==0,"GLOBAL LIVE reaches parameter, legacy alias and engine");
 require(other.value("gillQuality")==0&&other.getLatencySamples()==0,"GLOBAL LIVE reaches second instance");
 impulse(p,0); // Processing enabled, neutral engine: no bypass substitute.
 p.set("gillQuality",1);pump();settle(p);
 require(p.getLatencySamples()==2048&&p.value("mode")==0&&other.value("gillQuality")==0,"local PRO is isolated");impulse(p,2048);
 require(controller.broadcast(0),"repeated global LIVE broadcast");pump();settle(p);require(p.getLatencySamples()==0,"repeated GLOBAL LIVE overrides local PRO");
 p.set("denoise",.9f);p.set("strength",.8f);p.set("output",-12);p.set("bypass",1);settle(p);impulse(p,0);
 std::uint32_t random=0x137abc29u;double error=0;
 for(int block=0;block<32;++block){juce::AudioBuffer<float>b(2,128);float dry[2][128];for(int c=0;c<2;++c)for(int n=0;n<128;++n){random^=random<<13;random^=random>>17;random^=random<<5;dry[c][n]=float((double(random)/4294967295.-.5)*.4);b.setSample(c,n,dry[c][n]);}process(p,b);for(int c=0;c<2;++c)for(int n=0;n<128;++n)error=std::max(error,std::abs(double(b.getSample(c,n))-dry[c][n]));}
 require(error<=1e-7,"native LIVE bypass same-sample stereo random audio");
 auto tree=saved(p);require(float(tree.getChildWithProperty("id","bypass").getProperty("value"))==1,"native bypass persisted in actual state");
 // Legacy automation is authoritative too; callbacks do not commit active DSP.
 head.playing=true;settle(p);p.set("mode",0);pump();require(p.value("gillQuality")==1&&p.modePending()&&p.getLatencySamples()==0,"legacy automation mirrors without playing transition");
 head.playing=false;head.recording=true;settle(p);require(p.modePending(),"recording transition deferred");
 head.recording=false;head.known=false;settle(p);require(p.modePending(),"unknown transition deferred");
 head.known=true;settle(p);require(!p.modePending()&&p.getLatencySamples()==2048,"stopped applies requested quality");
 // Recall old state without added parameters into an already-used instance.
 tree.removeChild(tree.getChildWithProperty("id","gillQuality"),nullptr);tree.removeChild(tree.getChildWithProperty("id","bypass"),nullptr);tree.getChildWithProperty("id","mode").setProperty("value",1,nullptr);
 auto legacy=encode(tree);p.setStateInformation(legacy.getData(),int(legacy.getSize()));settle(p);
 require(p.value("mode")==1&&p.value("gillQuality")==0&&p.value("bypass")==0&&p.getLatencySamples()==0,"legacy LIVE state migrates and resets absent bypass");
 p.set("bypass",1);p.setCurrentProgram(4);require(p.value("mode")==1&&p.value("gillQuality")==0&&p.value("bypass")==1,"factory presets preserve session quality/bypass");
 // Common requests obey the same unknown and running transport contract.
 Processor headless;headless.prepareToPlay(rate,128);headless.set("gillQuality",0);pump();
 require(headless.modePending()&&headless.getLatencySamples()==2048,"headless QUALITY request must wait for reprepare");
 headless.prepareToPlay(rate,128);require(!headless.modePending()&&headless.getLatencySamples()==0,"reprepare commits common QUALITY request");
 head.playing=true;settle(p);require(controller.broadcast(1),"GLOBAL PRO while playing");pump();
 require(p.value("gillQuality")==1&&p.modePending()&&p.getLatencySamples()==0,"global QUALITY request must wait while playing");
 head.playing=false;settle(p);require(!p.modePending()&&p.getLatencySamples()==2048,"stopped commits deferred GLOBAL PRO");
 p.set("mode",1);p.set("mode",0);auto immediate=saved(p);require(float(immediate.getChildWithProperty("id","gillQuality").getProperty("value"))==1,"immediate save serialises latest legacy automation");
 std::cout<<"quality rate="<<rate<<" nativeLiveRandomError="<<error<<" passed\n";
}
void parameterRestore(){
 Processor p;juce::MemoryBlock clean;p.getStateInformation(clean);int checked=0;
 for(auto* parameter:p.getParameters())if(parameter->isBoolean())parameter->setValueNotifyingHost(.373652f);
 p.setStateInformation(clean.getData(),int(clean.getSize()));
 for(auto* parameter:p.getParameters())if(parameter->isBoolean()){
  require(parameter->getValue()==parameter->getDefaultValue(),"fractional native boolean must restore to saved normalised value");++checked;
 }
 require(checked>=28,"boolean restoration coverage");
 std::cout<<"fractional boolean native state restoration passed for "<<checked<<" parameters\n";
}
int main(int argc,char** argv){gillInitialiseTestApplication();juce::ScopedJuceInitialiser_GUI gui;try{if(argc>1&&juce::String(argv[1])=="--state-only"){parameterRestore();return 0;}for(double rate:{44100.,48000.,96000.,192000.})quality(rate);parameterRestore();std::cout<<"PREVIEW_QUALITY_INTEGRATION_PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
