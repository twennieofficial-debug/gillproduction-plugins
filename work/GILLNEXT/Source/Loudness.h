#pragma once
#include "NextDSPCommon.h"
#include <vector>
namespace gillnext {
// Mono/stereo K-weighting and gated 400-ms/100-ms loudness blocks, following
// ITU-R BS.1770-5. Histogram resolution is .01 LU; no EBU certification claimed.
class Loudness {
public:
 void prepare(double rate,int channels){fs=rate;ch=std::clamp(channels,1,2);window.assign(std::max(4,int(std::round(fs*.4))),0);hop=std::max(1,int(std::round(fs*.1)));
  for(int c=0;c<2;++c){shelf[c].c=rateConvert({1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,.73248077421585},fs);hp[c].c=rateConvert({1,-2,1,-1.99004745483398,.99007225036621},fs);}reset();}
 void reset()noexcept{for(auto&f:shelf)f.reset();for(auto&f:hp)f.reset();std::fill(window.begin(),window.end(),0);histPower.fill(0);histCount.fill(0);total=count=clock=position=filled=0;absolutePower=0;momentary=integrated=-100;}
 void sample(const std::array<float,2>&x,int channels)noexcept{if(window.empty())return;double energy=0;for(int c=0;c<std::min(ch,channels);++c){const double y=hp[c].process(shelf[c].process(detail::input(x[c])));energy+=y*y;}total+=energy-window[position];window[position]=energy;if(++position==window.size())position=0;filled=std::min(filled+1,int(window.size()));if(++clock>=hop){clock=0;const double e=std::max(0.,total/window.size());momentary=lufs(e);if(filled==window.size()&&momentary> -70){const int bin=std::clamp(int(std::floor((momentary+100)*100)),0,13999);histPower[bin]+=e;++histCount[bin];absolutePower+=e;++count;const double gate=std::max(-70.,lufs(absolutePower/count)-10);const int from=std::clamp(int(std::ceil((gate+100)*100)),0,13999);double sum=0;std::uint64_t n=0;for(int i=from;i<14000;++i){sum+=histPower[i];n+=histCount[i];}integrated=n?lufs(sum/n):-100;}}}
 static double lufs(double e)noexcept{return e>1e-20?std::max(-100.,-.691+10*std::log10(e)):-100;}
 double momentary=-100,integrated=-100;
private:
 static detail::Coefficients rateConvert(detail::Coefficients c,double fs)noexcept{const double a=1-fs/48000,b=1+fs/48000;auto polynomial=[&](double p,double q,double r){return std::array<double,3>{p*b*b+q*a*b+r*a*a,2*p*a*b+q*(a*a+b*b)+2*r*a*b,p*a*a+q*a*b+r*b*b};};auto n=polynomial(c.b0,c.b1,c.b2),d=polynomial(1,c.a1,c.a2);return detail::coefficients(n[0],n[1],n[2],d[0],d[1],d[2]);}
 double fs=48000,total=0,absolutePower=0;int ch=2,hop=4800,clock=0,position=0,filled=0;std::uint64_t count=0;
 std::array<detail::Biquad,2>shelf,hp;std::vector<double>window;std::array<double,14000>histPower{};std::array<std::uint64_t,14000>histCount{};
};
}
