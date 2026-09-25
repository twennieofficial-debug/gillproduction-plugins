#include "../Source/QuadDSP.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <new>
#include <vector>

namespace {bool guardAllocation=false;std::uint64_t allocations=0;}
void* operator new(std::size_t n){if(guardAllocation)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

namespace {
using gilldyn::QuadDSP;using gilldyn::QuadParameters;
constexpr double pi=3.14159265358979323846;
int checks=0,failures=0;std::uint64_t processed=0;double maxResponseError=0.,maxDisplayError=0.,maxAutomationCurvature=0.,realtimePercent=0.;
void check(bool good,const char* label,double value=0.){++checks;if(!good)++failures;std::printf("%s %-77s %.9g\n",good?"PASS":"FAIL",label,value);}
std::uint32_t next(std::uint32_t& seed){seed=1664525U*seed+1013904223U;return seed;}
float random(std::uint32_t& seed){return static_cast<float>((next(seed)>>8)*(2./16777215.)-1.);}
void process(QuadDSP& dsp,float** data,int n,int ch){guardAllocation=true;dsp.process(data,n,ch);guardAllocation=false;processed+=static_cast<std::uint64_t>(n*ch);}
void blocked(QuadDSP& dsp,std::vector<float>& l,std::vector<float>& r,int block,int ch=2){std::uint32_t seed=9182;int at=0;while(at<static_cast<int>(l.size())){const int n=std::min(block>0?block:1+static_cast<int>(next(seed)%521),static_cast<int>(l.size())-at);float* data[]{l.data()+at,r.data()+at};process(dsp,data,n,ch);at+=n;}}
std::complex<double> measured(const std::vector<float>& x,double fs,double hz,double amplitude){double ss=0,cc=0,sc=0,ys=0,yc=0;for(size_t i=x.size()*2/3;i<x.size();++i){const double a=2*pi*hz*i/fs,s=std::sin(a),c=std::cos(a);ss+=s*s;cc+=c*c;sc+=s*c;ys+=x[i]*s;yc+=x[i]*c;}const double det=ss*cc-sc*sc;return {(ys*cc-yc*sc)/(det*amplitude),(yc*ss-ys*sc)/(det*amplitude)};}
struct Tone {std::complex<double> transfer{};std::array<float,4> reductions{},gains{};double display=0;};
Tone tone(QuadParameters p,double fs,double hz,double amplitude=.2){QuadDSP e;e.prepare(fs,512,2);e.setParameters(p);const int n=static_cast<int>(fs*.6);std::vector<float> l(static_cast<size_t>(n)),r(l.size());for(int i=0;i<n;++i){l[static_cast<size_t>(i)]=static_cast<float>(amplitude*std::sin(2*pi*hz*i/fs));r[static_cast<size_t>(i)]=l[static_cast<size_t>(i)]*.37f;}blocked(e,l,r,-1);return {measured(l,fs,hz,amplitude),e.bandReductionDb(),e.bandGainDb(),e.responseDb(hz)};}
double db(double g){return 20*std::log10(std::max(1.e-15,g));}

void neutral(){bool exact=true,delay=true,blocks=true;for(double fs:{8000.,32000.,44100.,48000.,96000.,192000.})for(int ch:{1,2}){
    std::vector<float> input(17000),right(input.size());std::uint32_t seed=19;for(size_t i=0;i<input.size();++i){input[i]=random(seed)*.8f;right[i]=input[i]*-.37f;}
    for(int block:{1,17,127,512,4096,-1}){QuadDSP e;e.prepare(fs,block,ch);e.setParameters({});auto l=input,r=right;blocked(e,l,r,block,ch);exact=exact&&l==input&&(ch==1||r==right);delay=delay&&e.latencySamples()==0;}
    QuadParameters p;p.bands[0].rangeDb=-20;p.bands[2].rangeDb=-12;p.bands[1].gainDb=4;
    QuadDSP a,b;a.prepare(fs,1,ch);b.prepare(fs,4096,ch);a.setParameters(p);b.setParameters(p);auto la=input,ra=right,lb=input,rb=right;blocked(a,la,ra,1,ch);blocked(b,lb,rb,-1,ch);blocks=blocks&&la==lb&&ra==rb;
    QuadDSP impulse;impulse.prepare(fs,32,ch);std::vector<float> l(100),r(100);l[0]=.25f;float* data[]{l.data(),r.data()};process(impulse,data,100,ch);delay=delay&&l[0]==.25f;for(size_t i=1;i<l.size();++i)delay=delay&&l[i]==0;
}check(exact,"Neutral random mono/stereo is bit-exact at six rates and six block patterns");check(delay,"Neutral impulse occurs exactly at sample zero, equal to reported latency");check(blocks,"Active dynamics render bit-identically at block1 and varying blocks");}

std::complex<double> analytic(QuadParameters p,double fs,double hz){
    // Independent analog-prototype calculation with a bilinear frequency map;
    // intentionally does not use the engine coefficients or responseDb().
    QuadDSP clamps;clamps.prepare(fs,128,2);clamps.setParameters(p);const auto cross=clamps.crossoverHz();
    const double w=std::tan(pi*hz/fs);std::array<std::complex<double>,3> low{};
    for(size_t i=0;i<3;++i)low[i]=1./std::complex<double>(1.,w/std::tan(pi*cross[i]/fs));
    const std::array<std::complex<double>,4> bands{{low[0],low[1]-low[0],low[2]-low[1],1.-low[2]}};
    std::complex<double> h=0.;bool anySolo=false;for(const auto& b:p.bands)anySolo=anySolo||b.solo;
    for(size_t i=0;i<4;++i){const double g=anySolo&&!p.bands[i].solo?0.:p.bands[i].bypass?1.:std::pow(10.,p.bands[i].gainDb/20.);h+=g*bands[i];}return h*std::pow(10.,p.outputDb/20.);
}
void frequencyResponse(){bool okay=true;int cases=0;for(double fs:{8000.,32000.,44100.,48000.,96000.,192000.}){
    QuadParameters p;p.bands[0].gainDb=-8;p.bands[1].gainDb=4;p.bands[2].gainDb=-6;p.bands[3].gainDb=8;p.outputDb=-2;
    for(double hz:{35.,100.,350.,600.,1400.,3000.,6000.,10000.,17000.})if(hz<fs*.46){const auto t=tone(p,fs,hz);const auto h=analytic(p,fs,hz);const double error=std::abs(t.transfer-h),displayError=std::abs(t.display-db(std::abs(t.transfer)));maxResponseError=std::max(maxResponseError,error);maxDisplayError=std::max(maxDisplayError,displayError);okay=okay&&error<3.e-6&&displayError<.0001;++cases;}
}check(okay,"Measured complex response agrees with independent bilinear prototype",maxResponseError);check(maxDisplayError<.0001,"Displayed response agrees with measured amplitude, dB",maxDisplayError);std::printf("METRIC frequency response configurations %d\n",cases);}

void selectivity(){std::array<std::array<double,4>,4> loss{};for(size_t band=0;band<4;++band){QuadParameters p;p.bands[band].thresholdDb=-36;p.bands[band].rangeDb=-24;p.bands[band].attackMs=5;p.bands[band].releaseMs=80;
    for(size_t f=0;f<4;++f){const double hz=std::array<double,4>{{100,600,3000,10000}}[f];loss[band][f]=-db(std::abs(tone(p,48000,hz,.3).transfer));}
}for(size_t f=0;f<4;++f){double neighbor=-100;for(size_t b=0;b<4;++b)if(b!=f)neighbor=std::max(neighbor,loss[b][f]);const std::string label="Target band dominates attenuation at "+std::to_string(std::array<int,4>{{100,600,3000,10000}}[f])+" Hz";check(loss[f][f]>neighbor+1.&&loss[f][f]>3.,label.c_str(),loss[f][f]-neighbor);}
for(size_t b=0;b<4;++b)std::printf("METRIC band%zu attenuation dB at100/600/3000/10000 %.5f %.5f %.5f %.5f\n",b,loss[b][0],loss[b][1],loss[b][2],loss[b][3]);
QuadParameters p;p.bands[0].thresholdDb=-30;p.bands[0].rangeDb=-18;p.bands[0].attackMs=3;p.bands[0].releaseMs=50;const auto quiet=tone(p,48000,100,.03),loud=tone(p,48000,100,.5);const double rangeReduction=db(std::abs(quiet.transfer))-db(std::abs(loud.transfer));check(rangeReduction>3.,"Negative RANGE compresses the level difference between quiet and loud tones",rangeReduction);
p.bands[0].rangeDb=12;const auto expanded=tone(p,48000,100,.5);check(db(std::abs(expanded.transfer))>3.&&expanded.reductions[0]<-3.,"Positive RANGE expands loud band energy and meter shows signed boost",db(std::abs(expanded.transfer)));
}

void dynamicsTiming(){QuadParameters p;p.bands[0].thresholdDb=-30;p.bands[0].rangeDb=-24;p.bands[0].attackMs=10;p.bands[0].releaseMs=100;QuadDSP e;e.prepare(48000,1,1);e.setParameters(p);std::array<double,5> samples{};
for(int i=0;i<20160;++i){float x=i<7680?.5f:0.f;float* data[]{&x};process(e,data,1,1);if(i==479)samples[0]=e.bandReductionDb()[0];if(i==2879)samples[1]=e.bandReductionDb()[0];if(i==7679)samples[2]=e.bandReductionDb()[0];if(i==12479)samples[3]=e.bandReductionDb()[0];if(i==17279)samples[4]=e.bandReductionDb()[0];}
check(samples[0]>0&&samples[0]<samples[1]&&samples[1]>8&&samples[2]>samples[1],"Attack develops over time without instantaneous full reduction",samples[0]);const double ratio=samples[4]/samples[3];check(std::abs(ratio-std::exp(-1.))<.01,"100ms release measured by reduction decay over a later 100ms interval",ratio);
bool knee=true,monotonic=true;double previous=-1;for(double above:{-5.,-3.,-2.,-1.,0.,1.,2.,3.,5.}){QuadParameters q;q.bands[0].thresholdDb=-24;q.bands[0].rangeDb=-18;q.bands[0].attackMs=1;q.bands[0].releaseMs=10;QuadDSP test;test.prepare(48000,128,1);test.setParameters(q);const float amplitude=static_cast<float>(std::pow(10.,(-24+above)/20.));std::vector<float> l(24000,amplitude),r(l.size());blocked(test,l,r,127,1);const double actual=-db(l.back()/amplitude),level=20*std::log10(amplitude)+24,excess=level<=-3?0:level>=3?level:(level+3)*(level+3)/12,expected=18*(-std::expm1(-.75*excess/18));knee=knee&&std::abs(actual-expected)<.0001&&std::abs(test.bandReductionDb()[0]-expected)<.0001;monotonic=monotonic&&actual>=previous-1.e-6;previous=actual;}
check(knee&&monotonic,"Six-dB knee and finite RANGE agree with steady-state gain and real meter");
}

void routing(){std::vector<float> original(48000),sum(original.size()),right(original.size());std::uint32_t seed=611;for(size_t i=0;i<original.size();++i)original[i]=.1f*random(seed);double sumError=0;bool meters=true,bypass=true;
for(size_t band=0;band<4;++band){QuadParameters p;p.bands[band].solo=true;QuadDSP e;e.prepare(48000,512,2);e.setParameters(p);auto l=original,r=right;blocked(e,l,r,-1);for(size_t i=0;i<sum.size();++i)sum[i]+=l[i];const auto gains=e.bandGainDb();for(size_t b=0;b<4;++b)meters=meters&&std::abs(gains[b]-(b==band?0.f:-120.f))<.0001f;
    QuadParameters q;q.bands[band].thresholdDb=-60;q.bands[band].rangeDb=-24;q.bands[band].gainDb=12;q.bands[band].bypass=true;QuadDSP b;b.prepare(48000,512,2);b.setParameters(q);l=original;r=right;blocked(b,l,r,127);bypass=bypass&&l==original&&std::abs(b.bandReductionDb()[band])<1.e-5f;
}for(size_t i=0;i<sum.size();++i)sumError=std::max(sumError,std::abs(static_cast<double>(sum[i])-original[i]));check(sumError<5.e-8,"Four independently soloed band renders sum back to the original waveform",sumError);check(meters,"Band gain meters include actual SOLO mute and unity band gain");check(bypass,"Band BYPASS cancels its trim and dynamics while preserving audio exactly");
QuadParameters p;for(auto& b:p.bands){b.thresholdDb=-40;b.rangeDb=-20;b.gainDb=3;}QuadDSP stereo;stereo.prepare(48000,127,2);stereo.setParameters(p);auto l=original,r=original;for(auto& x:r)x*=.25f;blocked(stereo,l,r,127);double scaleError=0;for(size_t i=0;i<l.size();++i)scaleError=std::max(scaleError,std::abs(static_cast<double>(r[i])-l[i]*.25));check(scaleError<2.e-8,"Stereo-linked gain preserves correlated channel scaling",scaleError);
}

void automationAndRobustness(){bool finite=true,ordered=true,silence=true;std::uint32_t seed=431;for(double fs:{8000.,32000.,44100.,48000.,96000.,192000.}){
    QuadDSP e;e.prepare(fs,512,2);QuadParameters p;std::array<float,257> l{},r{};double last=0,older=0;std::uint64_t at=0;
    for(int block=0;block<350;++block){for(auto& b:p.bands){b.thresholdDb=-30+30*random(seed);b.rangeDb=-6+18*random(seed);b.gainDb=12*random(seed);b.attackMs=1+199*(random(seed)+1)*.5f;b.releaseMs=10+990*(random(seed)+1)*.5f;b.solo=next(seed)%12==0;b.bypass=next(seed)%8==0;}for(auto& f:p.crossoversHz)f=static_cast<float>(20+(fs*.6)*std::abs(random(seed)));p.outputDb=6*random(seed);e.setParameters(p);const auto cross=e.crossoverHz();ordered=ordered&&cross[0]>=20&&cross[1]>=cross[0]*1.349f&&cross[2]>=cross[1]*1.349f&&cross[2]<=fs*.45001;
        const int n=1+static_cast<int>(next(seed)%257);for(int i=0;i<n;++i){l[static_cast<size_t>(i)]=static_cast<float>(.03*std::sin(2*pi*200*(at+static_cast<std::uint64_t>(i))/fs));r[static_cast<size_t>(i)]=l[static_cast<size_t>(i)]*.37f;}float* data[]{l.data(),r.data()};process(e,data,n,2);for(int i=0;i<n;++i){const double y=l[static_cast<size_t>(i)];finite=finite&&std::isfinite(y)&&std::isfinite(r[static_cast<size_t>(i)]);if(at+static_cast<std::uint64_t>(i)>2&&fs==48000)maxAutomationCurvature=std::max(maxAutomationCurvature,std::abs(y-2*last+older));older=last;last=y;}at+=static_cast<std::uint64_t>(n);
    }
    p.crossoversHz={{std::numeric_limits<float>::quiet_NaN(),-1,std::numeric_limits<float>::infinity()}};for(auto& b:p.bands){b.thresholdDb=std::numeric_limits<float>::quiet_NaN();b.rangeDb=std::numeric_limits<float>::infinity();b.gainDb=std::numeric_limits<float>::quiet_NaN();}e.setParameters(p);for(int i=0;i<257;++i){l[static_cast<size_t>(i)]=i%2?std::numeric_limits<float>::infinity():std::numeric_limits<float>::quiet_NaN();r[static_cast<size_t>(i)]=i%2?std::numeric_limits<float>::max():-std::numeric_limits<float>::max();}float* data[]{l.data(),r.data()};process(e,data,257,2);for(size_t i=0;i<l.size();++i)finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(r[i])<=1.e12f;
    e.reset();l.fill(0);r.fill(0);for(int i=0;i<20;++i)process(e,data,257,2);for(size_t i=0;i<l.size();++i)silence=silence&&l[i]==0&&r[i]==0;
}check(ordered,"Crossovers remain ordered and inside Nyquist across extreme automation");check(finite&&silence,"Random automation, malformed parameters/samples, and reset silence stay finite");check(maxAutomationCurvature<.01,"Rapid 48k automation has bounded sample curvature on a smooth 200Hz tone",maxAutomationCurvature);
check(allocations==0,"All guarded audio processing calls perform zero heap allocations",static_cast<double>(allocations));
QuadDSP e;e.prepare(48000,128,2);QuadParameters p;for(auto& b:p.bands){b.rangeDb=-18;b.thresholdDb=-36;}e.setParameters(p);std::array<float,128> l{},r{};const auto start=std::chrono::steady_clock::now();for(int block=0;block<750;++block){for(int i=0;i<128;++i){l[static_cast<size_t>(i)]=static_cast<float>(.2*std::sin((block*128+i)*.11));r[static_cast<size_t>(i)]=l[static_cast<size_t>(i)]*.7f;}float* data[]{l.data(),r.data()};process(e,data,128,2);}realtimePercent=100*std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/2.;std::printf("METRIC 48k/128 stereo active-four-band CPU wall fraction %.4f%%, shared machine\n",realtimePercent);
}
}
int main(){const auto start=std::chrono::steady_clock::now();std::printf("QuadDSP original complementary four-band dynamics; C++17, %zu-bit pointers\n",sizeof(void*)*8);neutral();frequencyResponse();selectivity();dynamicsTiming();routing();automationAndRobustness();const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::printf("RESULT %d checks, %d failures, %llu audio channel samples, %.3f seconds\n",checks,failures,static_cast<unsigned long long>(processed),elapsed);std::ofstream f("quad-dsp-report.json");f<<"{\"passed\":"<<(failures?"false":"true")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_channel_samples\":"<<processed<<",\"audio_allocations\":"<<allocations<<",\"max_complex_response_error\":"<<maxResponseError<<",\"max_display_error_db\":"<<maxDisplayError<<",\"max_48k_automation_curvature\":"<<maxAutomationCurvature<<",\"cpu_48k128_stereo_percent\":"<<realtimePercent<<",\"elapsed_seconds\":"<<elapsed<<"}\n";return failures?1:0;}
