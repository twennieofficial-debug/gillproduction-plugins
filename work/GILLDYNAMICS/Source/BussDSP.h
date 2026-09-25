#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace gilldyn {
struct BussParameters { float driveDb=0,trimDb=0;int style=0;bool noise=false,bypass=false; };
namespace buss_detail {
constexpr double pi=3.1415926535897932384626433832795;
inline double finite(double x,double fallback=0) noexcept{return std::isfinite(x)?x:fallback;}
inline double quiet(double x) noexcept{return std::abs(x)<1e-30?0:x;}
template<int length> inline double dot(const double* samples,const double* coefficients) noexcept {
#if defined(_M_X64) || defined(__SSE2__)
    __m128d a=_mm_setzero_pd(),b=a,c=a,d=a;int i=0;
    for(;i+7<length;i+=8){
        a=_mm_add_pd(a,_mm_mul_pd(_mm_loadu_pd(samples+i),_mm_loadu_pd(coefficients+i)));
        b=_mm_add_pd(b,_mm_mul_pd(_mm_loadu_pd(samples+i+2),_mm_loadu_pd(coefficients+i+2)));
        c=_mm_add_pd(c,_mm_mul_pd(_mm_loadu_pd(samples+i+4),_mm_loadu_pd(coefficients+i+4)));
        d=_mm_add_pd(d,_mm_mul_pd(_mm_loadu_pd(samples+i+6),_mm_loadu_pd(coefficients+i+6)));
    }
    a=_mm_add_pd(_mm_add_pd(a,b),_mm_add_pd(c,d));
    double result=_mm_cvtsd_f64(_mm_add_sd(a,_mm_unpackhi_pd(a,a)));
    for(;i<length;++i)result+=samples[i]*coefficients[i];
    return result;
#else
    double result=0;for(int i=0;i<length;++i)result+=samples[i]*coefficients[i];return result;
#endif
}
inline double knee(double x) noexcept {
    if(x>=3)return 1;if(x<=-3)return -1;
    const double s=x*x;return x*(27+s)/(27+9*s);
}
inline double shape(double x,int mode) noexcept {
    if(mode==0)return x/std::sqrt(1+.16*x*x); // CLEAN: broad, gentle odd knee.
    if(mode==2)return x/std::sqrt(1+x*x);    // VELVET: stronger odd soft knee.
    constexpr double bias=.18,denom=27+9*bias*bias;
    constexpr double zero=bias*(27+bias*bias)/denom;
    constexpr double slope=((27+3*bias*bias)*denom-bias*(27+bias*bias)*18*bias)/(denom*denom);
    return (knee(x+bias)-zero)/slope;        // IRON: calibrated asymmetric knee.
}
inline double driveGain(double driveDb) noexcept {
    // Full branch reaches 24 dB. Blend it over the entire visible 0..24 dB
    // control to avoid an effectively inactive beginning on typical buses.
    return std::pow(10.,(12.+.5*std::clamp(driveDb,0.,24.))/20.);
}
inline double normalisation(double gain,int mode) noexcept {
    constexpr double reference=.12;
    if(mode==0)return gain/std::sqrt(1+.16*reference*reference*gain*gain);
    if(mode==2)return gain/std::sqrt(1+reference*reference*gain*gain);
    const double x=reference*gain;
    return (shape(x,1)-shape(-x,1))/(2*reference);
}
}

// Original console-coloring engine; the styles are descriptive tonal choices,
// not models of a particular console. The wrapper owns any cross-instance
// group sharing and supplies already combined parameter values.
// No limiter: TRIM preserves floating-point headroom above 0 dBFS.
// All parameter/state mutation must be synchronized to the audio thread.
class BussDSP {
public:
    static constexpr int maximumChannels=8;
    static constexpr int oversamplingFactor=8;
    static constexpr int filterTaps=193;
    static constexpr int fixedLatencySamples=24;
    void prepare(double fs,int maxBlock,int channels) noexcept {
        (void)maxBlock;
        fs_=std::clamp(buss_detail::finite(fs,48000),8000.,384000.);
        channels_=std::clamp(channels,1,maximumChannels);
        smoothing_=1-std::exp(-1/(.015*fs_));
        dcPole_=std::exp(-2*buss_detail::pi*8/(fs_*(liveMode_?1:oversamplingFactor)));
        humIncrement_=2*buss_detail::pi*50/fs_;
        makeFilter();prepared_=true;reset();
    }
    void setParameters(const BussParameters& p) noexcept {
        const double drive=std::clamp(buss_detail::finite(p.driveDb),0.,24.);
        targetGain_=buss_detail::driveGain(drive);targetAmount_=drive/24.;
        targetTrim_=std::pow(10.,std::clamp(buss_detail::finite(p.trimDb),-36.,24.)/20.);
        targetStyle_.fill(0);targetStyle_[std::clamp(p.style,0,2)]=1;
        targetNoise_=p.noise?1:0;targetActive_=p.bypass?0:1;
    }
    void reset() noexcept {
        for(int ch=0;ch<maximumChannels;++ch){
            auto& s=state_[ch];s.input.fill(0);s.residual.fill(0);s.dry.fill(0);
            s.inputPosition=s.residualPosition=s.dryPosition=0;s.previousResidual=s.dcState=0;
            s.noiseState=std::uint32_t(5791+ch*1039);
        }
        gain_=targetGain_;amount_=targetAmount_;trim_=targetTrim_;style_=targetStyle_;
        noise_=targetNoise_;active_=targetActive_;humPhase_=0;
    }
    void setLiveMode(bool live) noexcept {
        if (liveMode_ == live) return;
        liveMode_ = live;
        dcPole_ = std::exp(-2*buss_detail::pi*8/(fs_*(liveMode_?1:oversamplingFactor)));
        reset();
    }
    int latencySamples() const noexcept{return liveMode_?0:fixedLatencySamples;}
    void process(float* const* buffers,int frames,int channels) noexcept {
        if(!buffers||frames<=0||channels<=0)return;
        if(!prepared_)prepare(48000,frames,channels);
        const int count=std::min({channels,channels_,maximumChannels});
        for(int i=0;i<frames;++i){
            smooth(gain_,targetGain_);smooth(amount_,targetAmount_);smooth(trim_,targetTrim_);
            smooth(noise_,targetNoise_);smooth(active_,targetActive_);
            std::array<double,3> wetScale{};
            for(int mode=0;mode<3;++mode){
                smooth(style_[mode],targetStyle_[mode]);
                wetScale[mode]=style_[mode]/buss_detail::normalisation(gain_,mode);
            }
            const int mode=style_[0]==1?0:style_[1]==1?1:style_[2]==1?2:-1;
            const double hum=noise_>0?std::sin(humPhase_)*.000003*noise_:0;
            for(int ch=0;ch<count;++ch){
                auto& s=state_[ch];
                const float x=buffers[ch]?float(std::clamp(buss_detail::finite(buffers[ch][i]),-32.,32.)):0;
                const double dry=liveMode_?x:s.dry[s.dryPosition];s.dry[s.dryPosition]=x;
                s.dryPosition=(s.dryPosition+1)%fixedLatencySamples;
                s.input[s.inputPosition]=s.input[s.inputPosition+inputHistory]=x;
                double residualOutput=0;
                for(int phase=0;phase<(liveMode_?1:oversamplingFactor);++phase){
                    const double up=liveMode_?x:buss_detail::dot<inputHistory>(s.input.data()+s.inputPosition,interpolation_[phase].data());
                    double residual=0;
                    if(amount_>0){
                        const double driven=up*gain_;
                        double shaped=0;
                        if(mode>=0)shaped=buss_detail::shape(driven,mode)*wetScale[mode];
                        else for(int k=0;k<3;++k)if(style_[k]>0)shaped+=buss_detail::shape(driven,k)*wetScale[k];
                        residual=amount_*(shaped-up);
                    }
                    const double dc=residual-s.previousResidual+dcPole_*s.dcState;
                    s.previousResidual=residual;s.dcState=buss_detail::quiet(dc);
                    s.residual[s.residualPosition]=s.residual[s.residualPosition+filterTaps]=s.dcState;
                    if(phase==0)residualOutput=liveMode_?s.dcState:buss_detail::dot<filterTaps>(s.residual.data()+s.residualPosition,filter_.data());
                    if(--s.residualPosition<0)s.residualPosition=filterTaps-1;
                }
                if(--s.inputPosition<0)s.inputPosition=inputHistory-1;
                double noise=0;
                if(noise_>0){
                    auto& random=s.noiseState;random^=random<<13;random^=random>>17;random^=random<<5;
                    noise=(double(random)/4294967295.-.5)*.000032*noise_+hum;
                }
                const double wet=(dry+residualOutput+noise)*trim_;
                // Exactly dry at a settled bypass, including trim/noise changes.
                // Input sanitization bounds fault energy; no normal output clip.
                const double output=dry+active_*(wet-dry);
                if(buffers[ch])buffers[ch][i]=float(buss_detail::finite(output));
            }
            humPhase_+=humIncrement_;if(humPhase_>=2*buss_detail::pi)humPhase_-=2*buss_detail::pi;
        }
    }
private:
    bool liveMode_=false;
    static constexpr int inputHistory=(filterTaps+oversamplingFactor-1)/oversamplingFactor;
    struct State {
        std::array<double,inputHistory*2> input{};
        std::array<double,filterTaps*2> residual{};
        std::array<float,fixedLatencySamples> dry{};
        int inputPosition=0,residualPosition=0,dryPosition=0;
        double previousResidual=0,dcState=0;
        std::uint32_t noiseState=5791;
    };
    void smooth(double& value,double target) const noexcept {
        value=buss_detail::quiet(value+smoothing_*(target-value));
        if(std::abs(value-target)<1e-12)value=target;
    }
    void makeFilter() noexcept {
        constexpr double cutoff=.45/oversamplingFactor;
        constexpr int center=(filterTaps-1)/2;
        double sum=0;
        for(int tap=0;tap<filterTaps;++tap){
            const double n=tap-center;
            const double sinc=tap==center?2*cutoff:std::sin(2*buss_detail::pi*cutoff*n)/(buss_detail::pi*n);
            const double angle=2*buss_detail::pi*tap/(filterTaps-1);
            const double window=.42-.5*std::cos(angle)+.08*std::cos(2*angle);
            filter_[tap]=sinc*window;sum+=filter_[tap];
        }
        for(auto& c:filter_)c/=sum;
        for(int phase=0;phase<oversamplingFactor;++phase)for(int n=0;n<inputHistory;++n){
            const int tap=phase+n*oversamplingFactor;
            interpolation_[phase][n]=tap<filterTaps?filter_[tap]*oversamplingFactor:0;
        }
    }
    std::array<State,maximumChannels> state_{};
    std::array<double,filterTaps> filter_{};
    std::array<std::array<double,inputHistory>,oversamplingFactor> interpolation_{};
    std::array<double,3> style_{{1,0,0}},targetStyle_{{1,0,0}};
    double fs_=48000,smoothing_=0,dcPole_=0,humPhase_=0,humIncrement_=0;
    double gain_=3.9810717055349722,targetGain_=3.9810717055349722,amount_=0,targetAmount_=0;
    double trim_=1,targetTrim_=1,noise_=0,targetNoise_=0,active_=1,targetActive_=1;
    int channels_=2;bool prepared_=false;
};
} // namespace gilldyn

