#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace gillnext {
// Read-only metering of the FINAL output, including gain match, mono and bypass.
// 8x / 65-tap Blackman-windowed sinc reconstruction is a finite true-peak
// estimate, not a claim of certified/infinite-bandwidth peak detection. It has
// about 32 samples of meter delay; the audio itself is never delayed or changed.
// Call prepare/reset/process from the audio owner. The two getters are safe for
// the GUI thread. peak() uses a 0.5 second exponential amplitude time constant.
class OutputPeakMeter {
public:
    static constexpr int maximumChannels=2, phases=8, taps=65;
    void prepare(double sampleRate,int channels=2) noexcept {
        const double fs=std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=384000?sampleRate:48000;
        channels_=std::clamp(channels,1,maximumChannels);decay_=std::exp(-1/(fs*.5));
        constexpr double pi=3.14159265358979323846;
        for(int phase=0;phase<phases;++phase){
            double sum=0;
            for(int tap=0;tap<taps;++tap){
                const double x=tap-32+phase/double(phases);
                const double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);
                const double window=std::abs(x)<=32?.42+.5*std::cos(pi*x/32)+.08*std::cos(2*pi*x/32):0;
                coefficients_[phase][tap]=sinc*window;sum+=sinc*window;
            }
            for(auto&c:coefficients_[phase])c/=sum;
        }
        // Exact integer phase; avoids infinitesimal residuals for silence.
        coefficients_[0].fill(0);coefficients_[0][32]=1;
        prepared_=true;reset();
    }
    void reset() noexcept {
        for(auto&channel:history_)channel.fill(0);position_=0;current_=maximum_=0;
        peak_.store(0,std::memory_order_relaxed);maximumPeak_.store(0,std::memory_order_relaxed);
    }
    void process(float left,float right,int channels=2) noexcept {
        if(channels<=0)return;
        if(!prepared_)prepare(48000,channels);
        const int count=std::min(channels_,std::min(channels,maximumChannels));
        const std::array<double,2> sample{{finite(left),count==2?finite(right):0}};
        double estimated=0;
        for(int channel=0;channel<maximumChannels;++channel){
            auto&h=history_[channel];h[position_]=h[position_+taps]=sample[channel];
            if(channel>=count)continue;
            // Raw samples are part of the reconstruction and cannot be missed,
            // even before the causal interpolator has filled its history.
            estimated=std::max(estimated,std::abs(sample[channel]));
            const auto*x=h.data()+position_;
            for(const auto&c:coefficients_){
                double a=0,b=0;int tap=0;for(;tap+1<taps;tap+=2){a+=x[tap]*c[tap];b+=x[tap+1]*c[tap+1];}a+=x[tap]*c[tap];
                estimated=std::max(estimated,std::abs(a+b));
            }
        }
        if(--position_<0)position_=taps-1;
        current_=std::max(estimated,current_*decay_);if(current_<1e-30)current_=0;
        maximum_=std::max(maximum_,estimated);
        peak_.store(asFloat(current_),std::memory_order_relaxed);maximumPeak_.store(asFloat(maximum_),std::memory_order_relaxed);
    }
    void process(const float*const*audio,int channels,int frames) noexcept {
        if(!audio||channels<=0||frames<=0||!audio[0])return;
        const int count=std::min(channels_,std::min(channels,maximumChannels));
        for(int i=0;i<frames;++i)process(audio[0][i],count==2&&audio[1]?audio[1][i]:0,count);
    }
    float peak()const noexcept{return peak_.load(std::memory_order_relaxed);}
    float maximumPeak()const noexcept{return maximumPeak_.load(std::memory_order_relaxed);}
private:
    static double finite(float value)noexcept{return std::isfinite(value)?static_cast<double>(value):0;}
    static float asFloat(double value)noexcept{return static_cast<float>(std::min(value,static_cast<double>(std::numeric_limits<float>::max())));}
    std::array<std::array<double,taps>,phases>coefficients_{};
    std::array<std::array<double,taps*2>,maximumChannels>history_{};
    int channels_=2,position_=0;double decay_=1,current_=0,maximum_=0;bool prepared_=false;
    std::atomic<float>peak_{0},maximumPeak_{0};
};
}
