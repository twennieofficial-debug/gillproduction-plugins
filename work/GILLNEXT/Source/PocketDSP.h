#pragma once
#include "NextDSPCommon.h"

namespace gillnext {
struct PocketParameters {float amount=50,maxCutDb=6,focusLowHz=180,focusHighHz=8000,speed=50;};
class PocketDSP {
public:
    static constexpr int bandCount=12;
    void prepare(double fs,int maxBlock,int channels) noexcept{
        (void)maxBlock;fs_=std::clamp(detail::finite(fs,48000),8000.,192000.);rateMeter_=fs_;channels_=std::clamp(channels,1,8);
        smooth_=detail::alpha(.03,fs_);detectorAlpha_=detail::alpha(.008,fs_);meter_.prepare(fs_);
        for(int b=0;b<bandCount;++b){centers_[b]=80*std::pow(std::min(16000.,fs_*.42)/80.,b/double(bandCount-1));centerMeters_[b]=float(centers_[b]);for(auto& channel:detectors_)channel[b].c=detail::bandpass(fs_,centers_[b],1.6);}
        prepared_=true;reset();
    }
    void setParameters(const PocketParameters& p) noexcept{
        amountTarget_=std::clamp(detail::finite(p.amount,50),0.,100.)*.01;
        cutTarget_=std::clamp(detail::finite(p.maxCutDb,6),0.,18.);
        lowTarget_=std::clamp(detail::finite(p.focusLowHz,180),80.,4000.);
        highTarget_=std::clamp(detail::finite(p.focusHighHz,8000),1000.,16000.);
        if(highTarget_<lowTarget_*1.25)highTarget_=lowTarget_*1.25;
        speedTarget_=std::clamp(detail::finite(p.speed,50),0.,100.)*.01;
    }
    void reset() noexcept{
        for(auto& channel:detectors_)for(auto& f:channel)f.reset();for(auto& channel:filters_)for(auto& f:channel){f.c={};f.reset();}
        bandPower_.fill(0);cuts_.fill(0);for(auto& x:cutMeters_)x=0;
        amount_=amountTarget_;cut_=cutTarget_;low_=lowTarget_;high_=highTarget_;speed_=speedTarget_;
        scPower_=0;controlClock_=0;detectorChannels_=0;activeMeter_=0;reductionMeter_=0;meter_.reset();
    }
    void process(float*const* audio,int channels,int frames,const float*const* sidechain=nullptr,int sidechainChannels=0) noexcept{
        if(!audio||channels<=0||frames<=0)return;if(!prepared_)prepare(48000,frames,channels);
        const int count=std::min({channels,channels_,8}),scCount=sidechain?std::clamp(sidechainChannels,0,8):0;
        detectorChannels_=std::max(detectorChannels_,scCount);
        for(int i=0;i<frames;++i){
            detail::follow(amount_,amountTarget_,smooth_);detail::follow(cut_,cutTarget_,smooth_);detail::follow(low_,lowTarget_,smooth_);detail::follow(high_,highTarget_,smooth_);detail::follow(speed_,speedTarget_,smooth_);
            double scPower=0;std::array<double,bandCount> energy{};
            for(int c=0;c<detectorChannels_;++c){const double x=c<scCount&&sidechain[c]?detail::input(sidechain[c][i]):0;if(c<scCount)scPower+=x*x/scCount;for(int b=0;b<bandCount;++b){const double y=detectors_[c][b].process(x);if(c<scCount)energy[b]+=y*y/scCount;}}
            detail::follow(scPower_,scPower,detectorAlpha_);for(int b=0;b<bandCount;++b)detail::follow(bandPower_[b],energy[b],detectorAlpha_);
            if(controlClock_++==0){
                const double scDb=detail::db(std::sqrt(std::max(0.,scPower_)));
                const double active=scCount>0?std::clamp((scDb+55)/20.,0.,1.):0;
                activeMeter_=float(active);std::array<double,bandCount> weights{};double total=0;
                for(int b=0;b<bandCount;++b){
                    const double lo=std::clamp(1+2*std::log2(centers_[b]/low_),0.,1.);
                    const double hi=std::clamp(1+2*std::log2(high_/centers_[b]),0.,1.);
                    weights[b]=lo*hi*std::max(0.,bandPower_[b]-1e-10);total+=weights[b];
                }
                double sumCuts=0;
                for(int b=0;b<bandCount;++b){
                    // A finite shared dB budget bounds the cascade's worst cut,
                    // while dominant vocal bands receive most of that budget.
                    const double wanted=total>1e-14?active*amount_*cut_*weights[b]/total:0;
                    const double seconds=wanted>cuts_[b]?(.045-.039*speed_):(.32-.22*speed_);
                    detail::follow(cuts_[b],wanted,detail::alpha(seconds,fs_/16));
                    if(cuts_[b]<1e-7)cuts_[b]=0;
                    cutMeters_[b]=float(cuts_[b]);sumCuts+=cuts_[b];
                    const auto coefficients=detail::peak(fs_,centers_[b],1.6,-cuts_[b]);
                    for(int c=0;c<count;++c)filters_[c][b].c=coefficients;
                }
                reductionMeter_=float(sumCuts);
            }
            if(controlClock_>=16)controlClock_=0;
            double inputPower=0,outputPower=0,ip=0,op=0;
            for(int c=0;c<count;++c){const double x=audio[c]?detail::input(audio[c][i]):0;double y=x;for(auto& filter:filters_[c])y=filter.process(y);if(audio[c])audio[c][i]=float(y);inputPower+=x*x/count;outputPower+=y*y/count;ip=std::max(ip,std::abs(x));op=std::max(op,std::abs(y));}
            meter_.sample(inputPower,outputPower,ip,op);
        }
        meter_.publish();
        double maximumCut=0;for(int b=0;b<bandCount;++b)maximumCut=std::max(maximumCut,-double(responseDb(float(centers_[b]))));
        reductionMeter_=float(maximumCut);
    }
    int latencySamples()const noexcept{return 0;}double tailSeconds()const noexcept{return .1;}
    float gainReductionDb()const noexcept{return reductionMeter_.load(std::memory_order_relaxed);}
    float inputRms()const noexcept{return meter_.in.load(std::memory_order_relaxed);}float outputRms()const noexcept{return meter_.out.load(std::memory_order_relaxed);}
    bool sidechainActive()const noexcept{return activeMeter_.load(std::memory_order_relaxed)>.01f;}
    float sidechainActivity()const noexcept{return activeMeter_.load(std::memory_order_relaxed);}
    std::array<float,bandCount> bandCutsDb()const noexcept{std::array<float,bandCount> r{};for(int b=0;b<bandCount;++b)r[b]=cutMeters_[b].load(std::memory_order_relaxed);return r;}
    std::array<float,bandCount> bandFrequenciesHz()const noexcept{std::array<float,bandCount> r{};for(int b=0;b<bandCount;++b)r[b]=centerMeters_[b].load(std::memory_order_relaxed);return r;}
    float responseDb(float frequency)const noexcept{
        const double fs=rateMeter_.load(std::memory_order_relaxed);double result=0;
        for(int b=0;b<bandCount;++b)result+=detail::db(std::abs(detail::peak(fs,centerMeters_[b].load(std::memory_order_relaxed),1.6,-cutMeters_[b].load(std::memory_order_relaxed)).response(frequency,fs)));
        return float(result);
    }
private:
    detail::Meter meter_;std::atomic<double>rateMeter_{48000};std::atomic<float>activeMeter_{0},reductionMeter_{0};
    std::array<std::atomic<float>,bandCount>cutMeters_{},centerMeters_{};
    std::array<std::array<detail::Biquad,bandCount>,8>detectors_{},filters_{};
    std::array<double,bandCount>centers_{},bandPower_{},cuts_{};
    double fs_=48000,smooth_=0,detectorAlpha_=0,scPower_=0;
    double amountTarget_=.5,cutTarget_=6,lowTarget_=180,highTarget_=8000,speedTarget_=.5;
    double amount_=.5,cut_=6,low_=180,high_=8000,speed_=.5;
    int channels_=2,controlClock_=0,detectorChannels_=0;bool prepared_=false;
};
}
