#include "../Source/StageDSP.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>

namespace {bool watch=false;std::uint64_t allocations=0;int checks=0,failures=0;}
void* operator new(std::size_t n){if(watch)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
constexpr double pi=3.14159265358979323846;
void check(bool ok,const char* text,double value=0){++checks;std::printf("%s %s %.9g\n",ok?"PASS":"FAIL",text,value);if(!ok)++failures;}
std::vector<float> signal(double fs,double hz,double seconds=.4){std::vector<float> v(static_cast<size_t>(fs*seconds));for(size_t i=0;i<v.size();++i)v[i]=static_cast<float>(.3*std::sin(2*pi*hz*i/fs)+.08*std::sin(2*pi*hz*2*i/fs));return v;}
double rms(const std::vector<float>& x,size_t start=0){double s=0;for(size_t i=start;i<x.size();++i)s+=x[i]*x[i];return std::sqrt(s/(x.size()-start));}
void process(gill::StageDSP& d,std::vector<float>& l,std::vector<float>* r=nullptr,int block=127){for(int at=0;at<static_cast<int>(l.size());at+=block){float* p[]{l.data()+at,r?r->data()+at:nullptr};watch=true;d.process(p,r?2:1,std::min(block,static_cast<int>(l.size())-at));watch=false;}}
double maxError(const std::vector<float>&a,const std::vector<float>&b){double v=0;for(size_t i=0;i<a.size();++i)v=std::max(v,std::abs(static_cast<double>(a[i]-b[i])));return v;}
std::array<std::vector<float>,2> automatedPan(int block){
    gill::StageDSP dsp;dsp.prepare(48000,block,2);
    std::array<std::vector<float>,2> result{std::vector<float>(6720,0),std::vector<float>(6720,.2f)};
    for(int at=0;at<6720;){
        watch=true;
        if(at==960)dsp.setParameters(-1,0,100,0,100,0,false);
        if(at==2880)dsp.setParameters(1,0,100,0,100,0,false);
        if(at==4800)dsp.setParameters(0,0,100,0,100,0,false);
        const int event=at<960?960:at<2880?2880:at<4800?4800:6720;
        const int count=std::min(block,event-at);float* p[]{result[0].data()+at,result[1].data()+at};
        dsp.process(p,2,count);watch=false;at+=count;
    }return result;
}
void completeStereoPositioning(){
    const auto left=signal(48000,193.7),right=signal(48000,307.3);
    // Independent endpoint contract: both original channels contribute with
    // 1/sqrt(2) gain in the requested destination, including opposite-only input.
    for(int inputKind=0;inputKind<3;++inputKind)for(float x:{-1.f,1.f}){
        auto sourceL=left,sourceR=right;
        if(inputKind==0)std::fill(sourceL.begin(),sourceL.end(),0.f);
        if(inputKind==1)std::fill(sourceR.begin(),sourceR.end(),0.f);
        gill::StageDSP dsp;dsp.prepare(48000,127,2);dsp.setParameters(x,0,100,0,100,0,false);
        auto l=sourceL,r=sourceR;process(dsp,l,&r);const auto& destination=x<0?l:r;const auto& opposite=x<0?r:l;
        double error=0;for(size_t i=0;i<l.size();++i){const double expected=(static_cast<double>(sourceL[i])+sourceR[i])/std::sqrt(2.);error=std::max(error,std::abs(destination[i]-expected));}
        check(error<1e-7&&rms(destination)>.1,"whole stereo source survives at pan destination including opposite-only input",error);
        check(rms(opposite)==0,"whole-source endpoint leaves opposite output exactly silent");
    }
    for(float x:{-.75f,-.25f,.25f,.75f})for(float spread:{0.f,35.f,100.f}){
        gill::StageDSP dsp;dsp.prepare(48000,127,2);dsp.setParameters(x,0,spread,0,100,0,false);
        auto l=left,r=right;process(dsp,l,&r);double error=0;
        // Analytic routing matrix, independently calculated from the two inputs.
        const double width=spread*.01*(1-std::abs(x));
        const double gainL=std::sqrt(2.)*std::cos((x+1)*pi*.25),gainR=std::sqrt(2.)*std::sin((x+1)*pi*.25);
        for(size_t i=0;i<l.size();++i){const double expectedL=.5*gainL*((1+width)*left[i]+(1-width)*right[i]);const double expectedR=.5*gainR*((1-width)*left[i]+(1+width)*right[i]);error=std::max(error,std::max(std::abs(l[i]-expectedL),std::abs(r[i]-expectedR)));}
        check(error<1e-7,"asymmetric stereo matches whole-source pan and width routing matrix",error);
    }
    for(float x:{-1.f,0.f,1.f}){
        auto l=left,r=left;for(auto&v:r)v=-v;const auto originalR=r;
        gill::StageDSP dsp;dsp.prepare(48000,127,2);dsp.setParameters(x,0,100,0,100,0,false);process(dsp,l,&r);
        check(x==0?(l==left&&r==originalR):(rms(l)==0&&rms(r)==0),"anti-phase source stays exact in center and cancels when folded to side mono");
    }
    const auto reference=automatedPan(127);
    for(int block:{1,4096})check(reference==automatedPan(block),"whole-source automated pan is sample-exact across 1/127/4096 blocks");
    double jump=0;for(const auto& channel:reference)for(size_t i=1;i<channel.size();++i)jump=std::max(jump,std::abs(static_cast<double>(channel[i]-channel[i-1])));
    check(jump<.001,"opposite-only source moves across center smoothly under pan automation",jump);
    const double expected=.2f/std::sqrt(2.);
    check(std::abs(reference[0][2400]-expected)<1e-7&&reference[1][2400]==0&&reference[0][4400]==0&&std::abs(reference[1][4400]-expected)<1e-7,"automated endpoints route the original right channel completely to either side");
    check(reference[0].back()==0&&reference[1].back()==.2f,"return to center restores exact original channel isolation");
}
int main(){
    for(double fs:{8000.,11025.,22050.,44100.,48000.,88200.,96000.,192000.}){
        std::printf("RATE %.0f\n",fs);auto original=signal(fs,211.3),right=signal(fs,307.7);auto l=original,r=right;
        gill::StageDSP neutral;neutral.prepare(fs,127,2);process(neutral,l,&r);check(l==original&&r==right,"near center stereo neutral is sample-exact");check(neutral.latencySamples()==0,"immediate direct path declares zero PDC");
        gill::StageDSP dry;dry.prepare(fs,127,2);dry.setParameters(.8f,1,20,100,0,0,false);l=original;r=right;process(dry,l,&r);check(l==original&&r==right,"MIX0 at unity output is sample-exact despite active effects");
        gill::StageDSP mono;mono.prepare(fs,127,1);l=original;process(mono,l);check(l==original,"near center mono neutral is sample-exact");
        gill::StageDSP wet;wet.prepare(fs,127,2);wet.setParameters(-.23f,.71f,83,91,100,0,false);l=original;r=right;process(wet,l,&r);
        for(int block:{1,4096}){gill::StageDSP other;other.prepare(fs,block,2);other.setParameters(-.23f,.71f,83,91,100,0,false);auto a=original,b=right;process(other,a,&b,block);check(a==l&&b==r,"effect result is sample-exact across host blocks");}
        wet.reset();auto a=original,b=right;process(wet,a,&b);check(a==l&&b==r,"reset clears audio and LFO history deterministically");
        gill::StageDSP invalid;invalid.prepare(fs,127,2);invalid.setParameters(std::numeric_limits<float>::quiet_NaN(),999,-4,999,100,999,true);a=original;b=right;a[0]=std::numeric_limits<float>::infinity();a[1]=std::numeric_limits<float>::quiet_NaN();a[2]=std::numeric_limits<float>::max();b[4]=-std::numeric_limits<float>::max();process(invalid,a,&b);bool finite=true;for(size_t i=0;i<a.size();++i)finite&=std::isfinite(a[i])&&std::isfinite(b[i])&&std::abs(a[i])<=32&&std::abs(b[i])<=32;check(finite,"NaN Inf extreme floats and invalid parameters stay finite bounded");
    }
    completeStereoPositioning();
    constexpr double fs=48000;const auto original=signal(fs,220,1);
    for(float x:{-1.f,0.f,1.f}){gill::StageDSP pan;pan.prepare(fs,127,2);pan.setParameters(x,0,100,0,100,0,false);auto l=original,r=original;process(pan,l,&r);const double power=std::sqrt((rms(l)*rms(l)+rms(r)*rms(r))/(2*rms(original)*rms(original)));check(std::abs(power-1)<1e-6,"mono-source stereo pan preserves summed power",power);check(x==0||(x<0?rms(r):rms(l))==0,"pan endpoint completely silences opposite side");}
    for(double hz:{200.,8000.}){gill::StageDSP far;far.prepare(fs,127,2);far.setParameters(0,1,100,0,100,0,false);auto l=signal(fs,hz,1),r=l;const double input=rms(l,4800);process(far,l,&r);const double attenuation=20*std::log10(rms(l,4800)/input);check(attenuation< -9&&attenuation> -30,"distance reduces sound with bounded early reflections",attenuation);if(hz>1000)check(attenuation< -18,"far position adds measurable high-frequency damping",attenuation);}
    {
        gill::StageDSP d;d.prepare(fs,127,2);d.setParameters(0,1,100,0,100,0,false);std::vector<float> l(24000),r(24000);l[0]=r[0]=1;process(d,l,&r);check(l[0]>0&&r[0]>0,"distance impulse retains immediate direct arrival",l[0]);double later=0;for(int i=500;i<2500;++i)later+=l[i]*l[i]+r[i]*r[i];check(later>1e-4,"distance adds actual early reflection energy",later);double tail=0;for(int i=12000;i<24000;++i)tail=std::max(tail,std::abs(static_cast<double>(l[i]))+std::abs(static_cast<double>(r[i])));check(tail<1e-12,"declared quarter-second tail contains all audible output",tail);
    }
    {
        auto input=signal(fs,223.7,3);for(size_t i=0;i<input.size();++i)input[i]+=static_cast<float>(.08*std::sin(2*pi*701.3*i/fs));gill::StageDSP d;d.prepare(fs,127,2);d.setParameters(0,0,100,100,100,0,false);auto l=input,r=input;process(d,l,&r);double side=0,mid=0;for(size_t i=4800;i<l.size();++i){side+=(l[i]-r[i])*(l[i]-r[i]);mid+=(l[i]+r[i])*(l[i]+r[i]);}const double sideDb=10*std::log10(side/mid);check(sideDb> -20&&sideDb<0,"doubler creates genuine decorrelated stereo side energy",sideDb);check(rms(l)>.6*rms(input)&&rms(l)<1.4*rms(input),"full doubler maintains useful bounded vocal level",rms(l)/rms(input));
        gill::StageDSP mono;mono.prepare(fs,127,2);mono.setParameters(0,0,100,100,100,0,true);auto ml=input,mr=input;process(mono,ml,&mr);check(ml==mr,"MONO CHECK emits identical channels");double error=0;for(size_t i=0;i<ml.size();++i)error=std::max(error,std::abs(ml[i]-.5*(l[i]+r[i])));check(error<1e-7,"MONO CHECK equals independent stereo average",error);
        gill::StageDSP width; width.prepare(fs,127,2);width.setParameters(0,0,0,100,100,0,false);auto wl=input,wr=input;process(width,wl,&wr);check(wl==wr,"zero SPREAD folds the complete doubler image to mono");
    }
    {
        auto input=signal(fs,267.5,1);gill::StageDSP wet,mixed;wet.prepare(fs,127,2);mixed.prepare(fs,127,2);wet.setParameters(.4f,.6f,80,70,100,0,false);mixed.setParameters(.4f,.6f,80,70,50,0,false);auto l=input,r=input,ml=input,mr=input;process(wet,l,&r);process(mixed,ml,&mr);double error=0;for(size_t i=0;i<l.size();++i)error=std::max(error,std::max(std::abs(ml[i]-(input[i]+.5*(l[i]-input[i]))),std::abs(mr[i]-(input[i]+.5*(r[i]-input[i])))));check(error<1e-7,"MIX50 follows independent zero-latency dry/wet reference",error);
    }
    {
        // Stress block-by-block automation; constant input isolates parameter clicks.
        gill::StageDSP d;d.prepare(fs,127,2);std::vector<float> l(48000,.2f),r=l;
        for(int at=0;at<48000;at+=127){if(at>=12000&&at<24000)d.setParameters(1,1,0,100,100,12,true);else if(at>=24000)d.setParameters(0,0,100,0,100,0,false);float* p[]{l.data()+at,r.data()+at};watch=true;d.process(p,2,std::min(127,48000-at));watch=false;}
        double maxStep=0;for(size_t i=1;i<l.size();++i)maxStep=std::max(maxStep,std::max(std::abs(static_cast<double>(l[i]-l[i-1])),std::abs(static_cast<double>(r[i]-r[i-1]))));check(maxStep<.01,"XY distance doubler gain and mono automation avoid hard steps",maxStep);bool exact=true;for(int i=36000;i<48000;++i)exact&=l[i]==.2f&&r[i]==.2f;check(exact,"parameter ramps reach exact neutral endpoints");
    }
    {
        gill::StageDSP d;d.prepare(fs,127,2);d.setParameters(0,0,100,0,100,6,false);auto l=original,r=original;process(d,l,&r);check(std::abs(20*std::log10(rms(l)/rms(original))-6)<1e-5,"OUTPUT calibration is accurate in dB");
        gill::StageDSP unsupported;unsupported.prepare(4000,127,1);l=original;process(unsupported,l);check(l==original,"unsupported sample rate safely passes through");
    }
    {
        gill::StageDSP d;d.prepare(fs,128,2);d.setParameters(.3f,.9f,100,100,100,0,false);auto l=signal(fs,227.5,3),r=l;const auto start=std::chrono::steady_clock::now();process(d,l,&r,128);const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::printf("BENCH stereo48k/128 full effects: %.6fs /3 audio seconds = %.3f percent realtime\n",elapsed,elapsed/3*100);
    }
    // DIRECT OFF must remove only the original vocal, never cancel stereo mid.
    for(double rate:{44100.,48000.,96000.,192000.})for(float mix:{0.f,37.f,100.f}){
        gill::StageDSP d;d.prepare(rate,127,2);d.setParameters(0,0,100,100,mix,0,false,false);
        std::vector<float> l(static_cast<size_t>(rate*.1)),r=l;l[0]=r[0]=1;process(d,l,&r);
        bool immediateSilent=true;for(int i=0;i<static_cast<int>(rate*.009);++i)immediateSilent&=l[i]==0&&r[i]==0;
        check(immediateSilent,"DIRECT OFF removes immediate arrival at every mix setting");
        const double energy=rms(l)+rms(r);
        check(mix==0?energy==0:energy>.001,"DIRECT OFF retains delayed doubles only when wet contribution is enabled",energy);
    }
    for(bool enabled:{false,true}){
        gill::StageDSP d;d.prepare(48000,127,2);d.setParameters(0,0,100,0,100,0,false,enabled);
        auto l=original,r=original;process(d,l,&r);
        check(enabled?l==original:rms(l)==0,"DIRECT controls the original path without requiring the doubler");
    }
    {
        auto run=[](int block){gill::StageDSP d;d.prepare(48000,block,2);d.setParameters(0,0,100,0,50,0,false);std::array<std::vector<float>,2> a{std::vector<float>(8000,.2f),std::vector<float>(8000,.2f)};
          for(int at=0;at<8000;){if(at==2000)d.setParameters(0,0,100,0,50,0,false,false);if(at==5000)d.setParameters(0,0,100,0,50,0,false,true);const int event=at<2000?2000:at<5000?5000:8000;const int n=std::min(block,event-at);float*p[]{a[0].data()+at,a[1].data()+at};watch=true;d.process(p,2,n);watch=false;at+=n;}return a;};
        auto a=run(127);check(a==run(1)&&a==run(2048),"DIRECT automation is sample-exact across block sizes");
        double jump=0;for(size_t i=1;i<a[0].size();++i)jump=std::max(jump,std::abs(double(a[0][i]-a[0][i-1])));
        check(jump<.0003,"DIRECT button fades over 20 ms without hard switching",jump);
        check(a[0][4000]==0&&a[0].back()==.2f,"DIRECT ramp reaches exact muted and original endpoints");
    }
    check(allocations==0,"no dynamic allocation in any measured process call",static_cast<double>(allocations));
    std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
