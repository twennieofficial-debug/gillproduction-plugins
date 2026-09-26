#pragma once
#include "Foundation/NextDSPCommon.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace gill::master {
namespace math = gillnext::detail;
using Stereo = std::array<double,2>;

struct Smooth {
    double current=0,target=0,coefficient=.001;
    void prepare(double rate,double seconds=.015) noexcept { coefficient=math::alpha(seconds,rate);current=target; }
    void set(double x) noexcept { target=math::finite(x); }
    void reset() noexcept { current=target; }
    double next() noexcept { math::follow(current,target,coefficient);return current; }
};

// Matched TPT one-poles remain stable while their cutoff is automated.
// The three bands are complementary residuals: their sum is the input,
// including phase, rather than three independently summed crossover filters.
struct LowPass {
    double s1=0,s2=0;
    double process(double x,double g) noexcept {
        const double v1=(x-s1)*g,y1=v1+s1;s1=math::quiet(y1+v1);
        const double v2=(y1-s2)*g,y2=v2+s2;s2=math::quiet(y2+v2);
        return y2;
    }
    void reset() noexcept { s1=s2=0; }
    static double coefficient(double frequency,double rate) noexcept {
        const double t=std::tan(math::pi*std::clamp(frequency,10.,rate*.42)/rate);
        return t/(1+t);
    }
};

struct Splitter {
    std::array<LowPass,2> lo,hi;
    void reset() noexcept { for(auto& f:lo)f.reset();for(auto& f:hi)f.reset(); }
    std::array<Stereo,3> process(Stereo input,double lowCoefficient,double highCoefficient) noexcept {
        std::array<Stereo,3> bands{};
        for(int c=0;c<2;++c){bands[0][c]=lo[c].process(input[c],lowCoefficient);const double belowHigh=hi[c].process(input[c],highCoefficient);bands[2][c]=input[c]-belowHigh;bands[1][c]=input[c]-bands[0][c]-bands[2][c];}
        return bands;
    }
};

struct Correlation {
    double l=0,r=0,lr=0,a=.001;
    void prepare(double rate) noexcept { a=math::alpha(.3,rate);reset(); }
    void reset() noexcept { l=r=lr=0; }
    void sample(Stereo x) noexcept { math::follow(l,x[0]*x[0],a);math::follow(r,x[1]*x[1],a);math::follow(lr,x[0]*x[1],a); }
    double value() const noexcept { return l*r>1e-20?std::clamp(lr/std::sqrt(l*r),-1.,1.):0; }
};

class LowDSP {
public:
    struct Parameters { double amount=30,frequency=140,threshold=-18,protect=60,width=100; };
    void parameters(Parameters p) noexcept {
        amount.set(std::clamp(math::finite(p.amount),0.,100.)*.01);frequency.set(std::clamp(math::finite(p.frequency,140),40.,300.));
        threshold.set(std::clamp(math::finite(p.threshold,-18),-48.,0.));protect.set(std::clamp(math::finite(p.protect,60),0.,100.)*.01);width.set(std::clamp(math::finite(p.width,100),0.,100.)*.01);
    }
    void prepare(double rate) noexcept { fs=std::clamp(math::finite(rate,48000),8000.,192000.);for(auto* s:{&amount,&frequency,&threshold,&protect,&width})s->prepare(fs);attack=math::alpha(.008,fs);release=math::alpha(.15,fs);rmsAlpha=math::alpha(.02,fs);fastAlpha=math::alpha(.002,fs);slowAlpha=math::alpha(.06,fs);reset(); }
    void reset() noexcept { for(auto& f:filters)f.reset();for(auto* s:{&amount,&frequency,&threshold,&protect,&width})s->reset();power=fast=slow=reduction=0; }
    Stereo sample(Stereo x) noexcept {
        const double a=amount.next(),f=frequency.next(),th=threshold.next(),pr=protect.next(),w=width.next(),k=LowPass::coefficient(f,fs);
        Stereo low{};double energy=0;
        for(int c=0;c<2;++c){x[c]=math::input(x[c]);low[c]=filters[c].process(x[c],k);energy=std::max(energy,low[c]*low[c]);}
        math::follow(power,energy,rmsAlpha);math::follow(fast,energy,fastAlpha);math::follow(slow,energy,slowAlpha);
        const double transient=std::clamp((std::sqrt(std::max(0.,fast))-std::sqrt(std::max(0.,slow)))/(std::sqrt(std::max(0.,fast))+1e-7),0.,1.);
        const double wanted=a*.8*math::softExcess(math::db(std::sqrt(std::max(0.,power)))-th,6)*(1-pr*transient);
        math::follow(reduction,wanted,wanted>reduction?attack:release);
        const double gain=math::gain(-reduction),mid=.5*(low[0]+low[1]),side=.5*(low[0]-low[1])*w;
        if(gain==1&&w==1)return x;
        return {x[0]+(mid+side)*gain-low[0],x[1]+(mid-side)*gain-low[1]};
    }
    double reductionDb() const noexcept { return reduction; }
private:
    Smooth amount,frequency,threshold,protect,width;std::array<LowPass,2>filters;
    double fs=48000,attack=0,release=0,rmsAlpha=0,fastAlpha=0,slowAlpha=0,power=0,fast=0,slow=0,reduction=0;
};

class GlueDSP {
public:
    struct Parameters { double amount=30,detectorHz=90;int character=0;double attackMs=30,releaseMs=180; };
    void parameters(Parameters p) noexcept { amount.set(std::clamp(math::finite(p.amount),0.,100.)*.01);cutoff.set(std::clamp(math::finite(p.detectorHz,90),20.,300.));character=std::clamp(p.character,0,2);attackSeconds=std::clamp(math::finite(p.attackMs,30),1.,100.)*.001;releaseSeconds=std::clamp(math::finite(p.releaseMs,180),30.,1000.)*.001;updateTimes(); }
    void prepare(double rate) noexcept { fs=std::clamp(math::finite(rate,48000),8000.,192000.);amount.prepare(fs);cutoff.prepare(fs);detectorAlpha=math::alpha(.008,fs);updateTimes();reset(); }
    void reset() noexcept { for(auto& f:detector)f.reset();amount.reset();cutoff.reset();power=reduction=0; }
    Stereo sample(Stereo x) noexcept {
        const double a=amount.next(),k=LowPass::coefficient(cutoff.next(),fs);double e=0;
        for(int c=0;c<2;++c){x[c]=math::input(x[c]);const double hp=x[c]-detector[c].process(x[c],k);e=std::max(e,hp*hp);}
        math::follow(power,e,detectorAlpha);
        const double ratio=character==0?2.:character==1?2.5:4.;
        const double wanted=a*(1-1/ratio)*math::softExcess(math::db(std::sqrt(std::max(0.,power)))-(-9-18*a),8);
        math::follow(reduction,wanted,wanted>reduction?attacks[character]:releases[character]);
        const double g=math::gain(-reduction);return{x[0]*g,x[1]*g};
    }
    double reductionDb() const noexcept { return reduction; }
private:
    void updateTimes() noexcept { for(int i=0;i<3;++i){attacks[i]=math::alpha(attackSeconds*(i==1?1.25:i==2?.65:1),fs);releases[i]=math::alpha(releaseSeconds*(i==1?.7:i==2?1.25:1),fs);} }
    Smooth amount,cutoff;std::array<LowPass,2>detector;std::array<double,3>attacks{},releases{};
    double fs=48000,detectorAlpha=0,power=0,reduction=0,attackSeconds=.030,releaseSeconds=.180;int character=0;
};

class WidthDSP {
public:
    struct Parameters { std::array<double,3>width{100,100,100};double lowHz=160,highHz=4000;bool guard=true; };
    void parameters(Parameters p) noexcept { for(int i=0;i<3;++i)width[i].set(std::clamp(math::finite(p.width[i],100),0.,i==0?150.:200.)*.01);low.set(std::clamp(math::finite(p.lowHz,160),60.,500.));high.set(std::clamp(math::finite(p.highHz,4000),1500.,12000.));guard=p.guard; }
    void prepare(double rate) noexcept { fs=std::clamp(math::finite(rate,48000),8000.,192000.);for(auto&s:width)s.prepare(fs);low.prepare(fs);high.prepare(fs);correlation.prepare(fs);guardAlpha=math::alpha(.08,fs);reset(); }
    void reset() noexcept { split.reset();correlation.reset();for(auto&s:width)s.reset();low.reset();high.reset();protection=1; }
    Stereo sample(Stereo x) noexcept {
        for(auto&v:x)v=math::input(v);auto bands=split.process(x,LowPass::coefficient(low.next(),fs),LowPass::coefficient(high.next(),fs));Stereo out{};
        const double wanted=guard&&correlation.value()<-.15?.7:1.;math::follow(protection,wanted,guardAlpha);
        bool neutral=true;for(int i=0;i<3;++i){double w=width[i].next();neutral=neutral&&w==1;if(w>1)w=1+(w-1)*protection;const double m=.5*(bands[i][0]+bands[i][1]),s=.5*(bands[i][0]-bands[i][1])*w;out[0]+=m+s;out[1]+=m-s;}if(neutral)out=x;
        correlation.sample(out);return out;
    }
    double correlationValue() const noexcept { return correlation.value(); }
private:
    std::array<Smooth,3>width;Smooth low,high;Splitter split;Correlation correlation;double fs=48000,protection=1,guardAlpha=0;bool guard=true;
};

class PunchDSP {
public:
    struct Parameters { std::array<double,3>attack{0,0,0};double sustain=0,lowHz=180,highHz=3500; };
    void parameters(Parameters p) noexcept { for(int i=0;i<3;++i)attack[i].set(std::clamp(math::finite(p.attack[i]),-100.,100.)*.01);sustain.set(std::clamp(math::finite(p.sustain),-100.,100.)*.01);low.set(std::clamp(math::finite(p.lowHz,180),60.,500.));high.set(std::clamp(math::finite(p.highHz,3500),1500.,12000.)); }
    void prepare(double rate) noexcept { fs=std::clamp(math::finite(rate,48000),8000.,192000.);for(auto&s:attack)s.prepare(fs);for(auto*s:{&sustain,&low,&high})s->prepare(fs);fastAlpha=math::alpha(.001,fs);slowAlpha=math::alpha(.025,fs);gainAlpha=math::alpha(.002,fs);reset(); }
    void reset() noexcept { split.reset();for(auto&s:attack)s.reset();for(auto*s:{&sustain,&low,&high})s->reset();fast={};slow={};bandGain={}; }
    Stereo sample(Stereo x) noexcept {
        for(auto&v:x)v=math::input(v);auto bands=split.process(x,LowPass::coefficient(low.next(),fs),LowPass::coefficient(high.next(),fs));const double sustainAmount=sustain.next();Stereo out{};
        for(int i=0;i<3;++i){const double e=std::max(bands[i][0]*bands[i][0],bands[i][1]*bands[i][1]);math::follow(fast[i],e,fastAlpha);math::follow(slow[i],e,slowAlpha);const double ratio=std::clamp((fast[i]-slow[i])/(fast[i]+slow[i]+1e-12),-1.,1.);const double transient=std::max(0.,ratio),tail=std::max(0.,-ratio);const double wanted=6*(attack[i].next()*transient+sustainAmount*tail);math::follow(bandGain[i],wanted,gainAlpha);const double g=math::gain(bandGain[i]);for(int c=0;c<2;++c)out[c]+=bands[i][c]*g;}
        if(bandGain[0]==0&&bandGain[1]==0&&bandGain[2]==0)return x;return out;
    }
    std::array<double,3> gainDb() const noexcept { return bandGain; }
private:
    std::array<Smooth,3>attack;Smooth sustain,low,high;Splitter split;std::array<double,3>fast{},slow{},bandGain{};
    double fs=48000,fastAlpha=0,slowAlpha=0,gainAlpha=0;
};
} // namespace gill::master
