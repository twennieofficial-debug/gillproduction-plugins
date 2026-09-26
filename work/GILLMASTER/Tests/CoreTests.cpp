#include "../Source/MasterDSP.h"
#include "../Source/WeightDSP.h"
#include "../Source/DeliverMeter.h"
#include "TestSupport.h"
using namespace gill::master;

template<class Engine>void neutral(typename Engine::Parameters p,const char*label){
    bool exact=true,noalloc=true,reset=true;
    for(double fs:test::rates){Engine a,b;a.parameters(p);b.parameters(p);a.prepare(fs);b.prepare(fs);const auto before=allocations.load();
        for(int i=0;i<5000;++i){Stereo x{.1*std::sin(i*.19),.23*std::cos(i*.13)};const auto y=a.sample(x);exact=exact&&y==x;}
        noalloc=noalloc&&before==allocations.load();a.reset();for(int i=0;i<4000;++i){Stereo x{.1*std::sin(i*.13),.2*std::cos(i*.17)};reset=reset&&a.sample(x)==b.sample(x);}}
    test::check(exact,label);test::check(noalloc&&reset,"six rates: reset repeatable, sample processing allocates nothing");
}
void neutralTests(){LowDSP::Parameters low;low.amount=0;neutral<LowDSP>(low,"LOW amount zero / width 100 = exact dry");GlueDSP::Parameters glue;glue.amount=0;neutral<GlueDSP>(glue,"GLUE amount zero = exact dry");neutral<WidthDSP>({},"WIDTH 100/100/100 = exact stereo dry");neutral<PunchDSP>({},"PUNCH zero attack/sustain = exact dry");}
void dynamics(){
    for(double fs:test::rates){LowDSP low;LowDSP::Parameters p;p.amount=100;p.threshold=-24;p.protect=0;low.parameters(p);low.prepare(fs);double in=0,out=0;
        for(int i=0;i<int(fs);++i){const double x=.5*std::sin(2*test::pi*30*i/fs);const auto y=low.sample({x,x});if(i>fs*.5){in+=x*x;out+=y[0]*y[0];}}
        test::check(10*std::log10(out/in)<-6,"LOW reduces sustained sub bass by more than 6 dB",10*std::log10(out/in));
        for(int mode=0;mode<3;++mode){GlueDSP glue;glue.parameters({80,90,mode});glue.prepare(fs);double ratioError=0;
            for(int i=0;i<int(fs);++i){const double x=.6*std::sin(2*test::pi*1000*i/fs);auto y=glue.sample({x,.4*x});ratioError=std::max(ratioError,std::abs(y[1]-.4*y[0]));}
            test::check(glue.reductionDb()>3&&glue.reductionDb()<24&&ratioError<1e-14,"GLUE audible linked stereo compression in each character",glue.reductionDb());}
        WidthDSP width;WidthDSP::Parameters w;w.width={0,0,0};width.parameters(w);width.prepare(fs);bool mono=true;for(int i=0;i<5000;++i){auto y=width.sample({std::sin(i*.3),std::cos(i*.2)});mono=mono&&y[0]==y[1];}test::check(mono,"WIDTH all zero produces identical left/right");
        PunchDSP punch;PunchDSP::Parameters pp;pp.attack={100,100,100};punch.parameters(pp);punch.prepare(fs);double peakGain=0;for(int i=0;i<int(fs*.2);++i){double x=i>int(fs*.1)?.2*std::sin(i*.19):0;auto y=punch.sample({x,x});if(std::abs(x)>.1)peakGain=std::max(peakGain,std::abs(y[0]/x));}test::check(peakGain>1.15&&peakGain<3,"PUNCH increases onsets with bounded gain",peakGain);
    }
}
void weight(){
    bool zero=true,finite=true,noalloc=true;double first=0,last=0,worstDown=0;
    for(double fs:test::rates)for(bool pro:{false,true}){WeightDSP e;WeightDSP::Parameters p;p.amount=0;e.parameters(p);e.setPro(pro);e.prepare(fs);auto original=test::sine(fs,8192,733,.3);for(int i=0;i<8192;++i){const auto y=e.sample({original[i],original[i]});const double wanted=i>=e.latencySamples()?original[i-e.latencySamples()]:0;zero=zero&&y[0]==wanted&&y[1]==wanted;}
        p.amount=100;e.parameters(p);e.reset();const auto before=allocations.load();for(int i=0;i<30000;++i){double x=.5*std::sin(2*test::pi*60*i/fs);if(i==100)x=std::numeric_limits<double>::quiet_NaN();auto y=e.sample({x,x});finite=finite&&std::isfinite(y[0])&&std::abs(y[0])<4;}noalloc=noalloc&&before==allocations.load();}
    test::check(zero,"WEIGHT zero is exact dry, including correct PRO 32-sample delay");test::check(finite&&noalloc,"WEIGHT both modes stable at six rates without audio allocations");
    for(int amount=0;amount<=100;++amount){WeightDSP e;e.parameters({double(amount),110,0});e.prepare(48000);std::vector<float>y(48000);for(int i=0;i<48000;++i){double x=.5*std::sin(2*test::pi*60*i/48000);y[i]=float(e.sample({x,x})[0]);}const double third=test::amplitude(y,24000,180,48000);if(amount==0)first=third;else worstDown=std::min(worstDown,third-last);last=third;}
    test::check(first<1e-6&&last>.01,"WEIGHT creates measured third harmonics from a clean 60-Hz tone",last);test::check(worstDown> -1e-7,"WEIGHT third harmonic increases continuously across all 101 knob positions",worstDown);
}
void delivery(){
    for(double fs:test::rates)for(int channels:{1,2}){DeliverMeter meter;meter.prepare(fs,channels);meter.start();meter.beginBlock(false,true,true);for(int i=0;i<1000;++i)meter.sample({.1,.1},-60);meter.publish();test::check(meter.snapshot().duration==0,"DELIVER armed transport waits for playback");meter.beginBlock(true,true,true);
        for(int i=0;i<int(fs*2);++i){const double x=i<fs*.2||i>=fs*1.8?0:.1*std::sin(2*test::pi*1000*i/fs);meter.sample({x,x},-60);}meter.publish();auto s=meter.snapshot();
        test::check(std::abs(s.duration-2)<1e-6&&std::abs(s.leading-.2)<.002&&std::abs(s.trailing-.2)<.002,"DELIVER measures duration and leading/trailing silence",s.duration);
        meter.beginBlock(false,true,true);meter.publish();test::check(!meter.snapshot().running,"DELIVER ends measurement when transport stops");
        meter.start();meter.beginBlock(true,true,false);for(int i=0;i<int(fs*2);++i){const double x=.1*std::sin(2*test::pi*1000*i/fs);meter.sample({x,x},-60);}meter.publish();s=meter.snapshot();const double target=channels==2?-20:-23.0103;
        test::check(std::abs(s.integrated-target)<.10,"BS.1770 1-kHz calibrated sine, mono/stereo",s.integrated);
        meter.sample({1.1,-1.1},-60);meter.publish();test::check(meter.snapshot().clipped==1&&meter.snapshot().peakDb>0,"DELIVER detects over-full-scale samples without altering input");
    }
}
void range(){
    const std::vector<std::vector<double>> fixtures{{-20,-30},{-20,-15},{-40,-20},{-50,-35,-20,-35,-50}};
    const double expected[]{10,5,20,15};int at=0;
    for(const auto&levels:fixtures){auto meter=std::make_unique<gillnext::Loudness>();meter->prepare(48000,2);for(auto db:levels)for(int i=0;i<960000;++i){float x=float(std::pow(10.,db/20)*std::sin(2*test::pi*1000*i/48000));meter->sample({x,x},2);}test::check(std::abs(meter->loudnessRange-expected[at++])<=1,"EBU Tech 3342 synthetic LRA requirement",meter->loudnessRange);}
}
int main(){neutralTests();dynamics();weight();delivery();range();return test::result();}
