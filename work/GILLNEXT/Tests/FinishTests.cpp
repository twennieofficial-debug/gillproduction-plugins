#include "../Source/FinishDSP.h"
#include "TestSupport.h"
namespace {
// Independent 16x interpolation: 65 base-rate taps, Hann-windowed sinc, no
// coefficients shared with the engine's 8x/257-tap Blackman reconstruction.
double reconstructedPeak(const std::vector<float>& audio){
    constexpr int radius=32;std::array<std::array<double,65>,16> coefficients{};
    for(int phase=0;phase<16;++phase){double sum=0;for(int k=-radius;k<=radius;++k){const double d=k-phase/16.;const double sinc=std::abs(d)<1e-12?1:std::sin(test::pi*d)/(test::pi*d);const double w=std::abs(d)<=radius?.5+.5*std::cos(test::pi*d/radius):0;coefficients[phase][k+radius]=sinc*w;sum+=sinc*w;}for(auto&c:coefficients[phase])c/=sum;}
    double maximum=0;for(int i=-radius;i<int(audio.size())+radius;++i)for(int phase=0;phase<16;++phase){double value=0;for(int k=-radius;k<=radius;++k)if(i+k>=0&&i+k<int(audio.size()))value+=audio[size_t(i+k)]*coefficients[phase][k+radius];maximum=std::max(maximum,std::abs(value));}return maximum;
}
gillnext::FinishParameters neutral(){gillnext::FinishParameters p;p.toneEnabled=p.compEnabled=p.clipEnabled=p.stereoEnabled=p.limiterEnabled=false;return p;}
void neutralAndTone(){using namespace test;using namespace gillnext;
    bool identity=true,drive=true,tone=true;double worst=0;
    for(double fs:rates){auto p=neutral();auto d=std::make_unique<FinishDSP>();d->setParameters(p);d->prepare(fs,127,2);auto original=sine(fs,int(fs*.2),733,.03),l=original,r=l;run(*d,l,r,127);for(size_t i=0;i<l.size();++i)identity=identity&&l[i]==(i<size_t(d->latencySamples())?0:original[i-d->latencySamples()]);
        p.driveDb=12;d->setParameters(p);d->reset();l=r=original;run(*d,l,r,127);for(size_t i=size_t(d->latencySamples());i<l.size();++i)drive=drive&&std::abs(l[i]-original[i-d->latencySamples()]*std::pow(10.,12./20))<1e-7;
        p.driveDb=0;p.toneEnabled=true;p.lowDb=3;p.midDb=-2;p.highDb=4;d->setParameters(p);d->reset();l=sine(fs,int(fs*.5),900,.03);original=l;r=l;run(*d,l,r,127);const double measured=20*std::log10(rms(l,size_t(fs*.25))/rms(original,size_t(fs*.25)));const double error=std::abs(measured-d->toneResponseDb(900));worst=std::max(worst,error);tone=tone&&error<.025;
    }
    check(identity,"all disabled modules are bit-exact dry with reported fixed latency");check(drive,"drive is the requested dB gain when dynamics are disabled");check(tone,"tone response graph matches independently measured sine level",worst);
}
void limiting(){using namespace test;using namespace gillnext;
    bool samples=true,truePeak=true,stereo=true;double worstOver=-100,worstInputOver=0;int cases=0;
    FILE* csv=std::fopen("FinishTruePeak.csv","w");if(csv)std::fprintf(csv,"rate,fixture,ceiling_db,input_sample_peak,input_16x_peak,output_sample_peak,output_16x_peak,over_ceiling_db\n");
    for(double fs:rates)for(int fixture=0;fixture<6;++fixture)for(float ceiling:{-3.f,-1.f,0.f}){
        auto p=neutral();p.limiterEnabled=true;p.ceilingDb=ceiling;p.driveDb=fixture==5?12:0;p.clipEnabled=fixture==5;p.clip=fixture==5?65.f:0.f;
        auto d=std::make_unique<FinishDSP>();d->setParameters(p);d->prepare(fs,127,2);
        std::vector<float> l(4096+d->latencySamples()+96),r(l.size());std::uint32_t random=17329;
        for(int i=0;i<4096;++i){double x=0;switch(fixture){case 0:x=1.3*std::sin(2*pi*.25*i+pi*.25);break;case 1:x=1.4*std::sin(2*pi*.45*i+.7);break;case 2:x=.7*std::sin(2*pi*.31*i)+.8*std::sin(2*pi*.073*i)+.2*std::sin(2*pi*.003*i);break;case 3:x=(i%4<2?1.1:-1.1);break;case 4:random^=random<<13;random^=random>>17;random^=random<<5;x=(double(random)/4294967295.-.5)*3;break;default:x=(i>400&&i<1200?1: .1)*std::sin(2*pi*.017*i);break;}l[i]=float(x);r[i]=float(.4*x);}
        const double inputPeak=peak(l),inputTp=reconstructedPeak(l);worstInputOver=std::max(worstInputOver,20*std::log10(inputTp/inputPeak));run(*d,l,r,127);const double outputPeak=peak(l),outputTp=reconstructedPeak(l),ceilingGain=std::pow(10.,ceiling/20.);const double over=20*std::log10(outputTp/ceilingGain);worstOver=std::max(worstOver,over);samples=samples&&outputPeak<=ceilingGain*(1+1e-6);truePeak=truePeak&&over<=.05;if(fixture!=5)for(size_t i=0;i<l.size();++i)stereo=stereo&&std::abs(r[i]-.4*l[i])<2e-6;++cases;
        if(csv)std::fprintf(csv,"%.0f,%d,%.1f,%.9f,%.9f,%.9f,%.9f,%.6f\n",fs,fixture,ceiling,inputPeak,inputTp,outputPeak,outputTp,over);
    }
    if(csv)std::fclose(csv);check(samples,"all limiter fixtures respect sample ceiling");check(truePeak,"independent 16x reconstructed peaks stay <= ceiling +0.05 dB",worstOver);check(stereo,"limiter links stereo gain without moving the image");check(worstInputOver>2.8,"fixtures contain genuine intersample overs above sample peaks",worstInputOver);std::printf("TRUE PEAK %d cases; worst output relative to ceiling %.6f dB\n",cases,worstOver);
}
void dynamicsAndStereo(){using namespace test;using namespace gillnext;
    auto p=neutral();p.compEnabled=true;p.comp=100;auto d=std::make_unique<FinishDSP>();d->setParameters(p);d->prepare(48000,257,2);auto l=sine(48000,96000,317,.4),r=l;run(*d,l,r,257);check(d->compressorReductionDb()>4&&d->compressorReductionDb()<6,"bus compression has finite meaningful reduction on a loud signal",d->compressorReductionDb());
    p=neutral();p.stereoEnabled=true;p.width=100;p.bassMonoHz=150;d->setParameters(p);d->reset();l=sine(48000,48000,40,.3);r=l;for(auto&x:r)x=-x;run(*d,l,r,257);const double bass=rms(l,24000);d->reset();l=sine(48000,48000,4000,.3);r=l;for(auto&x:r)x=-x;run(*d,l,r,257);check(bass<.02&&rms(l,24000)>.20,"bass mono removes low side energy while preserving high stereo energy",bass);
    p.width=0;d->setParameters(p);d->reset();l=sine(48000,48000,1000,.4);r=sine(48000,48000,271,.2);run(*d,l,r,257);check(difference(l,r)==0,"zero width returns equal channels");
    p=neutral();p.limiterEnabled=true;p.ceilingDb=-1;d->setParameters(p);d->reset();l=sine(48000,96000,12000,1.3);l.resize(l.size()+d->latencySamples()+96,0);r=l;run(*d,l,r,257);const double tp=reconstructedPeak(l);check(std::abs(20*std::log10(d->maximumTruePeak()/tp))<.15,"8x maximum meter agrees with independent 16x reconstruction over the same interval",20*std::log10(d->maximumTruePeak()/tp));
    std::fill(l.begin(),l.end(),0.f);r=l;run(*d,l,r,257);d->resetPeakStatistics();run(*d,l,r,257);check(d->maximumTruePeak()<1e-7,"peak-statistics reset leaves the processing chain running");
}
void automation(){using namespace test;using namespace gillnext;
    bool invariant=true,finite=true,noalloc=true;double worst=0;
    for(double fs:rates){auto a=std::make_unique<FinishDSP>(),b=std::make_unique<FinishDSP>();a->prepare(fs,64,2);b->prepare(fs,64,2);std::array<float,64>left{},right{},refL{},refR{};FinishParameters p;
        const auto before=allocations.load();
        for(int step=0;step<101;++step){p.driveDb=step*.18f;p.ceilingDb=-3+step*.03f;p.comp=float(step);p.clip=float(100-step);p.width=step*1.5f;p.bassMonoHz=20+step*2.3f;p.lowDb=-6+step*.12f;p.midDb=-p.lowDb;p.highDb=p.lowDb;p.toneEnabled=step%7!=0;p.compEnabled=step%9!=0;p.clipEnabled=step%11!=0;p.stereoEnabled=step%13!=0;p.limiterEnabled=step%17!=0;a->setParameters(p);b->setParameters(p);
            for(int i=0;i<64;++i){left[i]=refL[i]=float(.2*std::sin((step*64+i)*.19));right[i]=refR[i]=float(.15*std::sin((step*64+i)*.27));}float* ptr[]{left.data(),right.data()};a->process(ptr,2,64);for(int i=0;i<64;++i){float* q[]{refL.data()+i,refR.data()+i};b->process(q,2,1);}for(int i=0;i<64;++i){worst=std::max(worst,std::abs(double(left[i])-refL[i]));invariant=invariant&&left[i]==refL[i]&&right[i]==refR[i];finite=finite&&std::isfinite(left[i])&&std::isfinite(right[i])&&std::abs(left[i])<32&&std::abs(right[i])<32;}
        }
        noalloc=noalloc&&before==allocations.load();
    }
    check(invariant,"all control steps and module switches are block-size invariant",worst);check(finite&&noalloc,"automated 0..100 travel remains finite without allocating");
    auto d=std::make_unique<FinishDSP>();FinishParameters p;p.driveDb=12;p.comp=80;p.clip=70;p.width=140;p.lowDb=5;p.midDb=-5;p.highDb=4;d->setParameters(p);d->prepare(48000,257,2);auto l=sine(48000,96000,317,.6),r=l;run(*d,l,r,257);d->setParameters(neutral());l=sine(48000,96000,317,.6);auto original=l;r=l;run(*d,l,r,257);bool dry=true;for(size_t i=90000;i<l.size();++i)dry=dry&&l[i]==original[i-d->latencySamples()];check(dry,"switching all modules off settles to exact delayed dry");
}
}
int main(){using namespace gillnext;FinishParameters p;p.driveDb=4;p.comp=40;p.clip=30;p.lowDb=1;p.highDb=2;test::common<FinishDSP>(p,"FINISH");neutralAndTone();limiting();dynamicsAndStereo();automation();return test::result();}
