#include "../Source/OutputPeakMeter.h"
#include "TestSupport.h"
#include <random>

using Meter=gillnext::OutputPeakMeter;
using Stereo=std::array<std::vector<float>,2>;
static double db(double x){return 20*std::log10(std::max(x,1e-30));}
static std::vector<float> programme(int samples,double frequency,double phase,double amplitude=1){
    std::vector<float> out(samples);for(int i=0;i<samples;++i){const double fade=std::min({1.,i/256.,(samples-1-i)/256.});out[i]=float(amplitude*std::max(0.,fade)*std::sin(2*test::pi*frequency*i+phase));}return out;
}
static void render(Meter&m,const Stereo&audio,int block,int channels=2){for(size_t i=0;i<audio[0].size();i+=block){const float*p[]{audio[0].data()+i,audio[1].data()+i};m.process(p,channels,int(std::min<size_t>(block,audio[0].size()-i)));}for(int i=0;i<70;++i)m.process(0.f,0.f,channels);}
// Independent offline reconstruction: 16x, 129 taps, Kaiser beta=8.6, centred
// indexing through the source vectors. It does not use the production phase
// coefficients/history, window, tap count or circular-buffer implementation.
static double i0(double x){double sum=1,term=1;for(int k=1;k<40;++k){term*=x*x/(4*k*k);sum+=term;if(term<sum*1e-16)break;}return sum;}
static double reference16(const std::vector<float>&signal){
    constexpr int oversample=16,radius=64;std::array<std::array<double,129>,16> coefficients{};const double normal=i0(8.6);
    for(int phase=0;phase<oversample;++phase){double sum=0;for(int tap=-radius;tap<=radius;++tap){const double t=tap-phase/double(oversample);const double sinc=std::abs(t)<1e-12?1:std::sin(test::pi*t)/(test::pi*t);const double w=std::abs(t)<=radius?i0(8.6*std::sqrt(std::max(0.,1-t*t/(radius*radius))))/normal:0;coefficients[phase][tap+radius]=sinc*w;sum+=sinc*w;}for(auto&c:coefficients[phase])c/=sum;}
    double maximum=test::peak(signal);for(int n=0;n<int(signal.size());++n)for(int phase=1;phase<oversample;++phase){double sample=0;for(int tap=-radius;tap<=radius;++tap){const int index=n+tap;if(index>=0&&index<int(signal.size()))sample+=signal[index]*coefficients[phase][tap+radius];}maximum=std::max(maximum,std::abs(sample));}return maximum;
}

int main(){
    double worst=0;bool calibrated=true,independent=true,invariant=true,noalloc=true;
    for(double fs:{8000.,22050.,32000.,44100.,48000.,88200.,96000.,192000.,384000.}){
        Stereo a{{programme(3072,.25,test::pi*.25),programme(3072,.25,test::pi*.25,.4)}};
        Meter meter;meter.prepare(fs,2);render(meter,a,127);const double samplePeak=test::peak(a[0]);
        test::check(samplePeak<.708&&meter.maximumPeak()>.999&&meter.maximumPeak()<1.003,"fs/4 phase-pi/4 intersample peak is detected",meter.maximumPeak()/samplePeak);
        Meter mono;mono.prepare(fs,1);Stereo swapped{{a[1],a[0]}};render(mono,swapped,1,1);test::check(std::abs(mono.maximumPeak()-.4)<.002,"mono ignores unused right channel",mono.maximumPeak());
        Meter left,right;left.prepare(fs,2);right.prepare(fs,2);render(left,a,1);render(right,a,511);invariant=invariant&&left.maximumPeak()==right.maximumPeak()&&left.peak()==right.peak();
        const auto baseline=allocations.load();std::array<float,257> l{},r{};const float*ptr[]{l.data(),r.data()};for(int iteration=0;iteration<40;++iteration){for(int i=0;i<257;++i){l[i]=float(std::sin((i+iteration*257)*.04));r[i]=-.3f*l[i];}meter.process(ptr,2,257);}noalloc=noalloc&&allocations.load()==baseline;
        meter.reset();test::check(meter.peak()==0&&meter.maximumPeak()==0,"reset clears current, maximum and reconstruction history");
        for(int i=0;i<3072;++i){const float x=float(.5*std::min(1.,i/256.));meter.process(x,x,2);}calibrated=calibrated&&std::abs(meter.peak()-.5)<.0005;
    }
    test::check(invariant,"sample and block APIs are bit-exact across nine rates and block sizes");
    test::check(calibrated,"DC reconstruction is calibrated",0);
    for(double f:{.01,.071,.125,.25,.33,.40,.45}){
        Stereo a{{programme(3072,f,.718,.83),programme(3072,f,.145,.41)}};Meter m;m.prepare(48000,2);render(m,a,193);const double ref=std::max(reference16(a[0]),reference16(a[1]));const double error=db(m.maximumPeak()/ref);worst=std::max(worst,std::abs(error));independent=independent&&std::abs(error)<.10;test::check(std::abs(error)<.10,"8x estimate vs independent 16x Kaiser reconstruction, sine",error);
    }
    Stereo multi{{std::vector<float>(4096),std::vector<float>(4096)}};for(int i=0;i<4096;++i){const double ramp=std::max(0.,std::min({1.,i/256.,(4095-i)/256.}));multi[0][i]=float(ramp*(.4*std::sin(i*.33)+.3*std::cos(i*.87)+.2*std::sin(i*2.31)));multi[1][i]=float(ramp*(.5*std::cos(i*.61)+.15*std::sin(i*2.09)));}Meter m;m.prepare(48000,2);render(m,multi,61);const double ref=std::max(reference16(multi[0]),reference16(multi[1]));const double error=db(m.maximumPeak()/ref);test::check(std::abs(error)<.12,"independent 16x reconstruction, unequal stereo multitone",error);
    Meter decay;decay.prepare(48000,2);decay.process(1,0,2);for(int i=0;i<128;++i)decay.process(0.f,0.f,2);const double start=decay.peak(),maximum=decay.maximumPeak();for(int i=0;i<24000;++i)decay.process(0.f,0.f,2);test::check(std::abs(decay.peak()/start-std::exp(-1))<2e-6,"display peak has exactly a 0.5 second amplitude time constant",decay.peak()/start);test::check(decay.maximumPeak()==maximum,"maximum peak does not decay");
    Meter guard;guard.prepare(48000,2);guard.process(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),2);test::check(guard.peak()==0&&guard.maximumPeak()==0,"NaN and infinity cannot poison meters");guard.process(2.5f,-3.25f,2);test::check(guard.peak()>=3.25f,"above-full-scale output is measured without clipping",guard.peak());guard.process(std::numeric_limits<float>::max(),0,2);test::check(std::isfinite(guard.peak())&&std::isfinite(guard.maximumPeak()),"extreme finite audio keeps finite meter output");
    const auto before=multi;Meter immutable;immutable.prepare(48000,2);render(immutable,multi,127);test::check(multi==before,"read-only meter leaves both audio channels sample-exact");test::check(noalloc,"zero C++ heap allocations during every measured audio process call");test::check(independent,"all sine reconstruction comparisons below 0.10 dB",worst);return test::result();
}
