#pragma once
// Original four-band parallel dynamics. No proprietary algorithms or source.
// Three ordered TPT one-pole lowpasses form complementary difference bands:
// L1, L2-L1, L3-L2, x-L3. They sum to the input, including phase. Slopes are
// deliberately gentle (6 dB/octave), not steep Linkwitz-Riley crossovers.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace gilldyn {
struct QuadBand {
    float thresholdDb=-24.f,rangeDb=0.f,gainDb=0.f,attackMs=12.f,releaseMs=140.f;
    bool solo=false,bypass=false;
};
struct QuadParameters {
    std::array<QuadBand,4> bands{};
    std::array<float,3> crossoversHz{{180.f,1400.f,6000.f}};
    float outputDb=0.f;
};

class QuadDSP {
public:
    void prepare(double sampleRate,int maxBlock,int channels) noexcept {
        (void)maxBlock;(void)channels;
        fs=std::isfinite(sampleRate)?std::clamp(sampleRate,8000.,192000.):48000.;
        parameterPole=std::exp(-1./(.008*fs));
        powerPole=std::exp(-1./(.003*fs));
        setParameters(parameters);reset();prepared=true;
    }
    void setParameters(const QuadParameters& value) noexcept {
        parameters=value;
        for(auto& b:parameters.bands){
            b.thresholdDb=valid(b.thresholdDb,-60.f,0.f,-24.f);
            b.rangeDb=valid(b.rangeDb,-24.f,12.f,0.f);
            b.gainDb=valid(b.gainDb,-12.f,12.f,0.f);
            b.attackMs=valid(b.attackMs,1.f,200.f,12.f);
            b.releaseMs=valid(b.releaseMs,10.f,1000.f,140.f);
        }
        parameters.outputDb=valid(parameters.outputDb,-18.f,12.f,0.f);
        auto& c=parameters.crossoversHz;
        const float top=static_cast<float>(.45*fs),spacing=1.35f;
        for(size_t i=0;i<c.size();++i)c[i]=valid(c[i],20.f,top,defaults[i]);
        std::sort(c.begin(),c.end());
        c[0]=std::min(c[0],top/(spacing*spacing));
        c[1]=std::clamp(c[1],c[0]*spacing,top/spacing);
        c[2]=std::clamp(c[2],c[1]*spacing,top);
        for(size_t i=0;i<3;++i){const double g=std::tan(pi*c[i]/fs);filterTarget[i]=g/(1.+g);}
        bool anySolo=false;for(const auto& b:parameters.bands)anySolo=anySolo||b.solo;
        for(size_t i=0;i<4;++i){
            attackPole[i]=std::exp(-1./(.001*parameters.bands[i].attackMs*fs));
            releasePole[i]=std::exp(-1./(.001*parameters.bands[i].releaseMs*fs));
            soloTarget[i]=anySolo&&!parameters.bands[i].solo?0.:1.;
            bypassTarget[i]=parameters.bands[i].bypass?1.:0.;
        }
        if(!started)snapParameters();
    }
    void reset() noexcept {
        for(auto& channel:filterState)channel.fill(0.);
        power.fill(0.);dynamicDb.fill(0.);desiredDb.fill(0.);
        controlCounter=0;lastChannels=0;started=false;snapParameters();
    }
    int latencySamples() const noexcept { return 0; }
    std::array<float,4> bandReductionDb() const noexcept {
        // Positive values denote attenuation; negative values denote expansion.
        // Bypass morph is included, static band trim and SOLO mute are excluded.
        std::array<float,4> result{};
        for(size_t i=0;i<4;++i){const double g=bypassWeight[i]+(1.-bypassWeight[i])*toGain(dynamicDb[i]);result[i]=static_cast<float>(-20.*std::log10(std::max(1.e-12,g)));}
        return result;
    }
    std::array<float,4> bandGainDb() const noexcept {
        std::array<float,4> result{};
        for(size_t i=0;i<4;++i)result[i]=static_cast<float>(20.*std::log10(std::max(1.e-6,appliedGain[i])));
        return result;
    }
    std::array<float,3> crossoverHz() const noexcept { return parameters.crossoversHz; }
    float responseDb(double hz) const noexcept {
        if(!std::isfinite(hz))return 0.f;
        hz=std::clamp(hz,0.,fs*.5);
        const std::complex<double> z=std::polar(1.,-2.*pi*hz/fs);
        std::array<std::complex<double>,3> low{};
        for(size_t i=0;i<3;++i)low[i]=filterCoefficient[i]*(1.+z)/(1.+(2.*filterCoefficient[i]-1.)*z);
        const std::array<std::complex<double>,4> band{{low[0],low[1]-low[0],low[2]-low[1],1.-low[2]}};
        std::complex<double> response=1.;
        for(size_t i=0;i<4;++i)response+=(appliedGain[i]-1.)*band[i];
        return static_cast<float>(20.*std::log10(std::max(1.e-9,std::abs(response)*toGain(outputDb))));
    }
    void process(float* const* data,int frames,int channels) noexcept {
        if(!data||frames<=0||channels<=0)return;
        const int count=std::min(channels,2);
        if(!prepared)return;
        if(lastChannels!=0&&lastChannels!=count)reset();lastChannels=count;started=true;
        for(int n=0;n<frames;++n){
            for(size_t i=0;i<3;++i)filterCoefficient[i]=smooth(filterCoefficient[i],filterTarget[i],parameterPole);
            outputDb=smooth(outputDb,parameters.outputDb,parameterPole);
            std::array<std::array<double,4>,2> bands{};
            std::array<double,2> dry{};
            for(int ch=0;ch<count;++ch){
                const auto c=static_cast<size_t>(ch);if(!data[ch])continue;
                dry[c]=sanitize(data[ch][n]);std::array<double,3> low{};
                for(size_t i=0;i<3;++i){
                    const double v=(dry[c]-filterState[c][i])*filterCoefficient[i];
                    low[i]=v+filterState[c][i];filterState[c][i]=tiny(low[i]+v);
                }
                bands[c]={{low[0],low[1]-low[0],low[2]-low[1],dry[c]-low[2]}};
            }
            for(size_t i=0;i<4;++i){
                // Shared maximum channel power preserves stereo image.
                double energy=0.;for(int ch=0;ch<count;++ch)energy=std::max(energy,bands[static_cast<size_t>(ch)][i]*bands[static_cast<size_t>(ch)][i]);
                power[i]=tiny(smooth(power[i],energy,powerPole));
                threshold[i]=smooth(threshold[i],parameters.bands[i].thresholdDb,parameterPole);
                range[i]=smooth(range[i],parameters.bands[i].rangeDb,parameterPole);
                gainDb[i]=smooth(gainDb[i],parameters.bands[i].gainDb,parameterPole);
                soloWeight[i]=smooth(soloWeight[i],soloTarget[i],parameterPole);
                bypassWeight[i]=smooth(bypassWeight[i],bypassTarget[i],parameterPole);
                if((controlCounter&7U)==0){
                    const double above=10.*std::log10(std::max(1.e-24,power[i]))-threshold[i];
                    // Six-dB soft knee; negative RANGE approaches 4:1 compression,
                    // positive RANGE approaches 1.5:1 upward expansion above
                    // threshold. The finite RANGE is approached asymptotically.
                    const double excess=above<=-3.?0.:above>=3.?above:(above+3.)*(above+3.)/12.;
                    const double limit=std::abs(range[i]),slope=range[i]<0.?.75:.5;
                    desiredDb[i]=limit<1.e-9?0.:std::copysign(limit*(-std::expm1(-slope*excess/limit)),range[i]);
                }
                const double pole=std::abs(desiredDb[i])>std::abs(dynamicDb[i])?attackPole[i]:releasePole[i];
                dynamicDb[i]=tiny(smooth(dynamicDb[i],desiredDb[i],pole));
                appliedGain[i]=soloWeight[i]*(bypassWeight[i]+(1.-bypassWeight[i])*toGain(gainDb[i]+dynamicDb[i]));
            }
            const double outputGain=toGain(outputDb);
            for(int ch=0;ch<count;++ch){
                if(!data[ch])continue;const auto c=static_cast<size_t>(ch);double y=dry[c];
                // The residual form makes neutral settings bit-exact, despite
                // round-off in the internally complementary band decomposition.
                for(size_t i=0;i<4;++i)y+=(appliedGain[i]-1.)*bands[c][i];
                data[ch][n]=static_cast<float>(sanitize(y*outputGain));
            }
            ++controlCounter;
        }
    }
private:
    static constexpr double pi=3.1415926535897932384626433832795;
    static constexpr std::array<float,3> defaults{{180.f,1400.f,6000.f}};
    static float valid(float x,float low,float high,float fallback) noexcept {return std::clamp(std::isfinite(x)?x:fallback,low,high);}
    static double sanitize(double x) noexcept {return std::isfinite(x)?std::clamp(x,-1.e12,1.e12):0.;}
    static double tiny(double x) noexcept {return std::abs(x)<1.e-30?0.:x;}
    static double toGain(double db) noexcept {return std::pow(10.,db*.05);}
    static double smooth(double current,double target,double pole) noexcept {return target+(current-target)*pole;}
    void snapParameters() noexcept {
        filterCoefficient=filterTarget;outputDb=parameters.outputDb;
        for(size_t i=0;i<4;++i){threshold[i]=parameters.bands[i].thresholdDb;range[i]=parameters.bands[i].rangeDb;gainDb[i]=parameters.bands[i].gainDb;soloWeight[i]=soloTarget[i];bypassWeight[i]=bypassTarget[i];appliedGain[i]=soloWeight[i]*(bypassWeight[i]+(1.-bypassWeight[i])*toGain(gainDb[i]+dynamicDb[i]));}
    }
    QuadParameters parameters{};
    double fs=48000.,parameterPole=.9974,powerPole=.9931,outputDb=0.;
    std::array<std::array<double,3>,2> filterState{};
    std::array<double,3> filterTarget{},filterCoefficient{};
    std::array<double,4> power{},dynamicDb{},desiredDb{},threshold{},range{},gainDb{};
    std::array<double,4> attackPole{},releasePole{},soloTarget{{1.,1.,1.,1.}},bypassTarget{},soloWeight{{1.,1.,1.,1.}},bypassWeight{},appliedGain{{1.,1.,1.,1.}};
    std::uint32_t controlCounter=0;int lastChannels=0;bool started=false,prepared=false;
};
} // namespace gilldyn
