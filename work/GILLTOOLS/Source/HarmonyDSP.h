#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gill::tools {

struct HarmonyParameters {
    struct Voice {
        bool enabled = true;
        int intervalMode = 0; // 0: scale-note steps; 1: fixed semitones.
        int interval = 2;
        float levelDb = -9, pan = 0; // Pan -100..100; -60 dB is exact mute.
    };
    int key = 0, scale = 1; // C=0; major, natural minor, harmonic minor, minor pentatonic.
    std::array<Voice, 3> voices{{{true,0,2,-9,-35}, {true,0,4,-12,35}, {false,0,-7,-15,0}}};
    bool direct = true;
    float natural = 75, outputDb = -6; // NATURAL controls spectral-envelope preservation.
    bool bypass = false;
};

// Interval integers remain independent of UI choice indices. Pentatonic steps
// are labelled explicitly: two pentatonic steps are not always a musical third.
inline int harmonyScaleLength(int scale) noexcept { return scale == 3 ? 5 : 7; }
inline int harmonyClampInterval(int mode, int interval, int scale) noexcept {
    const int limit = mode == 1 ? 12 : harmonyScaleLength(scale);
    return std::clamp(interval, -limit, limit);
}
inline const char* harmonyIntervalLabel(int mode, int interval, int scale) noexcept {
    static constexpr const char* fixed[] = {"-12 ST","-11 ST","-10 ST","-9 ST","-8 ST","-7 ST","-6 ST","-5 ST","-4 ST","-3 ST","-2 ST","-1 ST","UNISON","+1 ST","+2 ST","+3 ST","+4 ST","+5 ST","+6 ST","+7 ST","+8 ST","+9 ST","+10 ST","+11 ST","+12 ST"};
    static constexpr const char* diatonic[] = {"OCTAVE DOWN","SEVENTH DOWN","SIXTH DOWN","FIFTH DOWN","FOURTH DOWN","THIRD DOWN","SECOND DOWN","UNISON","SECOND UP","THIRD UP","FOURTH UP","FIFTH UP","SIXTH UP","SEVENTH UP","OCTAVE UP"};
    static constexpr const char* pentatonic[] = {"OCTAVE DOWN","-4 SCALE NOTES","-3 SCALE NOTES","-2 SCALE NOTES","-1 SCALE NOTE","UNISON","+1 SCALE NOTE","+2 SCALE NOTES","+3 SCALE NOTES","+4 SCALE NOTES","OCTAVE UP"};
    const int value = harmonyClampInterval(mode, interval, scale);
    return mode == 1 ? fixed[value+12] : scale == 3 ? pentatonic[value+5] : diatonic[value+7];
}

namespace harmony_detail {
constexpr double pi = 3.14159265358979323846;
inline double clean(double value, double fallback = 0) noexcept { return std::isfinite(value) ? value : fallback; }
inline double gain(double db) noexcept { return db <= -60 ? 0 : std::pow(10.0, db / 20); }

// One shared, downsampled YIN-style detector for all three voices. Analysis uses
// real input samples, never the generated harmonies, and allocates in prepare.
class Detector {
public:
    void prepare(double fs) {
        fs_ = fs; decimation_ = std::max(1, static_cast<int>(std::round(fs / 12000)));
        rate_ = fs / decimation_; minLag_ = std::max(2, static_cast<int>(rate_ / 1000));
        maxLag_ = static_cast<int>(std::ceil(rate_ / 70)); hop_ = std::max(1, static_cast<int>(rate_ * .005));
        ring_.assign(static_cast<size_t>(maxLag_*2+32), 0); ordered_.resize(ring_.size()); difference_.resize(maxLag_+2);
        lowpass_ = 1 - std::exp(-2*pi*2500/fs); powerDecay_ = std::exp(-1/(fs*.02)); reset();
    }
    void reset() noexcept {
        std::fill(ring_.begin(),ring_.end(),0.0f); index_=filled_=divider_=count_=selected_=0;
        filter1_=filter2_=hz_=confidence_=centreDelay_=0; power_.fill(0);
    }
    bool sample(const std::array<float,2>& input, int channels) noexcept {
        for (int c=0;c<channels;++c) power_[c]=powerDecay_*power_[c]+(1-powerDecay_)*input[c]*input[c];
        if (channels == 2 && power_[1-selected_] > power_[selected_]*2) selected_=1-selected_;
        if (channels == 1) selected_=0; // Do not cancel opposite-polarity stereo vocals.
        filter1_+=lowpass_*(input[selected_]-filter1_); filter2_+=lowpass_*(filter1_-filter2_);
        if (++divider_ < decimation_) return false;
        divider_=0; ring_[index_]=static_cast<float>(filter2_); index_=(index_+1)%static_cast<int>(ring_.size());
        filled_=std::min(filled_+1,static_cast<int>(ring_.size()));
        if (++count_ < hop_) return false;
        count_=0; analyse(); return true;
    }
    float hz() const noexcept { return hz_; }
    float confidence() const noexcept { return confidence_; }
    int centreDelay() const noexcept { return centreDelay_; }
private:
    void analyse() noexcept {
        const int window=std::min(maxLag_,filled_/2-10);
        hz_=confidence_=0;
        centreDelay_=std::max(0,static_cast<int>(window*decimation_));
        if (window<=minLag_+1) return;
        double energy=0;
        for (int i=0;i<static_cast<int>(ordered_.size());++i) {
            ordered_[i]=ring_[(index_+static_cast<int>(ring_.size())-1-i)%static_cast<int>(ring_.size())];
            if (i<window*2) energy+=ordered_[i]*ordered_[i];
        }
        if (energy/(window*2)<4e-6) return;
        difference_[0]=1; double cumulative=0;
        for (int lag=1;lag<=window+1;++lag) {
            double error=0;
            for (int i=0;i<window;++i) { const double delta=ordered_[i]-ordered_[i+lag]; error+=delta*delta; }
            cumulative+=error; difference_[lag]=cumulative>1e-20?static_cast<float>(error*lag/cumulative):1;
        }
        int best=-1;
        for (int lag=minLag_;lag<window;++lag) if (difference_[lag]<.10f) {
            while(lag<window && difference_[lag+1]<difference_[lag]) ++lag;
            best=lag; break;
        }
        if (best<0 || best>=window) return;
        const double left=difference_[best-1], middle=difference_[best], right=difference_[best+1];
        const double curve=left-2*middle+right;
        double lag=best+(std::abs(curve)>1e-12?std::clamp(.5*(left-right)/curve,-.5,.5):0);
        for (double step : {.2,.05,.01}) {
            const double a=periodError(lag-step,window), b=periodError(lag,window), c=periodError(lag+step,window);
            const double curvature=a-2*b+c;
            if (curvature>1e-18) lag+=std::clamp(.5*(a-c)/curvature,-1.0,1.0)*step;
        }
        const double found=rate_/lag;
        if (found<70 || found>1000) return;
        hz_=static_cast<float>(found); confidence_=static_cast<float>(std::clamp(1-middle,0.0,1.0));
        centreDelay_=static_cast<int>(std::round((window-1+lag)*decimation_*.5+2*(1-lowpass_)/lowpass_));
    }
    double periodError(double lag, int window) const noexcept {
        constexpr int taps=16; const int whole=static_cast<int>(std::floor(lag)); const double fraction=lag-whole;
        std::array<double,taps> kernel{}; double sum=0;
        for(int k=0;k<taps;++k) {
            const double x=k-7-fraction;
            const double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);
            const double weight=std::abs(x)<8?.42+.5*std::cos(pi*x/8)+.08*std::cos(2*pi*x/8):0;
            kernel[k]=sinc*weight; sum+=kernel[k];
        }
        double error=0;
        for(int i=0;i<window;++i) {
            double shifted=0;
            for(int k=0;k<taps;++k) shifted+=ordered_[i+whole+k-7]*kernel[k]/sum;
            const double delta=ordered_[i]-shifted; error+=delta*delta;
        }
        return error;
    }
    std::vector<float> ring_,ordered_,difference_;
    std::array<double,2> power_{};
    double fs_=48000,rate_=12000,lowpass_=0,powerDecay_=0,filter1_=0,filter2_=0;
    float hz_=0,confidence_=0;
    int centreDelay_=0,decimation_=4,minLag_=12,maxLag_=172,hop_=60,index_=0,filled_=0,divider_=0,count_=0,selected_=0;
};

// Pitch-synchronous overlap/add of complete source periods. Source grains keep
// their waveform duration when formants are preserved; changing the grain read
// speed shifts their spectral envelope independently of the output pitch marks.
// Unlike a short FFT-bin mapper, an output cycle is scheduled from the measured
// fractional period, including at the full +/-12-semitone limits.
class VoiceEngine {
public:
    void setLiveMode(bool live) noexcept { live_=live; }
    void prepare(double fs,int,int channels) {
        fs_=fs;channels_=channels;taps_=live_?16:48;
        latency_=static_cast<int>(std::ceil(fs*(live_?.025:.068)))+taps_;
        int length=1024;while(length<fs*.4+1024)length*=2;mask_=length-1;
        for(auto& b:input_)b.assign(length,0);for(auto& b:output_)b.assign(length,0);
        kernel_.resize(static_cast<size_t>(banks*(phases+1)*taps_));
        for(int bank=0;bank<banks;++bank)for(int phase=0;phase<=phases;++phase) {
            const double fraction=double(phase)/phases,cutoff=.45/(1+bank/8.0);double total=0;
            for(int k=0;k<taps_;++k) {
                const double x=k-(taps_/2-1)-fraction;
                const double sinc=std::abs(x)<1e-12?2*cutoff:std::sin(2*pi*cutoff*x)/(pi*x);
                const double window=std::abs(x)<taps_*.5?.42+.5*std::cos(pi*x/(taps_*.5))+.08*std::cos(2*pi*x/(taps_*.5)):0;
                coefficient(bank,phase,k)=sinc*window;total+=sinc*window;
            }
            for(int k=0;k<taps_;++k)coefficient(bank,phase,k)/=total;
        }
        prepared_=true;reset();
    }
    void setParameters(float semitones,float formants,float hz) noexcept {
        semitones_=std::clamp(double(semitones),-12.0,12.0);formants_=std::clamp(double(formants),-12.0,12.0);
        if(hz>=70&&hz<=1000)hz_=hz;
    }
    void reset() noexcept {
        for(auto&b:input_)std::fill(b.begin(),b.end(),0.0f);for(auto&b:output_)std::fill(b.begin(),b.end(),0.0);
        clock_=0;nextCentre_=0;hz_=200;selected_=0;power_.fill(0);grains_.fill({});futureReads_=0;
    }
    int latencySamples() const noexcept {return latency_;}
    unsigned causalReadViolations() const noexcept {return futureReads_;}
    void process(float*const* audio,int channels,int frames) noexcept {
        if(!prepared_)return;
        for(int n=0;n<frames;++n,++clock_) {
            for(int c=0;c<channels_;++c) {
                const float value=c<channels?audio[c][n]:0;input_[c][static_cast<size_t>(clock_)&mask_]=value;
                power_[c]+=.001*(double(value)*value-power_[c]);
            }
            if(channels_==2&&power_[1-selected_]>power_[selected_]*2)selected_=1-selected_;
            const double period=fs_/hz_,ratio=std::exp2(semitones_/12),readSpeed=std::exp2(formants_/12);
            const double half=period/readSpeed,targetPeriod=period/ratio;
            // PRO renders a complete grain when its first sample is due.
            // LIVE schedules the grain then reads each sample only when due.
            if(nextCentre_-half<=clock_) {
                if(live_)scheduleGrain(nextCentre_,period,half,readSpeed,targetPeriod);
                else grain(nextCentre_,period,half,readSpeed,targetPeriod);
                nextCentre_+=targetPeriod;
                if(nextCentre_-half<clock_-2*period)nextCentre_=clock_+targetPeriod;
            }
            std::array<double,2> current{};
            if(live_)for(auto& g:grains_)if(g.active) {
                if(clock_>g.centre+g.half){g.active=false;continue;}
                const double delta=clock_-g.centre;
                const double amplitude=(.5+.5*std::cos(pi*delta/g.half))*g.amplitude;
                for(int c=0;c<channels_;++c)current[c]+=amplitude*read(c,g.source+delta*g.speed,g.bank);
            }
            for(int c=0;c<channels;++c) {
                const size_t pos=static_cast<size_t>(clock_)&mask_;
                const double value=std::abs(semitones_)<1e-7&&std::abs(formants_)<1e-7?
                    input_[c][static_cast<size_t>(clock_-latency_)&mask_]:(live_?current[c]:output_[c][pos]);
                audio[c][n]=static_cast<float>(clean(value));output_[c][pos]=0;
            }
        }
    }
private:
    double& coefficient(int bank,int phase,int tap) noexcept {return kernel_[(static_cast<size_t>(bank)*(phases+1)+phase)*taps_+tap];}
    double read(int channel,double position,int bank) noexcept {
        if(position<0)return 0;
        const auto whole=static_cast<std::int64_t>(std::floor(position));const double fraction=(position-whole)*phases;
        if(whole+taps_/2>clock_)++futureReads_;
        const int phase=std::clamp(static_cast<int>(fraction),0,phases-1);const double mix=fraction-phase;
        const double* a=kernel_.data()+(static_cast<size_t>(bank)*(phases+1)+phase)*taps_;const double* b=a+taps_;
        double out=0;
        for(int k=0;k<taps_;++k) {
            const auto at=whole+k-(taps_/2-1);
            if(at>=0&&at<=clock_)out+=input_[channel][static_cast<size_t>(at)&mask_]*(a[k]+mix*(b[k]-a[k]));
        }
        return out;
    }
    void scheduleGrain(double centre,double period,double half,double speed,double targetPeriod) noexcept {
        // Only a completed, past source period is searched. Its phase is
        // extrapolated two periods; actual waveform samples are read later as
        // they become due, not guessed from future input. The worst read lead
        // for a +/-octave grain is 1.5 source periods plus the interpolator.
        const double source=mark(centre-latency_-2*period,period)+2*period;
        for(auto& g:grains_)if(!g.active||clock_>g.centre+g.half) {
            g={centre,half,source,speed,targetPeriod/half,std::clamp(static_cast<int>(std::ceil((speed-1)*8)),0,banks-1),true};return;
        }
    }
    double mark(double desired,double period) const noexcept {
        if(desired-period*.5<1)return desired;
        auto best=static_cast<std::int64_t>(std::round(desired));double peak=-1e30;
        const auto begin=static_cast<std::int64_t>(std::ceil(desired-period*.5));
        const auto end=static_cast<std::int64_t>(std::floor(desired+period*.5));
        for(auto i=begin;i<=end&&i<clock_-1;++i) {
            const double value=input_[selected_][static_cast<size_t>(i)&mask_];
            if(value>peak){peak=value;best=i;}
        }
        const double a=input_[selected_][static_cast<size_t>(best-1)&mask_],b=input_[selected_][static_cast<size_t>(best)&mask_],c=input_[selected_][static_cast<size_t>(best+1)&mask_];
        const double curvature=a-2*b+c;
        return best+(std::abs(curvature)>1e-12?std::clamp(.5*(a-c)/curvature,-.5,.5):0);
    }
    void grain(double centre,double period,double half,double speed,double targetPeriod) noexcept {
        const double source=mark(centre-latency_,period);
        const int bank=std::clamp(static_cast<int>(std::ceil((speed-1)*8)),0,banks-1);
        const auto first=std::max(clock_,static_cast<std::int64_t>(std::ceil(centre-half)));
        const auto last=static_cast<std::int64_t>(std::floor(centre+half));
        for(auto i=first;i<=last;++i) {
            const double delta=i-centre,window=.5+.5*std::cos(pi*delta/half);
            const double amplitude=window*targetPeriod/half;
            for(int c=0;c<channels_;++c)output_[c][static_cast<size_t>(i)&mask_]+=read(c,source+delta*speed,bank)*amplitude;
        }
    }
    static constexpr int phases=256,banks=9;
    struct Grain {double centre=0,half=0,source=0,speed=1,amplitude=0;int bank=0;bool active=false;};
    std::array<Grain,16> grains_{};
    std::array<std::vector<float>,2> input_;
    std::array<std::vector<double>,2> output_;
    std::vector<double> kernel_;
    std::array<double,2> power_{};
    double fs_=48000,hz_=200,semitones_=0,formants_=0,nextCentre_=0;
    std::int64_t clock_=0;
    int channels_=1,latency_=0,taps_=16,mask_=32767,selected_=0;
    unsigned futureReads_=0;
    bool live_=true,prepared_=false;
};
}

// Three independent full-octave period-synchronous voices.
// prepare runs while stopped. Other mutating methods run on the audio thread.
// Only the meter getters are shared with the UI. Both engines are preallocated.
class HarmonyDSP {
public:
    static const char* intervalLabel(int mode,int interval,int scale) noexcept { return harmonyIntervalLabel(mode,interval,scale); }
    static int scaleLength(int scale) noexcept { return harmonyScaleLength(scale); }
    void prepare(double fs,int maxBlock,int channels) {
        prepared_=false;
        if(!std::isfinite(fs)||fs<8000||fs>192000) { latency_=maximumLatency_=0; return; }
        fs_=fs; channels_=std::clamp(channels,1,2); detector_.prepare(fs);
        for(auto& v:voices_) {
            v.live.setLiveMode(true); v.pro.setLiveMode(false);
            v.live.prepare(fs,maxBlock,channels_); v.pro.prepare(fs,maxBlock,channels_);
        }
        maximumLatency_=voices_[0].pro.latencySamples()+chunk;
        latency_=selected(0).latencySamples()+chunk;
        for(auto& buffer:dry_) buffer.assign(static_cast<size_t>(maximumLatency_+1),0);
        int history=1024; while(history<maximumLatency_+fs*.10) history*=2;
        voiceHistory_.assign(static_cast<size_t>(history),0); historyMask_=history-1;
        step_=1/std::max(1.0,fs*.005); peakDecay_=std::exp(-1/(fs*.3));
        prepared_=true; setParameters(parameters_); reset();
    }
    void setLiveMode(bool live) noexcept {
        if(live_==live) return;
        live_=live;
        if(prepared_) { latency_=selected(0).latencySamples()+chunk; reset(); }
    }
    void setParameters(const HarmonyParameters& p) noexcept {
        parameters_=p; parameters_.key=std::clamp(p.key,0,11); parameters_.scale=std::clamp(p.scale,0,3);
        parameters_.natural=static_cast<float>(std::clamp(harmony_detail::clean(p.natural,75),0.0,100.0));
        parameters_.outputDb=static_cast<float>(std::clamp(harmony_detail::clean(p.outputDb,-6),-18.0,0.0));
        for(auto& v:parameters_.voices) {
            v.intervalMode=std::clamp(v.intervalMode,0,1);
            v.interval=harmonyClampInterval(v.intervalMode,v.interval,parameters_.scale);
            v.levelDb=static_cast<float>(std::clamp(harmony_detail::clean(v.levelDb,-60),-60.0,0.0));
            v.pan=static_cast<float>(std::clamp(harmony_detail::clean(v.pan),-100.0,100.0));
        }
        if(!processed_) snapGains();
    }
    void reset() noexcept {
        for(auto& v:voices_) { v.live.reset(); v.pro.reset(); for(auto& c:v.output)c.fill(0); v.pitch=0; v.gate=0; v.peak=0; }
        for(auto& c:frame_)c.fill(0); for(auto& c:dry_)std::fill(c.begin(),c.end(),0.0f);
        std::fill(voiceHistory_.begin(),voiceHistory_.end(),0); detector_.reset();
        phase_=dryAt_=0; clock_=0; lastVoiceTime_=-1; lastVoiced_=false; processed_=false;
        inputPeakValue_=outputPeakValue_=0; hzView_.store(0); confidenceView_.store(0);
        inputPeakView_.store(0);outputPeakView_.store(0);
        for(auto& value:voiceLevelView_)value.store(0); for(auto& value:voiceSemitonesView_)value.store(0);
        snapGains(); updateVoiceParameters();
    }
    int latencySamples() const noexcept { return latency_; }
    int maximumLatencySamples() const noexcept { return maximumLatency_; }
    unsigned causalReadViolations() const noexcept {
        unsigned count=0;for(const auto& v:voices_)count+=v.live.causalReadViolations()+v.pro.causalReadViolations();return count;
    }
    float detectedHz() const noexcept { return hzView_.load(std::memory_order_relaxed); }
    float confidence() const noexcept { return confidenceView_.load(std::memory_order_relaxed); }
    float inputPeak() const noexcept { return inputPeakView_.load(std::memory_order_relaxed); }
    float outputPeak() const noexcept { return outputPeakView_.load(std::memory_order_relaxed); }
    float voiceLevel(int voice) const noexcept { return voice>=0&&voice<3?voiceLevelView_[voice].load(std::memory_order_relaxed):0; }
    float voiceSemitones(int voice) const noexcept { return voice>=0&&voice<3?voiceSemitonesView_[voice].load(std::memory_order_relaxed):0; }

    // Exact scale lookup, also usable by the UI to display the target interval.
    static float scaleSemitones(float hz,int key,int scale,int steps) noexcept {
        if(!std::isfinite(hz)||hz<=0)return 0;
        key=std::clamp(key,0,11);scale=std::clamp(scale,0,3);
        const int count=harmonyScaleLength(scale);steps=std::clamp(steps,-count,count);
        if(steps==count)return 12; if(steps==-count)return -12;
        static constexpr int notes[4][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,8,11},{0,3,5,7,10,0,0}};
        const double midi=69+12*std::log2(hz/440.0); int degree=0,baseOctave=0;double distance=1e9;
        const int oct=static_cast<int>(std::floor((midi-key)/12));
        for(int o=oct-1;o<=oct+1;++o)for(int d=0;d<count;++d) {
            const double error=std::abs(key+o*12+notes[scale][d]-midi);
            if(error<distance) { distance=error;degree=d;baseOctave=o; }
        }
        int target=degree+steps;
        while(target<0){target+=count;--baseOctave;} while(target>=count){target-=count;++baseOctave;}
        return static_cast<float>(std::clamp(key+baseOctave*12+notes[scale][target]-midi,-12.0,12.0));
    }

    void process(float*const* audio,int channels,int frames) noexcept {
        if(!prepared_||!audio||frames<=0||channels<=0)return;
        const int active=std::min(channels_,channels); for(int c=0;c<active;++c)if(!audio[c])return;
        processed_=true;
        for(int n=0;n<frames;++n,++clock_) {
            std::array<float,2> input{};
            for(int c=0;c<active;++c) { input[c]=static_cast<float>(std::clamp(harmony_detail::clean(audio[c][n]),-32.0,32.0)); frame_[c][phase_]=input[c]; }
            for(int c=active;c<channels_;++c)frame_[c][phase_]=0;
            if(detector_.sample(input,active))publishVoicing();
            const auto delayedTime=clock_-latency_;
            const bool voiced=delayedTime<0?false:delayedTime>lastVoiceTime_?lastVoiced_:voiceHistory_[static_cast<size_t>(delayedTime)&historyMask_]!=0;
            slew(direct_,parameters_.direct?1:0); slew(bypass_,parameters_.bypass?1:0);
            slew(output_,harmony_detail::gain(parameters_.outputDb));
            const int read=(dryAt_+static_cast<int>(dry_[0].size())-latency_)%static_cast<int>(dry_[0].size());
            std::array<double,2> mixed{},dry{};
            for(int c=0;c<active;++c) { dry[c]=dry_[c][read];dry_[c][dryAt_]=input[c];mixed[c]=direct_*dry[c]; }
            for(int voice=0;voice<3;++voice) {
                auto& v=voices_[voice];const auto& p=parameters_.voices[voice];
                slew(v.gain,p.enabled?harmony_detail::gain(p.levelDb):0);
                slew(v.pan,p.pan*.01); slew(v.gate,p.intervalMode==1||voiced?1:0);
                const double amount=v.gain*v.gate;
                const double angle=(v.pan+1)*harmony_detail::pi*.25;
                double peak=0;
                for(int c=0;c<active;++c) {
                    const double pan=active==1?1:std::sqrt(2.0)*(c==0?std::cos(angle):std::sin(angle));
                    const double value=amount*pan*v.output[c][phase_]; mixed[c]+=value;peak=std::max(peak,std::abs(value));
                }
                v.peak=std::max(peak,v.peak*peakDecay_);
            }
            double inPeak=0,outPeak=0;
            for(int c=0;c<active;++c) {
                const double wet=mixed[c]*output_;
                const double result=bypass_==1?dry[c]:wet+bypass_*(dry[c]-wet);
                audio[c][n]=static_cast<float>(std::clamp(harmony_detail::clean(result),-32.0,32.0));
                inPeak=std::max(inPeak,std::abs(double(input[c])));outPeak=std::max(outPeak,std::abs(double(audio[c][n])));
            }
            inputPeakValue_=std::max(inPeak,inputPeakValue_*peakDecay_);outputPeakValue_=std::max(outPeak,outputPeakValue_*peakDecay_);
            dryAt_=(dryAt_+1)%static_cast<int>(dry_[0].size());
            if(++phase_==chunk) {
                updateVoiceParameters();
                for(int voice=0;voice<3;++voice) {
                    for(int c=0;c<channels_;++c) voices_[voice].output[c]=frame_[c];
                    float* data[]{voices_[voice].output[0].data(),voices_[voice].output[1].data()};
                    selected(voice).process(data,channels_,chunk);
                }
                phase_=0;
            }
        }
        hzView_.store(detector_.hz(),std::memory_order_relaxed);confidenceView_.store(detector_.confidence(),std::memory_order_relaxed);
        inputPeakView_.store(static_cast<float>(inputPeakValue_),std::memory_order_relaxed);outputPeakView_.store(static_cast<float>(outputPeakValue_),std::memory_order_relaxed);
        for(int v=0;v<3;++v) { voiceLevelView_[v].store(static_cast<float>(voices_[v].peak*output_),std::memory_order_relaxed);voiceSemitonesView_[v].store(voices_[v].pitch,std::memory_order_relaxed); }
    }
private:
    static constexpr int chunk=64;
    struct VoiceState { harmony_detail::VoiceEngine live,pro;std::array<std::array<float,chunk>,2> output{};double gain=0,pan=0,gate=0,peak=0;float pitch=0; };
    harmony_detail::VoiceEngine& selected(int voice) noexcept { return live_?voices_[voice].live:voices_[voice].pro; }
    void slew(double& value,double target) const noexcept { value+=std::clamp(target-value,-step_,step_); }
    void snapGains() noexcept {
        direct_=parameters_.direct?1:0;bypass_=parameters_.bypass?1:0;output_=harmony_detail::gain(parameters_.outputDb);
        for(int i=0;i<3;++i) { const auto& p=parameters_.voices[i];auto& v=voices_[i];v.gain=p.enabled?harmony_detail::gain(p.levelDb):0;v.pan=p.pan*.01;v.gate=p.intervalMode==1?1:0; }
    }
    void updateVoiceParameters() noexcept {
        for(int i=0;i<3;++i) {
            auto& v=voices_[i];const auto& p=parameters_.voices[i];
            if(p.intervalMode==1)v.pitch=static_cast<float>(p.interval);
            else if(detector_.hz()>0)v.pitch=scaleSemitones(detector_.hz(),parameters_.key,parameters_.scale,p.interval);
            const float formant=v.pitch*(1-parameters_.natural*.01f);
            v.live.setParameters(v.pitch,formant,detector_.hz());v.pro.setParameters(v.pitch,formant,detector_.hz());
        }
    }
    void publishVoicing() noexcept {
        const auto when=std::max(lastVoiceTime_+1,clock_-detector_.centreDelay());
        if(when<0)return;
        const bool next=detector_.hz()>0&&detector_.confidence()>=.90f;
        const auto middle=(lastVoiceTime_+when)/2;
        for(auto time=std::max<std::int64_t>(0,lastVoiceTime_+1);time<=when;++time)
            voiceHistory_[static_cast<size_t>(time)&historyMask_]=static_cast<unsigned char>(time<=middle?lastVoiced_:next);
        lastVoiceTime_=when;lastVoiced_=next;
    }
    HarmonyParameters parameters_{};
    std::array<VoiceState,3> voices_;
    harmony_detail::Detector detector_;
    std::array<std::vector<float>,2> dry_;
    std::vector<unsigned char> voiceHistory_;
    std::array<std::array<float,chunk>,2> frame_{};
    std::atomic<float> hzView_{0},confidenceView_{0},inputPeakView_{0},outputPeakView_{0};
    std::array<std::atomic<float>,3> voiceLevelView_{},voiceSemitonesView_{};
    double fs_=48000,step_=.001,peakDecay_=0,direct_=1,bypass_=0,output_=1,inputPeakValue_=0,outputPeakValue_=0;
    int channels_=1,latency_=0,maximumLatency_=0,phase_=0,dryAt_=0,historyMask_=1023;
    std::int64_t clock_=0,lastVoiceTime_=-1;
    bool live_=true,prepared_=false,processed_=false,lastVoiced_=false;
};
}
