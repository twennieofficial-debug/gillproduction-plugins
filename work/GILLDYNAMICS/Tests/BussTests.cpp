#include "../Source/BussDSP.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>
static std::atomic<size_t> allocations{0};
void* operator new(size_t n){allocations.fetch_add(1);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
namespace {
int checks=0,failures=0;constexpr double pi=gilldyn::buss_detail::pi;
void check(bool ok,const char* text,double metric=0){
    ++checks;if(!ok)++failures;std::printf("%s %s %.9g\n",ok?"PASS":"FAIL",text,metric);std::fflush(stdout);
}
void run(gilldyn::BussDSP& d,std::vector<float>& l,std::vector<float>& r,int block){
    for(size_t p=0;p<l.size();p+=block){float* ptr[]{l.data()+p,r.data()+p};d.process(ptr,int(std::min<size_t>(block,l.size()-p)),2);}
}
std::vector<float> sine(double fs,int count,double f,double amp){
    std::vector<float> a(count);for(int i=0;i<count;++i)a[i]=float(amp*std::sin(2*pi*f*i/fs));return a;
}
double amplitude(const std::vector<float>& x,size_t start,size_t count,double f,double fs){
    double re=1,im=0,a=0,b=0;const double cr=std::cos(2*pi*f/fs),ci=std::sin(2*pi*f/fs);
    for(size_t i=0;i<count;++i){a+=x[start+i]*re;b+=x[start+i]*im;const double n=re*cr-im*ci;im=im*cr+re*ci;re=n;}
    return 2*std::hypot(a,b)/count;
}
void identityAndTrim(){
    bool neutral=true,bypass=true,trim=true;
    for(double fs:{8000.,22050.,44100.,48000.,96000.,192000.,384000.})
    for(int block:{1,7,64,511})for(int mode=0;mode<3;++mode){
        auto original=sine(fs,2048,271,.65),l=original,r=original;
        gilldyn::BussDSP d;d.setParameters({0,0,mode,false,false});d.prepare(fs,block,2);run(d,l,r,block);
        neutral=neutral&&d.latencySamples()==24;
        for(size_t i=0;i<l.size();++i){const float ref=i<24?0:original[i-24];neutral=neutral&&l[i]==ref&&r[i]==ref;}
        d.setParameters({24,24,mode,true,true});d.reset();l=r=original;run(d,l,r,block);
        for(size_t i=0;i<l.size();++i){const float ref=i<24?0:original[i-24];bypass=bypass&&l[i]==ref&&r[i]==ref;}
        for(float db:{-36.f,-12.f,12.f,24.f}){
            d.setParameters({0,db,mode,false,false});d.reset();l=r=original;run(d,l,r,block);
            const double gain=std::pow(10.,db/20.);
            for(size_t i=24;i<l.size();++i)trim=trim&&std::abs(l[i]-original[i-24]*gain)<2e-6;
        }
    }
    check(neutral,"neutral drive exact delayed identity: 7 rates x 4 buffers x 3 styles");
    check(bypass,"bypass also excludes trim/noise and preserves 24-sample latency");
    check(trim,"DSP trim supports -36 to +24 dB including float headroom");
}
void aliases(){
    constexpr int count=48000;
    for(int mode=0;mode<3;++mode){
        auto a=sine(48000,count*2,10000,.8),r=a,naive=a;
        gilldyn::BussDSP d;d.setParameters({24,0,mode,false,false});d.prepare(48000,127,2);run(d,a,r,127);
        const double gain=gilldyn::buss_detail::driveGain(24),norm=gilldyn::buss_detail::normalisation(gain,mode);
        const double pole=std::exp(-2*pi*8/48000);double previous=0,dc=0;
        for(auto& x:naive){const double residual=gilldyn::buss_detail::shape(x*gain,mode)/norm-x;dc=residual-previous+pole*dc;previous=residual;x=float(x+dc);}
        const double baseline=amplitude(naive,count,count,18000,48000),actual=amplitude(a,count,count,18000,48000);
        const double attenuation=20*std::log10(baseline/std::max(1e-15,actual));
        std::printf("ALIAS style %d direct %.9f oversampled %.9f suppression %.3f dB\n",mode,baseline,actual,attenuation);
        check(baseline>.001&&attenuation>30,"max-drive alias reduced >30 dB against direct shaping",attenuation);
        check(actual<.001,"max-drive 18-kHz alias below 0.001 absolute amplitude",actual);
    }
}
void curves(){
    FILE* file=std::fopen("BussCurves.csv","w");
    if(!file){check(false,"open curve CSV");return;}
    std::fprintf(file,"rate,style,fixture,percent,thd,level_db,effect_rms_relative,colour_rms_relative\n");
    bool monotonic=true,finite=true;double worstStep=0,smallestTHDStep=1e9,smallestColourStep=1e9;
    for(double fs:{22050.,44100.,48000.,88200.,96000.,192000.})for(int mode=0;mode<3;++mode)for(int kind=0;kind<2;++kind){
        const int count=int(std::llround(fs*.20)),start=int(std::llround(fs*.12)),size=int(std::llround(fs*.08));
        auto source=sine(fs,count,750,.18);
        if(kind)for(int i=0;i<count;++i){
            const double t=i/fs,envelope=.55+.45*std::sin(pi*7*t)*std::sin(pi*7*t);
            source[i]=float(envelope*(.12*std::sin(2*pi*100*t)+.095*std::sin(2*pi*750*t+.6)+.07*std::sin(2*pi*3000*t+1.7)));
        }
        double priorThd=0,priorDb=0,priorColour=0;
        for(int p=0;p<=100;++p){
            gilldyn::BussDSP d;d.setParameters({float(p*.24),0,mode,false,false});d.prepare(fs,127,2);
            auto l=source,r=source;run(d,l,r,127);
            double inPower=0,outPower=0,error=0,cross=0;
            for(int i=start;i<start+size;++i){const double dry=source[i-24],wet=l[i];inPower+=dry*dry;outPower+=wet*wet;error+=(wet-dry)*(wet-dry);cross+=dry*wet;}
            const double level=10*std::log10(outPower/inPower);
            const double colour=std::sqrt(std::max(0.,outPower-cross*cross/inPower)/inPower);
            const double effect=std::sqrt(error/inPower);
            double thd=0;
            if(!kind){
                double energy=0;for(int h=2;h<=20&&750*h<fs*.45;++h){const double a=amplitude(l,start,size,750*h,fs);energy+=a*a;}
                thd=std::sqrt(energy)/amplitude(l,start,size,750,fs);
            }
            finite=finite&&std::isfinite(level)&&std::isfinite(colour)&&std::isfinite(thd);
            if(p){
                const double step=level-priorDb;worstStep=std::max(worstStep,std::abs(step));
                smallestColourStep=std::min(smallestColourStep,colour-priorColour);
                monotonic=monotonic&&colour>priorColour;
                if(!kind){smallestTHDStep=std::min(smallestTHDStep,thd-priorThd);monotonic=monotonic&&thd>priorThd;}
            }
            if(p==100)std::printf("CURVE %.0f style %d kind %d max THD %.6f RMSlevel %.4f dB coloration %.6f\n",fs,mode,kind,thd,level,colour);
            std::fprintf(file,"%.0f,%d,%d,%d,%.9g,%.9g,%.9g,%.9g\n",fs,mode,kind,p,thd,level,effect,colour);
            priorDb=level;priorThd=thd;priorColour=colour;
        }
        std::fflush(stdout);
    }
    std::fclose(file);
    check(finite,"3636 rendered control positions finite: 3 styles x 6 rates x 2 fixtures");
    check(monotonic,"every 1-percent step increases tone THD and fixture coloration",smallestTHDStep);
    check(smallestColourStep>0,"gain-compensated coloration grows monotonically",smallestColourStep);
    check(worstStep<.15,"adjacent 1-percent RMS output changes under 0.15 dB",worstStep);
}
void styles(){
    for(int mode=0;mode<3;++mode){
        gilldyn::BussDSP d;d.setParameters({24,0,mode,false,false});d.prepare(48000,127,2);
        auto l=sine(48000,96000,750,.12),r=l;run(d,l,r,127);
        const double even=amplitude(l,48000,48000,1500,48000),odd=amplitude(l,48000,48000,2250,48000);
        double dc=0;for(int i=48000;i<96000;++i)dc+=l[i];dc/=48000;
        std::printf("STYLE %d second %.8f third %.8f DC %.9g\n",mode,even,odd,dc);
        check(mode==1?even>.0005:even<1e-7,"IRON contributes even harmonics; CLEAN/VELVET remain odd",even);
        check(odd>.001,"all three styles provide actual saturation at full drive",odd);
        check(std::abs(dc)<1e-7,"generated steady DC removed",dc);
    }
}
void blocksStateAndNoise(){
    gilldyn::BussDSP a,b;a.setParameters({13,-4,1,false,false});b.setParameters({13,-4,1,false,false});
    a.prepare(48000,17,2);b.prepare(48000,1024,2);
    const auto input=sine(48000,16000,831,.3);auto l=input,r=input,x=input,y=input;
    run(a,l,r,17);run(b,x,y,1024);
    check(l==x&&r==y&&l==r,"buffer-independent processing and centered stereo matching");
    a.reset();x=input;y.assign(x.size(),0);run(a,x,y,127);
    check(std::all_of(y.begin(),y.end(),[](float v){return v==0;}),"no crosstalk into silent channel");
    a.setParameters({24,24,2,true,true});a.reset();run(a,x,y,64);
    a.setParameters({13,-4,1,false,false});a.reset();x=y=input;run(a,x,y,511);
    check(x==l&&y==r,"parameter recall plus reset reproduces exact DSP output");
    a.setParameters({24,0,1,false,false});a.reset();x.assign(24000,0);y=x;run(a,x,y,127);
    check(std::all_of(x.begin(),x.end(),[](float v){return v==0;}),"all modes preserve exact silence with noise OFF");
    a.setParameters({0,0,0,true,false});a.reset();x.assign(24000,0);y=x;run(a,x,y,127);
    double noise=0;for(float v:x)noise+=v*v;noise=std::sqrt(noise/x.size());
    check(noise>1e-6&&noise<2e-5,"optional noise remains near -100 dBFS",20*std::log10(noise));
}
void automationAndFaults(){
    gilldyn::BussDSP d;std::array<float,257> l{},r{};float* ptr[]{l.data(),r.data()};
    bool finite=true;size_t allocationDelta=0;double maxJump=0;
    for(double fs:{8000.,22050.,44100.,48000.,96000.,384000.}){
        d.prepare(fs,257,2);const size_t before=allocations.load();
        for(int k=0;k<2000;++k){
            d.setParameters({float(k%101)*.24f,float((k%61)-36),k%3,k%7==0,k%11==0});
            const int count=1+(k*31)%257;
            l.fill(.1f);r.fill(-.05f);
            if(k%57==0)l[0]=std::numeric_limits<float>::infinity();
            if(k%79==0)r[0]=std::numeric_limits<float>::quiet_NaN();
            d.process(ptr,count,2);
            for(int i=0;i<count;++i)finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(l[i])<1024&&std::abs(r[i])<1024;
        }
        allocationDelta+=allocations.load()-before;
    }
    check(finite,"12000 parameter/buffer/fault combinations stay finite");
    check(allocationDelta==0,"zero allocation while processing or setting parameters",double(allocationDelta));
    d.setParameters({0,0,0,false,false});d.prepare(48000,64,2);double previous=0;
    for(int k=0;k<1500;++k){
        d.setParameters({k%2?24.f:0.f,k%2?24.f:-36.f,k%3,false,k%3==0});
        l.fill(.1f);r.fill(.1f);d.process(ptr,64,2);
        for(int i=0;i<64;++i){if(k>5)maxJump=std::max(maxJump,std::abs(l[i]-previous));previous=l[i];}
    }
    check(maxJump<.02,"simultaneous drive/trim/style/bypass automation is smoothed",maxJump);
    d.setParameters({std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),999,false,false});d.reset();l.fill(0);r.fill(0);d.process(ptr,257,2);
    check(std::all_of(l.begin(),l.end(),[](float v){return v==0;}),"invalid parameters sanitize; reset clears all signal history");
    d.process(nullptr,4,2);d.process(ptr,0,2);d.process(ptr,4,0);float* missing[]{nullptr,r.data()};d.process(missing,1,2);
}
}
int main(){
    const auto start=std::chrono::steady_clock::now();
    identityAndTrim();aliases();curves();styles();blocksStateAndNoise();automationAndFaults();
    std::printf("RESULT %d checks, %d failures, %.3f seconds\n",checks,failures,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    return failures?1:0;
}

