#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gill {

// Original stereo distance/pan illusion. This is not HRTF or surround rendering.
// The direct path is immediate; reflection/doubler taps are intentional effects.
class StageDSP {
public:
    void prepare(double sampleRate,int maxBlock,int channels) {
        (void)maxBlock;
        prepared_=std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=192000;
        if(!prepared_){reset();return;}
        fs_=sampleRate;channels_=std::clamp(channels,1,2);
        int length=1;while(length<fs_*.1+8)length*=2;
        mask_=length-1;for(auto& v:history_)v.assign(length,0);
        lowpassCoefficient_=1-std::exp(-2*pi*2500/fs_);
        rampLength_=std::max(1,static_cast<int>(std::round(.02*fs_)));
        phaseStep_={2*pi*.63/fs_,2*pi*.83/fs_};
        reset();
    }

    void reset() noexcept {
        for(auto& v:history_)std::fill(v.begin(),v.end(),0.f);
        lowpass_={};phase_={0,2.2};clock_=0;
        current_=target_;step_={};remaining_=0;processed_=false;
    }

    // Thread ownership: configure while stopped; set/process on the audio thread.
    // Neutral: x=0 distance=0 spread=100 doubler=0 mix=100 output=0 mono=false.
    void setParameters(float x,float distance,float spread,float doubler,
                       float mix,float outputDb,bool monoCheck,bool directOn=true) noexcept {
        const double pan=valid(x,-1,1,0),far=valid(distance,0,1,0);
        std::array<double,parameterCount> next{};
        next[panLeft]=pan==0?1:std::sqrt(2.)*std::cos((pan+1)*pi*.25);
        next[panRight]=pan==0?1:std::sqrt(2.)*std::sin((pan+1)*pi*.25);
        if(pan==1)next[panLeft]=0;if(pan==-1)next[panRight]=0;
        // Move the complete stereo source, rather than discarding the input on
        // the opposite side as a balance control would. The image narrows into
        // its mid component at the sides; anti-phase content can cancel in mono.
        next[panNarrow]=1-std::abs(pan);
        next[distanceGain]=std::pow(10.,-12*far/20);
        next[hfBlend]=.75*far;
        next[reflection]=far*.24;
        next[doubling]=valid(doubler,0,100,0)*.01;
        next[width]=valid(spread,0,100,100)*.01;
        next[wetMix]=valid(mix,0,100,100)*.01;
        next[output]=std::pow(10.,valid(outputDb,-24,12,0)/20);
        next[mono]=monoCheck?1:0;
        next[direct]=directOn?1:0;
        if(next==target_)return;
        target_=next;
        if(!processed_){current_=target_;remaining_=0;step_={};}
        else {remaining_=rampLength_;for(int i=0;i<parameterCount;++i)step_[i]=(target_[i]-current_[i])/remaining_;}
    }

    void process(float* const* buffers,int channels,int samples) noexcept {
        if(!prepared_||!buffers||channels<1||samples<=0)return;
        const int active=std::min(channels_,channels);
        for(int c=0;c<active;++c)if(!buffers[c])return;
        processed_=true;
        for(int n=0;n<samples;++n){
            if(remaining_>0){for(int i=0;i<parameterCount;++i)current_[i]+=step_[i];if(--remaining_==0)current_=target_;}
            const float left=safe(buffers[0][n]),right=active==2?safe(buffers[1][n]):left;
            const std::array<float,2> dry{left,right};std::array<double,2> tone{};
            for(int c=0;c<2;++c){
                lowpass_[c]+=lowpassCoefficient_*(dry[c]-lowpass_[c]);
                // Silence cannot leave denormals burning CPU in the filter state.
                if(std::abs(lowpass_[c])<1e-24)lowpass_[c]=0;
                tone[c]=dry[c]+current_[hfBlend]*(lowpass_[c]-dry[c]);
                history_[c][static_cast<int>(clock_)&mask_]=static_cast<float>(tone[c]);
            }
            // Positive, differently timed taps avoid polarity tricks in mono.
            const double earlyL=.60*read(0,.0113*fs_)+.30*read(1,.0237*fs_)+.10*read(0,.0411*fs_);
            const double earlyR=.60*read(1,.0149*fs_)+.30*read(0,.0293*fs_)+.10*read(1,.0479*fs_);
            const double delayL=(.012+.00065*std::sin(phase_[0]))*fs_;
            const double delayR=(.019+.00085*std::sin(phase_[1]))*fs_;
            const double doubleL=.5*(read(0,delayL)+read(1,delayL));
            const double doubleR=.5*(read(0,delayR)+read(1,delayR));
            for(int c=0;c<2;++c){phase_[c]+=phaseStep_[c];if(phase_[c]>=2*pi)phase_[c]-=2*pi;}
            const double amount=current_[doubling],directGain=(1-.18*amount)*current_[direct];
            std::array<double,2> wet{
                current_[distanceGain]*(tone[0]*directGain+.48*amount*doubleL+current_[reflection]*earlyL),
                current_[distanceGain]*(tone[1]*directGain+.48*amount*doubleR+current_[reflection]*earlyR)};
            const double effectiveWidth=current_[width]*(active==2?current_[panNarrow]:1);
            if(effectiveWidth!=1){const double mid=.5*(wet[0]+wet[1]),side=.5*(wet[0]-wet[1])*effectiveWidth;wet={mid+side,mid-side};}
            if(active==2){wet[0]*=current_[panLeft];wet[1]*=current_[panRight];}
            std::array<double,2> result{};
            // DIRECT removes the original source in both signal branches while
            // continuing to feed the delayed doubles and room reflections.
            for(int c=0;c<2;++c){const double original=dry[c]*current_[direct];const double mixed=current_[wetMix]==0?original:current_[wetMix]==1?wet[c]:original+current_[wetMix]*(wet[c]-original);result[c]=mixed*current_[output];}
            const double mid=.5*(result[0]+result[1]);
            if(active==1)buffers[0][n]=bounded(mid);
            else for(int c=0;c<2;++c)buffers[c][n]=bounded(result[c]+current_[mono]*(mid-result[c]));
            ++clock_;
        }
    }

    int latencySamples() const noexcept {return 0;}
    double tailSeconds() const noexcept {return .25;}

private:
    enum Parameter {panLeft,panRight,panNarrow,distanceGain,hfBlend,reflection,doubling,width,wetMix,output,mono,direct,parameterCount};
    static constexpr double pi=3.14159265358979323846;
    static double valid(float value,double low,double high,double fallback) noexcept {return std::isfinite(value)?std::clamp(static_cast<double>(value),low,high):fallback;}
    static float safe(float x) noexcept {return std::isfinite(x)?std::clamp(x,-32.f,32.f):0;}
    static float bounded(double x) noexcept {return std::isfinite(x)?static_cast<float>(std::clamp(x,-32.,32.)):0;}
    double read(int channel,double delay) const noexcept {
        // Cubic interpolation keeps low-rate modulated delay smoother than linear.
        const auto whole=static_cast<std::int64_t>(std::floor(delay));const double f=delay-whole;
        const auto at=clock_-whole;
        const double a=history_[channel][static_cast<int>(at+1)&mask_],b=history_[channel][static_cast<int>(at)&mask_];
        const double c=history_[channel][static_cast<int>(at-1)&mask_],d=history_[channel][static_cast<int>(at-2)&mask_];
        return b+.5*f*(c-a+f*(2*a-5*b+4*c-d+f*(3*(b-c)+d-a)));
    }
    double fs_=48000,lowpassCoefficient_=.25;int channels_=2,mask_=8191,rampLength_=960,remaining_=0;
    std::array<std::vector<float>,2> history_;
    std::array<double,2> lowpass_{},phase_{0,2.2},phaseStep_{};
    std::array<double,parameterCount> target_{1,1,1,1,0,0,0,1,1,1,0,1},current_{target_},step_{};
    std::int64_t clock_=0;bool prepared_=false,processed_=false;
};
} // namespace gill
