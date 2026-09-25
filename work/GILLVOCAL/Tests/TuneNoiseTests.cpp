// Original deterministic synthetic voices/noise. v0.3 is frozen in reference
// headers, not reimplemented as an oracle. No user audio or listening claim.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <random>
#include <type_traits>
#include <complex>
#include <filesystem>
#include <fstream>
// Test-only observation of actual routing; output-level assertions remain primary.
#define private public
#include "TuneDSP-v030-reference.h"
#include "../Source/TuneDSP.h"
#undef private
constexpr double pi=3.14159265358979323846,fs=48000;
int checks=0,failures=0;
void check(bool ok,const char* name,double a=0,double b=0){++checks;if(!ok)++failures;std::printf("%s %s current %.9g baseline %.9g\n",ok?"PASS":"FAIL",name,a,b);}
struct Result{std::vector<float> y;int latency=0,switches=0,voiceSwitches=0;double voiceFraction=0;};
template<class D>Result render(const std::vector<float>& x,int mode,int block=1){D d;d.setQualityMode(mode);d.prepare(fs,block,1);d.setParameters(9,2,0,0,100);Result r{x,d.latencySamples()};bool prev=false,prevVoice=false;int voicedCount=0,count=0;
 for(int at=0;at<(int)x.size();at+=block){float*p[]{r.y.data()+at};d.process(p,1,std::min(block,int(x.size())-at));if(block==1&&at>24000){const auto gate=d.controlAt(d.sampleClock_-1-r.latency);bool active;
 if constexpr(std::is_same_v<D,gill::TuneDSP>)active=d.correctionActive_;else active=gate.voice>.5f&&std::abs(gate.shift)>.005f;
 r.switches+=active!=prev;prev=active;const bool voice=gate.voice>.5f;r.voiceSwitches+=voice!=prevVoice;prevVoice=voice;voicedCount+=voice;++count;}}
 r.voiceFraction=count?double(voicedCount)/count:0;return r;}
std::vector<float> signal(int type,double noiseAmp=0,int gap=0){std::vector<float>x(96000);std::mt19937 rng(8723);std::normal_distribution<float> random(0,1);double phase=0,lowNoise=0;
 for(int i=0;i<(int)x.size();++i){const double t=i/fs;double hz=230;
 if(type==1)hz=220*std::exp2(1./1200);
 if(type==2)hz=220;
 if(type==3)hz=220*std::exp2((20+30*std::sin(2*pi*5*t))/1200);
 if(type==4)hz=t<1?230:272;
 if(type==5)hz=82.40688923*std::exp2((20+25*std::sin(2*pi*5*t))/1200);
 if(type==6)hz=220*std::exp2((.5+.4*std::sin(2*pi*5*t))/1200);
 if(type==10)hz=t<1?230:220;
 phase+=2*pi*hz/fs;double v=.14*std::sin(phase)+.108*std::sin(phase*2)+.072*std::sin(phase*3)+.052*std::sin(phase*4)+.028*std::sin(phase*5);
 double n=random(rng);lowNoise+=.015*(n-lowNoise);
 if(type==7){const double gain=t<.6?1:t<.8?1-(t-.6)/.2:t<1.2?0:1;v*=gain;}
 if(type==8)n=lowNoise*10;
 if(type==9){const int phaseInChunk=i%9600;if(phaseInChunk>=7200&&phaseInChunk<7200+gap){v=0;}else n=0;}
 x[i]=float(v+noiseAmp*n);
 }return x;}
double nullError(const Result&r,const std::vector<float>&x,int a,int b){double e=0;for(int i=a;i<b;++i)e=std::max(e,std::abs(double(r.y[i+r.latency]-x[i])));return e;}
double difference(const Result&a,const Result&b,int start=24000){double e=0;for(int i=start;i<(int)a.y.size();++i)e=std::max(e,std::abs(double(a.y[i]-b.y[i])));return e;}
double residual(const Result&r,double hz){const int start=48000,len=48000;double energy=0,projected=0;for(int i=0;i<len;++i)energy+=r.y[start+i]*r.y[start+i];for(int h=1;h<=30&&h*hz<20000;++h){std::complex<double>s{};for(int i=0;i<len;++i)s+=double(r.y[start+i])*std::polar(1.,-2*pi*h*hz*i/fs);projected+=2*std::norm(s)/len;}return 10*std::log10(std::max(1e-15,energy-projected)/energy);}
double pitchRms(const Result&r,double hz){double squares=0;int count=0;const int lo=int(fs/hz*.9),hi=int(fs/hz*1.1),len=1536;std::vector<double>d(hi+2);for(int start=24000+r.latency;start+len+hi<(int)r.y.size();start+=480){std::fill(d.begin(),d.end(),0);int best=lo;for(int lag=lo;lag<=hi;++lag){for(int i=0;i<len;++i){double delta=r.y[start+i]-r.y[start+i+lag];d[lag]+=delta*delta;}if(d[lag]<d[best])best=lag;}if(best==lo||best==hi)return 999;const double f=fs/(best+.5*(d[best-1]-d[best+1])/(d[best-1]-2*d[best]+d[best+1]));const double cents=1200*std::log2(f/hz);squares+=cents*cents;++count;}return std::sqrt(squares/count);}
double highBandEnvelopeCV(const Result&r){double lp=0,sum=0,sum2=0,window=0;int count=0;const double alpha=1-std::exp(-2*pi*4000/fs);for(int i=0;i<(int)r.y.size();++i){lp+=alpha*(r.y[i]-lp);const double hi=r.y[i]-lp;window+=hi*hi;if(i%480==479){if(i>=24000){const double rms=std::sqrt(window/480);sum+=rms;sum2+=rms*rms;++count;}window=0;}}const double mean=sum/count;return std::sqrt(std::max(0.,sum2/count-mean*mean))/mean;}
void wav(const std::string&name,const std::vector<float>&samples,int start,int length){std::filesystem::create_directories("fixtures-v040");std::ofstream f("fixtures-v040/"+name+".wav",std::ios::binary);auto u16=[&](uint16_t v){f.put(char(v));f.put(char(v>>8));};auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};f.write("RIFF",4);u32(36+length*4);f.write("WAVEfmt ",8);u32(16);u16(3);u16(1);u32(48000);u32(192000);u16(4);u16(32);f.write("data",4);u32(length*4);f.write(reinterpret_cast<const char*>(samples.data()+start),length*4);}
int main(){for(int q:{0,1}){std::printf("MODE %s\n",q?"LIVE":"STUDIO");
 for(int type:{0,1,2,3,4,5}){const auto x=signal(type);const auto old=render<gill_v030::TuneDSP>(x,q),now=render<gill::TuneDSP>(x,q);std::printf("CLEAN type%d maxDelta %.9g\n",type,difference(now,old));check(now.latency==old.latency,"fixed reported latency preserved",now.latency,old.latency);
 if(type<=2||type==4)check(difference(now,old)==0,"clean steady and hard note-step audio unchanged sample-exact",difference(now,old));
 if(type==3||type==5){double a=pitchRms(now,type==5?82.40688923:220),b=pitchRms(old,type==5?82.40688923:220);check(a<=b,"clean vibrato pitch error does not worsen",a,b);}
 if(type==0){double a=residual(now,220),b=residual(old,220);check(a<=b+1e-8,"steady harmonic residual not worsened",a,b);}
 }
 for(double n:{0.,.005,.02,.05,.1}){const auto x=signal(6,n);const auto old=render<gill_v030::TuneDSP>(x,q),now=render<gill::TuneDSP>(x,q);std::printf("NEAR_UNITY noise%.4f oldSwitch%d newSwitch%d oldCV%.8f newCV%.8f\n",n,old.switches,now.switches,highBandEnvelopeCV(old),highBandEnvelopeCV(now));check(now.switches<=1,"near-unity correction does not chatter",now.switches,old.switches);check(now.voiceFraction>.99,"near-unity fix retains the voiced signal",now.voiceFraction,old.voiceFraction);if(n>=.005)check(highBandEnvelopeCV(now)<highBandEnvelopeCV(old),"measured high-band envelope variation is reduced",highBandEnvelopeCV(now),highBandEnvelopeCV(old));}
 for(double n:{.005,.02,.05,.1}){const auto x=signal(7,n);const auto old=render<gill_v030::TuneDSP>(x,q),now=render<gill::TuneDSP>(x,q);check(nullError(now,x,39000,57000)==0,"word-tail noise returns to exact dry without a noise gate",nullError(now,x,39000,57000));check(now.voiceSwitches<=old.voiceSwitches,"word-tail voicing chatter does not worsen",now.voiceSwitches,old.voiceSwitches);}
 for(double n:{.05,.1}){const auto x=signal(8,n);const auto old=render<gill_v030::TuneDSP>(x,q),now=render<gill::TuneDSP>(x,q);if(n==.1)check(now.voiceSwitches<old.voiceSwitches,"strong low-frequency noise produces fewer false voicing restarts",now.voiceSwitches,old.voiceSwitches);else check(now.voiceSwitches<=old.voiceSwitches,"moderate low-frequency noise does not produce extra restarts",now.voiceSwitches,old.voiceSwitches);}
 for(int ms:{10,20,40}){const int gap=ms*48;const auto x=signal(9,.08,gap);const auto old=render<gill_v030::TuneDSP>(x,q),now=render<gill::TuneDSP>(x,q);double a=0,b=0;for(int start=7200;start+gap+now.latency<(int)x.size();start+=9600){for(int i=start;i<start+gap;++i){a+=std::pow(now.y[i+now.latency]-x[i],2);b+=std::pow(old.y[i+old.latency]-x[i],2);}}std::printf("BREATH gap%dms currentError%.9g oldError%.9g\n",ms,a,b);check(a<=b,"short unvoiced segment alteration does not increase",a,b);}
 {const auto x=signal(10);const auto now=render<gill::TuneDSP>(x,q);check(nullError(now,x,62400,90000)==0,"settled target note returns to exact original after prior correction",nullError(now,x,62400,90000));}
 for(double n:{.005,.02}){const auto x=signal(0,n);const auto old=render<gill_v030::TuneDSP>(x,q),now=render<gill::TuneDSP>(x,q);check(difference(now,old)==0,"confident detuned vocal with background noise keeps existing correction",difference(now,old));}
 const auto x=signal(6,.02);const auto one=render<gill::TuneDSP>(x,q,1);for(int block:{127,4096}){const auto many=render<gill::TuneDSP>(x,q,block);check(one.y==many.y,"noisy near-unity audio is sample-exact across blocks");}
 }
 for(int type:{0,6,7}){const auto x=signal(type,type==0?0:.02);const std::string name=type==0?"01-clean-voice":type==6?"02-near-unity-noise":"03-word-tail-noise";const int length=93600;wav(name+"-input",x,0,length);const auto old=render<gill_v030::TuneDSP>(x,0),studio=render<gill::TuneDSP>(x,0),live=render<gill::TuneDSP>(x,1);wav(name+"-v030",old.y,old.latency,length);wav(name+"-v040-studio",studio.y,studio.latency,length);wav(name+"-v040-live",live.y,live.latency,length);}
 std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}
