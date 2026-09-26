#pragma once
#include "NextDSPCommon.h"

namespace gillnext {
struct FinishParameters {
    float releaseMs=90,ceilingDb=-1,driveDb=0,comp=0,clip=0,width=100,bassMonoHz=80,lowDb=0,midDb=0,highDb=0;
    bool toneEnabled=true,compEnabled=true,clipEnabled=true,stereoEnabled=true,limiterEnabled=true;
};

// Original stereo master chain: tone -> width/bass mono -> bus compression ->
// drive -> 8x soft clip/linked lookahead limiter -> FIR reconstruction.
// True-peak is estimated with finite 8x interpolation, not an infinite-bandwidth
// guarantee. Tests independently reconstruct at 16x; no LUFS/artist matching.
class FinishDSP {
public:
    static constexpr int maximumChannels=8,oversamplingFactor=8,filterTaps=257;
    static constexpr int filterLatency=32,maximumLookahead=8192,maximumBaseDelay=1024;
    void prepare(double fs,int maxBlock,int channels) noexcept{
        (void)maxBlock;fs_=std::clamp(detail::finite(fs,48000),8000.,192000.);rateMeter_=fs_;channels_=std::clamp(channels,1,maximumChannels);
        lookaheadBase_=std::max(1,int(std::ceil(fs_*.003)));lookahead_=lookaheadBase_*oversamplingFactor;latency_=lookaheadBase_+filterLatency;
        smoothing_=detail::alpha(.02,fs_);compressorDetect_=detail::alpha(.02,fs_);compressorAttack_=detail::alpha(.03,fs_);compressorRelease_=detail::alpha(.18,fs_);
        limiterAttack_=detail::alpha(.00004,fs_*(liveMode_?1:oversamplingFactor));limiterRelease_=detail::alpha(releaseSeconds_,fs_*(liveMode_?1:oversamplingFactor));
        truePeakDecay_=std::exp(-1/(fs_*.5));makeFilter();meter_.prepare(fs_);prepared_=true;reset();
    }
    void setParameters(const FinishParameters& p) noexcept{
        releaseSeconds_=std::clamp(detail::finite(p.releaseMs,90),20.,500.)*.001;
        limiterRelease_=detail::alpha(releaseSeconds_,fs_*(liveMode_?1:oversamplingFactor));
        ceilingTarget_=std::clamp(detail::finite(p.ceilingDb,-1),-12.,0.);
        driveTarget_=std::clamp(detail::finite(p.driveDb),0.,24.);
        compTarget_=p.compEnabled?std::clamp(detail::finite(p.comp),0.,100.)*.01:0;
        clipTarget_=p.clipEnabled?std::clamp(detail::finite(p.clip),0.,100.)*.01:0;
        stereoTarget_=p.stereoEnabled?1:0;widthTarget_=std::clamp(detail::finite(p.width,100),0.,150.)*.01;
        bassTarget_=std::clamp(detail::finite(p.bassMonoHz,80),20.,250.);
        toneTarget_={p.toneEnabled?std::clamp(detail::finite(p.lowDb),-6.,6.):0,p.toneEnabled?std::clamp(detail::finite(p.midDb),-6.,6.):0,p.toneEnabled?std::clamp(detail::finite(p.highDb),-6.,6.):0};
        limiterTarget_=p.limiterEnabled?1:0;oversamplingTarget_=p.limiterEnabled||clipTarget_>0?1:0;
    }
    void reset() noexcept{
        for(auto& s:state_){s.input.fill(0);s.output.fill(0);s.lookahead.fill(0);s.dry.fill(0);s.peakInput.fill(0);for(auto& f:s.tone)f.reset();s.inputPosition=s.outputPosition=s.peakPosition=0;}
        queueHead_=queueCount_=0;sampleClock_=0;delayPosition_=dryPosition_=controlClock_=0;
        sideLow1_=sideLow2_=compressorPower_=compressorDb_=truePeak_=maximumTruePeak_=0;limiterGain_=1;
        ceiling_=ceilingTarget_;drive_=driveTarget_;comp_=compTarget_;clip_=clipTarget_;width_=widthTarget_;bass_=bassTarget_;stereo_=stereoTarget_;limiter_=limiterTarget_;oversampling_=oversamplingTarget_;tone_=toneTarget_;
        meter_.reset();compressorMeter_=limiterMeter_=truePeakMeter_=maximumTruePeakMeter_=0;peakResetRequested_=false;updateTone();
    }
    void process(float*const* audio,int channels,int frames,const float*const* sidechain=nullptr,int sidechainChannels=0) noexcept{
        (void)sidechain;(void)sidechainChannels;if(!audio||channels<=0||frames<=0)return;if(!prepared_)prepare(48000,frames,channels);
        if(peakResetRequested_.exchange(false,std::memory_order_relaxed))truePeak_=maximumTruePeak_=0;
        const int count=std::min({channels,channels_,maximumChannels});double lastLimit=0;
        for(int i=0;i<frames;++i){
            smooth(ceiling_,ceilingTarget_);smooth(drive_,driveTarget_);smooth(comp_,compTarget_);smooth(clip_,clipTarget_);smooth(width_,widthTarget_);smooth(bass_,bassTarget_);smooth(stereo_,stereoTarget_);smooth(limiter_,limiterTarget_);smooth(oversampling_,oversamplingTarget_);for(int k=0;k<3;++k)smooth(tone_[k],toneTarget_[k]);
            if(controlClock_++==0)updateTone();if(controlClock_>=16)controlClock_=0;
            std::array<double,maximumChannels> x{},dry{},decimated{};double inPower=0,inPeak=0,detector=0;
            for(int c=0;c<count;++c){x[c]=audio[c]?detail::input(audio[c][i]):0;inPower+=x[c]*x[c]/count;inPeak=std::max(inPeak,std::abs(x[c]));for(auto& f:state_[c].tone)x[c]=f.process(x[c]);}
            if(count>=2){
                const double mid=.5*(x[0]+x[1]),side=.5*(x[0]-x[1]);
                const double a=1-std::exp(-2*detail::pi*bass_/fs_);detail::follow(sideLow1_,side,a);const double hp=side-sideLow1_;detail::follow(sideLow2_,hp,a);
                const double transformed=(hp-sideLow2_)*width_;const double s=side+stereo_*(transformed-side);x[0]=mid+s;x[1]=mid-s;
            }
            for(int c=0;c<count;++c)detector=std::max(detector,x[c]*x[c]);
            detail::follow(compressorPower_,detector,compressorDetect_);
            const double targetReduction=.7*detail::softExcess(detail::db(std::sqrt(std::max(0.,compressorPower_)))+18,8);
            detail::follow(compressorDb_,targetReduction,targetReduction>compressorDb_?compressorAttack_:compressorRelease_);
            const double preGain=detail::gain(drive_-comp_*compressorDb_);
            for(int c=0;c<count;++c){auto& s=state_[c];x[c]*=preGain;dry[c]=liveMode_?x[c]:s.dry[dryPosition_];s.dry[dryPosition_]=x[c];s.input[s.inputPosition]=s.input[s.inputPosition+inputHistory]=x[c];}
            const double ceiling=detail::gain(ceiling_);
            // Reserve 0.25 dB for finite interpolation/decimation and envelope
            // modulation. The final sample guard is separate from TP estimation.
            const double limitingCeiling=ceiling*0.9716279515771061;
            for(int phase=0;phase<(liveMode_?1:oversamplingFactor);++phase){
                std::array<double,maximumChannels> candidate{},delayed{};double peak=0,delayedPeak=0;
                for(int c=0;c<count;++c){auto& s=state_[c];double up=liveMode_?x[c]:dot<inputHistory>(s.input.data()+s.inputPosition,interpolation_[phase].data());
                    if(clip_>0){const double a=std::abs(up)/ceiling;const double shaped=a<=.75?up:std::copysign(ceiling*(.75+.25*std::tanh((a-.75)*4)),up);up+=clip_*(shaped-up);}
                    candidate[c]=up;peak=std::max(peak,std::abs(up));delayed[c]=liveMode_?up:s.lookahead[delayPosition_];s.lookahead[delayPosition_]=up;delayedPeak=std::max(delayedPeak,std::abs(delayed[c]));
                }
                while(queueCount_&&queueIndex_[queueHead_]+std::uint64_t(liveMode_?0:lookahead_)<sampleClock_){queueHead_=(queueHead_+1)%maximumLookahead;--queueCount_;}
                while(queueCount_){const int back=(queueHead_+queueCount_-1)%maximumLookahead;if(queuePeak_[back]>peak)break;--queueCount_;}
                const int tail=(queueHead_+queueCount_)%maximumLookahead;queuePeak_[tail]=peak;queueIndex_[tail]=sampleClock_;++queueCount_;
                const double wanted=std::min(1.,limitingCeiling/std::max(1e-20,queuePeak_[queueHead_]));
                detail::follow(limiterGain_,wanted,wanted<limiterGain_?limiterAttack_:limiterRelease_);
                const double guarded=std::min(limiterGain_,limitingCeiling/std::max(1e-20,delayedPeak));
                const double applied=1+limiter_*(guarded-1);lastLimit=-detail::db(applied);
                for(int c=0;c<count;++c){auto& s=state_[c];const double y=delayed[c]*applied;s.output[s.outputPosition]=s.output[s.outputPosition+filterTaps]=y;if(phase==0)decimated[c]=liveMode_?y:dot<filterTaps>(s.output.data()+s.outputPosition,filter_.data());if(--s.outputPosition<0)s.outputPosition=filterTaps-1;}
                delayPosition_=(delayPosition_+1)%lookahead_;++sampleClock_;
            }
            double outputPeak=0,outputPower=0;std::array<double,maximumChannels> output{};
            for(int c=0;c<count;++c){auto& s=state_[c];if(--s.inputPosition<0)s.inputPosition=inputHistory-1;output[c]=dry[c]+oversampling_*(decimated[c]-dry[c]);outputPeak=std::max(outputPeak,std::abs(output[c]));}
            const double sampleGuard=limiterTarget_>0?std::min(1.,ceiling/std::max(1e-20,outputPeak)):1;
            double estimated=0;
            for(int c=0;c<count;++c){auto& s=state_[c];const double y=detail::quiet(output[c]*sampleGuard);if(audio[c])audio[c][i]=float(y);outputPower+=y*y/count;s.peakInput[s.peakPosition]=s.peakInput[s.peakPosition+meterHistory]=y;for(int phase=0;phase<oversamplingFactor;++phase)estimated=std::max(estimated,std::abs(dot<meterHistory>(s.peakInput.data()+s.peakPosition,peakInterpolation_[phase].data())));if(--s.peakPosition<0)s.peakPosition=meterHistory-1;}
            truePeak_=std::max(estimated,detail::quiet(truePeak_*truePeakDecay_));maximumTruePeak_=std::max(maximumTruePeak_,estimated);meter_.sample(inPower,outputPower,inPeak,outputPeak*sampleGuard);dryPosition_=(dryPosition_+1)%latency_;
        }
        meter_.publish();compressorMeter_=float(comp_*compressorDb_);limiterMeter_=float(std::max(0.,lastLimit));truePeakMeter_=float(truePeak_);maximumTruePeakMeter_=float(maximumTruePeak_);
    }
    // LIVE retains tone, width, compression and sample-peak protection without
    // FIR/lookahead buffering. PRO provides the oversampled true-peak path.
    void setLiveMode(bool live) noexcept {
        if(liveMode_==live)return;liveMode_=live;
        limiterAttack_=detail::alpha(.00004,fs_*(liveMode_?1:oversamplingFactor));
        limiterRelease_=detail::alpha(releaseSeconds_,fs_*(liveMode_?1:oversamplingFactor));
        reset();
    }
    int latencySamples()const noexcept{return liveMode_?0:latency_;}double tailSeconds()const noexcept{return latencySamples()/fs_+.12;}
    float gainReductionDb()const noexcept{return compressorReductionDb()+limiterReductionDb();}
    float compressorReductionDb()const noexcept{return compressorMeter_.load(std::memory_order_relaxed);}
    float limiterReductionDb()const noexcept{return limiterMeter_.load(std::memory_order_relaxed);}
    float inputRms()const noexcept{return meter_.in.load(std::memory_order_relaxed);}float outputRms()const noexcept{return meter_.out.load(std::memory_order_relaxed);}
    float inputPeak()const noexcept{return meter_.inputPeak.load(std::memory_order_relaxed);}float outputPeak()const noexcept{return meter_.outputPeak.load(std::memory_order_relaxed);}
    float estimatedTruePeak()const noexcept{return truePeakMeter_.load(std::memory_order_relaxed);}
    float maximumTruePeak()const noexcept{return maximumTruePeakMeter_.load(std::memory_order_relaxed);}
    void resetPeakStatistics()noexcept{peakResetRequested_.store(true,std::memory_order_relaxed);}
    float toneResponseDb(float frequency)const noexcept{const double fs=rateMeter_.load(std::memory_order_relaxed);return float(detail::db(std::abs(detail::shelf(fs,100,toneMeters_[0].load(),false).response(frequency,fs)*detail::peak(fs,900,.55,toneMeters_[1].load()).response(frequency,fs)*detail::shelf(fs,8000,toneMeters_[2].load(),true).response(frequency,fs))));}
private:
    bool liveMode_=false;
    static constexpr int inputHistory=(filterTaps+oversamplingFactor-1)/oversamplingFactor,meterHistory=65;
    template<int N>static double dot(const double* x,const double* c)noexcept{double a=0,b=0;int i=0;for(;i+1<N;i+=2){a+=x[i]*c[i];b+=x[i+1]*c[i+1];}if(i<N)a+=x[i]*c[i];return a+b;}
    void smooth(double& x,double target)noexcept{detail::follow(x,target,smoothing_);}
    void updateTone()noexcept{const std::array<detail::Coefficients,3> c{detail::shelf(fs_,100,tone_[0],false),detail::peak(fs_,900,.55,tone_[1]),detail::shelf(fs_,8000,tone_[2],true)};for(auto& s:state_)for(int k=0;k<3;++k)s.tone[k].c=c[k];for(int k=0;k<3;++k)toneMeters_[k]=float(tone_[k]);}
    void makeFilter()noexcept{
        constexpr double cutoff=.48/oversamplingFactor;constexpr int center=(filterTaps-1)/2;double sum=0;
        for(int tap=0;tap<filterTaps;++tap){const double n=tap-center;const double sinc=tap==center?2*cutoff:std::sin(2*detail::pi*cutoff*n)/(detail::pi*n);const double angle=2*detail::pi*tap/(filterTaps-1);filter_[tap]=sinc*(.42-.5*std::cos(angle)+.08*std::cos(2*angle));sum+=filter_[tap];}
        for(auto& c:filter_)c/=sum;
        for(int phase=0;phase<oversamplingFactor;++phase)for(int n=0;n<inputHistory;++n){const int tap=phase+n*oversamplingFactor;interpolation_[phase][n]=tap<filterTaps?filter_[tap]*oversamplingFactor:0;}
        // Meter the returned base-rate signal with a longer full-band sinc,
        // rather than reusing the deliberately band-limited anti-alias filter.
        for(int phase=0;phase<oversamplingFactor;++phase){double total=0;for(int tap=0;tap<meterHistory;++tap){const double d=tap-32+phase/double(oversamplingFactor);const double sinc=std::abs(d)<1e-12?1:std::sin(detail::pi*d)/(detail::pi*d);const double window=std::abs(d)<=32?.42+.5*std::cos(detail::pi*d/32)+.08*std::cos(2*detail::pi*d/32):0;peakInterpolation_[phase][tap]=sinc*window;total+=sinc*window;}for(auto&c:peakInterpolation_[phase])c/=total;}
    }
    struct State{std::array<detail::Biquad,3>tone{};std::array<double,inputHistory*2>input{};std::array<double,meterHistory*2>peakInput{};std::array<double,filterTaps*2>output{};std::array<double,maximumLookahead>lookahead{};std::array<double,maximumBaseDelay>dry{};int inputPosition=0,outputPosition=0,peakPosition=0;};
    std::array<State,maximumChannels>state_{};
    std::array<double,filterTaps>filter_{};std::array<std::array<double,inputHistory>,oversamplingFactor>interpolation_{};
    std::array<std::array<double,meterHistory>,oversamplingFactor>peakInterpolation_{};
    std::array<double,maximumLookahead>queuePeak_{};std::array<std::uint64_t,maximumLookahead>queueIndex_{};
    detail::Meter meter_;std::atomic<double>rateMeter_{48000};std::atomic<float>compressorMeter_{0},limiterMeter_{0},truePeakMeter_{0},maximumTruePeakMeter_{0};std::array<std::atomic<float>,3>toneMeters_{};
    std::atomic<bool>peakResetRequested_{false};
    std::array<double,3>toneTarget_{},tone_{};
    double releaseSeconds_=.09,fs_=48000,smoothing_=0,compressorDetect_=0,compressorAttack_=0,compressorRelease_=0,limiterAttack_=0,limiterRelease_=0,truePeakDecay_=0;
    double ceilingTarget_=-1,driveTarget_=0,compTarget_=0,clipTarget_=0,widthTarget_=1,bassTarget_=80,stereoTarget_=1,limiterTarget_=1,oversamplingTarget_=1;
    double ceiling_=-1,drive_=0,comp_=0,clip_=0,width_=1,bass_=80,stereo_=1,limiter_=1,oversampling_=1;
    double sideLow1_=0,sideLow2_=0,compressorPower_=0,compressorDb_=0,limiterGain_=1,truePeak_=0,maximumTruePeak_=0;
    int channels_=2,lookaheadBase_=144,lookahead_=1152,latency_=176,queueHead_=0,queueCount_=0,delayPosition_=0,dryPosition_=0,controlClock_=0;std::uint64_t sampleClock_=0;bool prepared_=false;
};
}
