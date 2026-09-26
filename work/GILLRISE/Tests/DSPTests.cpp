#include "../Source/RiseDSP.h"
#include <cstdio>
#include <random>
#include <new>
#include <cstdlib>
std::atomic<std::size_t>allocations{0};
void*operator new(std::size_t n){allocations++;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using namespace gill::rise;
int checks=0,failures=0;void check(bool v,const char*n){++checks;if(!v)++failures;std::printf("%s %s\n",v?"PASS":"FAIL",n);}
Capture make(double fs){Capture c;c.sampleRate=fs;c.left.resize(int(fs*.5));c.right.resize(c.left.size());c.defaultStart=int(fs*.05);c.defaultEnd=int(fs*.4);c.onsetSample=std::int64_t(fs*10);c.hostPositionKnown=true;for(int i=c.defaultStart;i<c.defaultEnd;++i){double t=(i-c.defaultStart)/fs;float a=float(.25*std::sin(2*pi*180*t)*(.5-.5*std::cos(2*pi*t/.35)));c.left[i]=c.right[i]=a;}return c;}
int main(){
 for(double fs:{8000.,22050.,44100.,48000.,96000.,192000.}){
   auto c=make(fs);Render r;Settings s;check(render(c,s,r),"Reverse render succeeds at all supported representative sample rates");check(r.left.size()==std::size_t(fs*2)&&r.right.size()==r.left.size(),"Exact requested duration and stereo layout");check(r.left.front()==0&&r.left.back()==0&&r.right.front()==0&&r.right.back()==0,"Start and end fades finish at exact zero");bool bounded=true;for(std::size_t i=0;i<r.left.size();++i)bounded=bounded&&std::isfinite(r.left[i])&&std::isfinite(r.right[i])&&std::abs(r.left[i])<=std::pow(10.,-9./20)+1e-6&&std::abs(r.right[i])<=std::pow(10.,-9./20)+1e-6;check(bounded,"Finite output and requested ceiling with 3 dB reconstruction headroom");check(r.endSample==std::int64_t(fs*10)&&r.hostPositionKnown,"Placement ends exactly on the original syllable");
   Render again;render(c,s,again);check(r.left==again.left&&r.right==again.right,"Render is repeatable, no random or unbounded state");s.tremolo=true;render(c,s,again);check(r.left!=again.left,"Tremolo produces a distinct rendered envelope");s.start=.2;s.end=.8;render(c,s,again);check(again.endSample==c.onsetSample+int(.2*(c.defaultEnd-c.defaultStart)),"Edited selection moves placement by exact selected frames");
 }
 {Render r;auto c=make(48000);int calls=0;check(!render(c,{},r,[&]{return ++calls>5;}),"Cancellation interrupts an in-flight render before publication");c.left.assign(10000,0);c.right=c.left;c.defaultStart=0;c.defaultEnd=10000;check(!render(c,{},r),"Silence cannot create an apparently successful empty asset");}
 for(double fs:{44100.,48000.,96000.}){
   CaptureEngine engine;engine.prepare(fs);std::array<float,127>l{},r{};std::int64_t clock=0;
   for(int block=0;block<int(fs/127);++block){for(int i=0;i<127;++i)l[i]=r[i]=float(.002*std::sin(2*pi*60*(clock+i)/fs));engine.process(l.data(),r.data(),127,true,true,clock,true);clock+=127;}
   check(engine.status()==CaptureEngine::Armed,"Low-level background hum does not trigger capture");const auto expected=clock;
   for(int block=0;block<int(fs*.7/127);++block){for(int i=0;i<127;++i){auto t=(clock+i-expected)/fs;l[i]=r[i]=t<.27?float(.15*std::sin(2*pi*180*t)):0;}engine.process(l.data(),r.data(),127,true,true,clock,true);clock+=127;}
   Capture c;check(engine.take(c),"First voiced syllable automatically completes after its gap");check(c.hostPositionKnown&&std::abs(c.onsetSample-expected)<fs*.01,"Host sample placement is retained within onset detector resolution");check(c.defaultStart>0&&c.left.size()<std::size_t(fs*.6),"Preroll retained and capture bounded to first syllable");check(!engine.take(c),"Completed capture can only transfer to worker once");
   engine.arm();l.fill(0);r.fill(0);engine.process(l.data(),r.data(),127,false,true,0,true);check(engine.status()==CaptureEngine::Armed,"Armed capture waits while transport stopped");
 }
 {auto c=make(48000);Render base,changed;Settings settings;render(c,settings,base);for(int knob=0;knob<8;++knob){auto s=settings;switch(knob){case 0:s.length=3;break;case 1:s.decay=6;break;case 2:s.tone=1200;break;case 3:s.level=-18;break;case 4:s.tremolo=true;s.rate=13;break;case 5:s.tremolo=true;s.depth=.2;break;case 6:s.start=.3;break;case 7:s.end=.6;break;}render(c,s,changed);check(changed.left!=base.left,"Every render parameter changes the resulting audio");}for(auto&v:c.right)v=-v;check(render(c,settings,changed)&&changed.peak>.01f,"Antiphase stereo input does not disappear in a mono sum");}
 {CaptureEngine e;e.prepare(48000);std::array<float,127>l{},r{};auto before=allocations.load();std::int64_t position=0;for(int block=0;block<113386;++block){l.fill(.001f);r=l;e.process(l.data(),r.data(),127,true,true,position,true);position+=127;}check(e.status()==CaptureEngine::Armed&&position>=48000*300,"Can wait through five minutes of a song with bounded storage");check(allocations.load()==before,"Capture callback allocates no heap memory during five-minute run");e.arm();l.fill(0);r.fill(0);e.process(l.data(),r.data(),127,true,true,position,true);l[0]=r[0]=.9f;e.process(l.data(),r.data(),127,true,true,position+127,true);l.fill(0);r=l;for(int b=0;b<40;++b)e.process(l.data(),r.data(),127,true,true,position+(b+2)*127,true);check(e.status()==CaptureEngine::Armed,"Single short mouth click cannot satisfy syllable duration gate");}
 std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
