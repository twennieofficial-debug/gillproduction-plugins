#include "../Source/AirDSP.h"
#include "../Source/SpaceDSP.h"
#include "../Source/EchoDSP.h"
#include "../Source/BalanceDSP.h"
#include <iostream>
#include <vector>
#include <random>
#include <fstream>
#include <limits>
#include <memory>
namespace {
int checks=0,failures=0;constexpr double pi=3.14159265358979323846;
void check(bool good,const char* text,double value=0){++checks;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<text<<" : "<<value<<'\n';}
template<class E>void run(E& e,std::vector<float>& x,int block=127){for(size_t at=0;at<x.size();at+=block){float* p[]{x.data()+at};e.process(p,1,static_cast<int>(std::min(x.size()-at,static_cast<size_t>(block))));}}
void routing(){for(int engine=0;engine<2;++engine)for(double fs:{8000.,44100.,48000.,96000.})for(float mix:{0.f,25.f,100.f}){
    std::vector<float> dryOn(static_cast<size_t>(fs)),dryOff(dryOn.size()),fullWet(dryOn.size());dryOn[0]=dryOff[0]=fullWet[0]=.25f;
    if(engine==0){gill::SpaceDSP a,b,c;a.setParameters(mix,1.2f,80,50,50,80,1,100);b.setParameters(mix,1.2f,80,50,50,80,1,0);c.setParameters(100,1.2f,80,50,50,80,1,0);a.prepare(fs,127,1);b.prepare(fs,31,1);c.prepare(fs,64,1);run(a,dryOn);run(b,dryOff,31);run(c,fullWet,64);}
    else{gill::EchoDSP a,b,c;a.setParameters(80,40,mix,0,80,0,100);b.setParameters(80,40,mix,0,80,0,0);c.setParameters(80,40,100,0,80,0,0);a.prepare(fs,127,1);b.prepare(fs,31,1);c.prepare(fs,64,1);run(a,dryOn);run(b,dryOff,31);run(c,fullWet,64);}
    bool null=true,sameTail=true;double energy=0;for(size_t i=0;i<dryOn.size();++i){const double expected=i==0?.25*(1-mix*.01):0;null&=std::abs(dryOn[i]-dryOff[i]-expected)<1e-7;sameTail&=std::abs(dryOff[i]-fullWet[i]*(mix*.01))<1e-7;energy+=dryOff[i]*dryOff[i];}
    check(null,"DRY0 removes only original direct signal across rate / mix / variable buffers");check(sameTail,"DRY0 retains wet gain and tail exactly");check(mix==0?energy==0:energy>1e-8,"wet-only remains audible at nonzero MIX",energy);
}}
double exciterAmount(float amount){gill::AirDSP e;e.setParameters(amount,amount,100,0);e.prepare(48000,127,1);std::vector<float>x(48000);for(int i=0;i<48000;++i)x[i]=static_cast<float>(.15*std::sin(2*pi*2400*i/48000)+.10*std::sin(2*pi*7000*i/48000));auto original=x;run(e,x);double sum=0;for(int i=24000;i<48000;++i){const double d=x[i]-original[i-e.latencySamples()];sum+=d*d;}return std::sqrt(sum/24000);}
gill::LearnBalanceProfile learnedNoise(int block){gill::BalanceDSP e;e.prepare(48000,block,1);e.setParameters(0,0);e.startLearning();std::mt19937 rng(8373);std::uniform_real_distribution<float> uniform(-.05f,.05f);int total=static_cast<int>(48000*10.2);std::vector<float>x(total);for(int i=0;i<total;++i)x[i]=uniform(rng)+static_cast<float>(.045*std::sin(2*pi*2600*i/48000));run(e,x,block);check(e.learningState()==2,"actual ten-second audio yields a completed fine profile");return e.learnedProfile();}
void balance(){auto p=learnedNoise(127),other=learnedNoise(509);check(p.version==2&&p.fine.frames>=10,"new learns use validated fine spectral profile v2",p.fine.frames);check(p.fine.db==other.fine.db&&p.bandDb==other.bandDb,"fine learning is independent of variable host block size");
    auto e=std::make_unique<gill::BalanceDSP>();e->prepare(48000,127,1);e->setParameters(100,0);check(e->setLearnedProfile(p),"fine profile roundtrip accepted");auto view=e->fineView();bool found=false;for(int i=0;i<view.count;++i)found|=std::abs(std::log2(view.frequency[i]/2600.f))<.10&&view.q[i]>2;check(found,"FFT recognizes narrow 2600Hz resonance between original eight centers",view.count);
    std::vector<float>x(96000);for(size_t i=0;i<x.size();++i)x[i]=static_cast<float>(.05*std::sin(2*pi*2600*i/48000));const auto input=x;run(*e,x);double wet=0,dry=0;for(int i=48000;i<96000;++i){wet+=x[i]*x[i];dry+=input[i]*input[i];}const double attenuation=10*std::log10(wet/dry);check(attenuation<-3,"learned narrow resonance is reduced in actual output audio",attenuation);
    for(size_t i=0;i<x.size();++i)x[i]=static_cast<float>(.05*std::sin(2*pi*6400*i/48000));run(*e,x);
    view=e->fineView();bool dynamic=false;for(float g:view.dynamics)dynamic|=g<-.1;check(dynamic,"relative multiband detector reacts to a new 6400Hz spectral excess",view.dynamics[6]);
    gill::FineBalanceStage low,high;low.prepare(48000);high.prepare(48000);low.configure(p.fine,true,p.activeRmsDb,p.bandDb);high.configure(p.fine,true,p.activeRmsDb,p.bandDb);std::array<double,8> powers{};std::array<bool,8> active{};active.fill(true);powers.fill(.00001);powers[6]=.001;auto scaled=powers;for(auto& v:scaled)v*=4;for(int i=0;i<96000;++i){low.update(powers,.002,active);high.update(scaled,.008,active);}check(low.snapshot().dynamics==high.snapshot().dynamics,"doubling whole-vocal amplitude cannot trigger extra tonal compression");
    auto test=*e; // fixed storage copy is deterministic and retains detector state
    std::vector<float>a(20000),b(20000);for(int i=0;i<20000;++i)a[i]=b[i]=static_cast<float>(.02*std::sin(i*.19));run(*e,a,1);run(test,b,257);check(a==b,"fine static and dynamic processing are sample exact across buffers");
    gill::FineBalanceStage transition,oldStage;transition.prepare(48000);transition.configure(p.fine,true,p.activeRmsDb,p.bandDb);transition.setAmount(100);for(int i=0;i<48000;++i){transition.update(powers,.002,active);transition.process(.05*std::sin(2*pi*2600*i/48000),0);}oldStage=transition;auto changed=p.fine;changed.db.fill(-30);transition.configure(changed,true,p.activeRmsDb,p.bandDb);double maximumJump=0;for(int i=0;i<32;++i){const double input=.05*std::sin(2*pi*2600*(48000+i)/48000);transition.update(powers,.002,active);oldStage.update(powers,.002,active);maximumJump=std::max(maximumJump,std::abs(transition.process(input,0)-oldStage.process(input,0)));}check(maximumJump<.001,"live profile replacement crossfades without an abrupt filter reset",maximumJump);
    e->setParameters(0,0);std::vector<float> silence(48000*4);run(*e,silence);std::vector<float> dryAudio(3000,.123f);run(*e,dryAudio);check(std::all_of(dryAudio.begin(),dryAudio.end(),[](float y){return y==.123f;}),"AMOUNT0 settles to exact unchanged audio including dynamic stage");
    auto invalid=p;invalid.fine.db[100]=std::numeric_limits<float>::infinity();check(!e->setLearnedProfile(invalid),"malformed fine spectral data rejected");invalid=p;invalid.fine.frames=0;check(!e->setLearnedProfile(invalid),"empty v2 measurement rejected");
    p.version=1;check(e->setLearnedProfile(p)&&!e->fineView().enabled,"v1 profiles preserve legacy static processing until relearn");
    e->setLearnedProfile({});e->startLearning();std::vector<float> quiet(48000*30);run(*e,quiet,1024);check(e->learningState()==3&&!e->learnedProfile().valid,"silent learning never fabricates a fine EQ curve");
    p.version=2;std::mt19937 random(90344);std::uniform_real_distribution<float> unit(-1,1);bool finite=true;double peak=0;const double rates[]{8000,44100,48000,96000,192000,384000};
    for(int configuration=0;configuration<1000;++configuration){e->prepare(rates[configuration%6],31,2);e->setLearnedProfile(p);e->setParameters(static_cast<float>(configuration%101),configuration%5);std::array<float,257> left{},right{};for(int i=0;i<257;++i){left[i]=unit(random)*.4f;right[i]=unit(random)*.4f;}if(configuration%11==0)left[2]=std::numeric_limits<float>::infinity();float* channels[]{left.data(),right.data()};e->process(channels,2,257);for(int i=0;i<257;++i){finite&=std::isfinite(left[i])&&std::isfinite(right[i]);peak=std::max({peak,std::abs(static_cast<double>(left[i])),std::abs(static_cast<double>(right[i]))});}}
    check(finite&&peak<4,"1000 actual fine-profile audio configurations cover 6 sample rates, stereo and hostile samples",peak);
}
}
int main(){routing();const double subtle=exciterAmount(15),medium=exciterAmount(50),full=exciterAmount(100);check(subtle>0&&medium>subtle*3&&full>medium*3,"AIR effect grows from subtle to strong across practical levels",full/medium);balance();std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
