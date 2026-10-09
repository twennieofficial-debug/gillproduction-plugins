#include "../Source/SpectralDSP.h"
#include "../Source/Presets.h"
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <new>
#include <limits>
#include <vector>
#include <memory>
#include <cstdint>
#include <fstream>
#include <string>
static std::atomic<size_t> allocations{0};
void* operator new(size_t n){++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
namespace {
constexpr double pi=3.141592653589793;
int checks=0,failures=0;
void check(bool b,const char*s,double v=0){++checks;failures+=!b;std::printf("%s %s %.9g\n",b?"PASS":"FAIL",s,v);}
float noise(uint32_t&s){s=s*1664525u+1013904223u;return float(double(s>>8)/8388608.-1);}
// Independent RBJ bandpass fixture and measurement, not the production detector.
struct Band{double b=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;Band(double fs,double f,double q){double w=2*pi*f/fs,a=sin(w)/(2*q);b=a/(1+a);a1=-2*cos(w)/(1+a);a2=(1-a)/(1+a);}double tick(double x){double y=b*(x-x2)-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=y;return y;}};
std::vector<float> fixture(double fs,bool resonant){std::vector<float>x(int(fs*2.4));uint32_t seed=731;Band room(fs,1480,24);double brown=0,phase=0;for(int i=0;i<int(x.size());++i){double t=i/fs,n=noise(seed);brown+=.08*(n-brown);phase+=2*pi*(143+19*sin(2*pi*1.1*t))/fs;double env=.18+.82*pow(.5+.5*sin(2*pi*3.7*t),2);double voice=0;for(int h=1;h<22;++h)voice+=sin(phase*h+.05*h)/pow(h,1.25);double bed=.055*brown+.012*n+.035*env*voice;double r=room.tick(n)*1.4*env;x[i]=float(bed+(resonant?r:0));}return x;}
template<class D,class P>std::vector<float> render(const std::vector<float>&x,double fs,bool live,P p,int block=127){auto d=std::make_unique<D>();d->setParameters(p);d->prepare(fs,block,1);d->setLiveMode(live);auto y=x;for(int at=0;at<int(y.size());at+=block){float*a[]{y.data()+at};d->process(a,std::min(block,int(y.size())-at),1);}return y;}
int latency(double fs,bool live){auto d=std::make_unique<gillfinish::SilkDSP>();d->prepare(fs,127,1);d->setLiveMode(live);return d->latencySamples();}
double bandEnergy(const std::vector<float>&x,double fs,double f,int offset=0){Band b(fs,f,8);double sum=0;for(int i=0;i<int(fs*2.2);++i){double y=b.tick(x[i+offset]);if(i>int(fs*.6))sum+=y*y;}return sum;}
void moving(double fs,bool live){gillfinish::SilkParameters p;p.depth=85;p.sensitivity=70;p.attackMs=4;p.releaseMs=140;auto x=fixture(fs,true),clean=fixture(fs,false);auto y=render<gillfinish::SilkDSP>(x,fs,live,p);int lat=latency(fs,live);double cut=-10*log10(bandEnergy(y,fs,1480,lat)/bandEnergy(x,fs,1480));std::printf("CONFIG %.0f %s\n",fs,live?"LIVE":"PRO");check(cut>1.0,"moving room resonance suppressed",cut);auto z=render<gillfinish::SilkDSP>(clean,fs,live,p);double preserved=-10*log10(bandEnergy(z,fs,500,lat)/bandEnergy(clean,fs,500));check(std::abs(preserved)<1.5,"clean moving vocal body preserved",preserved);double neighbours=-10*log10(bandEnergy(y,fs,4200,lat)/bandEnergy(x,fs,4200));check(std::abs(neighbours)<1.0,"nonresonant upper band preserved",neighbours);auto split=render<gillfinish::SilkDSP>(x,fs,live,p,511);check(y==split,"block partition invariant");p.lowHz=4000;p.highHz=10000;z=render<gillfinish::SilkDSP>(x,fs,live,p);double excluded=-10*log10(bandEnergy(z,fs,1480,lat)/bandEnergy(x,fs,1480));check(std::abs(excluded)<.2,"focus excludes room resonance",excluded);p.lowHz=120;p.highHz=12000;p.mix=.37f;z=render<gillfinish::SilkDSP>(x,fs,live,p);double err=0;for(int i=lat;i<int(x.size());++i)err=std::max(err,std::abs(double(z[i])-(x[i-lat]+.37f*(y[i]-x[i-lat]))));check(err<2e-7,"mix independently matches aligned wet dry",err);p.mix=1;p.outputDb=-6;z=render<gillfinish::SilkDSP>(x,fs,live,p);err=0;for(int i=lat;i<int(x.size());++i)err=std::max(err,std::abs(z[i]-y[i]*pow(10.,-.3)));check(err<2e-7,"output trim measured on active audio",err);}
}
template<class D,class P>void safety(double fs,bool live,P p){auto d=std::make_unique<D>();d->setParameters(p);d->prepare(fs,257,1);d->setLiveMode(live);int lat=d->latencySamples();check(live?lat==0:lat>0,"quality reports causal LIVE / delayed PRO");p.depth=0;d->setParameters(p);d->reset();std::vector<float>x(lat+2048);x[0]=.25f;float*ptr[]{x.data()};d->process(ptr,int(x.size()),1);bool exact=true;for(int i=0;i<int(x.size());++i)exact &= x[i]==(i==lat?.25f:0.f);check(exact,"neutral impulse agrees with reported PDC");p.depth=85;d->setParameters(p);d->reset();std::array<float,257>a{};float*q[]{a.data()};auto before=allocations.load();bool finite=true;for(int b=0;b<60;++b){for(int i=0;i<257;++i)a[i]=float(.2*sin(i*.13));if(b==3){a[5]=std::numeric_limits<float>::infinity();a[6]=std::numeric_limits<float>::quiet_NaN();}d->process(q,257,1);for(auto v:a)finite&=std::isfinite(v);}check(allocations.load()==before,"active DSP performs zero heap allocations");check(finite,"active DSP rejects nonfinite input");}
void spark(double fs,bool live,std::ofstream&dump){gillfinish::SparkParameters p;const auto presets=gillfinish::presets(FinishKind::Spark);for(const auto&v:presets[0].values){if(v.first=="depth")p.depth=v.second;if(v.first=="sensitivity")p.sensitivity=v.second;if(v.first=="low")p.lowHz=v.second;if(v.first=="high")p.highHz=v.second;if(v.first=="attack")p.attackMs=v.second;if(v.first=="release")p.decayMs=v.second;}check(p.depth==70&&p.sensitivity==65&&p.lowHz==1500&&p.highHz==18000&&p.attackMs==.1f&&p.decayMs==45,"REMOVE MOUTH CLICKS exact preset retained");auto base=fixture(fs,false),clicks=base;int at=int(fs*1.2),length=int(fs*.003);for(int j=0;j<length;++j)clicks[at+j]+=float(.6*sin(pi*j/length)*sin(2*pi*7000*j/fs));double values[2]{};for(int mode=0;mode<2;++mode){p.mode=mode;auto y=render<gillfinish::SparkDSP>(clicks,fs,live,p),clean=render<gillfinish::SparkDSP>(base,fs,live,p);auto d=std::make_unique<gillfinish::SparkDSP>();d->prepare(fs,127,1);d->setLiveMode(live);int lat=d->latencySamples();double before=0,after=0;for(int i=at;i<at+int(fs*.1);++i){before+=pow(clicks[i]-base[i],2);after+=pow(y[i+lat]-clean[i+lat],2);}values[mode]=10*log10(after/before);check(mode?values[mode]>.1:values[mode]<-.1,mode?"Spark BOOST raises burst energy":"Spark CUT lowers mouth burst energy",values[mode]);if(dump)dump.write(reinterpret_cast<const char*>(y.data()),std::streamsize(y.size()*sizeof(float)));}safety<gillfinish::SparkDSP>(fs,live,p);}
double timing(double fs,bool live,float attack,float release,bool opening){gillfinish::SilkParameters p;p.depth=opening?0:100;p.sensitivity=100;p.attackMs=attack;p.releaseMs=release;auto d=std::make_unique<gillfinish::SilkDSP>();d->prepare(fs,127,1);d->setLiveMode(live);d->setParameters(p);d->reset();auto x=fixture(fs,true),y=x;int change=int(fs*1.2),lat=d->latencySamples();float*a[]{y.data()};d->process(a,change,1);p.depth=opening?100:0;d->setParameters(p);a[0]=y.data()+change;d->process(a,int(y.size())-change,1);double e=0;for(int i=change;i<change+int(fs*.08);++i)e+=pow(y[i+lat]-x[i],2);return e;}
void strength(double fs,bool live){
    const auto x=fixture(fs,true),clean=fixture(fs,false);const int lat=latency(fs,live);
    double previous=0;
    for(float depth:{35.f,70.f,100.f}){
        gillfinish::SilkParameters p;p.depth=depth;p.sensitivity=100;
        auto y=render<gillfinish::SilkDSP>(x,fs,live,p);
        const double cut=-10*log10(bandEnergy(y,fs,1480,lat)/bandEnergy(x,fs,1480));
        check(cut>previous+.7,"depth increases measured resonance suppression",cut);previous=cut;
        if(depth==100){
            check(cut>3,"maximum setting clearly suppresses room mode by over 3 dB",cut);
            auto z=render<gillfinish::SilkDSP>(clean,fs,live,p);
            const double body=-10*log10(bandEnergy(z,fs,500,lat)/bandEnergy(clean,fs,500));
            check(std::abs(body)<1.5,"maximum setting retains clean harmonic vocal body",body);
            auto before=x,after=y;
            for(int i=0;i<int(x.size());++i){before[i]-=clean[i];after[i]-=z[i];}
            const double injected=-10*log10(bandEnergy(after,fs,1480,lat)/bandEnergy(before,fs,1480));
            check(injected>3,"paired clean contaminated renders suppress injected room component",injected);
        }
    }
    const double attack=timing(fs,live,.1f,140,true)/timing(fs,live,80,140,true);
    const double release=timing(fs,live,4,600,false)/timing(fs,live,4,10,false);
    check(attack>2,"fast attack removes more onset energy than slow attack",attack);
    check(release>2,"long release retains more attenuation during recovery",release);
}
int main(int argc,char**argv){std::ofstream dump;if(argc>1)dump.open(argv[1],std::ios::binary);for(double fs:{44100.,48000.,88200.,96000.})for(bool live:{false,true}){moving(fs,live);strength(fs,live);safety<gillfinish::SilkDSP>(fs,live,gillfinish::SilkParameters{});spark(fs,live,dump);}std::printf("SILK_MOVING_RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}

