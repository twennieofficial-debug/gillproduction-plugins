#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Original vocal dynamics designs. Parameter/state mutation belongs to the
// audio thread; meter getters are safe to read from another thread.
namespace gilldyn {
struct VoxParameters { float gateDb=-60, comp=0, outputDb=0; };
struct OptaParameters { float gainDb=0, peakReduction=0; bool limit=false; float hf=0; bool noise=false; };

namespace dynamics_detail {
constexpr double pi=3.1415926535897932384626433832795;
inline double finite(double x,double fallback=0) noexcept { return std::isfinite(x)?x:fallback; }
inline double clean(double x) noexcept { return std::abs(x)<1e-30?0:x; }
inline double input(double x) noexcept { return std::clamp(finite(x),-32.,32.); }
inline double gain(double db) noexcept { return std::pow(10.,std::clamp(db,-160.,40.)/20.); }
inline double db(double amplitude) noexcept { return 20*std::log10(std::max(1e-8,amplitude)); }
inline double coefficient(double seconds,double fs) noexcept { return 1-std::exp(-1/(seconds*fs)); }
inline void follow(double& current,double target,double alpha) noexcept {
    current=clean(current+alpha*(target-current));
    if(std::abs(current-target)<1e-12)current=target;
}
inline double aboveKnee(double excess,double knee) noexcept {
    if(excess<=-knee*.5)return 0;
    if(excess>=knee*.5)return excess;
    const double v=excess+knee*.5;return v*v/(2*knee);
}
struct Meter {
    std::atomic<float> in{0},out{0},reduction{0};
    double inPower=0,outPower=0,alpha=0;
    void prepare(double fs) noexcept { alpha=coefficient(.3,fs);reset(); }
    void reset() noexcept { inPower=outPower=0;in.store(0);out.store(0);reduction.store(0); }
    void sample(double x,double y) noexcept { follow(inPower,x,alpha);follow(outPower,y,alpha); }
    void publish(double gr) noexcept {
        in.store(float(std::sqrt(std::max(0.,inPower))),std::memory_order_relaxed);
        out.store(float(std::sqrt(std::max(0.,outPower))),std::memory_order_relaxed);
        reduction.store(float(std::max(0.,finite(gr))),std::memory_order_relaxed);
    }
};
}

// Downward vocal expansion + soft-knee compression, followed by a linked
// lookahead sample-peak limiter. 0.75 ms total delay, rounded upward per rate.
// Half of that delay lets the gate see consonant onsets before the audio.
class VoxDSP {
public:
    static constexpr int maximumChannels=8, maximumDelay=512;
    static constexpr double ceiling=0.8912509381337456; // -1 dBFS sample peak
    void prepare(double fs,int maximumBlock,int channels) noexcept {
        (void)maximumBlock;
        fs_=std::clamp(dynamics_detail::finite(fs,48000),8000.,384000.);
        channels_=std::clamp(channels,1,maximumChannels);
        latency_=int(std::ceil(fs_*.00075));
        detectorDelay_=latency_/2;limiterDelay_=latency_-detectorDelay_;
        parameterAlpha_=dynamics_detail::coefficient(.015,fs_);
        rmsAttack_=dynamics_detail::coefficient(.003,fs_);
        rmsRelease_=rmsAttack_;
        peakRelease_=std::exp(-1/(.015*fs_));
        gateOpen_=dynamics_detail::coefficient(.00006,fs_);
        gateClose_=dynamics_detail::coefficient(.05,fs_);
        compressionAttack_=dynamics_detail::coefficient(.003,fs_);
        compressionRelease_=dynamics_detail::coefficient(.1,fs_);
        limiterAttack_=dynamics_detail::coefficient(.00006,fs_);
        limiterRelease_=dynamics_detail::coefficient(.075,fs_);
        holdSamples_=int(std::ceil(.06*fs_));
        meter_.prepare(fs_);prepared_=true;reset();
    }
    void setParameters(const VoxParameters& p) noexcept {
        targetGateDb_=std::clamp(dynamics_detail::finite(p.gateDb,-60),-90.,-10.);
        targetGateActive_=targetGateDb_<=-90?0:1;
        targetComp_=std::clamp(dynamics_detail::finite(p.comp),0.,100.)*.01;
        targetOutput_=dynamics_detail::gain(std::clamp(dynamics_detail::finite(p.outputDb),-24.,12.));
    }
    void reset() noexcept {
        for(auto& c:channelsState_){c.detectorDelay.fill(0);c.limiterDelay.fill(0);}
        detectorPosition_=limiterPosition_=queueHead_=queueCount_=0;
        sampleClock_=0;gateHold_=0;detectorPower_=detectorPeak_=gateReduction_=fullCompression_=0;
        limiterGain_=1;lastReduction_=0;
        gateDb_=targetGateDb_;gateActive_=targetGateActive_;
        comp_=targetComp_;output_=targetOutput_;
        meter_.reset();gateMeter_.store(0,std::memory_order_relaxed);
    }
    void setLiveMode(bool live) noexcept { if (liveMode_ != live) {liveMode_=live;reset();} }
    int latencySamples() const noexcept { return liveMode_ ? 0 : latency_; }
    float reductionDb() const noexcept { return meter_.reduction.load(std::memory_order_relaxed); }
    float gateReductionDb() const noexcept { return gateMeter_.load(std::memory_order_relaxed); }
    float inputRms() const noexcept { return meter_.in.load(std::memory_order_relaxed); }
    float outputRms() const noexcept { return meter_.out.load(std::memory_order_relaxed); }

    void process(float* const* buffers,int frames,int channels) noexcept {
        if(!buffers||frames<=0||channels<=0)return;
        if(!prepared_)prepare(48000,frames,channels);
        const int count=std::min({channels,channels_,maximumChannels});
        for(int i=0;i<frames;++i){
            dynamics_detail::follow(gateDb_,targetGateDb_,parameterAlpha_);
            dynamics_detail::follow(gateActive_,targetGateActive_,parameterAlpha_);
            dynamics_detail::follow(comp_,targetComp_,parameterAlpha_);
            dynamics_detail::follow(output_,targetOutput_,parameterAlpha_);
            std::array<double,maximumChannels> raw{},delayed{},candidate{},limitedInput{};
            double linkedPower=0,inputPower=0;
            for(int ch=0;ch<count;++ch){
                raw[ch]=buffers[ch]?dynamics_detail::input(buffers[ch][i]):0;
                const double power=raw[ch]*raw[ch];inputPower+=power;linkedPower=std::max(linkedPower,power);
                auto& s=channelsState_[ch];
                delayed[ch]=liveMode_?raw[ch]:s.detectorDelay[detectorPosition_];
                s.detectorDelay[detectorPosition_]=raw[ch];
            }
            dynamics_detail::follow(detectorPower_,linkedPower,linkedPower>detectorPower_?rmsAttack_:rmsRelease_);
            detectorPeak_=std::max(std::sqrt(linkedPower),dynamics_detail::clean(detectorPeak_*peakRelease_));
            const double detectedDb=dynamics_detail::db(std::sqrt(std::max(0.,detectorPower_)));
            const double gateDetectedDb=dynamics_detail::db(detectorPeak_);
            if(gateDetectedDb>=gateDb_)gateHold_=holdSamples_;
            else if(gateHold_>0)--gateHold_;
            const double gateTarget=gateHold_>0?0:std::clamp((gateDb_-gateDetectedDb)*1.5,0.,60.);
            dynamics_detail::follow(gateReduction_,gateTarget,gateTarget<gateReduction_?gateOpen_:gateClose_);
            const double expansionGain=1-gateActive_+gateActive_*dynamics_detail::gain(-gateReduction_);
            // A fixed knee with a continuous strength control avoids a long,
            // inactive knob region followed by a suddenly moving threshold.
            const double targetReduction=std::min(36.,.9*dynamics_detail::aboveKnee(detectedDb+28.,12.));
            dynamics_detail::follow(fullCompression_,targetReduction,
                targetReduction>fullCompression_?compressionAttack_:compressionRelease_);
            const double compression=comp_*fullCompression_;
            // Fixed reference makeup: 65% of reduction at a -18 dB detector
            // reference. This is intentionally not a loudness-matching AGC.
            const double preLimiterGain=expansionGain*dynamics_detail::gain(5.85*comp_-compression)*output_;
            double candidatePeak=0,delayedPeak=0;
            for(int ch=0;ch<count;++ch){
                candidate[ch]=delayed[ch]*preLimiterGain;
                auto& s=channelsState_[ch];
                limitedInput[ch]=liveMode_?candidate[ch]:s.limiterDelay[limiterPosition_];
                s.limiterDelay[limiterPosition_]=candidate[ch];
                candidatePeak=std::max(candidatePeak,std::abs(candidate[ch]));
                delayedPeak=std::max(delayedPeak,std::abs(limitedInput[ch]));
            }
            // O(1)-amortized maximum over the complete lookahead window.
            while(queueCount_&&peakIndex_[queueHead_]+std::uint64_t(liveMode_?0:limiterDelay_)<sampleClock_){
                queueHead_=(queueHead_+1)%maximumDelay;--queueCount_;
            }
            while(queueCount_){
                const int back=(queueHead_+queueCount_-1)%maximumDelay;
                if(peakValue_[back]>candidatePeak)break;
                --queueCount_;
            }
            const int tail=(queueHead_+queueCount_)%maximumDelay;
            peakValue_[tail]=candidatePeak;peakIndex_[tail]=sampleClock_;++queueCount_;
            const double desired=std::min(1.,ceiling/std::max(1e-20,peakValue_[queueHead_]));
            dynamics_detail::follow(limiterGain_,desired,desired<limiterGain_?limiterAttack_:limiterRelease_);
            // The lookahead attack normally reaches its target before the peak.
            // This linked guard also covers a single pathological sample exactly.
            const double applied=std::min(limiterGain_,ceiling/std::max(1e-20,delayedPeak));
            double outputPower=0;
            for(int ch=0;ch<count;++ch){
                const double y=dynamics_detail::clean(limitedInput[ch]*applied);
                if(buffers[ch])buffers[ch][i]=float(y);
                outputPower+=y*y;
            }
            lastReduction_=compression-dynamics_detail::db(applied);
            meter_.sample(inputPower/count,outputPower/count);
            detectorPosition_=(detectorPosition_+1)%detectorDelay_;
            limiterPosition_=(limiterPosition_+1)%limiterDelay_;
            ++sampleClock_;
        }
        meter_.publish(lastReduction_);
        gateMeter_.store(float(-dynamics_detail::db(1-gateActive_+gateActive_*dynamics_detail::gain(-gateReduction_))),std::memory_order_relaxed);
    }
private:
    bool liveMode_=false;
    struct ChannelState { std::array<double,maximumDelay> detectorDelay{},limiterDelay{}; };
    std::array<ChannelState,maximumChannels> channelsState_{};
    std::array<double,maximumDelay> peakValue_{};
    std::array<std::uint64_t,maximumDelay> peakIndex_{};
    dynamics_detail::Meter meter_;
    std::atomic<float> gateMeter_{0};
    double fs_=48000,parameterAlpha_=0,rmsAttack_=0,rmsRelease_=0,peakRelease_=0;
    double gateOpen_=0,gateClose_=0,compressionAttack_=0,compressionRelease_=0;
    double limiterAttack_=0,limiterRelease_=0;
    double detectorPower_=0,detectorPeak_=0,gateReduction_=0,fullCompression_=0,limiterGain_=1,lastReduction_=0;
    double gateDb_=-60,targetGateDb_=-60,gateActive_=1,targetGateActive_=1;
    double comp_=0,targetComp_=0,output_=1,targetOutput_=1;
    int channels_=2,latency_=36,detectorDelay_=18,limiterDelay_=18,detectorPosition_=0,limiterPosition_=0;
    int holdSamples_=2880,gateHold_=0,queueHead_=0,queueCount_=0;
    std::uint64_t sampleClock_=0;
    bool prepared_=false;
};

// Original program-dependent leveler. COMP and LIMIT are compression slopes,
// not a brickwall output limiter; there is no lookahead or added latency.
// Manual GAIN remains independent of PEAK REDUCTION.
class OptaDSP {
public:
    static constexpr int maximumChannels=8;
    void prepare(double fs,int maximumBlock,int channels) noexcept {
        (void)maximumBlock;
        fs_=std::clamp(dynamics_detail::finite(fs,48000),8000.,384000.);
        channels_=std::clamp(channels,1,maximumChannels);
        parameterAlpha_=dynamics_detail::coefficient(.015,fs_);
        rmsAttack_=dynamics_detail::coefficient(.008,fs_);
        rmsRelease_=rmsAttack_;
        fastAttackComp_=dynamics_detail::coefficient(.01,fs_);
        fastAttackLimit_=dynamics_detail::coefficient(.003,fs_);
        fastRelease_=dynamics_detail::coefficient(.065,fs_);
        slowAttack_=dynamics_detail::coefficient(.15,fs_);
        slowRelease_=dynamics_detail::coefficient(1.2,fs_);
        hfAlpha_=1-std::exp(-2*dynamics_detail::pi*1500/fs_);
        noisePhaseIncrement_=2*dynamics_detail::pi*50/fs_;
        meter_.prepare(fs_);prepared_=true;reset();
    }
    void setParameters(const OptaParameters& p) noexcept {
        targetGain_=dynamics_detail::gain(std::clamp(dynamics_detail::finite(p.gainDb),-20.,20.));
        targetAmount_=std::clamp(dynamics_detail::finite(p.peakReduction),0.,100.)*.01;
        targetLimit_=p.limit?1:0;
        targetHf_=std::clamp(dynamics_detail::finite(p.hf),0.,100.)*.01;
        targetNoise_=p.noise?1:0;
    }
    void reset() noexcept {
        hfLow_.fill(0);detectorPower_=fastReduction_=slowReduction_=lastReduction_=noisePhase_=0;
        for(int ch=0;ch<maximumChannels;++ch)noiseState_[ch]=std::uint32_t(1931+ch*1117);
        gain_=targetGain_;amount_=targetAmount_;limit_=targetLimit_;hf_=targetHf_;noise_=targetNoise_;
        meter_.reset();
    }
    int latencySamples() const noexcept { return 0; }
    float reductionDb() const noexcept { return meter_.reduction.load(std::memory_order_relaxed); }
    float inputRms() const noexcept { return meter_.in.load(std::memory_order_relaxed); }
    float outputRms() const noexcept { return meter_.out.load(std::memory_order_relaxed); }
    void process(float* const* buffers,int frames,int channels) noexcept {
        if(!buffers||frames<=0||channels<=0)return;
        if(!prepared_)prepare(48000,frames,channels);
        const int count=std::min({channels,channels_,maximumChannels});
        for(int i=0;i<frames;++i){
            dynamics_detail::follow(gain_,targetGain_,parameterAlpha_);
            dynamics_detail::follow(amount_,targetAmount_,parameterAlpha_);
            dynamics_detail::follow(limit_,targetLimit_,parameterAlpha_);
            dynamics_detail::follow(hf_,targetHf_,parameterAlpha_);
            dynamics_detail::follow(noise_,targetNoise_,parameterAlpha_);
            std::array<double,maximumChannels> raw{};
            double linkedPower=0,inputPower=0;
            for(int ch=0;ch<count;++ch){
                raw[ch]=buffers[ch]?dynamics_detail::input(buffers[ch][i]):0;
                inputPower+=raw[ch]*raw[ch];
                dynamics_detail::follow(hfLow_[ch],raw[ch],hfAlpha_);
                const double detector=raw[ch]+2*hf_*(raw[ch]-hfLow_[ch]);
                linkedPower=std::max(linkedPower,detector*detector);
            }
            dynamics_detail::follow(detectorPower_,linkedPower,linkedPower>detectorPower_?rmsAttack_:rmsRelease_);
            const double detectedDb=dynamics_detail::db(std::sqrt(std::max(0.,detectorPower_)));
            const double targetReduction=std::min(48.,(.75+.19*limit_)*dynamics_detail::aboveKnee(detectedDb+28,10));
            const double fastAttack=fastAttackComp_+limit_*(fastAttackLimit_-fastAttackComp_);
            dynamics_detail::follow(fastReduction_,targetReduction,targetReduction>fastReduction_?fastAttack:fastRelease_);
            dynamics_detail::follow(slowReduction_,targetReduction,targetReduction>slowReduction_?slowAttack_:slowRelease_);
            // Short phrases build little slow memory. Sustained material builds
            // a slower 45% recovery tail after the initial fast release.
            lastReduction_=amount_*std::max(fastReduction_,.45*slowReduction_);
            const double multiplier=gain_*dynamics_detail::gain(-lastReduction_);
            const double hum=noise_>0?std::sin(noisePhase_)*.000003*noise_:0;
            double outputPower=0;
            for(int ch=0;ch<count;++ch){
                double added=0;
                if(noise_>0){
                    auto& state=noiseState_[ch];state^=state<<13;state^=state>>17;state^=state<<5;
                    added=(double(state)/4294967295.-.5)*.000032*noise_+hum;
                }
                const double y=dynamics_detail::clean(std::clamp(raw[ch]*multiplier+added,-32.,32.));
                if(buffers[ch])buffers[ch][i]=float(y);
                outputPower+=y*y;
            }
            noisePhase_+=noisePhaseIncrement_;if(noisePhase_>=2*dynamics_detail::pi)noisePhase_-=2*dynamics_detail::pi;
            meter_.sample(inputPower/count,outputPower/count);
        }
        meter_.publish(lastReduction_);
    }
private:
    dynamics_detail::Meter meter_;
    std::array<double,maximumChannels> hfLow_{};
    std::array<std::uint32_t,maximumChannels> noiseState_{};
    double fs_=48000,parameterAlpha_=0,rmsAttack_=0,rmsRelease_=0;
    double fastAttackComp_=0,fastAttackLimit_=0,fastRelease_=0,slowAttack_=0,slowRelease_=0,hfAlpha_=0;
    double detectorPower_=0,fastReduction_=0,slowReduction_=0,lastReduction_=0;
    double gain_=1,targetGain_=1,amount_=0,targetAmount_=0,limit_=0,targetLimit_=0,hf_=0,targetHf_=0,noise_=0,targetNoise_=0;
    double noisePhase_=0,noisePhaseIncrement_=0;
    int channels_=2;
    bool prepared_=false;
};
} // namespace gilldyn
