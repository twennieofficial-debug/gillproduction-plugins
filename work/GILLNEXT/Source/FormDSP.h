#pragma once
#include "NextDSPCommon.h"
#include "FormantEnvelopeDSP.h"
#include "../ThirdParty/signalsmith-stretch/signalsmith-stretch.h"
#include <vector>

namespace gillnext {
struct FormParameters {float pitchSemitones=0,formantSemitones=0,mix=100;bool link=false,preserveTransients=true;};

// Independent pitch and spectral-envelope transposition. Unmodified MIT
// Signalsmith Stretch shifts pitch; the separate cepstral envelope stage moves
// formants without changing the excitation frequencies. This avoids changing
// the pitch mapper's peak assignments when the spectral envelope is edited.
// Mono/stereo voice takes; no source separation or claim of vocal reconstruction.
class FormDSP {
public:
    void prepare(double fs,int maxBlock,int channels){
        (void)maxBlock;prepared_=false;latency_=0;
        if(!std::isfinite(fs)||fs<8000||fs>192000)return;
        fs_=fs;channels_=std::clamp(channels,1,2);int fft=256;while(fft<fs*.042)fft*=2;
        stretch_.configure(channels_,fft*2,fft/2,false);
        formants_.prepare(fs,channels_,fft);
        stretch_.setFormantFactor(1,false);
        latency_=stretch_.inputLatency()+stretch_.outputLatency()+formants_.latencySamples()+chunk;
        for(auto&d:dry_)d.assign(latency_,0);transientDelay_.assign(latency_,0);
        paramRamp_=std::max(1,int(fs*.015));mixRamp_=std::max(1,int(fs*.005));
        shortAlpha_=detail::alpha(.001,fs);longAlpha_=detail::alpha(.040,fs);releaseAlpha_=detail::alpha(.010,fs);
        meter_.prepare(fs);prepared_=true;reset();
    }
    void setParameters(const FormParameters&p) noexcept{
        const double pitch=std::clamp(detail::finite(p.pitchSemitones),-12.,12.);
        const double formant=p.link?pitch:std::clamp(detail::finite(p.formantSemitones),-12.,12.);
        const double mix=std::clamp(detail::finite(p.mix),0.,100.)*.01;
        if(pitch!=pitchTarget_||formant!=formantTarget_){pitchTarget_=pitch;formantTarget_=formant;paramRemaining_=paramRamp_;}
        const double wet=(pitch==0&&formant==0)?0:mix;
        if(wet!=wetTarget_){wetTarget_=wet;mixRemaining_=mixRamp_;}
        preserve_=p.preserveTransients;
        if(!processed_){pitch_=pitchTarget_;formant_=formantTarget_;wet_=wetTarget_;paramRemaining_=mixRemaining_=0;}
    }
    void reset() noexcept{
        if(prepared_){stretch_.reset();formants_.reset();}
        for(auto&d:dry_)std::fill(d.begin(),d.end(),0.f);std::fill(transientDelay_.begin(),transientDelay_.end(),0.f);
        for(auto&b:input_)b.fill(0);for(auto&b:wetBuffer_)b.fill(0);
        phase_=dryIndex_=transientHold_=0;shortPower_=longPower_=transient_=0;wasOnset_=false;
        pitch_=pitchTarget_;formant_=formantTarget_;wet_=wetTarget_;paramRemaining_=mixRemaining_=0;processed_=false;
        meter_.reset();
    }
    void process(float*const* audio,int channels,int frames) noexcept{
        if(!prepared_||!audio||channels<=0||frames<=0)return;const int active=std::min(channels,channels_);
        for(int c=0;c<active;++c)if(!audio[c])return;processed_=true;
        for(int i=0;i<frames;++i){
            if(paramRemaining_>0){pitch_+=(pitchTarget_-pitch_)/paramRemaining_;formant_+=(formantTarget_-formant_)/paramRemaining_;--paramRemaining_;}
            if(mixRemaining_>0){wet_+=(wetTarget_-wet_)/mixRemaining_;--mixRemaining_;}
            std::array<double,2>x{};double inputPower=0,inputPeak=0;
            for(int c=0;c<channels_;++c){x[c]=c<active?detail::input(audio[c][i]):0;input_[c][phase_]=float(x[c]);inputPower+=x[c]*x[c]/channels_;inputPeak=std::max(inputPeak,std::abs(x[c]));}
            // M/S shares one timing/phase model while keeping identical and
            // opposite-polarity stereo vocals exactly related after synthesis.
            if(channels_==2){input_[0][phase_]=float((x[0]+x[1])*.5);input_[1][phase_]=float((x[0]-x[1])*.5);}
            detail::follow(shortPower_,inputPower,shortAlpha_);detail::follow(longPower_,inputPower,longAlpha_);
            const bool onset=shortPower_>1e-5&&shortPower_>longPower_*8;
            if(onset&&!wasOnset_)transientHold_=std::max(1,int(fs_*.005));wasOnset_=onset;
            if(transientHold_>0){transient_=1;--transientHold_;}else detail::follow(transient_,0,releaseAlpha_);
            const double dryTransient=transientDelay_[dryIndex_];transientDelay_[dryIndex_]=float(transient_);
            const double wetAmount=wet_*(preserve_?1-dryTransient:1);
            double outPower=0,outPeak=0;
            for(int c=0;c<channels_;++c){const double dry=dry_[c][dryIndex_];dry_[c][dryIndex_]=float(x[c]);const double shifted=detail::input(channels_==2?double(wetBuffer_[0][phase_])+(c==0?1:-1)*wetBuffer_[1][phase_]:wetBuffer_[c][phase_]);const double y=wetAmount==0?dry:dry+wetAmount*(shifted-dry);if(c<active)audio[c][i]=float(y);outPower+=y*y/channels_;outPeak=std::max(outPeak,std::abs(y));}
            meter_.sample(inputPower,outPower,inputPeak,outPeak);
            if(++dryIndex_>=latency_)dryIndex_=0;
            if(++phase_==chunk){
                stretch_.setTransposeSemitones(float(pitch_));
                formants_.setSemitones(formant_-pitch_);
                std::array<const float*,2> in{input_[0].data(),input_[1].data()};std::array<float*,2>out{wetBuffer_[0].data(),wetBuffer_[1].data()};
                std::array<float*,2> intermediate{pitchBuffer_[0].data(),pitchBuffer_[1].data()};
                stretch_.process(in.data(),chunk,intermediate.data(),chunk);
                formants_.process(intermediate.data(),out.data(),chunk);phase_=0;
            }
        }
        meter_.publish();
    }
    int latencySamples()const noexcept{return latency_;}
    double tailSeconds()const noexcept{return prepared_?double(latency_+stretch_.blockSamples())/fs_:0;}
    float inputRms()const noexcept{return meter_.in.load(std::memory_order_relaxed);}
    float outputRms()const noexcept{return meter_.out.load(std::memory_order_relaxed);}
    float gainReductionDb()const noexcept{return 0;}
private:
    static constexpr int chunk=64;
    signalsmith::stretch::SignalsmithStretch<float> stretch_{91273};detail::FormantEnvelopeDSP formants_;detail::Meter meter_;
    std::array<std::array<float,chunk>,2>input_{},wetBuffer_{},pitchBuffer_{};std::array<std::vector<float>,2>dry_;std::vector<float>transientDelay_;
    double fs_=48000,pitch_=0,formant_=0,wet_=0,pitchTarget_=0,formantTarget_=0,wetTarget_=0;
    double shortPower_=0,longPower_=0,transient_=0,shortAlpha_=0,longAlpha_=0,releaseAlpha_=0;
    int channels_=1,latency_=0,phase_=0,dryIndex_=0,paramRamp_=720,mixRamp_=240,paramRemaining_=0,mixRemaining_=0,transientHold_=0;
    bool prepared_=false,processed_=false,preserve_=true,wasOnset_=false;
};
}
