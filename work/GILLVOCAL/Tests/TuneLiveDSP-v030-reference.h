#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gill_v030 {
// Monophonic, period-aligned dual read heads. The heads are one measured input
// period apart, so a stable voiced cycle adds coherently instead of forming a
// free-running chorus. This is a low-latency alternative, not a formant model.
class TuneLiveDSP {
public:
    void prepare(double rate,int channels,int latency,bool studio=false){fs_=rate;channels_=channels;latency_=latency;taps=studio?64:24;int length=1;while(length<static_cast<int>(rate*.1)+128)length*=2;mask_=length-1;for(auto& b:ring_)b.assign(length,0);coefficients_.resize((phases+1)*taps);
        constexpr double pi=3.14159265358979323846;const double cutoff=studio?.42:.40;
        for(int p=0;p<=phases;++p){const double fraction=static_cast<double>(p)/phases;double sum=0;for(int k=0;k<taps;++k){const double x=k-(taps/2-1)-fraction;const double sinc=std::abs(x)<1e-12?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x);const double window=.42+.5*std::cos(pi*x/(taps*.5))+.08*std::cos(2*pi*x/(taps*.5));const double value=std::abs(x)<taps*.5?sinc*window:0;coefficients_[p*taps+k]=value;sum+=value;}for(int k=0;k<taps;++k)coefficients_[p*taps+k]/=sum;}
        reset();
    }
    void reset()noexcept{for(auto& b:ring_)std::fill(b.begin(),b.end(),0.f);clock_=0;offset_=0;period_=0;}
    void processSample(const std::array<float,2>& input,std::array<float,2>& output,float hz,float semitones,bool render)noexcept{
        for(int c=0;c<channels_;++c)ring_[c][static_cast<int>(clock_)&mask_]=input[c];
        const double wanted=hz>=70&&hz<=1000?fs_/hz:period_>0?period_:fs_/200.;
        // Losing voicing must not jump a read head to an unrelated 200-Hz
        // period while the old vowel is still fading. Large new-note changes
        // move the join gradually; normal vibrato remains below this slew.
        if(!render||period_<=0)period_=wanted;else period_+=std::clamp(wanted-period_,-.05,.05);
        const double period=period_;
        const double ratio=std::exp2(std::clamp(static_cast<double>(semitones),-2.,2.)/12.);
        offset_+=1-ratio;offset_-=std::floor(offset_/period)*period;
        if(render){const double u=offset_/period,w=u*u*(3-2*u);const double a=clock_-latency_-offset_,b=a+period;for(int c=0;c<channels_;++c)output[c]=static_cast<float>(read(c,a)*(1-w)+read(c,b)*w);}
        else {for(int c=0;c<channels_;++c)output[c]=ring_[c][static_cast<int>(clock_-latency_)&mask_];}
        ++clock_;
    }
private:
    double read(int channel,double position)const noexcept{const auto base=static_cast<std::int64_t>(std::floor(position));const double phase=(position-base)*phases;const int p=std::clamp(static_cast<int>(phase),0,phases-1);const double blend=phase-p;const double* a=coefficients_.data()+p*taps;const double* b=a+taps;double result=0;for(int k=0;k<taps;++k)result+=ring_[channel][static_cast<int>(base+k-(taps/2-1))&mask_]*(a[k]+blend*(b[k]-a[k]));return result;}
    static constexpr int phases=512;int taps=24;double fs_=48000,offset_=0,period_=0;int channels_=1,latency_=768,mask_=8191;std::int64_t clock_=0;
    std::array<std::vector<float>,2> ring_;std::vector<double> coefficients_;
};
}
