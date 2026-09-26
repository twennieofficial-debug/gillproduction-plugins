#pragma once
#include "SmartDeEsserDSP.h"
#include <cstdint>

namespace gillsmart {
struct Profile {
    double frequency=6500, q=.8, threshold=-48, maximum=12, amount=55;
    double activeSeconds=0, voiceSeconds=0, sibilantSeconds=0;
    bool valid=false;
};
inline bool validProfile(const Profile& p) noexcept {
    return p.valid && std::isfinite(p.frequency) && p.frequency>=2500 && p.frequency<=12000
        && std::isfinite(p.q) && p.q>=.6 && p.q<=2.5
        && std::isfinite(p.threshold) && p.threshold>=-72 && p.threshold<=-6
        && std::isfinite(p.maximum) && p.maximum>=3 && p.maximum<=18
        && std::isfinite(p.amount) && p.amount>=0 && p.amount<=100;
}

// Fixed-size, sample-clocked analysis. The low voice band establishes context;
// high-band AR(2) innovation distinguishes noisy consonants from tonal energy.
// This is a signal heuristic, not a trained speech/S-phoneme classifier.
class Learner {
public:
    static constexpr int bandCount=9;
    inline static constexpr std::array<double,bandCount> centres{2500,3100,3800,4600,5600,6800,8200,10000,12000};
    void prepare(double rate) noexcept {
        fs=validSampleRate(rate);frameLength=std::max(1,static_cast<int>(std::lround(fs*.01)));
        powerDecay=std::exp(-1/(.002*fs));correlationDecay=std::exp(-1/(.008*fs));
        const double w=2*pi*220/fs,a=std::sin(w)/(2*.7),denom=1+a;
        voiceCoefficients={a/denom,-a/denom,-2*std::cos(w)/denom,(1-a)/denom};
        voiceLag=std::clamp(static_cast<int>(std::lround(fs/(4*220))),1,128);
        for(int b=0;b<bandCount;++b){coefficients[b]=designBand(centres[b],fs,1.3);lags[b]=std::clamp(static_cast<int>(std::lround(fs/(4*effectiveFrequency(centres[b],fs)))),1,128);}
        reset();
    }
    void reset() noexcept {
        for(auto& channel:filters)for(auto& filter:channel)filter.reset();
        for(auto& channel:detectors)for(auto& detector:channel)detector.reset();
        for(auto& filter:voiceFilters)filter.reset();for(auto& detector:voiceDetectors)detector.reset();
        energy.fill(0);histogram.fill(0);framePower=frameVoice=0;frameBands.fill(0);
        framePosition=0;voiceFrames=sibilantFrames=activeFrames=frames=0;lastVoice=-1000;
    }
    template<class T> void process(const T* const* data,int channels,int samples) noexcept {
        if(!data||channels<1||samples<1||!data[0])return;
        channels=std::clamp(channels,1,2);
        for(int n=0;n<samples;++n){
            if(elapsedSeconds()>=300.0)break;
            for(int c=0;c<channels;++c){
                const double raw=data[c]?static_cast<double>(data[c][n]):0;
                const double x=std::isfinite(raw)?std::clamp(raw,-16.,16.):0;
                const double v=voiceFilters[c].process(x,voiceCoefficients);
                voiceDetectors[c].update(x,v,voiceLag,powerDecay,correlationDecay);
                framePower+=x*x/channels;frameVoice+=v*v/channels;
                for(int b=0;b<bandCount;++b){const double y=filters[c][b].process(x,coefficients[b]);detectors[c][b].update(x,y,lags[b],powerDecay,correlationDecay);frameBands[b]+=y*y/channels;}
            }
            if(++framePosition==frameLength)finishFrame(channels);
        }
    }
    double activeSeconds() const noexcept{return activeFrames*frameLength/fs;}
    double elapsedSeconds() const noexcept{return frames*frameLength/fs;}
    bool shouldFinish(bool fullSong=false) const noexcept{return fullSong?elapsedSeconds()>=300.0:(activeSeconds()>=8 || elapsedSeconds()>=30);}
    Profile result() const noexcept {
        Profile p;p.activeSeconds=activeSeconds();p.voiceSeconds=voiceFrames*frameLength/fs;p.sibilantSeconds=sibilantFrames*frameLength/fs;
        if(p.activeSeconds<4 || p.voiceSeconds<2.5 || p.sibilantSeconds<.18)return p;
        double sum=0,logHz=0;
        for(int b=0;b<bandCount;++b)if(centres[b]<fs*.43){const double e=energy[b]*energy[b];sum+=e;logHz+=e*std::log(centres[b]);}
        if(sum<1e-16)return p;
        p.frequency=std::clamp(std::exp(logHz/sum),2500.,std::min(12000.,fs*.43));
        double variance=0;for(int b=0;b<bandCount;++b)if(centres[b]<fs*.43)variance+=energy[b]*energy[b]*std::pow(std::log(centres[b]/p.frequency),2)/sum;
        p.q=std::clamp(.5/std::max(.24,std::sqrt(variance)),.7,1.8);
        const auto percentile=[&](double fraction){double total=0;for(auto count:histogram)total+=count;double seen=0;for(int i=0;i<80;++i){seen+=histogram[i];if(seen>=fraction*total)return i-80.;}return -30.;};
        const double quiet=percentile(.2),loud=percentile(.8);
        p.threshold=std::clamp(quiet-5,-65.,-10.);
        p.maximum=std::clamp(8+(loud-quiet)*.4,6.,16.);
        p.amount=std::clamp(55+(loud+30)*.5,45.,75.);
        p.valid=true;return p;
    }
private:
    void finishFrame(int channels) noexcept {
        const double power=framePower/frameLength;const bool active=power>1e-5;
        double voiceNoise=0,voiceWeight=0;
        for(int c=0;c<channels;++c){const double w=voiceDetectors[c].bandPower;voiceNoise+=w*voiceDetectors[c].noisiness();voiceWeight+=w;}
        if(active){++activeFrames;const bool voiced=frameVoice/std::max(1e-20,framePower)>.10 && voiceNoise/std::max(1e-20,voiceWeight)<.30;
            if(voiced){++voiceFrames;lastVoice=frames;}
            double loudest=0;int selected=0;
            for(int b=0;b<bandCount;++b)if(centres[b]<fs*.43 && frameBands[b]>loudest){loudest=frameBands[b];selected=b;}
            double noise=0,weight=0;for(int c=0;c<channels;++c){const double w=detectors[c][selected].bandPower;noise+=w*detectors[c][selected].noisiness();weight+=w;}
            if(frames-lastVoice<=35 && loudest/std::max(1e-20,framePower)>.20 && noise/std::max(1e-20,weight)>.045){
                ++sibilantFrames;for(int b=0;b<bandCount;++b)energy[b]+=frameBands[b]/frameLength;
                const double db=10*std::log10(std::max(1e-16,loudest/frameLength));++histogram[std::clamp(static_cast<int>(std::floor(db+80)),0,79)];
            }
        }
        ++frames;framePosition=0;framePower=frameVoice=0;frameBands.fill(0);
    }
    double fs=48000,powerDecay=.99,correlationDecay=.999,framePower=0,frameVoice=0;
    int frameLength=480,framePosition=0,voiceLag=55,voiceFrames=0,sibilantFrames=0,activeFrames=0,frames=0,lastVoice=-1000;
    BandCoefficients voiceCoefficients;std::array<BandCoefficients,bandCount>coefficients{};std::array<int,bandCount>lags{};
    std::array<detail::Biquad,2>voiceFilters;std::array<detail::Detector,2>voiceDetectors;
    std::array<std::array<detail::Biquad,bandCount>,2>filters;
    std::array<std::array<detail::Detector,bandCount>,2>detectors;
    std::array<double,bandCount>energy{},frameBands{};std::array<std::uint32_t,80>histogram{};
};
}
