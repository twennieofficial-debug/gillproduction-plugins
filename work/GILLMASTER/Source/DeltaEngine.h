#pragma once
#include "DeltaMapping.h"
#include "MasterDSP.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <complex>
#include <chrono>

namespace gill::master {
// Same-host-process, same-signal-chain SOURCE/RETURN link. Shared audio uses
// stamped atomic integer cells: no floats, pointers or C++ atomic objects are
// placed in mapped storage. No mutex, allocation or FFT occurs on audio threads.
class DeltaEngine {
public:
    static constexpr std::size_t capacity=262144, mask=capacity-1;
    struct alignas(8) Cell { std::uint64_t stamp,pcm; };
    struct alignas(8) Pair { std::uint64_t owner,end,rate,heartbeat;Cell samples[capacity]; };
    struct Bus { Pair pairs[8]; };
    struct Alignment { bool valid=false;int delay=0;double gain=1,confidence=0; };
    DeltaEngine();
    ~DeltaEngine();
    void prepare(double);
    void configure(bool source,int pair) noexcept;
    void learn() noexcept { learnRequest.fetch_add(1); }
    void process(float*const*,int,int,int audition,bool match,bool offline,bool bypass) noexcept;
    Alignment alignment() const noexcept;
    bool linked() const noexcept { return connected.load(); }
    bool conflict() const noexcept { return sourceConflict.load(); }
    bool learning() const noexcept { return learningView.load(); }
    // Independent pure estimator also used by native tests.
    static Alignment estimate(const std::vector<double>& pre,const std::vector<double>& post,int maximumDelay);
private:
    static std::uint64_t pack(float,float) noexcept;
    static Stereo unpack(std::uint64_t) noexcept;
    static bool read(const Cell*,std::uint64_t,Stereo&) noexcept;
    static void write(Cell*,std::uint64_t,Stereo) noexcept;
    void workerLoop();
    void releaseSource() noexcept;
    delta09::NativeMapping mapping;
    Bus*bus=nullptr;std::unique_ptr<Cell[]>post;
    std::thread worker;std::atomic<bool>exit{false};
    std::atomic<bool>sourceMode{false},connected{false},sourceConflict{false},learningView{false};
    std::atomic<int>pairChoice{0};std::atomic<double>sampleRate{48000};
    std::atomic<unsigned>learnRequest{0},resultSequence{0};
    std::atomic<std::uint64_t>postEnd{0},postOwner{0},postRevision{0},resultOwner{0},resultRevision{0};
    std::atomic<int>resultPair{-1},resultDelay{0};std::atomic<double>resultGain{1},resultConfidence{0};
    std::uint64_t token=0,clock=0,lastSourceEnd=0,revision=0,observedOwner=0;
    int claimedPair=-1,lastPair=-1;bool lastSource=false;double fs=48000;
    Smooth matchGain;Stereo previous{},anchor{};int auditionMode=0,fadeRemaining=0,fadeLength=240;
};
}
