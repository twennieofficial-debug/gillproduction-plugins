#pragma once
#include "MasterDSP.h"
#include "Foundation/Loudness.h"
#include "Foundation/OutputPeakMeter.h"
#include <atomic>
#include <cstdint>

namespace gill::master {
class DeliverMeter {
public:
    struct Snapshot {
        double integrated=-100,momentary=-100,peakDb=-160,duration=0,leading=0,trailing=0;
        std::uint64_t clipped=0;bool running=false,hasAudio=false,truePeak=false;double range=0,shortTerm=-100;
    };
    void prepare(double rate,int count) {
        fs=std::clamp(math::finite(rate,48000),8000.,192000.);channels=std::clamp(count,1,2);
        loudness.prepare(fs,channels);peak.prepare(fs,channels);reset(false);command=0;
    }
    // GUI commands are consumed exclusively by the audio thread.
    void start() noexcept { command.store(1,std::memory_order_release); }
    void stop() noexcept { command.store(2,std::memory_order_release); }
    void clear() noexcept { command.store(3,std::memory_order_release); }
    void beginBlock(bool playing,bool hasTransport,bool useTruePeak) noexcept {
        // One report uses one peak-measurement method. A quality change starts
        // a new capture instead of labelling old LIVE samples as true-peak data.
        if(useTruePeak!=pro){pro=useTruePeak;const bool wasActive=active;reset(wasActive);waiting=wasActive&&hasTransport&&!playing;}
        const int request=command.exchange(0,std::memory_order_acq_rel);
        if(request==1){reset(true);waiting=hasTransport&&!playing;}
        if(request==2)active=false;
        if(request==3)reset(false);
        // An armed transport capture waits for playback and stops after its end.
        if(active&&hasTransport){if(playing){waiting=false;wasPlaying=true;}else if(wasPlaying){active=false;}}
    }
    void sample(Stereo x,double silenceDb) noexcept {
        if(!active||waiting)return;
        const float left=float(math::input(x[0])),right=float(math::input(x[1]));
        loudness.sample({left,right},channels);
        const double maximum=std::max(std::abs(double(left)),channels==2?std::abs(double(right)):0.);
        if(maximum>=1)++clipped;
        if(maximum>math::gain(silenceDb)){if(first<0)first=std::int64_t(frames);last=std::int64_t(frames);}
        peakMaximum=std::max(peakMaximum,maximum);
        if(pro){peak.process(left,right,channels);peakMaximum=std::max(peakMaximum,double(peak.maximumPeak()));}
        ++frames;
    }
    void publish() noexcept {
        // Each scalar is independently atomic. A sequence guards coherent GUI
        // reports without ever blocking the real-time writer.
        sequence.fetch_add(1,std::memory_order_acq_rel);
        integrated=loudness.integrated;momentary=loudness.momentary;peakDb=math::db(peakMaximum);
        duration=frames/fs;leading=first<0?frames/fs:first/fs;
        trailing=last<0?frames/fs:(frames-1-std::uint64_t(last))/fs;
        clipView=clipped;running=active;hasAudio=first>=0;truePeak=pro;
        range=loudness.loudnessRange;shortTerm=loudness.shortTerm;
        sequence.fetch_add(1,std::memory_order_release);
    }
    Snapshot snapshot() const noexcept {
        Snapshot s;
        for(int tries=0;tries<4;++tries){const auto before=sequence.load(std::memory_order_acquire);if(before&1)continue;
            s={integrated.load(),momentary.load(),peakDb.load(),duration.load(),leading.load(),trailing.load(),clipView.load(),running.load(),hasAudio.load(),truePeak.load(),range.load(),shortTerm.load()};
            if(before==sequence.load(std::memory_order_acquire))break;
        }return s;
    }
private:
    void reset(bool run) noexcept { loudness.reset();peak.reset();frames=clipped=0;first=last=-1;peakMaximum=0;active=run;wasPlaying=waiting=false;publish(); }
    gillnext::Loudness loudness;gillnext::OutputPeakMeter peak;
    double fs=48000,peakMaximum=0;int channels=2;
    std::uint64_t frames=0,clipped=0;std::int64_t first=-1,last=-1;bool active=false,waiting=false,wasPlaying=false,pro=true;
    std::atomic<int>command{0};std::atomic<unsigned>sequence{0};
    std::atomic<double>integrated{-100},momentary{-100},peakDb{-160},duration{0},leading{0},trailing{0};
    std::atomic<double>range{0},shortTerm{-100};
    std::atomic<std::uint64_t>clipView{0};std::atomic<bool>running{false},hasAudio{false},truePeak{true};
};
}
