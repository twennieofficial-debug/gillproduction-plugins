#include "../../GILLNEXT/Source/FormDSP.h"
#include "../../GILLTOOLS/Source/HarmonyDSP.h"
#include "../../GILLTOOLS/Source/RescueDSP.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <cstring>
#include <memory>
static bool watch=false;static size_t allocations=0;
void* operator new(size_t n){if(watch)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
static int checks=0,failures=0;
void check(bool good,const char* label){++checks;if(!good){++failures;std::printf("FAIL %s\n",label);}}
template<class Engine>void identity(Engine& e,int channels,int block,bool expectSilence=false){
 std::mt19937 random(19131);std::uniform_real_distribution<float>noise(-.8f,.8f);
 std::array<std::array<float,512>,2> signal{},before{};
 bool identical=true;
 for(int pass=0;pass<100;++pass){
  for(int c=0;c<channels;++c)for(int n=0;n<block;++n)signal[c][n]=pass==0?(n==0?(c==0?.4f:-.7f):0):noise(random);
  before=signal;float*ptr[]{signal[0].data(),signal[1].data()};
  watch=true;e.process(ptr,channels,block);watch=false;
  for(int c=0;c<channels;++c)for(int n=0;n<block;++n)identical=identical&&(signal[c][n]==(expectSilence?0.f:before[c][n]));
 }
 check(identical,"LIVE isolated direct path identity (or intentional direct mute), including first impulse and stereo random input");
 check(e.latencySamples()==0,"LIVE reports exactly zero samples");
}
// Wet pitch/formant synthesis necessarily needs past periods. Its documented
// onset delay is not the direct monitoring path's latency. Verify the actual
// direct contribution while the generated path is active, sample by sample.
template<class Engine>std::vector<float> render(Engine& e,const std::vector<float>& source,int block=127){
 auto out=source;for(size_t at=0;at<out.size();at+=block){float* p=out.data()+at;watch=true;e.process(&p,1,int(std::min<size_t>(block,out.size()-at)));watch=false;}return out;
}
double rmsDifference(const std::vector<float>& a,const std::vector<float>& b){double sum=0;for(size_t i=a.size()/2;i<a.size();++i){double d=a[i]-b[i];sum+=d*d;}return std::sqrt(sum/(a.size()-a.size()/2));}
std::vector<float> vowel(double fs){
 std::vector<float> x(size_t(fs*.8));for(size_t n=0;n<x.size();++n){double y=0;for(int h=1;h<=40;++h){const double hz=100.*h;if(hz>=fs*.45)break;const double envelope=std::exp(-std::pow((hz-500)/160,2))+.5*std::exp(-std::pow((hz-1500)/300,2))+.02;y+=envelope*std::sin(6.283185307179586*hz*n/fs)/h;}x[n]=float(.4*y);}x[0]=.4f;return x;
}
double pitch(const std::vector<float>& x,double fs,double expected){
 double best=-1;int lag=0;for(int k=int(fs/(expected*1.1));k<=int(fs/(expected*.9));++k){double aa=0,bb=0,ab=0;for(size_t i=x.size()/2;i+k<x.size();++i){aa+=double(x[i])*x[i];bb+=double(x[i+k])*x[i+k];ab+=double(x[i])*x[i+k];}const double score=ab/std::sqrt(std::max(1e-30,aa*bb));if(score>best){best=score;lag=k;}}return best>.8?fs/lag:0;
}
double centroid(const std::vector<float>& x,double fs){double sum=0,weighted=0;for(int h=2;h<=35;++h){double re=0,im=0;const double hz=100.*h;for(size_t n=x.size()/2;n<x.size();++n){re+=x[n]*std::cos(6.283185307179586*hz*n/fs);im+=x[n]*std::sin(6.283185307179586*hz*n/fs);}double p=re*re+im*im;sum+=p;weighted+=hz*p;}return weighted/std::max(sum,1e-30);}
void activePaths(){
 for(double fs:{8000.,44100.,48000.,96000.,192000.})for(int shift:{-12,12}){
  const auto source=vowel(fs);
  auto f=std::make_unique<gillnext::FormDSP>();f->prepare(fs,127,1);f->setLiveMode(true);
  gillnext::FormParameters fp;fp.pitchSemitones=float(shift);fp.preserveTransients=false;fp.mix=100;f->setParameters(fp);f->reset();auto wet=render(*f,source);
  fp.mix=50;f->setParameters(fp);f->reset();auto mixed=render(*f,source);double error=0;
  for(size_t n=0;n<source.size();++n)error=std::max(error,std::abs(double(mixed[n])-.5*wet[n]-.5*source[n]));
  check(error<1e-7,"FORM active wet mix retains exact same-sample dry contribution, including first impulse");
  check(wet[0]==0&&std::abs(mixed[0]-.5f*source[0])<1e-7,"FORM wet onset is separate from immediate dry impulse");
  check(rmsDifference(wet,source)>.005,"FORM LIVE generates audible non-identity wet output");
  const double measured=pitch(wet,fs,100*std::exp2(shift/12.));
  std::printf("METRIC FORM fs=%.0f shift=%d pitch=%.4f directError=%.9g\n",fs,shift,measured,error);
  check(measured>0&&std::abs(1200*std::log2(measured/(100*std::exp2(shift/12.))))<25,"FORM LIVE generated pitch follows requested octave");
  auto h=std::make_unique<gill::tools::HarmonyDSP>();gill::tools::HarmonyParameters hp;hp.outputDb=0;hp.direct=false;
  for(auto& v:hp.voices){v.enabled=true;v.intervalMode=1;v.interval=shift;v.levelDb=-12;}
  h->setParameters(hp);h->setLiveMode(true);h->prepare(fs,127,1);auto voices=render(*h,source);
  hp.direct=true;h->setParameters(hp);h->reset();auto direct=render(*h,source);error=0;
  for(size_t n=0;n<source.size();++n)error=std::max(error,std::abs(double(direct[n])-voices[n]-source[n]));
  check(error<1e-7,"HARMONY active voices retain exact same-sample direct contribution");
  check(voices[0]==0&&direct[0]==source[0],"HARMONY generated onset does not delay first direct impulse");
  std::vector<float> silence(source.size(),0);check(rmsDifference(voices,silence)>.005,"HARMONY LIVE enabled voices produce audible output with direct muted");
  check(h->causalReadViolations()==0,"HARMONY generated voices never read future samples");
 }
 auto f=std::make_unique<gillnext::FormDSP>();f->prepare(48000,127,1);f->setLiveMode(true);const auto source=vowel(48000);
 gillnext::FormParameters p;p.preserveTransients=false;p.formantSemitones=-12;f->setParameters(p);f->reset();auto low=render(*f,source);p.formantSemitones=12;f->setParameters(p);f->reset();auto high=render(*f,source);
 const double lowHz=centroid(low,48000),highHz=centroid(high,48000);std::printf("METRIC FORM formant centroid low=%.3f high=%.3f\n",lowHz,highHz);
 check(highHz>lowHz*1.3,"FORM LIVE independent formant control moves measured vowel envelope");
 check(std::abs(pitch(low,48000,100)-100)<2&&std::abs(pitch(high,48000,100)-100)<2,"FORM LIVE formant changes preserve excitation pitch");
}
int main(){
 for(double fs:{8000.,44100.,48000.,96000.,192000.})for(int channels:{1,2})for(int block:{1,63,127,512}){
  gillnext::FormDSP f;f.prepare(fs,block,channels);f.setLiveMode(true);
  for(float shift:{-12.f,0.f,12.f}){gillnext::FormParameters p;p.pitchSemitones=shift;p.formantSemitones=-shift;p.mix=0;f.setParameters(p);f.reset();identity(f,channels,block);}
  f.setLiveMode(false);check(f.latencySamples()>0,"FORM PRO still provides real buffered processing");f.setLiveMode(true);identity(f,channels,block);
  gill::tools::HarmonyDSP h;gill::tools::HarmonyParameters hp;hp.outputDb=0;hp.direct=true;h.setParameters(hp);h.setLiveMode(true);h.prepare(fs,block,channels);
  for(int shift:{-12,0,12}){for(auto&v:hp.voices){v.enabled=false;v.intervalMode=1;v.interval=shift;v.levelDb=0;}h.setParameters(hp);h.reset();identity(h,channels,block);}
  h.setLiveMode(false);check(h.latencySamples()>0,"HARMONY PRO still has real processing context");h.setLiveMode(true);identity(h,channels,block);
  hp.direct=false;h.setParameters(hp);h.reset();identity(h,channels,block,true);
  gill::tools::RescueDSP r;gill::tools::RescueParameters rp;rp.outputDb=0;rp.clipDb=-12;rp.negativeClipDb=-12;r.setParameters(rp);r.setLiveMode(true);r.prepare(fs,block,channels);
  for(float amount:{0.f,50.f,100.f}){rp.repair=amount;r.setParameters(rp);r.reset();identity(r,channels,block);check(r.repairs()==0,"RESCUE LIVE does not falsely claim repaired samples");}
  r.setLiveMode(false);check(r.latencySamples()>0,"RESCUE PRO still uses right boundary for repair");r.setLiveMode(true);identity(r,channels,block);
 }
 activePaths();
 check(allocations==0,"LIVE process performs zero heap allocations");
 std::printf("STRICT LIVE: %d checks, %d failures, %zu realtime allocations\n",checks,failures,allocations);return failures?1:0;
}
