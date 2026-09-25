#include "../Source/CleanDSP.h"
#include <cstdio>
#include <random>
#include <limits>
#include <fstream>
#include <new>
#include <cstdlib>

// Frozen CleanDSP.h SHA256 at test start:
// 3FE9E2610EA17D0AA87AB75E2CB4E8090ED7BD76EAF1812E5F1ADDF68A7AD4D6
namespace probe { bool active=false; size_t count=0; }
void*operator new(size_t n){if(probe::active)++probe::count;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
namespace {
constexpr double pi=3.14159265358979323846;
int checks=0,failures=0;
void check(bool pass,const char* name,double value=0){++checks;if(!pass)++failures;std::printf("%s %s %.10g\n",pass?"PASS":"FAIL",name,value);std::fflush(stdout);}
using Audio=std::array<std::vector<float>,2>;
enum class Fixture {Plosive,Voice,Breath};
double voice(double t){
 return .055*std::sin(2*pi*120*t)+.063*std::sin(2*pi*240*t+.3)
       +.042*std::sin(2*pi*480*t+.1)+.028*std::sin(2*pi*960*t+.7)
       +.032*std::sin(2*pi*3360*t+.2)+.021*std::sin(2*pi*5280*t+.9);
}
Audio fixture(double fs,Fixture which){
 Audio out;for(auto&c:out)c.resize(size_t(fs*1.8));
 std::mt19937 rng(711);std::normal_distribution<double> random(0,1);
 double low=0;const double pole=std::exp(-2*pi*2200/fs);
 for(size_t i=0;i<out[0].size();++i){const double t=i/fs;
  const double n=random(rng);low=pole*low+(1-pole)*n;
  double x=voice(t)*std::min(1.,t/.03);
  if(which==Fixture::Plosive){const double d=(t-1.10)/.035;x+=.45*std::exp(-.5*d*d)*std::sin(2*pi*70*t);}
  if(which==Fixture::Breath){
   x*=std::clamp((1.-t)/.025,0.,1.);
   const double env=std::clamp((t-1.02)/.025,0.,1.)*std::clamp((1.55-t)/.025,0.,1.);
   x+=.008*env*(n-low);
  }
  out[0][i]=float(x);out[1][i]=float(x*.7);
 }return out;
}
struct Render {Audio audio;int latency=0;};
Render run(const Audio&in,double fs,gillnext::CleanParameters p,int block=127){
 gillnext::CleanDSP dsp;dsp.setParameters(p);dsp.prepare(fs,block,2);
 Render r{in,dsp.latencySamples()};
 for(int at=0;at<int(in[0].size());at+=block){float*ptr[]{r.audio[0].data()+at,r.audio[1].data()+at};
  probe::active=true;dsp.process(ptr,2,std::min(block,int(in[0].size())-at));probe::active=false;
 }return r;
}
double power(const std::vector<float>&a,int from,int to){double p=0;for(int i=from;i<to;++i)p+=double(a[i])*a[i];return p/std::max(1,to-from);}
double ratio(const Render&r,const Audio&in,double fs,double from,double to){
 const int a=int(from*fs),b=int(to*fs);return 10*std::log10(std::max(1e-30,power(r.audio[0],a+r.latency,b+r.latency))/std::max(1e-30,power(in[0],a,b)));
}
double amplitude(const std::vector<float>&a,int from,int to,double fs,double frequency){
 double re=0,im=0;for(int i=from;i<to;++i){const double w=2*pi*frequency*i/fs;re+=a[i]*std::cos(w);im+=a[i]*std::sin(w);}return 2*std::hypot(re,im)/(to-from);
}
double fundamental(const Render&r,const Audio&in,double fs){
 const int a=int(.45*fs),b=int(.85*fs);return 20*std::log10(amplitude(r.audio[0],a+r.latency,b+r.latency,fs,120)/amplitude(in[0],a,b,fs,120));
}
double plosiveResidual(const Render&r,const Audio&in,double fs){
 const int a=int(.99*fs),b=int(1.23*fs);double original=0,after=0;
 for(int i=a;i<b;++i){const double v=voice(i/fs),before=in[0][i]-v,out=r.audio[0][i+r.latency]-v;original+=before*before;after+=out*out;}
 return 10*std::log10(std::max(after,1e-30)/std::max(original,1e-30));
}
void measuredControls(){
 std::ofstream csv("CleanSpecificTests-metrics.csv");
 csv<<"sample_rate,control,amount,event_attenuation_db,steady_voice_db,fundamental_120hz_db\n";
 for(double fs:{22050.,44100.,48000.,88200.,96000.,192000.}){
  const auto plosive=fixture(fs,Fixture::Plosive),breath=fixture(fs,Fixture::Breath),harmonic=fixture(fs,Fixture::Voice);
  std::array<double,3> pops{},breaths{};
  for(int step=0;step<3;++step){const float amount=float(step*50);
   const auto pop=run(plosive,fs,{0,amount,0,0}),air=run(breath,fs,{0,0,amount,0}),vocal=run(harmonic,fs,{0,amount,amount,0});
   pops[step]=plosiveResidual(pop,plosive,fs);breaths[step]=ratio(air,breath,fs,1.10,1.42);
   const double vocalChange=ratio(vocal,harmonic,fs,.45,.85),lowChange=fundamental(vocal,harmonic,fs);
   csv<<fs<<",PLOSIVES,"<<amount<<','<<pops[step]<<','<<vocalChange<<','<<lowChange<<'\n';
   csv<<fs<<",BREATHS,"<<amount<<','<<breaths[step]<<','<<vocalChange<<','<<lowChange<<'\n';
   std::printf("METRIC fs=%.0f amount=%.0f plosive=%.6f dB breath=%.6f dB voice=%.6f dB fundamental=%.6f dB\n",fs,amount,pops[step],breaths[step],vocalChange,lowChange);
   check(std::abs(vocalChange)<.2&&std::abs(lowChange)<.2,"steady harmonic voice and120Hz fundamental retained within0.2dB",lowChange);
  }
  check(std::abs(pops[0])<1e-6&&std::abs(breaths[0])<1e-6,"zero controls preserve aligned disturbance levels");
  check(pops[1]<pops[0]-.25&&pops[2]<pops[1]-.25,"PLOSIVES 0/50/100 progressively reduce the injected low burst",pops[2]);
  check(pops[2]<-2,"full PLOSIVES measurably reduces burst residual, not guaranteed removal",pops[2]);
  check(breaths[1]<breaths[0]-.25&&breaths[2]<breaths[1]-.25,"BREATHS 0/50/100 progressively reduce diffuse post-voice breath",breaths[2]);
  check(breaths[2]<-2,"full BREATHS measurably reduces the post-voice breath",breaths[2]);
 }
}
void listenSwitch(){
 const double fs=48000;const auto original=fixture(fs,Fixture::Plosive);
 std::array<Render,4> reference;for(int mode=0;mode<4;++mode)reference[mode]=run(original,fs,{65,80,70,mode});
 gillnext::CleanDSP dsp;gillnext::CleanParameters p{65,80,70,0};dsp.setParameters(p);dsp.prepare(fs,1,2);
 auto actual=original;std::array<double,4> weight{1,0,0,0};const double alpha=1-std::exp(-1/(fs*.005));
 double worst=0,worstFirstStep=0;int switches=0;
 for(int i=0;i<int(original[0].size());++i){
  const int mode=(i<int(fs*.25)?0:i<int(fs*.5)?1:i<int(fs*.8)?2:i<int(fs*1.2)?3:0);
  const bool changed=p.listen!=mode;if(changed){++switches;p.listen=mode;}
  double previousMix=0;for(int j=0;j<4;++j)previousMix+=weight[j]*reference[j].audio[0][i];
  float*ptr[]{actual[0].data()+i,actual[1].data()+i};probe::active=true;dsp.setParameters(p);dsp.process(ptr,2,1);probe::active=false;
  double expected=0;for(int j=0;j<4;++j){weight[j]+=alpha*((mode==j?1:0)-weight[j]);expected+=weight[j]*reference[j].audio[0][i];}
  worst=std::max(worst,std::abs(expected-actual[0][i]));
  if(changed)worstFirstStep=std::max(worstFirstStep,std::abs(actual[0][i]-previousMix));
 }
 check(switches==4&&worst<1e-7,"LISTEN transitions follow continuous5ms blend of independently rendered components",worst);
 check(worstFirstStep<.002,"LISTEN switch contribution has no instantaneous full-route jump",worstFirstStep);
}
void invalidInputs(){
 bool finite=true;double peak=0;
 for(double fs:{22050.,44100.,48000.,88200.,96000.,192000.}){
  gillnext::CleanDSP dsp;dsp.prepare(fs,257,2);std::array<float,257> l{},r{};float*ptr[]{l.data(),r.data()};
  for(int block=0;block<160;++block){
   for(int i=0;i<257;++i)l[i]=r[i]=float(.12*std::sin((block*257+i)*.12));
   l[block%257]=std::numeric_limits<float>::quiet_NaN();r[(block+15)%257]=std::numeric_limits<float>::infinity();r[(block+70)%257]=-std::numeric_limits<float>::infinity();
   gillnext::CleanParameters p;p.noise=(block%3?std::numeric_limits<float>::quiet_NaN():100);p.plosives=(block%3?std::numeric_limits<float>::infinity():100);p.breaths=(block%3?-std::numeric_limits<float>::infinity():100);p.listen=block%2?999:-999;
   probe::active=true;dsp.setParameters(p);dsp.process(ptr,2,257);probe::active=false;
   for(int i=0;i<257;++i){finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i]);peak=std::max({peak,std::abs(double(l[i])),std::abs(double(r[i]))});}
   for(float x:dsp.reductionsDb())finite=finite&&std::isfinite(x);
   finite=finite&&std::isfinite(dsp.inputRms())&&std::isfinite(dsp.outputRms());
  }
 }
 check(finite&&peak<1,"six rates: NaN/Inf input and controls do not poison samples or meters",peak);
 check(probe::count==0,"measured audio processing, LISTEN and invalid parameter updates allocate no C++ heap",double(probe::count));
}
}
int main(){measuredControls();listenSwitch();invalidInputs();std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}
