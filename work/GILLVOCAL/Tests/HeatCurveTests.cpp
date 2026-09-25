#ifdef GILL_HEAT_BASELINE
#include "HeatFixtures/HeatDSP-v02-reference.h"
#else
#include "../Source/HeatDSP.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include <string>

// Deterministic engineering fixtures, not a recording/artist dataset.
// Samples are generated once per rate and reused unchanged for every 1% step.
namespace {
constexpr double pi=gill::heat_detail::pi;
int failures=0;
void check(bool ok,const char* what,double value=0){
    std::printf("%s %s %.8g\n",ok?"PASS":"FAIL",what,value);
    if(!ok)++failures;
}
struct Metric { double outDb=0,effect=0,colour=0,thd=0,peak=0; };
std::vector<float> tone(double fs,double hz,double amp) {
    std::vector<float> x(static_cast<size_t>(std::llround(fs*.20)));
    for(size_t i=0;i<x.size();++i)x[i]=float(amp*std::sin(2*pi*hz*i/fs));
    return x;
}
std::vector<float> vocal(double fs,bool syllables,double amplitude) {
    std::vector<float> x(static_cast<size_t>(std::llround(fs*.20)));
    double peak=0;unsigned noise=1987;
    for(size_t i=0;i<x.size();++i){
        const double t=i/fs;
        double v=0;
        if(syllables) {
            for(int h=1;h<=24&&125*h<fs*.4;++h){
                const double formant=1+.8*std::exp(-std::pow((125*h-750)/400.,2))
                                      +.55*std::exp(-std::pow((125*h-2500)/650.,2));
                v+=std::sin(2*pi*125*h*t+h*h*.713)*formant/std::pow(double(h),1.12);
            }
            noise=1664525u*noise+1013904223u;
            v+=.025*(double(noise)/4294967295.-.5);
            // Smooth syllable envelope, 19 ms rise / 60 ms decay, quiet troughs.
            const double syllable=std::fmod(t,.08)/.08;
            const double envelope=.09+.91*std::pow(std::sin(pi*std::min(1.,syllable/.24)),2);
            const double decay=std::exp(-3.3*std::max(0.,syllable-.24));
            const double attack=syllable<.24?envelope:1.;
            v*=attack*decay;
        } else {
            v=std::sin(2*pi*100*t)+.7*std::sin(2*pi*750*t+.3)
              +.6*std::sin(2*pi*3000*t+1.2);
            if(fs>20000)v+=.25*std::sin(2*pi*6750*t+.7);
        }
        x[i]=float(v);peak=std::max(peak,std::abs(v));
    }
    for(auto& v:x)v=float(v*amplitude/peak);
    return x;
}
double amplitude(const std::vector<float>& x,size_t start,size_t n,double f,double fs){
    const double angle=2*pi*f/fs,cr=std::cos(angle),ci=std::sin(angle);
    double re=1,im=0,sumR=0,sumI=0;
    for(size_t i=0;i<n;++i){
        sumR+=x[start+i]*re;sumI+=x[start+i]*im;
        const double next=re*cr-im*ci;im=im*cr+re*ci;re=next;
    }
    return 2*std::hypot(sumR,sumI)/n;
}
Metric analyse(const std::vector<float>& y,const std::vector<float>& x,double fs,double hz){
    Metric m;const size_t start=static_cast<size_t>(std::llround(fs*.12));
    const size_t count=static_cast<size_t>(std::llround(fs*.08));
    double inputEnergy=0,outputEnergy=0,errorEnergy=0,cross=0;
    for(size_t i=start;i<start+count;++i){
        const double a=x[i-24],b=y[i],e=b-a;
        inputEnergy+=a*a;outputEnergy+=b*b;errorEnergy+=e*e;cross+=a*b;
        m.peak=std::max(m.peak,std::abs(b));
    }
    m.outDb=10*std::log10(std::max(outputEnergy,1e-30)/inputEnergy);
    m.effect=std::sqrt(errorEnergy/inputEnergy);
    m.colour=std::sqrt(std::max(0.,outputEnergy-cross*cross/inputEnergy)/inputEnergy);
    if(hz>0){
        const double fundamental=amplitude(y,start,count,hz,fs);
        double harmonics=0;
        for(int h=2;h<=20&&hz*h<fs*.45;++h){
            const double a=amplitude(y,start,count,hz*h,fs);harmonics+=a*a;
        }
        m.thd=std::sqrt(harmonics)/std::max(fundamental,1e-15);
    }
    return m;
}
void curve(double fs,int mode,int band,const char* name,const std::vector<float>& input,double hz,FILE* csv) {
    auto dsp=std::make_unique<gill::HeatDSP>();
    std::array<Metric,101> metrics{};
    double worstStep=0,worstEffectDip=0,worstColourDip=0,worstThdDip=0;
    double minimumColourStep=1e9;
    bool finite=true;
    for(int percent=0;percent<=100;++percent) {
        const float drive=float(percent*.24);
        dsp->setParameters(band==0||band==3?drive:0,band==1||band==3?drive:0,
                           band==2||band==3?drive:0,mode,100,0);
        dsp->prepare(fs,127,1);
        auto output=input;
        // Alternate odd chunk lengths to exercise arbitrary-buffer processing.
        for(size_t offset=0;offset<output.size();) {
            const int count=int(std::min<size_t>(1+(offset%509),output.size()-offset));
            float* p[]{output.data()+offset};dsp->process(p,1,count);offset+=count;
        }
        auto& m=metrics[percent];m=analyse(output,input,fs,hz);
        finite=finite&&std::isfinite(m.outDb)&&std::isfinite(m.thd)&&m.peak<32;
        if(percent){
            worstStep=std::max(worstStep,std::abs(m.outDb-metrics[percent-1].outDb));
            worstEffectDip=std::max(worstEffectDip,metrics[percent-1].effect-m.effect);
            worstColourDip=std::max(worstColourDip,metrics[percent-1].colour-m.colour);
            worstThdDip=std::max(worstThdDip,metrics[percent-1].thd-m.thd);
            minimumColourStep=std::min(minimumColourStep,m.colour-metrics[percent-1].colour);
        }
        if(csv)std::fprintf(csv,"%.0f,%d,%d,%s,%d,%.10g,%.10g,%.10g,%.10g,%.10g\n",
            fs,mode,band,name,percent,m.outDb,m.effect,m.colour,m.thd,m.peak);
    }
    std::printf("CURVE %.0f mode %d band %d %s: levelStep %.6f dB; effectDip %.6f colourDip %.6f THDdip %.6f; colour20/50/70/100 %.5f %.5f %.5f %.5f; THD20/50/70/100 %.5f %.5f %.5f %.5f; level0/20/50/70/100 %.3f %.3f %.3f %.3f %.3f\n",
        fs,mode,band,name,worstStep,worstEffectDip,worstColourDip,worstThdDip,
        metrics[20].colour,metrics[50].colour,metrics[70].colour,metrics[100].colour,
        metrics[20].thd,metrics[50].thd,metrics[70].thd,metrics[100].thd,
        metrics[0].outDb,metrics[20].outDb,metrics[50].outDb,metrics[70].outDb,metrics[100].outDb);
    check(finite,"all 101 rendered positions finite");
    check(worstStep<.35,"no adjacent 1-percent level jump exceeds 0.35 dB",worstStep);
    check(worstEffectDip<.002,"added-effect RMS does not reverse by >0.2% input per step",worstEffectDip);
    check(minimumColourStep>0,"every 1-percent step increases gain-independent coloration",minimumColourStep);
    if(hz>0)check(worstThdDip<1e-5,"tone THD is monotonic through all 101 positions",worstThdDip);
    check(metrics[70].colour>metrics[20].colour*1.5,"20-to-70 percent increases gain-independent coloration");
    // Harmonics/noise content below 0.5% input can be inaudible; report it honestly.
    check(metrics[20].colour>.004,"20 percent creates measurable coloration beyond gain change",metrics[20].colour);
    std::fflush(stdout);
}
}
int main(int argc,char** argv) {
    const bool quick=argc>1&&std::string(argv[1])=="quick";
    const auto start=std::chrono::steady_clock::now();
#ifdef GILL_HEAT_BASELINE
    const char* file="HeatCurves-v02.csv";
#else
    const char* file=quick?"HeatCurves-v03-quick.csv":"HeatCurves-v03.csv";
#endif
    FILE* csv=std::fopen(file,"w");
    if(!csv){std::puts("FAIL cannot write curve CSV");return 1;}
    std::fprintf(csv,"rate,mode,band,fixture,percent,output_gain_db,effect_rms_relative,colour_rms_relative,thd,peak\n");
    for(double fs:{22050.,44100.,48000.,88200.,96000.,192000.}) {
        if(quick&&fs!=48000)continue;
        const auto multi=vocal(fs,false,.25),voice=vocal(fs,true,.25);
        for(int mode=0;mode<3;++mode){
            for(int band=0;band<3;++band){
                curve(fs,mode,band,"TONE",tone(fs,band==0?100.:band==1?750.:3000.,.05),
                      band==0?100.:band==1?750.:3000.,csv);
                curve(fs,mode,band,"VOCAL",voice,0,csv);
            }
            curve(fs,mode,3,"MULTITONE",multi,0,csv);
            curve(fs,mode,3,"VOCAL",voice,0,csv);
        }
    }
    std::fclose(csv);
    std::printf("RESULT %d failures; %.3f seconds; %s\n",failures,
        std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),file);
    return failures?1:0;
}
