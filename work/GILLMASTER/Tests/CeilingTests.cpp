#include "../Source/Foundation/FinishDSP.h"
#include "TestSupport.h"
double reconstructedPeak(const std::vector<float>& audio){
    constexpr int radius=32;std::array<std::array<double,65>,16> coefficients{};
    for(int phase=0;phase<16;++phase){double sum=0;for(int k=-radius;k<=radius;++k){const double d=k-phase/16.;const double sinc=std::abs(d)<1e-12?1:std::sin(test::pi*d)/(test::pi*d);const double w=std::abs(d)<=radius?.5+.5*std::cos(test::pi*d/radius):0;coefficients[phase][k+radius]=sinc*w;sum+=sinc*w;}for(auto&c:coefficients[phase])c/=sum;}
    double maximum=0;for(int i=-radius;i<int(audio.size())+radius;++i)for(int phase=0;phase<16;++phase){double value=0;for(int k=-radius;k<=radius;++k)if(i+k>=0&&i+k<int(audio.size()))value+=audio[size_t(i+k)]*coefficients[phase][k+radius];maximum=std::max(maximum,std::abs(value));}return maximum;
}

int main(){
    bool ceilingOk=true,linked=true,finite=true,liveZero=true;double worst=-100;
    for(double fs:{44100.,48000.,96000.,192000.})for(float ceiling:{-12.f,-1.f,0.f})for(int character=0;character<3;++character){
        auto e=std::make_unique<gillnext::FinishDSP>();gillnext::FinishParameters p;p.toneEnabled=p.compEnabled=p.stereoEnabled=false;p.ceilingDb=ceiling;p.driveDb=character==2?24:6;p.clip=character==0?0:character==1?20:65;p.releaseMs=character==0?150:character==1?65:35;e->setParameters(p);e->prepare(fs,257,2);
        std::vector<float>l(4096+e->latencySamples()+96),r(l.size());for(int i=0;i<4096;++i){l[i]=float(.9*std::sin(test::pi*.9*i)+.5*std::sin(test::pi*.21*i));r[i]=l[i]*.4f;}test::run(*e,l,r,257);const double over=20*std::log10(reconstructedPeak(l))-ceiling;worst=std::max(worst,over);ceilingOk=ceilingOk&&over<=.05;
        for(size_t i=0;i<l.size();++i){finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i]);if(character==0)linked=linked&&std::abs(r[i]-.4*l[i])<2e-6;}
        e->setLiveMode(true);liveZero=liveZero&&e->latencySamples()==0;e->reset();l.assign(4096,2);r=l;test::run(*e,l,r,63);ceilingOk=ceilingOk&&test::peak(l)<=std::pow(10.,ceiling/20.)*(1+1e-6);
    }
    test::check(ceilingOk,"CEILING independent 16x reconstruction stays <= ceiling +0.05 dB across rates, modes, ceilings and drive",worst);test::check(linked,"CEILING CLEAN limiting preserves linked stereo image");test::check(finite&&liveZero,"CEILING all fixtures finite, LIVE reports zero latency");return test::result();
}
