#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
static int checks=0,failures=0;static std::atomic<int>allocations{0};static thread_local bool watch=false;
void*operator new(size_t n){if(watch)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](size_t n){return::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void check(bool good,const juce::String&name,double v=0){++checks;if(!good)++failures;std::printf("%s %s %.9g\n",good?"PASS":"FAIL",name.toRawUTF8(),v);}
float signal(double t){return float(.16*std::sin(2*gill::motion::pi*193*t)+.04*std::sin(2*gill::motion::pi*386*t)+.02*std::sin(2*gill::motion::pi*1930*t));}
struct Head:juce::AudioPlayHead{double rate=48000,bpm=123;std::int64_t position=0;bool playing=true;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setTimeInSamples(position);p.setTimeInSeconds(position/rate);p.setBpm(bpm);p.setPpqPosition(position/rate*bpm/60);p.setIsPlaying(playing);return p;}};
void gather(juce::Component&c,std::vector<juce::Component*>&out){for(auto*ch:c.getChildren()){out.push_back(ch);gather(*ch,out);}}
bool waitReady(GillMotionProcessor&p){for(int i=0;i<3000;++i){if(p.result()&&!p.rendering)return true;juce::Thread::sleep(10);}return false;}
void dspTests(){using namespace gill::motion;Clip c;c.rate=16000;c.bpm=123;c.anchored=true;c.onset=48000;c.left.resize(12000);c.right.resize(12000);for(int i=0;i<12000;++i)c.left[i]=c.right[i]=signal(i/c.rate)*fade(i,12000,80);
 for(int k=0;k<7;++k){Settings s;s.bpm=123;s.voices=6;s.dry=0;Render r;check(render(Kind(k),c,s,r),juce::String(name(Kind(k)))+" DSP render");check(r.left.size()==r.right.size()&&r.peak>.001&&r.peak<.9,juce::String(name(Kind(k)))+" finite audible render with headroom",r.peak);bool finite=true;for(auto v:r.left)finite=finite&&std::isfinite(v);check(finite,"All rendered samples finite");check(r.left.front()==0&&r.left.back()==0,"Rendered edges fade to zero");if(k==5){check(r.left.size()==size_t(std::round(c.rate*60/123*4)),"Stutter sample-exact beat length at noninteger tempo");double first=0,last=0;for(size_t i=0;i<r.left.size()/4;++i){first+=r.left[i]*r.left[i]+r.right[i]*r.right[i];const auto j=r.left.size()-1-i;last+=r.left[j]*r.left[j]+r.right[j]*r.right[j];}check(last>first*2,"Stutter builds from quiet to loud",last/std::max(first,1.e-9));double difference=0;for(size_t i=0;i<r.left.size();++i)difference+=std::abs(r.left[i]-r.right[i]);check(difference>10,"Stutter pans between left and right");check(r.endsAtOnset,"Stutter placement ends at vocal onset");}
  if(k==6){double side=0,mid=0;for(size_t i=0;i<r.left.size();++i){side+=std::pow(r.left[i]-r.right[i],2);mid+=std::pow(r.left[i]+r.right[i],2);}check(side>mid*.005,"Crowd has independent stereo voices",side/std::max(mid,1.e-9));s.width=0;Render mono;check(render(Kind(k),c,s,mono),"Mono crowd renders");check(mono.left==mono.right,"Width zero collapses crowd to mono");}
 }
 Clip silence=c;std::fill(silence.left.begin(),silence.left.end(),0);silence.right=silence.left;Render empty;Settings s;check(!render(Kind::Stutter,silence,s,empty),"Silent capture not exported as fake effect");check(!render(Kind::Crowd,c,s,empty,[]{return true;}),"Crowd rendering is cancellable");
}
void nativeBypassIdentity(GillMotionProcessor& p,Head& head,double rate){
 juce::AudioBuffer<float> b(2,127);juce::MidiBuffer midi;
 auto process=[&]{watch=true;p.processBlock(b,midi);watch=false;head.position+=127;};
 auto settle=[&]{for(int at=0;at<int(std::ceil(rate*.25));at+=127){b.clear();process();}};
 auto* bypass=p.getBypassParameter();
 check(bypass&&bypass==p.apvts.getParameter("bypass"),p.getName()+" regression uses actual native BYPASS parameter");
 if(!bypass)return;
 for(int mode:{0,1}){
  p.setValue("gillQuality",float(mode));bypass->setValueNotifyingHost(1);settle();
  double error=0;bool finite=true;
  for(int position:{0,37,126}){
   b.clear();b.setSample(0,position,.75f);b.setSample(1,position,-.625f);process();
   for(int c=0;c<2;++c)for(int n=0;n<127;++n){const float expected=n==position?(c==0?.75f:-.625f):0.f,actual=b.getSample(c,n);finite=finite&&std::isfinite(actual);error=std::max(error,std::abs(double(actual)-expected));}
  }
  std::uint32_t random=0x137abc29u;
  for(int block=0;block<32;++block){float expected[2][127]{};
   for(int c=0;c<2;++c)for(int n=0;n<127;++n){random^=random<<13;random^=random>>17;random^=random<<5;expected[c][n]=float((double(random)/4294967295.-.5)*.40);b.setSample(c,n,expected[c][n]);}
   process();for(int c=0;c<2;++c)for(int n=0;n<127;++n){const float actual=b.getSample(c,n);finite=finite&&std::isfinite(actual);error=std::max(error,std::abs(double(actual)-expected[c][n]));}
  }
  check(finite&&error==0&&p.value("bypass")==1&&p.getLatencySamples()==0,
        p.getName()+" native BYPASS exact same-sample impulses/random "+juce::String(int(rate))+" Hz mode"+juce::String(mode),error);
  // Exercise the processor's actual transport-discontinuity reset, including
  // the first sample after a seek while its native bypass remains enabled.
  head.position-=253;b.clear();b.setSample(0,0,.75f);b.setSample(1,0,-.625f);process();
  double seekError=0;bool seekFinite=true;for(int c=0;c<2;++c)for(int n=0;n<127;++n){const float expected=n==0?(c==0?.75f:-.625f):0.f,actual=b.getSample(c,n);seekFinite=seekFinite&&std::isfinite(actual);seekError=std::max(seekError,std::abs(double(actual)-expected));}
  check(seekFinite&&seekError==0,p.getName()+" active native BYPASS survives transport seek at "+juce::String(int(rate))+" Hz mode"+juce::String(mode),seekError);
  for(bool playing:{false,true}){head.playing=playing;b.clear();b.setSample(0,0,.75f);b.setSample(1,0,-.625f);process();
   bool exact=true;for(int c=0;c<2;++c)for(int n=0;n<127;++n)exact=exact&&b.getSample(c,n)==(n==0?(c==0?.75f:-.625f):0.f);
   check(exact,p.getName()+" active native BYPASS survives transport "+(playing?"restart ":"stop ")+juce::String(int(rate))+" Hz mode"+juce::String(mode));
  }
  bypass->setValueNotifyingHost(0);settle();
 }
}
void product(MotionKind kind){GillMotionProcessor p(kind);const auto name=p.getName();check(p.getNumPrograms()==6,name+" six presets");std::set<juce::String>names;std::vector<std::vector<float>>values;
 for(int i=0;i<6;++i){p.setCurrentProgram(i);names.insert(p.getProgramName(i));std::vector<float>state;for(auto*param:p.getParameters())state.push_back(param->getValue());check(std::find(values.begin(),values.end(),state)==values.end(),name+" distinct preset");values.push_back(state);juce::MemoryBlock saved;p.getStateInformation(saved);p.setCurrentProgram((i+1)%6);p.setStateInformation(saved.getData(),int(saved.getSize()));check(p.getCurrentProgram()==i,name+" program recall");for(size_t j=0;j<state.size();++j)check(std::abs(p.getParameters()[int(j)]->getValue()-state[j])<1.e-5f,name+" parameter roundtrip");}check(names.size()==6,name+" named presets");
 Head head;p.setPlayHead(&head);juce::MidiBuffer midi;juce::AudioBuffer<float>b(2,127);p.setCurrentProgram(0);
 for(double rate:{44100.,48000.,96000.,192000.}){head.rate=rate;head.position=0;p.setPlayConfigDetails(2,2,rate,127);p.prepareToPlay(rate,127);check(p.getLatencySamples()==0,name+" zero direct-path latency");for(int mode:{0,1}){p.setValue("gillQuality",float(mode));bool finite=true,unchanged=true;for(int block=0;block<250;++block){for(int i=0;i<127;++i){float x=signal((head.position+i)/rate);b.setSample(0,i,x);b.setSample(1,i,x);}watch=true;p.processBlock(b,midi);watch=false;for(int i=0;i<127;++i){finite=finite&&std::isfinite(b.getSample(0,i))&&std::abs(b.getSample(0,i))<4;if(gill::motion::designer(kind))unchanged=unchanged&&b.getSample(0,i)==signal((head.position+i)/rate);}head.position+=127;}check(finite,name+" LIVE/PRO finite audio");if(gill::motion::designer(kind))check(unchanged,name+" designer leaves source bit exact");}
  b.clear();b.setSample(0,0,.37f);watch=true;p.processBlockBypassed(b,midi);watch=false;check(b.getSample(0,0)==.37f,name+" host bypass bit exact");head.position+=127;
  nativeBypassIdentity(p,head,rate);}
 p.setCurrentProgram(0);p.setValue("voices",6);head.rate=48000;head.position=480000;p.prepareToPlay(48000,127);p.arm();for(int block=0;block<700;++block){for(int i=0;i<127;++i){double t=(block*127+i)/48000.;float x=((t>.1&&t<.28)||(t>.36&&t<.51)||(t>.60&&t<.78))?signal(t):0;b.setSample(0,i,x);b.setSample(1,i,x);}watch=true;p.processBlock(b,midi);watch=false;head.position+=127;}p.finishCapture();b.clear();p.processBlock(b,midi);check(waitReady(p),name+" captured phrase renders on worker");
 if(auto r=p.result()){check(r->anchored&&r->onset>=480000&&r->onset<490000,name+" actual source timeline retained");juce::String error;auto file=p.exportWav(error,juce::File::getCurrentWorkingDirectory().getChildFile("Exports"));juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatReader>reader(wav.createReaderFor(file.createInputStream().release(),true));check(reader&&reader->numChannels==2&&reader->sampleRate==48000&&reader->bitsPerSample==24&&reader->lengthInSamples==r->left.size(),name+" real stereo WAV exact metadata");check(file==p.exportWav(error),name+" repeated drag reuses unchanged WAV");juce::MemoryBlock state;p.getStateInformation(state);p.arm();p.setStateInformation(state.getData(),int(state.getSize()));check(waitReady(p)&&bool(p.captured()),name+" source and settings survive project recall");}
 p.setNonRealtime(true);p.audition(true);b.clear();p.processBlock(b,midi);check(!p.previewing,name+" offline host export excludes audition");p.setNonRealtime(false);
 std::unique_ptr<juce::AudioProcessorEditor>editor(p.createEditor());std::vector<juce::Component*>components;gather(*editor,components);bool focus=!editor->getWantsKeyboardFocus(),fits=true;int knobs=0;for(auto*c:components){if(dynamic_cast<juce::TextEditor*>(c)==nullptr)focus=focus&&!c->getWantsKeyboardFocus();if(c->isVisible())fits=fits&&editor->getLocalBounds().contains(editor->getLocalArea(c,c->getLocalBounds()));if(auto*s=dynamic_cast<juce::Slider*>(c))if(s->getComponentID().isNotEmpty()){const auto id=s->getComponentID();auto*param=p.apvts.getParameter(id);const float target=param->convertFrom0to1(.37f);s->setValue(target,juce::sendNotificationSync);check(std::abs(p.value(id.toRawUTF8())-float(s->getValue()))<.011f,name+" knob wired "+id);++knobs;}}
 check(focus,name+" ordinary controls do not capture DAW keyboard");check(fits,name+" all visible controls fit compact rectangle");check(knobs>=4,name+" meaningful visible sound controls");juce::TextEditor edit;editor->addAndMakeVisible(edit);edit.setWantsKeyboardFocus(true);check(edit.getWantsKeyboardFocus(),name+" real text entry still accepts keyboard");editor->removeChildComponent(&edit);
 p.setCurrentProgram(0);check(waitReady(p),name+" final preset render ready");juce::Timer::callPendingTimersSynchronously();for(float scale:{1.f,1.5f}){auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);auto file=juce::File::getCurrentWorkingDirectory().getChildFile(name+"-UI-"+juce::String(image.getWidth())+".png");auto stream=file.createOutputStream();if(stream){stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;check(png.writeImageToStream(image,*stream),name+" actual native editor image");}else check(false,name+" screenshot write");}editor.reset();p.setPlayHead(nullptr);
}
int main(){
#if JUCE_MAC
gillInitialiseMacTestApplication();
#endif
std::setvbuf(stdout,nullptr,_IONBF,0);juce::ScopedJuceInitialiser_GUI init;dspTests();for(int k=0;k<7;++k)product(MotionKind(k));check(allocations==0,"No heap allocation inside real audio callback",allocations);std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}
