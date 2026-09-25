#include "MixProtocol.h"
#include <iostream>
#include <memory>
#if !defined(_WIN32)
#include <dlfcn.h>
#include <sys/wait.h>
#endif
using namespace gill::mix07;
int checks=0,failures=0;void check(bool b,const char*m){++checks;if(!b){++failures;std::cerr<<"FAIL "<<m<<'\n';}}
class Module{public:explicit Module(const char*path){
#if defined(_WIN32)
 h=LoadLibraryA(path);
#else
 h=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#endif
 }~Module(){
#if defined(_WIN32)
 if(h)FreeLibrary(h);
#else
 if(h)dlclose(h);
#endif
 }template<class T>T get(const char*name){
#if defined(_WIN32)
 return reinterpret_cast<T>(GetProcAddress(h,name));
#else
 return reinterpret_cast<T>(dlsym(h,name));
#endif
 }bool ok()const{return h!=nullptr;}
private:
#if defined(_WIN32)
 HMODULE h=nullptr;
#else
 void*h=nullptr;
#endif
};
int main(int argc,char**argv){
 if(argc==2&&std::string(argv[1])=="--isolated-child"){MixBus bus(true);LocalState s{};s.persistent=newId();s.session=newId();bus.service(s);return bus.discover().count?1:0;}
 if(argc!=3)return 2;Module a(argv[1]),b(argv[2]);check(a.ok()&&b.ok(),"two actual native libraries loaded");if(!a.ok()||!b.ok())return 1;
 using Make=void*(*)(int);using Destroy=void(*)(void*);using AddressFn=Address(*)(void*);using Count=int(*)(void*);using Gain=float(*)(void*);using Set=void(*)(void*,float);using Connect=int(*)(void*,Address);using Prepare=uint64_t(*)(void*,Address,float);using Commit=int(*)(void*,uint64_t);
 auto makeA=a.get<Make>("mixCreate"),makeB=b.get<Make>("mixCreate");auto destroyA=a.get<Destroy>("mixDestroy"),destroyB=b.get<Destroy>("mixDestroy");
 auto count=a.get<Count>("mixCount"),step=b.get<Count>("mixStep");auto address=b.get<AddressFn>("mixAddress");auto getGain=b.get<Gain>("mixGain");auto set=b.get<Set>("mixLocalGain");auto connect=a.get<Connect>("mixConnect");auto prepare=a.get<Prepare>("mixPrepare");auto commit=a.get<Commit>("mixCommit");auto restore=b.get<Destroy>("mixRestore");
 check(makeA&&makeB&&destroyA&&destroyB&&count&&step&&address&&getGain&&set&&connect&&prepare&&commit&&restore,"test ABI functions explicitly exported");if(!makeA||!makeB||!count||!address||!connect)return 1;
 void*master=makeA(1),*link=makeB(0),*untouched=makeB(0);check(count(master)==2,"discovery crosses independent library implementations");auto target=address(link);check(connect(master,target)==static_cast<int>(Status::ok),"explicit cross-DLL binding");const auto tx=prepare(master,target,-3);check(tx!=0,"cross-DLL prepare accepted");step(link);check(getGain(link)==0,"prepare does not change audio gain");check(commit(master,tx)==static_cast<int>(Status::ok),"cross-DLL ready ACK permits commit");step(link);check(getGain(link)==-3&&getGain(untouched)==0,"commit affects only selected real DLL client");step(link);check(getGain(link)==-3,"repeated service does not replay commit");
 const auto stale=prepare(master,target,-1);set(link,2);step(link);check(commit(master,stale)==static_cast<int>(Status::conflict)&&getGain(link)==2,"intervening local edit rejects prepared remote set");restore(link);check(step(link)==0&&getGain(link)==2,"state recall revokes binding but preserves local gain");
#if defined(_WIN32)
 std::string line="\""+std::string(argv[0])+"\" --isolated-child";STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};bool child=CreateProcessA(nullptr,line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;DWORD code=1;if(child){if(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}check(child&&code==0,"another native host process cannot discover parent links");
#else
 const auto pid=fork();if(pid==0){execl(argv[0],argv[0],"--isolated-child",static_cast<char*>(nullptr));_exit(2);}int status=0;check(pid>0&&waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"another native host process cannot discover parent links");
#endif
 destroyB(link);check(count(master)==1,"unload unregisters exact client");destroyB(untouched);destroyA(master);std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
