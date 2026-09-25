#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <ctime>
#endif
#include "../Source/TuneDSP.h"
#define gill gill_v020
#include "TuneDSP-v020-reference.h"
#undef gill
#include <chrono>
#include <iostream>
#include <iomanip>

static double cpuSeconds() {
#if defined(_WIN32)
    FILETIME created,exited,kernel,user;
    GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)*1e-7;
#else
    return double(std::clock()) / double(CLOCKS_PER_SEC);
#endif
}
static std::vector<float> vowel(int length) {
    std::vector<float> a(length);
    for(int n=0;n<length;++n){double x=0;for(int h=1;h<18;++h){const double f=267.5*h;
        const double env=.18+2*std::exp(-std::pow((f-700)/220,2))+1.4*std::exp(-std::pow((f-1200)/300,2));
        x+=.16*std::sin(6.283185307179586*f*n/48000)*env/h;}a[n]=static_cast<float>(x);}
    return a;
}
int main(){
    std::cout<<std::fixed<<std::setprecision(6);
    constexpr int length=48000*3,block=128;
    const auto source=vowel(length);
    for(int repeat=0;repeat<3;++repeat)for(int mode=0;mode<3;++mode){
        auto l=source,r=source;
        gill::TuneDSP tune;tune.setQualityMode(mode==2?1:0);tune.prepare(48000,block,2);tune.setParameters(0,0,0,0,100);
        gill_v020::TuneDSP old;old.prepare(48000,block,2);old.setParameters(0,0,0,0,100);
        const double cpu0=cpuSeconds();const auto begin=std::chrono::steady_clock::now();
        for(int n=0;n<length;n+=block){float* ip[]{l.data()+n,r.data()+n};
            if(mode==0)old.process(ip,2,std::min(block,length-n));
            else tune.process(ip,2,std::min(block,length-n));}
        const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count(),cpu=cpuSeconds()-cpu0;
        std::cout<<"repeat="<<repeat<<" mode="<<(mode==0?"OLD_020":mode==1?"STUDIO_030":"LIVE_030")
            <<" latency="<<(mode==0?old.latencySamples():tune.latencySamples())
            <<" audio_seconds=3 wall_seconds="<<wall<<" process_cpu_seconds="<<cpu<<" process_cpu_realtime_fraction="<<cpu/3<<'\n';
    }
}
