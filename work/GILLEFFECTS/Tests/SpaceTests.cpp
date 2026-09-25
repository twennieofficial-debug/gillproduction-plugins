#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <ctime>
#endif
#include "../Source/SpaceDSP.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>

static bool watch=false;static size_t allocations=0;static int failures=0;
void* operator new(std::size_t n){if(watch)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static constexpr double pi=3.14159265358979323846;
static void check(bool good,const std::string& text){std::cout<<(good?"PASS ":"FAIL ")<<text<<'\n';if(!good)++failures;}
static void process(gill::SpaceDSP& dsp,std::vector<float>& l,int block,std::vector<float>* r=nullptr){
    for(int n=0;n<static_cast<int>(l.size());n+=block){float* p[]{l.data()+n,r?r->data()+n:nullptr};watch=true;
        dsp.process(p,r?2:1,std::min(block,static_cast<int>(l.size())-n));watch=false;}}
static std::vector<float> signal(double fs,double seconds,int kind){
    std::vector<float> a(static_cast<size_t>(fs*seconds));std::mt19937 rng(741);std::uniform_real_distribution<float> uniform(-.15f,.15f);
    for(size_t n=0;n<a.size();++n)a[n]=kind==0?uniform(rng):kind==1?static_cast<float>(.12*std::sin(2*pi*440*n/fs)):.15f;
    return a;
}
static double peak(const std::vector<float>& a){double p=0;for(float x:a)p=std::max(p,std::abs(static_cast<double>(x)));return p;}
static double rms(const std::vector<float>& a,int first,int last){double e=0;for(int n=first;n<last;++n)e+=static_cast<double>(a[n])*a[n];return std::sqrt(e/std::max(1,last-first));}
static double correlation(const std::vector<float>& a,const std::vector<float>& b,int first){double ab=0,aa=0,bb=0;
    for(size_t n=first;n<a.size();++n){ab+=static_cast<double>(a[n])*b[n];aa+=static_cast<double>(a[n])*a[n];bb+=static_cast<double>(b[n])*b[n];}return ab/std::sqrt(std::max(1e-40,aa*bb));}
static double estimateT60(std::vector<float> l,std::vector<float> r,double fs){
    // Independent Schroeder integrated-energy regression after a 400 Hz low-pass.
    const double pole=std::exp(-2*pi*400/fs);double sL=0,sR=0;
    for(size_t n=0;n<l.size();++n){sL=(1-pole)*l[n]+pole*sL;sR=(1-pole)*r[n]+pole*sR;l[n]=static_cast<float>(sL);r[n]=static_cast<float>(sR);}
    std::vector<double> energy(l.size());double sum=0;
    for(size_t n=l.size();n-->0;){sum+=static_cast<double>(l[n])*l[n]+static_cast<double>(r[n])*r[n];energy[n]=sum;}
    if(sum<1e-20)return 0;
    double sx=0,sy=0,sxx=0,sxy=0,count=0;
    for(size_t n=0;n<energy.size();n+=16){const double db=10*std::log10(std::max(1e-40,energy[n]/sum));
        if(db<-5&&db>-35){const double t=n/fs;sx+=t;sy+=db;sxx+=t*t;sxy+=t*db;++count;}}
    if(count<10)return 0;const double slope=(count*sxy-sx*sy)/(count*sxx-sx*sx);return -60/slope;
}
static double cpuSeconds(){
#if defined(_WIN32)
    FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);ULARGE_INTEGER ki{},ui{};
    ki.LowPart=k.dwLowDateTime;ki.HighPart=k.dwHighDateTime;ui.LowPart=u.dwLowDateTime;ui.HighPart=u.dwHighDateTime;return(ki.QuadPart+ui.QuadPart)*1e-7;
#else
    return double(std::clock()) / double(CLOCKS_PER_SEC);
#endif
}

int main(){std::cout<<std::fixed<<std::setprecision(6);const auto begin=std::chrono::steady_clock::now();
    for(int style=0;style<3;++style)for(float decay:{.2f,.35f,1.2f,3.5f,8.0f,15.0f}){
        constexpr double fs=48000;gill::SpaceDSP dsp;dsp.prepare(fs,128,2);dsp.setParameters(100,decay,0,100,60,100,style);
        const int length=static_cast<int>(fs*(decay*2+.8));std::vector<float> l(length,0),r(length,0);l[0]=r[0]=.5f;
        process(dsp,l,128,&r);const double t60=estimateT60(l,r,fs),corr=correlation(l,r,static_cast<int>(fs*.08));
        std::cout<<"DECAY style="<<style<<" requested="<<decay<<" measured_LF_RT60="<<t60<<" ratio="<<t60/decay
            <<" correlation="<<corr<<" peak="<<std::max(peak(l),peak(r))<<" reported_tail="<<dsp.tailSeconds()<<'\n';
        check(t60>decay*.65&&t60<decay*1.4,"nominal LF decay matches requested RT60");
        check(std::abs(corr)<.85,"late stereo response decorrelated from mono excitation");
        check(std::max(peak(l),peak(r))<.75,"impulse response gain bounded");
        check(rms(l,length-static_cast<int>(fs*.1),length)<.00001,"late impulse energy reaches inaudible floor");
    }
    for(double fs:{8000.0,44100.0,48000.0,96000.0,192000.0,384000.0}){
        for(int channels:{1,2}){gill::SpaceDSP dsp;dsp.prepare(fs,31,channels);dsp.setParameters(0,2,30,55,65,100,1);
            auto l=signal(fs,.35,0),r=signal(fs,.35,1);const auto originalL=l,originalR=r;
            process(dsp,l,31,channels==2?&r:nullptr);check(l==originalL&&(channels==1||r==originalR),"initial zero MIX exact dry at actual rate/layout");
            check(dsp.latencySamples()==0,"dry path reports zero latency");dsp.reset();dsp.setParameters(100,1.2f,80,60,50,100,1);
            l.assign(static_cast<size_t>(fs*.6),0);r=l;l[0]=.5f;if(channels==2)r[0]=.5f;process(dsp,l,127,channels==2?&r:nullptr);
            const int first=static_cast<int>(std::find_if(l.begin(),l.end(),[](float x){return std::abs(x)>1e-8f;})-l.begin());
            check(first>=static_cast<int>(fs*.08)&&first<static_cast<int>(fs*.14),"predelay preserves requested minimum delay");
            check(peak(l)>.00001&&peak(l)<1&&std::all_of(l.begin(),l.end(),[](float x){return std::isfinite(x);}),"wet output finite and nonzero across rates/layouts");
            dsp.reset();std::fill(l.begin(),l.end(),0.0f);if(channels==2)std::fill(r.begin(),r.end(),0.0f);process(dsp,l,4096,channels==2?&r:nullptr);
            check(peak(l)==0&&(channels==1||peak(r)==0),"reset clears tank and exact silence remains silence");
        }
    }
    {gill::SpaceDSP dsp;dsp.prepare(48000,64,2);dsp.setParameters(100,2.5f,0,60,70,0,1);
        std::vector<float> l(96000,0),r(96000,0);l[0]=.5f;process(dsp,l,64,&r);check(l==r,"width zero produces exact mono wet output");}
    {gill::SpaceDSP a,b;a.prepare(48000,1,2);b.prepare(48000,4096,2);a.setParameters(100,2,17,65,73,90,2);b.setParameters(100,2,17,65,73,90,2);
        auto al=signal(48000,.75,0),ar=signal(48000,.75,1),bl=al,br=ar;process(a,al,1,&ar);process(b,bl,4096,&br);
        check(al==bl&&ar==br,"wet stereo processing independent of host block boundaries");}
    {double energies[2]{};for(int bright=0;bright<2;++bright){gill::SpaceDSP dsp;dsp.prepare(48000,128,1);dsp.setParameters(100,3,0,bright?100.0f:0.0f,60,100,1);
        std::vector<float> a(96000,0);for(int n=0;n<9600;++n)a[n]=static_cast<float>(.15*std::sin(2*pi*4000*n/48000));process(dsp,a,128);
        energies[bright]=rms(a,24000,48000);}
        std::cout<<"TONE dark_4k_tail_rms="<<energies[0]<<" bright_4k_tail_rms="<<energies[1]<<'\n';
        check(energies[1]>energies[0]*2,"tone controls high-frequency decay rather than merely wet level");}
    for(int style=0;style<3;++style)for(int kind:{0,1,2}){
        gill::SpaceDSP dsp;dsp.prepare(48000,128,2);dsp.setParameters(100,15,0,100,100,100,style);
        auto l=signal(48000,3,kind),r=l;process(dsp,l,128,&r);
        std::cout<<"STABILITY style="<<style<<" source="<<kind<<" peak="<<std::max(peak(l),peak(r))<<" late_rms="<<rms(l,96000,144000)<<'\n';
        check(peak(l)<1.2&&peak(r)<1.2&&std::isfinite(rms(l,0,144000)),"maximum-decay sustained input remains bounded");
    }
    {gill::SpaceDSP dsp;dsp.prepare(48000,64,1);dsp.setParameters(100,3,0,75,70,100,1);
        std::vector<float> mono(48000*7,0);mono[0]=.5f;process(dsp,mono,64);
        check(rms(mono,48000,96000)>.000001&&rms(mono,48000*6,48000*7)<.000001,"mono output retains then decays its tail");
        const double before=dsp.tailSeconds();dsp.setParameters(100,.2f,0,50,0,100,0);
        check(dsp.tailSeconds()>=before,"shortening preset cannot underreport an existing tail");dsp.reset();
        check(std::abs(dsp.tailSeconds()-1.1)<.000001,"reset clears conservative old-decay tail hold");}
    {gill::SpaceDSP dsp;dsp.prepare(48000,64,1);dsp.setParameters(100,.2f,0,50,0,50,0);
        std::vector<float> tiny(48000,1e-30f);process(dsp,tiny,1);check(peak(tiny)==0,"denormal-scale input does not propagate through wet tank");}

    for(double fs:{44100.0,48000.0,96000.0}){
        const int block=128;auto source=signal(fs,1.0,1),wet=source;gill::SpaceDSP reference;reference.prepare(fs,block,1);
        reference.setParameters(100,2,20,60,65,85,1);process(reference,wet,block);
        int change=static_cast<int>(fs*.55)/block*block;
        for(int n=change;n<static_cast<int>(fs*.7);n+=block)if(std::abs(wet[n]-source[n])>std::abs(wet[change]-source[change]))change=n;
        for(bool rising:{false,true}){gill::SpaceDSP dsp;dsp.prepare(fs,block,1);dsp.setParameters(rising?0.0f:100.0f,2,20,60,65,85,1);
            auto actual=source;for(int n=0;n<static_cast<int>(actual.size());n+=block){dsp.setParameters(((n>=change)==rising)?100.0f:0.0f,2,20,60,65,85,1);
                float* p[]{actual.data()+n};watch=true;dsp.process(p,1,std::min(block,static_cast<int>(actual.size())-n));watch=false;}
            const int ramp=static_cast<int>(std::round(fs*.005));double error=0;bool exact=true;
            for(int n=0;n<static_cast<int>(actual.size());++n){const double f=std::clamp((n-change+1)/static_cast<double>(ramp),0.0,1.0),mix=rising?f:1-f;
                error=std::max(error,std::abs(actual[n]-(source[n]+mix*(wet[n]-source[n]))));if(n>=change+ramp)exact&=actual[n]==(rising?wet[n]:source[n]);}
            check(error<.000002&&exact,"MIX automation follows actual wet/dry 5 ms ramp and exact endpoints");
        }
    }
    {const int length=96000,block=64,change=48000;auto source=signal(48000,2,1),reference=source,actual=source;
        gill::SpaceDSP a,b;a.prepare(48000,block,1);b.prepare(48000,block,1);a.setParameters(100,5,0,80,20,100,0);b.setParameters(100,5,0,80,20,100,0);
        process(a,reference,block);
        for(int n=0;n<length;n+=block){if(n==change)b.setParameters(100,12,200,15,100,35,2);
            float* p[]{actual.data()+n};watch=true;b.process(p,1,std::min(block,length-n));watch=false;}
        double jump=0;for(int n=change+1;n<change+4800;++n)jump=std::max(jump,std::abs(static_cast<double>(actual[n]-actual[n-1])));
        std::cout<<"AUTOMATION first_change="<<std::abs(actual[change]-reference[change])<<" maximum_adjacent_step="<<jump<<'\n';
        check(std::abs(actual[change]-reference[change])<.005&&jump<.08,"large preset change crossfades delay heads without a step or tank reset");
        check(rms(actual,change,change+2400)>.00001,"existing tail survives style and predelay changes");}
    {gill::SpaceDSP dsp;dsp.prepare(48000,64,1);dsp.setParameters(std::numeric_limits<float>::quiet_NaN(),999,-999,999,-999,999,99);
        std::vector<float> a(10000,0);a[0]=std::numeric_limits<float>::infinity();a[3]=std::numeric_limits<float>::quiet_NaN();process(dsp,a,512);
        check(std::all_of(a.begin(),a.end(),[](float x){return std::isfinite(x);}),"invalid input and parameters remain finite");
        dsp.prepare(4000,64,1);auto original=signal(4000,.2,1);a=original;process(dsp,a,64);
        check(a==original&&dsp.tailSeconds()==0,"unsupported actual sample rate transparently passes through");}
    {gill::SpaceDSP dsp;dsp.prepare(48000,128,2);dsp.setParameters(35,8,40,60,90,100,1);auto l=signal(48000,5,0),r=l;
        const auto t=std::chrono::steady_clock::now();const double cpu=cpuSeconds();process(dsp,l,128,&r);
        std::cout<<"CPU stereo_48k_audio_seconds=5 process_cpu_seconds="<<cpuSeconds()-cpu<<" wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count()<<'\n';}
    check(allocations==0,"no allocations during process (count="+std::to_string(allocations)+")");
    std::cout<<"RESULT failures="<<failures<<" elapsed_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<'\n';return failures?1:0;
}
