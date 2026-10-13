#include "../../Source/Spectral/Spectral.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <chrono>
#include <ctime>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static bool watchAllocation=false;
static std::size_t allocations=0;
void* operator new(std::size_t n){if(watchAllocation)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace gillstudio::spectral;
using Audio=std::array<std::vector<float>,2>;
static unsigned checks=0;
void require(bool value,const std::string& text){++checks;if(!value)throw std::runtime_error(text);}
double cpuSeconds(){
#ifdef _WIN32
 FILETIME created{},ended{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user))throw std::runtime_error("process CPU clock unavailable");
 const auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};return double(ticks(kernel)+ticks(user))*1e-7;
#else
 const auto t=std::clock();if(t==std::clock_t(-1))throw std::runtime_error("process CPU clock unavailable");return double(t)/CLOCKS_PER_SEC;
#endif
}
Audio zeros(std::size_t n){return {std::vector<float>(n),std::vector<float>(n)};}
double noise(std::uint32_t& rng){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return double(rng)/2147483648.-1.;}
Audio stimulus(std::size_t n,double fs){auto a=zeros(n);std::uint32_t rng=12345;
 for(std::size_t i=0;i<n;++i){a[0][i]=float(.09*std::sin(2*pi*307*i/fs)+.008*noise(rng));a[1][i]=float(.07*std::cos(2*pi*5011*i/fs)+.005*noise(rng));}return a;}
void render(Processor&p,Audio&a,const std::vector<std::size_t>& blocks={257}){
 std::size_t pos=0,k=0;while(pos<a[0].size()){const auto n=std::min(blocks[k++%blocks.size()],a[0].size()-pos);float*ptrs[]{a[0].data()+pos,a[1].data()+pos};p.process(ptrs,n);pos+=n;}}
double peakError(const Audio&a,const Audio&b,std::size_t delay){double e=0;for(int c=0;c<2;++c)for(std::size_t i=0;i<a[c].size();++i){const double ref=i<delay?0:b[c][i-delay];e=std::max(e,std::abs(a[c][i]-ref));}return e;}
double tone(const std::vector<float>&a,double fs,double hz,std::size_t start,std::size_t count){
 double real=0,imag=0;const double w=2*pi*hz/fs;
 for(std::size_t i=start;i<start+count;++i){real+=a[i]*std::cos(w*i);imag-=a[i]*std::sin(w*i);}return 2*std::hypot(real,imag)/count;}
double noiseBand(const std::vector<float>&a,double fs,std::size_t start,std::size_t count){double energy=0;
 for(int hz=2200;hz<5800;hz+=113)if(std::abs(hz-3500)>250)energy+=std::pow(tone(a,fs,hz,start,count),2);return energy;}
void reconstruction(){
 for(double fs:{44100.,48000.,96000.,192000.})for(auto mode:{Mode::Pro,Mode::Live})for(auto routing:{Channels::Stereo,Channels::MidSide}){
  Processor p; p.prepare(fs,257,2,1024);Parameters par;par.mode=mode;par.routing=routing;p.setParameters(par);p.reset();
  auto source=stimulus(13001,fs),a=source;render(p,a,{1,17,64,257,1024});const auto error=peakError(a,source,p.latencySamples());require(error<2e-7,"neutral reconstruction/routing");
  auto impulse=zeros(4096);impulse[0][0]=1;impulse[1][0]=.5;p.reset();render(p,impulse);
  const auto peak=std::max_element(impulse[0].begin(),impulse[0].end())-impulse[0].begin();require(std::size_t(peak)==p.latencySamples(),"measured impulse delay");require(std::abs(impulse[0][peak]-1)<2e-7,"impulse unity");
 }
 // Deliberately compare to incorrect latency: a checker that ignores alignment fails.
 Processor p;p.prepare(48000,256,2,1024);auto s=stimulus(9000,48000),a=s;render(p,a);require(peakError(a,s,1023)>.001,"negative latency control");
 std::cout<<"neutral and measured latency: 16 configurations passed\n";
}
void blockAndRuntime(){
 Processor p,q;p.prepare(48000,257,2);q.prepare(48000,257,2);Parameters par;par.transient=.7;par.resonance=.8;par.lowHz=700;par.highHz=13000;
 p.setParameters(par);q.setParameters(par);auto a=stimulus(48000,48000),b=a;render(p,a,{1,17,64,257});render(q,b,{1024});require(peakError(a,b,0)==0,"block partition invariance");
 const std::vector<std::size_t> blocks{257};allocations=0;watchAllocation=true;par.sensitivity=.93;p.setParameters(par);p.reset();render(p,a,blocks);watchAllocation=false;require(allocations==0,"audio path allocated memory");
 watchAllocation=true;void* allocated=::operator new(17);watchAllocation=false;::operator delete(allocated);require(allocations>0,"allocation interception positive control failed");
 // Parameters/state input corruption must fail safely, without poisoning subsequent finite samples.
 par.attackMs=std::numeric_limits<double>::quiet_NaN();par.highHz=std::numeric_limits<double>::infinity();par.nodes[0].enabled=true;par.nodes[0].q=0;
 p.setParameters(par);a[0][50]=std::numeric_limits<float>::infinity();a[1][120]=std::numeric_limits<float>::quiet_NaN();render(p,a);
 for(const auto&c:a)for(auto x:c)require(std::isfinite(x),"nonfinite output");
 std::cout<<"partition, realtime allocation and hostile values passed\n";
}
void denoise(){
 constexpr double fs=48000;Processor p;p.prepare(fs,257,2);Parameters par;par.learn=true;par.lowHz=2000;par.highHz=6000;par.preserveTransient=0;par.maxReductionDb=30;par.attackMs=1;par.releaseMs=35;p.setParameters(par);
 auto learn=zeros(144000);std::uint32_t rng=72501;for(std::size_t i=0;i<learn[0].size();++i){learn[0][i]=float(.008*noise(rng));learn[1][i]=float(.008*noise(rng));}render(p,learn);
 require(p.hasProfile(),"noise profile missing");auto profile=p.exportProfile();require(profile.frames>100,"insufficient learned frames");
 Profile snapshot=profile;allocations=0;watchAllocation=true;const bool copied=p.copyProfileInto(snapshot);watchAllocation=false;
 require(copied&&allocations==0&&snapshot.density==profile.density&&snapshot.frames==profile.frames,"allocation-free profile snapshot");
 Profile undersized;undersized.frames=777;require(!p.copyProfileInto(undersized)&&undersized.frames==777,"undersized profile snapshot changed state");
 par.learn=false;par.denoise=1;p.setParameters(par);p.reset();auto dry=zeros(192000);
 for(std::size_t i=0;i<dry[0].size();++i)for(int c=0;c<2;++c)dry[c][i]=float(.008*noise(rng)+.045*std::sin(2*pi*3500*i/fs)+.04*std::sin(2*pi*250*i/fs));
 auto wet=dry;render(p,wet);const auto start=std::size_t(fs)+p.latencySamples(),count=std::size_t(2*fs);
 const double reduction=10*std::log10(noiseBand(wet[0],fs,start,count)/noiseBand(dry[0],fs,start-p.latencySamples(),count));
 const double protect=20*std::log10(tone(wet[0],fs,3500,start,count)/tone(dry[0],fs,3500,start-p.latencySamples(),count));
 const double remote=20*std::log10(tone(wet[0],fs,250,start,count)/tone(dry[0],fs,250,start-p.latencySamples(),count));
 std::cout<<"denoise band="<<reduction<<" dB, protected tone="<<protect<<" dB, remote="<<remote<<" dB\n";
 require(reduction<-6,"in-band noise reduction <6dB");require(std::abs(protect)<1,"protected tone loss >1dB");require(std::abs(remote)<.5,"out-of-band change >0.5dB");
 // No-profile control must leave the same fixture neutral, disproving generic gating.
 Processor control;control.prepare(fs,257,2);control.setParameters(par);auto neutral=dry;render(control,neutral);require(peakError(neutral,dry,control.latencySamples())<2e-7,"no-profile negative control altered audio");
 require(control.importProfile(profile),"profile import");control.reset();auto recall=dry;render(control,recall);require(peakError(recall,wet,0)<1e-7,"profile recall changed result");
 auto invalid=profile;invalid.density[0][5]=-1;require(!control.importProfile(invalid),"negative profile accepted");
 invalid=profile;invalid.density[0].pop_back();require(!control.importProfile(invalid),"truncated profile accepted");
 control.clearProfile();require(!control.hasProfile(),"profile clear");
 control.prepare(96000,257,2,4096);require(control.importProfile(profile),"rate-mapped profile rejected");
}
void transient(){
 constexpr double fs=48000;auto dry=zeros(192000);for(std::size_t i=0;i<dry[0].size();++i){const double t=i/fs;
  const double a=(t>=1&&t<1.09)?std::sin(pi*(t-1)/.09):0,b=(t>=2&&t<2.09)?std::sin(pi*(t-2)/.09):0;
  dry[0][i]=dry[1][i]=float(.025*std::sin(2*pi*400*t)+.09*a*std::sin(2*pi*2000*t)+.09*b*std::sin(2*pi*6000*t));}
 Parameters par;par.transient=1;par.lowHz=1500;par.highHz=9000;par.preserveTransient=0;par.sensitivity=1;par.maxReductionDb=24;par.attackMs=.1;par.releaseMs=10;par.transientDecayMs=20;
 par.nodes[0]={true,Shape::Notch,6000,-48,2,12};
 Processor p;p.prepare(fs,257,2,1024);p.setParameters(par);auto wet=dry;render(p,wet);const auto delay=p.latencySamples();
 const auto length=std::size_t(fs*.09);
 auto energy=[&](const Audio&a,std::size_t begin,std::size_t offset,double hz){return tone(a[0],fs,hz,begin+offset,length);};
 const double targeted=20*std::log10(energy(wet,48000,delay,2000)/energy(dry,48000,0,2000));
 const double protectedDb=20*std::log10(energy(wet,96000,delay,6000)/energy(dry,96000,0,6000));
 std::cout<<"transient cut targeted="<<targeted<<" dB, protected="<<protectedDb<<" dB\n";
 require(targeted<-3,"transient weighting didn't cut selected event");require(std::abs(protectedDb)<1.5,"transient weighting damaged protected event");
 par.transient=0;p.setParameters(par);p.reset();auto neutral=dry;render(p,neutral);require(peakError(neutral,dry,delay)<2e-7,"Depth0 weighting must not become outputEQ");
 par.transient=1;par.boost=true;p.setParameters(par);p.reset();auto boost=dry;render(p,boost);
 require(energy(boost,48000,delay,2000)>energy(dry,48000,0,2000)*1.3,"boost did not increase selected event");
 auto scaled=dry;for(auto&c:scaled)for(auto&x:c)x*=.01f;p.reset();render(p,scaled);
 double err=0;for(std::size_t i=0;i<scaled[0].size();++i)err=std::max(err,std::abs(scaled[0][i]*100.-boost[0][i]));
 require(err<.0001,"normalized transient is not level-invariant");
}
void resonanceAndDelta(){
 constexpr double fs=48000;auto dry=zeros(144000);std::uint32_t rng=8123;
 for(std::size_t i=0;i<dry[0].size();++i)dry[0][i]=dry[1][i]=float(.06*std::sin(2*pi*3000*i/fs)+.04*std::sin(2*pi*200*i/fs)+.002*noise(rng));
 Processor p;p.prepare(fs,257,2);Parameters par;par.resonance=1;par.maxReductionDb=12;par.lowHz=1000;par.highHz=6000;par.resonanceThresholdDb=3;p.setParameters(par);
 auto wet=dry;render(p,wet);const auto delay=p.latencySamples(),start=48000+delay,count=std::size_t(48000);
 const double r=20*std::log10(tone(wet[0],fs,3000,start,count)/tone(dry[0],fs,3000,start-delay,count));
 require(r<-6,"resonance target unchanged");
 par.delta=true;p.setParameters(par);p.reset();auto delta=dry;render(p,delta);double error=0;
 for(std::size_t i=delay;i<dry[0].size();++i)error=std::max(error,std::abs(double(wet[0][i])+delta[0][i]-dry[0][i-delay]));require(error<2e-7,"delta decomposition");
 std::cout<<"resonance cut="<<r<<" dB and delta null passed\n";
}
void liveProfileAndStereo(){
 constexpr double fs=48000;Processor p;p.prepare(fs,257,2);Parameters par;par.mode=Mode::Live;par.learn=true;par.lowHz=2000;par.highHz=6000;par.preserveTransient=0;par.attackMs=1;par.releaseMs=50;p.setParameters(par);
 std::uint32_t rng=6785;auto learn=zeros(96000);for(auto&c:learn)for(auto&x:c)x=float(.02*noise(rng));render(p,learn);require(p.hasProfile(),"LIVE learn failed");
 auto profile=p.exportProfile();par.learn=false;par.denoise=1;p.setParameters(par);p.reset();auto dry=zeros(96000);
 for(std::size_t i=0;i<dry[0].size();++i)for(int c=0;c<2;++c)dry[c][i]=float(.02*noise(rng)+.1*std::sin(2*pi*150*i/fs));
 auto wet=dry;render(p,wet);const double r=10*std::log10(noiseBand(wet[0],fs,48000,48000)/noiseBand(dry[0],fs,48000,48000));
 const double remote=20*std::log10(tone(wet[0],fs,150,48000,48000)/tone(dry[0],fs,150,48000,48000));
 std::cout<<"LIVE denoise="<<r<<" dB, remote="<<remote<<" dB\n";require(r<-3,"LIVE selected noise unaffected");require(std::abs(remote)<1.5,"LIVE excessive remote coloration");
 par.routing=Channels::MidSide;p.setParameters(par);require(!p.hasProfile(),"routing changed but invalid profile retained");require(!p.importProfile(profile),"profile in wrong channel basis accepted");
 par.learn=true;p.setParameters(par);p.reset();auto mid=zeros(96000);for(std::size_t i=0;i<mid[0].size();++i)mid[0][i]=mid[1][i]=float(.01*noise(rng));render(p,mid);
 par.learn=false;p.setParameters(par);p.reset();auto identical=dry;identical[1]=identical[0];render(p,identical);
 for(std::size_t i=0;i<identical[0].size();++i)require(identical[0][i]==identical[1][i],"M/S introduced unintended side signal");
 // Log scheduling-inclusive wall time separately from process CPU work. Fresh
 // parallel builds can starve this process, so this pathological-work oracle is
 // not a callback deadline or universal realtime guarantee.
 for(auto mode:{Mode::Pro,Mode::Live}){Processor perf;perf.prepare(192000,257,2);Parameters settings;settings.mode=mode;settings.resonance=.5;settings.transient=.5;perf.setParameters(settings);auto audio=stimulus(192000,192000);
  const auto begin=std::chrono::steady_clock::now();const double cpuStart=cpuSeconds();render(perf,audio);const double cpu=cpuSeconds()-cpuStart;const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
  std::cout<<(mode==Mode::Live?"LIVE":"PRO")<<" 192k stereo 1s fixture wall_seconds="<<elapsed<<" process_cpu_seconds="<<cpu<<"\n";require(cpu>=0&&cpu<2,"pathological CPU work >2 seconds per audio second on local machine");}
}
int main(){try{reconstruction();blockAndRuntime();denoise();transient();resonanceAndDelta();liveProfileAndStereo();std::cout<<"checks="<<checks<<"\nSPECTRAL_AUDIO_TESTS_OK\n";return 0;}catch(const std::exception&e){watchAllocation=false;std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
