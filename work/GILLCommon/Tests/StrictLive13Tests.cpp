#include "../../GILLNEXT/Source/FormDSP.h"
#include "../../GILLTOOLS/Source/HarmonyDSP.h"
#include "../../GILLTOOLS/Source/RescueDSP.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <cstring>
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
 check(identical,"LIVE current-sample identity (or intentional direct mute), including first impulse and stereo random input");
 check(e.latencySamples()==0,"LIVE reports exactly zero samples");
}
int main(){
 for(double fs:{8000.,44100.,48000.,96000.,192000.})for(int channels:{1,2})for(int block:{1,63,127,512}){
  gillnext::FormDSP f;f.prepare(fs,block,channels);f.setLiveMode(true);
  for(float shift:{-12.f,0.f,12.f}){gillnext::FormParameters p;p.pitchSemitones=shift;p.formantSemitones=-shift;f.setParameters(p);identity(f,channels,block);}
  f.setLiveMode(false);check(f.latencySamples()>0,"FORM PRO still provides real buffered processing");f.setLiveMode(true);identity(f,channels,block);
  gill::tools::HarmonyDSP h;gill::tools::HarmonyParameters hp;hp.outputDb=0;hp.direct=true;h.setParameters(hp);h.setLiveMode(true);h.prepare(fs,block,channels);
  for(int shift:{-12,0,12}){for(auto&v:hp.voices){v.enabled=true;v.intervalMode=1;v.interval=shift;v.levelDb=0;}h.setParameters(hp);h.reset();identity(h,channels,block);}
  h.setLiveMode(false);check(h.latencySamples()>0,"HARMONY PRO still has real processing context");h.setLiveMode(true);identity(h,channels,block);
  hp.direct=false;h.setParameters(hp);h.reset();identity(h,channels,block,true);
  gill::tools::RescueDSP r;gill::tools::RescueParameters rp;rp.outputDb=0;rp.clipDb=-12;rp.negativeClipDb=-12;r.setParameters(rp);r.setLiveMode(true);r.prepare(fs,block,channels);
  for(float amount:{0.f,50.f,100.f}){rp.repair=amount;r.setParameters(rp);r.reset();identity(r,channels,block);check(r.repairs()==0,"RESCUE LIVE does not falsely claim repaired samples");}
  r.setLiveMode(false);check(r.latencySamples()>0,"RESCUE PRO still uses right boundary for repair");r.setLiveMode(true);identity(r,channels,block);
 }
 check(allocations==0,"LIVE process performs zero heap allocations");
 std::printf("STRICT LIVE: %d checks, %d failures, %zu realtime allocations\n",checks,failures,allocations);return failures?1:0;
}
