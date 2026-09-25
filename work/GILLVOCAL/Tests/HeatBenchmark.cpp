// Paired benchmark: compare archived 0.2 DSP and current DSP on identical input.
// Alternate order per repetition to reduce background CPU-load bias.
#define gill gill_heat_v02
#include "HeatFixtures/HeatDSP-v02-reference.h"
#undef gill
#include "../Source/HeatDSP.h"
#include <chrono>
#include <cstdio>
#include <memory>
namespace {
volatile double checksum=0;
template<class Engine> double render(int mode,bool extreme){
    constexpr int blockSize=128,blocks=3750;
    std::array<float,blockSize> left{},right{},source{};
    float* ptr[]{left.data(),right.data()};
    for(int n=0;n<blockSize;++n)
        source[n]=static_cast<float>(.2*std::sin(n*.051)+.13*std::sin(n*.093)+.08*std::cos(n*.17));
    auto dsp=std::make_unique<Engine>();
    dsp->setParameters(extreme?24.f:8.f,extreme?24.f:12.f,extreme?24.f:7.f,mode,100,0);
    dsp->prepare(48000,128,2);
    const auto start=std::chrono::steady_clock::now();
    for(int b=0;b<blocks;++b){
        left=right=source;dsp->process(ptr,2,blockSize);checksum+=left[b%blockSize];
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
}
}
int main(){
    static_assert(gill::HeatDSP::oversamplingFactor==32,"Keep antialias quality");
    static_assert(gill::HeatDSP::fixedLatencySamples==24,"Keep latency");
    std::puts("GILLHEAT paired stereo 48 kHz / 128 samples; median of 3 x 10 seconds");
    for(int extreme=0;extreme<2;++extreme)for(int mode=0;mode<3;++mode){
        std::array<double,3> oldTimes{},newTimes{},ratios{};
        for(int repetition=0;repetition<3;++repetition){
            if(repetition%2){
                newTimes[repetition]=render<gill::HeatDSP>(mode,extreme!=0);
                oldTimes[repetition]=render<gill_heat_v02::HeatDSP>(mode,extreme!=0);
            }else{
                oldTimes[repetition]=render<gill_heat_v02::HeatDSP>(mode,extreme!=0);
                newTimes[repetition]=render<gill::HeatDSP>(mode,extreme!=0);
            }
            ratios[repetition]=newTimes[repetition]/oldTimes[repetition];
        }
        std::sort(oldTimes.begin(),oldTimes.end());
        std::sort(newTimes.begin(),newTimes.end());
        std::sort(ratios.begin(),ratios.end());
        std::printf("BENCH mode %d %s old %.4f sec / new %.4f sec; new %.2f percent realtime; paired median ratio %.4f\n",
                    mode,extreme?"MAXIMUM":"MODERATE",oldTimes[1],newTimes[1],newTimes[1]*10,ratios[1]);
        std::fflush(stdout);
    }
    return std::isfinite(checksum)?0:1;
}
