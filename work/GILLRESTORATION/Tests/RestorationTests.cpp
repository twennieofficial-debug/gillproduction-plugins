#include "../Source/RestorationDSP.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <limits>

static bool guardAllocations=false;
static std::uint64_t audioAllocations=0;
void* operator new(std::size_t n){if(guardAllocations)++audioAllocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

namespace {
using gillrestoration::Mode;using gillrestoration::RestorationEngine;
constexpr double pi=3.14159265358979323846;
std::uint64_t checks=0,failures=0,channelSamples=0;
void require(bool yes,const std::string& label){++checks;if(!yes){++failures;if(failures<30)std::cerr<<"FAIL: "<<label<<'\n';}}
std::uint32_t rng=0x82fac173;
std::uint32_t randomBits(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
double random01(){return randomBits()/4294967295.0;}
double energy(const std::vector<double>& a){double sum=0;for(auto x:a)sum+=x*x;return sum;}
double snr(const std::vector<double>& out,const std::vector<double>& ref){double e=0;for(size_t i=0;i<ref.size();++i)e+=(out[i]-ref[i])*(out[i]-ref[i]);return 10*std::log10(std::max(1.e-30,energy(ref))/std::max(1.e-30,e));}
std::vector<double> readWav(const std::filesystem::path& file){
    std::ifstream f(file,std::ios::binary);char tag[4];std::uint32_t size=0;f.read(tag,4);f.read(reinterpret_cast<char*>(&size),4);f.read(tag,4);
    std::uint16_t format=0,channels=0,bits=0;std::uint32_t fs=0;std::vector<char> bytes;
    while(f.read(tag,4)){f.read(reinterpret_cast<char*>(&size),4);if(!f)break;
        if(std::memcmp(tag,"fmt ",4)==0){std::vector<char>b(size);f.read(b.data(),size);std::memcpy(&format,b.data(),2);std::memcpy(&channels,b.data()+2,2);std::memcpy(&fs,b.data()+4,4);std::memcpy(&bits,b.data()+14,2);}
        else if(std::memcmp(tag,"data",4)==0){bytes.resize(size);f.read(bytes.data(),size);}else f.seekg(size,std::ios::cur);
        if(size&1)f.seekg(1,std::ios::cur);
    }
    if(channels!=1 || fs!=48000 || format!=3 || bits!=32)throw std::runtime_error("Expected mono float32 48k WAV: "+file.string());
    std::vector<double> out(bytes.size()/4);for(size_t i=0;i<out.size();++i){float x;std::memcpy(&x,bytes.data()+4*i,4);out[i]=x;}return out;
}
void writeWav(const std::filesystem::path& file,const std::vector<double>& data){
    std::ofstream f(file,std::ios::binary);auto u16=[&](std::uint16_t n){f.write(reinterpret_cast<const char*>(&n),2);};auto u32=[&](std::uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);};
    f.write("RIFF",4);u32(static_cast<std::uint32_t>(36+4*data.size()));f.write("WAVEfmt ",8);u32(16);u16(3);u16(1);u32(48000);u32(192000);u16(4);u16(32);f.write("data",4);u32(static_cast<std::uint32_t>(4*data.size()));
    for(double x:data){float y=static_cast<float>(x);f.write(reinterpret_cast<const char*>(&y),4);}
}
std::vector<double> process(const std::vector<double>& input,Mode mode,double amount,int block=127,double fs=48000){
    RestorationEngine e;e.prepare(fs,mode);e.setAmount(amount);const int delay=e.getLatencySamples();
    std::vector<double> work(input);work.resize(input.size()+static_cast<size_t>(delay),0);size_t pos=0;
    while(pos<work.size()){int n=std::min(block,static_cast<int>(work.size()-pos));double* data[]={work.data()+pos};guardAllocations=true;e.process(data,1,n);guardAllocations=false;pos+=static_cast<size_t>(n);channelSamples+=n;}
    return {work.begin()+delay,work.end()};
}
struct Quality{std::string name;double cleanSnr=0,cleanGain=0,cleanChanged=0,before=0,after=0,improvement=0,mouthBefore=0,mouthAfter=0,drumSnr=0;};
std::vector<Quality> qualities;
struct RateQuality{std::string mode;double rate,amount,cleanSnr,improvement;};
std::vector<RateQuality> rateQualities;
std::vector<double> resample(const std::vector<double>& source,double rate){
    if(rate==48000)return source;
    constexpr int phases=512,half=32,taps=64;
    std::vector<double> kernels(phases*taps);const double cutoff=std::min(1.0,rate/48000.0)*.95;
    for(int phase=0;phase<phases;++phase){const double frac=static_cast<double>(phase)/phases;double sum=0;
        for(int j=0;j<taps;++j){const double x=j-(half-1)-frac;const double v=std::abs(x)<half?(std::abs(x)<1.e-10?cutoff:std::sin(pi*x*cutoff)/(pi*x))*.5*(1+std::cos(pi*x/half)):0;
            kernels[phase*taps+j]=v;sum+=v;}
        for(int j=0;j<taps;++j)kernels[phase*taps+j]/=sum;}
    std::vector<double> out(static_cast<size_t>(std::lround(source.size()*rate/48000)));
    for(size_t i=0;i<out.size();++i){const double at=i*48000.0/rate;const int centre=static_cast<int>(std::floor(at));const int phase=std::min(phases-1,static_cast<int>((at-centre)*phases));double v=0;
        for(int j=0;j<taps;++j){const int k=centre+j-(half-1);if(k>=0&&k<static_cast<int>(source.size()))v+=source[static_cast<size_t>(k)]*kernels[phase*taps+j];}out[i]=v;}
    return out;
}
std::vector<double> corruption(const std::vector<double>& dry,bool dense){
    auto wet=dry;std::uint32_t seed=dense?0x24b86e51u:0x8c5137adu;
    auto next=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    const int count=dense?500:26;
    for(int k=0;k<count;++k){const size_t at=24000+next()%225000;const int n=dense?1+next()%4:1+next()%22;const double level=(dense?.014:.12)+(next()/4294967295.0)*(dense?.10:.30);const double sign=(next()&1)?1:-1;
        for(int j=0;j<n && at+j<wet.size();++j)wet[at+j]+=sign*level;}
    return wet;
}
std::vector<double> mouthBursts(const std::vector<double>& dry){
    auto wet=dry;std::uint32_t seed=0x334f85c1u;
    auto next=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    // Plausible short impulsive mouth-click trains, not real mouth recordings.
    // Irregular bipolar packets occupy about 0.3-1.5 ms, grouped in short bursts.
    for(int event=0;event<38;++event){size_t at=28000+next()%220000;const int packets=2+next()%5;
        for(int p=0;p<packets;++p){const int width=2+next()%6;const double level=.02+.11*(next()/4294967295.0);const double sign=(next()&1)?1:-1;
            for(int j=0;j<width;++j){wet[at+j]+=sign*level*(1.0-.5*j/width);wet[at+width+j]-=sign*level*.45*(1.0-static_cast<double>(j)/width);}
            at+=width*2+2+next()%14;}}
    return wet;
}
std::vector<double> drums(){
    std::vector<double> x(96000);std::uint32_t seed=0x713bf12u;
    for(size_t i=0;i<x.size();++i){seed=1664525u*seed+1013904223u;const double noise=seed/4294967295.0*2-1;const double t=i/48000.0;double v=0;
        for(int k=0;k<5;++k){const double a=t-.15-.35*k;if(a>=0){const double attack=1-std::exp(-a/.0012);v+=attack*std::exp(-a/.08)*(.18*noise+.25*std::sin(2*pi*(60*a+32*.015*(1-std::exp(-a/.015)))));}}
        x[i]=v;}
    return x;
}
void qualityTests(const std::filesystem::path& fixtures){
    const auto dry=readWav(fixtures/"dry_48k.wav");const auto drum=drums();const auto mouth=mouthBursts(dry);
    for(auto mode:{Mode::Declick,Mode::Decrackle}){Quality q;q.name=mode==Mode::Declick?"DECLICK":"DECRACKLE";
        const auto clean=process(dry,mode,.55);q.cleanSnr=snr(clean,dry);q.cleanGain=10*std::log10(energy(clean)/energy(dry));
        size_t changed=0;for(size_t i=0;i<dry.size();++i)if(std::abs(clean[i]-dry[i])>1.e-9)++changed;q.cleanChanged=100.0*changed/dry.size();
        const auto wet=corruption(dry,mode==Mode::Decrackle),out=process(wet,mode,.55);q.before=snr(wet,dry);q.after=snr(out,dry);q.improvement=q.after-q.before;
        const auto mouthOut=process(mouth,mode,.55);q.mouthBefore=snr(mouth,dry);q.mouthAfter=snr(mouthOut,dry);q.drumSnr=snr(process(drum,mode,.55),drum);
        std::cerr<<q.name<<" clean="<<q.cleanSnr<<" changed="<<q.cleanChanged<<" gain="<<q.cleanGain<<" corrupted="<<q.before<<" -> "<<q.after<<" improvement="<<q.improvement<<" mouth="<<q.mouthBefore<<" -> "<<q.mouthAfter<<" drum="<<q.drumSnr<<'\n';
        require(q.cleanSnr>40,q.name+" clean speech SNR >40 dB");require(std::abs(q.cleanGain)<.10,q.name+" clean speech RMS change <0.1 dB");
        require(q.improvement>8,q.name+" corruption SNR improvement >8 dB");require(q.drumSnr>30,q.name+" percussion preservation SNR >30 dB");
        require(q.mouthAfter>q.mouthBefore+2,q.name+" short mouth-click train improvement >2 dB");
        writeWav(fixtures/(q.name+"-corrupted.wav"),wet);writeWav(fixtures/(q.name+"-restored.wav"),out);writeWav(fixtures/(q.name+"-clean-processed.wav"),clean);writeWav(fixtures/(q.name+"-mouth-restored.wav"),mouthOut);
        qualities.push_back(q);
    }
    writeWav(fixtures/"mouth-click-trains.wav",mouth);
    // Independent rates and maximum Amount are measured against their actual
    // clean reference. These are not merely finite-number checks.
    for(auto mode:{Mode::Declick,Mode::Decrackle})for(double fs:{44100.0,48000.0,96000.0,192000.0}){
        auto reference=resample(dry,fs);auto corrupted=reference;std::uint32_t seed=0x9671acf3u;
        auto next=[&](){seed=1664525u*seed+1013904223u;return seed;};
        const int events=mode==Mode::Declick?22:320;
        for(int j=0;j<events;++j){const size_t at=static_cast<size_t>(.6*fs)+next()%static_cast<size_t>(4.5*fs);const int len=mode==Mode::Declick?1+next()%std::max(1,static_cast<int>(fs*.0003)):1+next()%std::max(1,static_cast<int>(fs*.00005));
            const double level=(mode==Mode::Declick?.18:.02)+(next()/4294967295.0)*(mode==Mode::Declick?.2:.075);const double sign=(next()&1)?1:-1;for(int k=0;k<len;++k)corrupted[at+k]+=sign*level;}
        for(double amount:{.55,1.0}){const double clean=snr(process(reference,mode,amount,127,fs),reference);const double improvement=snr(process(corrupted,mode,amount,511,fs),reference)-snr(corrupted,reference);
            rateQualities.push_back({mode==Mode::Declick?"DECLICK":"DECRACKLE",fs,amount,clean,improvement});
            std::cerr<<(mode==Mode::Declick?"DECLICK":"DECRACKLE")<<" rate="<<fs<<" amount="<<amount<<" clean="<<clean<<" improvement="<<improvement<<'\n';
            require(clean>(amount<.9?40:32),"sample-rate clean speech preservation");require(improvement>7,"sample-rate corruption repair benefit");}
    }
}
void exactAndBlockTests(){
    for(auto mode:{Mode::Declick,Mode::Decrackle})for(double fs:{44100.0,48000.0,88200.0,96000.0,192000.0}){
        const int previous=static_cast<int>(std::ceil(fs*(mode==Mode::Declick?.004:.008)));
        const int gap=std::max(2,static_cast<int>(std::ceil(fs*(mode==Mode::Declick?.00065:.00035))));
        const int expectedDelay=std::min(previous,gap+41*std::max(1,static_cast<int>(std::lround(fs/48000.0)))+2);
        RestorationEngine e;e.prepare(fs,mode);e.setAmount(0);const int delay=e.getLatencySamples();require(delay==expectedDelay,"reported minimal context-preserving latency");
        std::vector<double> original(4096),left(4096+delay),right(4096+delay),dry(left.size());for(size_t i=0;i<original.size();++i){original[i]=.1*std::sin(i*.11);left[i]=original[i];right[i]=.37*original[i];}
        bool exact=true;size_t offset=0;
        while(offset<left.size()){const int n=std::min(1+static_cast<int>(randomBits()%521),static_cast<int>(left.size()-offset));double* d[]={left.data()+offset,right.data()+offset};double* a[]={dry.data()+offset,nullptr};guardAllocations=true;e.process(d,2,n,a);guardAllocations=false;offset+=n;channelSamples+=2*n;}
        for(size_t i=0;i<left.size();++i){const double expected=i<static_cast<size_t>(delay)?0:original[i-delay];exact=exact && left[i]==expected && dry[i]==expected && right[i]==.37*expected;}
        require(exact,"amount0 exact aligned dry and stereo scaling");
        e.reset();e.setAmount(0);std::vector<float> impulse(delay+20);impulse[0]=.25f;float* ptr[]={impulse.data()};e.process(ptr,1,static_cast<int>(impulse.size()));
        int nonzero=0,peak=-1;for(size_t i=0;i<impulse.size();++i)if(impulse[i]!=0){++nonzero;peak=static_cast<int>(i);}require(nonzero==1 && peak==delay && impulse[delay]==.25f,"measured exact impulse latency");
        auto signal=original;signal[1000]+=.5;const auto a=process(signal,mode,.65,1,fs),b=process(signal,mode,.65,4096,fs);require(a==b,"block1 vs block4096 deterministic equality");
    }
    for(auto mode:{Mode::Declick,Mode::Decrackle}){
        RestorationEngine e;e.prepare(48000,mode);e.setAmount(.8);std::vector<float> l(4800),r(4800);for(int i=0;i<2400;++i){const double fade=std::min({1.0,i/120.0,(2399-i)/120.0});r[i]=static_cast<float>(.1*fade*std::sin(i*.08));}l[700]=.8f;const auto reference=r;auto alone=r;RestorationEngine mono;mono.prepare(48000,mode);mono.setAmount(.8);float* solo[]={alone.data()};mono.process(solo,1,4800);float* p[]={l.data(),r.data()};e.process(p,2,4800);
        bool same=true;for(int i=e.getLatencySamples();i<4800;++i)same=same && r[i]==reference[i-e.getLatencySamples()];require(same,"click in left leaves clean right exactly unchanged");
        require(r==alone,"unrelated left event cannot affect right-channel decisions");
        std::vector<float> input(6000);for(size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.10*std::sin(i*.013)+.03*std::sin(i*.37));input[1700]+=.4f;
        std::vector<float> f=input,scaled=input;for(auto& v:scaled)v*=.5f;std::vector<double> d(input.begin(),input.end());
        RestorationEngine ef,ed;ef.prepare(48000,mode);ed.prepare(48000,mode);ef.setAmount(.75);ed.setAmount(.75);
        float* fp[]={f.data(),scaled.data()};double* dp[]={d.data()};ef.process(fp,2,6000);ed.process(dp,1,6000);
        bool precise=true,stereo=true;for(size_t i=0;i<f.size();++i){precise=precise && f[i]==static_cast<float>(d[i]);stereo=stereo && std::abs(scaled[i]-.5f*f[i])<1.e-7;}
        require(precise,"float/double consistency on identical representable input");require(stereo,"correlated events retain stereo scaling");
    }
}
void randomizedTests(){
    RestorationEngine e;const double rates[]={8000,44100,48000,88200,96000,192000};
    for(int config=0;config<10000;++config){const auto mode=(config&1)?Mode::Declick:Mode::Decrackle;const double fs=rates[randomBits()%6];e.prepare(fs,mode);e.setAmount(random01());
        const int total=e.getLatencySamples()+384;std::vector<float> l(total),r(total),dry(total);
        const double amplitude=.005+.8*random01(),freq=70+std::min(7500.0,fs*.32)*random01();
        for(int i=0;i<total;++i){l[i]=static_cast<float>(amplitude*(.65*std::sin(2*pi*freq*i/fs)+.03*(random01()*2-1)));r[i]=l[i]*.71f;}
        for(int k=0;k<4;++k)l[randomBits()%static_cast<unsigned>(total)]+=static_cast<float>((random01()*2-1)*.6);
        if(config%100==0)l[3]=std::numeric_limits<float>::quiet_NaN();if(config%137==0)r[7]=std::numeric_limits<float>::infinity();
        int at=0;while(at<total){const int n=std::min(1+static_cast<int>(randomBits()%257),total-at);float* p[]={l.data()+at,r.data()+at};float* d[]={dry.data()+at,nullptr};guardAllocations=true;e.process(p,2,n,d);guardAllocations=false;at+=n;channelSamples+=2*n;}
        bool finite=true;for(int i=0;i<total;++i)finite=finite && std::isfinite(l[i]) && std::isfinite(r[i]) && std::isfinite(dry[i]);require(finite,"randomized finite output/aligned dry");
    }
    require(audioAllocations==0,"zero allocations inside all guarded audio processing calls");
}
}
int main(int argc,char** argv){
    const auto start=std::chrono::steady_clock::now();const std::filesystem::path fixtures=argc>1?argv[1]:"fixtures";
    try{qualityTests(fixtures);exactAndBlockTests();randomizedTests();}catch(const std::exception& ex){require(false,ex.what());}
    std::ofstream report("restoration-dsp-report.json");report<<std::setprecision(12)<<"{\n  \"passed\": "<<(failures?"false":"true")<<",\n  \"checks\": "<<checks<<",\n  \"failures\": "<<failures<<",\n  \"randomized_configurations\": 10000,\n  \"audio_channel_samples\": "<<channelSamples<<",\n  \"audio_thread_allocations\": "<<audioAllocations<<",\n  \"quality\": [";
    for(size_t i=0;i<qualities.size();++i){const auto&q=qualities[i];report<<(i?",":"")<<"\n    {\"mode\":\""<<q.name<<"\",\"clean_speech_snr_db\":"<<q.cleanSnr<<",\"clean_gain_db\":"<<q.cleanGain<<",\"clean_changed_samples_percent\":"<<q.cleanChanged<<",\"corrupt_snr_db\":"<<q.before<<",\"restored_snr_db\":"<<q.after<<",\"snr_improvement_db\":"<<q.improvement<<",\"mouth_before_snr_db\":"<<q.mouthBefore<<",\"mouth_after_snr_db\":"<<q.mouthAfter<<",\"drum_snr_db\":"<<q.drumSnr<<"}";}
    report<<"\n  ],\n  \"sample_rate_quality\": [";
    for(size_t i=0;i<rateQualities.size();++i){const auto&r=rateQualities[i];report<<(i?",":"")<<"\n    {\"mode\":\""<<r.mode<<"\",\"sample_rate\":"<<r.rate<<",\"amount\":"<<r.amount<<",\"clean_snr_db\":"<<r.cleanSnr<<",\"repair_improvement_db\":"<<r.improvement<<"}";}
    report<<"\n  ],\n  \"elapsed_seconds\": "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\n  \"scope\":\"One clean speech excerpt, synthetic click/crackle/mouth-like bursts and percussion; no human-listening or universal-repair claim\"\n}\n";
    std::cout<<"Restoration DSP: "<<checks<<" checks, "<<failures<<" failures; audio allocations "<<audioAllocations<<'\n';return failures?1:0;
}
