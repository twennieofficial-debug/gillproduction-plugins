#include "../Source/HarmonyDSP.h"
#include "../Source/HarmonyPresets.h"
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>
#include <cstring>

static bool watched=false;
static size_t allocations=0;
static unsigned futureReads=0;
void* operator new(size_t size) { if(watched)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
void operator delete[](void* p,size_t) noexcept { std::free(p); }
using namespace gill::tools;
constexpr double pi=3.14159265358979323846;
static int checks=0,failures=0;
void check(bool ok,const char* text,double a=0,double b=0) {
    ++checks;if(!ok)++failures;std::printf("%s %s %.9g %.9g\n",ok?"PASS":"FAIL",text,a,b);std::fflush(stdout);
}
HarmonyParameters solo(int interval,bool fixed=true) {
    HarmonyParameters p;p.direct=false;p.outputDb=0;p.voices[0]={true,fixed?1:0,interval,0,0};
    p.voices[1].enabled=p.voices[2].enabled=false;return p;
}
std::vector<float> signal(double fs,double hz,double seconds=.7,bool vowel=false) {
    std::vector<float> x(static_cast<size_t>(fs*seconds));
    for(size_t n=0;n<x.size();++n) {
        double value=0;
        if(vowel)for(int h=1;h<60&&h*hz<fs*.42;++h) {
            const double f=h*hz,envelope=.2+2*std::exp(-std::pow((f-500)/150,2))+1.5*std::exp(-std::pow((f-1500)/250,2));
            value+=envelope/std::sqrt(double(h))*std::sin(2*pi*f*n/fs);
        }
        else value=std::sin(2*pi*hz*n/fs);
        x[n]=static_cast<float>(value*(vowel?.045:.15));
    }
    return x;
}
struct Render {std::vector<float> left,right;int latency=0;float detected=0,confidence=0,pitch=0;};
Render render(const std::vector<float>& x,double fs,HarmonyParameters p,bool live,int block=127,bool stereo=false,bool inverted=false) {
    HarmonyDSP d;d.setParameters(p);d.setLiveMode(live);d.prepare(fs,block,stereo?2:1);
    Render r;r.left=x;r.right=x;r.latency=d.latencySamples();if(inverted)for(auto&v:r.right)v=-v;
    for(int n=0;n<static_cast<int>(x.size());n+=block) {
        float* data[]{r.left.data()+n,r.right.data()+n};watched=true;
        d.process(data,stereo?2:1,std::min(block,static_cast<int>(x.size())-n));watched=false;
    }
    r.detected=d.detectedHz();r.confidence=d.confidence();r.pitch=d.voiceSemitones(0);futureReads+=d.causalReadViolations();return r;
}
double rms(const std::vector<float>& x,size_t start=0) {
    double sum=0;for(size_t n=start;n<x.size();++n)sum+=x[n]*x[n];return std::sqrt(sum/std::max<size_t>(1,x.size()-start));
}
double pitch(const std::vector<float>& x,double fs,double expected) {
    const int begin=static_cast<int>(x.size()-fs*.3),length=static_cast<int>(fs*.2);
    const int lo=std::max(2,static_cast<int>(fs/expected*.85)),hi=static_cast<int>(fs/expected*1.15);
    std::vector<double> errors(hi+2);int best=lo;
    for(int lag=lo;lag<=hi;++lag) {
        for(int n=0;n<length;++n){const double d=x[begin+n]-x[begin+n+lag];errors[lag]+=d*d;}
        if(errors[lag]<errors[best])best=lag;
    }
    if(best==lo||best==hi)return 0;
    const double divisor=errors[best-1]-2*errors[best]+errors[best+1];
    return std::abs(divisor)>1e-20?fs/(best+.5*(errors[best-1]-errors[best+1])/divisor):0;
}
double maxError(const std::vector<float>& a,const std::vector<float>& b) {
    double error=0;for(size_t n=0;n<a.size();++n)error=std::max(error,std::abs(double(a[n])-b[n]));return error;
}
double spectralCentroid(const std::vector<float>& x,double hz,double low,double high) {
    double weight=0,sum=0;const int start=24000,length=24000;
    for(int harmonic=1;harmonic*hz<=high;++harmonic) {
        const double frequency=harmonic*hz;if(frequency<low)continue;
        std::complex<double> value{},phase{1,0},step=std::polar(1.0,-2*pi*frequency/48000);
        for(int n=0;n<length;++n){value+=double(x[start+n])*(.5-.5*std::cos(2*pi*n/(length-1)))*phase;phase*=step;}
        const double magnitude=std::abs(value);weight+=magnitude;sum+=frequency*magnitude;
    }
    return weight>1e-9?sum/weight:0;
}
int main() {
    // Labels and lookup are checked independently of the rendering code.
    check(std::strcmp(harmonyIntervalLabel(0,2,0),"THIRD UP")==0,"major third label uses two scale steps");
    check(std::strcmp(harmonyIntervalLabel(0,2,3),"+2 SCALE NOTES")==0,"pentatonic label does not promise a third");
    check(std::strcmp(harmonyIntervalLabel(0,5,3),"OCTAVE UP")==0,"pentatonic octave uses five scale steps");
    check(harmonyClampInterval(0,99,3)==5&&harmonyClampInterval(1,-99,0)==-12,"scale and fixed ranges are distinct");
    struct Expected {int key,scale,sourceMidi,steps,targetMidi;};
    for(auto e:std::array<Expected,9>{{{0,0,60,2,64},{0,0,64,2,67},{0,0,62,-2,59},{0,1,60,2,63},{0,2,68,2,72},{2,0,62,4,69},{0,3,60,2,65},{0,3,60,5,72},{0,3,70,-2,65}}}) {
        const float hz=static_cast<float>(440*std::exp2((e.sourceMidi-69)/12.0));
        const auto semitones=HarmonyDSP::scaleSemitones(hz,e.key,e.scale,e.steps);
        check(std::abs(semitones-(e.targetMidi-e.sourceMidi))<.001,"musical scale interval reaches explicit expected note",semitones,e.targetMidi-e.sourceMidi);
    }
    // All 25 fixed semitone settings are measured from actual sound in both modes.
    for(bool live:{true,false})for(int shift=-12;shift<=12;++shift) {
        const auto r=render(signal(48000,220),48000,solo(shift),live);
        const double expected=220*std::exp2(shift/12.0),found=pitch(r.left,48000,expected);
        const double cents=found>0?1200*std::log2(found/expected):999;
        check(std::abs(cents)<10,"all fixed intervals produce measured target pitch",cents,shift);
        check(rms(r.left,24000)>.005,"pitched voice has audible nonzero energy",rms(r.left,24000),live);
    }
    for(bool live:{true,false})for(double hz:{80.,120.,330.,740.,800.})for(int shift:{-12,-7,7,12}) {
        const auto r=render(signal(48000,hz,.75,true),48000,solo(shift),live);
        const double target=hz*std::exp2(shift/12.0),found=pitch(r.left,48000,target);
        const double cents=found>0?1200*std::log2(found/target):999;
        check(std::abs(cents)<10,"vocal-like low/high fundamental tracks wide interval",cents,hz);
    }
    for(bool live:{true,false})for(auto e:std::array<Expected,4>{{{0,0,60,2,64},{0,1,60,2,63},{2,0,62,4,69},{0,3,60,2,65}}}) {
        const double input=440*std::exp2((e.sourceMidi-69)/12.0),target=440*std::exp2((e.targetMidi-69)/12.0);
        auto p=solo(e.steps,false);p.key=e.key;p.scale=e.scale;
        const auto r=render(signal(48000,input,.8,true),48000,p,live);
        const double found=pitch(r.left,48000,target),cents=found>0?1200*std::log2(found/target):999;
        check(std::abs(cents)<10,"learned pitch actually renders the keyed harmony",cents,e.scale);
        check(r.confidence>.9&&std::abs(1200*std::log2(r.detected/input))<5,"detector reports actual source pitch and confidence",r.detected,r.confidence);
    }
    for(double fs:{8000.,44100.,48000.,96000.,192000.})for(bool live:{true,false}) {
        HarmonyDSP probe;probe.setLiveMode(live);probe.prepare(fs,127,1);const int latency=probe.latencySamples();
        std::vector<float> pulse(static_cast<size_t>(latency+300));pulse[23]=.5f;
        auto dry=solo(7);dry.direct=true;for(auto&v:dry.voices)v.enabled=false;
        const auto r=render(pulse,fs,dry,live);double error=0;
        for(size_t n=0;n<r.left.size();++n)error=std::max(error,std::abs(double(r.left[n])-(n==static_cast<size_t>(latency+23)?.5:0)));
        check(error==0,"direct path impulse matches declared latency exactly",error,latency);
        dry.direct=false;const auto muted=render(pulse,fs,dry,live);
        check(rms(muted.left)==0,"DIRECT OFF plus voices off has no hidden original",rms(muted.left));
        auto unison=solo(0);const auto same=render(pulse,fs,unison,live);
        check(same.left==r.left,"unison voice and direct share exact timing",fs,latency);
        auto bypass=solo(12);bypass.bypass=true;bypass.outputDb=-18;
        const auto passed=render(pulse,fs,bypass,live);
        check(passed.left==r.left,"bypass ignores processing and output trim with aligned original",fs);
    }
    const auto tone=signal(48000,220,.8,true);
    for(bool live:{true,false}) {
        auto p=solo(7);const auto one=render(tone,48000,p,live,1);
        for(int block:{16,64,127,512,2048})check(one.left==render(tone,48000,p,live,block).left,"sample-exact host block independence",block,live);
        const auto opposite=render(tone,48000,p,live,127,true,true);double error=0;
        for(size_t n=0;n<tone.size();++n)error=std::max(error,std::abs(double(opposite.left[n])+opposite.right[n]));
        check(error<2e-6,"opposite-polarity stereo retained and detected",error,opposite.detected);
        p.voices[0].pan=-100;const auto left=render(tone,48000,p,live,127,true);
        p.voices[0].pan=100;const auto right=render(tone,48000,p,live,127,true);
        check(rms(left.right)<1e-7&&rms(right.left)<1e-7,"hard pan places only the harmony on chosen side");
        check(std::abs(rms(left.left)-rms(right.right))<1e-6,"left/right pan has equal measured energy");
        p=solo(4);p.voices[0].levelDb=-6;const auto quiet=render(tone,48000,p,live);
        p.voices[0].levelDb=0;const auto loud=render(tone,48000,p,live);
        const double db=20*std::log10(rms(quiet.left,24000)/rms(loud.left,24000));check(std::abs(db+6)<.002,"voice level has actual calibrated dB gain",db);
        p=solo(2,false);std::vector<float> noise(48000);std::mt19937 random(883);std::normal_distribution<float> normal(0,.025f);
        for(auto&v:noise)v=normal(random);
        const auto unvoiced=render(noise,48000,p,live);
        check(rms(unvoiced.left,24000)<1e-5,"uncertain unvoiced input does not generate random harmonies",rms(unvoiced.left,24000));
        const auto silence=render(std::vector<float>(48000),48000,p,live);
        check(rms(silence.left)==0&&silence.detected==0,"silence produces no held note or detection");
        auto bad=tone;bad[123]=std::numeric_limits<float>::quiet_NaN();bad[200]=-std::numeric_limits<float>::infinity();
        p=solo(12);p.voices[0].pan=std::numeric_limits<float>::infinity();p.natural=std::numeric_limits<float>::quiet_NaN();
        const auto safe=render(bad,48000,p,live);check(std::all_of(safe.left.begin(),safe.left.end(),[](float v){return std::isfinite(v);}),"non-finite input and parameters remain finite");
    }
    {
        HarmonyDSP d;d.prepare(48000,127,2);d.setParameters(solo(7));auto input=tone;std::vector<float> second=tone;
        float* data[]{input.data(),second.data()};watched=true;d.process(data,2,127);d.setLiveMode(false);d.reset();d.setLiveMode(true);watched=false;
        check(d.latencySamples()>0&&d.maximumLatencySamples()>=d.latencySamples(),"both modes report their real nonzero latency",d.latencySamples(),d.maximumLatencySamples());
    }
    for(bool live:{true,false}) {
        for(float natural:{0.f,100.f})for(int interval:{-12,12}) {
            auto p=solo(interval);p.natural=natural;const auto y=render(signal(48000,100,1,true),48000,p,live);
            const double expected=100*std::exp2(interval/12.0),found=pitch(y.left,48000,expected);
            check(found>0&&std::abs(1200*std::log2(found/expected))<10,"NATURAL endpoints retain the requested octave pitch",found,expected);
        }
        auto p=solo(12);p.natural=0;const auto shifted=render(signal(48000,100,1,true),48000,p,live);
        p.natural=100;const auto preserved=render(signal(48000,100,1,true),48000,p,live);
        const double moved=spectralCentroid(shifted.left,200,250,1250),kept=spectralCentroid(preserved.left,200,250,1250);
        check(moved>kept*1.25,"NATURAL measurably preserves the vowel envelope instead of raising it an octave",moved,kept);
        const auto x=signal(48000,220,.8,true);auto all=solo(4);all.voices[1]={true,1,-5,-9,-40};all.voices[2]={true,1,12,-12,50};
        const auto stack=render(x,48000,all,live,127,true);std::vector<float> sumL(x.size()),sumR(x.size());
        for(int voice=0;voice<3;++voice){auto one=all;for(int j=0;j<3;++j)if(j!=voice)one.voices[j].enabled=false;const auto y=render(x,48000,one,live,127,true);for(size_t n=0;n<x.size();++n){sumL[n]+=y.left[n];sumR[n]+=y.right[n];}}
        check(maxError(stack.left,sumL)<2e-7&&maxError(stack.right,sumR)<2e-7,"three separately generated voices equal their independent measured sum");
        HarmonyDSP d;d.setLiveMode(live);d.prepare(48000,127,1);auto controls=solo(7);d.setParameters(controls);
        auto changing=signal(48000,220,1.5);double peak=0,jump=0;float previous=0;
        for(int n=0;n<static_cast<int>(changing.size());n+=64) {
            if(n%4800<64){controls.voices[0].interval=(n/4800)%2?-12:12;controls.natural=(n/4800)%3?100:0;watched=true;d.setParameters(controls);watched=false;}
            float* data[]{changing.data()+n};watched=true;d.process(data,1,std::min(64,static_cast<int>(changing.size())-n));watched=false;
        }
        for(float value:changing){peak=std::max(peak,std::abs(double(value)));jump=std::max(jump,std::abs(double(value)-previous));previous=value;}
        futureReads+=d.causalReadViolations();
        check(peak<.65&&jump<.18,"large pitch and NATURAL changes remain bounded without full-scale clicks",peak,jump);
    }
    for(double fs:{8000.,44100.,96000.,192000.})for(bool live:{true,false})for(int shift:{-12,12}) {
        const auto r=render(signal(fs,220,.75,true),fs,solo(shift),live);
        const double expected=220*std::exp2(shift/12.0),found=pitch(r.left,fs,expected);
        check(found>0&&std::abs(1200*std::log2(found/expected))<10,"octave audio accuracy across supported sample rates",found,fs);
    }
    for(const auto& preset:harmonyPresets()) {
        bool valid=preset.name&&std::strlen(preset.name)>3;
        for(const auto&v:preset.parameters.voices)valid=valid&&v.interval==harmonyClampInterval(v.intervalMode,v.interval,preset.parameters.scale)&&v.levelDb<=0&&v.pan>=-100&&v.pan<=100;
        check(valid,"six factory presets contain valid named controls");
    }
    check(futureReads==0,"all rendered grains read only samples already received",futureReads);
    check(allocations==0,"processing, reset and quality switches allocate no heap memory",double(allocations));
    std::printf("HARMONY: %d checks, %d failures, %zu audio allocations\n",checks,failures,allocations);
    return failures?1:0;
}
