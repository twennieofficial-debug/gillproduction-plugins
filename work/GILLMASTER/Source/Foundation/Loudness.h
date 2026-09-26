#pragma once
#include "NextDSPCommon.h"
#include <vector>
namespace gillnext {
// BS.1770 K weighting; EBU Tech 3342 LRA uses 3 s / 100 ms blocks,
// -70 LUFS / -20 LU gates and the 10th..95th percentiles. No certification claim.
class Loudness {
public:
 void prepare(double rate,int channels){fs=std::clamp(detail::finite(rate,48000),8000.,192000.);ch=std::clamp(channels,1,2);window.assign(int(std::round(fs*.4)),0);longWindow.assign(int(std::round(fs*3)),0);hop=std::max(1,int(std::round(fs*.1)));for(int c=0;c<2;++c){shelf[c].c=rateConvert({1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,.73248077421585},fs);hp[c].c=rateConvert({1,-2,1,-1.99004745483398,.99007225036621},fs);}reset();}
 void reset()noexcept{for(auto&f:shelf)f.reset();for(auto&f:hp)f.reset();std::fill(window.begin(),window.end(),0);std::fill(longWindow.begin(),longWindow.end(),0);histPower.fill(0);histCount.fill(0);rangeCount.fill(0);total=longTotal=absolutePower=rangePower=0;count=rangeBlocks=0;clock=position=longPosition=filled=longFilled=0;momentary=integrated=shortTerm=-100;loudnessRange=0;}
 void sample(const std::array<float,2>&x,int channels)noexcept{
  if(window.empty())return;double energy=0;for(int c=0;c<std::min(ch,channels);++c){const double y=hp[c].process(shelf[c].process(detail::input(x[c])));energy+=y*y;}
  total+=energy-window[position];window[position]=energy;if(++position==window.size())position=0;filled=std::min(filled+1,int(window.size()));
  longTotal+=energy-longWindow[longPosition];longWindow[longPosition]=energy;if(++longPosition==longWindow.size())longPosition=0;longFilled=std::min(longFilled+1,int(longWindow.size()));
  if(++clock<hop)return;clock=0;const double e=std::max(0.,total/window.size());momentary=lufs(e);
  if(filled==window.size()&&momentary> -70){const int at=bin(momentary);histPower[at]+=e;++histCount[at];absolutePower+=e;++count;const int from=gateBin(std::max(-70.,lufs(absolutePower/count)-10));double sum=0;std::uint64_t n=0;for(int i=from;i<bins;++i){sum+=histPower[i];n+=histCount[i];}integrated=n?lufs(sum/n):-100;}
  const double le=std::max(0.,longTotal/longWindow.size());shortTerm=lufs(le);
  if(longFilled==longWindow.size()&&shortTerm> -70){++rangeCount[bin(shortTerm)];rangePower+=le;++rangeBlocks;const int from=gateBin(std::max(-70.,lufs(rangePower/rangeBlocks)-20));std::uint64_t n=0;for(int i=from;i<bins;++i)n+=rangeCount[i];if(n){const auto p10=std::uint64_t(std::floor((n-1)*.10)),p95=std::uint64_t(std::floor((n-1)*.95));std::uint64_t seen=0;int lo=from,hi=from;bool haveLo=false;for(int i=from;i<bins;++i){seen+=rangeCount[i];if(!haveLo&&seen>p10){lo=i;haveLo=true;}if(seen>p95){hi=i;break;}}loudnessRange=(hi-lo)*.01;}}
 }
 static double lufs(double e)noexcept{return e>1e-20?std::max(-100.,-.691+10*std::log10(e)):-100;}
 double momentary=-100,integrated=-100,shortTerm=-100,loudnessRange=0;
private:
 static constexpr int bins=14000;
 static int bin(double x)noexcept{return std::clamp(int(std::floor((x+100)*100)),0,bins-1);}
 static int gateBin(double x)noexcept{return std::clamp(int(std::ceil((x+100)*100)),0,bins-1);}
 static detail::Coefficients rateConvert(detail::Coefficients c,double fs)noexcept{const double a=1-fs/48000,b=1+fs/48000;auto polynomial=[&](double p,double q,double r){return std::array<double,3>{p*b*b+q*a*b+r*a*a,2*p*a*b+q*(a*a+b*b)+2*r*a*b,p*a*a+q*a*b+r*b*b};};auto n=polynomial(c.b0,c.b1,c.b2),d=polynomial(1,c.a1,c.a2);return detail::coefficients(n[0],n[1],n[2],d[0],d[1],d[2]);}
 double fs=48000,total=0,longTotal=0,absolutePower=0,rangePower=0;int ch=2,hop=4800,clock=0,position=0,longPosition=0,filled=0,longFilled=0;std::uint64_t count=0,rangeBlocks=0;
 std::array<detail::Biquad,2>shelf,hp;std::vector<double>window,longWindow;std::array<double,bins>histPower{};std::array<std::uint64_t,bins>histCount{},rangeCount{};
};
}
