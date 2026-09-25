#include "../Source/TuneDSP.h"
#ifndef GILL_TUNE_TEST_LIVE
#define GILL_TUNE_TEST_LIVE 0
#endif
class TestTuneDSP:public gill::TuneDSP { public: TestTuneDSP(){setQualityMode(GILL_TUNE_TEST_LIVE);} };
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>

static bool allocationWatch = false;
static size_t watchedAllocations = 0;
void* operator new(std::size_t size) {
    if (allocationWatch) ++watchedAllocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
static constexpr double pi = 3.14159265358979323846;
static void check(bool good, const std::string& name) {
    std::cout << (good ? "PASS " : "FAIL ") << name << '\n';
    if (!good) ++failures;
}
static double frequency(int note) { return 440.0 * std::exp2((note - 69) / 12.0); }
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
static std::vector<float> tone(double fs, double hz, double seconds, bool vowel = false) {
    std::vector<float> result(static_cast<size_t>(fs * seconds));
    for (size_t i = 0; i < result.size(); ++i) {
        double x = 0;
        for (int h = 1; h <= (vowel ? 18 : 1) && h * hz < fs * 0.45; ++h) {
            const double f = h * hz;
            const double envelope = vowel ? 0.18 + 2.0 * std::exp(-std::pow((f - 700.0)/220.0, 2)) +
                1.4 * std::exp(-std::pow((f - 1200.0)/300.0, 2)) + 0.8 * std::exp(-std::pow((f - 2500.0)/450.0, 2)) : 1.0;
            x += std::sin(2*pi*f*i/fs) * envelope / h;
        }
        result[i] = static_cast<float>(0.16*x);
    }
    return result;
}
static void process(TestTuneDSP& dsp, std::vector<float>& left, int block, std::vector<float>* right = nullptr) {
    for (int offset = 0; offset < static_cast<int>(left.size()); offset += block) {
        float* pointers[]{left.data()+offset, right ? right->data()+offset : nullptr};
        allocationWatch = true;
        dsp.process(pointers, right ? 2 : 1, std::min(block, static_cast<int>(left.size())-offset));
        allocationWatch = false;
    }
}
// Independent output estimator: least squared period, confined to +/-8% of the
// expected fundamental, using the last stable 0.25 s. Does not reuse engine YIN.
static double measure(const std::vector<float>& data, double fs, double expected) {
    const int centre = static_cast<int>(fs / expected);
    const int lo = std::max(2, static_cast<int>(centre*0.92));
    const int hi = static_cast<int>(centre*1.08)+2;
    const int length = std::min(static_cast<int>(fs*0.25), static_cast<int>(data.size())-hi-1);
    const int start = static_cast<int>(data.size())-length-hi-1;
    std::vector<double> values(hi+2);
    int best = lo;
    for (int lag = lo; lag <= hi; ++lag) {
        double d = 0;
        for (int i = 0; i < length; ++i) { const double x = data[start+i]-data[start+i+lag]; d += x*x; }
        values[lag] = d;
        if (d < values[best]) best = lag;
    }
    if (best <= lo || best >= hi) return 0;
    const double a=values[best-1], b=values[best], c=values[best+1];
    return fs / (best + 0.5*(a-c)/(a-2*b+c));
}
static void correctionCase(double fs, int block, int note, double detune, bool vowel) {
    TestTuneDSP dsp;
    dsp.prepare(fs, block, 1);
    dsp.setParameters(0, 0, 0, 0, 100);
    auto data = tone(fs, frequency(note)*std::exp2(detune/1200), 0.85, vowel);
    process(dsp, data, block);
    const double output = measure(data, fs, frequency(note));
    const double error = output > 0 ? cents(output, frequency(note)) : 99999;
    double peak=0; for (float x:data) peak=std::max(peak,std::abs(static_cast<double>(x)));
    std::cout << "MEASURE fs=" << fs << " block=" << block << " note=" << note << " source=" << (vowel?"vowel":"sine")
        << " detune_cents=" << detune << " detected_hz=" << dsp.detectedHz() << " target_hz=" << dsp.targetHz()
        << " output_hz=" << output << " output_error_cents=" << error << " confidence=" << dsp.confidence()
        << " latency=" << dsp.latencySamples() << " peak=" << peak << '\n';
    check(std::abs(error) < 8.0 && std::abs(cents(dsp.detectedHz(),frequency(note)*std::exp2(detune/1200))) < 8.0
        && std::abs(cents(dsp.targetHz(),frequency(note))) < 0.1 && peak < 1.0,
        "steady fundamental correction");
}

int main() {
    std::cout << std::fixed << std::setprecision(5);
    const auto start = std::chrono::steady_clock::now();
    for (double fs : {8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        correctionCase(fs, 127, 57, 37, false);
        correctionCase(fs, 512, 64, -42, true);
    }
    for (int note : {39, 45, 52, 69, 76, 82}) {
        correctionCase(48000, 64, note, 32, false);
        correctionCase(48000, 257, note, -34, true);
    }
    for (int note : {39,45,52,60,69,76,82}) {
        correctionCase(48000,128,note,47,false);
        correctionCase(48000,128,note,-47,true);
    }
    for (int block : {1, 31, 4096}) correctionCase(48000, block, 60, 39, false);

    // Every key, major and natural minor: choose a non-scale semitone near the third.
    for (int key=0; key<12; ++key) for (int scale=1; scale<=2; ++scale) {
        TestTuneDSP dsp; dsp.prepare(48000,256,1); dsp.setParameters(key,scale,0,0,100);
        const int target=60+key+(scale==1?4:3);
        const double source=frequency(target)*std::exp2((scale==1?-65:65)/1200.0);
        auto data=tone(48000,source,0.45);
        process(dsp,data,256);
        check(std::abs(cents(dsp.targetHz(),frequency(target))) < 0.1,"key/scale target key="+std::to_string(key)+" scale="+std::to_string(scale));
        const double measured=measure(data,48000,frequency(target));
        check(measured>0 && std::abs(cents(measured,frequency(target)))<8,"key/scale audio correction");
    }

    for (double fs : {8000.0,44100.0,48000.0,96000.0,384000.0}) {
        TestTuneDSP dsp; dsp.prepare(fs,31,2); dsp.setParameters(0,0,0,0,0);
        std::mt19937 rng(93); std::uniform_real_distribution<float> random(-0.2f,0.2f);
        std::vector<float> left(static_cast<size_t>(fs*0.30)),right(left.size());
        for (size_t i=0;i<left.size();++i) {left[i]=random(rng);right[i]=random(rng);}
        const auto originalLeft=left,originalRight=right;
        process(dsp,left,31,&right);
        const int latency=dsp.latencySamples();
        bool exact=true;
        for (size_t i=0;i<left.size();++i) {
            exact &= left[i]==(i>=static_cast<size_t>(latency)?originalLeft[i-latency]:0.0f);
            exact &= right[i]==(i>=static_cast<size_t>(latency)?originalRight[i-latency]:0.0f);
        }
        check(exact,"zero mix stereo exact reported latency fs="+std::to_string(fs));
        dsp.reset(); dsp.setParameters(0,0,0,0,100); left=originalLeft;right=originalRight;
        process(dsp,left,31,&right);
        exact=true;
        for (size_t i=0;i<left.size();++i) {
            exact &= left[i]==(i>=static_cast<size_t>(latency)?originalLeft[i-latency]:0.0f);
            exact &= right[i]==(i>=static_cast<size_t>(latency)?originalRight[i-latency]:0.0f);
        }
        check(exact && dsp.confidence()==0,"unvoiced noise exact delayed stereo preservation");
        dsp.reset(); std::fill(left.begin(),left.end(),0.0f);std::fill(right.begin(),right.end(),0.0f);
        process(dsp,left,31,&right);
        check(std::all_of(left.begin(),left.end(),[](float x){return x==0;}) && dsp.detectedHz()==0,"silence and reset");
    }

    { // Anti-phase stereo must not cancel the detector, and channels remain coherent.
        TestTuneDSP dsp; dsp.prepare(48000,128,2); dsp.setParameters(0,0,0,0,100);
        auto left=tone(48000,frequency(57)*std::exp2(35.0/1200),0.7,true),right=left;
        for (float& x:right) x=-x;
        process(dsp,left,128,&right);
        double error=0; for(size_t i=0;i<left.size();++i)error=std::max(error,std::abs(static_cast<double>(left[i]+right[i])));
        check(dsp.confidence()>0.9f && error<0.00005,"anti-phase stereo detection and coherence");
    }
    { // Different retune settings yield different transient correction.
        TestTuneDSP fast,slow; fast.prepare(48000,64,1);slow.prepare(48000,64,1);
        fast.setParameters(0,0,0,0,100);slow.setParameters(0,0,200,0,100);
        auto a=tone(48000,frequency(60)*std::exp2(42.0/1200),0.35),b=a;
        process(fast,a,64);process(slow,b,64);
        const double ef=std::abs(cents(measure(a,48000,frequency(60)),frequency(60)));
        const double es=std::abs(cents(measure(b,48000,frequency(60)),frequency(60)));
        std::cout<<"RETUNE fast_error="<<ef<<" slow_error="<<es<<'\n';
        check(es>ef+4,"retune time affects correction speed");
    }
    { // Humanize leaves up to 20 cents on a sustained note instead of full locking.
        TestTuneDSP dsp;dsp.prepare(48000,128,1);dsp.setParameters(0,0,0,100,100);
        auto data=tone(48000,frequency(60)*std::exp2(39.0/1200),1.3);
        process(dsp,data,128);
        const double residual=cents(measure(data,48000,frequency(60)),frequency(60));
        std::cout<<"HUMANIZE residual_cents="<<residual<<'\n';
        check(residual>14 && residual<25,"humanize relaxes sustained note");
    }
    { // Invalid values cannot contaminate the detector or audio-delay history.
        TestTuneDSP dsp;dsp.prepare(48000,64,1);
        dsp.setParameters(-900,99,std::numeric_limits<float>::quiet_NaN(),999,-99);
        std::vector<float> data(10000,0); data[0]=std::numeric_limits<float>::infinity();data[100]=std::numeric_limits<float>::quiet_NaN();
        data[200]=std::numeric_limits<float>::max();data[300]=-std::numeric_limits<float>::max();
        process(dsp,data,4096);
        check(std::all_of(data.begin(),data.end(),[](float x){return std::isfinite(x)&&std::abs(x)<=32;}),"invalid and extreme input and parameter sanitation");
    }
    for (double fs : {44100.0,48000.0,96000.0}) {
        // Real voiced signal, independently rendered wet reference and exact delayed
        // dry reference. Repeating setParameters every block must not restart ramps.
        const int block=128;
        auto source=tone(fs,frequency(60)*std::exp2(39.0/1200),0.85);
        TestTuneDSP wetDsp;wetDsp.prepare(fs,block,1);wetDsp.setParameters(0,0,0,0,100);
        auto wet=source;process(wetDsp,wet,block);
        const int latency=wetDsp.latencySamples(),ramp=static_cast<int>(std::round(fs*0.005));
        std::vector<float> dry(source.size(),0);
        for(int i=latency;i<static_cast<int>(dry.size());++i)dry[i]=source[i-latency];
        int change=static_cast<int>(fs*0.45)/block*block;
        for(int n=change;n<static_cast<int>(fs*0.60);n+=block)
            if(std::abs(wet[n]-dry[n])>std::abs(wet[change]-dry[change]))change=n;
        for(bool rising : {false,true}) {
            TestTuneDSP dsp;dsp.prepare(fs,block,1);
            dsp.setParameters(0,0,0,0,rising?0.0f:100.0f);
            auto actual=source;
            for(int offset=0;offset<static_cast<int>(actual.size());offset+=block) {
                const bool after=offset>=change;
                dsp.setParameters(0,0,0,0,(after==rising)?100.0f:0.0f);
                float* p[]{actual.data()+offset};
                allocationWatch=true;
                dsp.process(p,1,std::min(block,static_cast<int>(actual.size())-offset));
                allocationWatch=false;
            }
            double error=0;bool endpointExact=true;
            for(int i=0;i<static_cast<int>(actual.size());++i) {
                const double progress=std::clamp((i-change+1)/static_cast<double>(ramp),0.0,1.0);
                const double mix=rising?progress:1.0-progress;
                const double expected=dry[i]+mix*(wet[i]-dry[i]);
                error=std::max(error,std::abs(actual[i]-expected));
                if(i>=change+ramp)endpointExact &= actual[i]==(rising?wet[i]:dry[i]);
            }
            const double hardDelta=std::abs(wet[change]-dry[change]);
            const double firstAutomationStep=std::abs(actual[change]-(rising?dry[change]:wet[change]));
            std::cout<<"MIX_AUTOMATION fs="<<fs<<" direction="<<(rising?"0_to_100":"100_to_0")
                <<" ramp_samples="<<ramp<<" reference_error="<<error<<" hard_switch_delta="<<hardDelta
                <<" first_ramped_step="<<firstAutomationStep<<'\n';
            check(error<0.000002,"actual voiced MIX follows finite 5 ms reference ramp");
            check(hardDelta>0.05 && firstAutomationStep<hardDelta*0.01,"MIX automation suppresses the hard transition");
            check(endpointExact,"MIX reaches bit-exact delayed dry or wet endpoint");
            dsp.reset();
            auto restarted=source;process(dsp,restarted,block);
            check(restarted==(rising?wet:dry),"reset snaps MIX to existing target without startup ramp");
        }
    }
    { // Unsupported rates must not silently run at another physical clock.
        TestTuneDSP dsp;dsp.prepare(4000,64,1);auto data=tone(4000,220,0.1);const auto original=data;
        process(dsp,data,64);check(data==original && dsp.latencySamples()==0,"unsupported sample rate transparent fallback");
    }
    { // An upper-bound sample rate must also retain its actual clock.
        TestTuneDSP dsp;dsp.prepare(768000,512,1);dsp.setParameters(0,0,0,0,0);
        std::vector<float> data(65536,0);data[0]=1;process(dsp,data,512);
        check(data[dsp.latencySamples()]==1 && dsp.latencySamples()==(GILL_TUNE_TEST_LIVE?12288:32768),"768 kHz actual-rate delay");
    }
    { // Whole-engine stereo CPU measurement, informative rather than machine-specific gate.
        TestTuneDSP dsp;dsp.prepare(48000,128,2);dsp.setParameters(0,0,0,0,100);
        auto left=tone(48000,267.5,3.0,true),right=left;
        const auto then=std::chrono::steady_clock::now();process(dsp,left,128,&right);
        const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-then).count();
        std::cout<<"CPU stereo_48k_audio_seconds=3 wall_seconds="<<wall<<" realtime_fraction="<<wall/3.0<<'\n';
    }
    check(watchedAllocations==0,"no dynamic allocations during all process calls (count="+std::to_string(watchedAllocations)+")");
    std::cout<<"RESULT failures="<<failures<<" elapsed_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
    return failures ? 1 : 0;
}
