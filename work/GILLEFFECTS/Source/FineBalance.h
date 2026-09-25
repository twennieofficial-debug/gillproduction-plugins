#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

namespace gill {
// Analysis only: this FFT never sits in the audio path, so it adds no latency.
// Both channels contribute power independently (anti-phase audio cannot cancel).
struct FineBalanceProfile {
    static constexpr int points=256;
    std::array<float,points> db{};
    double analysisRate=48000;
    std::uint32_t frames=0;
    static double frequency(int i) noexcept {return 20*std::pow(1000.,i/255.);}
};
class FineBalanceAnalyzer {
public:
    static constexpr int size=4096;
    void prepare(double rate) noexcept {
        decimation=std::max(1,static_cast<int>(std::ceil(rate/48000.)));
        analysisRate=rate/decimation;
        lowAlpha=1-std::exp(-2*pi*(analysisRate*.35)/rate);
        for(int i=0;i<size;++i)window[i]=.5-.5*std::cos(2*pi*i/(size-1));
        clear();
    }
    void clear() noexcept {ring={};low={};accum={};position=filled=hop=phase=0;frames=0;}
    void push(double left,double right,int channels) noexcept {
        double x[2]{left,channels>1?right:left};
        if(decimation>1)for(int c=0;c<2;++c)for(auto& state:low[c]){state+=lowAlpha*(x[c]-state);x[c]=state;}
        if(++phase<decimation)return;phase=0;
        for(int c=0;c<2;++c)ring[c][position]=x[c];
        position=(position+1)%size;filled=std::min(size,filled+1);
        if(++hop<size/2||filled<size)return;hop=0;
        std::array<double,size/2+1> power{};double total=0,peak=0,rms=0;
        for(int c=0;c<2;++c){
            for(int i=0;i<size;++i){const double sample=ring[c][(position+i)%size];rms+=sample*sample;peak=std::max(peak,std::abs(sample));bins[i]={sample*window[i],0};}
            fft();for(int k=0;k<=size/2;++k)power[k]+=std::norm(bins[k]);
        }
        rms/=2*size;
        // Ignore silence and isolated clicks; learn sustained spectral content.
        if(rms<1e-6||rms>15.85||peak*peak>rms*100)return;
        for(double xPower:power)total+=xPower;
        if(total<=1e-25)return;
        for(int k=0;k<=size/2;++k)accum[k]+=power[k]/total;
        ++frames;
    }
    FineBalanceProfile profile() const noexcept {
        FineBalanceProfile p;p.analysisRate=analysisRate;p.frames=frames;
        for(int i=0;i<p.points;++i){const double bin=FineBalanceProfile::frequency(i)*size/analysisRate;
            if(bin>=size*.48||frames==0){p.db[i]=-120;continue;}
            const int k=std::clamp(static_cast<int>(bin),1,size/2-1);const double fraction=std::clamp(bin-k,0.,1.);
            const double power=(accum[k]*(1-fraction)+accum[k+1]*fraction)/frames;
            p.db[i]=static_cast<float>(std::max(-120.,10*std::log10(std::max(1e-30,power))));
        }return p;
    }
private:
    static constexpr double pi=3.14159265358979323846;
    void fft() noexcept {
        for(int i=1,j=0;i<size;++i){int bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(bins[i],bins[j]);}
        for(int length=2;length<=size;length<<=1){const auto step=std::polar(1.,-2*pi/length);
            for(int first=0;first<size;first+=length){std::complex<double> w{1,0};for(int j=0;j<length/2;++j){const auto u=bins[first+j],v=bins[first+j+length/2]*w;bins[first+j]=u+v;bins[first+j+length/2]=u-v;w*=step;}}}
    }
    int decimation=1,position=0,filled=0,hop=0,phase=0;double analysisRate=48000,lowAlpha=0;
    std::uint32_t frames=0;
    std::array<std::array<double,size>,2> ring{};
    std::array<std::array<double,4>,2> low{};
    std::array<double,size> window{};
    std::array<std::complex<double>,size> bins{};
    std::array<double,size/2+1> accum{};
};

struct FineBalanceView {
    std::array<float,12> frequency{},gain{},q{};
    std::array<float,8> dynamics{};
    std::array<float,256> spectrum{};
    int count=0;bool enabled=false;
};

// Up to twelve learned narrow resonance cuts plus eight independent dynamic
// bell bands. Detectors are stereo linked and relative to full-band energy:
// simply singing louder does not trigger a different tonal correction.
class FineBalanceStage {
public:
    void prepare(double rate) noexcept {fs=rate;attack=1-std::exp(-16/(fs*.018));release=1-std::exp(-16/(fs*.220));reset();}
    void reset() noexcept {states={};for(auto& c:coefficients)c={1,0,0,0,0};gain={};dynamics={};quantum=0;view={};enabled=false;transitionRemaining=0;transitionBlend=1;}
    void configure(const FineBalanceProfile& profile,bool use,float rmsDb,const std::array<float,8>& bands) noexcept {
        const bool wasEnabled=enabled;const auto priorCoefficients=coefficients;const auto priorStates=states;
        reset();if(wasEnabled){previousCoefficients=priorCoefficients;previousStates=priorStates;transitionLength=std::max(1,static_cast<int>(fs*.080));transitionRemaining=transitionLength;transitionBlend=0;}
        enabled=use;view.enabled=use;view.spectrum=profile.db;reference=bands;targets={};frequencies={};quality={};count=0;
        if(!use)return;
        struct Candidate{double frequency,gain,q,score;};std::array<Candidate,256> candidates{};int found=0;
        for(int i=10;i<246;++i){const double hz=FineBalanceProfile::frequency(i),level=profile.db[i];
            if(hz<180||hz>std::min(fs*.44,profile.analysisRate*.44)||level<-65||level+rmsDb<-80)continue;
            if(level<profile.db[i-1]||level<=profile.db[i+1])continue;
            double left=0,right=0;for(int d=4;d<=10;++d){left+=profile.db[i-d];right+=profile.db[i+d];}
            const double baseline=(left+right)/14,prominence=level-baseline;
            if(prominence<3.0||baseline<-85)continue;
            int lo=i-1,hi=i+1;while(lo>1&&profile.db[lo]>level-3)--lo;while(hi<254&&profile.db[hi]>level-3)++hi;
            const double width=std::max(profile.analysisRate/FineBalanceAnalyzer::size*2, FineBalanceProfile::frequency(hi)-FineBalanceProfile::frequency(lo));
            candidates[found++]={hz,-std::min(7.,(prominence-1.5)*.65),std::clamp(hz/width,1.5,12.),prominence};
        }
        std::sort(candidates.begin(),candidates.begin()+found,[](const Candidate& a,const Candidate& b){return a.score>b.score;});
        double total=0;
        for(int i=0;i<found&&count<12;++i){bool near=false;for(int j=0;j<count;++j)near|=std::abs(std::log2(candidates[i].frequency/frequencies[j]))<.13;if(near)continue;
            frequencies[count]=candidates[i].frequency;quality[count]=candidates[i].q;targets[count]=candidates[i].gain;total-=targets[count];++count;}
        // Bound the summed narrow cuts to 24 dB, including overlapping filters.
        const double scale=24/std::max(24.,total);for(auto& x:targets)x*=scale;
        view.count=count;for(int i=0;i<count;++i){view.frequency[i]=static_cast<float>(frequencies[i]);view.q[i]=static_cast<float>(quality[i]);}
    }
    void setAmount(float a) noexcept {amount=std::clamp(static_cast<double>(a)*.01,0.,1.);}
    FineBalanceView snapshot() const noexcept {return view;}
    bool active() const noexcept {return enabled;}
    void update(const std::array<double,8>& powers,double fullPower,const std::array<bool,8>& available) noexcept {
        if(transitionRemaining>0){--transitionRemaining;transitionBlend=1-static_cast<double>(transitionRemaining)/transitionLength;}
        if(!enabled)return;
        if(++quantum<16)return;quantum=0;
        double sum=0;std::array<double,8> desired{};
        for(int b=0;b<8;++b){const double relative=10*std::log10(std::max(1e-30,powers[b])/std::max(1e-30,fullPower));
            desired[b]=available[b]&&fullPower>1e-7?-std::clamp((relative-reference[b]-2.0)*.75,0.,4.)*amount:0;sum-=desired[b];}
        const double scale=8/std::max(8.,sum);
        for(int b=0;b<8;++b){const double dest=desired[b]*scale;dynamics[b]+=(dest<dynamics[b]?attack:release)*(dest-dynamics[b]);if(amount==0&&std::abs(dynamics[b])<1e-6)dynamics[b]=0;
            coefficients[12+b]=bell(centers[b],.9,dynamics[b]);view.dynamics[b]=static_cast<float>(dynamics[b]);}
        for(int b=0;b<12;++b){const double dest=enabled&&b<count?targets[b]*amount:0;gain[b]+=(1-std::exp(-16/(fs*.080)))*(dest-gain[b]);if(amount==0&&std::abs(gain[b])<1e-6)gain[b]=0;coefficients[b]=bell(frequencies[b],quality[b],gain[b]);view.gain[b]=static_cast<float>(gain[b]);}
    }
    double process(double x,int channel) noexcept {
        const double input=x;bool neutral=!enabled;
        if(amount==0&&enabled){neutral=true;for(auto g:gain)neutral&=g==0;for(auto d:dynamics)neutral&=d==0;}
        auto chain=[&](double value,const auto& cfs,auto& memory){for(int b=0;b<20;++b){const auto& c=cfs[b];auto& s=memory[channel][b];const double y=c[0]*value+s[0];s[0]=c[1]*value-c[3]*y+s[1];s[1]=c[2]*value-c[4]*y;if(std::abs(s[0])<1e-30)s[0]=0;if(std::abs(s[1])<1e-30)s[1]=0;value=y;}return value;};
        if(neutral)states[channel]={};else x=chain(x,coefficients,states);
        if(transitionRemaining>0){const double previous=chain(input,previousCoefficients,previousStates);x=previous+(x-previous)*transitionBlend;}
        return x;
    }
private:
    using Coeff=std::array<double,5>;
    Coeff bell(double hz,double q,double db) const noexcept {if(hz<=0||hz>fs*.45||q<=0||std::abs(db)<1e-12)return {1,0,0,0,0};const double w=6.283185307179586*hz/fs,a=std::pow(10.,db/40),alpha=std::sin(w)/(2*q),norm=1/(1+alpha/a),c=std::cos(w);return {(1+alpha*a)*norm,-2*c*norm,(1-alpha*a)*norm,-2*c*norm,(1-alpha/a)*norm};}
    inline static constexpr std::array<double,8> centers{100,200,400,800,1600,3200,6400,12000};
    double fs=48000,attack=.02,release=.002,amount=.6;bool enabled=false;int count=0,quantum=0;
    std::array<float,8> reference{};
    std::array<double,12> frequencies{},quality{},targets{},gain{};
    std::array<double,8> dynamics{};
    std::array<Coeff,20> coefficients{};
    std::array<std::array<std::array<double,2>,20>,2> states{};
    std::array<Coeff,20> previousCoefficients{};
    std::array<std::array<std::array<double,2>,20>,2> previousStates{};
    int transitionRemaining=0,transitionLength=1;double transitionBlend=1;
    FineBalanceView view;
};
}
