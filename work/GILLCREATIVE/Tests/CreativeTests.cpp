#include "CreativeDSP.h"
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <new>
#include <cstdlib>
#include <thread>
using namespace gill::creative;
static int checks=0,failures=0,allocations=0;static thread_local bool watch=false;
void*operator new(size_t n){if(watch)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](size_t n){return::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void check(bool good,const char*name,double value=0){++checks;if(!good)++failures;std::printf("%s %s %.9g\n",good?"PASS":"FAIL",name,value);}
float vocal(double t){const double local=std::fmod(t,1.);if(local<.08||local>.60)return 0;const double fade=std::min(1.,std::min((local-.08)/.025,(.60-local)/.035));return static_cast<float>(fade*(.2*std::sin(2*pi*171*t)+.055*std::sin(2*pi*342*t)+.027*std::sin(2*pi*683*t)));}
double run(Engine&e,double rate,double seconds,Controls c,bool input=true,bool host=true,double start=0,int block=127,bool side=false){std::array<float,2048>l{},r{},sc{};double power=0;int total=static_cast<int>(seconds*rate);for(int at=0;at<total;at+=block){const int n=std::min(block,total-at);for(int i=0;i<n;++i){l[i]=r[i]=input?vocal((at+i)/rate):0;sc[i]=.18f;}Transport t;t.hasPPQ=host;t.ppq=start+at/rate*2;t.bpm=120;watch=true;e.process(l.data(),r.data(),side?sc.data():nullptr,side?sc.data():nullptr,n,c,t);watch=false;for(int i=0;i<n;++i){if(!std::isfinite(l[i])||!std::isfinite(r[i]))return std::numeric_limits<double>::quiet_NaN();power+=l[i]*l[i]+r[i]*r[i];}}return power;}
void workflows(Kind kind){Engine e(kind);e.prepare(48000);Controls c;c.amount=1;c.mix=1;c.dry=0;c.sensitivity=-44;
 e.request(1);run(e,48000,2,c,false);e.request(2);run(e,48000,.01,c,false,true,4);check(e.learningState()==4&&!e.snapshot().valid(),"silence cannot manufacture a learned plan");
 e.request(1);run(e,48000,4,c,true,true,8,127,true);e.request(2);run(e,48000,.01,c,false,true,16);const auto plan=e.snapshot();check(plan.valid()&&plan.count==4,"four vocal phrases produce four editable markers",plan.count);check(e.learningState()==2,"learning stops into review rather than auto-apply");
 bool bounds=true;for(int i=0;i<plan.count;++i){const auto&m=plan.markers[i];bounds&=m.endSec>m.startSec&&m.endSec<4.1&&m.strength>0&&m.strength<=1;}check(bounds,"all source trims and strengths remain valid");
 e.request(5);run(e,48000,.01,c,false,true,16.02);check(e.isPreviewing(),"valid candidate starts a separately indicated preview");run(e,48000,8.1,c,false,true,16.04);check(!e.isPreviewing(),"preview completion clears its UI indicator for the next audition");
 e.request(3);const double effect=run(e,48000,5,c,kind!=Kind::Reply,true,8);check(e.isApplied(),"apply activates the learned plan");check(std::isfinite(effect)&&effect>1.e-4,"learned effect is audible",effect);
 if(kind==Kind::Reply){for(int variant=0;variant<3;++variant){c.variant=variant;double energy=run(e,48000,5,c,false,true,8);check(energy>1.e-4,"each deterministic reply variant plays captured audio",energy);const double repeat=run(e,48000,5,c,false,true,8);check(std::abs(repeat-energy)<1.e-7,"same reply variant replays deterministically after a seek",repeat-energy);}const double skipped=run(e,48000,.4,c,false,true,100);check(skipped<1.e-12,"seeking past a take never triggers all earlier replies",skipped);c.amount=0;double silence=run(e,48000,5,c,false,true,8);check(silence<1.e-12,"zero density suppresses all new replies",silence);}
 if(kind==Kind::Director)check(e.hasSidechain(),"optional beat guide contributes to learned analysis");
 e.request(4);run(e,48000,.01,c,false,true,18);check(!e.isApplied(),"undo first apply restores unlearned rack");
 auto edited=plan;edited.markers[0].strength=.13f;e.postPlan(edited,true);run(e,48000,.01,c,false,true,18.02);check(std::abs(e.snapshot().markers[0].strength-.13f)<1.e-6f&&e.isApplied(),"editable learned marker changes actual plan");
 auto invalid=plan;invalid.markers[0].endSec=std::numeric_limits<float>::quiet_NaN();check(!Engine::validPlan(invalid),"invalid saved/edited marker rejected");
 e.request(1);run(e,48000,.5,c);run(e,48000,.01,c,false,true,100);check(e.learningState()==4,"transport seek safely cancels a discontinuous capture");
}
void boundedLearning(Kind kind){
 Engine e(kind);e.prepare(8000);Controls c;e.request(1);run(e,8000,60.05,c,true,true,0,511);const auto plan=e.snapshot();check(e.learningState()==2&&plan.valid()&&plan.durationSeconds<=60.001f&&plan.count==maxMarkers,"capture auto-stops at 60 seconds with a bounded marker count");
 if(kind==Kind::Reply){const auto*bank=e.capture(plan.capture);check(bank&&bank->used.load()==8000*60&&bank->capacity==48000*60,"recording respects native low rates inside the fixed 48 kHz bank");}
 else{bool noPCM=true;for(int i=0;i<3;++i)noPCM&=!e.capture(i)->data;check(noPCM,"PHRASE and DIRECTOR never reserve PCM capture banks");}
 e.prepare(48000);e.request(1);std::array<float,256>l{},r{};for(int at=0;at<48000;at+=256){const int n=std::min(256,48000-at);for(int i=0;i<n;++i)l[i]=r[i]=.2f*static_cast<float>(std::sin(2*pi*7000*(at+i)/48000));Transport t;t.hasPPQ=true;t.ppq=at/24000.;watch=true;e.process(l.data(),r.data(),nullptr,nullptr,n,c,t);watch=false;}e.request(2);run(e,48000,.01,c,false,true,2);check(e.learningState()==4&&!e.snapshot().valid(),"isolated high-frequency consonant-like tone cannot become voiced phrases");
 if(kind==Kind::Reply){e.prepare(192000);e.request(1);run(e,192000,1,c);e.request(2);run(e,192000,.01,c,false,true,2);const auto capture=e.snapshot();const auto*bank=e.capture(capture.capture);check(capture.valid()&&bank&&bank->rate.load()==48000&&bank->used.load()==48000,"192 kHz host capture is anti-aliased into 48 kHz storage");}
}
void concurrentCapture(){
 Engine e(Kind::Reply);e.prepare(48000);std::vector<float>pcm(9600);for(size_t i=0;i<pcm.size()/2;++i)pcm[i*2]=pcm[i*2+1]=vocal(i/48000.);
 Plan p;p.count=1;p.durationSeconds=.1f;p.durationBeats=.2f;p.markers[0]={0,.18f,0,.09f,.7f};Controls c;c.amount=1;c.mix=1;c.dry=0;
 std::atomic<bool>started{false},audioFinite{true};std::thread audio([&]{std::array<float,64>l{},r{};started=true;for(int block=0;block<6000;++block){if(block%97==0)e.request(1);if(block%97==20)e.request(2);if(block%251==0)e.request(7);for(int n=0;n<64;++n)l[n]=r[n]=vocal((block*64+n)/48000.);Transport t;t.hasPPQ=true;t.ppq=block*64/24000.;watch=true;e.process(l.data(),r.data(),nullptr,nullptr,64,c,t);watch=false;for(float sample:l)if(!std::isfinite(sample))audioFinite=false;if(block%8==0)std::this_thread::yield();}});
 while(!started.load())std::this_thread::yield();int imported=0;
 for(int attempt=0;attempt<512;++attempt){const int bank=e.importCapture(pcm.data(),4800,48000);if(bank>=0){p.capture=bank;p.captureEpoch=e.capture(bank)->epoch.load();e.postPlan(p,attempt%2==0);++imported;}std::this_thread::yield();}
 audio.join();check(imported>0&&audioFinite.load(),"concurrent state imports and LEARN/clear preserve finite audio",imported);
 e.request(2);run(e,48000,.01,c,false,true,100);const int bank=e.importCapture(pcm.data(),4800,48000);check(bank>=0,"capture import slots remain available after concurrency stress");if(bank>=0){p.capture=bank;p.captureEpoch=e.capture(bank)->epoch.load();e.postPlan(p,true);run(e,48000,.01,c,false,true,0);const auto saved=e.savedPlan();check(e.isApplied()&&saved.capture==bank&&saved.captureEpoch==p.captureEpoch&&e.capture(bank)->role.load()==3,"final imported take owns the exact applied bank and epoch");}
}
int main(){for(auto kind:{Kind::Phrase,Kind::Director,Kind::Reply}){workflows(kind);boundedLearning(kind);for(double rate:{8000.,44100.,48000.,96000.,192000.}){Engine e(kind);e.prepare(rate);Controls c;for(int block:{1,16,64,127,256,512,1024,2048}){check(std::isfinite(run(e,rate,.025,c,true,true,0,block)),"supported rate and odd/even block lengths remain finite");}c.bypass=true;check(std::isfinite(run(e,rate,.1,c)),"bypass finite at each sample rate");}
 Engine e(kind);e.prepare(48000);float a[]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-.2f},b[]={0,0,.2f};Controls c;Transport t;watch=true;e.process(a,b,nullptr,nullptr,3,c,t);watch=false;check(std::isfinite(a[0])&&std::isfinite(a[1]),"non-finite input is contained");}
 concurrentCapture();check(allocations==0,"audio processing including learning and commands allocates no heap",allocations);std::ofstream report("creative-dsp-report.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_allocations\":"<<allocations<<"}";std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}
