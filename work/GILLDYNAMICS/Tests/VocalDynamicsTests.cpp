#include "../Source/VocalDynamicsDSP.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>
#include <string>

static std::atomic<size_t> allocations{0};
void* operator new(size_t n){allocations.fetch_add(1);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
namespace {
int failures=0,checks=0;
constexpr double pi=3.14159265358979323846;
void check(bool ok,const char* label,double value=0){
    ++checks;if(!ok)++failures;std::printf("%s %s %.9g\n",ok?"PASS":"FAIL",label,value);std::fflush(stdout);
}
template<class Engine> void run(Engine& e,std::vector<float>& l,std::vector<float>& r,int block){
    for(size_t p=0;p<l.size();p+=block){
        float* ptr[]{l.data()+p,r.data()+p};e.process(ptr,int(std::min<size_t>(block,l.size()-p)),2);
    }
}
template<class Engine> void constant(Engine& e,float level,int samples,int block=127){
    std::array<float,1024> left{},right{};float* ptr[]{left.data(),right.data()};
    for(int p=0;p<samples;p+=block){const int n=std::min(block,samples-p);left.fill(level);right.fill(level);e.process(ptr,n,2);}
}
std::vector<float> fixture(double fs,int count){
    std::vector<float> x(count);for(int i=0;i<count;++i)x[i]=float(.37*std::sin(2*pi*233*i/fs)+.12*std::sin(2*pi*1731*i/fs));return x;
}
double rms(const std::vector<float>& x,size_t begin=0){
    double p=0;for(size_t i=begin;i<x.size();++i)p+=double(x[i])*x[i];return std::sqrt(p/(x.size()-begin));
}
void identity(){
    bool exact=true,delayOK=true;
    for(double fs:{8000.,22050.,44100.,48000.,96000.,192000.,384000.})
      for(int block:{1,7,64,511}){
        gilldyn::VoxDSP vox;gilldyn::OptaDSP opta;
        vox.setParameters({-90,0,0});opta.setParameters({0,0,false,100,false});
        vox.prepare(fs,block,2);opta.prepare(fs,block,2);
        auto original=fixture(fs,4096),l=original,r=original;
        run(vox,l,r,block);const int delay=int(std::ceil(fs*.00075));
        delayOK=delayOK&&vox.latencySamples()==delay&&opta.latencySamples()==0;
        for(size_t i=0;i<l.size();++i){const float expected=i<size_t(delay)?0:original[i-delay];exact=exact&&l[i]==expected&&r[i]==expected;}
        l=r=original;run(opta,l,r,block);exact=exact&&l==original&&r==original;
    }
    check(exact,"CLEAN START bit-exact: 7 rates x 4 block sizes");
    check(delayOK,"VOX reported ceil(0.75 ms), OPTA zero latency");
}
void staticCalibration(){
    constexpr float level=.18f;
    const double excess=20*std::log10(double(level))+28.;
    for(float amount:{0.f,10.f,25.f,50.f,75.f,100.f}){
        gilldyn::VoxDSP vox;vox.setParameters({-90,amount,0});vox.prepare(48000,127,2);constant(vox,level,96000);
        const double expected=.9*excess*amount*.01;
        const double output=level*std::pow(10.,(5.85*amount*.01-expected)/20.);
        check(std::abs(vox.reductionDb()-expected)<.002,"VOX steady reduction calibrated in dB",vox.reductionDb()-expected);
        check(std::abs(vox.outputRms()/output-1)<.002,"VOX fixed reference makeup calibrated",vox.outputRms()/output);
        for(bool limit:{false,true}){
            gilldyn::OptaDSP opta;opta.setParameters({3,amount,limit,0,false});opta.prepare(48000,127,2);constant(opta,level,96000);
            const double expectedOpta=(limit?.94:.75)*excess*amount*.01;
            const double expectedOutput=level*std::pow(10.,(3-expectedOpta)/20.);
            check(std::abs(opta.reductionDb()-expectedOpta)<.003,"OPTA steady reduction calibrated in dB",opta.reductionDb()-expectedOpta);
            check(std::abs(opta.outputRms()/expectedOutput-1)<.003,"OPTA manual gain independent of reduction",opta.outputRms()/expectedOutput);
        }
    }
}
void smoothStrengthCurves(){
    double worstStep=0,minStep=1e9;
    bool continuous=true;
    // The measured stationary input level is held constant while the control is
    // changed by 1%. Independent expected dB values come from the declared
    // fixed-knee slopes, not a second copy of the processing implementation.
    for(double fs:{22050.,44100.,48000.,88200.,96000.,192000.}){
        for(int kind=0;kind<3;++kind){
            double priorGR=-1,priorLevel=0;
            for(int p=0;p<=100;++p){
                double gr=0,out=0;
                if(kind==0){
                    gilldyn::VoxDSP dsp;dsp.setParameters({-90,float(p),0});dsp.prepare(fs,127,2);
                    constant(dsp,.18f,int(fs*.65));gr=dsp.reductionDb();out=dsp.outputRms();
                }else{
                    gilldyn::OptaDSP dsp;dsp.setParameters({0,float(p),kind==2,0,false});dsp.prepare(fs,127,2);
                    constant(dsp,.18f,int(fs*.65));gr=dsp.reductionDb();out=dsp.outputRms();
                }
                if(p){
                    const double step=gr-priorGR;minStep=std::min(minStep,step);
                    worstStep=std::max(worstStep,std::abs(20*std::log10(out/priorLevel)));
                    continuous=continuous&&step>0&&std::isfinite(out);
                }
                priorGR=gr;priorLevel=out;
            }
        }
    }
    check(continuous,"101 strength positions strictly increase GR: 3 modes x 6 rates",minStep);
    check(worstStep<.2,"stationary 1-percent output step below 0.2 dB",worstStep);
}
void gate(){
    gilldyn::VoxDSP dsp;dsp.setParameters({-45,0,0});dsp.prepare(48000,127,2);
    constant(dsp,.0001f,48000);
    check(dsp.gateReductionDb()>50,"gate reduces quiet background by >50 dB",dsp.gateReductionDb());
    std::vector<float> phrase(4800,.02f),r=phrase;run(dsp,phrase,r,7);
    const int onset=dsp.latencySamples();
    check(phrase[onset]>.0195f,"lookahead preserves >97.5 percent of first speech sample",phrase[onset]/.02);
    std::vector<float> tail(1920,.00056f);r=tail;run(dsp,tail,r,17);
    bool held=true;for(size_t i=onset;i<tail.size();++i)held=held&&std::abs(tail[i]-.00056f)<1e-7;
    check(held,"60 ms hold preserves a 40 ms quiet word ending");
    constant(dsp,.0001f,24000);
    check(dsp.gateReductionDb()>49,"gate closes naturally after speech and hold",dsp.gateReductionDb());
    dsp.setParameters({-90,0,0});constant(dsp,.0001f,24000);
    check(std::abs(dsp.gateReductionDb())<1e-5,"gate minimum fully disables expansion",dsp.gateReductionDb());
}
void limiter(){
    bool bounded=true,stereo=true;double peak=0;
    for(double fs:{8000.,22050.,44100.,48000.,96000.,192000.,384000.}){
        gilldyn::VoxDSP dsp;dsp.setParameters({-90,0,12});dsp.prepare(fs,1,2);
        std::vector<float> l(4096),r;
        for(size_t i=0;i<l.size();++i)l[i]=i%113==0?32.f:i%37==0?-7.f:float(.85*std::sin(2*pi*973*i/fs));
        r=l;run(dsp,l,r,17);
        for(size_t i=0;i<l.size();++i){bounded=bounded&&std::isfinite(l[i])&&std::abs(l[i])<=gilldyn::VoxDSP::ceiling+1e-7;stereo=stereo&&l[i]==r[i];peak=std::max(peak,std::abs(double(l[i])));}
    }
    check(bounded,"VOX limiter bounds samples at -1 dBFS including impulses/boost",peak);
    check(stereo,"VOX limiter links stereo without altering centered image");
}
void attackRelease(){
    for(int kind=0;kind<3;++kind){
        double at1=0,at10=0,at50=0,at100=0,release100=0,release500=0,settled=0;
        if(kind==0){
            gilldyn::VoxDSP e;e.setParameters({-90,100,0});e.prepare(48000,48,2);
            constant(e,.2f,48);at1=e.reductionDb();constant(e,.2f,432);at10=e.reductionDb();
            constant(e,.2f,1920);at50=e.reductionDb();constant(e,.2f,2400);at100=e.reductionDb();
            constant(e,.2f,91200);settled=e.reductionDb();constant(e,0,4800);release100=e.reductionDb();constant(e,0,19200);release500=e.reductionDb();
        }else{
            gilldyn::OptaDSP e;e.setParameters({0,100,kind==2,0,false});e.prepare(48000,48,2);
            constant(e,.2f,48);at1=e.reductionDb();constant(e,.2f,432);at10=e.reductionDb();
            constant(e,.2f,1920);at50=e.reductionDb();constant(e,.2f,2400);at100=e.reductionDb();
            constant(e,.2f,91200);settled=e.reductionDb();constant(e,0,4800);release100=e.reductionDb();constant(e,0,19200);release500=e.reductionDb();
        }
        std::printf("ENVELOPE kind %d attack 1/10/50/100 ms %.5f %.5f %.5f %.5f; steady %.5f; release 100/500ms %.5f %.5f dB\n",kind,at1,at10,at50,at100,settled,release100,release500);
        check(at1>=0&&at1<at10&&at10<at50&&at100>settled*.98,"attack rises smoothly toward calibrated gain reduction");
        check(release100<settled&&release500<release100&&release100>0,"release reduces attenuation progressively");
    }
    double shortTail=0,longTail=0;
    for(int length:{1440,96000}){
        gilldyn::OptaDSP e;e.setParameters({0,100,false,0,false});e.prepare(48000,127,2);
        constant(e,.5f,length);constant(e,0,38400);
        if(length==1440)shortTail=e.reductionDb();else longTail=e.reductionDb();
    }
    check(longTail>shortTail*1.5&&longTail>1,"OPTA sustained audio creates a longer recovery tail",longTail/std::max(.0001,shortTail));
    std::printf("OPTA recovery at 800 ms after short/long phrase: %.5f / %.5f dB\n",shortTail,longTail);
}
void hfAndNoise(){
    auto render=[](double hz,float hf){
        gilldyn::OptaDSP dsp;dsp.setParameters({0,70,false,hf,false});dsp.prepare(48000,127,2);
        std::vector<float> x(48000),r;for(size_t i=0;i<x.size();++i)x[i]=float(.2*std::sin(2*pi*hz*i/48000));r=x;run(dsp,x,r,127);return dsp.reductionDb();
    };
    const double highDelta=render(6000,100)-render(6000,0),lowDelta=render(100,100)-render(100,0);
    check(highDelta>3&&lowDelta<.5,"HF weighting targets high frequencies in the detector",highDelta);
    gilldyn::OptaDSP e;e.setParameters({0,0,false,0,false});e.prepare(48000,127,2);
    std::vector<float> l(24000,0),r=l;run(e,l,r,127);
    check(rms(l)==0,"noise OFF is exact silence");
    e.setParameters({0,0,false,0,true});e.reset();l.assign(24000,0);r=l;run(e,l,r,127);
    const double level=rms(l);const auto first=l;
    check(level>1e-6&&level<2e-5,"optional noise has a bounded very low RMS",20*std::log10(level));
    e.reset();l.assign(24000,0);r=l;run(e,l,r,17);
    check(first==l,"noise reset is deterministic across buffer sizes");
}
template<class Engine,class Parameters> void stateAndBlocks(const Parameters& p,const char* label){
    Engine first,second;first.setParameters(p);second.setParameters(p);first.prepare(48000,17,2);second.prepare(48000,1024,2);
    const auto source=fixture(48000,10000);auto l=source,r=source,a=source,b=source;
    run(first,l,r,17);run(second,a,b,1024);check(l==a&&r==b,label);
    first.setParameters(Parameters{});constant(first,.9f,128);first.setParameters(p);first.reset();
    a=b=source;run(first,a,b,127);check(a==l&&b==r,"parameter snapshot + reset recalls deterministic audio");
    first.reset();a=source;b.assign(a.size(),0);run(first,a,b,37);
    check(rms(b)==0,"silent stereo channel has no crosstalk with noise OFF");
}
void stress(){
    bool finite=true;
    gilldyn::VoxDSP vox;gilldyn::OptaDSP opta;
    std::array<float,257> l{},r{};float* ptr[]{l.data(),r.data()};
    size_t before=0,after=0;
    for(double fs:{8000.,22050.,44100.,48000.,96000.,384000.}){
        vox.prepare(fs,257,2);opta.prepare(fs,257,2);
        before+=allocations.load();
        for(int k=0;k<2000;++k){
            const float p=float(k%101);
            vox.setParameters({k%2?-90.f:-45.f,p,float((k%37)-24)});
            opta.setParameters({float((k%41)-20),p,k%2==0,float((k*13)%101),k%11==0});
            const int frames=1+(k*29)%257;
            for(int i=0;i<frames;++i){
                l[i]=float(.8*std::sin((k*257+i)*.103));r[i]=l[i]*.5f;
                if(k%43==0&&i==0)l[i]=std::numeric_limits<float>::quiet_NaN();
                if(k%53==0&&i==1)r[i]=std::numeric_limits<float>::infinity();
            }
            vox.process(ptr,frames,2);
            for(int i=0;i<frames;++i)finite=finite&&std::isfinite(l[i])&&std::abs(l[i])<.892f&&std::isfinite(r[i]);
            opta.process(ptr,frames,2);
            for(int i=0;i<frames;++i)finite=finite&&std::isfinite(l[i])&&std::abs(l[i])<=32&&std::isfinite(r[i]);
        }
        after+=allocations.load();
    }
    check(finite,"12000 varied parameter/buffer/fault configurations remain finite");
    check(before==after,"no allocation in setParameters or audio processing",double(after-before));
    vox.setParameters({std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-999});
    opta.setParameters({std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),false,999,false});
    vox.reset();opta.reset();l.fill(0);r.fill(0);
    vox.process(ptr,257,2);opta.process(ptr,257,2);
    check(std::all_of(l.begin(),l.end(),[](float x){return x==0;}),"invalid parameters sanitize and reset clears delay/history");
    vox.process(nullptr,100,2);opta.process(nullptr,100,2);vox.process(ptr,0,2);opta.process(ptr,100,0);
    float* partial[]{nullptr,r.data()};vox.process(partial,1,2);opta.process(partial,1,2);
}
void meters(){
    gilldyn::VoxDSP v;v.setParameters({-90,0,0});v.prepare(48000,127,2);
    gilldyn::OptaDSP o;o.setParameters({0,0,false,0,false});o.prepare(48000,127,2);
    constant(v,.25f,144000);constant(o,.25f,144000);
    check(std::abs(v.inputRms()-.25)<.00002&&std::abs(v.outputRms()-.25)<.00002,"VOX 300 ms meters report linear RMS");
    check(std::abs(o.inputRms()-.25)<.00002&&std::abs(o.outputRms()-.25)<.00002,"OPTA 300 ms meters report linear RMS");
}
}
int main(){
    const auto start=std::chrono::steady_clock::now();
    identity();staticCalibration();smoothStrengthCurves();gate();limiter();attackRelease();hfAndNoise();
    stateAndBlocks<gilldyn::VoxDSP>(gilldyn::VoxParameters{-48,65,-2},"VOX sample-exact arbitrary-block processing");
    stateAndBlocks<gilldyn::OptaDSP>(gilldyn::OptaParameters{4,67,true,72,false},"OPTA sample-exact arbitrary-block processing");
    stress();meters();
    std::printf("RESULT %d checks, %d failures, %.3f seconds\n",checks,failures,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    return failures?1:0;
}

