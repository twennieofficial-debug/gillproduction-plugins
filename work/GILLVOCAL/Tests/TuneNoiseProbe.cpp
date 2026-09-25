#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <random>
#include <type_traits>
#define private public
#include "TuneDSP-v030-reference.h"
#include "../Source/TuneDSP.h"
#undef private
constexpr double pi=3.14159265358979323846,fs=48000;
template<class D>void probe(const char*label,int mode,int scenario,double noiseLevel){
 D d;d.setQualityMode(mode);d.prepare(fs,127,1);d.setParameters(0,0,0,0,100);
 std::mt19937 rng(8723);std::normal_distribution<float> noise(0,1);std::vector<float>x(96000),y(x.size());double phase=0,lp=0;int switches=0,voiceSwitch=0;bool last=false,lastVoice=false;double jitter=0,shiftLast=0,unvoicedErr=0,unvoicedPower=0;int count=0;double maxStep=0;
 for(int i=0;i<(int)x.size();++i){double t=i/fs;double hz=scenario==2?220*std::exp2((.5+.4*std::sin(2*pi*5*t))/1200):230;phase+=2*pi*hz/fs;double v=.14*std::sin(phase)+.09*std::sin(phase*2)+.05*std::sin(phase*3);double n=noise(rng);lp+=.015*(n-lp);double envelope=1;
 if(scenario==1)envelope=t<.6?1:t<.8?(1-(t-.6)/.2):t<1.2?0:1;
 if(scenario==3)envelope=((i/2400)%4)==3?0:1;
 x[i]=(float)(v*envelope+noiseLevel*(scenario==4?lp*10:n));y[i]=x[i];float*p[]{y.data()+i};d.process(p,1,1);
 if(i>24000){const auto gate=d.controlAt(d.sampleClock_-1-d.latencySamples());bool corr;if constexpr(std::is_same_v<D,gill::TuneDSP>)corr=d.correctionActive_;else corr=gate.voice>.5&&std::abs(gate.shift)>.005;switches+=corr!=last;last=corr;voiceSwitch+=(gate.voice>.5)!=lastVoice;lastVoice=gate.voice>.5;jitter+=std::abs(gate.shift-shiftLast);shiftLast=gate.shift;if(i>0)maxStep=std::max(maxStep,std::abs(double(y[i]-y[i-1])));}
 const int j=i-d.latencySamples();if(j>=0&&scenario==1&&j>=39000&&j<57000){unvoicedErr+=std::pow(y[i]-x[j],2);unvoicedPower+=x[j]*x[j];++count;}
 }
 std::printf("%s q%d sc%d noise%.4f correctionSwitch%d voiceSwitch%d shiftVariation%.4f maxStep%.5f noiseNull%.2f\n",label,mode,scenario,noiseLevel,switches,voiceSwitch,jitter,maxStep,10*std::log10(std::max(1e-30,unvoicedErr)/std::max(1e-30,unvoicedPower)));
}
int main(){for(int q:{0,1})for(int sc:{0,1,2,3,4})for(double noise:{0.,.005,.02,.05,.1}){probe<gill_v030::TuneDSP>("OLD",q,sc,noise);probe<gill::TuneDSP>("NEW",q,sc,noise);} }
