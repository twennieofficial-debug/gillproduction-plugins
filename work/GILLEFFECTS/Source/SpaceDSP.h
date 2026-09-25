#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace gill {

// Original algorithmic reverb: input diffusion and an eight-line orthogonal FDN.
// Mono/stereo planar audio; lifecycle and parameter calls belong to the audio
// thread, with prepare while stopped. No allocation or locking in process.
class SpaceDSP {
public:
    void prepare(double sampleRate, int maxBlock, int channels) {
        (void)maxBlock;
        prepared_ = false;
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) {
            reset();
            return;
        }
        sampleRate_ = sampleRate;
        channels_ = std::clamp(channels, 1, 2);
        mixRampSamples_ = std::max(1, static_cast<int>(std::round(sampleRate_ * 0.005)));
        colourRampSamples_ = std::max(1, static_cast<int>(std::round(sampleRate_ * 0.020)));
        headFadeSamples_ = std::max(1, static_cast<int>(std::round(sampleRate_ * 0.050)));
        highpassPole_ = static_cast<float>(std::exp(-2.0 * pi * 65.0 / sampleRate_));
        for (auto& line : tank_) line.prepare(static_cast<int>(std::ceil(sampleRate_ * 0.190)) + 8);
        for (auto& line : predelay_) line.prepare(static_cast<int>(std::ceil(sampleRate_ * 0.235)) + 8);
        constexpr double diffusionMs[2][3]{{3.17,5.43,8.29},{3.73,6.13,9.19}};
        for (int c = 0; c < 2; ++c) for (int a = 0; a < 3; ++a)
            diffusion_[c][a].prepare(std::max(2, static_cast<int>(std::round(diffusionMs[c][a] * 0.001 * sampleRate_))));
        for (int i = 0; i < 8; ++i)
            modulationStep_[i] = static_cast<float>((0.067 + i * 0.0173) / sampleRate_);
        prepared_ = true;
        reset();
    }

    void reset() noexcept {
        for (auto& line : tank_) line.clear();
        for (auto& line : predelay_) line.clear();
        for (auto& channel : diffusion_) for (auto& stage : channel) stage.clear();
        dampingState_.fill(0.0f);
        highpassInput_.fill(0.0f);
        highpassOutput_.fill(0.0f);
        for (int i = 0; i < 8; ++i) modulationPhase_[i] = (i + 0.37f) * 0.117f;
        hasProcessed_ = false;
        tailDecayHold_ = decaySeconds_;
        updateTargets(true);
        refreshTail();
    }

    // style 0=ROOM, 1=HALL, 2=PLATE. Decay is nominal low-frequency RT60.
    // MIX is linear dry/wet, with a finite 5 ms ramp and exact endpoint snapping.
    void setParameters(float mix, float decaySeconds, float predelayMs,
                       float tone, float size, float width, int style, float dry=100) noexcept {
        const float direct = finiteClamp(dry,0.0f,100.0f)*0.01f;
        const float m = finiteClamp(mix,0.0f,100.0f) * 0.01f;
        const float d = finiteClamp(decaySeconds,0.2f,15.0f);
        const float p = finiteClamp(predelayMs,0.0f,200.0f);
        const float t = finiteClamp(tone,0.0f,100.0f) * 0.01f;
        const float s = finiteClamp(size,0.0f,100.0f) * 0.01f;
        const float w = finiteClamp(width,0.0f,100.0f) * 0.01f;
        const int character = std::clamp(style,0,2);
        if (m==mixTarget_ && d==decaySeconds_ && p==predelayMs_ && t==tone_ &&
            s==size_ && w==widthTarget_ && character==style_ && direct==dryTarget_) return;
        dryTarget_=direct;
        mixTarget_=m;decaySeconds_=d;predelayMs_=p;tone_=t;size_=s;widthTarget_=w;style_=character;
        if (hasProcessed_) tailDecayHold_ = std::max(tailDecayHold_,decaySeconds_);
        else tailDecayHold_ = decaySeconds_;
        updateTargets(!hasProcessed_);
        refreshTail();
    }

    void process(float* const* buffers, int channels, int samples) noexcept {
        if (!prepared_ || !buffers || samples<=0 || channels<=0) return;
        const int active=std::min(channels_,channels);
        for(int c=0;c<active;++c) if(!buffers[c]) return;
        hasProcessed_=true;
        constexpr float norm=0.3535533905932738f;
        constexpr float injectLeft[8]{1,1,-1,-1,1,1,-1,-1};
        constexpr float injectRight[8]{1,-1,1,-1,1,-1,1,-1};
        constexpr float outputLeft[8]{1,1,1,1,-1,-1,-1,-1};
        constexpr float outputRight[8]{1,-1,-1,1,-1,1,1,-1};
        for(int n=0;n<samples;++n) {
            const float dryL=finiteInput(buffers[0][n]);
            const float dryR=active==2?finiteInput(buffers[1][n]):dryL;
            const float mix=mix_.next(),direct=dry_.next(),width=width_.next(),damping=damping_.next();
            const float diffuse=diffusionGain_.next(),inGain=inputGain_.next();
            const float earlyGain=earlyGain_.next(),earlyScale=earlyScale_.next(),depth=modDepth_.next();
            const auto preHead=predelayHead_.next(headFadeSamples_);
            const float input[2]{std::clamp(dryL,-8.0f,8.0f),std::clamp(dryR,-8.0f,8.0f)};
            for(int c=0;c<2;++c) {
                const float h=zap(input[c]-highpassInput_[c]+highpassPole_*highpassOutput_[c]);
                highpassInput_[c]=input[c];highpassOutput_[c]=h;
                predelay_[c].write(h);
            }
            float diffuseL=readHead(predelay_[0],preHead,0),diffuseR=readHead(predelay_[1],preHead,0);
            const float erL=0.40f*readHead(predelay_[0],preHead,earlyScale*0.0043f*static_cast<float>(sampleRate_))
                +0.30f*readHead(predelay_[1],preHead,earlyScale*0.0071f*static_cast<float>(sampleRate_))
                -0.24f*readHead(predelay_[0],preHead,earlyScale*0.0119f*static_cast<float>(sampleRate_))
                +0.18f*readHead(predelay_[1],preHead,earlyScale*0.0187f*static_cast<float>(sampleRate_));
            const float erR=0.35f*readHead(predelay_[1],preHead,earlyScale*0.0057f*static_cast<float>(sampleRate_))
                -0.25f*readHead(predelay_[0],preHead,earlyScale*0.0093f*static_cast<float>(sampleRate_))
                +0.29f*readHead(predelay_[1],preHead,earlyScale*0.0157f*static_cast<float>(sampleRate_))
                +0.19f*readHead(predelay_[0],preHead,earlyScale*0.0221f*static_cast<float>(sampleRate_));
            predelay_[0].advance();predelay_[1].advance();
            for(int a=0;a<3;++a) {
                diffuseL=diffusion_[0][a].process(diffuseL,diffuse);
                diffuseR=diffusion_[1][a].process(diffuseR,diffuse);
            }
            float values[8];float wetL=0,wetR=0;
            for(int i=0;i<8;++i) {
                modulationPhase_[i]+=modulationStep_[i];
                if(modulationPhase_[i]>=1.0f) modulationPhase_[i]-=1.0f;
                const float x=2.0f*modulationPhase_[i]-1.0f;
                const float wave=4.0f*x*(1.0f-std::abs(x));
                const auto head=tankHeads_[i].next(headFadeSamples_);
                const float delayed=readHead(tank_[i],head,depth*wave);
                dampingState_[i]=zap(delayed+damping*(dampingState_[i]-delayed));
                values[i]=dampingState_[i]*feedback_[i].next();
                wetL+=outputLeft[i]*delayed;wetR+=outputRight[i]*delayed;
            }
            // Normalized Walsh-Hadamard transform: orthogonal, no feedback gain.
            for(int stride=1;stride<8;stride*=2) for(int start=0;start<8;start+=2*stride)
                for(int i=0;i<stride;++i) {const float a=values[start+i],b=values[start+i+stride];
                    values[start+i]=a+b;values[start+i+stride]=a-b;}
            for(int i=0;i<8;++i) {
                const float driven=inGain*(injectLeft[i]*diffuseL+injectRight[i]*diffuseR)*norm;
                tank_[i].write(zap(std::clamp(values[i]*norm+driven,-8.0f,8.0f)));
                tank_[i].advance();
            }
            wetL=wetL*norm*1.6f+erL*earlyGain;
            wetR=wetR*norm*1.6f+erR*earlyGain;
            const float mid=0.5f*(wetL+wetR),side=0.5f*(wetL-wetR)*width;
            wetL=mid+side;wetR=mid-side;
            // Explicit branches preserve exact dry and wet endpoints.
            if(active==1) buffers[0][n]=direct==1?(mix==0?dryL:mix==1?mid:dryL+mix*(mid-dryL)):(1-mix)*direct*dryL+mix*mid;
            else {
                buffers[0][n]=direct==1?(mix==0?dryL:mix==1?wetL:dryL+mix*(wetL-dryL)):(1-mix)*direct*dryL+mix*wetL;
                buffers[1][n]=direct==1?(mix==0?dryR:mix==1?wetR:dryR+mix*(wetR-dryR)):(1-mix)*direct*dryR+mix*wetR;
            }
        }
    }

    int latencySamples() const noexcept {return 0;}
    // Conservative -120 dB nominal tail plus the maximum pre/diffusion/tank path.
    // Retains the longest decay since reset so shortening a preset cannot cause
    // the host to truncate an already ringing tail.
    double tailSeconds() const noexcept {return reportedTail_.load(std::memory_order_relaxed);}

private:
    static constexpr double pi=3.14159265358979323846;
    static float finiteClamp(float x,float lo,float hi) noexcept {return std::isfinite(x)?std::clamp(x,lo,hi):lo;}
    static float finiteInput(float x) noexcept {return std::isfinite(x)?x:0.0f;}
    static float zap(float x) noexcept {return std::isfinite(x)&&std::abs(x)>=1.0e-20f?x:0.0f;}
    struct Ramp {
        double value=0,target=0,step=0;int remaining=0;
        void set(double next,int length,bool immediate) noexcept {
            if(immediate){value=target=next;step=0;remaining=0;return;}
            if(next==target)return;
            target=next;remaining=std::max(1,length);step=(target-value)/remaining;
        }
        float next() noexcept {if(remaining>0){value+=step;if(--remaining==0)value=target;}return static_cast<float>(value);}
    };
    struct Ring {
        std::vector<float> data;int index=0;
        void prepare(int count){data.assign(static_cast<size_t>(count),0.0f);index=0;}
        void clear() noexcept {std::fill(data.begin(),data.end(),0.0f);index=0;}
        void write(float x) noexcept {data[index]=x;}
        void advance() noexcept {if(++index==static_cast<int>(data.size()))index=0;}
        float read(float delay) const noexcept {
            delay=std::clamp(delay,0.0f,static_cast<float>(data.size()-2));
            const int integer=static_cast<int>(delay);const float fraction=delay-integer;
            int newer=index-integer;if(newer<0)newer+=static_cast<int>(data.size());
            int older=newer-1;if(older<0)older+=static_cast<int>(data.size());
            return data[newer]+fraction*(data[older]-data[newer]);
        }
    };
    struct Allpass {
        std::vector<float> data;int index=0;
        void prepare(int count){data.assign(static_cast<size_t>(count),0.0f);index=0;}
        void clear() noexcept {std::fill(data.begin(),data.end(),0.0f);index=0;}
        float process(float input,float gain) noexcept {
            const float output=data[index]-gain*input;
            data[index]=zap(input+gain*output);
            if(++index==static_cast<int>(data.size()))index=0;
            return output;
        }
    };
    struct HeadValues {float from,to,blend;};
    struct Head {
        float current=0,destination=0,pending=0;int remaining=0;
        void set(float next,bool immediate) noexcept {
            pending=next;
            if(immediate){current=destination=next;remaining=0;}
        }
        HeadValues next(int fadeLength) noexcept {
            if(remaining==0&&pending!=current){destination=pending;remaining=fadeLength;}
            if(remaining==0)return{current,current,0};
            const float blend=static_cast<float>(fadeLength-remaining+1)/fadeLength;
            const HeadValues result{current,destination,blend};
            if(--remaining==0)current=destination;
            return result;
        }
    };
    static float readHead(const Ring& ring,HeadValues head,float offset) noexcept {
        const float a=ring.read(head.from+offset);
        return head.blend==0?a:a+head.blend*(ring.read(head.to+offset)-a);
    }
    void updateTargets(bool immediate) noexcept {
        constexpr double lengthsMs[8]{29.7,37.1,41.1,43.7,47.9,53.3,59.9,67.7};
        constexpr double characterSize[3]{0.78,1.18,0.95};
        constexpr double characterTone[3]{0.85,0.90,1.20};
        constexpr double characterDiffusion[3]{0.55,0.69,0.74};
        constexpr double characterEarly[3]{0.30,0.16,0.12};
        constexpr double characterModulation[3]{0.000025,0.00015,0.000075};
        const double spaceScale=(0.40+1.75*size_)*characterSize[style_];
        double gainSquared=0;
        for(int i=0;i<8;++i) {
            const double seconds=lengthsMs[i]*0.001*spaceScale;
            const double gain=std::pow(10.0,-3.0*seconds/decaySeconds_);
            tankHeads_[i].set(static_cast<float>(seconds*sampleRate_),immediate);
            feedback_[i].set(gain,colourRampSamples_,immediate);gainSquared+=gain*gain;
        }
        const double cutoff=std::min(sampleRate_*0.42,1500.0*std::pow(12.0,tone_)*characterTone[style_]);
        damping_.set(std::exp(-2.0*pi*cutoff/sampleRate_),colourRampSamples_,immediate);
        const double diffuse=std::min(characterDiffusion[style_],std::pow(10.0,-3.0*0.010/decaySeconds_));
        diffusionGain_.set(diffuse,colourRampSamples_,immediate);
        inputGain_.set(0.8*std::sqrt(std::max(0.0001,1.0-gainSquared/8.0)),colourRampSamples_,immediate);
        earlyGain_.set(characterEarly[style_],colourRampSamples_,immediate);
        earlyScale_.set(0.4+0.9*size_,colourRampSamples_,immediate);
        modDepth_.set(characterModulation[style_]*sampleRate_*(0.4+0.6*size_),colourRampSamples_,immediate);
        mix_.set(mixTarget_,mixRampSamples_,immediate);
        dry_.set(dryTarget_,mixRampSamples_,immediate);
        width_.set(widthTarget_,colourRampSamples_,immediate);
        predelayHead_.set(static_cast<float>(predelayMs_*0.001*sampleRate_),immediate);
    }
    void refreshTail() noexcept {
        reportedTail_.store(prepared_?2.0*std::max(decaySeconds_,tailDecayHold_)+0.70:0.0,std::memory_order_relaxed);
    }

    std::array<Ring,8> tank_;
    std::array<Ring,2> predelay_;
    std::array<std::array<Allpass,3>,2> diffusion_;
    std::array<Head,8> tankHeads_;
    Head predelayHead_;
    std::array<Ramp,8> feedback_;
    Ramp mix_,dry_,width_,damping_,diffusionGain_,inputGain_,earlyGain_,earlyScale_,modDepth_;
    std::array<float,8> dampingState_{},modulationPhase_{},modulationStep_{};
    std::array<float,2> highpassInput_{},highpassOutput_{};
    double sampleRate_=48000;
    std::atomic<double> reportedTail_{0.0};
    float mixTarget_=0.25f,dryTarget_=1.f,decaySeconds_=2.0f,predelayMs_=20.0f,tone_=0.60f,size_=0.60f,widthTarget_=1.0f;
    float tailDecayHold_=2.0f,highpassPole_=0.99f;
    int channels_=2,style_=1,mixRampSamples_=240,colourRampSamples_=960,headFadeSamples_=2400;
    bool prepared_=false,hasProcessed_=false;
};

} // namespace gill
