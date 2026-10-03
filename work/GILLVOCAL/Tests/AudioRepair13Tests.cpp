#include "../Source/TuneDSP.h"
#include "../../GILLRESTORATION/Source/RestorationDSP.h"
#include "../../GILLNEXT/Source/CleanDSP.h"
#include "../../GILLDEREVERB/Source/DereverbDSP.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846,fs=48000;
int checks=0,failures=0;
void check(bool good,const char* name,double metric=0){++checks;if(!good)++failures;std::printf("%s %s %.9g\n",good?"PASS":"FAIL",name,metric);}
std::vector<float> vowel(double seconds,double fundamental=227){std::vector<float>x(size_t(fs*seconds));for(size_t i=0;i<x.size();++i){double v=0;for(int h=1;h<=20;++h)v+=.2/h*std::sin(2*pi*h*fundamental*i/fs);x[i]=float(v);}return x;}
template<class D>std::vector<float> render(D& d,const std::vector<float>&x,int block=127){auto y=x;for(int at=0;at<int(y.size());at+=block){float*p[]{y.data()+at};d.process(p,1,std::min(block,int(y.size())-at));}return y;}
double snr(const std::vector<float>&y,const std::vector<float>&x,int latency,int start=24000){double e=0,err=0;for(int i=start;i+latency<int(y.size());++i){e+=double(x[i])*x[i];const double d=y[i+latency]-x[i];err+=d*d;}return 10*std::log10(std::max(e,1e-30)/std::max(err,1e-30));}
double maximum(const std::vector<float>&x){double peak=0;for(float v:x)peak=std::max(peak,std::abs(double(v)));return peak;}
double amplitude(const std::vector<float>&x,double hz){std::complex<double>sum{};const int n=24000,start=int(x.size())-n;for(int i=0;i<n;++i)sum+=double(x[start+i])*std::polar(1.,-2*pi*hz*i/fs);return 2*std::abs(sum)/n;}
double centroid(const std::vector<float>&x){double energy=0,weighted=0;for(int h=2;h<=70;++h){double a=amplitude(x,100*h);energy+=a*a;weighted+=100*h*a*a;}return weighted/std::max(energy,1e-20);}
void liveAndNoise(){
    for(double rate:{8000.,44100.,48000.,96000.,192000.}){
        std::mt19937 rng(47);std::uniform_real_distribution<float> noise(-.2,.2);std::vector<float>x(8192);for(auto&v:x)v=noise(rng);x[0]=.8f;
        for(float formant:{-12.f,0.f,12.f}){gill::TuneDSP d;d.prepare(rate,127,1);d.setParameters(7,2,0,0,100);d.setFormant(formant);d.setLiveMode(true);
            const auto y=render(d,x);check(d.latencySamples()==0&&x==y,"LIVE Tune: first sample and every sample exact, all rates and formants");}
    }
    // Low-passed pitch detection alone can mistake a faint hum under hiss for a
    // reliable voice. Broadband aperiodicity must leave the audible hiss alone.
    std::mt19937 rng(271);std::normal_distribution<float> noise(0,.055f);std::vector<float>x(96000);
    for(int i=0;i<96000;++i)x[i]=float(.08*std::sin(2*pi*227*i/fs))+noise(rng);
    gill::TuneDSP d;d.prepare(fs,127,1);d.setParameters(9,2,0,0,100);const auto y=render(d,x);
    const double untouched=snr(y,x,d.latencySamples());check(untouched>80,"retune0: uncertain periodic hum plus hiss is not pitch-modulated",untouched);
    auto breath=x;for(int i=0;i<96000;++i)breath[i]=noise(rng);gill::TuneDSP b;b.prepare(fs,128,1);b.setParameters(0,0,0,0,100);const auto by=render(b,breath);
    check(snr(by,breath,b.latencySamples())>100,"retune0: aperiodic breath/noise retains exact aligned waveform");
}
void formants(){
    std::vector<float>x(96000);for(int i=0;i<96000;++i){double v=0;for(int h=1;h<=90;++h){double f=h*100.,e=.01+1.2*std::exp(-std::pow((f-700)/200,2))+.8*std::exp(-std::pow((f-1500)/300,2));v+=e*.1*std::sin(2*pi*f*i/fs);}x[i]=float(v);}
    std::array<std::vector<float>,3>result;
    for(int q=0;q<3;++q){gill::FormantDSP d;d.prepare(fs,1);d.setSemitones(float((q-1)*6));result[q]=render(d,x);if(q==1)check(snr(result[q],x,d.latencySamples())>150,"FORMANT zero is exact delayed bypass");
        // A periodic output must retain its 100-Hz fundamental period.
        double e=0,diff=0;for(int i=48000;i<95000;++i){e+=double(result[q][i])*result[q][i];double delta=result[q][i]-result[q][i-480];diff+=delta*delta;}
        check(10*std::log10(std::max(diff,1e-30)/e)<-55,"FORMANT changes envelope, not harmonic pitch",10*std::log10(std::max(diff,1e-30)/e));}
    const double down=centroid(result[0]),neutral=centroid(result[1]),up=centroid(result[2]);
    std::printf("FORMANT measured centroids down %.6f neutral %.6f up %.6f Hz\n",down,neutral,up);
    check(down<neutral*.90&&up>neutral*1.10,"FORMANT -6/+6 move independently measured envelope centroid in requested direction");
    gill::TuneDSP d;d.prepare(fs,127,1);d.setParameters(0,0,0,0,0);d.setFormant(12);auto dry=render(d,x);check(snr(dry,x,d.latencySamples())>150,"MIX0 bypasses pitch and formant together with truthful PDC");
}
void restoration(){
    std::mt19937 rng(77);std::normal_distribution<float> random(0,1);std::vector<float>noise(96000);for(auto&v:noise)v=random(rng)*.08f;
    auto voice=vowel(2);for(int i=0;i<int(voice.size());++i)voice[i]*=float(.5+.5*std::sin(2*pi*3*i/fs));
    for(bool crackle:{false,true})for(bool live:{false,true})for(double amount:{.5,1.}){
        gillrestoration::RestorationEngine d;d.prepare(fs,crackle?gillrestoration::Mode::Decrackle:gillrestoration::Mode::Declick);d.setAmount(amount);d.setLiveMode(live);
        auto y=render(d,noise);check(maximum(y)<=maximum(noise)+1e-6,"click restoration must not synthesize a peak above a noise fixture's input bounds",maximum(y));
        check(snr(y,noise,d.getLatencySamples())>30,"diffuse uncorrupted noise avoids false interpolation damage",snr(y,noise,d.getLatencySamples()));
        d.reset();y=render(d,voice);check(snr(y,voice,d.getLatencySamples())>35,"periodic glottal edges survive 50 and100percent cleanup",snr(y,voice,d.getLatencySamples()));
        auto corrupt=voice;for(int i=12000;i<88000;i+=5300)corrupt[i]+=.6f;d.reset();y=render(d,corrupt);
        const double improvement=snr(y,voice,d.getLatencySamples())-snr(corrupt,voice,0);
        check(improvement>2,"speech protection retains actual isolated click reduction",improvement);
        if(live)check(d.getLatencySamples()==0,"LIVE click/crackle remains causal with zero sample delay");
    }
}
void sustainedClean(){
    const auto x=vowel(20,220);
    for(bool live:{false,true}){gillnext::CleanDSP d;d.prepare(fs,127,1);d.setParameters({100,0,0,0});d.setLiveMode(live);const auto y=render(d,x);const int delay=d.latencySamples();double a=0,b=0;
        for(int i=18*48000;i<19*48000;++i){a+=double(x[i])*x[i];b+=double(y[i+delay])*y[i+delay];}
        const double level=10*std::log10(b/a);check(level>-.4&&level<.2,"CLEAN: sustained voice is not relearned as noise after20seconds",level);
        if(live)check(delay==0,"LIVE CLEAN reports actual zero algorithmic delay");}
}
}
int main(){liveAndNoise();formants();restoration();sustainedClean();std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}
