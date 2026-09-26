#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace gill::mix07 {
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free,
              "MIX/LINK audio requires native lock-free numeric atomics");
constexpr int maxLinks = 64, maxLearnFrames = 15000;
enum class Role : uint32_t { automatic, main, doubleVoice, adlib, beat };
inline const char* roleName(Role r) noexcept {
    constexpr const char* names[]{"AUTO", "MAIN", "DOUBLE", "ADLIB", "BEAT"};
    return names[std::min(4u, static_cast<unsigned>(r))];
}

// Every writer obtains a unique stamp; equality is the conflict contract.
// Stamp issue order is not assumed to be publication order under concurrency.
class GainValue {
public:
    GainValue() noexcept { set(0); }
    static uint16_t encode(float db) noexcept { return static_cast<uint16_t>(std::lround((std::clamp(db,-24.f,6.f)+24.f)*100.f)); }
    static float decode(uint64_t word) noexcept { return static_cast<float>(word & 65535u)*.01f-24.f; }
    uint64_t snapshot() const noexcept { return value.load(std::memory_order_acquire); }
    float db() const noexcept { return decode(snapshot()); }
    void set(float db) noexcept {
        if (!std::isfinite(db)) return;
        value.exchange(newWord(db), std::memory_order_acq_rel); dirty.store(true,std::memory_order_release);
    }
    bool compareSet(uint64_t expected, float db, uint64_t& accepted) noexcept {
        if (!std::isfinite(db) || db < -24 || db > 6 || !remoteAllowed.load(std::memory_order_relaxed)) return false;
        const auto next = newWord(db);
        if (!remoteAllowed.load(std::memory_order_relaxed)) return false;
        if (!value.compare_exchange_strong(expected,next,std::memory_order_acq_rel)) return false;
        accepted=next; dirty.store(true,std::memory_order_release); return true;
    }
    bool takeDirty() noexcept { return dirty.exchange(false,std::memory_order_acq_rel); }
private:
    uint64_t newWord(float db) noexcept {
        const auto stamp=counter.fetch_add(1,std::memory_order_relaxed);
        if (stamp >= (uint64_t(1)<<48)) remoteAllowed.store(false,std::memory_order_relaxed);
        return (stamp<<16)|encode(db);
    }
    alignas(64) std::atomic<uint64_t> value{0};
    std::atomic<uint64_t> counter{1};
    std::atomic<bool> dirty{false}, remoteAllowed{true};
};

template<class T, size_t Capacity> class SpscRing {
    static_assert(Capacity > 1 && std::is_trivially_copyable_v<T>);
public:
    bool push(const T& item) noexcept {
        const auto w=write.load(std::memory_order_relaxed);
        if (w-read.load(std::memory_order_acquire)>=Capacity) return false;
        data[w%Capacity]=item; write.store(w+1,std::memory_order_release); return true;
    }
    bool pop(T& item) noexcept {
        const auto r=read.load(std::memory_order_relaxed);
        if (r==write.load(std::memory_order_acquire)) return false;
        item=data[r%Capacity]; read.store(r+1,std::memory_order_release); return true;
    }
private:
    std::array<T,Capacity> data{};
    alignas(64) std::atomic<uint64_t> write{0};
    alignas(64) std::atomic<uint64_t> read{0};
};

struct TelemetryFrame {
    uint64_t learn=0, sequence=0, segment=0;
    int64_t position=0;
    uint32_t rate=0, samples=0, flags=0, reserved=0;
    double energy=0, lowEnergy=0, highEnergy=0, sideEnergy=0;
    float inputPeak=0, outputPeak=0, levelDb=-120, gainDb=0, crossings=0, voiced=0;
    uint64_t dropped=0;
    uint64_t padding[2]{};
};
static_assert(sizeof(TelemetryFrame)==128 && std::is_trivially_copyable_v<TelemetryFrame>);
struct AudioContext { int64_t position=0; bool positionValid=false, playing=false; };

class LinkAudio {
public:
    void prepare(double rate, float savedDb) noexcept {
        fs=std::isfinite(rate)&&rate>=8000&&rate<=384000?rate:48000;
        frameSize=std::max(1,static_cast<int>(std::lround(fs*.020))); rampSize=std::max(1,static_cast<int>(std::lround(fs*.030)));
        target=current=std::pow(10.0,static_cast<double>(std::clamp(savedDb,-24.f,6.f))*.05); left=0;
        lowCoefficient=1-std::exp(-2*3.141592653589793*300/fs); highCoefficient=1-std::exp(-2*3.141592653589793*4000/fs);
        low={}; high={}; voiceWindow={};voiceCount=voiceWrite=0;voicePhase=voiceFilter=0;
        voiceCoefficient=1-std::exp(-2*3.141592653589793*1000/fs);
        resetFrame(); expectedPosition=0; hadPosition=false; ++segment;
    }
    void requestLearn(uint64_t id) noexcept { desiredLearn.store(id,std::memory_order_release); }
    bool pop(TelemetryFrame& frame) noexcept { return ring.pop(frame); }
    template<typename T> void process(T* const* channels,int count,int samples,float db,bool bypass,const AudioContext& context,bool alter=true,bool pro=false) noexcept {
        if (count<1||count>2||samples<=0) return;
        const auto requested=desiredLearn.load(std::memory_order_acquire);
        constexpr int64_t positionLimit=int64_t(1)<<60;
        const auto position=std::clamp(context.position,-positionLimit,positionLimit);
        const bool positionValid=context.positionValid&&position==context.position;
        const bool discontinuity=positionValid&&hadPosition&&position!=expectedPosition;
        if (requested!=learn||discontinuity) { learn=requested; resetFrame(); ++segment; }
        if (discontinuity) { low={}; high={}; voiceWindow={};voiceCount=voiceWrite=0;voicePhase=voiceFilter=0; }
        hadPosition=positionValid; expectedPosition=position+samples;
        const double next=alter&&!bypass&&std::isfinite(db)?std::pow(10.,std::clamp(static_cast<double>(db),-24.,6.)*.05):1.;
        if (next!=target) { target=next; left=rampSize; step=(target-current)/left; }
        float blockIn=0,blockOut=0;
        for(int n=0;n<samples;++n){
            if(left>0){current+=step;if(--left==0)current=target;}
            double in[2]{}; float peakIn=0,peakOut=0;
            for(int c=0;c<count;++c){
                const double original=std::isfinite(static_cast<double>(channels[c][n]))?static_cast<double>(channels[c][n]):0.;
                const double x=std::clamp(original,-1.0e6,1.0e6);in[c]=x;
                const double y=alter?original*current:original;const T output=static_cast<T>(y);channels[c][n]=std::isfinite(static_cast<double>(output))?output:T(0);
                peakIn=std::max(peakIn,static_cast<float>(std::abs(x)));peakOut=std::max(peakOut,static_cast<float>(std::min(1.0e6,std::abs(static_cast<double>(channels[c][n])))));
                low[c]+=lowCoefficient*(x-low[c]);high[c]+=highCoefficient*(x-high[c]);
                square+=x*x/count;loSquare+=low[c]*low[c]/count;const double hi=x-high[c];hiSquare+=hi*hi/count;
            }
            const double mono=count==2?(in[0]+in[1])*.5:in[0],side=count==2?(in[0]-in[1])*.5:0.;
            voiceFilter+=voiceCoefficient*(mono-voiceFilter);voicePhase+=4000;
            if(voicePhase>=fs){voicePhase-=fs;voiceWindow[static_cast<size_t>(voiceWrite)]=static_cast<float>(voiceFilter);voiceWrite=(voiceWrite+1)%160;voiceCount=std::min(160,voiceCount+1);}
            sideSquare+=side*side;if((mono>=0)!=(previous>=0))++zeroCrossings;previous=mono;
            blockIn=std::max(blockIn,peakIn);blockOut=std::max(blockOut,peakOut);
            if(frameCount==0){framePosition=position+n;frameFlags=(positionValid?1u:0u)|(context.playing?2u:0u);}
            if(bypass)frameFlags|=4u;
            frameIn=std::max(frameIn,peakIn);frameOut=std::max(frameOut,peakOut);
            if(++frameCount>=frameSize){
                TelemetryFrame f{};f.learn=learn;f.sequence=++sequence;f.segment=segment;f.position=framePosition;f.rate=static_cast<uint32_t>(std::lround(fs));f.samples=static_cast<uint32_t>(frameCount);f.flags=frameFlags;
                f.energy=square/frameCount;f.lowEnergy=loSquare/frameCount;f.highEnergy=hiSquare/frameCount;f.sideEnergy=sideSquare/frameCount;f.inputPeak=frameIn;f.outputPeak=frameOut;f.gainDb=db;
                f.levelDb=static_cast<float>(10*std::log10(std::max(1e-12,f.energy)));f.crossings=static_cast<float>(zeroCrossings*fs/(2*frameCount));
                f.voiced=f.levelDb>-55&&f.crossings>=65&&f.crossings<=1200&&f.highEnergy<std::max(1e-12,f.energy)*.45?1.f:0.f;
                if(pro)f.voiced=f.levelDb>-55&&periodicity()>.70f?1.f:0.f;
                f.dropped=dropped;
                if(!ring.push(f))++dropped;resetFrame();
            }
        }
        inputPeak.store(blockIn,std::memory_order_relaxed);outputPeak.store(blockOut,std::memory_order_relaxed);
    }
    std::atomic<float> inputPeak{0},outputPeak{0};
private:
    float periodicity()const noexcept {
        if(voiceCount<160)return 0;double best=0;
        for(int lag=5;lag<=61;++lag){double cross=0,aa=0,bb=0;
            for(int n=0;n<80;++n){const double a=voiceWindow[static_cast<size_t>((voiceWrite+159-n)%160)],b=voiceWindow[static_cast<size_t>((voiceWrite+159-n-lag)%160)];cross+=a*b;aa+=a*a;bb+=b*b;}
            if(aa>1e-14&&bb>1e-14)best=std::max(best,cross/std::sqrt(aa*bb));
        }return static_cast<float>(std::clamp(best,0.,1.));
    }
    void resetFrame() noexcept {frameCount=zeroCrossings=0;square=loSquare=hiSquare=sideSquare=0;frameIn=frameOut=0;}
    SpscRing<TelemetryFrame,256> ring;
    std::atomic<uint64_t> desiredLearn{0};
    double fs=48000,current=1,target=1,step=0,lowCoefficient=0,highCoefficient=0,previous=0;
    double square=0,loSquare=0,hiSquare=0,sideSquare=0;
    std::array<double,2>low{},high{};
    std::array<float,160>voiceWindow{};int voiceWrite=0,voiceCount=0;double voicePhase=0,voiceFilter=0,voiceCoefficient=0;
    int frameSize=960,rampSize=1440,left=0,frameCount=0,zeroCrossings=0;
    int64_t expectedPosition=0,framePosition=0;bool hadPosition=false;
    uint64_t learn=0,sequence=0,segment=0,dropped=0;uint32_t frameFlags=0;
    float frameIn=0,frameOut=0;
};

struct LearnedTrack {
    // Bounded 0.1 dB histogram preserves a robust active-frame median over
    // 300 seconds without allocating 60 KB per track or sorting on each read.
    std::array<uint16_t,1441> levelHistogram{};
    int count=0,active=0,voiced=0,starts=0;bool lastActive=false,overflow=false,aligned=true;
    double low=0,high=0,side=0,energy=0;uint64_t dropped=0,segment=0;
    int64_t begin=std::numeric_limits<int64_t>::max(),end=std::numeric_limits<int64_t>::min();uint32_t rate=0;
    void add(const TelemetryFrame& f) noexcept {
        if(count>=maxLearnFrames){overflow=true;return;}
        if(f.energy<0||!std::isfinite(f.energy)||!std::isfinite(f.levelDb)||!std::isfinite(f.voiced)
           ||!std::isfinite(f.lowEnergy)||!std::isfinite(f.highEnergy)||!std::isfinite(f.sideEnergy)
           ||f.rate<8000||f.rate>384000||f.samples<1||f.samples>8000||f.position<-(int64_t(1)<<60)||f.position>(int64_t(1)<<60)){aligned=false;return;}
        if(count&&((rate&&rate!=f.rate)||segment!=f.segment||f.dropped!=dropped))aligned=false;
        if((f.flags&3u)!=3u||(f.flags&4u)!=0)aligned=false;
        rate=f.rate;segment=f.segment;dropped=f.dropped;begin=std::min(begin,f.position);end=std::max(end,f.position+f.samples);
        const bool isActive=f.levelDb>-55;lastActive=isActive?(++active,voiced+=f.voiced>.5f,starts+=!lastActive,true):false;
        if(isActive)++levelHistogram[static_cast<size_t>(std::clamp(int(std::lround((std::clamp(f.levelDb,-120.f,24.f)+120.f)*10)),0,1440))];
        ++count;low+=f.lowEnergy;high+=f.highEnergy;side+=f.sideEnergy;energy+=f.energy;
    }
    float activeLevel() const noexcept {
        if(!active)return -120;int accumulated=0;
        for(int bin=0;bin<int(levelHistogram.size());++bin){accumulated+=levelHistogram[static_cast<size_t>(bin)];if(accumulated>active/2)return bin*.1f-120.f;}
        return -120;
    }
    double seconds()const noexcept{return count&&rate?static_cast<double>(end-begin)/rate:0;}
};
struct RoleHint {Role role=Role::automatic;float confidence=0;bool high=false;};
inline RoleHint audioHint(const LearnedTrack& t) noexcept {
    if(t.active<25||t.count<100)return{};
    const float voice=static_cast<float>(t.voiced)/t.active,duty=static_cast<float>(t.active)/t.count;
    if(voice>.65f&&duty>.55f)return{Role::main,.58f,false};
    if(voice>.45f&&duty<.4f&&t.starts>=2)return{Role::adlib,.55f,false};
    if(voice<.2f&&t.side>t.energy*.15&&duty>.65f)return{Role::beat,.66f,false};
    return{}; // Audio-only role hints always need manual confirmation in V1.
}
inline float correction(float current,float measured,float target,float limit) noexcept {
    if(!std::isfinite(current)||!std::isfinite(measured)||!std::isfinite(target)||!std::isfinite(limit))return current;
    const float delta=std::clamp(target-measured,-std::clamp(limit,0.f,6.f),std::clamp(limit,0.f,6.f));
    return std::round(std::clamp(current+delta,-24.f,6.f)*10.f)*.1f;
}
}
