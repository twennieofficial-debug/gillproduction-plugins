#include "../Source/CharacterDSP.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>

static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace {
using namespace gillfinish;
constexpr double rates[]{8000,32000,44100,48000,96000,192000};
int checks=0,failures=0;
void check(bool ok,const char* name,double metric=0){++checks;failures+=!ok;std::printf("%s %-80s %.9g\n",ok?"PASS":"FAIL",name,metric);}
float random(std::uint32_t& state){state=1664525U*state+1013904223U;return static_cast<float>((state>>8)*(2./16777215.)-1.);}
template<class D>void run(D&d,std::vector<float>&l,std::vector<float>&r,int block=127,int channels=2){for(std::size_t at=0;at<l.size();at+=static_cast<std::size_t>(block)){float* ptr[]{l.data()+at,r.data()+at};d.process(ptr,static_cast<int>(std::min(static_cast<std::size_t>(block),l.size()-at)),channels);}}
std::vector<float> toneSignal(int n,double hz,double amplitude=.1,double fs=48000){std::vector<float> a(static_cast<std::size_t>(n));for(int i=0;i<n;++i)a[static_cast<std::size_t>(i)]=static_cast<float>(amplitude*std::sin(2*pi*hz*i/fs));return a;}
double toneGain(const std::vector<float>&x,int start,int count,double hz,double fs=48000){double re=0,im=0;for(int i=0;i<count;++i){const double ph=2*pi*hz*i/fs;re+=x[static_cast<std::size_t>(start+i)]*std::cos(ph);im+=x[static_cast<std::size_t>(start+i)]*std::sin(ph);}return 2*std::hypot(re,im)/count;}
double rms(const std::vector<float>&x,int start,int count){double a=0;for(int i=start;i<start+count;++i)a+=static_cast<double>(x[static_cast<std::size_t>(i)])*x[static_cast<std::size_t>(i)];return std::sqrt(a/count);}
StripParameters cleanStrip(){StripParameters p;p.compress=p.deess=p.space=p.echo=p.width=0;return p;}

// Independent bilinear-transform responses of analogue filter prototypes.
// This does not read Biquad coefficients or call its magnitude helper.
std::complex<double> analytic(int type,double fs,double hz,double cutoff,double q,double dB){
    cutoff=std::clamp(cutoff,5.,fs*.475);const double r=std::tan(pi*hz/fs)/std::tan(pi*cutoff/fs),A=std::pow(10.,dB/40.),a=1-r*r;const std::complex<double> j(0,1);
    if(type==0)return 1./(a+j*r/q);
    if(type==1)return -r*r/(a+j*r/q);
    if(type==2)return (a+j*r*A/q)/(a+j*r/(A*q));
    const auto low=A*(A-r*r+j*std::sqrt(2*A)*r)/(1-A*r*r+j*std::sqrt(2*A)*r);
    return type==3?low:A*A/low;
}
std::complex<double> goldResponse(const GoldParameters&p,double fs,double hz){return analytic(3,fs,hz,p.lowHz,.707,p.lowBoost)*analytic(3,fs,hz,p.lowHz*2.4,.707,-p.lowCut)*analytic(2,fs,hz,p.highHz,2.8-2.45*p.bandwidth*.01,p.highBoost)*analytic(4,fs,hz,p.cutHz,.707,-p.highCut);}

void gold(){
    bool exact=true,lowIndependent=true;double worst=0,analyticWorst=0;
    for(double fs:rates){
        GoldDSP d;GoldParameters p;d.setParameters(p);d.prepare(fs,127,2);auto l=toneSignal(4096,731,.2,fs),r=l,original=l;run(d,l,r);exact=exact&&l==original&&r==original&&d.latencySamples()==0;
        p.lowBoost=12;p.lowCut=9;p.highBoost=11;p.highCut=8;p.mix=0;d.setParameters(p);d.reset();l=original;r=l;run(d,l,r);exact=exact&&l==original&&r==original;
        for(int mode=0;mode<3;++mode){p={};p.lowBoost=mode==1?0.f:8.f;p.lowCut=mode==0?0.f:8.f;p.highBoost=5;p.highCut=4;p.bandwidth=65;p.highHz=3000;p.cutHz=3500;d.setParameters(p);d.reset();
            for(double hz:{30.,60.,140.,500.,1500.,3000.}){if(hz>fs*.42)continue;const int count=static_cast<int>(fs);l=toneSignal(2*count,hz,.1,fs);r=l;run(d,l,r);const double measured=toneGain(l,count,count,hz,fs)/.1,expected=std::abs(goldResponse(p,fs,hz));worst=std::max(worst,std::abs(db(measured/expected)));analyticWorst=std::max(analyticWorst,std::abs(db(d.magnitude(hz)/expected)));}
        }
    }
    check(exact,"GOLD neutral / MIX0 is sample-exact at six rates including8k, zero latency");
    check(worst<.01,"GOLD rendered filter response agrees with independent analytic response dB",worst);
    check(analyticWorst<1.e-6,"GOLD magnitude display matches independent analogue prototype calculation dB",analyticWorst);
    std::array<double,3> low{},dip{};for(int mode=0;mode<3;++mode){GoldParameters p;p.lowBoost=mode==1?0.f:8.f;p.lowCut=mode==0?0.f:8.f;low[static_cast<std::size_t>(mode)]=db(std::abs(goldResponse(p,48000,10)));dip[static_cast<std::size_t>(mode)]=db(std::abs(goldResponse(p,48000,150)));}
    lowIndependent=low[0]>7.9&&low[1]<-7.9&&std::abs(low[2])<.1&&dip[2]<-3;check(lowIndependent,"GOLD simultaneous independent low boost/attenuation retains bass with upper-bass dip",dip[2]);
}

void dive(){
    DiveDSP d;DiveParameters p;p.depth=75;p.resonance=0;p.motion=0;p.envelope=0;d.setParameters(p);d.prepare(48000,127,2);double ratios[2]{};
    for(int b=0;b<2;++b){d.reset();auto l=toneSignal(96000,b?8000:100,.1),r=l;run(d,l,r);ratios[b]=toneGain(l,48000,48000,b?8000:100)/.1;}
    check(db(ratios[1]/ratios[0])<-60,"DIVE underwater setting rejects8kHz more than100Hz by60dB",db(ratios[1]/ratios[0]));
    check(std::abs(db(ratios[0]))<.2,"DIVE underwater setting retains100Hz bass within0.2dB",db(ratios[0]));
    bool exact=true;for(double fs:rates){p.mix=0;p.depth=100;p.resonance=100;d.setParameters(p);d.prepare(fs,127,2);auto l=toneSignal(4096,731,.2,fs),r=l,original=l;run(d,l,r);exact=exact&&l==original&&r==original&&d.latencySamples()==0;}check(exact,"DIVE MIX0 gives exact zero-latency dry at six sample rates");
    p={};p.depth=50;p.motion=60;p.rateHz=2;p.resonance=0;d.setParameters(p);d.prepare(48000,128,1);std::array<float,128> samples{};float* ptr[]{samples.data()};double lowest=1.e9,highest=0;for(int at=0;at<48000;at+=128){samples.fill(0);d.process(ptr,128,1);lowest=std::min(lowest,static_cast<double>(d.cutoffHz()));highest=std::max(highest,static_cast<double>(d.cutoffHz()));}check(highest/lowest>10,"DIVE LFO moves cutoff through its expected multi-octave range",highest/lowest);
    p.motion=0;p.envelope=75;d.setParameters(p);d.reset();const double resting=d.cutoffHz();for(int k=0;k<200;++k){samples.fill(.25f);d.process(ptr,128,1);}const double raised=d.cutoffHz();for(int k=0;k<600;++k){samples.fill(0);d.process(ptr,128,1);}const double returned=d.cutoffHz();check(raised/resting>4 && std::abs(returned/resting-1)<.001,"DIVE positive envelope opens and then returns the cutoff",raised/resting);
    p.envelope=-75;d.setParameters(p);d.reset();for(int k=0;k<200;++k){samples.fill(.25f);d.process(ptr,128,1);}check(d.cutoffHz()/resting<.3,"DIVE negative envelope closes the cutoff",d.cutoffHz()/resting);
    d.reset();check(std::abs(d.cutoffHz()-DiveDSP::baseCutoff(p.depth,48000))<.001,"DIVE reset clears modulation and detector history");
    std::array<double,2> resonant{};p={};p.depth=50;p.motion=p.envelope=0;for(int amount=0;amount<2;++amount){p.resonance=amount?100.f:0.f;d.setParameters(p);d.reset();const double hz=d.cutoffHz();auto l=toneSignal(96000,hz,.05),r=l;run(d,l,r);resonant[static_cast<std::size_t>(amount)]=toneGain(l,48000,48000,hz);}check(db(resonant[1]/resonant[0])>5,"DIVE resonance control creates a measurable cutoff emphasis dB",db(resonant[1]/resonant[0]));
}

void stripDynamics(){
    bool exact=true;for(double fs:rates){StripDSP d;auto p=cleanStrip();d.setParameters(p);d.prepare(fs,127,2);auto l=toneSignal(12000,731,.2,fs),r=l,original=l;run(d,l,r);exact=exact&&l==original&&r==original&&d.latencySamples()==0;}check(exact,"STRIP all sections off is exact neutral stereo at six rates");
    StripDSP d;auto p=cleanStrip();p.compress=85;d.setParameters(p);d.prepare(48000,127,2);std::array<double,2> levels{};for(int level=0;level<2;++level){d.reset();auto l=toneSignal(96000,750,level?.5:.05),r=l;run(d,l,r);levels[static_cast<std::size_t>(level)]=rms(l,48000,48000);}const double range=db(levels[1]/levels[0]);check(range<8 && range>0,"STRIP compression reduces20dB input dynamic range below8dB",range);check(d.reductionDb()>10 && d.reductionDb()<=24.01,"STRIP compressor meter reports bounded active reduction",d.reductionDb());
    p=cleanStrip();p.deess=100;d.setParameters(p);d.reset();std::array<double,3> reduction{};for(int band=0;band<3;++band){const double hz=band==0?500:band==1?5800:12000;d.reset();auto l=toneSignal(96000,hz,.3),r=l;run(d,l,r);reduction[static_cast<std::size_t>(band)]=db(toneGain(l,48000,48000,hz)/.3);}
    check(reduction[2]<-5,"STRIP de-esser attenuates a strong12-kHz sibilant by5dB",reduction[2]);check(std::abs(reduction[0])<.2,"STRIP de-esser preserves500-Hz low vocal within0.2dB",reduction[0]);check(reduction[1]<=.1,"STRIP de-esser does not boost the targeted crossover band",reduction[1]);
}

void stripTime(){
    bool exact=true;double worst=0;
    for(double fs:rates)for(int style=0;style<3;++style){StripDSP d;auto p=cleanStrip();p.echo=100;p.style=style;p.bpm=120;d.setParameters(p);d.prepare(fs,127,2);const double delay=(style==1?.095:style==2?.375:.5)*fs;const int n=static_cast<int>(std::ceil(delay))+4;std::vector<float> l(static_cast<std::size_t>(n)),r=l;l[0]=1;run(d,l,r);int onset=-1;for(int i=1;i<n;++i)if(std::abs(l[static_cast<std::size_t>(i)])>1.e-6){onset=i;break;}worst=std::max(worst,std::abs(onset-delay));exact=exact&&l[0]==1&&onset>=0&&std::abs(onset-delay)<=1.01;}
    check(exact,"STRIP clean/slap/dotted echo first-repeat timing within one sample at six rates",worst);
    // Let a tempo change settle, then measure the delay of a fresh impulse.
    bool changesSettle=true;for(int target=0;target<2;++target){StripDSP engine;auto p=cleanStrip();p.echo=100;p.bpm=120;engine.setParameters(p);engine.prepare(48000,127,2);std::vector<float>a(4800),b=a;run(engine,a,b);p.bpm=60;engine.setParameters(p);a.assign(240,0);b=a;run(engine,a,b);if(target==1)p.style=1;else p.bpm=90;engine.setParameters(p);a.assign(240,0);b=a;run(engine,a,b);p.bpm=60;engine.setParameters(p);a.assign(4800,0);b=a;run(engine,a,b);const int expected=target?4560:48000;a.assign(static_cast<std::size_t>(expected+4),0);b=a;a[0]=1;run(engine,a,b);int onset=-1;for(int i=1;i<static_cast<int>(a.size());++i)if(std::abs(a[static_cast<std::size_t>(i)])>1.e-6){onset=i;break;}changesSettle=changesSettle&&onset==expected;}
    check(changesSettle,"STRIP queued tempo/style changes reach correct delay within100ms");
    {StripDSP engine;auto p=cleanStrip();p.echo=100;p.style=2;engine.setParameters(p);engine.prepare(48000,127,2);std::vector<float>a(36004),b=a;a[0]=1;run(engine,a,b);check(std::abs(a[18000]-.65)<1.e-6&&b[18000]==0&&b[36000]>.01,"STRIP dotted echo feedback crosses to the opposite stereo channel",b[36000]);}
    StripDSP d;auto p=cleanStrip();p.echo=100;p.bpm=20;d.setParameters(p);d.prepare(8000,127,1);const int n=8000*32;std::vector<float> l(static_cast<std::size_t>(n)),r=l;l[0]=1;run(d,l,r,127,1);double pastSeven=0;for(int i=8000*8;i<8000*12;++i)pastSeven=std::max(pastSeven,std::abs(static_cast<double>(l[static_cast<std::size_t>(i)])));const int reportedEnd=std::min(n-1,static_cast<int>(std::ceil(d.tailSeconds()*8000)));double atTail=0;for(int i=reportedEnd;i<n;++i)atTail=std::max(atTail,std::abs(static_cast<double>(l[static_cast<std::size_t>(i)])));check(pastSeven>1.e-3,"STRIP slow-tempo echo genuinely continues beyond seven seconds",pastSeven);check(atTail<1.e-4,"STRIP reported tail contains all significant slow-tempo repeats",atTail);
    p=cleanStrip();p.space=100;p.style=2;d.setParameters(p);d.prepare(48000,127,2);l.assign(48000*5,0);r=l;l[0]=r[0]=1;run(d,l,r);const double early=rms(l,4800,19200),late=rms(l,48000*4,48000);check(early>1.e-4&&late<early*.001,"STRIP room generates a real tail and decays by60dB within five seconds",db(late/early));bool stereoDifferent=false;for(std::size_t i=1000;i<l.size();++i)stereoDifferent=stereoDifferent||l[i]!=r[i];check(stereoDifferent,"STRIP room produces distinct stereo reflections from mono input");
    p=cleanStrip();p.width=100;d.setParameters(p);d.prepare(48000,127,2);l=toneSignal(48000,731,.2);r=l;run(d,l,r);double difference=0;for(int i=12000;i<48000;++i)difference+=std::abs(l[static_cast<std::size_t>(i)]-r[static_cast<std::size_t>(i)]);check(difference>10,"STRIP width produces measurable stereo decorrelation",difference/36000);d.prepare(48000,127,1);l=toneSignal(48000,731,.2);r=l;run(d,l,r,127,1);bool finite=true;for(float x:l)finite=finite&&std::isfinite(x);check(finite&&r==toneSignal(48000,731,.2),"STRIP width supports mono without touching absent channel");
}

template<class D,class P>void stress(const char*name,P base){
    bool finite=true,identical=true,silent=true,noAlloc=true;double biggest=0;
    for(double fs:rates){D a,b;a.setParameters(base);b.setParameters(base);a.prepare(fs,1,2);b.prepare(fs,1024,2);auto l=toneSignal(22000,731,.2,fs),r=l,other=l,otherR=l;run(a,l,r,1);run(b,other,otherR,1024);identical=identical&&l==other&&r==otherR;a.reset();l.assign(12000,0);r=l;run(a,l,r);for(float x:l)silent=silent&&x==0;
        std::array<float,511> left{},right{};float*ptr[]{left.data(),right.data()};std::uint32_t seed=42;const auto before=allocations.load();for(int block=0;block<240;++block){P p=base;
            if constexpr(std::is_same_v<P,GoldParameters>){p.lowBoost=block%2?12.f:0.f;p.lowCut=block%3?18.f:0.f;p.lowHz=block%2?20.f:200.f;p.highBoost=12;p.highHz=block%2?1000.f:20000.f;p.highCut=block%2?18.f:0.f;p.bandwidth=static_cast<float>(block%101);p.mix=static_cast<float>(block%5)/4;p.outputDb=block%2?6.f:-18.f;}
            else if constexpr(std::is_same_v<P,DiveParameters>){p.depth=block%2?100.f:0.f;p.resonance=100;p.motion=100;p.rateHz=block%2?20.f:.01f;p.envelope=block%2?100.f:-100.f;p.mix=static_cast<float>(block%5)/4;}
            else{p.compress=100;p.deess=100;p.space=100;p.echo=100;p.width=100;p.bass=block%2?12.f:-12.f;p.treble=block%2?12.f:-12.f;p.style=block%3;p.bpm=block%2?20.f:300.f;}
            if(block==7){p.outputDb=std::numeric_limits<float>::quiet_NaN();}
            a.setParameters(p);const int count=1+static_cast<int>((seed>>5)%511);for(int i=0;i<count;++i){left[static_cast<std::size_t>(i)]=.2f*random(seed);right[static_cast<std::size_t>(i)]=-.7f*left[static_cast<std::size_t>(i)];}if(block==3&&count>3){left[0]=std::numeric_limits<float>::infinity();left[1]=std::numeric_limits<float>::quiet_NaN();left[2]=-std::numeric_limits<float>::max();}a.process(ptr,count,2);for(int i=0;i<count;++i){finite=finite&&std::isfinite(left[static_cast<std::size_t>(i)])&&std::isfinite(right[static_cast<std::size_t>(i)]);biggest=std::max(biggest,std::abs(static_cast<double>(left[static_cast<std::size_t>(i)])));}}
        noAlloc=noAlloc&&allocations.load()==before;
    }
    check(identical,(std::string(name)+" processing is block-independent at six rates").c_str());check(silent,(std::string(name)+" reset/silence stays exactly silent at six rates").c_str());check(finite,(std::string(name)+" extreme parameters, NaN/Inf samples and random blocks remain finite").c_str(),biggest);check(noAlloc,(std::string(name)+" processing and parameter automation allocate no memory").c_str());
}
template<class D,class P>void bench(const char*name,P p){D d;d.setParameters(p);d.prepare(48000,128,2);std::array<float,128>l{},r{};float*ptr[]{l.data(),r.data()};const auto start=std::chrono::steady_clock::now();for(int at=0;at<48000*3;at+=128){for(int i=0;i<128;++i)l[static_cast<std::size_t>(i)]=r[static_cast<std::size_t>(i)]=static_cast<float>(.2*std::sin(2*pi*731*(at+i)/48000));d.process(ptr,128,2);}const double t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::printf("BENCH %s stereo48k/128 three seconds: %.6fs, %.3f%% realtime budget\n",name,t,t/3*100);}
}
int main(){std::printf("C++17; pointer bits %zu\n",sizeof(void*)*8);gold();dive();stripDynamics();stripTime();GoldParameters g;g.lowBoost=5;g.highBoost=6;DiveParameters d;d.depth=65;d.motion=40;StripParameters s;stress<GoldDSP>("GOLD",g);stress<DiveDSP>("DIVE",d);stress<StripDSP>("STRIP",s);bench<GoldDSP>("GOLD",g);bench<DiveDSP>("DIVE",d);bench<StripDSP>("STRIP",s);std::printf("RESULT %d checks, %d failures\n",checks,failures);return failures?1:0;}
