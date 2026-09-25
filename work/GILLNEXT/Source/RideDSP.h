#pragma once
#include "NextDSPCommon.h"

namespace gillnext {
struct RideParameters {float targetDb=-18,rangeDownDb=12,rangeUpDb=9,speed=50;bool hold=false;};
struct RideHistory {std::array<float,128> inputDb{},outputDb{},gainDb{};};

// Original phrase-aware vocal fader, not a compressor or noise suppressor.
// All processing/state mutation belongs to the audio thread. Getters use atomics.
class RideDSP {
public:
    void prepare(double fs,int maxBlock,int channels) noexcept{
        (void)maxBlock;fs_=std::clamp(detail::finite(fs,48000),8000.,192000.);channels_=std::clamp(channels,1,8);
        smooth_=detail::alpha(.025,fs_);shortAlpha_=detail::alpha(.012,fs_);
        activityOpen_=detail::alpha(.003,fs_);activityClose_=detail::alpha(.025,fs_);
        historyInterval_=std::max(1,int(fs_*.05));meter_.prepare(fs_);prepared_=true;reset();
    }
    void setParameters(const RideParameters& p) noexcept{
        target_=std::clamp(detail::finite(p.targetDb,-18),-36.,-6.);
        downTarget_=std::clamp(detail::finite(p.rangeDownDb),0.,18.);upTarget_=std::clamp(detail::finite(p.rangeUpDb),0.,18.);
        speedTarget_=std::clamp(detail::finite(p.speed,50),0.,100.)*.01;hold_=p.hold;
    }
    void reset() noexcept{
        targetDb_=target_;down_=downTarget_;up_=upTarget_;speed_=speedTarget_;
        shortPower_=phrasePower_=activity_=faderDb_=appliedDb_=0;historyClock_=0;historyWrite_=0;
        for(size_t i=0;i<128;++i){historyIn_[i]=-160;historyOut_[i]=-160;historyGain_[i]=0;}
        meter_.reset();gainMeter_=activityMeter_=0;
    }
    void process(float*const* audio,int channels,int frames,const float*const* sidechain=nullptr,int sidechainChannels=0) noexcept{
        (void)sidechain;(void)sidechainChannels;if(!audio||channels<=0||frames<=0)return;
        if(!prepared_)prepare(48000,frames,channels);
        const int count=std::min({channels,channels_,8});
        for(int i=0;i<frames;++i){
            detail::follow(targetDb_,target_,smooth_);detail::follow(down_,downTarget_,smooth_);detail::follow(up_,upTarget_,smooth_);detail::follow(speed_,speedTarget_,smooth_);
            double power=0,peak=0;std::array<double,8> x{};
            for(int c=0;c<count;++c){x[c]=audio[c]?detail::input(audio[c][i]):0;power+=x[c]*x[c]/count;peak=std::max(peak,std::abs(x[c]));}
            detail::follow(shortPower_,power,shortAlpha_);
            const double shortDb=detail::db(std::sqrt(std::max(0.,shortPower_)));
            const double activeTarget=std::clamp((shortDb+58)/10.,0.,1.);
            detail::follow(activity_,activeTarget,activeTarget>activity_?activityOpen_:activityClose_);
            const bool speech=shortDb>-53;
            // Stop adapting below the speech floor: breath/noise tails must not
            // be interpreted as quiet words that need ever more amplification.
            if(speech){
                if(phrasePower_<1e-10)phrasePower_=shortPower_;
                const double phraseSeconds=.45-.32*speed_;
                detail::follow(phrasePower_,shortPower_,detail::alpha(phraseSeconds,fs_));
                if(!hold_){
                    const double delta=targetDb_-detail::db(std::sqrt(std::max(1e-16,phrasePower_)));
                    const double outside=std::copysign(std::max(0.,std::abs(delta)-1.),delta);
                    const double wanted=std::clamp(.9*outside,-down_,up_);
                    // A fader follows phrases more slowly than waveform dynamics.
                    const double seconds=(wanted<faderDb_?.20:.65)*(1-.75*speed_);
                    detail::follow(faderDb_,wanted,detail::alpha(seconds,fs_));
                }
            }
            faderDb_=std::clamp(faderDb_,-down_,up_);
            // Exact unity boost on sufficiently quiet tails; attenuation may remain.
            const double boostActivity=shortDb<=-58?0:activity_;
            appliedDb_=faderDb_>0?faderDb_*boostActivity:faderDb_;
            const double g=detail::gain(appliedDb_);double outputPower=0,outputPeak=0;
            for(int c=0;c<count;++c){const double y=x[c]*g;if(audio[c])audio[c][i]=float(y);outputPower+=y*y/count;outputPeak=std::max(outputPeak,std::abs(y));}
            meter_.sample(power,outputPower,peak,outputPeak);
            if(++historyClock_>=historyInterval_){historyClock_=0;const int slot=historyWrite_.load(std::memory_order_relaxed);historyIn_[slot]=float(shortDb);historyOut_[slot]=float(shortDb+appliedDb_);historyGain_[slot]=float(appliedDb_);historyWrite_.store((slot+1)%128,std::memory_order_release);}
        }
        meter_.publish();gainMeter_.store(float(appliedDb_),std::memory_order_relaxed);activityMeter_.store(float(activity_),std::memory_order_relaxed);
    }
    int latencySamples()const noexcept{return 0;}double tailSeconds()const noexcept{return 0;}
    float gainReductionDb()const noexcept{return std::max(0.f,-gainDb());}
    float gainDb()const noexcept{return gainMeter_.load(std::memory_order_relaxed);}
    float activity()const noexcept{return activityMeter_.load(std::memory_order_relaxed);}
    float inputRms()const noexcept{return meter_.in.load(std::memory_order_relaxed);}
    float outputRms()const noexcept{return meter_.out.load(std::memory_order_relaxed);}
    RideHistory history()const noexcept{
        RideHistory h;const int start=historyWrite_.load(std::memory_order_acquire);
        for(int i=0;i<128;++i){const int j=(start+i)%128;h.inputDb[i]=historyIn_[j].load(std::memory_order_relaxed);h.outputDb[i]=historyOut_[j].load(std::memory_order_relaxed);h.gainDb[i]=historyGain_[j].load(std::memory_order_relaxed);}return h;
    }
private:
    detail::Meter meter_;std::atomic<float>gainMeter_{0},activityMeter_{0};
    std::array<std::atomic<float>,128>historyIn_{},historyOut_{},historyGain_{};std::atomic<int>historyWrite_{0};
    double fs_=48000,smooth_=0,shortAlpha_=0,activityOpen_=0,activityClose_=0;
    double target_=-18,downTarget_=12,upTarget_=9,speedTarget_=.5,targetDb_=-18,down_=12,up_=9,speed_=.5;
    double shortPower_=0,phrasePower_=0,activity_=0,faderDb_=0,appliedDb_=0;
    int channels_=2,historyClock_=0,historyInterval_=2400;bool hold_=false,prepared_=false;
};
}
