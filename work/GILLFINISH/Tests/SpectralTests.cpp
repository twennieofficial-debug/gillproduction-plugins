#include "../Source/SpectralDSP.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n) { ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
namespace {
constexpr double pi = gillfinish::spectral_detail::pi;
int failures = 0, checks = 0;
void check(bool good, const char* name, double metric = 0) { ++checks; failures += !good; std::printf("%s %-76s %.9g\n", good ? "PASS" : "FAIL", name, metric); }
float noise(std::uint32_t& seed) { seed = seed * 1664525U + 1013904223U; return static_cast<float>((seed >> 8) * (2. / 16777215.) - 1.); }
template<class DSP> void run(DSP& dsp, std::vector<float>& l, std::vector<float>& r, int block = 127, int channels = 2) {
    for (std::size_t at = 0; at < l.size(); at += static_cast<std::size_t>(block)) { float* ptr[]{l.data() + at, r.data() + at}; dsp.process(ptr, static_cast<int>(std::min(static_cast<std::size_t>(block), l.size() - at)), channels); }
}
std::vector<float> source(int n, double fs = 48000) { std::vector<float> a(static_cast<std::size_t>(n)); std::uint32_t seed = 491; for (int i = 0; i < n; ++i) a[static_cast<std::size_t>(i)] = static_cast<float>(.2 * std::sin(2*pi*431*i/fs) + .08 * std::sin(2*pi*3791*i/fs)) + .02f*noise(seed); return a; }
double tone(const std::vector<float>& a, int start, int count, double hz, double fs = 48000) { double re = 0, im = 0; for (int n = 0; n < count; ++n) { const double phase = 2*pi*hz*n/fs; re += a[static_cast<std::size_t>(start+n)]*std::cos(phase); im += a[static_cast<std::size_t>(start+n)]*std::sin(phase); } return 2*std::hypot(re,im)/count; }
double energy(const std::vector<float>& a, int start, int count) { double sum = 0; for (int n = start; n < start+count; ++n) sum += static_cast<double>(a[static_cast<std::size_t>(n)])*a[static_cast<std::size_t>(n)]; return sum/count; }
double db(double ratio) { return 20*std::log10(std::max(1.e-15,ratio)); }

template<class DSP> void neutral(const char* name) {
    bool exact = true, latencyCorrect = true;
    for (double fs : {32000.,44100.,48000.,96000.,192000.}) for (int channels : {1,2}) for (int route : {0,1}) {
        auto dsp = std::make_unique<DSP>(); typename DSP::Parameters p; p.depth = route ? 100.f : 0.f; p.mix = route ? 0.f : 1.f; dsp->setParameters(p); dsp->prepare(fs, 511, channels);
        const int lat = dsp->latencySamples(), size = lat+4096; auto left = source(size, fs), right = left, original = left;
        run(*dsp,left,right,channels==1?1:511,channels);
        for (int n = 0; n < size; ++n) { const float expected = n < lat ? 0.f : original[static_cast<std::size_t>(n-lat)]; exact = exact && left[static_cast<std::size_t>(n)] == expected && (channels == 1 || right[static_cast<std::size_t>(n)] == expected); }
        dsp->reset(); left.assign(static_cast<std::size_t>(size),0); right = left; left[0] = .25f; run(*dsp,left,right,37,channels);
        for (int n = 0; n < size; ++n) latencyCorrect = latencyCorrect && left[static_cast<std::size_t>(n)] == (n==lat?.25f:0.f);
        if (fs == 48000 && route == 0 && channels == 1) std::printf("METRIC %s latency at 48000 Hz: %d samples (%.4f ms)\n",name,lat,1000.*lat/fs);
    }
    check(exact,(std::string(name)+" depth=0 / mix=0 exact delayed unity at five rates, mono/stereo").c_str());
    check(latencyCorrect,(std::string(name)+" dry impulse lands exactly at reported latency at five rates").c_str());
}

void resonance() {
    auto dsp = std::make_unique<gillfinish::SilkDSP>(); gillfinish::SilkParameters p; p.depth=100; p.sensitivity=60; p.lowHz=200; p.highHz=10000; p.attackMs=1; p.releaseMs=80; dsp->setParameters(p); dsp->prepare(48000,127,2);
    const int n=144000,lat=dsp->latencySamples(); std::vector<float> original(static_cast<std::size_t>(n)); std::uint32_t seed=719;
    for(int i=0;i<n;++i)original[static_cast<std::size_t>(i)]=static_cast<float>(.25*std::sin(2*pi*1500*i/48000))+.045f*noise(seed);
    auto left=original,right=original;run(*dsp,left,right); const int start=96000,count=40000;
    const double attenuation=-db(tone(left,start+lat,count,1500)/tone(original,start,count,1500));
    double before=0,after=0;for(double f=650;f<=950;f+=10){const double a=tone(original,start,count,f),b=tone(left,start+lat,count,f);before+=a*a;after+=b*b;}for(double f=2200;f<=2600;f+=10){const double a=tone(original,start,count,f),b=tone(left,start+lat,count,f);before+=a*a;after+=b*b;}
    const double neighbours=-10*std::log10(after/before);
    check(attenuation>7,"Silk removes a stationary 1.5-kHz resonance, attenuation dB",attenuation);
    check(std::abs(neighbours)<2,"Silk retains broadband neighbours within 2 dB",neighbours);
    check(attenuation-neighbours>6,"Silk resonance suppression exceeds unrelated-band attenuation by 6 dB",attenuation-neighbours);
    check(dsp->reductionDb()>5 && dsp->reductionDb()<=18.01,"Silk reduction meter reflects bounded active attenuation dB",dsp->reductionDb());
    bool viewFinite=true;float maximum=0;for(float x:dsp->reductionView()){viewFinite=viewFinite&&std::isfinite(x)&&x>=0&&x<=18.01f;maximum=std::max(maximum,x);}check(viewFinite&&std::abs(maximum-dsp->reductionDb())<1.e-4f,"Silk 128-point reduction display agrees with actual mask");
    // A focus band that excludes the resonance must leave it intact.
    dsp->reset();p.lowHz=4000;p.highHz=10000;dsp->setParameters(p);dsp->reset();left=original;right=original;run(*dsp,left,right);
    const double excluded=-db(tone(left,start+lat,count,1500)/tone(original,start,count,1500));check(std::abs(excluded)<.1,"Silk focus excludes low resonance (change below 0.1 dB)",excluded);
}

void transient() {
    const int n=144000,clickAt=60000;
    std::vector<float> baseline(static_cast<std::size_t>(n)); for(int i=0;i<n;++i)baseline[static_cast<std::size_t>(i)]=static_cast<float>(.15*std::sin(2*pi*375*i/48000)+.08*std::sin(2*pi*4500*i/48000));
    auto clicks=baseline;for(int at:{clickAt,84000,108000})clicks[static_cast<std::size_t>(at)]+=.7f;
    std::array<double,2> changes{};
    for(int mode=0;mode<2;++mode){
        auto a=std::make_unique<gillfinish::SparkDSP>(),b=std::make_unique<gillfinish::SparkDSP>();gillfinish::SparkParameters p;p.depth=100;p.sensitivity=85;p.lowHz=1000;p.highHz=18000;p.attackMs=.1f;p.decayMs=20;p.mode=mode;a->setParameters(p);b->setParameters(p);a->prepare(48000,127,2);b->prepare(48000,127,2);const int lat=a->latencySamples();
        auto left=clicks,right=clicks,clean=baseline,cleanRight=baseline;run(*a,left,right);run(*b,clean,cleanRight);
        std::vector<float> difference(left.size());for(std::size_t i=0;i<left.size();++i)difference[i]=left[i]-clean[i];
        const double clickEnergy=energy(difference,clickAt+lat-512,2048)*2048;
        changes[static_cast<std::size_t>(mode)]=10*std::log10(clickEnergy/(.7*.7));
        check(mode?changes[1]>2:changes[0]<-2,mode?"Spark BOOST increases transient energy by more than 2 dB":"Spark CUT lowers click energy by more than 2 dB",changes[static_cast<std::size_t>(mode)]);
        const double held=db(tone(clean,96000+lat,40000,4500)/tone(baseline,96000,40000,4500));check(std::abs(held)<.15,mode?"Spark BOOST preserves steady in-focus 4.5-kHz tone":"Spark CUT preserves steady in-focus 4.5-kHz tone",held);
        const double low=db(tone(left,96000+lat,40000,375)/tone(baseline,96000,40000,375));check(std::abs(low)<.1,mode?"Spark BOOST preserves out-of-focus low tone":"Spark CUT preserves out-of-focus low tone",low);
    }
    check(changes[1]-changes[0]>5,"Spark CUT and BOOST produce materially different transient responses dB",changes[1]-changes[0]);
    // A short high-frequency burst stands in for a mouth click rather than a single sample.
    auto dsp=std::make_unique<gillfinish::SparkDSP>();gillfinish::SparkParameters p;p.depth=100;p.sensitivity=85;p.attackMs=.1f;p.decayMs=20;dsp->setParameters(p);dsp->prepare(48000,127,1);auto burst=baseline;
    for(int i=0;i<96;++i)burst[static_cast<std::size_t>(clickAt+i)]+=static_cast<float>(.4*std::sin(pi*i/96)*std::sin(2*pi*9000*i/48000));auto copy=burst;run(*dsp,burst,copy,127,1);
    const int lat=dsp->latencySamples();const double highChange=db(tone(burst,clickAt+lat-256,1024,9000)/tone(copy,clickAt-256,1024,9000));check(highChange<-3,"Spark suppresses a 2-ms high-frequency mouth-click surrogate dB",highChange);
}

template<class DSP> void robustness(const char* name) {
    bool stable=true,silent=true,noAlloc=true,matching=true,isolated=true;
    for(double fs:{32000.,44100.,48000.,96000.,192000.}) {
        auto a=std::make_unique<DSP>(),b=std::make_unique<DSP>();typename DSP::Parameters p;p.depth=92;p.sensitivity=80;a->setParameters(p);b->setParameters(p);a->prepare(fs,1,2);b->prepare(fs,1024,2);
        auto left=source(24000,fs),right=left,other=left,otherRight=left;run(*a,left,right,1);run(*b,other,otherRight,1024);matching=matching&&left==other&&right==otherRight&&left==right;
        a->reset();left=source(24000,fs);right.assign(left.size(),0);run(*a,left,right);for(float x:right)isolated=isolated&&x==0;
        a->reset();left.assign(20000,0);right=left;run(*a,left,right);for(float x:left)silent=silent&&x==0;
        std::array<float,257> l{},r{};float* ptr[]{l.data(),r.data()};std::uint32_t seed=2;
        const auto before=allocations.load();
        for(int block=0;block<240;++block){p.depth=static_cast<float>(block%101);p.sensitivity=100-p.depth;p.mix=static_cast<float>(block%7)/6;p.outputDb=block%2?-18.f:6.f;p.lowHz=block%2?20.f:18000.f;p.highHz=block%2?20000.f:200.f;a->setParameters(p);for(std::size_t i=0;i<l.size();++i){l[i]=noise(seed);r[i]=-.7f*l[i];}if(block==3){l[0]=std::numeric_limits<float>::infinity();l[1]=std::numeric_limits<float>::quiet_NaN();l[2]=-std::numeric_limits<float>::max();}a->process(ptr,1+block%257,2);for(float x:l)stable=stable&&std::isfinite(x)&&std::abs(x)<=32.001f;for(float x:r)stable=stable&&std::isfinite(x)&&std::abs(x)<=32.001f;}
        noAlloc=noAlloc&&allocations.load()==before;
        p.depth=std::numeric_limits<float>::quiet_NaN();p.mix=std::numeric_limits<float>::infinity();p.lowHz=std::numeric_limits<float>::infinity();a->setParameters(p);a->process(ptr,257,2);for(float x:l)stable=stable&&std::isfinite(x);
        a->reset();l.fill(0);r.fill(0);a->process(nullptr,257,2);a->process(ptr,0,2);a->process(ptr,-1,2);a->process(ptr,257,0);float* nullChannel[]{nullptr,r.data()};a->process(nullChannel,257,2);
    }
    check(matching,(std::string(name)+" 1/1024-sample blocks and stereo matching are sample-exact").c_str());
    check(isolated,(std::string(name)+" linked detector does not leak audio into silent channel").c_str());
    check(silent,(std::string(name)+" reset and digital silence are exactly silent at five rates").c_str());
    check(stable,(std::string(name)+" automation, reversed focus, NaN/Inf and extreme samples stay finite/bounded").c_str());
    check(noAlloc,(std::string(name)+" process and parameter automation allocate no heap memory").c_str());
}

template<class DSP> void speed(const char* name) {
    auto dsp=std::make_unique<DSP>();typename DSP::Parameters p;p.depth=70;p.sensitivity=70;dsp->setParameters(p);dsp->prepare(48000,128,2);std::array<float,128> l{},r{};float* ptr[]{l.data(),r.data()};
    constexpr int frames=48000*4;const auto start=std::chrono::steady_clock::now();for(int at=0;at<frames;at+=128){for(int i=0;i<128;++i)l[static_cast<std::size_t>(i)]=r[static_cast<std::size_t>(i)]=static_cast<float>(.3*std::sin(2*pi*1700*(at+i)/48000));dsp->process(ptr,128,2);}const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::printf("BENCH %s stereo 48k/128, 4s audio: %.6f seconds, %.3f%% realtime budget\n",name,elapsed,elapsed/4*100);
}
template<class DSP> void mixing(const char* name) {
    auto wet=std::make_unique<DSP>(),mixed=std::make_unique<DSP>();typename DSP::Parameters p;p.depth=85;p.sensitivity=80;p.mix=1;wet->setParameters(p);p.mix=.37f;mixed->setParameters(p);wet->prepare(48000,127,2);mixed->prepare(48000,127,2);const int lat=wet->latencySamples();
    const auto original=source(96000);auto a=original,ar=original,b=original,br=original;run(*wet,a,ar);run(*mixed,b,br);double maximum=0;
    for(std::size_t n=0;n<a.size();++n){const double dry=n<static_cast<std::size_t>(lat)?0.:original[n-static_cast<std::size_t>(lat)];maximum=std::max(maximum,std::abs(b[n]-(dry+.37f*(a[n]-dry))));}
    check(maximum<1.e-7,(std::string(name)+" wet/dry mix matches phase-aligned linear interpolation").c_str(),maximum);
    p.outputDb=-12;p.mix=1;mixed->setParameters(p);auto settling=source(24000),sr=settling;run(*mixed,settling,sr);p.outputDb=0;p.mix=0;mixed->setParameters(p);a=source(48000);ar=a;const auto input=a;run(*mixed,a,ar);bool exact=true;
    for(int n=24000;n<48000;++n)exact=exact&&a[static_cast<std::size_t>(n)]==input[static_cast<std::size_t>(n-lat)];
    check(exact,(std::string(name)+" automated mix=0/output=0 settles to exact delayed dry").c_str());
}
}
int main(){
#if defined(_MSC_VER)
    std::printf("COMPILER MSVC %d; C++17; pointer bits %zu\n",_MSC_VER,sizeof(void*)*8);
#endif
    neutral<gillfinish::SilkDSP>("Silk");neutral<gillfinish::SparkDSP>("Spark");resonance();transient();robustness<gillfinish::SilkDSP>("Silk");robustness<gillfinish::SparkDSP>("Spark");mixing<gillfinish::SilkDSP>("Silk");mixing<gillfinish::SparkDSP>("Spark");speed<gillfinish::SilkDSP>("Silk");speed<gillfinish::SparkDSP>("Spark");std::printf("RESULT %d checks, %d failures\n",checks,failures);return failures?1:0;
}
