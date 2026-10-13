#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>

namespace gillnext { namespace detail {
constexpr double pi=3.1415926535897932384626433832795;
inline double finite(double x,double fallback=0) noexcept{return std::isfinite(x)?x:fallback;}
inline double input(double x) noexcept{return std::clamp(finite(x),-32.,32.);}
inline double quiet(double x) noexcept{return std::abs(x)<1e-30?0:x;}
inline double gain(double db) noexcept{return std::pow(10.,std::clamp(db,-160.,60.)/20.);}
inline double db(double x) noexcept{return 20*std::log10(std::max(1e-8,std::abs(x)));}
inline double alpha(double seconds,double fs) noexcept{return 1-std::exp(-1/(std::max(.000001,seconds)*fs));}
inline void follow(double& x,double target,double a) noexcept{x=quiet(x+a*(target-x));if(std::abs(x-target)<1e-12)x=target;}
inline double softExcess(double x,double knee=6) noexcept{if(x<=-knee*.5)return 0;if(x>=knee*.5)return x;return (x+knee*.5)*(x+knee*.5)/(2*knee);}
struct Meter {
    std::atomic<float> in{0},out{0},inputPeak{0},outputPeak{0};
    double inPower=0,outPower=0,inPeak=0,outPeak=0,a=0,decay=0;
    void prepare(double fs) noexcept{a=alpha(.3,fs);decay=std::exp(-1/(.3*fs));reset();}
    void reset() noexcept{inPower=outPower=inPeak=outPeak=0;publish();}
    void sample(double inputPower,double outputPower,double ip,double op) noexcept{
        follow(inPower,inputPower,a);follow(outPower,outputPower,a);
        inPeak=std::max(ip,quiet(inPeak*decay));outPeak=std::max(op,quiet(outPeak*decay));
    }
    void publish() noexcept{in.store(float(std::sqrt(std::max(0.,inPower))),std::memory_order_relaxed);out.store(float(std::sqrt(std::max(0.,outPower))),std::memory_order_relaxed);inputPeak.store(float(inPeak),std::memory_order_relaxed);outputPeak.store(float(outPeak),std::memory_order_relaxed);}
};
struct Coefficients {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    std::complex<double> response(double frequency,double fs)const noexcept{
        const auto z=std::polar(1.,-2*pi*std::clamp(frequency,0.,fs*.5)/fs);
        return (b0+b1*z+b2*z*z)/(1.+a1*z+a2*z*z);
    }
};
struct Biquad {
    Coefficients c;double s1=0,s2=0;
    double process(double x) noexcept{const double y=c.b0*x+s1;s1=quiet(c.b1*x-c.a1*y+s2);s2=quiet(c.b2*x-c.a2*y);return y;}
    void reset() noexcept{s1=s2=0;}
};
inline Coefficients coefficients(double b0,double b1,double b2,double a0,double a1,double a2) noexcept{return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};}
inline Coefficients peak(double fs,double frequency,double q,double gainDb) noexcept{
    if(std::abs(gainDb)<1e-12)return {};
    const double w=2*pi*std::clamp(frequency,10.,fs*.45)/fs,cs=std::cos(w),a=std::sin(w)/(2*q),A=std::pow(10.,gainDb/40.);
    return coefficients(1+a*A,-2*cs,1-a*A,1+a/A,-2*cs,1-a/A);
}
inline Coefficients bandpass(double fs,double frequency,double q) noexcept{
    const double w=2*pi*std::clamp(frequency,10.,fs*.45)/fs,a=std::sin(w)/(2*q);
    return coefficients(a,0,-a,1+a,-2*std::cos(w),1-a);
}
inline Coefficients shelf(double fs,double frequency,double gainDb,bool high) noexcept{
    if(std::abs(gainDb)<1e-12)return {};
    const double w=2*pi*std::clamp(frequency,10.,fs*.4)/fs,c=std::cos(w),A=std::pow(10.,gainDb/40.);
    const double k=std::sin(w)*std::sqrt(2*A);
    if(high)return coefficients(A*((A+1)+(A-1)*c+k),-2*A*((A-1)+(A+1)*c),A*((A+1)+(A-1)*c-k),(A+1)-(A-1)*c+k,2*((A-1)-(A+1)*c),(A+1)-(A-1)*c-k);
    return coefficients(A*((A+1)-(A-1)*c+k),2*A*((A-1)-(A+1)*c),A*((A+1)-(A-1)*c-k),(A+1)+(A-1)*c+k,-2*((A-1)+(A+1)*c),(A+1)+(A-1)*c-k);
}
} }
