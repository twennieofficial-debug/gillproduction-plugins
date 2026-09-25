#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <vector>
static std::atomic<size_t> allocations{0};
void* operator new(size_t n){allocations.fetch_add(1);if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
namespace test {
constexpr double pi=3.14159265358979323846;
constexpr double rates[]{22050,32000,44100,48000,96000,192000};
int checks=0,failures=0;
void check(bool ok,const char* name,double metric=0){++checks;if(!ok)++failures;std::printf("%s %s %.10g\n",ok?"PASS":"FAIL",name,metric);std::fflush(stdout);}
std::vector<float> sine(double fs,int n,double frequency,double level){std::vector<float>x(n);for(int i=0;i<n;++i)x[i]=float(level*std::sin(2*pi*frequency*i/fs));return x;}
double rms(const std::vector<float>&x,size_t start){double power=0;for(size_t i=start;i<x.size();++i)power+=double(x[i])*x[i];return std::sqrt(power/std::max<size_t>(1,x.size()-start));}
double amplitude(const std::vector<float>&x,size_t start,double f,double fs){double re=1,im=0,a=0,b=0,cr=std::cos(2*pi*f/fs),ci=std::sin(2*pi*f/fs);for(size_t i=start;i<x.size();++i){a+=x[i]*re;b+=x[i]*im;const double r=re*cr-im*ci;im=im*cr+re*ci;re=r;}return 2*std::hypot(a,b)/(x.size()-start);}
double peak(const std::vector<float>&x){double p=0;for(auto s:x)p=std::max(p,std::abs(double(s)));return p;}
double difference(const std::vector<float>&a,const std::vector<float>&b){double d=0;for(size_t i=0;i<a.size();++i)d=std::max(d,std::abs(double(a[i])-b[i]));return d;}
template<class DSP>void run(DSP&d,std::vector<float>&l,std::vector<float>&r,int block,const std::vector<float>*sc=nullptr){
    for(size_t p=0;p<l.size();p+=block){float*audio[]{l.data()+p,r.data()+p};const float* sidechain[]{sc?sc->data()+p:nullptr};d.process(audio,2,int(std::min<size_t>(block,l.size()-p)),sc?sidechain:nullptr,sc?1:0);}
}
template<class DSP,class Parameters>void common(const Parameters&p,const char*name){
    bool invariant=true,reset=true,stereo=true,safe=true,noalloc=true;double worst=0;
    for(double fs:rates){
        const auto original=sine(fs,8192,317,.21);auto a=original,b=original,c=original,d=original,sc=sine(fs,8192,1700,.3);
        auto x=std::make_unique<DSP>();auto y=std::make_unique<DSP>();x->setParameters(p);y->setParameters(p);x->prepare(fs,511,2);y->prepare(fs,511,2);
        run(*x,a,b,1,&sc);run(*y,c,d,257,&sc);const double diff=difference(a,c);worst=std::max(worst,diff);invariant=invariant&&diff==0;stereo=stereo&&difference(a,b)==0;
        y->reset();c=d=original;run(*y,c,d,63,&sc);reset=reset&&difference(a,c)==0;
        std::array<float,257> left{},right{},side{};float*ptr[]{left.data(),right.data()};const float*sp[]{side.data()};
        const auto before=allocations.load();
        for(int n=0;n<200;++n){for(int i=0;i<257;++i){left[i]=float(std::sin((n*257+i)*.07)*.1);right[i]=left[i];side[i]=left[i];}if(n%9==0)left[n%257]=std::numeric_limits<float>::quiet_NaN();if(n%11==0)right[n%257]=std::numeric_limits<float>::infinity();x->setParameters(p);x->process(ptr,2,257,sp,1);for(int i=0;i<257;++i)safe=safe&&std::isfinite(left[i])&&std::isfinite(right[i]);}
        noalloc=noalloc&&before==allocations.load();
    }
    std::printf("COMMON %s\n",name);check(invariant,"six rates: block sizes 1/257 are sample-identical",worst);check(reset,"reset reproduces initial processing with a different buffer size");check(stereo,"equal stereo inputs receive identical processing");check(safe,"NaN/infinite input does not poison processing");check(noalloc,"audio processing and parameter update allocate no C++ heap memory");
}
int result(){std::printf("RESULT %d checks, %d failures\n",checks,failures);return failures?1:0;}
}
