#pragma once
#include "MasterDSP.h"

namespace gill::master {
// Parallel bass harmonics. The high-pass residual removes DC and most of the
// fundamental; the original full-band signal is kept at unity. PRO runs the
// nonlinear stage at 4x with matched 129-tap interpolation/reconstruction.
class WeightDSP {
public:
    struct Parameters { double amount=25,frequency=110;int colour=0; };
    void parameters(Parameters p) noexcept {
        amount.set(std::clamp(math::finite(p.amount),0.,100.)*.01);
        frequency.set(std::clamp(math::finite(p.frequency,110),40.,220.));
        colour.set(std::clamp(p.colour,0,2));
    }
    void prepare(double rate) noexcept {
        fs=std::clamp(math::finite(rate,48000),8000.,192000.);
        amount.prepare(fs);frequency.prepare(fs);colour.prepare(fs);makeFilter();reset();
    }
    void setPro(bool value) noexcept { if(pro!=value){pro=value;reset();} }
    int latencySamples() const noexcept { return pro?32:0; }
    void reset() noexcept {
        amount.reset();frequency.reset();colour.reset();for(auto&f:inputLow)f.reset();for(auto&f:residualLow)f.reset();
        for(auto&h:up)h.fill(0);for(auto&h:down)h.fill(0);for(auto&h:dry)h.fill(0);upPos=downPos=dryPos=0;
    }
    Stereo sample(Stereo x) noexcept {
        const double a=amount.next(),frequencyValue=frequency.next(),character=colour.next();
        const double lowK=LowPass::coefficient(frequencyValue,fs);
        const double harmonicK=LowPass::coefficient(frequencyValue*.9,fs*(pro?4:1));
        Stereo bass{},result{},delayed{};
        for(int c=0;c<2;++c){x[c]=math::input(x[c]);bass[c]=inputLow[c].process(x[c],lowK);delayed[c]=pro?dry[c][dryPos]:x[c];dry[c][dryPos]=x[c];up[c][upPos]=up[c][upPos+history]=bass[c];}
        for(int phase=0;phase<(pro?4:1);++phase){
            for(int c=0;c<2;++c){
                double b=bass[c];if(pro){b=0;for(int n=0;n<history;++n)b+=up[c][upPos+n]*interpolation[phase][n];}
                const double drive=2+6*a,z=b*drive;
                const double odd=(std::tanh(z)-z)/drive;
                const double even=(std::tanh(z+.5)-std::tanh(.5)-z*(1-std::pow(std::tanh(.5),2)))/drive;
                const double evenMix=character<1?character:2-character;
                const double shaped=odd*(1-evenMix)+even*evenMix;
                const double h=shaped-residualLow[c].process(shaped,harmonicK);
                down[c][downPos]=down[c][downPos+taps]=h;
                if(phase==0){if(pro){for(int k=0;k<taps;++k)result[c]+=down[c][downPos+k]*filter[k];}else result[c]=h;}
            }
            if(--downPos<0)downPos=taps-1;
        }
        if(--upPos<0)upPos=history-1;dryPos=(dryPos+1)%32;
        const double gain=a*(1.5+std::clamp(character-1,0.,1.));
        for(int c=0;c<2;++c)result[c]=a==0?delayed[c]:math::quiet(delayed[c]+result[c]*gain);
        return result;
    }
private:
    static constexpr int taps=129,history=33;
    void makeFilter() noexcept {
        double sum=0;for(int k=0;k<taps;++k){const double n=k-64.,angle=2*math::pi*k/(taps-1);const double sinc=n==0?.24:std::sin(math::pi*.24*n)/(math::pi*n);filter[k]=sinc*(.42-.5*std::cos(angle)+.08*std::cos(2*angle));sum+=filter[k];}
        for(auto&v:filter)v/=sum;
        for(int p=0;p<4;++p)for(int n=0;n<history;++n)interpolation[p][n]=p+4*n<taps?4*filter[p+4*n]:0;
    }
    bool pro=true;double fs=48000;Smooth amount,frequency,colour;
    std::array<LowPass,2>inputLow,residualLow;
    std::array<double,taps>filter{};std::array<std::array<double,history>,4>interpolation{};
    std::array<std::array<double,history*2>,2>up{};
    std::array<std::array<double,taps*2>,2>down{};
    std::array<std::array<double,32>,2>dry{};int upPos=0,downPos=0,dryPos=0;
};
}
