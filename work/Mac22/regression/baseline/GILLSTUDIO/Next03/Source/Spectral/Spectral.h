#pragma once
// Original GILL spectral kernels. No vendor algorithms or assets.
// Core only: product UI, presets, sidechain and host state are separate work.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace gillstudio::spectral {
constexpr double pi = 3.1415926535897932384626433832795;
inline double finite(double x, double fallback = 0) noexcept { return std::isfinite(x) ? x : fallback; }
inline double bounded(double x, double lo, double hi, double fallback = 0) noexcept { return std::clamp(finite(x, fallback),lo,hi); }
inline double amplitude(double db) noexcept { return std::pow(10.,db/20.); }
inline double powerDb(double p) noexcept { return 10*std::log10(std::max(p,1e-30)); }
enum class Shape { Bell, LowShelf, HighShelf, Tilt, Notch, LowCut, HighCut, Bandpass, Gain };
enum class Mode { Pro, Live };
enum class Channels { Stereo, MidSide };
struct Node {
    bool enabled = false;
    Shape shape = Shape::Bell;
    double frequency = 1000, gainDb = 0, q = 1, slope = 12;
};
struct Parameters {
    Mode mode = Mode::Pro;
    Channels routing = Channels::Stereo;
    double lowHz=20, highHz=20000;
    double denoise=0, resonance=0, transient=0;
    bool boost=false, learn=false, trackNoise=false, delta=false, externalKey=false;
    double keyGainDb=0;
    double maxReductionDb=24, noiseBiasDb=0, resonanceThresholdDb=6;
    double sensitivity=.5, sharpness=.5, preserveTransient=.5;
    double attackMs=5, releaseMs=100, transientDecayMs=100;
    double stereoLink=1, mix=1, outputDb=0;
    std::array<Node,12> nodes{};
    // Opt-in absolute detector. Legacy prominence and noise-profile units keep
    // their original meaning. This bank weights detector level, not output EQ.
    double levelStrength=0, levelThresholdDb=-30, levelSlopeDbPerOct=0;
    std::array<Node,12> detectorNodes{};
};

class FFT {
public:
    void prepare(std::size_t n) {
        n_=n; reversal_.resize(n); roots_.resize(n/2);
        unsigned bits=0; for(auto s=n;s>1;s>>=1)++bits;
        for(std::size_t i=0;i<n;++i){std::size_t v=i,r=0;for(unsigned k=0;k<bits;++k){r=(r<<1)|(v&1);v>>=1;}reversal_[i]=r;}
        for(std::size_t i=0;i<n/2;++i)roots_[i]=std::polar(1.,-2*pi*i/n);
    }
    void transform(std::complex<double>* x, bool inverse) const noexcept {
        for(std::size_t i=0;i<n_;++i)if(i<reversal_[i])std::swap(x[i],x[reversal_[i]]);
        for(std::size_t len=2;len<=n_;len*=2){const auto half=len/2,step=n_/len;
            for(std::size_t base=0;base<n_;base+=len)for(std::size_t j=0;j<half;++j){
                const auto root=inverse?std::conj(roots_[j*step]):roots_[j*step];
                const auto a=x[base+j],b=x[base+j+half]*root;x[base+j]=a+b;x[base+j+half]=a-b;
            }
        }
        if(inverse)for(std::size_t i=0;i<n_;++i)x[i]/=double(n_);
    }
private:
    std::size_t n_=0;
    std::vector<std::size_t> reversal_;
    std::vector<std::complex<double>> roots_;
};

struct Profile {
    double sampleRate=48000;
    Channels routing=Channels::Stereo;
    std::size_t fftSize=0;
    std::uint64_t frames=0, liveSamples=0;
    std::array<std::vector<double>,2> density;
    std::array<std::array<double,32>,2> livePower{};
};

struct Filter {
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    double tick(double x) noexcept { const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y; }
    void reset() noexcept { z1=z2=0; }
    void lowpass(double hz,double fs) noexcept {
        // Matched one-pole parallel splits: neighboring differences have much
        // less phase rotation near their band centers than a pair of independent
        // resonant two-pole lowpasses. Unity gains still telescope exactly.
        const double pole=std::exp(-2*pi*hz/fs);
        b0=1-pole;b1=b2=a2=0;a1=-pole;
    }
    void bandpass(double hz,double fs,double q) noexcept {
        const double w=2*pi*std::clamp(hz,10.,fs*.48)/fs,a=std::sin(w)/(2*q),d=1+a;
        b0=a/d;b1=0;b2=-b0;a1=-2*std::cos(w)/d;a2=(1-a)/d;
    }
};

class Processor {
public:
    static constexpr std::size_t liveBands=32;
    // Exact double integers, far beyond any practical learning session. Bound
    // imported counters and saturate increments before they can wrap to zero.
    static constexpr std::uint64_t maxProfileUpdates=std::uint64_t(1)<<48;
    void prepare(double sampleRate,std::size_t /*maxBlock*/,std::size_t channels,std::size_t fftSize=2048) {
        fs_=bounded(sampleRate,8000,192000,48000);channels_=std::clamp<std::size_t>(channels,1,2);
        n_=512;while(n_<fftSize&&n_<8192)n_*=2;hop_=n_/4;bins_=n_/2+1;
        fft_.prepare(n_);window_.resize(n_);weights_.resize(bins_);focus_.resize(bins_);levelThresholds_.resize(bins_);
        for(std::size_t j=0;j<n_;++j)window_[j]=std::sqrt(.5-.5*std::cos(2*pi*(j+.5)/n_));
        double windowSum=0;for(auto w:window_)windowSum+=w;
        levelNormalization_=2/(windowSum*windowSum);
        for(auto& c:pro_){c.input.assign(n_,0);c.ola.assign(2*n_,0);c.dry.assign(n_,0);c.spectrum.assign(n_,{});
            c.keyInput.assign(n_,0);c.keySpectrum.assign(n_,{});c.keyPower.assign(bins_,0);
            c.power.assign(bins_,0);c.baseline.assign(bins_,0);c.event.assign(bins_,0);c.gainDb.assign(bins_,0);
            c.target.assign(bins_,0);c.noise.assign(bins_,0);c.levelGainDb.assign(bins_,0);c.levelTarget.assign(bins_,0);}
        for(std::size_t b=0;b<liveBands-1;++b){
            cuts_[b]=50*std::pow(std::min(20000.,fs_*.44)/50.,double(b)/(liveBands-2));
            for(auto& c:live_)c.filters[b].lowpass(cuts_[b],fs_);
        }
        for(std::size_t b=0;b<liveBands;++b){const double a=b==liveBands-1?1:1-std::exp(-2*pi*cuts_[b]/fs_),d=b==0?0:1-std::exp(-2*pi*cuts_[b-1]/fs_);liveBandwidth_[b]=std::max(1e-10,a/(2-a)+d/(2-d)-2*a*d/std::max(1e-12,a+d-a*d));}
        noiseScratch_.resize(bins_);
        for(std::size_t b=0;b<liveBands;++b){liveHz_[b]=b==0?25:b==liveBands-1?std::min(fs_*.47,cuts_.back()*1.25):std::sqrt(cuts_[b-1]*cuts_[b]);
            liveDetectorBandwidth_[b]=(std::sin(2*pi*std::min(liveHz_[b],fs_*.48)/fs_)/6)/(1+std::sin(2*pi*std::min(liveHz_[b],fs_*.48)/fs_)/6);
            liveLevelAverage_[b]=1-std::exp(-1/(fs_*std::max(.01,2/liveHz_[b])));
            for(auto& c:live_){c.detectors[b].bandpass(liveHz_[b],fs_,3);c.keyDetectors[b].bandpass(liveHz_[b],fs_,3);}}
        weightsDirty_=true;clearProfile();setParameters(p_);reset();prepared_=true;
    }
    void setParameters(Parameters p) noexcept {
        p.lowHz=bounded(p.lowHz,10,fs_*.49,20);p.highHz=bounded(p.highHz,p.lowHz,fs_*.499,fs_*.45);
        p.denoise=bounded(p.denoise,0,1);p.resonance=bounded(p.resonance,0,1);p.transient=bounded(p.transient,0,1);
        p.maxReductionDb=bounded(p.maxReductionDb,0,60,24);p.noiseBiasDb=bounded(p.noiseBiasDb,-24,24);
        p.resonanceThresholdDb=bounded(p.resonanceThresholdDb,0,40,6);
        p.keyGainDb=bounded(p.keyGainDb,-24,24);
        p.levelStrength=bounded(p.levelStrength,0,1);p.levelThresholdDb=bounded(p.levelThresholdDb,-100,0,-30);
        p.levelSlopeDbPerOct=bounded(p.levelSlopeDbPerOct,-12,12);
        p.sensitivity=bounded(p.sensitivity,0,1,.5);p.sharpness=bounded(p.sharpness,0,1,.5);
        p.preserveTransient=bounded(p.preserveTransient,0,1,.5);p.attackMs=bounded(p.attackMs,.05,1000,5);
        p.releaseMs=bounded(p.releaseMs,1,5000,100);p.transientDecayMs=bounded(p.transientDecayMs,5,2000,100);
        p.stereoLink=bounded(p.stereoLink,0,1,1);p.mix=bounded(p.mix,0,1,1);p.outputDb=bounded(p.outputDb,-36,24);
        if(p.mode!=Mode::Pro&&p.mode!=Mode::Live)p.mode=Mode::Pro;
        if(p.routing!=Channels::Stereo&&p.routing!=Channels::MidSide)p.routing=Channels::Stereo;
        for(auto& n:p.nodes){n.frequency=bounded(n.frequency,10,fs_*.49,1000);n.gainDb=bounded(n.gainDb,-48,24);
            n.q=bounded(n.q,.1,20,1);n.slope=bounded(n.slope,6,96,12);
            if(int(n.shape)<0||int(n.shape)>int(Shape::Gain))n.shape=Shape::Bell;}
        for(auto& n:p.detectorNodes){n.frequency=bounded(n.frequency,10,fs_*.49,1000);n.gainDb=bounded(n.gainDb,-48,24);
            n.q=bounded(n.q,.1,20,1);n.slope=bounded(n.slope,6,96,12);
            if(int(n.shape)<0||int(n.shape)>int(Shape::Gain))n.shape=Shape::Bell;}
        bool reweight=weightsDirty_||p.lowHz!=p_.lowHz||p.highHz!=p_.highHz;
        bool relevel=weightsDirty_||p.levelThresholdDb!=p_.levelThresholdDb||p.levelSlopeDbPerOct!=p_.levelSlopeDbPerOct;
        for(std::size_t i=0;i<p.nodes.size();++i){const auto&a=p.nodes[i];const auto&b=p_.nodes[i];
            reweight|=a.enabled!=b.enabled||a.shape!=b.shape||a.frequency!=b.frequency||a.gainDb!=b.gainDb||a.q!=b.q||a.slope!=b.slope;}
        for(std::size_t i=0;i<p.detectorNodes.size();++i){const auto&a=p.detectorNodes[i];const auto&b=p_.detectorNodes[i];
            relevel|=a.enabled!=b.enabled||a.shape!=b.shape||a.frequency!=b.frequency||a.gainDb!=b.gainDb||a.q!=b.q||a.slope!=b.slope;}
        const bool basisChanged=p.routing!=p_.routing;
        const bool changed=p.mode!=p_.mode||basisChanged;p_=p;
        if(basisChanged)clearProfile();
        const double frameRate=fs_/std::max<std::size_t>(1,hop_);
        attack_=1-std::exp(-1/(frameRate*p_.attackMs*.001));release_=1-std::exp(-1/(frameRate*p_.releaseMs*.001));
        decay_=std::exp(-1/(frameRate*p_.transientDecayMs*.001));
        liveAttack_=1-std::exp(-1/(fs_*p_.attackMs*.001));liveRelease_=1-std::exp(-1/(fs_*p_.releaseMs*.001));
        liveDecay_=std::exp(-1/(fs_*p_.transientDecayMs*.001));
        mixSmooth_=1-std::exp(-1/(fs_*.005));outputTarget_=amplitude(p_.outputDb);
        keyPowerGain_=amplitude(2*p_.keyGainDb);
        minimumGain_=amplitude(-p_.maxReductionDb);maximumGain_=p_.boost?amplitude(24):1;
        liveFast_=1-std::exp(-1/(fs_*.004));liveSlow_=1-std::exp(-1/(fs_*.12));
        denoiseOpen_=1-std::exp(-1/(fs_*(.002*(p_.releaseMs/100.))));partAverage_=1-std::exp(-1/(fs_*.012));
        noiseFall_=1-std::exp(-1/(fs_*.05));noiseRise_=1-std::exp(-1/(fs_*8));
        // Curves are consumed only at analysis boundaries. Coalesce arbitrary
        // host block/automation updates until then, preserving the parameter
        // values used for the next actual detector calculation.
        if(reweight)proWeightsDirty_=liveWeightsDirty_=true;
        if(relevel)proLevelDirty_=liveLevelDirty_=true;
        weightsDirty_=false;
        if(changed)reset(); // Product host must update PDC when changing mode; no allocation here.
    }
    void reset() noexcept {
        inputPos_=outputPos_=dryPos_=hopClock_=0;processed_=0;
        mix_=p_.mix;output_=amplitude(p_.outputDb);deltaMix_=p_.delta?1:0;
        for(auto& c:pro_){std::fill(c.input.begin(),c.input.end(),0);std::fill(c.ola.begin(),c.ola.end(),0);std::fill(c.dry.begin(),c.dry.end(),0);
            std::fill(c.power.begin(),c.power.end(),0);std::fill(c.baseline.begin(),c.baseline.end(),0);std::fill(c.event.begin(),c.event.end(),0);std::fill(c.gainDb.begin(),c.gainDb.end(),0);std::fill(c.levelGainDb.begin(),c.levelGainDb.end(),0);std::fill(c.levelTarget.begin(),c.levelTarget.end(),0);}
        for(auto& c:live_){for(auto& f:c.filters)f.reset();for(auto& f:c.detectors)f.reset();c.fast={};c.partPower={};c.diffuse=0;c.slow={};c.event={};c.gain.fill(1);c.wanted.fill(1);c.levelPower={};c.levelTarget={};c.levelGain.fill(1);c.levelWanted.fill(1);}
        resetKey();keyActive_=false;
    }
    void clearProfile() noexcept {
        profileFrames_=liveProfileSamples_=0;
        for(auto& c:pro_)std::fill(c.noise.begin(),c.noise.end(),0);
        for(auto& c:live_)c.noise={};
    }
    // Call export/import on a suspended engine, not concurrently with process.
    Profile exportProfile() const {
        Profile profile;profile.sampleRate=fs_;profile.routing=p_.routing;profile.fftSize=n_;profile.frames=profileFrames_;profile.liveSamples=liveProfileSamples_;
        for(std::size_t c=0;c<2;++c){profile.density[c]=pro_[c].noise;profile.livePower[c]=live_[c].noise;}return profile;
    }
    // Audio-thread snapshot publication: destination storage must be prepared
    // off-thread and exclusively owned for this copy. False leaves it unchanged.
    bool copyProfileInto(Profile& profile) const noexcept {
        for(std::size_t c=0;c<2;++c)if(profile.density[c].size()!=bins_)return false;
        profile.sampleRate=fs_;profile.routing=p_.routing;profile.fftSize=n_;
        profile.frames=profileFrames_;profile.liveSamples=liveProfileSamples_;
        for(std::size_t c=0;c<2;++c){std::copy(pro_[c].noise.begin(),pro_[c].noise.end(),profile.density[c].begin());profile.livePower[c]=live_[c].noise;}
        return true;
    }
    bool importProfile(const Profile& profile) noexcept {
        if(profile.frames>maxProfileUpdates||profile.liveSamples>maxProfileUpdates)return false;
        if(!std::isfinite(profile.sampleRate)||profile.sampleRate<8000||profile.sampleRate>192000||profile.fftSize<512||profile.fftSize>8192||(profile.fftSize&(profile.fftSize-1))!=0||profile.routing!=p_.routing)return false;
        const auto count=profile.fftSize/2+1;
        for(std::size_t c=0;c<2;++c){if(profile.density[c].size()!=count)return false;
            for(auto v:profile.density[c])if(!std::isfinite(v)||v<0||v>1e12)return false;
            for(auto v:profile.livePower[c])if(!std::isfinite(v)||v<0||v>1e12)return false;}
        for(std::size_t c=0;c<2;++c){for(std::size_t b=0;b<bins_;++b){const double index=(b*fs_/n_)*profile.fftSize/profile.sampleRate;
                if(index>count-1){pro_[c].noise[b]=0;continue;}
                const auto lo=std::size_t(index),hi=std::min(lo+1,count-1);const double t=index-lo;
                pro_[c].noise[b]=(1-t)*profile.density[c][lo]+t*profile.density[c][hi];}
            // LIVE banks move with Nyquist; unlike PSD interpolation, band energies
            // are not interchangeable across sample rates. Require fresh LIVE learn.
            live_[c].noise=profile.sampleRate==fs_?profile.livePower[c]:std::array<double,32>{};}
        profileFrames_=profile.frames;liveProfileSamples_=profile.sampleRate==fs_?profile.liveSamples:0;return true;
    }
    std::size_t latencySamples() const noexcept { return p_.mode==Mode::Live?0:n_; }
    bool hasProfile() const noexcept {return p_.mode==Mode::Live?liveProfileSamples_>0:profileFrames_>0;}
    double reductionDb() const noexcept { return reduction_; }
    // Optional key is detector-only. Missing/invalid key falls back to main.
    // A connected silent key remains external; it must not trigger resonance.
    void process(float*const* audio,std::size_t frames,const float*const* key=nullptr,std::size_t keyChannels=0) noexcept {
        if(!prepared_||!audio)return;for(std::size_t c=0;c<channels_;++c)if(!audio[c])return;
        if(frames==0)return;
        const bool active=p_.externalKey&&key&&(keyChannels==1||keyChannels==2)&&key[0]&&(keyChannels==1||key[1]);
        if(active!=keyActive_){resetKey();keyActive_=active;}
        for(std::size_t i=0;i<frames;++i){std::array<double,2>x{},wet{},dry{};
            std::array<double,2> detectorKey{};
            for(std::size_t c=0;c<channels_;++c)x[c]=bounded(audio[c][i],-1e6,1e6); // float host abuse guard, not a ceiling
            if(keyActive_){detectorKey[0]=bounded(key[0][i],-1e6,1e6);
                detectorKey[1]=keyChannels==2?bounded(key[1][i],-1e6,1e6):detectorKey[0];
                if(channels_==1&&keyChannels==2)detectorKey[0]=.5*(detectorKey[0]+detectorKey[1]);
                if(p_.routing==Channels::MidSide&&channels_==2)toMidSide(detectorKey);}
            if(p_.routing==Channels::MidSide&&channels_==2)toMidSide(x);
            if(p_.mode==Mode::Pro){
                for(std::size_t c=0;c<channels_;++c){auto& s=pro_[c];s.input[inputPos_]=x[c];dry[c]=s.dry[dryPos_];s.dry[dryPos_]=x[c];
                    s.keyInput[inputPos_]=detectorKey[c];
                    wet[c]=s.ola[outputPos_];s.ola[outputPos_]=0;}
                inputPos_=(inputPos_+1)%n_;dryPos_=(dryPos_+1)%n_;
                if(++hopClock_==hop_){hopClock_=0;processFrame();}
                outputPos_=(outputPos_+1)%(2*n_);
            } else {dry=x;processLive(x,detectorKey,wet);}
            if(p_.routing==Channels::MidSide&&channels_==2){toMidSide(wet);toMidSide(dry);}
            mix_+=mixSmooth_*(p_.mix-mix_);output_+=mixSmooth_*(outputTarget_-output_);deltaMix_+=mixSmooth_*((p_.delta?1.:0.)-deltaMix_);
            for(std::size_t c=0;c<channels_;++c){const double normal=(dry[c]+mix_*(wet[c]-dry[c]))*output_;
                audio[c][i]=float(finite(normal*(1-deltaMix_)+(dry[c]-wet[c])*deltaMix_));}
            ++processed_;
        }
    }
private:
    struct ProState {std::vector<double>input,ola,dry,power,baseline,event,gainDb,target,noise,keyInput,keyPower,levelGainDb,levelTarget;std::vector<std::complex<double>>spectrum,keySpectrum;};
    struct LiveState {std::array<Filter,31>filters{};std::array<Filter,32>detectors{},keyDetectors{};double diffuse=0;std::array<double,32>partPower{},fast{},slow{},event{},gain{},wanted{},noise{},keyFast{},levelPower{},keyLevelPower{},levelTarget{},levelGain{},levelWanted{};};
    void resetKey() noexcept {
        for(auto&c:pro_){std::fill(c.keyInput.begin(),c.keyInput.end(),0);std::fill(c.keyPower.begin(),c.keyPower.end(),0);}
        for(auto&c:live_){for(auto&f:c.keyDetectors)f.reset();c.keyFast={};c.keyLevelPower={};}
    }
    static void incrementProfile(std::uint64_t& count) noexcept {if(count<maxProfileUpdates)++count;}
    double prominence(double main,double key,double neighbors) const noexcept {
        const double numerator=keyActive_?key*keyPowerGain_:main;
        return numerator<=1e-30?-300:powerDb(numerator)-powerDb(neighbors);
    }
    static void toMidSide(std::array<double,2>&x) noexcept {const double a=(x[0]+x[1])*.7071067811865475244,b=(x[0]-x[1])*.7071067811865475244;x={a,b};}
    double bandFocus(double hz) const noexcept {
        if(hz<=0)return p_.lowHz<=10?1:0;
        const double l=std::log2(hz/p_.lowHz),h=std::log2(p_.highHz/hz);
        // Boundaries are half-power weights, quarter-octave raised-cosine transitions.
        auto ramp=[](double d){d=std::clamp(d/.25+.5,0.,1.);return .5-.5*std::cos(pi*d);};
        return ramp(l)*ramp(h);
    }
public:
    // Pure display/control helper: same weighting definition as the audio path.
    // Supply a sanitized parameter snapshot, never read live engine state on UI.
    static double nodeWeightDbAt(const Parameters& parameters,double hz) noexcept {
        return curveDbAt(parameters.nodes,hz);
    }
    static double detectorWeightDbAt(const Parameters& parameters,double hz) noexcept {
        return curveDbAt(parameters.detectorNodes,hz);
    }
private:
    static double curveDbAt(const std::array<Node,12>& nodes,double hz) noexcept {
        double db=0;const double safe=std::max(hz,1.);
        for(const auto&n:nodes)if(n.enabled){const double oct=std::log2(safe/n.frequency),g=n.gainDb;
            switch(n.shape){
            case Shape::Bell:db+=g*std::exp(-.5*std::pow(oct*n.q*1.4,2));break;
            case Shape::Notch:db-=std::abs(g)*std::exp(-.5*std::pow(oct*n.q*1.4,2));break;
            case Shape::LowShelf:db+=g/(1+std::exp(std::clamp(4*n.q*oct,-80.,80.)));break;
            case Shape::HighShelf:db+=g/(1+std::exp(std::clamp(-4*n.q*oct,-80.,80.)));break;
            case Shape::Tilt:db+=g*std::tanh(oct*n.q);break;
            case Shape::LowCut:db+=std::min(0.,oct*n.slope);break;
            case Shape::HighCut:db+=std::min(0.,-oct*n.slope);break;
            case Shape::Bandpass:db+=std::min(0.,(.5/n.q-std::abs(oct))*n.slope);break;
            case Shape::Gain:db+=g;break;
            }
        }
        return std::clamp(db,-120.,36.);
    }
private:
    double nodeWeight(double hz) const noexcept { return amplitude(nodeWeightDbAt(p_,hz)); }
    double levelThresholdAt(double hz)const noexcept {
        // Adding a dB detector response is exactly equivalent to lowering the
        // threshold by that response; neither operation touches output audio.
        return p_.levelThresholdDb+p_.levelSlopeDbPerOct*std::log2(std::max(hz,10.)/1000.)-detectorWeightDbAt(p_,hz);
    }
    double levelTarget(double power,double threshold,double focus)const noexcept {
        if(p_.levelStrength==0||power<=1e-30)return 0;
        return -std::min(p_.maxReductionDb,p_.levelStrength*std::max(0.,powerDb(power)-threshold))*focus;
    }
    void refreshProCurves()noexcept{
        if(proWeightsDirty_){for(std::size_t b=0;b<bins_;++b){const double hz=b*fs_/n_;weights_[b]=nodeWeight(hz);focus_[b]=bandFocus(hz);}proWeightsDirty_=false;}
        if(proLevelDirty_){for(std::size_t b=0;b<bins_;++b)levelThresholds_[b]=levelThresholdAt(b*fs_/n_);proLevelDirty_=false;}
    }
    void refreshLiveCurves()noexcept{
        if(liveWeightsDirty_){for(std::size_t b=0;b<liveBands;++b){liveWeights_[b]=nodeWeight(liveHz_[b]);liveFocus_[b]=bandFocus(liveHz_[b]);}liveWeightsDirty_=false;}
        if(liveLevelDirty_){for(std::size_t b=0;b<liveBands;++b)liveLevelThresholds_[b]=levelThresholdAt(liveHz_[b]);liveLevelDirty_=false;}
    }
    double target(double power,double noise,double novelty,double prominence,double weight,double focus,bool learned,double denoisePower=-1,double denoiseNoise=-1) const noexcept {
        const double transientProtection=1-p_.preserveTransient*std::clamp((novelty-1)/5.,0.,1.);
        double db=0;
        if(p_.denoise>0&&learned){const double fraction=(denoiseNoise>=0?denoiseNoise:noise)*amplitude(2*p_.noiseBiasDb)/std::max(denoisePower>=0?denoisePower:power,1e-30);
            // Default subtraction is conservative; an explicit positive Noise Bias
            // can still reach the full user-selected Max Reduction range.
            const double gain=std::sqrt(std::max(amplitude(-2*p_.maxReductionDb),1-.8*std::clamp(fraction,0.,.98*std::max(1.,amplitude(2*p_.noiseBiasDb)))));
            db+=std::max(-p_.maxReductionDb,20*std::log10(std::max(gain,1e-12)))*p_.denoise*transientProtection;}
        db-=std::clamp((prominence-p_.resonanceThresholdDb)*(1+3*p_.sharpness),0.,p_.maxReductionDb)*p_.resonance;
        const double onset=std::clamp((novelty-(1.2+3*(1-p_.sensitivity)))/4.,0.,1.);
        db+=(p_.boost?1:-1)*p_.transient*p_.maxReductionDb*onset;
        return std::clamp(db*weight,-p_.maxReductionDb,p_.boost?24.:0.)*focus;
    }
    void processFrame() noexcept {
        refreshProCurves();
        const double rate=fs_/hop_,powerA=1-std::exp(-1/(rate*.035)),slowA=1-std::exp(-1/(rate*.15));
        const double normalization=2./(fs_*n_); // PSD using window energy N/2
        if(p_.learn)incrementProfile(profileFrames_);
        for(std::size_t c=0;c<channels_;++c){auto&s=pro_[c];
            for(std::size_t j=0;j<n_;++j)s.spectrum[j]={s.input[(inputPos_+j)%n_]*window_[j],0};
            fft_.transform(s.spectrum.data(),false);
            if(keyActive_){for(std::size_t j=0;j<n_;++j)s.keySpectrum[j]={s.keyInput[(inputPos_+j)%n_]*window_[j],0};fft_.transform(s.keySpectrum.data(),false);}
            for(std::size_t b=0;b<bins_;++b){const double power=std::norm(s.spectrum[b])*normalization;
                s.power[b]+=powerA*(power-s.power[b]);
                if(keyActive_)s.keyPower[b]+=powerA*(std::norm(s.keySpectrum[b])*normalization-s.keyPower[b]);
                if(p_.learn)s.noise[b]+=(power-s.noise[b])/double(profileFrames_);
                else if(p_.trackNoise&&profileFrames_>0){const double a=power<s.noise[b]?1-std::exp(-1/(rate*.05)):1-std::exp(-1/(rate*8));s.noise[b]+=a*(power-s.noise[b]);}
                const double novelty=power/std::max(s.baseline[b],1e-20);
                s.event[b]=std::max(std::min(novelty,20.),s.event[b]*decay_);
                s.baseline[b]+=slowA*(power-s.baseline[b]);
            }
            // A lower spectral quantile bounds the floor by current diffuse
            // energy. A learned profile must not subtract an absent noise bed.
            for(std::size_t b=0;b<bins_;++b)noiseScratch_[b]=s.power[b];
            auto percentile=noiseScratch_.begin()+bins_/3;std::nth_element(noiseScratch_.begin(),percentile,noiseScratch_.end());
            const double diffuse=*percentile*1.7;
            for(std::size_t b=0;b<bins_;++b){double neighbors=0;int count=0;const auto radius=std::size_t(3+6*(1-p_.sharpness));
                for(auto k=b>radius?b-radius:0;k<std::min(bins_,b+radius+1);++k)if(k+1<b||k>b+1){neighbors+=s.power[k];++count;}
                const double prominenceDb=prominence(s.power[b],s.keyPower[b],neighbors/std::max(1,count));
                const double event=processed_>n_?s.event[b]:1.;
                s.target[b]=target(s.power[b],s.noise[b],event,prominenceDb,weights_[b],focus_[b],(profileFrames_>0||p_.trackNoise)&&!p_.learn,s.power[b],profileFrames_>0?std::min(s.noise[b],diffuse):diffuse);
                const double levelPower=p_.levelStrength>0?std::norm(keyActive_?s.keySpectrum[b]:s.spectrum[b])*levelNormalization_*(keyActive_?keyPowerGain_:1)*((b==0||b==n_/2)?.5:1):0;
                s.levelTarget[b]=levelTarget(levelPower,levelThresholds_[b],focus_[b]);
            }
        }
        reduction_=0;
        for(std::size_t b=0;b<bins_;++b){double strongest=0,strongestLevel=0;
            for(std::size_t c=0;c<channels_;++c)if(std::abs(pro_[c].target[b])>std::abs(strongest))strongest=pro_[c].target[b];
            for(std::size_t c=0;c<channels_;++c)strongestLevel=std::min(strongestLevel,pro_[c].levelTarget[b]);
            for(std::size_t c=0;c<channels_;++c){auto&s=pro_[c];
                const double linked=s.target[b]+p_.stereoLink*(strongest-s.target[b]);
                s.gainDb[b]+=(std::abs(linked)>std::abs(s.gainDb[b])?attack_:(p_.denoise>0?1-std::exp(-1/(rate*(.004*(p_.releaseMs/100.)))):release_))*(linked-s.gainDb[b]);
                const double levelLinked=s.levelTarget[b]+p_.stereoLink*(strongestLevel-s.levelTarget[b]);
                s.levelGainDb[b]+=(levelLinked<s.levelGainDb[b]?attack_:release_)*(levelLinked-s.levelGainDb[b]);
                reduction_=std::max(reduction_,std::abs(std::clamp(s.gainDb[b]+s.levelGainDb[b],-p_.maxReductionDb,p_.boost?24.:0.)));
            }
        }
        for(std::size_t c=0;c<channels_;++c){auto&s=pro_[c];
            for(std::size_t b=0;b<bins_;++b){const auto lo=b?b-1:0,hi=std::min(b+1,bins_-1);
                double db=(s.gainDb[lo]+2*s.gainDb[b]+s.gainDb[hi])*.25;
                if(s.levelGainDb[b]!=0)db=std::clamp(db+s.levelGainDb[b],-p_.maxReductionDb,p_.boost?24.:0.);
                const double gain=amplitude(db);s.spectrum[b]*=gain;if(b>0&&b<n_/2)s.spectrum[n_-b]*=gain;}
            fft_.transform(s.spectrum.data(),true);
            for(std::size_t j=0;j<n_;++j)s.ola[(outputPos_+1+j)%(2*n_)]+=s.spectrum[j].real()*window_[j]*.5;
        }
    }
    void processLive(const std::array<double,2>&x,const std::array<double,2>&key,std::array<double,2>&y) noexcept {
        std::array<std::array<double,32>,2>parts{},wanted{};
        const bool update=(processed_&15)==0;
        if(update)refreshLiveCurves();
        if(p_.learn)incrementProfile(liveProfileSamples_);
        for(std::size_t c=0;c<channels_;++c){auto&s=live_[c];double previous=0;
            for(std::size_t b=0;b<liveBands-1;++b){const double low=s.filters[b].tick(x[c]);parts[c][b]=low-previous;previous=low;}parts[c].back()=x[c]-previous;
            for(std::size_t b=0;b<liveBands;++b){const double detector=s.detectors[b].tick(x[c]);const double power=detector*detector;s.fast[b]+=liveFast_*(power-s.fast[b]);s.partPower[b]+=partAverage_*(parts[c][b]*parts[c][b]-s.partPower[b]);
                s.levelPower[b]+=liveLevelAverage_[b]*(power-s.levelPower[b]);
                if(keyActive_){const double d=s.keyDetectors[b].tick(key[c]);s.keyFast[b]+=liveFast_*(d*d-s.keyFast[b]);s.keyLevelPower[b]+=liveLevelAverage_[b]*(d*d-s.keyLevelPower[b]);}
                if(p_.learn)s.noise[b]+=(power-s.noise[b])/double(liveProfileSamples_);
                else if(p_.trackNoise&&liveProfileSamples_>0){const double a=s.fast[b]<s.noise[b]?noiseFall_:noiseRise_;s.noise[b]+=a*(s.fast[b]-s.noise[b]);}
                const double novelty=s.fast[b]/std::max(s.slow[b],1e-20);s.event[b]=std::max(std::min(novelty,20.),s.event[b]*liveDecay_);s.slow[b]+=liveSlow_*(s.fast[b]-s.slow[b]);
            }
            if(update){std::array<double,32> whitened{};for(std::size_t b=0;b<liveBands;++b)whitened[b]=s.fast[b]/std::max(1e-8,liveDetectorBandwidth_[b]);std::sort(whitened.begin(),whitened.end());s.diffuse+=(1-std::exp(-16/(fs_*.08)))*(whitened[3]*std::clamp(2*whitened[3]/(whitened[liveBands/2]+1e-20),0.,1.)-s.diffuse);}
            if(update)for(std::size_t b=0;b<liveBands;++b){double neighbors=0;int count=0;
                // Adjacent detector bands overlap substantially. Excluding
                // their immediate shoulders keeps a narrow tone from being
                // mistaken for the surrounding spectral baseline.
                for(auto k=b>4?b-4:0;k<std::min(liveBands,b+5);++k)if(k+1<b||k>b+1){neighbors+=s.fast[k];++count;}
                const double prominenceDb=prominence(s.fast[b],s.keyFast[b],neighbors/std::max(1,count));
                wanted[c][b]=target(s.fast[b],s.noise[b],processed_>fs_*.05?s.event[b]:1.,prominenceDb,liveWeights_[b],liveFocus_[b],(liveProfileSamples_>0||p_.trackNoise)&&!p_.learn,s.partPower[b],.85*liveBandwidth_[b]*(liveProfileSamples_>0?std::min(s.diffuse,s.noise[b]/std::max(1e-8,liveDetectorBandwidth_[b])):s.diffuse));
                s.levelTarget[b]=levelTarget(keyActive_?s.keyLevelPower[b]*keyPowerGain_:s.levelPower[b],liveLevelThresholds_[b],liveFocus_[b]);}
        }
        if(update)reduction_=0;
        for(std::size_t b=0;b<liveBands;++b){double strongest=0,strongestLevel=0;
            if(update)for(std::size_t c=0;c<channels_;++c)if(std::abs(wanted[c][b])>std::abs(strongest))strongest=wanted[c][b];
            if(update)for(std::size_t c=0;c<channels_;++c)strongestLevel=std::min(strongestLevel,live_[c].levelTarget[b]);
            for(std::size_t c=0;c<channels_;++c){auto&s=live_[c];
                if(update){const double linked=wanted[c][b]+p_.stereoLink*(strongest-wanted[c][b]);s.wanted[b]=amplitude(linked);
                    s.levelWanted[b]=amplitude(s.levelTarget[b]+p_.stereoLink*(strongestLevel-s.levelTarget[b]));
                    reduction_=std::max(reduction_,std::abs(20*std::log10(std::max(minimumGain_,s.gain[b]*s.levelGain[b]))));}
                s.gain[b]+=(std::abs(s.wanted[b]-1)>std::abs(s.gain[b]-1)?liveAttack_:(p_.denoise>0?denoiseOpen_:liveRelease_))*(s.wanted[b]-s.gain[b]);
                s.levelGain[b]+=(s.levelWanted[b]<s.levelGain[b]?liveAttack_:liveRelease_)*(s.levelWanted[b]-s.levelGain[b]);
                const double gain=s.levelGain[b]==1?s.gain[b]:std::clamp(s.gain[b]*s.levelGain[b],minimumGain_,maximumGain_);
                y[c]+=parts[c][b]*gain;}}
    }
    std::vector<double>noiseScratch_;Parameters p_{};FFT fft_;double fs_=48000;std::size_t channels_=2,n_=0,hop_=1,bins_=0;
    std::array<ProState,2>pro_{};std::array<LiveState,2>live_{};
    std::vector<double>window_,weights_,focus_,levelThresholds_;
    std::array<double,31>cuts_{};std::array<double,32>liveHz_{},liveWeights_{},liveFocus_{},liveLevelThresholds_{},liveLevelAverage_{},liveBandwidth_{},liveDetectorBandwidth_{};
    std::size_t inputPos_=0,outputPos_=0,dryPos_=0,hopClock_=0;
    std::uint64_t processed_=0,profileFrames_=0,liveProfileSamples_=0;
    double attack_=1,release_=1,decay_=1,liveAttack_=1,liveRelease_=1,liveDecay_=1,mix_=1,output_=1,deltaMix_=0,reduction_=0;
    double mixSmooth_=0,outputTarget_=1,liveFast_=0,liveSlow_=0,noiseFall_=0,noiseRise_=0,denoiseOpen_=0,partAverage_=0;
    double keyPowerGain_=1;
    double levelNormalization_=1,minimumGain_=1,maximumGain_=1;
    bool prepared_=false,weightsDirty_=true,keyActive_=false;
    bool proWeightsDirty_=true,liveWeightsDirty_=true,proLevelDirty_=true,liveLevelDirty_=true;
};
} // namespace gillstudio::spectral
