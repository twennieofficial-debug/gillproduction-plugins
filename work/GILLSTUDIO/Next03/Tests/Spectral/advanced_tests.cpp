#include "../../Source/Spectral/Spectral.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
static bool watch=false;static std::size_t allocations=0;
void* operator new(std::size_t n){if(watch)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void*p)noexcept{::operator delete(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace gillstudio::spectral;
using Audio=std::array<std::vector<float>,2>;
static int checks=0;void require(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
Audio zeros(std::size_t n){return {std::vector<float>(n),std::vector<float>(n)};}
double random(std::uint32_t&r){r^=r<<13;r^=r>>17;r^=r<<5;return double(r)/2147483648.-1;}
Audio source(double fs,double seconds=2){auto a=zeros(std::size_t(fs*seconds));std::uint32_t r=718;
 for(std::size_t i=0;i<a[0].size();++i)for(int c=0;c<2;++c)a[c][i]=float(.035*std::sin(2*pi*3000*i/fs)+.025*std::sin(2*pi*240*i/fs)+.02*random(r));return a;}
Audio keyTone(double fs,std::size_t n,double level,int channel=-1){auto a=zeros(n);for(std::size_t i=0;i<n;++i)for(int c=0;c<2;++c)if(channel<0||channel==c)a[c][i]=float(level*std::sin(2*pi*3000*i/fs));return a;}
void process(Processor&p,Audio&a,const Audio*key=nullptr,std::size_t keyChannels=2,std::size_t block=257){for(std::size_t pos=0;pos<a[0].size();){const auto n=std::min(block,a[0].size()-pos);float*x[]{a[0].data()+pos,a[1].data()+pos};const float*k[]{key?(*key)[0].data()+pos:nullptr,key?(*key)[1].data()+pos:nullptr};p.process(x,n,key?k:nullptr,keyChannels);pos+=n;}}
Audio render(const Audio&src,double fs,Parameters par,const Audio*key=nullptr,std::size_t mainChannels=2,std::size_t keyChannels=2,std::size_t block=257){Processor p;p.prepare(fs,block,mainChannels);p.setParameters(par);p.reset();auto a=src;process(p,a,key,keyChannels,block);return a;}
double difference(const Audio&a,const Audio&b,std::size_t start=0){double d=0;for(int c=0;c<2;++c)for(std::size_t i=start;i<a[c].size();++i)d=std::max(d,std::abs(double(a[c][i])-b[c][i]));return d;}
double tone(const Audio&a,double fs,int channel=0){double x=0,y=0;const auto begin=std::size_t(fs),end=a[0].size();for(auto i=begin;i<end;++i){x+=a[channel][i]*std::cos(2*pi*3000*i/fs);y+=a[channel][i]*std::sin(2*pi*3000*i/fs);}return 2*std::hypot(x,y)/(end-begin);}
Parameters parameters(Mode mode){Parameters p;p.mode=mode;p.externalKey=true;p.resonance=.7;p.maxReductionDb=18;p.resonanceThresholdDb=8;p.attackMs=2;p.releaseMs=35;p.lowHz=2400;p.highHz=3800;p.stereoLink=0;p.preserveTransient=0;return p;}
void detectorFixtures(){for(double fs:{44100.,48000.,96000.})for(auto mode:{Mode::Live,Mode::Pro}){
 auto src=source(fs),silent=zeros(src[0].size()),key=keyTone(fs,src[0].size(),.07);auto p=parameters(mode);
 auto connectedSilence=render(src,fs,p,&silent);auto active=render(src,fs,p,&key);
 p.keyGainDb=-24;auto low=render(src,fs,p,&key);p.keyGainDb=24;auto high=render(src,fs,p,&key);
 const double silentLevel=tone(connectedSilence,fs),activeLevel=tone(active,fs),lowLevel=tone(low,fs),highLevel=tone(high,fs);
 std::cout<<(mode==Mode::Live?"LIVE":"PRO")<<" "<<fs<<" tone ratios silent/low/active/high = 1 / "<<lowLevel/silentLevel<<" / "<<activeLevel/silentLevel<<" / "<<highLevel/silentLevel<<"\n";
 require(activeLevel<silentLevel*.95,"key did not reduce constant main");require(lowLevel>activeLevel*1.04,"key gain low not independently effective");require(highLevel<activeLevel*.99,"key gain high not independently effective");
 p.keyGainDb=0;auto missing=render(src,fs,p);p.externalKey=false;auto disabled=render(src,fs,p,&key),internal=render(src,fs,p);require(difference(missing,internal)==0&&difference(disabled,internal)==0,"missing/disabled key not exact internal fallback");
 p.externalKey=true;auto loud=keyTone(fs,src[0].size(),.9);auto noMain=render(silent,fs,p,&loud);require(difference(noMain,silent)==0,"external key leaked into output");
 auto mono=render(src,fs,p,&key,2,1);require(difference(mono,active)==0,"mono key duplication differs from identical stereo");
 auto onlyLeft=keyTone(fs,src[0].size(),.07,0);auto unlinked=render(src,fs,p,&onlyLeft);require(tone(unlinked,fs,0)<tone(connectedSilence,fs,0)*.95,"left key did not act");require(std::abs(tone(unlinked,fs,1)/tone(connectedSilence,fs,1)-1)<1e-6,"unlinked left key affected right");
 p.stereoLink=1;auto linked=render(src,fs,p,&onlyLeft);require(tone(linked,fs,1)<tone(connectedSilence,fs,1)*.95,"stereo link did not link detector");
 p.stereoLink=0;p.resonanceThresholdDb=40;auto threshold=render(src,fs,p,&key);require(tone(threshold,fs)>activeLevel*1.04,"threshold does not change external detector");
 p.resonance=0;auto neutralKey=render(src,fs,p,&key);require(difference(neutralKey,connectedSilence)<2e-7,"resonance zero changed main under key");
 }}
void routingAndRuntime(){constexpr double fs=48000;auto src=source(fs),key=keyTone(fs,src[0].size(),.035);
 for(auto mode:{Mode::Live,Mode::Pro}){
 auto p=parameters(mode);p.routing=Channels::MidSide;auto a=render(src,fs,p,&key,2,2,17),b=render(src,fs,p,&key,2,1,1024);require(difference(a,b)==0,"M/S mono key and block partition differ");
 auto stereoKey=key;for(auto&x:stereoKey[1])x*=.5f;auto monoKey=key;for(std::size_t i=0;i<key[0].size();++i)monoKey[0][i]=stereoKey[0][i];p.routing=Channels::Stereo;
 a=render(src,fs,p,&stereoKey,1,2);b=render(src,fs,p,&monoKey,1,1);require(difference(a,b)<2e-7,"mono key differs from strongest independently filtered stereo detector lane");
 Processor processor;processor.prepare(fs,257,2);processor.setParameters(p);auto audio=src;allocations=0;watch=true;process(processor,audio,&key);p.keyGainDb=12;processor.setParameters(p);process(processor,audio,&key,1);p.externalKey=false;processor.setParameters(p);process(processor,audio,&key);p.externalKey=true;processor.setParameters(p);process(processor,audio);processor.reset();watch=false;require(allocations==0,"key processing/automation allocated");
 watch=true;void* allocation=::operator new(19);watch=false;::operator delete(allocation);require(allocations>0,"allocation positive control failed");
 // A disconnected key returns to current main detection without resetting the
 // main overlap-add delay or learned profile. Compare after release has settled.
 Processor control;control.prepare(fs,257,2);auto internal=p;internal.externalKey=false;control.setParameters(internal);processor.setParameters(p);processor.reset();auto first=src,ref=src;process(processor,first,&key);process(control,ref);first=src;ref=src;process(processor,first);process(control,ref);require(difference(first,ref,std::size_t(fs))<1e-6,"disconnection retained stale key or reset main audio");
 // Reconnected silent key clears old key energy and relaxes smoothly.
 auto silent=zeros(src[0].size());first=src;process(processor,first,&silent);auto silenceOnly=render(src,fs,p,&silent);require(difference(first,silenceOnly,std::size_t(fs))<1e-6,"reconnection retained stale key");
 }
}
void hostileProfiles(){for(auto mode:{Mode::Live,Mode::Pro}){
 Processor p;p.prepare(48000,257,2);Parameters par;par.mode=mode;par.learn=true;p.setParameters(par);auto audio=source(48000,.1);process(p,audio);auto original=p.exportProfile();
 for(auto count:{std::numeric_limits<std::uint64_t>::max(),Processor::maxProfileUpdates+1}){auto corrupt=original;corrupt.frames=count;require(!p.importProfile(corrupt),"invalid PRO count accepted");corrupt=original;corrupt.liveSamples=count;require(!p.importProfile(corrupt),"invalid LIVE count accepted");}
 auto after=p.exportProfile();require(after.frames==original.frames&&after.liveSamples==original.liveSamples&&after.density==original.density,"invalid profile partly committed");
 auto bounded=original;bounded.frames=bounded.liveSamples=Processor::maxProfileUpdates;require(p.importProfile(bounded),"valid max counters rejected");process(p,audio);after=p.exportProfile();require(after.frames==Processor::maxProfileUpdates&&after.liveSamples==Processor::maxProfileUpdates,"learning counter overflow");for(auto&c:after.density)for(auto v:c)require(std::isfinite(v),"PRO learning profile poisoned");for(auto&c:after.livePower)for(auto v:c)require(std::isfinite(v),"LIVE learning profile poisoned");
 }}
int main(){std::cout.setf(std::ios::unitbuf);try{detectorFixtures();routingAndRuntime();hostileProfiles();std::cout<<checks<<" assertions passed\nSPECTRA_ADVANCED_AUDIO_OK\n";return 0;}catch(const std::exception&e){watch=false;std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
