#include "MixProtocol.h"
#if defined(_WIN32)
 #define EXPORT extern "C" __declspec(dllexport)
#else
 #define EXPORT extern "C" __attribute__((visibility("default")))
#endif
using namespace gill::mix07;
struct Client{MixBus bus;LocalState state{};GainValue gain;explicit Client(bool m):bus(m){state.persistent=newId();state.session=newId();state.gain=gain.snapshot();bus.service(state);}};
EXPORT void*mixCreate(int master){return new Client(master!=0);}
EXPORT void mixDestroy(void*p){delete static_cast<Client*>(p);}
EXPORT Address mixAddress(void*p){auto&c=*static_cast<Client*>(p);return{c.bus.runtimeId(),c.state.epoch};}
EXPORT int mixCount(void*p){return static_cast<Client*>(p)->bus.discover().count;}
EXPORT float mixGain(void*p){return static_cast<Client*>(p)->gain.db();}
EXPORT void mixLocalGain(void*p,float d){auto&c=*static_cast<Client*>(p);c.gain.set(d);c.state.gain=c.gain.snapshot();c.bus.service(c.state);}
EXPORT void mixRestore(void*p){auto&c=*static_cast<Client*>(p);++c.state.epoch;c.state.gain=c.gain.snapshot();c.bus.service(c.state);}
EXPORT int mixConnect(void*p,Address address){return static_cast<int>(static_cast<Client*>(p)->bus.connect(&address,1));}
EXPORT uint64_t mixPrepare(void*p,Address target,float db){auto&c=*static_cast<Client*>(p);const auto d=c.bus.discover();for(int i=0;i<d.count;++i){const auto&r=d.rows[static_cast<size_t>(i)];if(r.address.runtime==target.runtime){Change change{};change.target=target;change.binding=r.binding;change.expectedGain=r.local.gain;change.expectedMetadata=r.local.metadataRevision;change.newDb=db;change.limit=6;uint64_t tx=0;return c.bus.prepare(&change,1,tx)==Status::ok?tx:0;}}return 0;}
EXPORT int mixCommit(void*p,uint64_t tx){return static_cast<int>(static_cast<Client*>(p)->bus.commit(tx));}
EXPORT int mixStep(void*p){auto&c=*static_cast<Client*>(p);c.state.gain=c.gain.snapshot();const auto poll=c.bus.service(c.state);const auto&command=poll.command;if(command.transaction){Ack a{command.transaction,c.state.gain,c.state.metadataRevision,command.phase,Status::conflict,c.state.role,0};if(command.change.expectedGain==c.state.gain){if(command.phase==Phase::prepare)a.status=Status::ready;else if(c.gain.compareSet(command.change.expectedGain,command.change.newDb,a.gain))a.status=Status::applied;}c.bus.acknowledge(a);}return poll.connected?1:0;}
