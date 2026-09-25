#pragma once
// Original statistical vocal tonal balance. No learned artist reference,
// semantic voice classifier or lookahead audio delay. Version 2 learns a fine
// spectrum, up to twelve narrow bells and eight linked dynamic EQ bands.
#include "FineBalance.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gill {
struct LearnBalanceProfile {
    std::uint32_t version=1;
    bool valid=false;
    double sampleRate=48000;
    // Median band RMS relative to full-band RMS, NOT an absolute dBFS axis.
    std::array<float,8> bandDb{};
    std::array<bool,8> validBands{};
    float activeRmsDb=-24;
    FineBalanceProfile fine;
};

class BalanceDSP {
public:
    static constexpr std::size_t bandCount=8;
    inline static constexpr std::array<float,8> centersHz{100,200,400,800,1600,3200,6400,12000};
    static constexpr double bellQ=.85;
    void prepare(double rate,int maxBlock,int channels) noexcept {
        (void)maxBlock;(void)channels;
        fs=std::isfinite(rate)&&rate>=8000&&rate<=384000?rate:48000;
        frameLength=std::max(1,static_cast<int>(std::lround(fs*.020)));
        rampLength=std::max(16,static_cast<int>(std::lround(fs*.080)));
        meterDecay=std::exp(-1/(fs*.200));
        detectorDecay=std::exp(-1/(fs*.012));
        fineAnalyzer.prepare(fs);fineStage.prepare(fs);
        for(std::size_t b=0;b<bandCount;++b){
            available[b]=centersHz[b]<=fs*.45;
            const double w=2*pi*centersHz[b]/fs;
            cosine[b]=std::cos(w);alpha[b]=std::sin(w)/(2*bellQ);
            if(available[b]){const double a=std::sin(w)/(2*1.1),norm=1/(1+a);analysis[b]={a*norm,0,-a*norm,-2*cosine[b]*norm,(1-a)*norm};}
            else analysis[b]={};
        }
        reset();
    }
    // Completed profile/settings survive reset and prepare. Any incomplete
    // attempt is discarded, as are audio/filter/meter histories.
    void reset() noexcept {
        inputState={};outputState={};equalizerState={};prePower={};postPower={};
        preDb.fill(-120);postDb.fill(-120);hasAudio=false;quantumLeft=0;fullPower=0;detectorPower={};
        fineStage.configure(profile.fine,profile.valid&&profile.version==2,profile.activeRmsDb,profile.bandDb);
        rebuildTargets();setGainTargets(true);clearLearning();learnState=profile.valid?2:0;
    }
    void setParameters(float amount0to100,int target0to4) noexcept {
        requestedAmount=std::isfinite(amount0to100)?std::clamp(amount0to100,0.f,100.f):60.f;
        requestedTarget=std::clamp(target0to4,0,4);setGainTargets(!hasAudio);
        fineStage.setAmount(requestedAmount);
    }
    void startLearning() noexcept {clearLearning();learnState=1;}
    void cancelLearning() noexcept {clearLearning();learnState=profile.valid?2:0;}
    int latencySamples() const noexcept {return 0;}
    int learningState() const noexcept {return learnState;}
    float learningProgress() const noexcept {return learnState==2?1.f:(learnState==0?0.f:static_cast<float>(std::min(1.,activeSamples/(fs*10))));}
    LearnBalanceProfile learnedProfile() const noexcept {return profile;}
    bool setLearnedProfile(const LearnBalanceProfile& p) noexcept {
        if(p.version!=1&&p.version!=2)return false;
        if(!p.valid){profile=LearnBalanceProfile{};fineStage.configure(profile.fine,false,profile.activeRmsDb,profile.bandDb);cancelLearning();rebuildTargets();setGainTargets(!hasAudio);return true;}
        if(!inRange(p.sampleRate,8000,384000)||!inRange(p.activeRmsDb,-60,12))return false;
        int usable=0;
        for(std::size_t b=0;b<bandCount;++b){
            if(!inRange(p.bandDb[b],-96,0))return false;
            if(p.validBands[b]){if(centersHz[b]>p.sampleRate*.45)return false;++usable;}
        }
        if(usable<3)return false;
        if(p.version==2){if(p.fine.frames<10||p.fine.frames>10000||!inRange(p.fine.analysisRate,8000,48000))return false;for(float x:p.fine.db)if(!inRange(x,-120,0))return false;}
        profile=p;fineStage.configure(profile.fine,p.version==2,profile.activeRmsDb,profile.bandDb);fineStage.setAmount(requestedAmount);cancelLearning();rebuildTargets();setGainTargets(!hasAudio);return true;
    }
    std::array<float,8> currentGainsDb() const noexcept {std::array<float,8> g{};for(std::size_t b=0;b<bandCount;++b)g[b]=static_cast<float>(gain[b]);return g;}
    std::array<float,8> preBandLevelsDb() const noexcept {return preDb;}
    std::array<float,8> postBandLevelsDb() const noexcept {return postDb;}
    // Nominal centers never move. An unavailable high band stays unavailable.
    std::array<bool,8> activeBands() const noexcept {return available;}
    FineBalanceView fineView() const noexcept {return fineStage.snapshot();}
    void process(float* const* buffers,int channels,int samples) noexcept {
        if(!buffers||channels<1||samples<=0||!buffers[0])return;
        const int count=channels>1&&buffers[1]?2:1;hasAudio=true;
        for(int i=0;i<samples;++i){
            if(quantumLeft==0)beginQuantum();
            bool exactDry=true;
            for(std::size_t b=0;b<bandCount;++b){
                if(gainLeft[b]>0){gain[b]+=gainStep[b];if(--gainLeft[b]==0)gain[b]=gainTarget[b];}
                coefficients[b]+=coefficientStep[b];
                exactDry=exactDry&&gain[b]==0&&gainTarget[b]==0;
            }
            if(--quantumLeft==0)coefficients=quantumEnd;
            double inputEnergy=0,peak=0;
            std::array<double,2> rawInputs{};
            fineStage.update(detectorPower,fullPower,available);
            std::array<double,8> frameBand{},outputBand{};
            for(int c=0;c<count;++c){
                const double raw=buffers[c][i];const double x=std::isfinite(raw)&&std::abs(raw)<=1e6?(std::abs(raw)<1e-30?0:raw):0;
                inputEnergy+=x*x;peak=std::max(peak,std::abs(x));double y=x;
                rawInputs[c]=x;
                if(exactDry){for(auto& state:equalizerState[c])state={};}
                else for(std::size_t b=0;b<bandCount;++b)if(available[b])y=equalizerState[c][b].tick(y,coefficients[b]);
                y=fineStage.process(y,c);
                if(!std::isfinite(y)||std::abs(y)>1e12)y=0;
                if(std::abs(y)<1e-30)y=0;buffers[c][i]=static_cast<float>(y);
                for(std::size_t b=0;b<bandCount;++b)if(available[b]){
                    const double pre=inputState[c][b].tick(x,analysis[b]),post=outputState[c][b].tick(y,analysis[b]);
                    frameBand[b]+=pre*pre;outputBand[b]+=post*post;
                }
            }
            for(std::size_t b=0;b<bandCount;++b){prePower[b]=meterDecay*prePower[b]+(1-meterDecay)*frameBand[b]/count;postPower[b]=meterDecay*postPower[b]+(1-meterDecay)*outputBand[b]/count;if(prePower[b]<1e-30)prePower[b]=0;if(postPower[b]<1e-30)postPower[b]=0;}
            for(std::size_t b=0;b<bandCount;++b)detectorPower[b]=detectorDecay*detectorPower[b]+(1-detectorDecay)*frameBand[b]/count;
            fullPower=detectorDecay*fullPower+(1-detectorDecay)*inputEnergy/count;
            if(learnState==1)fineAnalyzer.push(rawInputs[0],rawInputs[1],count);
            observe(inputEnergy/count,peak,frameBand,count);
        }
        for(std::size_t b=0;b<bandCount;++b){preDb[b]=available[b]?static_cast<float>(std::max(-120.,db(prePower[b]))):-120.f;postDb[b]=available[b]?static_cast<float>(std::max(-120.,db(postPower[b]))):-120.f;}
    }
private:
    static constexpr double pi=3.14159265358979323846;
    struct Coeff {
        double b0=1,b1=0,b2=0,a1=0,a2=0;
        Coeff& operator+=(const Coeff& x)noexcept{b0+=x.b0;b1+=x.b1;b2+=x.b2;a1+=x.a1;a2+=x.a2;return *this;}
    };
    struct State {double z1=0,z2=0;double tick(double x,const Coeff& c)noexcept{
        const double y=c.b0*x+z1;z1=c.b1*x-c.a1*y+z2;z2=c.b2*x-c.a2*y;
        if(std::abs(z1)<1e-30)z1=0;if(std::abs(z2)<1e-30)z2=0;return y;
    }};
    static bool inRange(double x,double lo,double hi)noexcept{return std::isfinite(x)&&x>=lo&&x<=hi;}
    static double db(double power)noexcept{return 10*std::log10(std::max(1e-30,power));}
    Coeff bell(std::size_t b,double dbGain) const noexcept {
        if(!available[b]||dbGain==0)return {};
        const double a=std::pow(10.,dbGain/40),norm=1/(1+alpha[b]/a);
        return {(1+alpha[b]*a)*norm,-2*cosine[b]*norm,(1-alpha[b]*a)*norm,-2*cosine[b]*norm,(1-alpha[b]/a)*norm};
    }
    void beginQuantum()noexcept{
        quantumLeft=16;
        for(std::size_t b=0;b<bandCount;++b){
            const auto advance=std::min(16,gainLeft[b]);const double next=advance==gainLeft[b]?gainTarget[b]:gain[b]+gainStep[b]*advance;
            quantumEnd[b]=bell(b,next);
            const auto& a=coefficients[b];const auto& z=quantumEnd[b];coefficientStep[b]={(z.b0-a.b0)/16,(z.b1-a.b1)/16,(z.b2-a.b2)/16,(z.a1-a.a1)/16,(z.a2-a.a2)/16};
        }
    }
    void setGainTargets(bool immediate)noexcept{
        for(std::size_t b=0;b<bandCount;++b){const double desired=targets[requestedTarget][b]*requestedAmount*.01;
            if(immediate){gain[b]=gainTarget[b]=desired;gainStep[b]=0;gainLeft[b]=0;coefficients[b]=bell(b,desired);}
            else if(desired!=gainTarget[b]){gainTarget[b]=desired;gainLeft[b]=rampLength;gainStep[b]=(desired-gain[b])/rampLength;}
        }
        // Keep the current coefficient interpolation uninterrupted. New gain
        // destinations are picked up at the next fixed sample quantum.
    }
    void rebuildTargets()noexcept{
        // Relative, deliberately modest curves. They are not artist profiles.
        constexpr double styles[5][8]{
            {0,0,0,0,0,0,0,0},
            {-1,-.5,-.8,-.3,.8,1.2,1,1.5},
            {-1.2,-.5,-1.8,-.6,.5,1.8,.7,.5},
            {-1.2,-.7,-1.5,-.5,.4,1.2,1.8,2.5},
            {-.8,-.2,-.8,0,.5,.2,-1.5,-2.5}};
        targets={};if(!profile.valid)return;
        for(int style=0;style<5;++style){double positive=0,negative=0;
            for(std::size_t b=0;b<bandCount;++b){
                if(!available[b]||!profile.validBands[b])continue;
                double local=0;
                // Retain the broad spectral slope: only interior local tonal
                // bulges/depressions are softened, never blindly flattened.
                if(b>=2&&b+1<bandCount&&profile.validBands[b-1]&&profile.validBands[b+1])
                    local=.65*((profile.bandDb[b-1]+profile.bandDb[b+1])*.5-profile.bandDb[b]);
                double limit=b<2?0:(b<6?3:2);
                if(profile.bandDb[b]<-32||profile.activeRmsDb+profile.bandDb[b]<-58)limit=0;
                const double value=std::clamp(local+styles[style][b],-6.,limit);targets[style][b]=value;
                if(value>0)positive+=value;else negative-=value;
            }
            // Each bell lies between 0 dB and its signed center gain. Bounding
            // each sum therefore bounds the WHOLE static cascade to +/-6 dB,
            // including frequencies between the nominal center points.
            const double scale=std::min(1.,6/std::max({6.,positive,negative}));
            for(auto& value:targets[style])value*=scale;
        }
    }
    void clearLearning()noexcept{fineAnalyzer.clear();histogram={};rmsHistogram={};measurableFrames={};framePower=framePeak=0;frameBands={};frameSamples=0;acceptedFrames=0;activeSamples=wallSamples=0;}
    static int bin(double value)noexcept{return std::clamp(static_cast<int>(std::lround((value+96)*2)),0,216);}
    double median(const std::array<std::uint32_t,217>& values)const noexcept{
        std::uint32_t total=0;const auto half=(acceptedFrames+1)/2;
        for(std::size_t i=0;i<values.size();++i){total+=values[i];if(total>=half)return static_cast<double>(i)*.5-96;}return -96;
    }
    void finishLearning()noexcept{
        LearnBalanceProfile next;next.valid=true;next.sampleRate=fs;next.activeRmsDb=static_cast<float>(median(rmsHistogram));int validCount=0;
        for(std::size_t b=0;b<bandCount;++b){next.bandDb[b]=static_cast<float>(std::clamp(median(histogram[b]),-96.,0.));next.validBands[b]=available[b]&&measurableFrames[b]*2>=acceptedFrames&&next.bandDb[b]>-45&&next.activeRmsDb+next.bandDb[b]>-72;if(next.validBands[b])++validCount;}
        next.fine=fineAnalyzer.profile();next.version=next.fine.frames>=10?2:1;
        if(validCount>=3){profile=next;fineStage.configure(next.fine,next.version==2,next.activeRmsDb,next.bandDb);fineStage.setAmount(requestedAmount);learnState=2;rebuildTargets();setGainTargets(false);}else learnState=3;
    }
    void observe(double power,double peak,const std::array<double,8>& bands,int count)noexcept{
        if(learnState!=1)return;
        ++wallSamples;++frameSamples;framePower+=power;framePeak=std::max(framePeak,peak);for(std::size_t b=0;b<bandCount;++b)frameBands[b]+=bands[b]/count;
        if(frameSamples>=frameLength){const double rms=db(framePower/frameSamples),peakDb=20*std::log10(std::max(1e-30,framePeak));
            if(rms>-60&&rms<=12&&peakDb<18&&peakDb-rms<20){
                ++acceptedFrames;activeSamples+=frameSamples;++rmsHistogram[static_cast<std::size_t>(bin(rms))];
                for(std::size_t b=0;b<bandCount;++b){const double relative=std::min(0.,db(frameBands[b]/std::max(1e-30,framePower)));++histogram[b][static_cast<std::size_t>(bin(relative))];if(available[b]&&relative>-45&&relative+rms>-72)++measurableFrames[b];}
            }
            frameSamples=0;framePower=framePeak=0;frameBands={};if(activeSamples>=fs*10)finishLearning();
        }
        if(learnState==1&&wallSamples>=fs*30)learnState=3;
    }
    double fs=48000,meterDecay=std::exp(-1/(48000*.2));int frameLength=960,rampLength=3840,frameSamples=0,learnState=0,quantumLeft=0;
    float requestedAmount=60;int requestedTarget=2;bool hasAudio=false;
    LearnBalanceProfile profile;
    FineBalanceAnalyzer fineAnalyzer;
    FineBalanceStage fineStage;
    double fullPower=0,detectorDecay=std::exp(-1/(48000*.012));
    std::array<double,8> detectorPower{};
    std::array<bool,8> available{true,true,true,true,true,true,true,true};
    std::array<double,8> cosine{},alpha{},gain{},gainTarget{},gainStep{},prePower{},postPower{},frameBands{};
    std::array<int,8> gainLeft{};
    std::array<Coeff,8> analysis{},coefficients{},coefficientStep{},quantumEnd{};
    std::array<std::array<State,8>,2> inputState{},outputState{},equalizerState{};
    std::array<float,8> preDb{},postDb{};
    std::array<std::array<double,8>,5> targets{};
    std::array<std::array<std::uint32_t,217>,8> histogram{};std::array<std::uint32_t,217> rmsHistogram{};std::array<std::uint32_t,8> measurableFrames{};
    std::uint32_t acceptedFrames=0;std::uint64_t activeSamples=0,wallSamples=0;double framePower=0,framePeak=0;
};
}
