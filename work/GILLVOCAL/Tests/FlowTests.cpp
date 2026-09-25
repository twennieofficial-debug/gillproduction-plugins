#include "../Source/FlowDSP.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace allocationWatch {bool active=false;std::uint64_t count=0;}
void* operator new(std::size_t n){if(allocationWatch::active)++allocationWatch::count;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

namespace {
std::uint64_t checks=0,failures=0,channelSamples=0,configurations=0;
double maxAutomationStep=0,maxFiniteOutput=0,autoGainLevelDifference=0;
constexpr double pi=3.14159265358979323846;
struct Measurement {int mode;double amount,reduction,expected;};std::vector<Measurement> compression;
void require(bool ok,const char* message){++checks;if(!ok){++failures;if(failures<=30)std::cerr<<"FAIL: "<<message<<'\n';}}
void process(gill::FlowDSP& dsp,float* const* buffers,int channels,int samples){allocationWatch::active=true;dsp.process(buffers,channels,samples);allocationWatch::active=false;if(channels>0&&samples>0)channelSamples+=static_cast<std::uint64_t>(channels)*samples;}
bool same(const gill::LearnProfile& a,const gill::LearnProfile& b){return a.version==b.version&&a.valid==b.valid&&a.rmsDb==b.rmsDb&&a.peakDb==b.peakDb&&a.crestDb==b.crestDb&&a.thresholdDb==b.thresholdDb&&a.dynamicRangeDb==b.dynamicRangeDb&&a.motionDb==b.motionDb&&a.attackMs==b.attackMs&&a.releaseMs==b.releaseMs;}
void constant(gill::FlowDSP& dsp,int count,float value,int block=127){std::array<float,2048> left{},right{};float* channels[]{left.data(),right.data()};for(int done=0;done<count;){const int n=std::min({block,count-done,2048});left.fill(value);right.fill(value);process(dsp,channels,2,n);done+=n;}}
void sine(gill::FlowDSP& dsp,double fs,double seconds,double amplitude=.1,int block=127,double hz=200){std::array<float,2048> left{},right{};float* channels[]{left.data(),right.data()};const auto total=static_cast<int>(std::lround(seconds*fs));for(int done=0;done<total;){const auto n=std::min({block,total-done,2048});for(int i=0;i<n;++i){left[i]=static_cast<float>(amplitude*std::sin(2*pi*hz*(done+i)/fs));right[i]=-.37f*left[i];}process(dsp,channels,2,n);done+=n;}}

void unityAndConfiguration(){
    for(double fs:{8000.,44100.,48000.,96000.,192000.,384000.})for(int nc:{1,2})for(int mode:{0,1,2})for(int block:{1,17,64,127,512,2048}){
        gill::FlowDSP dsp;dsp.prepare(fs,block,nc);dsp.setParameters(0,mode,true);require(dsp.latencySamples()==0,"all rates channels modes and blocks report zero latency");std::array<float,2048> a{},b{},originalA{},originalB{};float* buffers[]{a.data(),b.data()};for(int i=0;i<block;++i){a[i]=originalA[i]=static_cast<float>(.3*std::sin(.113*i));b[i]=originalB[i]=static_cast<float>(.2*std::cos(.083*i));}process(dsp,buffers,nc,block);for(int i=0;i<block;++i){require(a[i]==originalA[i],"zero amount is exact undelayed unity");if(nc==2)require(b[i]==originalB[i],"zero amount is exact stereo unity");}}
    gill::FlowDSP invalid;for(double fs:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){invalid.prepare(fs,0,7);invalid.setParameters(std::numeric_limits<float>::quiet_NaN(),999,true);constant(invalid,1024,.01f);require(std::isfinite(invalid.gainReductionDb()),"invalid sample rate and amount select finite safe defaults");}
    invalid.process(nullptr,2,128);float* absent[]{nullptr};invalid.process(absent,1,128);require(invalid.latencySamples()==0,"null and empty processing is safe");
}
void calibratedCompression(){
    for(int mode:{0,1,2}){double prior=-1;for(float amount:{0.f,25.f,55.f,80.f,100.f}){gill::FlowDSP dsp;dsp.prepare(48000,128,1);dsp.setParameters(amount,mode,false);constant(dsp,static_cast<int>(48000*2),.25f);
        const double strength=amount*.01,ratio=mode==0?3:(mode==1?6:12),shift=mode==0?8:(mode==1?11:15);const double expected=std::min(24.,(1-1/ratio)*(20*std::log10(.25)-(-18-strength*shift)))*strength;
        const double actual=dsp.gainReductionDb();require(std::abs(actual-expected)<.002,"steady compression agrees with independently computed ratio and threshold law");require(actual>=prior,"increasing amount monotonically increases steady reduction");prior=actual;compression.push_back({mode,amount,actual,expected});
        float sample=.25f;float* ptr[]{&sample};process(dsp,ptr,1,1);require(std::abs(20*std::log10(sample/.25)+expected)<.002,"actual audio attenuation matches the gain reduction readout");require(dsp.makeupGainDb()==0,"AUTO GAIN disabled adds no makeup");}}
    double last=-1;for(int mode:{0,1,2}){gill::FlowDSP dsp;dsp.prepare(48000,128,1);dsp.setParameters(100,mode,false);constant(dsp,48000,.001f);constant(dsp,240,.5f);const auto reduction=dsp.gainReductionDb();require(reduction>last+.05,"NATURAL FOCUS CRUSH have progressively stronger 5 ms transient response");last=reduction;constant(dsp,48000,.5f);const auto held=dsp.gainReductionDb();constant(dsp,480,.001f);const auto early=dsp.gainReductionDb();constant(dsp,48000,.001f);require(early<held&&dsp.gainReductionDb()<early*.02,"release decays smoothly after a loud phrase");}
    gill::FlowDSP impulse;impulse.prepare(48000,32,1);impulse.setParameters(100,2,false);std::array<float,32> x{};x[0]=.5f;float* p[]{x.data()};process(impulse,p,1,32);require(x[0]>0&&x[0]<=.5f,"compressor processes the first impulse sample without lookahead delay");for(size_t i=1;i<x.size();++i)require(x[i]==0,"gain-only processing creates no audio tail after an impulse");
}
void learning(){
    for(double fs:{8000.,44100.,48000.,96000.,192000.}){gill::FlowDSP dsp;dsp.prepare(fs,127,2);dsp.startLearning();constant(dsp,static_cast<int>(fs*2),0);require(dsp.learningState()==1&&dsp.learningProgress()==0,"silence adds no learning progress");sine(dsp,fs,5);require(dsp.learningState()==1&&dsp.learningProgress()>.49f&&dsp.learningProgress()<.51f,"five active seconds gives half learning progress");sine(dsp,fs,5.04);const auto profile=dsp.learnedProfile();require(dsp.learningState()==2&&dsp.learningProgress()==1&&profile.valid,"ten active seconds completes learning at each sample rate");require(std::abs(profile.rmsDb-(20*std::log10(.1/std::sqrt(2.))))<.6&&std::abs(profile.peakDb+20)<.6,"learned RMS and robust peak match independently known sine statistics");require(std::abs(profile.thresholdDb-(profile.rmsDb+3))<1e-6,"threshold derives from median active RMS");
        dsp.startLearning();constant(dsp,static_cast<int>(fs),0);dsp.cancelLearning();require(dsp.learningState()==2&&same(dsp.learnedProfile(),profile),"cancel keeps the prior completed profile");dsp.startLearning();dsp.reset();require(dsp.learningState()==2&&same(dsp.learnedProfile(),profile),"reset cancels an unfinished attempt and preserves completed profile");dsp.prepare(fs,512,1);require(dsp.learningState()==2&&same(dsp.learnedProfile(),profile),"prepare at a new block layout preserves learned profile");}
    gill::FlowDSP fresh;fresh.prepare(8000,64,1);fresh.startLearning();constant(fresh,240000,0);require(fresh.learningState()==3&&fresh.learningProgress()==0&&!fresh.learnedProfile().valid,"30 seconds without active audio returns insufficient instead of a fabricated profile");fresh.cancelLearning();require(fresh.learningState()==0,"cancel with no completed profile returns idle");
    gill::LearnProfile profile;profile.valid=true;profile.rmsDb=-25;profile.peakDb=-12;profile.crestDb=13;profile.thresholdDb=-22;require(fresh.setLearnedProfile(profile),"valid learned profile imports");fresh.startLearning();constant(fresh,240000,0);require(fresh.learningState()==3&&same(fresh.learnedProfile(),profile),"insufficient re-learning preserves previous usable profile");fresh.reset();require(fresh.learningState()==2&&same(fresh.learnedProfile(),profile),"reset after insufficient learning restores ready status");
    for(int field=0;field<5;++field){auto corrupt=profile;if(field==0)corrupt.version=99;if(field==1)corrupt.rmsDb=std::numeric_limits<float>::quiet_NaN();if(field==2)corrupt.peakDb=99;if(field==3)corrupt.crestDb=-1;if(field==4)corrupt.thresholdDb=-99;require(!fresh.setLearnedProfile(corrupt)&&same(fresh.learnedProfile(),profile),"invalid imported profiles are rejected without modifying current learned settings");}
    require(fresh.setLearnedProfile(gill::LearnProfile{})&&fresh.learningState()==0&&!fresh.learnedProfile().valid,"explicit invalid/empty profile clears learned state");
    gill::FlowDSP robust;robust.prepare(48000,960,1);robust.startLearning();std::array<float,960> frame{};float* p[]{frame.data()};for(int f=0;f<660&&robust.learningState()==1;++f){for(int i=0;i<960;++i)frame[i]=static_cast<float>(.1*std::sin(2*pi*200*i/48000.));if(f%5==0){frame.fill(0);frame[300]=1;}process(robust,p,1,960);}require(robust.learningState()==2&&std::abs(robust.learnedProfile().rmsDb+23)<.6,"isolated impulse-outlier frames do not bias the learned vocal level");
    gill::FlowDSP a,b;a.prepare(48000,1,2);b.prepare(48000,2048,2);a.setParameters(0,0,false);b.setParameters(100,2,true);a.startLearning();b.startLearning();sine(a,48000,10.04,.1,1);sine(b,48000,10.04,.1,2048);require(same(a.learnedProfile(),b.learnedProfile()),"learning observes original input independently of amount mode autogain and block partitioning");
    gill::LearnProfile quiet,loud;quiet.valid=loud.valid=true;quiet.rmsDb=-30;quiet.peakDb=-18;quiet.crestDb=12;quiet.thresholdDb=-27;loud.rmsDb=-18;loud.peakDb=-6;loud.crestDb=12;loud.thresholdDb=-15;
    require(a.setLearnedProfile(quiet)&&b.setLearnedProfile(loud),"different valid reference vocal profiles import");a.setParameters(55,0,false);b.setParameters(55,0,false);constant(a,96000,.1f);constant(b,96000,.1f);require(a.gainReductionDb()>b.gainReductionDb()+2,"learned vocal level changes the real compressor threshold and audio attenuation");
}
void linkedAndPartitioned(){
    std::vector<float> source(24000);for(size_t i=0;i<source.size();++i)source[i]=static_cast<float>(.3*std::sin(.137*i)*(i%4000<700?.15:1.));
    for(int mode:{0,1,2})for(bool gain:{false,true}){gill::FlowDSP dsp;dsp.prepare(48000,127,2);dsp.setParameters(87,mode,gain);auto left=source,right=source;for(auto& x:right)x=-x;for(size_t at=0;at<left.size();){const int n=static_cast<int>(std::min<size_t>(127,left.size()-at));float* p[]{left.data()+at,right.data()+at};process(dsp,p,2,n);at+=n;}for(size_t i=0;i<left.size();++i)require(right[i]==-left[i],"shared detector and gain preserve stereo antiphase exactly");
        gill::FlowDSP other;other.prepare(48000,1,1);other.setParameters(87,mode,gain);auto mono=source;for(size_t i=0;i<mono.size();++i){float* p[]{mono.data()+i};process(other,p,1,1);}require(mono==left,"mono versus equal-magnitude stereo and block partitioning are sample exact");}
}
void learnedDynamics(){
    std::array<double,3> measured{};
    auto phrase=[](int n,double scale,double speed){const double t=n/48000.;return static_cast<float>(scale*std::exp(.9*std::sin(2*pi*speed*t))*(std::sin(2*pi*220*t)+.25*std::sin(2*pi*440*t)));};
    gill::LearnProfile slowProfile,fastProfile;
    for(int level=0;level<3;++level){gill::FlowDSP dsp;dsp.prepare(48000,127,1);dsp.setParameters(75,1,false);dsp.startLearning();const double inputScale=.025*std::pow(4.,level);
        std::array<float,127> buffer{};float* ptr[]{buffer.data()};
        for(int at=0;at<486400;at+=127){for(int i=0;i<127;++i)buffer[i]=phrase(at+i,inputScale,2.);process(dsp,ptr,1,127);}
        const auto learned=dsp.learnedProfile();require(learned.valid&&learned.version==2&&learned.dynamicRangeDb>10,"v2 learns measured phrase dynamic range, not only median level");
        require(learned.attackMs>=6&&learned.attackMs<=30&&learned.releaseMs>=70&&learned.releaseMs<=280,"measured learning generates bounded attack and release");
        double dry=0,wet=0;for(int at=0;at<144000;at+=127){std::array<float,127> before{};for(int i=0;i<127;++i)before[i]=buffer[i]=phrase(at+i,inputScale,2.);process(dsp,ptr,1,127);if(at>48000)for(int i=0;i<127;++i){dry+=before[i]*before[i];wet+=buffer[i]*buffer[i];}}
        measured[level]=10*std::log10(wet/dry);require(measured[level]<-4&&measured[level]>-20,"learned compressor produces useful measurable attenuation of dynamic phrases");
        if(level==1)slowProfile=learned;
    }
    require(*std::max_element(measured.begin(),measured.end())-*std::min_element(measured.begin(),measured.end())<.6,"same phrase learned across a 24 dB recording-level range receives comparable compression");
    gill::FlowDSP fast;fast.prepare(48000,960,1);fast.startLearning();std::array<float,960> buffer{};float* ptr[]{buffer.data()};for(int at=0;at<486720;at+=960){for(int i=0;i<960;++i)buffer[i]=phrase(at+i,.1,9.);process(fast,ptr,1,960);}fastProfile=fast.learnedProfile();
    require(fastProfile.motionDb>slowProfile.motionDb+2&&fastProfile.releaseMs<slowProfile.releaseMs-10,"faster measured vocal envelope movement selects quicker release");
    for(int field=0;field<4;++field){auto bad=slowProfile;if(field==0)bad.dynamicRangeDb=-1;if(field==1)bad.motionDb=100;if(field==2)bad.attackMs=0;if(field==3)bad.releaseMs=std::numeric_limits<float>::infinity();require(!fast.setLearnedProfile(bad)&&same(fast.learnedProfile(),fastProfile),"invalid new learned envelope fields cannot corrupt current state");}
    std::cout<<"v2 learned phrase reduction over 24 dB input range: "<<measured[0]<<", "<<measured[1]<<", "<<measured[2]<<" dB; slow/fast release "<<slowProfile.releaseMs<<" / "<<fastProfile.releaseMs<<" ms\n";
}
void autoGain(){gill::FlowDSP dsp;dsp.prepare(48000,128,1);dsp.setParameters(55,0,true);std::array<float,128> buffer{};float* p[]{buffer.data()};double dryEnergy=0,wetEnergy=0;
    for(int block=0;block<4500;++block){std::array<float,128> dry{};for(int i=0;i<128;++i){const double t=(block*128+i)/48000.;dry[i]=buffer[i]=static_cast<float>((.16+.10*std::sin(2*pi*1.7*t))*(std::sin(2*pi*220*t)+.3*std::sin(2*pi*440*t)));}process(dsp,p,1,128);require(dsp.makeupGainDb()>=0&&dsp.makeupGainDb()<=9.00001,"automatic makeup gain stays within 0 to +9 dB");if(block>2250)for(int i=0;i<128;++i){dryEnergy+=dry[i]*dry[i];wetEnergy+=buffer[i]*buffer[i];}}
    autoGainLevelDifference=10*std::log10(wetEnergy/dryEnergy);require(std::abs(autoGainLevelDifference)<1.0,"slow automatic makeup approximately matches power on a varying vocal-like test");
    dsp.setParameters(0,0,true);constant(dsp,4800,.137f);float exact=.137f;float* ex[]{&exact};process(dsp,ex,1,1);require(exact==.137f&&dsp.gainReductionDb()==0&&dsp.makeupGainDb()==0,"amount zero reaches exact unity after an active compensated passage");
}
void stress(){std::mt19937 random(73582);std::uniform_real_distribution<float> uniform(-1,1);const double rates[]{8000,44100,48000,96000,192000,384000};
    for(int test=0;test<10000;++test){gill::FlowDSP dsp;const auto fs=rates[test%6];dsp.prepare(fs,257,2);dsp.setParameters(test%11==0?0.f:(uniform(random)+1)*50,test%3,test%2!=0);if(test%9==0)dsp.startLearning();std::array<float,257> a{},b{};for(int i=0;i<257;++i){a[i]=uniform(random)*.5f;b[i]=uniform(random)*.5f;}if(test%23==0)a[15]=std::numeric_limits<float>::quiet_NaN();if(test%29==0)b[52]=std::numeric_limits<float>::infinity();if(test%31==0)a[7]=std::numeric_limits<float>::max();if(test%37==0)b[4]=std::numeric_limits<float>::denorm_min();float* p[]{a.data(),b.data()};process(dsp,p,2,257);for(int i=0;i<257;++i){require(std::isfinite(a[i])&&std::isfinite(b[i]),"random parameters and malformed samples remain finite");maxFiniteOutput=std::max({maxFiniteOutput,std::abs(static_cast<double>(a[i])),std::abs(static_cast<double>(b[i]))});}require(dsp.gainReductionDb()>=0&&dsp.gainReductionDb()<=24.001&&dsp.learningProgress()>=0&&dsp.learningProgress()<=1,"random meters and learning progress remain bounded");++configurations;}
    gill::FlowDSP automation;automation.prepare(48000,17,2);automation.setParameters(55,0,true);constant(automation,48000,.25f);float previous=.25f;{float x=.25f;float* p[]{&x};process(automation,p,1,1);previous=x;}
    for(int block=0;block<12000;++block){automation.setParameters(block%2?100.f:0.f,block%3,block%5!=0);std::array<float,17> buffer{};buffer.fill(.25f);float* p[]{buffer.data()};process(automation,p,1,17);for(float x:buffer){maxAutomationStep=std::max(maxAutomationStep,std::abs(static_cast<double>(x-previous)));previous=x;}}
    require(maxAutomationStep<.005,"rapid extreme parameter automation stays smoothly bounded on constant input");
    automation.reset();std::array<float,512> silent{};float* ptr[]{silent.data()};for(int i=0;i<20;++i)process(automation,ptr,1,512);require(std::all_of(silent.begin(),silent.end(),[](float x){return x==0;}),"reset silence produces exact zero and no denormals");require(allocationWatch::count==0,"process callback performed no heap allocation");
}
}
int main(){const auto start=std::chrono::steady_clock::now();unityAndConfiguration();calibratedCompression();learning();learnedDynamics();linkedAndPartitioned();autoGain();stress();const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream report("flow-dsp-report.json");report<<std::setprecision(12)<<"{\n\"suite\":\"GILLFLOW DSP\",\"passed\":"<<(failures?"false":"true")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"actual_audio_configurations\":"<<configurations<<",\"audio_channel_samples\":"<<channelSamples<<",\"process_heap_allocations\":"<<allocationWatch::count<<",\"maximum_automation_sample_step\":"<<maxAutomationStep<<",\"maximum_random_output\":"<<maxFiniteOutput<<",\"auto_gain_test_level_difference_db\":"<<autoGainLevelDifference<<",\"elapsed_seconds\":"<<seconds<<",\n\"steady_compression\":[";
    for(size_t i=0;i<compression.size();++i){const auto& m=compression[i];if(i)report<<',';report<<"{\"mode\":"<<m.mode<<",\"amount\":"<<m.amount<<",\"actual_reduction_db\":"<<m.reduction<<",\"analytical_reduction_db\":"<<m.expected<<'}';}report<<"],\n\"scope\":\"Deterministic synthetic tests only; no subjective listening, real-speaker corpus, host certification, or neural-model equivalence claim\"\n}\n";
    std::cout<<checks<<" checks, "<<failures<<" failures, "<<configurations<<" audio configurations, "<<channelSamples<<" channel samples, "<<seconds<<" seconds\n";return failures?1:0;}
