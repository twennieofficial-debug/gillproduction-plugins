#pragma once
#include "NativeMapping.h"
#include "MixDSP.h"
#include <array>
#include <cstring>

namespace gill::mix07 {
enum class Status:uint32_t{ok,busy,unbound,stale,ambiguous,conflict,invalid,capacity,recalled,timedOut,ready,applied};
inline const char*statusText(Status s){constexpr const char*n[]{"READY","BUSY","NOT CONNECTED","OFFLINE","DUPLICATE ID","CHANGED / CHECK","INVALID","CAPACITY FULL","RECALLED","TIMED OUT","PREPARED","APPLIED"};return n[std::min(11u,static_cast<unsigned>(s))];}
enum class Phase:uint32_t{none,prepare,commit};
struct Address{Id runtime{};uint64_t epoch=0;};
struct LocalState{Id persistent{},session{},controller{};uint64_t epoch=1,gain=0,metadataRevision=1;uint32_t role=0,manualRole=0,locked=0;char name[96]{};};
struct Change{Address target{};Id binding{};uint64_t expectedGain=0,expectedMetadata=0;float beforeDb=0,newDb=0,limit=3;uint32_t beforeRole=0,newRole=0,manualRole=0,beforeManualRole=0;};
struct Command{Change change{};Id owner{},session{};uint64_t ownerEpoch=0,transaction=0,deadline=0,ownerLease=0;Phase phase=Phase::none;uint32_t ownerSlot=0;};
struct Ack{uint64_t transaction=0,gain=0,metadata=0;Phase phase=Phase::none;Status status=Status::unbound;uint32_t role=0,reserved=0;};
struct Entry {
 Id runtime{};LocalState local{};Id owner{},binding{};uint64_t ownerEpoch=0,heartbeat=0,learn=0,writeTelemetry=0,ownerLease=0;
 uint32_t kind=0,reserved=0;Command command{};Ack ack{};TelemetryFrame telemetry[128]{};
};
struct Shared {uint64_t magic=0;uint32_t version=0,bytes=0;Id incarnation{};uint64_t commandCounter=0;alignas(8) uint64_t leases[32]{};Entry entries[288]{};};
static_assert(std::is_trivially_copyable_v<Shared> && sizeof(Id)==16);
// Version 1 is a fixed-width 64-bit little-endian wire ABI shared by separate
// DLLs/bundles. Any layout drift fails compilation rather than joining a bus.
static_assert(sizeof(LocalState)==184 && sizeof(Change)==88 && sizeof(Command)==160);
static_assert(sizeof(Ack)==40 && sizeof(Entry)==16864 && sizeof(Shared)==4857128);
static_assert(offsetof(Shared,leases)==40 && offsetof(Shared,entries)==296);
static_assert(alignof(Shared)>=8 && std::atomic<uint64_t>::is_always_lock_free);
constexpr uint64_t protocolMagic=0x47494c4c4d583731ULL,staleMs=10000;
struct Row{Address address{};LocalState local{};Id owner{},binding{};bool duplicate=false,reachable=false;Ack ack{};uint64_t heartbeat=0;};
struct Discovery{std::array<Row,256>rows{};int count=0;Status status=Status::ok;};
struct Poll{bool connected=false;Id session{},controller{};uint64_t learn=0;Status status=Status::unbound;Command command{};};

class MixBus {
public:
 explicit MixBus(bool controller):mapping(sizeof(Shared)),runtime(newId()),isController(controller){NativeMapping::Lock lock(mapping);if(lock.ok)initialise();}
 ~MixBus(){invalidate(expectedEpoch.load()+1);NativeMapping::Lock lock(mapping);if(lock.ok&&valid())if(auto*e=find(runtime))*e=Entry{};}
 // Called by state recall without any OS call, wait, lock, or heap allocation.
 // It revokes unexecuted commands immediately; a CAS already executing on a
 // different LINK is a local commit, not a globally atomic transaction.
 void invalidate(uint64_t epoch) noexcept {expectedEpoch.store(epoch,std::memory_order_release);const auto token=publishedLease.exchange(0);if(auto*p=leaseAddress.load(std::memory_order_acquire))if(token)wireCompare(p,token,0);}
 bool commandActive(const Command&c)const noexcept {return shared&&c.ownerSlot<32&&c.ownerLease&&wireLoad(&shared->leases[c.ownerSlot])==c.ownerLease;}
 Id runtimeId()const noexcept{return runtime;}
 std::string mappingIdentity()const{return mapping.identity();}
 bool available()const noexcept{return shared!=nullptr;}
#if defined(GILL_MIX_TESTING)
 void expireForTest(){NativeMapping::Lock lock(mapping);if(lock.ok&&valid())if(auto*e=find(runtime))e->heartbeat=nowMs()-staleMs-1;}
#endif
 Poll service(const LocalState&state,const TelemetryFrame*frames=nullptr,int count=0){
  Poll result;if(!mapping.ensureOpen())return result;NativeMapping::Lock lock(mapping);if(!lock.ok){result.status=Status::busy;return result;}if(!valid()&&!initialise())return result;
  auto*e=find(runtime);const auto now=nowMs();if(e&&!alive(*e,now)){invalidate(expectedEpoch.load());*e=Entry{};runtime=newId();e=nullptr;}if(!e){e=registerOwn(now);if(!e){result.status=Status::capacity;return result;}}
  if(e->local.epoch!=state.epoch||e->local.persistent!=state.persistent){e->owner={};e->binding={};e->ownerEpoch=0;e->command={};e->ack={};e->learn=0;}
  e->local=state;e->heartbeat=now;
  if(isController){
   const auto slot=static_cast<size_t>(e-shared->entries-256);auto*p=&shared->leases[slot];leaseAddress.store(p,std::memory_order_release);
   if(expectedEpoch.load(std::memory_order_acquire)==state.epoch){
    if(!publishedLease.load()){const auto token=++shared->commandCounter;publishedLease.store(token);wireStore(p,token);}
    if(expectedEpoch.load(std::memory_order_acquire)!=state.epoch){const auto token=publishedLease.exchange(0);if(token)wireCompare(p,token,0);}
   }
  }
  for(int i=0;i<count&&i<256;++i)e->telemetry[(e->writeTelemetry++)%128]=frames[i];
  if(isController){result.connected=true;result.status=duplicate(*e)?Status::ambiguous:Status::ok;return result;}
  auto*owner=find(e->owner);
  if(!owner||!alive(*owner,now)||owner->local.epoch!=e->ownerEpoch||!e->ownerLease||wireLoad(&shared->leases[static_cast<size_t>(owner-shared->entries-256)])!=e->ownerLease){if(e->owner){e->owner={};e->binding={};e->command={};e->learn=0;}return result;}
  if(duplicate(*e)||duplicate(*owner)){result.status=Status::ambiguous;return result;}
  result.connected=true;result.status=Status::ok;result.learn=e->learn;result.session=owner->local.session;result.controller=owner->local.persistent;
  const auto&cmd=e->command;
  if(cmd.transaction&&cmd.phase!=Phase::none&&(e->ack.transaction!=cmd.transaction||e->ack.phase!=cmd.phase)){
   if(cmd.deadline<now||cmd.owner!=owner->runtime||cmd.ownerEpoch!=owner->local.epoch||cmd.change.binding!=e->binding||cmd.change.target.epoch!=state.epoch){e->ack={cmd.transaction,state.gain,state.metadataRevision,cmd.phase,Status::timedOut,state.role,0};}
   else result.command=cmd;
  }
  return result;
 }
 bool acknowledge(const Ack&ack){NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return false;auto*e=find(runtime);if(!e||e->command.transaction!=ack.transaction||e->command.phase!=ack.phase)return false;e->ack=ack;return true;}
 Discovery discover(){Discovery out;NativeMapping::Lock lock(mapping);if(!lock.ok||!valid()){out.status=Status::busy;return out;}const auto now=nowMs();for(const auto&e:shared->entries)if(e.runtime&&e.kind==1&&out.count<256){auto&r=out.rows[static_cast<size_t>(out.count++)];r.address={e.runtime,e.local.epoch};r.local=e.local;r.owner=e.owner;r.binding=e.binding;r.reachable=alive(e,now);r.duplicate=duplicate(e);r.ack=e.ack;r.heartbeat=e.heartbeat;}return out;}
 Status connect(const Address*targets,int count){
  NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return Status::busy;auto*owner=find(runtime);if(!isController||!owner||duplicate(*owner))return Status::ambiguous;if(count<1||count>maxLinks)return Status::invalid;if(!wireLoad(&shared->leases[static_cast<size_t>(owner-shared->entries-256)]))return Status::recalled;
  int linked=0;for(const auto&e:shared->entries)if(e.owner==runtime)++linked;
  for(int i=0;i<count;++i){auto*e=find(targets[i].runtime);if(!e||e->kind!=1||!alive(*e,nowMs())||e->local.epoch!=targets[i].epoch)return Status::stale;if(duplicate(*e))return Status::ambiguous;if(e->owner&&e->owner!=runtime)return Status::conflict;for(int j=0;j<i;++j)if(targets[j].runtime==targets[i].runtime)return Status::invalid;if(!e->owner)++linked;}
  if(linked>maxLinks)return Status::capacity;
  for(int i=0;i<count;++i){auto*e=find(targets[i].runtime);e->owner=runtime;e->ownerEpoch=owner->local.epoch;e->ownerLease=wireLoad(&shared->leases[static_cast<size_t>(owner-shared->entries-256)]);e->binding=newId();e->command={};e->ack={};e->learn=0;}
  return Status::ok;
 }
 Status disconnect(Id target){NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return Status::busy;auto*e=find(target);if(!e)return Status::stale;if(e->runtime!=runtime&&e->owner!=runtime)return Status::conflict;e->owner={};e->binding={};e->command={};e->learn=0;if(isController&&target==runtime)for(auto&member:shared->entries)if(member.owner==runtime){member.owner={};member.binding={};member.command={};member.learn=0;}return Status::ok;}
 uint64_t beginLearn(){NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return 0;auto*o=find(runtime);if(!o||!isController||duplicate(*o))return 0;const auto id=++shared->commandCounter;for(auto&e:shared->entries)if(e.owner==runtime)e.learn=id;return id;}
 void stopLearn(){NativeMapping::Lock lock(mapping);if(lock.ok&&valid())for(auto&e:shared->entries)if(e.owner==runtime)e.learn=0;}
 uint64_t telemetryCursor(Id target){NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return 0;auto*e=find(target);return e&&e->owner==runtime?e->writeTelemetry:0;}
 int readTelemetry(Id target,uint64_t&cursor,TelemetryFrame*out,int capacity,bool&lost){NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return 0;auto*e=find(target);if(!e||e->owner!=runtime)return 0;const auto end=e->writeTelemetry;if(end<cursor){cursor=end;lost=true;}if(end-cursor>128){cursor=end-128;lost=true;}int n=0;while(cursor<end&&n<capacity)out[n++]=e->telemetry[(cursor++)%128];return n;}
 Status prepare(const Change*rows,int count,uint64_t&transaction){
  NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return Status::busy;auto*o=find(runtime);if(!o||!isController||duplicate(*o))return Status::ambiguous;if(count<1||count>maxLinks)return Status::invalid;
  for(int i=0;i<count;++i){const auto&r=rows[i];auto*e=find(r.target.runtime);if(!e||!alive(*e,nowMs())||e->owner!=runtime||e->ownerEpoch!=o->local.epoch||e->binding!=r.binding||e->local.epoch!=r.target.epoch||!e->ownerLease||wireLoad(&shared->leases[static_cast<size_t>(o-shared->entries-256)])!=e->ownerLease)return Status::unbound;
   if(duplicate(*e)||e->local.locked||e->local.gain!=r.expectedGain||e->local.metadataRevision!=r.expectedMetadata)return Status::conflict;
   if(!std::isfinite(r.newDb)||r.newDb < -24||r.newDb > 6||r.newRole>4||!std::isfinite(r.limit)||r.limit<0||r.limit>30||std::abs(r.newDb-GainValue::decode(r.expectedGain))>r.limit+.011f)return Status::invalid;
   for(int j=0;j<i;++j)if(rows[j].target.runtime==r.target.runtime)return Status::invalid;
  }
  transaction=++shared->commandCounter;const auto deadline=nowMs()+3000;
  for(int i=0;i<count;++i){auto*e=find(rows[i].target.runtime);e->command={rows[i],runtime,o->local.session,o->local.epoch,transaction,deadline,e->ownerLease,Phase::prepare,static_cast<uint32_t>(o-shared->entries-256)};}
  return Status::ok;
 }
 Status commit(uint64_t transaction){NativeMapping::Lock lock(mapping);if(!lock.ok||!valid())return Status::busy;int count=0;const auto now=nowMs();for(const auto&e:shared->entries)if(e.command.owner==runtime&&e.command.transaction==transaction){++count;if(!commandActive(e.command))return Status::recalled;if(e.command.deadline<now||!alive(e,now))return Status::timedOut;if(e.ack.transaction!=transaction||e.ack.phase!=Phase::prepare)return Status::busy;if(e.ack.status!=Status::ready)return e.ack.status;}
  if(count<1)return Status::invalid;for(auto&e:shared->entries)if(e.command.owner==runtime&&e.command.transaction==transaction){e.command.phase=Phase::commit;e.command.deadline=now+3000;}return Status::ok;
 }
private:
 bool initialise(){if(!mapping.data()||!runtime)return false;shared=static_cast<Shared*>(mapping.data());if(!shared->magic){std::memset(shared,0,sizeof(Shared));shared->version=1;shared->bytes=sizeof(Shared);shared->incarnation=newId();shared->magic=protocolMagic;}if(!valid()){shared=nullptr;return false;}return true;}
 bool valid()const{return shared&&shared->magic==protocolMagic&&shared->version==1&&shared->bytes==sizeof(Shared);}
 static bool alive(const Entry&e,uint64_t now){return e.runtime&&now>=e.heartbeat&&now-e.heartbeat<=staleMs;}
 Entry*find(Id id)const{if(!id)return nullptr;for(auto&e:shared->entries)if(e.runtime==id)return&e;return nullptr;}
 bool duplicate(const Entry&e)const{if(!e.local.persistent)return true;for(const auto&o:shared->entries)if(o.runtime!=e.runtime&&o.kind==e.kind&&alive(o,nowMs())&&(o.local.persistent==e.local.persistent||(e.kind==2&&o.local.session==e.local.session)))return true;return false;}
 Entry*registerOwn(uint64_t now){const int begin=isController?256:0,end=isController?288:256;for(int i=begin;i<end;++i){auto&e=shared->entries[i];if(!e.runtime||!alive(e,now)){e=Entry{};e.runtime=runtime;e.kind=isController?2u:1u;e.heartbeat=now;return&e;}}return nullptr;}
 NativeMapping mapping;Shared*shared=nullptr;Id runtime{};bool isController=false;std::atomic<uint64_t>expectedEpoch{1},publishedLease{0};std::atomic<uint64_t*>leaseAddress{nullptr};
};
}
