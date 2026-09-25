#include "../Source/TuneDSP.h"
#define gill gill_v020
#include "TuneDSP-v020-reference.h"
#undef gill
#include <chrono>
#include <cstdio>
#include <complex>
#include <random>
#include <vector>
constexpr double pi=3.14159265358979323846;
std::vector<float> source(double hz,double seconds,bool vowel=true){std::vector<float> x(static_cast<size_t>(48000*seconds));for(size_t i=0;i<x.size();++i){double v=0;for(int h=1;h<=(vowel?20:1);++h){double f=h*hz;if(f>21600)break;double a=vowel?.16+2*std::exp(-std::pow((f-700)/220,2))+1.4*std::exp(-std::pow((f-1200)/300,2))+.8*std::exp(-std::pow((f-2500)/450,2)):1;v+=a/h*std::sin(2*pi*f*i/48000.);}x[i]=static_cast<float>(v*.16);}return x;}
template<class D>std::vector<float> render(D& dsp,const std::vector<float>& x,int block){auto y=x;for(int at=0;at<static_cast<int>(x.size());at+=block){float* p[]{y.data()+at};dsp.process(p,1,std::min(block,static_cast<int>(x.size())-at));}return y;}
double periodicResidual(const std::vector<float>& y,double f){const int n=24000,at=static_cast<int>(y.size())-n;double energy=0,projected=0;for(int i=0;i<n;++i)energy+=y[at+i]*y[at+i];for(int h=1;h*f<20000&&h<=40;++h){std::complex<double> sum{};for(int i=0;i<n;++i)sum+=static_cast<double>(y[at+i])*std::polar(1.,-2*pi*h*f*i/48000.);projected+=2*std::norm(sum)/n;}return 10*std::log10(std::max(1e-14,energy-projected)/std::max(energy,1e-14));}
double energy(const std::vector<float>&x,int start){double e=0;for(size_t i=start;i<x.size();++i)e+=x[i]*x[i];return e/(x.size()-start);}
int main(int argc,char** argv){const int quality=argc>1?std::atoi(argv[1]):0;for(double target:{110.,220.,440.})for(double detune:{0.,1.,15.,40.}){const double hz=target*std::exp2(detune/1200.);const auto x=source(hz,1.5);gill_v020::TuneDSP before;before.prepare(48000,128,1);before.setParameters(0,0,0,0,100);auto a=render(before,x,128);gill::TuneDSP after;after.setQualityMode(quality);after.prepare(48000,128,1);after.setParameters(0,0,0,0,100);auto b=render(after,x,128);double nullA=0,nullB=0,dryEnergy=0;for(size_t i=48000;i<x.size();++i){const double d=x[i-before.latencySamples()];dryEnergy+=d*d;nullA+=std::pow(a[i]-d,2);const double e=x[i-after.latencySamples()];nullB+=std::pow(b[i]-e,2);}std::printf("ARTIFACT target %.0f detune %.0f baseline_periodic_residual %.3f new %.3f baseline_unity_null %.3f new %.3f level_before %.3f after %.3f dB\n",target,detune,periodicResidual(a,target),periodicResidual(b,target),10*std::log10(std::max(1e-14,nullA)/dryEnergy),10*std::log10(std::max(1e-14,nullB)/dryEnergy),10*std::log10(energy(a,48000)/energy(x,48000)),10*std::log10(energy(b,48000)/energy(x,48000)));std::fflush(stdout);}
    auto x=source(267.5,1);gill_v020::TuneDSP a,b;a.prepare(48000,1,1);b.prepare(48000,127,1);a.setParameters(0,0,0,0,100);b.setParameters(0,0,0,0,100);auto x1=render(a,x,1),x2=render(b,x,127);double err=0,power=0;for(size_t i=12000;i<x.size();++i){err+=std::pow(x1[i]-x2[i],2);power+=x1[i]*x1[i];}std::printf("BASELINE BLOCK DEPENDENCE error %.3f dB relative\n",10*std::log10(err/power));return 0;
}
