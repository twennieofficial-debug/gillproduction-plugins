#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <sddl.h>
 #include <bcrypt.h>
 #ifdef near
  #undef near
 #endif
 #ifdef far
  #undef far
 #endif
 #ifdef small
  #undef small
 #endif
#else
 #include <fcntl.h>
 #include <sys/file.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <time.h>
 #include <unistd.h>
 #if defined(__APPLE__)
  #include <libproc.h>
  #include <dirent.h>
  #include <signal.h>
  #include <cerrno>
 #endif
#endif

namespace gill::mix07 {
struct Id {uint64_t lo=0,hi=0;bool operator==(Id b)const noexcept{return lo==b.lo&&hi==b.hi;}bool operator!=(Id b)const noexcept{return !(*this==b);}explicit operator bool()const noexcept{return lo||hi;}};
inline Id newId(){Id id{};
#if defined(_WIN32)
 if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&id),sizeof(id),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return{};
#elif defined(__APPLE__)
 arc4random_buf(&id,sizeof(id));
#else
 const int f=::open("/dev/urandom",O_RDONLY);if(f<0)return{};const auto n=::read(f,&id,sizeof(id));::close(f);if(n!=sizeof(id))return{};
#endif
 return id;
}
inline uint64_t nowMs()noexcept{
#if defined(_WIN32)
 return GetTickCount64();
#else
 timespec ts{};clock_gettime(CLOCK_MONOTONIC,&ts);return uint64_t(ts.tv_sec)*1000+uint64_t(ts.tv_nsec/1000000);
#endif
}
inline uint64_t hash(const std::string&s){uint64_t h=14695981039346656037ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}return h;}
inline std::string hex(uint64_t x){constexpr const char*d="0123456789abcdef";std::string s(16,'0');for(int i=15;i>=0;--i){s[static_cast<size_t>(i)]=d[x&15];x>>=4;}return s;}
inline std::string idText(Id id){return hex(id.hi)+hex(id.lo);}
inline Id parseId(const std::string&s){if(s.size()!=32)return{};for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return{};return{std::strtoull(s.substr(16).c_str(),nullptr,16),std::strtoull(s.substr(0,16).c_str(),nullptr,16)};}

// Plain aligned wire integers use platform atomics, so no C++ object-lifetime
// construction or standard-library ABI is required inside a shared mapping.
inline uint64_t wireLoad(const uint64_t* p) noexcept {
#if defined(_WIN32)
 return static_cast<uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(const_cast<uint64_t*>(p)),0,0));
#else
 return __atomic_load_n(p,__ATOMIC_ACQUIRE);
#endif
}
inline void wireStore(uint64_t* p,uint64_t v) noexcept {
#if defined(_WIN32)
 InterlockedExchange64(reinterpret_cast<volatile LONG64*>(p),static_cast<LONG64>(v));
#else
 __atomic_store_n(p,v,__ATOMIC_RELEASE);
#endif
}
inline bool wireCompare(uint64_t* p,uint64_t expected,uint64_t value) noexcept {
#if defined(_WIN32)
 return static_cast<uint64_t>(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(p),static_cast<LONG64>(value),static_cast<LONG64>(expected)))==expected;
#else
 return __atomic_compare_exchange_n(p,&expected,value,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
#endif
}

// Metadata-only IPC. No mapping method may run from processBlock or setValue.
class NativeMapping {
public:
 explicit NativeMapping(size_t n):bytes(n){open();}
 ~NativeMapping(){close();}
 NativeMapping(const NativeMapping&)=delete;NativeMapping&operator=(const NativeMapping&)=delete;
 void* data()const noexcept{return memory;}
 bool ensureOpen(){if(!memory){close();open();}return memory!=nullptr;}
 bool lock()noexcept{
#if defined(_WIN32)
  if(!mutex)return false;const auto r=WaitForSingleObject(mutex,0);return r==WAIT_OBJECT_0||r==WAIT_ABANDONED;
#else
  return fd>=0&&flock(fd,LOCK_EX|LOCK_NB)==0;
#endif
 }
 void unlock()noexcept{
#if defined(_WIN32)
  ReleaseMutex(mutex);
#else
  flock(fd,LOCK_UN);
#endif
 }
 struct Lock {NativeMapping&m;bool ok;explicit Lock(NativeMapping&x):m(x),ok(x.lock()){}~Lock(){if(ok)m.unlock();}};
 const std::string&identity()const noexcept{return name;}
private:
#if defined(__APPLE__)
 void cleanDeadMappings(const std::string& directory){
  DIR* dir=opendir(directory.c_str());if(!dir)return;int visited=0,removed=0;
  while(auto* entry=readdir(dir)){if(++visited>2048||removed>=32)break;
   unsigned long long uid=0,start=0;int pid=0,end=0;
   if(std::sscanf(entry->d_name,".gillmix07_%llu_%d_%llx%n",&uid,&pid,&start,&end)!=3||entry->d_name[end]||uid!=getuid()||pid<=0)continue;
   proc_bsdinfo info{};const auto size=proc_pidinfo(pid,PROC_PIDTBSDINFO,0,&info,sizeof(info));
   const bool different=size==sizeof(info)&&(uint64_t(info.pbi_start_tvsec)*1000000+info.pbi_start_tvusec)!=start;
   errno=0;const bool gone=::kill(pid,0)<0&&errno==ESRCH;if(!different&&!gone)continue;
   struct stat state{};if(fstatat(dirfd(dir),entry->d_name,&state,AT_SYMLINK_NOFOLLOW)!=0||state.st_uid!=getuid()||!S_ISREG(state.st_mode)||(state.st_mode&077)||state.st_nlink!=1)continue;
   if(unlinkat(dirfd(dir),entry->d_name,0)==0)++removed;
  }
  closedir(dir);
 }
#endif
 void open(){
#if defined(_WIN32)
  HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return;
  DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);auto*u=static_cast<TOKEN_USER*>(LocalAlloc(LPTR,n));
  const bool ok=u&&GetTokenInformation(token,TokenUser,u,n,&n);CloseHandle(token);if(!ok){if(u)LocalFree(u);return;}
  LPSTR sid=nullptr;if(!ConvertSidToStringSidA(u->User.Sid,&sid)){LocalFree(u);return;}std::string user(sid);LocalFree(sid);LocalFree(u);
  FILETIME c{},e{},k{},t{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&t))return;
  name="Local\\GillMix07_"+hex(hash(user))+"_"+std::to_string(GetCurrentProcessId())+"_"+hex((uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime);
  PSECURITY_DESCRIPTOR sd=nullptr;const auto sddl="D:P(A;;GA;;;"+user+")";
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return;
  SECURITY_ATTRIBUTES a{sizeof(a),sd,FALSE};mutex=CreateMutexA(&a,FALSE,(name+"_lock").c_str());
  mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,&a,PAGE_READWRITE,0,static_cast<DWORD>(bytes),name.c_str());LocalFree(sd);
  if(mutex&&mapping)memory=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,bytes);
#else
  uint64_t birth=0;
 #if defined(__APPLE__)
  proc_bsdinfo info{};if(proc_pidinfo(getpid(),PROC_PIDTBSDINFO,0,&info,sizeof(info))!=sizeof(info))return;
  birth=uint64_t(info.pbi_start_tvsec)*1000000+info.pbi_start_tvusec;
  char temp[4096]{};if(!confstr(_CS_DARWIN_USER_TEMP_DIR,temp,sizeof(temp)))return;name=std::string(temp);
 #else
  FILE*f=std::fopen("/proc/self/stat","r");if(!f)return;char line[4096]{};const auto*r=std::fgets(line,sizeof(line),f);std::fclose(f);if(!r)return;
  const char*p=std::strrchr(line,')');if(!p)return;p+=2;for(int field=3;field<22;++field){p=std::strchr(p,' ');if(!p)return;++p;}birth=std::strtoull(p,nullptr,10);name="/tmp/";
 #endif
  #if defined(__APPLE__)
  cleanDeadMappings(name);
 #endif
  name+=".gillmix07_"+std::to_string(getuid())+"_"+std::to_string(getpid())+"_"+hex(birth);
  fd=::open(name.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW,S_IRUSR|S_IWUSR);if(fd<0)return;fcntl(fd,F_SETFD,FD_CLOEXEC);
  struct stat s{};
  if(fstat(fd,&s)!=0||s.st_uid!=getuid()||!S_ISREG(s.st_mode)||(s.st_mode&077)!=0||s.st_nlink!=1||(s.st_size!=0&&s.st_size!=static_cast<off_t>(bytes)))return;
  if(s.st_size==0){
   if(flock(fd,LOCK_EX|LOCK_NB)!=0)return;
   const bool sized=ftruncate(fd,static_cast<off_t>(bytes))==0;unlock();if(!sized)return;
  }
  memory=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(memory==MAP_FAILED)memory=nullptr;
  // Keep active process metadata to avoid last-unlink/new-open split buses.
  // A later Mac constructor deletes only files whose PID is dead or reused.
  // This bounded cleanup never touches PCM or any live host's mapping.

#endif
 }
 void close(){
#if defined(_WIN32)
  if(memory)UnmapViewOfFile(memory);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);mapping=mutex=nullptr;
#else
  if(memory)munmap(memory,bytes);if(fd>=0)::close(fd);fd=-1;
#endif
  memory=nullptr;
 }
 size_t bytes;void*memory=nullptr;std::string name;
#if defined(_WIN32)
 HANDLE mutex=nullptr,mapping=nullptr;
#else
 int fd=-1;
#endif
};
}
