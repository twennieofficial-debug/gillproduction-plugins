#include "MixProtocol.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cstring>
#include <new>
#include <cstdlib>
#include <memory>
namespace gillMixAllocationAudit {thread_local bool enabled=false;thread_local unsigned allocations=0;}
void*operator new(size_t n){if(gillMixAllocationAudit::enabled)++gillMixAllocationAudit::allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](size_t n){return::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
using namespace gill::mix07;
int checks=0,failures=0;void check(bool b,const char*m){++checks;if(!b){++failures;std::cerr<<"FAIL "<<m<<'\n';}}
int main(){
 GainValue gain;const auto before=gain.snapshot();uint64_t accepted=0;
 check(gain.compareSet(before,-3,accepted)&&gain.db()==-3,"atomic remote gain accepts exact snapshot");
 check(!gain.compareSet(before,2,accepted)&&gain.db()==-3,"replay/stale gain snapshot cannot overwrite");
 const auto same=gain.snapshot();gain.set(-3);check(gain.snapshot()!=same&&!gain.compareSet(same,1,accepted),"same-value host edit still invalidates pending remote edit");
 gain.set(std::numeric_limits<float>::quiet_NaN());check(gain.db()==-3,"NaN parameter does not change value");
 for(double rate:{44100.,48000.,88200.,96000.,192000.})for(int block:{1,16,64,127,256,512,1024,2048}){
  LinkAudio audio;audio.prepare(rate,-6);std::vector<double>l(static_cast<size_t>(block),.25),r(l);double*ptr[]{l.data(),r.data()};gillMixAllocationAudit::allocations=0;gillMixAllocationAudit::enabled=true;
  audio.process(ptr,2,block,-6,false,{0,true,true});gillMixAllocationAudit::enabled=false;
  check(gillMixAllocationAudit::allocations==0,"audio processing has no C++ allocation");
  check(std::abs(20*std::log10(l[0]/.25)+6)<.0001,"saved static gain acts at sample zero");
  check(l==r,"stereo gain/ramp is linked");
 }
 {
  LinkAudio a;a.prepare(48000,0);std::array<float,2000>x{};x.fill(.5f);float*p[]{x.data()};a.process(p,1,2000,-6,false,{0,true,true});
  bool smooth=x[0]<.5f&&x[0]>.499f;for(size_t i=1;i<x.size();++i)smooth&=x[i]<=x[i-1]&&x[i-1]-x[i]<.001f;
  check(smooth,"30-ms gain ramp is monotonic and has a bounded per-sample step");check(std::abs(20*std::log10(x.back()/.5f)+6)<.0001,"ramp reaches exact target");
 }
 {
  SpscRing<uint64_t,64>q;std::atomic<bool>good{true};std::thread producer([&]{for(uint64_t i=0;i<100000;++i)while(!q.push(i))std::this_thread::yield();});
  for(uint64_t i=0;i<100000;++i){uint64_t v=0;while(!q.pop(v))std::this_thread::yield();if(v!=i)good=false;}producer.join();check(good,"SPSC concurrent payload ordering is exact");
  SpscRing<int,2>small;check(small.push(1)&&small.push(2)&&!small.push(3),"full ring rejects without overwriting unread payload");int x=0;check(small.pop(x)&&x==1&&small.pop(x)&&x==2,"overflow preserves unread order");
 }
 for(float db:{-18.f,-6.f,0.f,3.f})for(float limit:{0.f,1.f,3.f,6.f}){const auto c=correction(db,-12,-2,limit);check(c>=-24&&c<=6&&std::abs(c-db)<=limit+.001f,"learn correction obeys change limit and absolute gain range");}
 {
  LearnedTrack t;for(int i=0;i<500;++i){TelemetryFrame f{};f.rate=48000;f.samples=960;f.position=int64_t(i)*960;f.flags=3;f.energy=.01;f.levelDb=-20;f.voiced=1;t.add(f);}check(t.activeLevel()==-20&&t.seconds()==10,"active median and sample-position duration are exact");check(!audioHint(t).high,"audio-only semantic role suggestion never silently authorizes gain");
 }
 {
  MixBus master(true),other(true),link(false);LocalState m{},o{},l{};m.persistent=newId();m.session=newId();o.persistent=newId();o.session=newId();l.persistent=newId();l.gain=gain.snapshot();std::strcpy(l.name,"MAIN");
  check(master.available()&&other.available()&&link.available(),"real native shared mapping available");master.service(m);other.service(o);link.service(l);
  auto d=master.discover();check(d.count==1&&!d.rows[0].owner,"discovery never connects or changes gain");Address addr{link.runtimeId(),l.epoch};check(master.connect(&addr,1)==Status::ok,"explicit runtime connect accepted");
  check(other.connect(&addr,1)==Status::conflict,"second session cannot take connected link");check(link.service(l).connected,"link observes explicit controller binding");d=master.discover();
  Change c{};c.target=addr;c.binding=d.rows[0].binding;c.expectedGain=l.gain;c.expectedMetadata=l.metadataRevision;c.newDb=-6;c.beforeDb=gain.db();c.newRole=1;c.limit=6;uint64_t tx=0;
  check(master.prepare(&c,1,tx)==Status::ok,"whole valid proposal prepares");auto p=link.service(l);check(p.command.phase==Phase::prepare&&gain.db()==-3,"prepare does not alter local gain");
  Ack ack{tx,l.gain,l.metadataRevision,Phase::prepare,Status::ready,l.role,0};link.acknowledge(ack);check(master.commit(tx)==Status::ok,"all-ready transaction enters commit");p=link.service(l);check(p.command.phase==Phase::commit,"link receives actual commit phase");
  check(gain.compareSet(p.command.change.expectedGain,p.command.change.newDb,accepted),"link applies conditional local gain");l.gain=accepted;l.role=1;++l.metadataRevision;ack={tx,l.gain,l.metadataRevision,Phase::commit,Status::applied,l.role,0};link.acknowledge(ack);check(link.service(l).command.phase==Phase::none,"duplicate commit is acknowledged without reapplying");
  ++l.epoch;link.service(l);check(!link.service(l).connected,"state recall disarms remote rights");check(gain.db()==-6,"disconnection preserves last local gain");
  MixBus duplicateLink(false);duplicateLink.service(l);d=master.discover();bool duplicate=false;for(int i=0;i<d.count;++i)duplicate|=d.rows[static_cast<size_t>(i)].duplicate;check(duplicate,"cloned persistent link identities are visibly ambiguous");
  addr.epoch=l.epoch;check(master.connect(&addr,1)==Status::ambiguous,"ambiguous link cannot be armed");
 }
 {
  MixBus master(true);LocalState m{};m.persistent=newId();m.session=newId();master.service(m);
  std::vector<std::unique_ptr<MixBus>>links;std::array<LocalState,65>states{};std::array<Address,65>addresses{};
  for(int i=0;i<65;++i){links.push_back(std::make_unique<MixBus>(false));auto&s=states[static_cast<size_t>(i)];s.persistent=newId();s.session=newId();links.back()->service(s);addresses[static_cast<size_t>(i)]={links.back()->runtimeId(),s.epoch};}
  check(master.connect(addresses.data(),64)==Status::ok,"exactly 64 links can join one session");check(master.connect(&addresses[64],1)==Status::capacity,"65th link is rejected without removing another link");
  const auto old=addresses[0];links[0]->expireForTest();check(!master.discover().rows[0].reachable,"missing heartbeat is reported as unreachable");const auto p=links[0]->service(states[0]);check(!p.connected&&links[0]->runtimeId()!=old.runtime,"stale resume creates a fresh unarmed runtime identity");check(master.connect(&old,1)==Status::stale,"old runtime address cannot reconnect reused slot");
 }
 {
  GainValue v;std::atomic<bool>done{false};std::thread host([&]{for(int i=0;i<100000;++i)v.set(static_cast<float>((i%300)-240)*.1f);done=true;});unsigned writes=0;bool finite=true;
  while(!done.load()){const auto expected=v.snapshot();uint64_t next=0;v.compareSet(expected,-3,next);const float d=v.db();finite&=std::isfinite(d)&&d>=-24&&d<=6;++writes;}host.join();check(finite&&writes>0,"parallel host edits and conditional commits remain bounded finite gains");
 }
 {
  LinkAudio audio;audio.prepare(48000,0);std::array<double,4>x{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::max(),-.25};double*p[]{x.data()};audio.process(p,1,4,6,false,{std::numeric_limits<int64_t>::max(),true,true});bool finite=true;for(double y:x)finite&=std::isfinite(y);check(finite,"nonfinite and extreme input cannot produce nonfinite output or sample-position overflow");
  LearnedTrack t;TelemetryFrame f{};f.energy=1;f.levelDb=std::numeric_limits<float>::quiet_NaN();f.rate=48000;f.samples=960;t.add(f);check(t.count==0&&!t.aligned,"invalid telemetry cannot enter learning statistics");
 }
 {
  LinkAudio live,pro;live.prepare(48000,-2);pro.prepare(48000,-2);std::array<float,960>x{},y{};bool same=true;int voiced=0;
  for(int block=0;block<30;++block){for(int i=0;i<960;++i)x[static_cast<size_t>(i)]=y[static_cast<size_t>(i)]=.1f*static_cast<float>(std::sin((block*960+i)*2*3.141592653589793*220/48000));float*a[]{x.data()},*b[]{y.data()};gillMixAllocationAudit::allocations=0;gillMixAllocationAudit::enabled=true;live.process(a,1,960,-2,false,{block*960,true,true},true,false);pro.process(b,1,960,-2,false,{block*960,true,true},true,true);gillMixAllocationAudit::enabled=false;same&=x==y&&gillMixAllocationAudit::allocations==0;TelemetryFrame frame{};while(pro.pop(frame))voiced+=frame.voiced>.5f;while(live.pop(frame)){};}
  check(same,"LIVE and PRO have identical causal gain output without allocations");check(voiced>=25,"PRO periodicity analysis recognizes a real voiced tone");
 }
 {
  NativeMapping held(sizeof(Shared));std::unique_ptr<MixBus>deferred;
  {NativeMapping::Lock lock(held);check(lock.ok,"mapping initialization contention lock acquired");std::thread constructor([&]{deferred=std::make_unique<MixBus>(false);});constructor.join();check(!deferred->available(),"parallel constructor does not block for mapping initialization");}
  LocalState state{};state.persistent=newId();state.session=newId();deferred->service(state);check(deferred->available(),"deferred mapping initialization recovers on later service");
 }
 std::cout<<"WIRE "<<sizeof(LocalState)<<" "<<sizeof(Change)<<" "<<sizeof(Command)<<" "<<sizeof(Ack)<<" "<<sizeof(Entry)<<" "<<sizeof(Shared)<<" offsets "<<offsetof(Shared,leases)<<" "<<offsetof(Shared,entries)<<"\n";
 std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
