#include "../Source/RescueDSP.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>

static bool watched = false;
static size_t allocations = 0;
void* operator new(size_t size) { if (watched) ++allocations; if (auto* p = std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
using namespace gill::tools;
constexpr double pi = 3.14159265358979323846;
static int checks = 0, failures = 0;
void check(bool condition, const char* label, double value = 0) {
    ++checks; if (!condition) ++failures;
    std::printf("%s %s %.9g\n", condition ? "PASS" : "FAIL", label, value);
}
struct Render { std::vector<float> audio; int latency = 0; unsigned repaired = 0, rejected = 0; };
Render render(const std::vector<float>& input, double rate, RescueParameters p, bool live, int block = 127) {
    RescueDSP dsp; dsp.setParameters(p); dsp.setLiveMode(live); dsp.prepare(rate, block, 1);
    Render out; out.latency = dsp.latencySamples(); out.audio = input; out.audio.resize(input.size() + out.latency);
    for (int n = 0; n < static_cast<int>(out.audio.size()); n += block) {
        float* channels[] {out.audio.data() + n}; watched = true;
        dsp.process(channels, 1, std::min(block, static_cast<int>(out.audio.size()) - n)); watched = false;
    }
    out.repaired = dsp.repairs(); out.rejected = dsp.rejected(); return out;
}
std::vector<float> signal(double rate, double hz, int kind, double seconds = .7) {
    std::vector<float> out(static_cast<size_t>(rate * seconds)); double peak = 0;
    for (size_t n = 0; n < out.size(); ++n) {
        const double phase = 2*pi*hz*n/rate;
        double v = std::sin(phase);
        if (kind == 1) v += .3*std::sin(2*phase+.3) + .12*std::sin(3*phase-.2);
        if (kind == 2) v = std::sin(phase) + .43*std::sin(2*phase+.2) + .25*std::sin(4*phase-.4) + .11*std::sin(7*phase);
        out[n] = static_cast<float>(v); peak = std::max(peak, std::abs(v));
    }
    for (auto& v : out) v = static_cast<float>(v / peak);
    return out;
}
double improvement(const std::vector<float>& original, const std::vector<float>& clipped, const Render& repaired, double rate) {
    double before = 0, after = 0;
    const size_t margin = static_cast<size_t>(rate * .03);
    for (size_t n = margin; n + margin < original.size(); ++n) {
        before += std::pow(original[n] - clipped[n], 2);
        after += std::pow(original[n] - repaired.audio[n + repaired.latency], 2);
    }
    return 10 * std::log10((before + 1e-30) / (after + 1e-30));
}
int main() {
    RescueParameters p; p.outputDb = 0;
    std::vector<double> gains;
    for (double rate : {44100.,48000.,96000.,192000.}) {
        for (bool live : {true,false}) {
            for (double hz : {80.,173.,330.,800.}) for (int kind : {0,1,2}) {
                const auto original = signal(rate,hz,kind);
                for (double clipDb : {-1.,-3.}) {
                    const double limit = std::pow(10.,clipDb/20.);
                    auto clipped = original; for (auto& v : clipped) v = static_cast<float>(std::clamp(static_cast<double>(v),-limit,limit));
                    p.clipDb = p.negativeClipDb = static_cast<float>(clipDb);
                    const auto r = render(clipped,rate,p,live);
                    const double gain = improvement(original,clipped,r,rate); gains.push_back(gain);
                    check(gain >= -1., "MILD known-original reconstruction does not regress >1dB", gain);
                    check(r.latency == static_cast<int>(std::ceil(rate*(live?.004:.012))), "Declared mode latency is the actual delay", r.latency);
                    double untouched = 0;
                    for (size_t n = 0; n < original.size(); ++n) if (std::abs(clipped[n]) < limit - 1e-7)
                        untouched = std::max(untouched, std::abs(static_cast<double>(r.audio[n+r.latency]-clipped[n])));
                    check(untouched == 0, "Intact samples outside clipped plateau remain bit exact", untouched);
                }
            }
            for (double hz : {80.,173.,997.,8000.}) {
                auto clean = signal(rate,hz,0); for (auto& v : clean) v *= .999f;
                p.clipDb = p.negativeClipDb = 0;
                const auto r = render(clean,rate,p,live); double error = 0;
                for (size_t n = 0; n < clean.size(); ++n) error = std::max(error,std::abs(static_cast<double>(r.audio[n+r.latency]-clean[n])));
                check(error == 0, "Clean high-level sinusoid unchanged except delay", error);
            }
        }
    }
    std::sort(gains.begin(),gains.end());
    check(gains[gains.size()/2] >= 3., "Predetermined mild clipping corpus median improvement >=3dB", gains[gains.size()/2]);
    { const auto original = signal(48000,137,1,1.); auto clipped = original;
      for (auto& v:clipped) v=std::clamp(v,-.5f,.8f);
      p.clipDb=float(20*std::log10(.8));p.negativeClipDb=float(20*std::log10(.5));
      const auto one=render(clipped,48000,p,false,1),other=render(clipped,48000,p,false,2048);
      check(one.audio==other.audio,"Asymmetric processing independent of host block size");
      check(one.repaired>0,"Asymmetric plateaus are repaired",one.repaired);
      p.repair=0;const auto dry=render(clipped,48000,p,false);double error=0;
      for(size_t n=0;n<clipped.size();++n)error=std::max(error,std::abs(double(dry.audio[n+dry.latency]-clipped[n])));
      check(error==0,"REPAIR0 is aligned exact dry",error);
      p.repair=100;p.listenRepairs=true;const auto delta=render(clipped,48000,p,false);error=0;
      for(size_t n=0;n<clipped.size();++n)error=std::max(error,std::abs(double(delta.audio[n+delta.latency])-(double(one.audio[n+one.latency])-clipped[n])));
      check(error<1e-7,"LISTEN REPAIRS is aligned processed-minus-original",error);
      p.listenRepairs=false;p.bypass=true;const auto bypass=render(clipped,48000,p,false);error=0;
      for(size_t n=0;n<clipped.size();++n)error=std::max(error,std::abs(double(bypass.audio[n+bypass.latency]-clipped[n])));
      check(error==0,"BYPASS is aligned exact original",error);p.bypass=false;
    }
    { RescueDSP dsp; dsp.prepare(48000,127,1);dsp.requestLearn();auto x=signal(48000,173,1,3.2);
      for(auto&v:x)v=std::clamp(v,-.5f,.8f);
      for(int n=0;n<int(x.size());n+=127){float*a[]{x.data()+n};watched=true;dsp.process(a,1,std::min(127,int(x.size())-n));watched=false;}
      check(dsp.isLearning()&&dsp.learnRevision()==0,"whole-song LEARN remains active beyond three seconds");dsp.requestFinish();float stopSample=0;float*stopData[]{&stopSample};watched=true;dsp.process(stopData,1,1);watched=false;check(!dsp.isLearning()&&dsp.learnRevision()==1,"manual FINISH commits the captured clip statistics");
      check(std::abs(dsp.learnedPositiveDb()-20*std::log10(.8))<.01,"LEARN measures actual positive clip boundary",dsp.learnedPositiveDb());
      check(std::abs(dsp.learnedNegativeDb()-20*std::log10(.5))<.01,"LEARN measures separate negative clip boundary",dsp.learnedNegativeDb());
      dsp.reset();dsp.requestLearn();x.assign(153600,0);
      for(int n=0;n<int(x.size());n+=127){float*a[]{x.data()+n};dsp.process(a,1,std::min(127,int(x.size())-n));}
      dsp.requestFinish();dsp.process(stopData,1,1);check(dsp.learnedPositiveDb()==-100&&dsp.learnedNegativeDb()==-100,"Silence produces no fabricated clip threshold");
    }
    { auto x=signal(48000,137,0);x[1000]=std::numeric_limits<float>::infinity();x[2000]=std::numeric_limits<float>::quiet_NaN();
      const auto r=render(x,48000,p,false);bool good=true;for(float v:r.audio)good=good&&std::isfinite(v);check(good,"Nonfinite input cannot contaminate output");
      x.assign(48000,.8f);const auto longRun=render(x,48000,p,true);check(longRun.repaired==0,"Long clipped plateau is not falsely reconstructed");
    }
    for(double rate:{44100.,48000.,96000.,192000.})for(bool live:{false,true}){
        p=RescueParameters{};p.outputDb=0;std::vector<float>impulse(1000);impulse[100]=.5f;
        const auto r=render(impulse,rate,p,live);size_t first=r.audio.size();for(size_t n=0;n<r.audio.size();++n)if(r.audio[n]!=0){first=n;break;}
        check(first==size_t(100+r.latency),"Impulse delay agrees with declared latency sample for sample",double(first));
    }
    {RescueDSP dsp;p=RescueParameters{};p.outputDb=0;p.clipDb=p.negativeClipDb=-3;dsp.setParameters(p);dsp.prepare(48000,127,2);
     auto a=signal(48000,173,1),b=a;for(size_t n=0;n<a.size();++n){a[n]=std::clamp(a[n],-.7079458f,.7079458f);b[n]=-a[n];}
     for(int n=0;n<int(a.size());n+=127){float*channels[]{a.data()+n,b.data()+n};watched=true;dsp.process(channels,2,std::min(127,int(a.size())-n));watched=false;}
     double error=0;for(size_t n=0;n<a.size();++n)error=std::max(error,std::abs(double(a[n]+b[n])));check(error==0,"Stereo opposite polarity retained with matching thresholds",error);
    }
    {RescueDSP dsp;dsp.prepare(48000,127,2);std::array<float,127>a{},b{};bool good=true;
     for(int block=0;block<240;++block){p.repair=float(block%101);p.clipDb=-float(block%25);p.negativeClipDb=-float((block*3)%25);p.outputDb=-float(block%19);p.maxRepairDb=float(block%13);p.listenRepairs=block%7==0;p.bypass=block%11==0;dsp.setParameters(p);
      for(int n=0;n<127;++n)a[n]=b[n]=float(.95*std::sin((block*127+n)*.07));float*channels[]{a.data(),b.data()};watched=true;dsp.setLiveMode(block%2==0);dsp.process(channels,2,127);watched=false;
      for(float v:a)good=good&&std::isfinite(v)&&std::abs(v)<4;
     }check(good,"Parameter and mode stress is finite and bounded");
    }
    {RescueDSP dsp;dsp.prepare(8000,127,1);dsp.requestLearn();std::array<float,127>samples{};
     for(int start=0;start<2400400;start+=127){const int n=std::min(127,2400400-start);for(int i=0;i<n;++i){const double t=(start+i)/8000.;const float x=static_cast<float>(std::sin(2*3.14159265358979323846*173*t));samples[i]=std::clamp(x,t<3?-.5f:-.25f,t<3?.8f:.5f);}float*audio[]{samples.data()};watched=true;dsp.process(audio,1,n);watched=false;}
     check(!dsp.isLearning()&&dsp.learnRevision()==1&&std::abs(dsp.learningSeconds()-300)<.001f,"full-song clip learning auto-finishes at exactly five minutes");
     check(std::abs(dsp.learnedPositiveDb()-20*std::log10(.5))<.01&&std::abs(dsp.learnedNegativeDb()-20*std::log10(.25))<.01,"five-minute analysis follows the whole song instead of its first three seconds");
     const float prior=dsp.learnedPositiveDb();dsp.startLearning();dsp.cancelLearning();check(dsp.learnedPositiveDb()==prior&&!dsp.isLearning(),"canceling an armed learn preserves its previous clip result");
    }
    check(allocations==0,"Audio processing performs no heap allocation",double(allocations));
    std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
