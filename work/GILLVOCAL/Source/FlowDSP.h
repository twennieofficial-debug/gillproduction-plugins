#pragma once
// Original, zero-lookahead vocal compressor. Learning uses bounded local
// signal statistics, not a trained model or semantic voice recognition.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gill {
struct LearnProfile {
    std::uint32_t version=1;
    bool valid=false;
    float rmsDb=-24,peakDb=-12,crestDb=12,thresholdDb=-18;
    // v2 adds robust phrase range and envelope movement. v1 imports use the
    // original fixed timing, so saved sessions do not acquire invented data.
    float dynamicRangeDb=0, motionDb=0, attackMs=20, releaseMs=160;
};

class FlowDSP {
public:
    void prepare(double rate,int maxBlock,int channels) noexcept {
        (void)maxBlock;(void)channels;
        fs=std::isfinite(rate)&&rate>=8000&&rate<=384000?rate:48000;
        smoothingSamples=std::max(1,static_cast<int>(std::lround(fs*.020)));
        frameLength=std::max(1,static_cast<int>(std::lround(fs*.020)));
        rmsDecay=std::exp(-1/(fs*.008));makeupPowerDecay=std::exp(-1/(fs*.500));
        makeupDecay=std::exp(-1/(fs*.300));thresholdDecay=std::exp(-1/(fs*.050));
        const double attacks[]{.020,.007,.001},releases[]{.160,.090,.060};
        for(int i=0;i<3;++i){attack[i]=std::exp(-1/(fs*attacks[i]));release[i]=std::exp(-1/(fs*releases[i]));}
        reset();
    }
    // Reset/prepare preserve the completed profile and user settings, but
    // cancel unfinished learning and discard all running audio history.
    void reset() noexcept {
        amount.reset(requestedAmount*.01);style.reset(requestedMode);autoBlend.reset(requestedAuto?1:0);
        power=gr=lastReduction=makeupDb=matchInput=matchOutput=0;
        threshold=profile.valid?profile.thresholdDb:-18;
        updateLearnedDynamics();
        clearLearning();learnState=profile.valid?2:0;hasAudio=false;
    }
    void setParameters(float amount0to100,int mode,bool autoGain) noexcept {
        requestedAmount=finiteClamp(amount0to100,0,100,55);requestedMode=std::clamp(mode,0,2);requestedAuto=autoGain;
        if(hasAudio){amount.set(requestedAmount*.01,smoothingSamples);style.set(requestedMode,smoothingSamples);autoBlend.set(autoGain?1:0,smoothingSamples);}
        else{amount.reset(requestedAmount*.01);style.reset(requestedMode);autoBlend.reset(autoGain?1:0);}
    }
    void startLearning() noexcept {clearLearning();learnState=1;}
    void cancelLearning() noexcept {clearLearning();learnState=profile.valid?2:0;}
    int latencySamples() const noexcept {return 0;}
    float gainReductionDb() const noexcept {return static_cast<float>(lastReduction);}
    float makeupGainDb() const noexcept {return static_cast<float>(makeupDb*autoBlend.value*std::min(1.,amount.value*10));}
    int learningState() const noexcept {return learnState;}
    float learningProgress() const noexcept {return learnState==2?1.f:(learnState==0?0.f:static_cast<float>(std::min(1.,activeSamples/(fs*10.))));}
    LearnProfile learnedProfile() const noexcept {return profile;}
    bool setLearnedProfile(const LearnProfile& imported) noexcept {
        if(imported.version!=1&&imported.version!=2)return false;
        if(!imported.valid){profile=LearnProfile{};updateLearnedDynamics();cancelLearning();return true;}
        if(!inRange(imported.rmsDb,-72,6)||!inRange(imported.peakDb,-72,18)||!inRange(imported.crestDb,0,48)
            ||!inRange(imported.thresholdDb,imported.version==1?-48:-66,imported.version==1?-8:-3)||imported.peakDb<imported.rmsDb)return false;
        if(imported.version==2&&(!inRange(imported.dynamicRangeDb,0,48)||!inRange(imported.motionDb,0,48)
            ||!inRange(imported.attackMs,6,30)||!inRange(imported.releaseMs,70,280)))return false;
        profile=imported;updateLearnedDynamics();cancelLearning();return true;
    }
    void process(float* const* buffers,int channels,int samples) noexcept {
        if(!buffers||channels<1||samples<=0||!buffers[0])return;
        const int count=channels>1&&buffers[1]?2:1;hasAudio=true;
        for(int i=0;i<samples;++i){
            std::array<double,2> input{};double peak=0;
            for(int c=0;c<count;++c){const double value=buffers[c][i];input[c]=std::isfinite(value)&&std::abs(value)<=1e12?value:0;peak=std::max(peak,std::abs(input[c]));}
            observeLearning(peak);
            amount.next();style.next();autoBlend.next();
            power=rmsDecay*power+(1-rmsDecay)*peak*peak;if(power<1e-30)power=0;
            const double rmsDb=toDb(power),peakDb=20*std::log10(std::max(1e-15,peak));
            const double level=interpolate(rmsDb,std::max(rmsDb,peakDb-8),peakDb,style.value);
            const double desiredThreshold=profile.valid?profile.thresholdDb:-18;
            threshold=desiredThreshold+thresholdDecay*(threshold-desiredThreshold);
            const double knee=interpolate(8,6,3,style.value),ratio=interpolate(3,6,12,style.value)*learnedRatioScale;
            const double over=level-(threshold-amount.value*interpolate(8,11,15,style.value));
            double target=0;if(over>=knee*.5)target=(1-1/ratio)*over;
            else if(over>-knee*.5){const auto soft=over+knee*.5;target=(1-1/ratio)*soft*soft/(2*knee);}
            target=std::clamp(target,0.,24.);
            const auto decay=target>gr?interpolate(attack[0],attack[1],attack[2],style.value):interpolate(release[0],release[1],release[2],style.value);
            gr=target+decay*(gr-target);if(gr<1e-12)gr=0;
            lastReduction=std::clamp(gr*amount.value,0.,24.);
            const auto compressedGain=std::pow(10.,-lastReduction/20.);
            // Track a slow ratio of input and compressed powers on active
            // audio. This is bounded compensation, not a loudness standard.
            if(peak>1e-3){const auto p=peak*peak;matchInput=makeupPowerDecay*matchInput+(1-makeupPowerDecay)*p;matchOutput=makeupPowerDecay*matchOutput+(1-makeupPowerDecay)*p*compressedGain*compressedGain;}
            const double makeupTarget=matchOutput>1e-24?std::clamp(10*std::log10(matchInput/matchOutput),0.,9.):0;
            makeupDb=makeupTarget+makeupDecay*(makeupDb-makeupTarget);
            const double totalGain=std::pow(10.,(-lastReduction+makeupDb*autoBlend.value*std::min(1.,amount.value*10))/20.);
            for(int c=0;c<count;++c){const double result=input[c]*totalGain;buffers[c][i]=std::isfinite(result)?static_cast<float>(std::abs(result)<1e-30?0:result):0;}
        }
    }
private:
    struct Ramp {double value=0,target=0,step=0;int left=0;
        void reset(double v)noexcept{value=target=v;step=0;left=0;}
        void set(double v,int n)noexcept{if(v!=target){target=v;left=std::max(1,n);step=(target-value)/left;}}
        void next()noexcept{if(left>0){if(--left==0)value=target;else value+=step;}}
    };
    static double finiteClamp(double v,double lo,double hi,double fallback)noexcept{return std::isfinite(v)?std::clamp(v,lo,hi):fallback;}
    static bool inRange(double v,double lo,double hi)noexcept{return std::isfinite(v)&&v>=lo&&v<=hi;}
    static double toDb(double p)noexcept{return 10*std::log10(std::max(1e-30,p));}
    static double interpolate(double a,double b,double c,double mode)noexcept{return mode<=1?a+(b-a)*mode:b+(c-b)*(mode-1);}
    void updateLearnedDynamics()noexcept{
        const bool modern=profile.valid&&profile.version==2;
        const double attackScale=modern?profile.attackMs/20.:1.,releaseScale=modern?profile.releaseMs/160.:1.;
        const double attacks[]{.020,.007,.001},releases[]{.160,.090,.060};
        for(int i=0;i<3;++i){attack[i]=std::exp(-1/(fs*attacks[i]*attackScale));release[i]=std::exp(-1/(fs*releases[i]*releaseScale));}
        learnedRatioScale=modern?1+std::min(.4,profile.dynamicRangeDb/45.):1.;
    }
    void clearLearning()noexcept{rmsHistogram.fill(0);peakHistogram.fill(0);motionHistogram.fill(0);frameSamples=0;framePower=framePeak=0;activeSamples=wallSamples=0;acceptedFrames=0;previousFrameDb=-96;previousFrameActive=false;}
    static int histogramBin(double db)noexcept{return std::clamp(static_cast<int>(std::lround((db+96)*2)),0,256);}
    double percentile(const std::array<std::uint32_t,257>& histogram,double fraction)const noexcept{
        const auto target=static_cast<std::uint32_t>(std::max(1.,std::ceil(acceptedFrames*fraction)));std::uint32_t sum=0;
        for(size_t i=0;i<histogram.size();++i){sum+=histogram[i];if(sum>=target)return static_cast<double>(i)*.5-96;}return -24;
    }
    void observeLearning(double peak)noexcept{
        if(learnState!=1)return;
        ++wallSamples;framePower+=peak*peak;framePeak=std::max(framePeak,peak);++frameSamples;
        if(frameSamples>=frameLength){const double rms=toDb(framePower/frameSamples),maximum=20*std::log10(std::max(1e-15,framePeak));
            // Ignore silence, isolated high-crest spikes and gross overloads.
            // Continuous noise can still be learned: there is no voice model.
            if(rms>-60&&maximum<18&&maximum-rms<24){++rmsHistogram[static_cast<size_t>(histogramBin(rms))];++peakHistogram[static_cast<size_t>(histogramBin(maximum))];
                const double movement=previousFrameActive?std::abs(rms-previousFrameDb):0;
                ++motionHistogram[static_cast<size_t>(histogramBin(movement))];previousFrameDb=rms;previousFrameActive=true;
                ++acceptedFrames;activeSamples+=frameSamples;
            }else previousFrameActive=false;
            frameSamples=0;framePower=framePeak=0;
            if(activeSamples>=fs*10.){LearnProfile next;next.version=2;next.valid=true;
                next.rmsDb=static_cast<float>(std::clamp(percentile(rmsHistogram,.5),-72.,6.));
                next.peakDb=static_cast<float>(std::clamp(std::max(static_cast<double>(next.rmsDb),percentile(peakHistogram,.8)),-72.,18.));next.crestDb=next.peakDb-next.rmsDb;
                next.dynamicRangeDb=static_cast<float>(std::clamp(percentile(rmsHistogram,.9)-percentile(rmsHistogram,.2),0.,48.));
                next.motionDb=static_cast<float>(std::clamp(percentile(motionHistogram,.85),0.,48.));
                next.thresholdDb=std::clamp(next.rmsDb+3.f-.15f*next.dynamicRangeDb,-66.f,-3.f);
                next.attackMs=std::clamp(28.f-1.2f*next.crestDb-.9f*next.motionDb,6.f,30.f);
                next.releaseMs=std::clamp(180.f-6.f*next.motionDb+4.f*next.dynamicRangeDb,70.f,280.f);
                profile=next;updateLearnedDynamics();learnState=2;}
        }
        // A bounded attempt avoids a permanently spinning learner. Failed
        // attempts preserve any previously completed profile.
        if(learnState==1&&wallSamples>=fs*30.)learnState=3;
    }
    double fs=48000;int smoothingSamples=960,frameLength=960,frameSamples=0,learnState=0;
    double requestedAmount=55;int requestedMode=0;bool requestedAuto=true,hasAudio=false;
    Ramp amount{.55,.55,0,0},style{},autoBlend{1,1,0,0};
    double power=0,gr=0,lastReduction=0,threshold=-18,makeupDb=0,matchInput=0,matchOutput=0,learnedRatioScale=1;
    double rmsDecay=std::exp(-1/(48000*.008)),makeupPowerDecay=std::exp(-1/(48000*.500)),makeupDecay=std::exp(-1/(48000*.300)),thresholdDecay=std::exp(-1/(48000*.050));
    std::array<double,3> attack{std::exp(-1/(48000*.020)),std::exp(-1/(48000*.007)),std::exp(-1/(48000*.001))},release{std::exp(-1/(48000*.160)),std::exp(-1/(48000*.090)),std::exp(-1/(48000*.060))};
    LearnProfile profile;
    std::array<std::uint32_t,257> rmsHistogram{},peakHistogram{},motionHistogram{};
    std::uint64_t activeSamples=0,wallSamples=0;std::uint32_t acceptedFrames=0;
    double framePower=0,framePeak=0,previousFrameDb=-96;bool previousFrameActive=false;
};
}
