#include "LiveCausal.h"
#include "../GILLVOCAL/Source/HeatDSP.h"
#include "../GILLVOCAL/Source/TuneDSP.h"
#include "../GILLEFFECTS/Source/AirDSP.h"
#include "../GILLDYNAMICS/Source/BussDSP.h"
#include "../GILLDYNAMICS/Source/VocalDynamicsDSP.h"
#include "../GILLFINISH/Source/SpectralDSP.h"
#include "../GILLDEREVERB/Source/DereverbDSP.h"
#include "../GILLRESTORATION/Source/RestorationDSP.h"
#include "../GILLNEXT/Source/CleanDSP.h"
#include "../GILLNEXT/Source/FormDSP.h"
#include "../GILLNEXT/Source/FinishDSP.h"
#include <iostream>
#include <memory>
#include <vector>
#include <limits>
#include <cstdlib>
#include <new>
static bool observe=false;static size_t allocations=0;
void*operator new(std::size_t n){if(observe)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
int checks=0,failures=0;
void check(bool ok,const std::string&s){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<s<<'\n';}
namespace efficacy {
constexpr double pi=3.14159265358979323846;
using Audio=std::vector<float>;
double power(const Audio&a,int from,int to){double sum=0;for(int i=from;i<to;++i)sum+=double(a[size_t(i)])*a[size_t(i)];return sum/std::max(1,to-from);}
double difference(const Audio&a,const Audio&b,int from,int to){double sum=0;for(int i=from;i<to;++i){const double x=a[size_t(i)]-b[size_t(i)];sum+=x*x;}return std::sqrt(sum/std::max(1,to-from));}
double amplitude(const Audio&a,double fs,double hz,int from,int to){double real=0,imaginary=0;for(int i=from;i<to;++i){const double phase=2*pi*hz*i/fs;real+=a[size_t(i)]*std::cos(phase);imaginary-=a[size_t(i)]*std::sin(phase);}return 2*std::hypot(real,imaginary)/(to-from);}
double ratioDb(double output,double input){return 10*std::log10(std::max(1e-30,output)/std::max(1e-30,input));}
double noise(std::uint32_t& state){state=state*1664525u+1013904223u;return state/2147483648.-1;}
template<class E,class Call>void process(E&e,Audio&x,Call call,int block=127){for(size_t i=0;i<x.size();i+=block){float* p=x.data()+i;observe=true;call(e,&p,int(std::min<size_t>(block,x.size()-i)));observe=false;}}
void spectral(){
 for(double fs:{44100.,48000.,96000.}){
  Audio source(size_t(fs*.6));for(size_t i=0;i<source.size();++i)source[i]=float(.16*std::sin(2*pi*3500*i/fs)+.035*std::sin(2*pi*220*i/fs));
  auto silk=std::make_unique<gillfinish::SilkDSP>();silk->prepare(fs,127,1);silk->setLiveMode(true);gillfinish::SilkParameters p;p.depth=100;p.sensitivity=100;p.lowHz=1500;p.highHz=6500;p.attackMs=1;p.releaseMs=70;silk->setParameters(p);silk->reset();auto wet=source;process(*silk,wet,[](auto&d,float**a,int n){d.process(a,n,1);});
  const int first=int(fs*.2),last=int(fs*.6);const double resonantDb=20*std::log10(amplitude(wet,fs,3500,first,last)/amplitude(source,fs,3500,first,last));const double voiceDb=20*std::log10(amplitude(wet,fs,220,first,last)/amplitude(source,fs,220,first,last));
  std::cout<<"METRIC Silk "<<fs<<" resonance="<<resonantDb<<" dB voice="<<voiceDb<<" dB\n";
  check(resonantDb<-.5,"Silk LIVE attenuates measured in-range resonance "+std::to_string(int(fs)));check(std::abs(voiceDb)<.5,"Silk LIVE preserves out-of-range voiced fundamental "+std::to_string(int(fs)));
  p.lowHz=10000;p.highHz=16000;silk->setParameters(p);silk->reset();auto unfocused=source;process(*silk,unfocused,[](auto&d,float**a,int n){d.process(a,n,1);});check(difference(unfocused,source,first,last)<difference(wet,source,first,last)*.2,"Silk LIVE focus moves actual attenuation away from resonance");
  source.assign(size_t(fs*.8),0);for(size_t i=0;i<source.size();++i){const double t=i/fs;source[i]=float(.025*std::sin(2*pi*220*t)+(t>=.2&&t<.32?.2*std::sin(2*pi*5000*t):0));}
  for(int mode:{0,1}){auto spark=std::make_unique<gillfinish::SparkDSP>();spark->prepare(fs,127,1);spark->setLiveMode(true);gillfinish::SparkParameters settings;settings.depth=100;settings.sensitivity=100;settings.lowHz=2000;settings.highHz=9000;settings.attackMs=.5;settings.decayMs=50;settings.mode=mode;spark->setParameters(settings);spark->reset();auto out=source;process(*spark,out,[](auto&d,float**a,int n){d.process(a,n,1);});const double db=ratioDb(power(out,int(fs*.206),int(fs*.23)),power(source,int(fs*.206),int(fs*.23)));std::cout<<"METRIC Spark "<<fs<<" mode="<<mode<<" onset="<<db<<" dB\n";check(mode?db>.5:db<-.5,mode?"Spark LIVE BOOST increases onset energy":"Spark LIVE CUT reduces onset energy");check(difference(out,source,int(fs*.60),int(fs*.78))<.001,"Spark LIVE recovers voiced background after transient");}
 }
}
Audio cleanProcess(const Audio& source,double fs,gillnext::CleanParameters p){auto d=std::make_unique<gillnext::CleanDSP>();d->prepare(fs,127,1);d->setLiveMode(true);d->setParameters(p);d->reset();auto out=source;process(*d,out,[](auto&d,float**a,int n){d.process(a,1,n);});return out;}
void clean(){
 constexpr double fs=48000;std::uint32_t rng=15723;
 Audio hiss(size_t(fs*8));for(auto&v:hiss)v=float(.01*noise(rng));gillnext::CleanParameters noiseOnly;noiseOnly.noise=100;noiseOnly.plosives=noiseOnly.breaths=0;auto denoised=cleanProcess(hiss,fs,noiseOnly);const double noiseDb=ratioDb(power(denoised,int(fs*7),int(fs*8)),power(hiss,int(fs*7),int(fs*8)));std::cout<<"METRIC Clean stationary noise="<<noiseDb<<" dB after 7s floor adaptation\n";check(noiseDb<-3,"Clean LIVE NOISE reduces stationary low-level hiss after adaptation");
 auto voice=hiss;for(size_t i=size_t(fs*6);i<voice.size();++i)voice[i]+=float(.15*std::sin(2*pi*220*i/fs));auto voiced=cleanProcess(voice,fs,noiseOnly);const double voiceDb=20*std::log10(amplitude(voiced,fs,220,int(fs*6.5),int(fs*7.5))/amplitude(voice,fs,220,int(fs*6.5),int(fs*7.5)));check(std::abs(voiceDb)<.75,"Clean LIVE NOISE preserves newly arriving voice above learned floor");
 Audio plosive(static_cast<size_t>(fs),0.f);for(size_t i=0;i<plosive.size();++i){const double t=i/fs;plosive[i]=float(.03*std::sin(2*pi*350*t)+(t>=.25&&t<.37?.8*std::sin(2*pi*65*(t-.25)):0));}gillnext::CleanParameters pop;pop.noise=pop.breaths=0;pop.plosives=100;auto popped=cleanProcess(plosive,fs,pop);const double popDb=ratioDb(power(popped,int(fs*.26),int(fs*.31)),power(plosive,int(fs*.26),int(fs*.31)));std::cout<<"METRIC Clean plosive="<<popDb<<" dB\n";check(popDb<-1,"Clean LIVE PLOSIVES reduces low-frequency burst");check(difference(popped,plosive,int(fs*.7),int(fs*.9))<.002,"Clean LIVE plosive suppression releases back to voice");
 Audio breath(size_t(fs*1.2));double low=0;for(size_t i=0;i<breath.size();++i){const double t=i/fs;const double n=noise(rng);low+=.38*(n-low);breath[i]=float(t<.6?.15*std::sin(2*pi*220*t)+.035*std::sin(2*pi*440*t):t<1.0?.075*(n-low):0);}gillnext::CleanParameters air;air.noise=air.plosives=0;air.breaths=100;auto breathed=cleanProcess(breath,fs,air);const double breathDb=ratioDb(power(breathed,int(fs*.72),int(fs*.92)),power(breath,int(fs*.72),int(fs*.92)));std::cout<<"METRIC Clean breath="<<breathDb<<" dB\n";check(breathDb<-1,"Clean LIVE BREATHS reduces diffuse high-frequency breath after vocal context");check(difference(breathed,breath,int(fs*.2),int(fs*.5))<.0005,"Clean LIVE breath control retains preceding voiced section");
 // All three removed-listen buses should sum back to the processing residual,
 // including detector release. This tests audible buffers, not meter values.
 for(int fixture=0;fixture<3;++fixture){const Audio& source=fixture==0?hiss:fixture==1?plosive:breath;auto settings=fixture==0?noiseOnly:fixture==1?pop:air;auto result=cleanProcess(source,fs,settings);std::array<Audio,3>removed;for(int mode=1;mode<=3;++mode){settings.listen=mode;removed[size_t(mode-1)]=cleanProcess(source,fs,settings);}double reconstruction=0;double intended=0,other=0;for(size_t i=0;i<source.size();++i){double sum=result[i];for(int j=0;j<3;++j){sum+=removed[size_t(j)][i];const double e=double(removed[size_t(j)][i])*removed[size_t(j)][i];(j==fixture?intended:other)+=e;}reconstruction=std::max(reconstruction,std::abs(sum-source[i]));}std::cout<<"METRIC Clean listen fixture="<<fixture<<" reconstructionMax="<<reconstruction<<'\n';check(intended>1e-6&&other<intended*1e-8,"Clean LIVE LISTEN routes only selected removal family");check(reconstruction<2e-6,"Clean LIVE output plus three LISTEN buses reconstructs source through release");}
}
template<class Render>void continuous(const std::string& name,const Audio& source,Render render){
 Audio previous;double previousDistance=-1,minStep=1e9,maxStep=0,middle=0,late=0,full=0;bool monotone=true;const int first=int(source.size()/2),last=int(source.size());
 for(int percent=0;percent<=100;++percent){auto out=render(percent);const double distance=difference(out,source,first,last);if(percent==0)check(distance<1e-6,name+" zero strength is transparent");if(previousDistance>=0){monotone&=distance+1e-7>=previousDistance;const double step=difference(out,previous,first,last);minStep=std::min(minStep,step);maxStep=std::max(maxStep,step);}if(percent==20)middle=distance;if(percent==70)late=distance;if(percent==100)full=distance;previousDistance=distance;previous=std::move(out);}
 std::cout<<"METRIC "<<name<<" residual20="<<middle<<" residual70="<<late<<" residual100="<<full<<" min1%step="<<minStep<<" maxStep/full="<<maxStep/std::max(full,1e-20)<<'\n';
 check(monotone,name+" effect distance increases over all 101 strength positions");check(minStep>1e-8,name+" every 1% step changes output");check(late>middle*1.25&&full>.0005,name+" has substantial usable effect in middle and maximum ranges");check(maxStep<full*.07,name+" no abrupt 1% jump exceeds 7% of full effect");
}
void colour(){
 constexpr double fs=48000;const int count=9600;
 for(int style=0;style<3;++style)for(int band=0;band<3;++band){const double hz=band==0?100:band==1?1000:6000;Audio source(count);for(int i=0;i<count;++i)source[size_t(i)]=float(.15*std::sin(2*pi*hz*i/fs));auto heat=std::make_unique<gill::HeatDSP>();heat->prepare(fs,127,1);heat->setLiveMode(true);continuous("Heat style "+std::to_string(style)+" band "+std::to_string(band),source,[&](int percent){std::array<float,3> a{};a[size_t(band)]=percent*.24f;heat->setParameters(a[0],a[1],a[2],style,100,0);heat->reset();auto out=source;process(*heat,out,[](auto&d,float**a,int n){d.process(a,1,n);});return out;});}
 for(int band=0;band<2;++band){const double hz=band?6000:3000;Audio source(count);for(int i=0;i<count;++i)source[size_t(i)]=float(.15*std::sin(2*pi*hz*i/fs));auto air=std::make_unique<gill::AirDSP>();air->prepare(fs,127,1);air->setLiveMode(true);continuous("Air band "+std::to_string(band),source,[&](int percent){air->setParameters(band?0.f:float(percent),band?float(percent):0.f,100,0);air->reset();auto out=source;process(*air,out,[](auto&d,float**a,int n){d.process(a,1,n);});return out;});}
 for(int style=0;style<3;++style){Audio source(count);for(int i=0;i<count;++i)source[size_t(i)]=float(.2*std::sin(2*pi*1000*i/fs));auto buss=std::make_unique<gilldyn::BussDSP>();buss->prepare(fs,127,1);buss->setLiveMode(true);continuous("Buss style "+std::to_string(style),source,[&](int percent){gilldyn::BussParameters p;p.driveDb=percent*.24f;p.style=style;buss->setParameters(p);buss->reset();auto out=source;process(*buss,out,[](auto&d,float**a,int n){d.process(a,n,1);});return out;});}
}
void finish(){
 for(double fs:{8000.,44100.,48000.,96000.,192000.})for(float ceiling:{-3.f,-1.f,0.f}){
  auto d=std::make_unique<gillnext::FinishDSP>();d->prepare(fs,127,2);d->setLiveMode(true);gillnext::FinishParameters p;p.toneEnabled=p.compEnabled=p.clipEnabled=p.stereoEnabled=false;p.limiterEnabled=true;p.ceilingDb=ceiling;p.driveDb=18;d->setParameters(p);d->reset();const int count=int(fs*.04);Audio left(size_t(count),0),right(size_t(count),0);for(int i=0;i<count;++i){left[size_t(i)]=float(1.2*std::sin(2*pi*997*i/fs)+.3*std::sin(2*pi*std::min(6000.,fs*.35)*i/fs));if(i==0||i==17)left[size_t(i)]=i==0?32:-32;right[size_t(i)]=-.37f*left[size_t(i)];}float* data[]{left.data(),right.data()};observe=true;d->process(data,2,count);observe=false;double peak=0,stereoError=0;for(int i=0;i<count;++i){peak=std::max({peak,std::abs(double(left[size_t(i)])),std::abs(double(right[size_t(i)]))});stereoError=std::max(stereoError,std::abs(right[size_t(i)]+.37*left[size_t(i)]));}check(peak<=std::pow(10.,ceiling/20.)*(1+3e-7),"Finish LIVE obeys sample ceiling including first impulsive sample "+std::to_string(int(fs))+" / "+std::to_string(ceiling));check(stereoError<2e-7,"Finish LIVE linked limiter preserves stereo level/polarity relation");check(power(left,0,count)>.001,"Finish LIVE limiter retains audible output");
 }
}
template<class E,class Call>void stereo(E& d,Call call,const std::string& name){Audio source(8192),silent(8192,0);for(int i=0;i<8192;++i)source[size_t(i)]=float(.15*std::sin(i*.133)+.03*std::sin(i*.71));float* data[]{source.data(),silent.data()};observe=true;call(d,data,8192);observe=false;bool noLeak=true;for(auto x:silent)noLeak&=x==0;check(noLeak,name+" LIVE has no leakage from active left into silent right");d.reset();auto same=source;data[0]=source.data();data[1]=same.data();observe=true;call(d,data,8192);observe=false;check(source==same,name+" LIVE keeps dual-mono stereo identical");}
void stereoIntegrity(){
 {auto d=std::make_unique<gillfinish::SilkDSP>();d->prepare(48000,127,2);d->setLiveMode(true);stereo(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Silk");}
 {auto d=std::make_unique<gillfinish::SparkDSP>();d->prepare(48000,127,2);d->setLiveMode(true);stereo(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Spark");}
 {auto d=std::make_unique<gillnext::CleanDSP>();d->prepare(48000,127,2);d->setLiveMode(true);stereo(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Clean");}
 {auto d=std::make_unique<gill::HeatDSP>();d->prepare(48000,127,2);d->setLiveMode(true);d->setParameters(24,24,24,0,100,0);d->reset();stereo(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Heat");}
 {auto d=std::make_unique<gill::AirDSP>();d->prepare(48000,127,2);d->setLiveMode(true);d->setParameters(100,100,100,0);d->reset();stereo(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Air");}
 {auto d=std::make_unique<gilldyn::BussDSP>();d->prepare(48000,127,2);d->setLiveMode(true);gilldyn::BussParameters p;p.driveDb=24;p.style=1;d->setParameters(p);d->reset();stereo(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Buss");}
}
}
template<class E,class Process>void sweep(E&d,Process process,const std::string&label,int latency=0){
 std::array<std::array<float,2048>,2>b{};std::array<float*,2>p{b[0].data(),b[1].data()};bool valid=true;
 for(int block:{1,16,64,127,256,2048}){for(int i=0;i<block;++i){b[0][i]=float(.12*std::sin(i*.37));b[1][i]=-b[0][i];}
  if(block==256){b[0][3]=std::numeric_limits<float>::quiet_NaN();b[1][6]=std::numeric_limits<float>::infinity();}
  observe=true;process(d,p.data(),block);observe=false;
  for(int c=0;c<2;++c)for(int i=0;i<block;++i)valid&=std::isfinite(b[c][i])&&std::abs(b[c][i])<32.01f;
 }
 check(valid,label+" finite variable-block audio");
 observe=true;d.setLiveMode(false);d.setLiveMode(true);observe=false;
 (void)latency;
}
int main(){
 for(double fs:{8000.,44100.,48000.,96000.,192000.}){
  const auto rate=std::to_string(int(fs));
  {auto d=std::make_unique<gill::HeatDSP>();d->prepare(fs,127,2);d->setLiveMode(true);d->setParameters(12,12,12,1,100,0);check(d->latencySamples()==0,"Heat LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Heat "+rate);}
  {auto d=std::make_unique<gill::AirDSP>();d->prepare(fs,127,2);d->setLiveMode(true);d->setParameters(70,70,100,0);check(d->latencySamples()==0,"Air LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Air "+rate);}
  {auto d=std::make_unique<gilldyn::BussDSP>();d->prepare(fs,127,2);d->setLiveMode(true);check(d->latencySamples()==0,"Buss LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Buss "+rate);}
  {auto d=std::make_unique<gilldyn::VoxDSP>();d->prepare(fs,127,2);d->setLiveMode(true);check(d->latencySamples()==0,"Vox LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Vox "+rate);}
  {auto d=std::make_unique<gillfinish::SilkDSP>();d->prepare(fs,127,2);d->setLiveMode(true);check(d->latencySamples()==0,"Silk LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Silk "+rate);}
  {auto d=std::make_unique<gillfinish::SparkDSP>();d->prepare(fs,127,2);d->setLiveMode(true);check(d->latencySamples()==0,"Spark LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,n,2);},"Spark "+rate);}
  {auto d=std::make_unique<gilldereverb::DereverbEngine>();d->prepare(fs);d->setLiveMode(true);check(d->getLatencySamples()==0,"Room LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Room "+rate);}
  for(auto mode:{gillrestoration::Mode::Declick,gillrestoration::Mode::Decrackle}){auto d=std::make_unique<gillrestoration::RestorationEngine>();d->prepare(fs,mode);d->setLiveMode(true);check(d->getLatencySamples()==0,"Restoration LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Restoration "+rate);}
  {auto d=std::make_unique<gillnext::CleanDSP>();d->prepare(fs,127,2);d->setLiveMode(true);check(d->latencySamples()==0,"Clean LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Clean "+rate);}
  {auto d=std::make_unique<gillnext::FinishDSP>();d->prepare(fs,127,2);d->setLiveMode(true);check(d->latencySamples()==0,"Finish LIVE0 "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Finish "+rate);}
  {auto d=std::make_unique<gill::TuneDSP>();d->prepare(fs,127,2);auto pro=d->latencySamples();d->setLiveMode(true);check(d->latencySamples()>0&&d->latencySamples()<pro&&d->latencySamples()<fs*.017,"Tune truthful sub17ms "+rate);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Tune "+rate);}
  {auto d=std::make_unique<gillnext::FormDSP>();d->prepare(fs,127,2);auto pro=d->latencySamples();d->setLiveMode(true);check(d->latencySamples()>0&&d->latencySamples()<pro&&d->latencySamples()<fs*.060,"Form smaller truthful buffer "+rate);gillnext::FormParameters p;p.pitchSemitones=5;p.formantSemitones=-2;d->setParameters(p);sweep(*d,[](auto&d,float**p,int n){d.process(p,2,n);},"Form "+rate);}
 }
 // Impulse repair effectiveness without shifting the clean surrounding signal.
 for(bool crackle:{false,true}){gill::live::ClickRepair d;d.prepare(48000,crackle);d.setAmount(1);d.reset();std::array<float,4800>b{},ref{};for(int i=0;i<4800;++i)b[i]=ref[i]=float(.1*std::sin(6.28318530718*220*i/48000));b[2400]+=.8f;float*ptr=b.data();d.process(&ptr,1,int(b.size()));double cleanError=0;for(int i=500;i<2200;++i)cleanError=std::max(cleanError,double(std::abs(b[i]-ref[i])));check(std::abs(b[2400]-ref[2400])<.3,"LIVE repairs isolated vocal click");check(cleanError<.005,"LIVE retains clean voiced tone");}
 // All causal bands are exactly transparent at zero strength/mix after reset.
 for(double hz:{100.,220.,1000.,5000.,12000.,18000.}){gill::live::ClickRepair d;d.prepare(48000,false);d.setAmount(1);d.reset();std::vector<float>b(48000),ref(48000);for(int i=0;i<48000;++i)b[i]=ref[i]=float(.15*std::sin(6.28318530718*hz*i/48000));float*ptr=b.data();d.process(&ptr,1,48000);double err=0,power=0;for(int i=12000;i<48000;++i){err+=(b[i]-ref[i])*(b[i]-ref[i]);power+=ref[i]*ref[i];}check(err<power*.0001,"causal click repair preserves steady tone at "+std::to_string(hz));}
 for(auto kind:{gill::live::Kind::Room,gill::live::Kind::Silk,gill::live::Kind::Spark,gill::live::Kind::Clean}){gill::live::CausalBands d;d.prepare(48000,2,kind);gill::live::Settings s;s.amount=s.noise=s.plosives=s.breaths=0;d.set(s);d.reset();std::array<float,2048>b{},ref{};for(int i=0;i<2048;++i)b[i]=ref[i]=float(.3*std::sin(i*.19));float*ptr=b.data();d.process(&ptr,1,2048);check(b==ref,"causal zero strength is sample-exact with zero delay");}
 // A declining room tail should be attenuated while the attack is retained.
 {gill::live::CausalBands d;d.prepare(48000,1,gill::live::Kind::Room);gill::live::Settings s;s.amount=1;s.preserve=.3;d.set(s);d.reset();std::vector<float>b(48000),ref(48000);for(int i=0;i<48000;++i)b[i]=ref[i]=float(.3*std::exp(-i/8000.)*std::sin(i*.04));float*ptr=b.data();d.process(&ptr,1,48000);double before=0,after=0;for(int i=10000;i<30000;++i){before+=ref[i]*ref[i];after+=b[i]*b[i];}check(after<before*.8,"causal room mode audibly reduces declining tails");}
 efficacy::spectral();efficacy::clean();efficacy::colour();efficacy::finish();efficacy::stereoIntegrity();
 check(allocations==0,"LIVE processing and runtime mode switches allocate no C++ heap");
 std::cout<<"RESULT "<<checks<<" checks "<<failures<<" failures allocations="<<allocations<<'\n';return failures?1:0;
}
