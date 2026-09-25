#include "../Source/CleanDSP.h"
#include "../Source/Loudness.h"
#include <cstdio>
#include <random>
#include <limits>
#include <new>
#include <cstdlib>
static bool watch=false;static int allocations=0,checks=0,failures=0;
void*operator new(size_t n){if(watch)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void check(bool ok,const char*name,double x=0){++checks;if(!ok)++failures;std::printf("%s %s %.9g\n",ok?"PASS":"FAIL",name,x);}
using Audio=std::array<std::vector<float>,2>;
Audio source(double fs,double seconds,bool noise=false){Audio a;std::mt19937 rng(19);std::normal_distribution<float>d(0,.012f);for(auto&c:a)c.resize(size_t(fs*seconds));for(size_t i=0;i<a[0].size();++i){const double t=i/fs;a[0][i]=noise?d(rng):float(.15*std::sin(2*gillnext::detail::pi*219.2*t)+.04*std::sin(2*gillnext::detail::pi*657.6*t));a[1][i]=a[0][i]*.7f;}return a;}
void render(gillnext::CleanDSP&d,Audio&a,int block){for(int at=0;at<int(a[0].size());at+=block){float*p[]{a[0].data()+at,a[1].data()+at};watch=true;d.process(p,2,std::min(block,int(a[0].size())-at));watch=false;}}
double energy(const std::vector<float>&a,int from){double sum=0;for(int i=from;i<int(a.size());++i)sum+=double(a[i])*a[i];return sum/(a.size()-from);}
int main(){
 for(double fs:{22050.,44100.,48000.,88200.,96000.,192000.}){
  auto in=source(fs,.6);gillnext::CleanDSP d;d.setParameters({0,0,0,0});d.prepare(fs,127,2);auto a=in;render(d,a,127);const int latency=d.latencySamples();bool exact=true;for(int i=latency;i<int(a[0].size());++i)for(int c=0;c<2;++c)exact&=a[c][i]==in[c][i-latency];check(exact,"CLEAN zero settings preserve exact aligned original");
  gillnext::CleanDSP active;active.prepare(fs,127,2);active.setParameters({60,80,40,0});a=in;render(active,a,127);gillnext::CleanDSP other;other.prepare(fs,1,2);other.setParameters({60,80,40,0});auto b=in;render(other,b,1);check(a==b,"CLEAN sample-exact across host block sizes");
  bool finite=true;for(const auto&c:a)for(auto x:c)finite&=std::isfinite(x);check(finite,"CLEAN full processing is finite");
  gillnext::Loudness l;l.prepare(fs,1);for(int i=0;i<int(fs*2);++i){const float x=float(std::sin(2*gillnext::detail::pi*997*i/fs));watch=true;l.sample({x,0},1);watch=false;}check(std::abs(l.integrated+3.01)<.06,"LUFS mono full-scale 997Hz calibration",l.integrated);
 }
 auto in=source(48000,4,true);auto a=in;gillnext::CleanDSP d;d.setParameters({100,0,0,0});d.prepare(48000,127,2);render(d,a,127);const double suppression=10*std::log10(energy(a[0],96000)/energy(in[0],96000));check(suppression< -6&&suppression> -25,"stationary noise is measurably attenuated at full NOISE",suppression);
 auto voice=source(48000,2);auto cleanVoice=voice;gillnext::CleanDSP gentle;gentle.setParameters({30,0,0,0});gentle.prepare(48000,127,2);render(gentle,cleanVoice,127);const double change=10*std::log10(energy(cleanVoice[0],48000)/energy(voice[0],48000));check(change> -1,"gentle noise reduction preserves steady harmonic vocal energy",change);
 // Removed components add up to the complete reduction, each rendered from the same input.
 std::array<Audio,4>versions;for(int mode=0;mode<4;++mode){gillnext::CleanDSP v;v.setParameters({70,70,70,mode});v.prepare(48000,127,2);versions[mode]=in;render(v,versions[mode],127);}double error=0;for(int i=1024;i<int(in[0].size());++i){double sum=0;for(int mode=0;mode<4;++mode)sum+=versions[mode][0][i];error=std::max(error,std::abs(sum-in[0][i-1024]));}check(error<1e-7,"LISTEN components reconstruct exactly the removed portions",error);
 gillnext::Loudness stereo;stereo.prepare(48000,2);for(int i=0;i<48000*3;++i){float x=float(.1*std::sin(2*gillnext::detail::pi*997*i/48000));stereo.sample({x,x},2);}const auto before=stereo.integrated;for(int i=0;i<48000*4;++i)stereo.sample({0,0},2);check(std::abs(before+20)<.05,"stereo summation uses channel energy rather than channel average",before);check(std::abs(stereo.integrated-before)<.3,"integrated loudness gates trailing silence",stereo.integrated-before);stereo.reset();check(stereo.integrated==-100,"meter reset removes previous programme");
 check(allocations==0,"no allocation in measured CLEAN and loudness audio processing",allocations);std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
