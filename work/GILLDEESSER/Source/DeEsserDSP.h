#pragma once
// Original GILLDEESSER implementation. The public RBJ constant-peak band-pass
// equations are documented at https://www.w3.org/TR/audio-eq-cookbook/ .
// No commercial de-esser code or trained model is included.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace gilldeesser {
constexpr double pi=3.1415926535897932384626433832795;
constexpr double bandQ=0.8;
inline double validSampleRate(double fs) noexcept {return std::isfinite(fs)&&fs>=8000&&fs<=768000?fs:48000.0;}
inline double clampFinite(double x,double lo,double hi,double fallback) noexcept {return std::isfinite(x)?std::clamp(x,lo,hi):fallback;}
inline double effectiveFrequency(double hz,double fs) noexcept {return clampFinite(hz,2500.0,std::min(12000.0,.45*validSampleRate(fs)),std::min(6500.0,.45*validSampleRate(fs)));}
inline std::array<double,2> bandEdges(double hz,double fs) noexcept {
    fs=validSampleRate(fs);hz=effectiveFrequency(hz,fs);
    const double warped=std::tan(pi*hz/fs),root=std::sqrt(4*bandQ*bandQ+1);
    return {fs/pi*std::atan(warped*(root-1)/(2*bandQ)),fs/pi*std::atan(warped*(root+1)/(2*bandQ))};
}
struct BandCoefficients {double b0=0,b2=0,a1=0,a2=0;};
inline BandCoefficients designBand(double hz,double fs) noexcept {
    fs=validSampleRate(fs);hz=effectiveFrequency(hz,fs);
    const double w=2*pi*hz/fs,alpha=std::sin(w)/(2*bandQ),a0=1+alpha;
    return {alpha/a0,-alpha/a0,-2*std::cos(w)/a0,(1-alpha)/a0};
}
inline std::complex<double> bandResponse(double hz,double fs,double probeHz) noexcept {
    fs=validSampleRate(fs);const auto c=designBand(hz,fs);
    const auto z=std::polar(1.0,-2*pi*clampFinite(probeHz,0,fs*.5,0)/fs);
    return (c.b0+c.b2*z*z)/(1.0+c.a1*z+c.a2*z*z);
}
inline std::complex<double> processedResponse(double hz,double fs,double probeHz,double reductionDb) noexcept {
    const double attenuation=1-std::pow(10.0,-clampFinite(reductionDb,0,12,0)/20.0);
    return 1.0-attenuation*bandResponse(hz,fs,probeHz);
}
namespace detail {
struct Biquad {
    double x1=0,x2=0,y1=0,y2=0;
    void reset() noexcept {x1=x2=y1=y2=0;}
    double process(double x,const BandCoefficients& c) noexcept {
        double y=c.b0*x+c.b2*x2-c.a1*y1-c.a2*y2;
        if(!std::isfinite(y)){reset();return 0;}
        if(std::abs(y)<1e-30)y=0;
        x2=x1;x1=x;y2=y1;y1=y;return y;
    }
};
struct Ramp {
    double value=0,target=0,step=0;int remaining=0;
    void reset(double v) noexcept {value=target=v;step=0;remaining=0;}
    void set(double v,int samples) noexcept {if(v!=target){target=v;remaining=samples;step=(target-value)/samples;}}
    bool next() noexcept {if(remaining<=0)return false;if(--remaining==0)value=target;else value+=step;return true;}
};
struct Detector {
    std::array<double,256> history{};
    std::size_t cursor=0;
    int activeSamples=0;
    double fullPower=0,bandPower=0,correlationPower=0,lag1=0,lag2=0;
    void reset() noexcept {*this={};}
    void update(double input,double band,int lag,double powerDecay,double correlationDecay) noexcept {
        const double previous1=history[(cursor+256-static_cast<std::size_t>(lag))%256];
        const double previous2=history[(cursor+256-static_cast<std::size_t>(2*lag))%256];
        history[cursor]=band;cursor=(cursor+1)%256;
        fullPower=powerDecay*fullPower+(1-powerDecay)*input*input;
        bandPower=powerDecay*bandPower+(1-powerDecay)*band*band;
        activeSamples=bandPower>1e-12?std::min(activeSamples+1,1000000):0;
        correlationPower=correlationDecay*correlationPower+(1-correlationDecay)*band*band;
        lag1=correlationDecay*lag1+(1-correlationDecay)*band*previous1;
        lag2=correlationDecay*lag2+(1-correlationDecay)*band*previous2;
    }
    double noisiness() const noexcept {
        if(correlationPower<1e-24)return 0;
        const double r1=std::clamp(lag1/correlationPower,-.9999,.9999),r2=std::clamp(lag2/correlationPower,-1.0,1.0);
        const double denominator=std::max(1e-5,1-r1*r1);
        const double a1=r1*(1-r2)/denominator,a2=(r2-r1*r1)/denominator;
        // Normalized AR(2) prediction innovation: a sustained sinusoid is
        // predictable, while a noisy consonant has substantial innovation.
        // This is a heuristic, not a speech recognizer or an S-label certainty.
        return std::clamp(1-a1*r1-a2*r2,0.0,1.0);
    }
};
inline double smoothStep(double value) noexcept {value=std::clamp(value,0.0,1.0);return value*value*(3-2*value);}
}

class DeEsserEngine {
public:
    void prepare(double fs) noexcept {
        sampleRate=validSampleRate(fs);parameterRampSamples=std::max(1,static_cast<int>(std::lround(.015*sampleRate)));
        powerDecay=std::exp(-1.0/(.002*sampleRate));correlationDecay=std::exp(-1.0/(.008*sampleRate));
        attackDecay=std::exp(-1.0/(.0008*sampleRate));releaseDecay=std::exp(-1.0/(.055*sampleRate));
        reset();
    }
    void reset() noexcept {
        for(auto& b:bands)b.reset();for(auto& d:detectors)d.reset();
        amount.reset(requestedAmount);logFrequency.reset(std::log(effectiveFrequency(requestedFrequency,sampleRate)));
        coefficients=designBand(std::exp(logFrequency.value),sampleRate);coefficientCountdown=0;
        updatePredictorLag();
        reduction=0;sibilanceDb=-160;processed=false;frequencyDirty=false;
    }
    void setAmount(double value) noexcept {
        requestedAmount=clampFinite(value,0,1,.55);
        if(processed)amount.set(requestedAmount,parameterRampSamples);else amount.reset(requestedAmount);
    }
    void setFrequency(double hz) noexcept {
        requestedFrequency=clampFinite(hz,2500,12000,6500);
        const double target=std::log(effectiveFrequency(requestedFrequency,sampleRate));
        if(processed)logFrequency.set(target,parameterRampSamples);else {logFrequency.reset(target);coefficients=designBand(std::exp(target),sampleRate);updatePredictorLag();}
    }
    void process(float** channels,int nChannels,int nSamples,float** listenBand=nullptr) noexcept {processImpl(channels,nChannels,nSamples,listenBand);}
    void process(double** channels,int nChannels,int nSamples,double** listenBand=nullptr) noexcept {processImpl(channels,nChannels,nSamples,listenBand);}
    double getReductionDb() const noexcept {return amount.value*reduction;}
    double getSibilanceDb() const noexcept {return sibilanceDb;}
    double getFrequency() const noexcept {return std::exp(logFrequency.value);}
    double getSampleRate() const noexcept {return sampleRate;}
    int getLatencySamples() const noexcept {return 0;}
    // Conservative upper bound for the audio band-pass tail across supported
    // rates. Detector/release state does not itself synthesize an audio tail.
    double getTailLengthSeconds() const noexcept {return .05;}
private:
    double sampleRate=48000,requestedAmount=.55,requestedFrequency=6500;
    int parameterRampSamples=720,lag=2,coefficientCountdown=0;
    bool processed=false,frequencyDirty=false;
    detail::Ramp amount{.55,.55,0,0},logFrequency{std::log(6500.0),std::log(6500.0),0,0};
    BandCoefficients coefficients=designBand(6500,48000);
    std::array<detail::Biquad,2> bands{};
    std::array<detail::Detector,2> detectors{};
    double powerDecay=std::exp(-1.0/96),correlationDecay=std::exp(-1.0/384);
    double attackDecay=std::exp(-1.0/38.4),releaseDecay=std::exp(-1.0/2640);
    double reduction=0,sibilanceDb=-160;
    void updatePredictorLag() noexcept {
        // Approximately a quarter-period spacing makes innovation comparable
        // across detector centres and sample rates. A one-sample predictor at
        // a high sample rate would call even filtered noise nearly tonal.
        lag=std::clamp(static_cast<int>(std::lround(sampleRate/(4*std::exp(logFrequency.value)))),1,128);
    }
    template<class Sample> static Sample safeCast(double value) noexcept {
        if(!std::isfinite(value))return Sample{};const auto cast=static_cast<Sample>(value);return std::isfinite(static_cast<double>(cast))?cast:Sample{};
    }
    double targetFor(const detail::Detector& d) const noexcept {
        // After silence, collect enough history before classifying a new
        // high-frequency onset. Otherwise an initially empty predictor calls
        // the first cycles of a clean tone noise, and the release keeps a
        // spurious reduction audible well after its start.
        if(d.activeSamples<static_cast<int>(std::ceil(.008*sampleRate)))return 0;
        const double ratio=d.bandPower/std::max(1e-24,d.fullPower);
        const double threshold=.18-.11*amount.value;
        const double spectral=detail::smoothStep((ratio-threshold)/(.42-threshold));
        const double noise=detail::smoothStep((d.noisiness()-.015)/.16);
        const double level=10*std::log10(std::max(1e-16,d.bandPower));
        const double audible=detail::smoothStep((level+72)/22);
        return 12*spectral*noise*audible;
    }
    template<class Sample> void processImpl(Sample** channels,int nChannels,int nSamples,Sample** listenBand) noexcept {
        if(!channels||nChannels<1||nSamples<=0||!channels[0])return;
        const bool stereo=nChannels>1&&channels[1]!=nullptr;const int channelCount=stereo?2:1;processed=true;
        for(int n=0;n<nSamples;++n) {
            amount.next();frequencyDirty=logFrequency.next()||frequencyDirty;
            if(--coefficientCountdown<=0){coefficientCountdown=8;if(frequencyDirty){coefficients=designBand(std::exp(logFrequency.value),sampleRate);updatePredictorLag();frequencyDirty=false;}}
            std::array<double,2> input{},selected{};double target=0,bandPower=0;
            for(int c=0;c<channelCount;++c) {
                const double raw=static_cast<double>(channels[c][n]);input[c]=std::isfinite(raw)&&std::abs(raw)<=1e100?raw:0;
                selected[c]=bands[c].process(input[c],coefficients);
                detectors[c].update(input[c],selected[c],lag,powerDecay,correlationDecay);
                target=std::max(target,targetFor(detectors[c]));bandPower=std::max(bandPower,detectors[c].bandPower);
            }
            sibilanceDb=10*std::log10(std::max(1e-16,bandPower));
            const double decay=target>reduction?attackDecay:releaseDecay;
            reduction=std::clamp(target+decay*(reduction-target),0.0,12.0);
            const double attenuation=amount.value==0?0:1-std::pow(10.0,-amount.value*reduction/20);
            for(int c=0;c<channelCount;++c) {
                // The residual split has exact unity reconstruction at a=0.
                // For fixed 0<=a<=1, Re(H)=|H|^2 for this band-pass, so
                // |1-aH|^2=1-a(2-a)|H|^2 <=1: no static out-of-band boost.
                const double output=attenuation==0?input[c]:input[c]-attenuation*selected[c];
                if(listenBand&&listenBand[c])listenBand[c][n]=safeCast<Sample>(selected[c]);
                channels[c][n]=safeCast<Sample>(output);
            }
        }
    }
};
}
