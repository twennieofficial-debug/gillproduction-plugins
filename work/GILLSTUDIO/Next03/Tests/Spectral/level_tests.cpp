#define gillstudio legacygillstudio
#include "../../../Source/Spectral/Spectral.h"
#undef gillstudio
#include "../../Source/Spectral/Spectral.h"
#include <iostream>
#include <stdexcept>
#include <random>
#include <chrono>
#include <cstdlib>
#include <new>
#include <cstring>
namespace sp=gillstudio::spectral;
namespace old=legacygillstudio::spectral;
thread_local bool countAllocations=false;thread_local std::size_t allocations=0;
void* operator new(std::size_t n){if(countAllocations)++allocations;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
volatile void* allocationEscape=nullptr;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Result {std::vector<float> wave;double db=0;};
double measure(const std::vector<float>& wave,double fs,double hz,double peak){
 double ss=0,cc=0,sc=0,ys=0,yc=0;const auto start=wave.size()/2;
 for(std::size_t i=start;i<wave.size();++i){const double phase=2*sp::pi*hz*i/fs,s=std::sin(phase),c=std::cos(phase);ss+=s*s;cc+=c*c;sc+=s*c;ys+=wave[i]*s;yc+=wave[i]*c;}
 const double determinant=ss*cc-sc*sc,a=(ys*cc-yc*sc)/determinant,b=(yc*ss-ys*sc)/determinant;
 return 20*std::log10(std::max(1e-20,std::hypot(a,b)/std::max(peak,1e-20)));
}
Result tone(sp::Parameters p,double peak=.2,double hz=3000,double fs=48000,double keyPeak=-1,int partition=257){
 const auto n=std::size_t(fs*.6);Result r;r.wave.resize(n);std::vector<float> key(n);
 for(std::size_t i=0;i<n;++i){r.wave[i]=float(peak*std::sin(2*sp::pi*hz*i/fs));key[i]=float(std::max(0.,keyPeak)*std::sin(2*sp::pi*hz*i/fs));}
 sp::Processor dsp;dsp.prepare(fs,1024,1);dsp.setParameters(p);allocations=0;countAllocations=true;
 for(std::size_t start=0;start<n;){const auto count=std::min<std::size_t>(std::size_t(partition),n-start);float* audio[]={r.wave.data()+start};const float* detector[]={key.data()+start};dsp.process(audio,count,keyPeak<0?nullptr:detector,keyPeak<0?0:1);start+=count;}
 countAllocations=false;check(allocations==0,"level audio callback allocated");for(auto v:r.wave)check(std::isfinite(v),"nonfinite level output");r.db=measure(r.wave,fs,hz,peak);return r;
}
sp::Parameters level(int mode=0){sp::Parameters p;p.mode=sp::Mode(mode);p.levelStrength=.65;p.levelThresholdDb=-30;p.maxReductionDb=60;p.attackMs=.05;p.releaseMs=5;return p;}
// Independent Fourier-series prediction for a steady bin-centred sinusoid:
// sqrt-Hann's relative sidelobe coefficients are 1/(1-4m^2), with normalized
// energy weights 8/pi^2/(1-4m^2)^2. This predicts the reconstructed fundamental,
// rather than mistaking a per-bin target for broadband compressor gain.
double expectedSineGain(double rmsDb,double threshold,double strength,double depth){
 double gain=0,total=0;for(int m=-80;m<=80;++m){double d=std::abs(1.-4.*m*m),w=8/(sp::pi*sp::pi*d*d),excess=std::max(0.,rmsDb-20*std::log10(d)-threshold);gain+=w*std::pow(10.,-std::min(depth,strength*excess)/20);total+=w;}return 20*std::log10(gain/total);
}
void absoluteLaw(){for(double fs:{44100.,48000.,96000.}){const double hz=fs*128/2048;for(double strength:{0.,.25,.5,1.})for(double excess:{-6.,0.,6.,12.,24.}){auto p=level();p.levelStrength=strength;const double rmsDb=p.levelThresholdDb+excess,peak=std::sqrt(2.)*sp::amplitude(rmsDb);auto r=tone(p,peak,hz,fs);const double predicted=expectedSineGain(rmsDb,p.levelThresholdDb,strength,p.maxReductionDb);if(std::abs(r.db-predicted)>.12){std::cerr<<"law rate="<<fs<<" strength="<<strength<<" excess="<<excess<<" got="<<r.db<<" expected="<<predicted<<"\n";throw std::runtime_error("PRO absolute-level transfer law");}}}
 auto p=level();const double peak=std::sqrt(2.)*sp::amplitude(-18);for(double cap:{0.,3.,6.,12.,24.}){p.levelThresholdDb=-100;p.maxReductionDb=cap;p.levelStrength=1;const auto r=tone(p,peak);check(std::abs(r.db+cap)<.05,"PRO final level cap");}
 p.resonance=1;p.resonanceThresholdDb=0;p.maxReductionDb=12;check(std::abs(tone(p,peak).db+12)<.05,"combined legacy + level path exceeds shared PRO cap");
 // Relative prominence is invariant to scaling; the requested absolute path must not be.
 p=level();auto quiet=tone(p,std::sqrt(2.)*sp::amplitude(-36)),loud=tone(p,std::sqrt(2.)*sp::amplitude(-18));check(std::abs(quiet.db)<.01&&loud.db<-4,"absolute threshold onset negative control");
 std::cout<<"calibrated PRO transfer / thresholds / strength / caps at three rates passed\n";
}
void curves(){for(int mode=0;mode<2;++mode){auto p=level(mode),lower=p;p.detectorNodes[0]={true,sp::Shape::Gain,1000,6,1,12};lower.levelThresholdDb-=6;
 auto a=tone(p),b=tone(lower);check(std::abs(a.db-b.db)<.001,"detector gain is not equivalent to threshold shift");for(std::size_t i=0;i<a.wave.size();++i)check(std::abs(a.wave[i]-b.wave[i])<1e-6,"detector weighting affects audio outside detector");
 p.levelStrength=0;auto neutral=tone(p);check(std::abs(neutral.db)<.001,"prefilter acts as output EQ at zero strength");
 }
 for(double slope:{-6.,6.})for(double hz:{3000.,6000.}){auto p=level();p.levelSlopeDbPerOct=slope;auto flat=p;flat.levelSlopeDbPerOct=0;flat.levelThresholdDb+=slope*std::log2(hz/1000.);auto a=tone(p,.2,hz),b=tone(flat,.2,hz);check(std::abs(a.db-b.db)<.12,"threshold slope has wrong direction or calibration");}
 for(int shape=0;shape<9;++shape){auto p=level();auto&n=p.detectorNodes[0];n.enabled=true;n.shape=sp::Shape(shape);n.frequency=3000;n.gainDb=6;n.q=1;n.slope=12;double response=6;
  if(shape==1||shape==2)response=3;if(shape==3){n.frequency=1500;response=6*std::tanh(1.);}if(shape==4)response=-6;if(shape==5){n.frequency=6000;response=-12;}if(shape==6){n.frequency=1500;response=-12;}if(shape==7){n.frequency=1500;response=-6;}
  auto flat=level();flat.levelThresholdDb-=response;auto a=tone(p),b=tone(flat);if(std::abs(a.db-b.db)>.16){std::cerr<<"shape="<<shape<<" got="<<a.db<<" expected="<<b.db<<"\n";throw std::runtime_error("PRO detector shape local response");}
 }
 std::cout<<"detector-only weighting, all nine shapes and signed slope passed\n";
}
void keysAndLive(){for(int mode=0;mode<2;++mode){auto p=level(mode);p.externalKey=true;auto small=tone(p,.02,3000,48000,.3),large=tone(p,.2,3000,48000,.3);check(std::abs(small.db-large.db)<.002&&small.db<-1,"absolute key depends on main amplitude or lacks effect");auto silent=tone(p,.2,3000,48000,0);check(std::abs(silent.db)<.001,"silent connected key triggers reduction");auto zero=tone(p,0,3000,48000,.3);for(auto v:zero.wave)check(v==0,"key leaks to main output");auto absent=tone(p);p.externalKey=false;auto internal=tone(p);check(absent.wave==internal.wave,"missing key does not use main fallback");
  p.externalKey=true;p.keyGainDb=6;auto boosted=tone(p,.2,3000,48000,.3);p.keyGainDb=0;auto scaled=tone(p,.2,3000,48000,.3*sp::amplitude(6));check(std::abs(boosted.db-scaled.db)<.002,"key gain incorrect dB scaling");
  p.externalKey=false;p.levelStrength=1;auto below=tone(p,std::sqrt(2.)*sp::amplitude(-36));check(std::abs(below.db)<.01,"level path acts below threshold");double previous=0;for(double strength:{0.,.25,.5,1.}){p.levelStrength=strength;auto result=tone(p,.4);check(result.db<=previous+.002,"level strength is not monotonic");previous=result.db;}check(previous<-2,"LIVE/PRO level path lacks useful reduction");
  p=level(mode);auto whole=tone(p,.2,3000,48000,-1,1024),single=tone(p,.2,3000,48000,-1,1);check(whole.wave==single.wave,"level output depends on host partition");
 }
 std::cout<<"key independence/gain/silence/fallback, LIVE onset and block invariance passed\n";
}
void stereo(){constexpr double fs=48000;constexpr std::size_t n=24000;using Audio=std::array<std::vector<float>,2>;
 auto render=[=](sp::Parameters p,bool midSide,int keyChannels,bool sideKey=false){Audio a{std::vector<float>(n),std::vector<float>(n)},k{std::vector<float>(n),std::vector<float>(n)};for(std::size_t i=0;i<n;++i){const float common=float(.2*std::sin(2*sp::pi*3000*i/fs)),side=float(.1*std::sin(2*sp::pi*6000*i/fs));a[0][i]=midSide?common+side:common;a[1][i]=midSide?common-side:common;k[0][i]=float(.3*std::sin(2*sp::pi*(sideKey?6000:3000)*i/fs));k[1][i]=midSide?(sideKey?-k[0][i]:k[0][i]):0;}sp::Processor dsp;dsp.prepare(fs,257,2);dsp.setParameters(p);for(std::size_t start=0;start<n;){const auto size=std::min<std::size_t>(257,n-start);float* audio[]={a[0].data()+start,a[1].data()+start};const float* key[]={k[0].data()+start,k[1].data()+start};dsp.process(audio,size,key,std::size_t(keyChannels));start+=size;}return a;};
 for(int mode=0;mode<2;++mode){auto p=level(mode);p.externalKey=true;p.stereoLink=0;auto unlinked=render(p,false,2);check(measure(unlinked[0],fs,3000,.2)<-1&&std::abs(measure(unlinked[1],fs,3000,.2))<.01,"unlinked external detector crosses stereo channels");p.stereoLink=1;auto linked=render(p,false,2);check(linked[0]==linked[1],"full link fails to share level reduction");p.stereoLink=0;auto mono=render(p,false,1);check(mono[0]==mono[1]&&mono[0]==unlinked[0],"mono key is not duplicated exactly");p.routing=sp::Channels::MidSide;for(bool side:{false,true}){auto result=render(p,true,2,side);std::vector<float> mid(n),sideAudio(n);for(std::size_t i=0;i<n;++i){mid[i]=.5f*(result[0][i]+result[1][i]);sideAudio[i]=.5f*(result[0][i]-result[1][i]);}const double midDb=measure(mid,fs,3000,.2),sideDb=measure(sideAudio,fs,6000,.1);check(side?(std::abs(midDb)<.01&&sideDb<-1):(midDb<-1&&std::abs(sideDb)<.01),"M/S level detector affects wrong basis");}}
 std::cout<<"stereo link endpoints / mono key / separate mid and side detector action passed\n";
}
template<class P>void legacyParameters(P& p,int mode){p.mode=decltype(p.mode)(mode);p.denoise=.7;p.resonance=.4;p.attackMs=2;p.releaseMs=80;p.maxReductionDb=18;p.mix=.8;p.outputDb=-3;p.learn=true;for(int i=0;i<12;++i){auto&n=p.nodes[std::size_t(i)];n.enabled=true;n.frequency=80*std::pow(200.,i/11.);n.gainDb=i%2?4:-6;n.q=.8;}}
void compatibility(){for(int mode=0;mode<2;++mode)for(double fs:{44100.,48000.,96000.}){sp::Processor next;old::Processor previous;next.prepare(fs,257,2);previous.prepare(fs,257,2);sp::Parameters p;old::Parameters q;legacyParameters(p,mode);legacyParameters(q,mode);
// Denoise was deliberately redesigned before side22 (diffuse floor and release).
// Bit equality certifies only unchanged legacy paths. The revised denoiser is
// independently bound against the frozen pre-side22 core in side22 native tests.
p.denoise=q.denoise=0;std::mt19937 rng(19);std::normal_distribution<float>d(0,.03f);std::array<std::array<float,257>,2>a{},b{};for(int block=0;block<80;++block){p.learn=q.learn=block<30;p.nodes[0].frequency=q.nodes[0].frequency=100+block*7;next.setParameters(p);previous.setParameters(q);const int n=block%3==0?17:257;for(int c=0;c<2;++c)for(int i=0;i<n;++i)a[c][i]=b[c][i]=d(rng);float*ap[]={a[0].data(),a[1].data()},*bp[]={b[0].data(),b[1].data()};next.process(ap,std::size_t(n));previous.process(bp,std::size_t(n));for(int c=0;c<2;++c)for(int i=0;i<n;++i)check(a[c][i]==b[c][i],"default-off new core changes legacy audio");}}
 std::cout<<"bit-exact unchanged legacy core paths (denoise OFF), learning + automated nodes at three rates passed\n";
}
void hostile(){allocationEscape=nullptr;allocations=0;countAllocations=true;auto* positive=new char[37];positive[0]=1;allocationEscape=positive;delete[]positive;countAllocations=false;check(allocations>0,"allocation interceptor positive control failed");
 for(int mode=0;mode<2;++mode)for(double fs:{44100.,48000.,96000.,192000.}){sp::Processor p;p.prepare(fs,17,2);auto controls=level(mode);controls.levelThresholdDb=-100;controls.externalKey=true;for(auto&n:controls.detectorNodes)n={true,sp::Shape::Bell,3000,24,20,96};p.setParameters(controls);std::array<std::array<float,257>,2>x{},key{};float* audio[]={x[0].data(),x[1].data()};const float* detector[]={key[0].data(),key[1].data()};const float values[]={0,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),1e30f,-1e30f};allocations=0;countAllocations=true;for(int block=0;block<25;++block){for(int c=0;c<2;++c)for(int i=0;i<257;++i){x[c][i]=values[(i+block)%6];key[c][i]=values[(i+2*block)%6];}p.process(audio,257,detector,2);for(auto&channel:x)for(auto v:channel)check(std::isfinite(v),"hostile level input not finite");}countAllocations=false;check(allocations==0,"hostile level callback allocation");for(int block=0;block<80;++block){for(auto&c:x)c.fill(0);for(auto&c:key)c.fill(0);p.process(audio,257,detector,2);}for(auto&c:x)for(auto v:c)check(std::isfinite(v)&&std::abs(v)<.001,"hostile signal failed to recover");controls.levelStrength=std::numeric_limits<double>::quiet_NaN();controls.levelThresholdDb=std::numeric_limits<double>::infinity();controls.levelSlopeDbPerOct=-std::numeric_limits<double>::infinity();p.setParameters(controls);for(auto&c:x)c.fill(.1f);p.process(audio,257);for(auto&c:x)for(auto v:c)check(std::isfinite(v),"malformed level controls");}
 std::cout<<"allocation positive control / hostile stereo main+key / recovery / malformed controls passed\n";
}
int main(){std::cout.setf(std::ios::unitbuf);try{compatibility();absoluteLaw();curves();keysAndLive();stereo();hostile();std::cout<<"SPECTRA_LEVEL_AUDIO_PASS\n";}catch(const std::exception&e){countAllocations=false;std::cerr<<e.what()<<"\n";return 1;}}
