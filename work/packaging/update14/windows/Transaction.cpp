// Offline Update14 file transaction. All product paths come from a compiled
// manifest. No audio process is killed and no write is scheduled for reboot.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "PayloadIdentity.h"
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"advapi32.lib")
namespace fs=std::filesystem;
struct Error:std::runtime_error{int code;Error(int c,const std::string&s):runtime_error(s),code(c){}};
void need(bool ok,const std::string&s,int code=30){if(!ok)throw Error(code,s+" [win32="+std::to_string(GetLastError())+"]");}
std::wstring wide(const std::string&s){const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0||s.empty(),"Invalid UTF8");std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);return r;}
std::string utf8(const std::wstring&s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::string hex(const unsigned char*p,size_t n){const char*d="0123456789abcdef";std::string s;for(size_t i=0;i<n;++i){s+=d[p[i]>>4];s+=d[p[i]&15];}return s;}
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;explicit Handle(HANDLE v):h(v){}~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;};
bool flStudioRunning(){
 Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));need(snapshot.h!=INVALID_HANDLE_VALUE,"Cannot inspect running audio host",28);
 PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);need(Process32FirstW(snapshot.h,&entry),"Cannot enumerate running audio host",28);
 do{std::wstring name=entry.szExeFile;std::transform(name.begin(),name.end(),name.begin(),towlower);if(name==L"fl.exe"||name==L"fl64.exe"||name==L"fl (scaled).exe"||name==L"fl64 (scaled).exe")return true;}while(Process32NextW(snapshot.h,&entry));
 need(GetLastError()==ERROR_NO_MORE_FILES,"Process enumeration incomplete",28);return false;
}
std::string hashHandle(HANDLE file){
 LARGE_INTEGER start{};need(SetFilePointerEx(file,start,nullptr,FILE_BEGIN),"Cannot seek locked file");
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char result[32],buffer[65536];DWORD n=0;bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
 if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
 while(ok){if(!ReadFile(file,buffer,sizeof(buffer),&n,nullptr)){ok=false;break;}if(!n)break;ok=BCryptHashData(hash,buffer,n,0)>=0;}
 if(ok)ok=BCryptFinishHash(hash,result,sizeof(result),0)>=0;
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);need(ok,"Cannot hash locked file",21);return hex(result,32);
}
std::string hashFile(const fs::path&p){Handle file(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));need(file.h!=INVALID_HANDLE_VALUE,"Cannot read "+p.u8string(),21);return hashHandle(file.h);}
bool pathExists(const fs::path&p){const auto a=GetFileAttributesW(p.c_str());if(a!=INVALID_FILE_ATTRIBUTES)return true;const auto e=GetLastError();need(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND,"Cannot inspect "+p.u8string(),21);return false;}
std::string current(const fs::path&p){return pathExists(p)?hashFile(p):"-";}
void safePath(const fs::path&p){auto a=fs::absolute(p).lexically_normal();fs::path q=a.root_path();for(const auto&part:a.relative_path()){q/=part;const auto bits=GetFileAttributesW(q.c_str());if(bits!=INVALID_FILE_ATTRIBUTES)need(!(bits&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse point refused: "+q.u8string(),22);else{const auto e=GetLastError();need(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND,"Cannot inspect path component: "+q.u8string(),22);}}}
// Each directory is opened without write/delete sharing while a named mutation
// runs. Parents cannot be rebound between reparse inspection and the operation.
struct DirectoryGuards{
 std::vector<HANDLE> handles;
 explicit DirectoryGuards(const fs::path&directory,bool create){
  const auto full=fs::absolute(directory).lexically_normal();fs::path at=full.root_path();
  try{for(const auto&part:full.relative_path()){
   at/=part;if(create&&!pathExists(at))need(CreateDirectoryW(at.c_str(),nullptr)||GetLastError()==ERROR_ALREADY_EXISTS,"Cannot create guarded parent");
   HANDLE h=CreateFileW(at.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
   need(h!=INVALID_HANDLE_VALUE,"Cannot guard directory: "+at.u8string(),22);handles.push_back(h);BY_HANDLE_FILE_INFORMATION info{};
   need(GetFileInformationByHandle(h,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Unsafe guarded directory",22);
  }}catch(...){for(auto h:handles)CloseHandle(h);handles.clear();throw;}
 }
 ~DirectoryGuards(){for(auto h:handles)CloseHandle(h);}
};
bool safeRelative(const std::string&s){if(s.empty()||s.find_first_of(":\t\r\n\"")!=std::string::npos)return false;fs::path p=wide(s);if(p.is_absolute()||p.has_root_path())return false;for(auto part:p)if(part==L".."||part==L"."||part.empty()||part.wstring().back()==L'.'||part.wstring().back()==L' ')return false;return true;}
void parents(const fs::path&p){safePath(p);std::error_code ec;fs::create_directories(p.parent_path(),ec);need(!ec,"Cannot create parent");safePath(p);}
void durable(const fs::path&p,const std::string&text){DirectoryGuards guard(p.parent_path(),true);Handle h(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));need(h.h!=INVALID_HANDLE_VALUE,"Cannot write journal");BY_HANDLE_FILE_INFORMATION info{};need(GetFileInformationByHandle(h.h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Unsafe journal file",22);LARGE_INTEGER start{};need(SetFilePointerEx(h.h,start,nullptr,FILE_BEGIN)&&SetEndOfFile(h.h),"Cannot truncate journal");DWORD n=0;need(WriteFile(h.h,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size()&&FlushFileBuffers(h.h),"Cannot flush journal");}
std::string readText(const fs::path&p){std::ifstream in(p,std::ios::binary);need(bool(in),"Cannot read control file");return std::string(std::istreambuf_iterator<char>(in),{});}
void copyExact(const fs::path&a,const fs::path&b){DirectoryGuards sourceGuard(a.parent_path(),false),targetGuard(b.parent_path(),true);safePath(a);need(CopyFileW(a.c_str(),b.c_str(),TRUE),"Cannot stage file");}
void unlockCheck(const fs::path&p){if(!pathExists(p))return;HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);need(h!=INVALID_HANDLE_VALUE,"File in use or not writable: "+p.u8string(),20);CloseHandle(h);}
void renameLocked(const fs::path&from,const fs::path&to,const std::string&expected){
 DirectoryGuards sourceGuard(from.parent_path(),false),targetGuard(to.parent_path(),true);
 Handle h(CreateFileW(from.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));need(h.h!=INVALID_HANDLE_VALUE,"Cannot exclusively rename source",20);
 BY_HANDLE_FILE_INFORMATION info{};need(GetFileInformationByHandle(h.h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse file refused",22);
 need(hashHandle(h.h)==expected,"Locked source changed",24);const auto name=fs::absolute(to).wstring();
 std::vector<unsigned char>storage(sizeof(FILE_RENAME_INFO)+(name.size()+1)*sizeof(wchar_t));auto*r=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
 r->ReplaceIfExists=FALSE;r->RootDirectory=nullptr;r->FileNameLength=DWORD(name.size()*sizeof(wchar_t));memcpy(r->FileName,name.c_str(),r->FileNameLength+sizeof(wchar_t));
 need(SetFileInformationByHandle(h.h,FileRenameInfo,r,DWORD(storage.size())),"Guarded rename failed",20);
}
bool testCrashEviction=false;
void replace(const fs::path&from,const fs::path&to,const std::string&expectedBefore){
 DirectoryGuards targetGuard(to.parent_path(),true);const auto newHash=hashFile(from);
 if(expectedBefore!="-"){need(current(to)==expectedBefore,"Destination changed before replacement",24);renameLocked(to,fs::path(from.wstring()+L".old"),expectedBefore);if(testCrashEviction)TerminateProcess(GetCurrentProcess(),97);}
 else need(!pathExists(to),"Unexpected new destination",24);
 // Never overwrite a path that another writer creates in the brief vacancy.
 renameLocked(from,to,newHash);
}
void removeExact(const fs::path&p,const std::string&expected=""){
 safePath(p);if(!pathExists(p))return;DirectoryGuards guard(p.parent_path(),false);
 Handle h(CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));need(h.h!=INVALID_HANDLE_VALUE,"Cannot exclusively remove file",20);
 BY_HANDLE_FILE_INFORMATION info{};need(GetFileInformationByHandle(h.h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Reparse file refused",22);
 if(!expected.empty())need(hashHandle(h.h)==expected,"File changed before conditional removal",24);FILE_DISPOSITION_INFO disposition{TRUE};need(SetFileInformationByHandle(h.h,FileDispositionInfo,&disposition,sizeof(disposition)),"Conditional deletion failed",20);
}
struct Row{char root='P';std::string rel,sha;uint64_t bytes=0;std::string origin;};
std::vector<Row> payload(){std::vector<Row> rows;std::istringstream in(gillPayload);std::string line;std::set<std::wstring> seen;while(std::getline(in,line)){if(line.empty())continue;std::istringstream fields(line);Row r;std::string a,b,c,d,e;need(bool(std::getline(fields,a,'\t')&&std::getline(fields,b,'\t')&&std::getline(fields,c,'\t')&&std::getline(fields,d,'\t')&&std::getline(fields,e)) ,"Bad compiled manifest");need(a=="P"||a=="S","Unknown root");r.root=a[0];r.rel=b;r.sha=c;r.bytes=std::stoull(d);r.origin=e;need(safeRelative(b)&&c.size()==64,"Unsafe manifest entry");auto key=wide(a+"/"+b);std::transform(key.begin(),key.end(),key.begin(),towlower);need(seen.insert(key).second,"Duplicate target");rows.push_back(r);}return rows;}
struct Op{Row row;std::string before,after;};
// Keep both the release-independent lock and the previous installer's lock.
// Each guard is a member, so partial init failures release only locks we own.
struct InstallMutex {
 HANDLE handle=nullptr;bool owned=false;
 InstallMutex()=default;InstallMutex(const InstallMutex&)=delete;
 ~InstallMutex(){if(handle){if(owned)ReleaseMutex(handle);CloseHandle(handle);}}
 void acquire(const std::wstring&name){
  need(!handle,"Install mutex acquired twice",23);
  SetLastError(ERROR_SUCCESS);handle=CreateMutexW(nullptr,TRUE,name.c_str());const auto result=GetLastError();
  owned=handle&&result!=ERROR_ALREADY_EXISTS;
  need(owned,"Another setup is running",23);
 }
};
struct App{
 fs::path plugins,support,payloadRoot,control,journal;std::vector<Row> rows;int failAfter=-1,crashAfter=-1,crashCleanup=-1;bool additive=false,remove=false,pauseRollback=false;std::array<InstallMutex,2> mutexes;
#ifdef GILL_TEST_BUILD
 bool requireFLClosed=false;
#else
 bool requireFLClosed=true;
#endif
 fs::path target(const Row&r)const{return(r.root=='P'?plugins:support)/wide(r.rel);}
 fs::path backup(size_t i)const{return journal/L"backup"/std::to_wstring(i);}
 fs::path staged(size_t i)const{return journal/L"stage"/std::to_wstring(i);}
 void configure(const std::wstring&testcase){
#ifdef GILL_TEST_BUILD
  need(!testcase.empty()&&testcase.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::wstring::npos,"Invalid isolated case",22);
  fs::path root=fs::path(gillTestRoot)/testcase;plugins=root/L"Plugins";support=root/L"Support";
#else
  need(testcase.empty(),"Test arguments refused in production",22);PWSTR c=nullptr,p=nullptr;need(SHGetKnownFolderPath(FOLDERID_ProgramFilesCommonX64,0,nullptr,&c)==S_OK&&SHGetKnownFolderPath(FOLDERID_ProgramFilesX64,0,nullptr,&p)==S_OK,"Standard folders unavailable");plugins=fs::path(c)/L"VST3/GILLPRODUCTION";support=fs::path(p)/L"GILLPRODUCTION";CoTaskMemFree(c);CoTaskMemFree(p);
#endif
  safePath(plugins);safePath(support);need(plugins.root_name()==support.root_name(),"Plugin/support volumes differ",22);journal=support/L".gill14-transaction";
 }
 std::array<std::wstring,2> mutexNames()const{
  const auto key=std::to_wstring(std::hash<std::wstring>{}(plugins.wstring()));
  return {L"Local\\GILL_INSTALL_"+key,L"Local\\GILL12_"+key};
 }
 void rejectOtherJournals(){
  if(!pathExists(support))return;
  DirectoryGuards guard(support,false);std::error_code error;
  fs::directory_iterator it(support,error),end;need(!error,"Cannot inspect previous setup journals",24);
  while(it!=end){
   auto name=it->path().filename().wstring();std::transform(name.begin(),name.end(),name.begin(),towlower);
   const std::wstring suffix=L"-transaction";
   const bool journalName=name.rfind(L".gill",0)==0&&name.size()>=suffix.size()&&name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0;
   // Never reinterpret another payload's recovery data, even if COMMITTED.
   // Its own installer must finish recovery before this release can write.
   need(!journalName||name==L".gill14-transaction","Previous setup journal preserved. Complete recovery with its original installer first.",24);
   it.increment(error);need(!error,"Cannot finish previous setup journal inspection",24);
  }
 }
 void init(const std::wstring&testcase){
  configure(testcase);const auto names=mutexNames();
  for(size_t i=0;i<mutexes.size();++i)mutexes[i].acquire(names[i]);
  rejectOtherJournals();rows=payload();
 }
 void saveRegistry(){
#ifndef GILL_TEST_BUILD
  HKEY key=nullptr;const auto status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GILLPRODUCTION.VST3",0,KEY_READ|KEY_WOW64_64KEY,&key);std::string data;
  if(status==ERROR_FILE_NOT_FOUND)data="ABSENT\n";else{need(status==ERROR_SUCCESS,"Cannot read registration");data="PRESENT\n";for(DWORD index=0;;++index){wchar_t name[16384];DWORD count=16384,type=0,size=0;const auto r=RegEnumValueW(key,index,name,&count,nullptr,&type,nullptr,&size);if(r==ERROR_NO_MORE_ITEMS)break;need(r==ERROR_SUCCESS,"Cannot enumerate registration");std::vector<unsigned char>value(size);DWORD again=count+1;need(RegEnumValueW(key,index,name,&again,nullptr,&type,value.data(),&size)==ERROR_SUCCESS,"Cannot snapshot registration");data+=hex(reinterpret_cast<const unsigned char*>(name),count*sizeof(wchar_t))+"\t"+std::to_string(type)+"\t"+hex(value.data(),size)+"\n";}RegCloseKey(key);}durable(journal/L"registry.txt",data);
#endif
 }
 void restoreRegistry(){
#ifndef GILL_TEST_BUILD
  if(!pathExists(journal/L"registry.txt"))return;const auto bytes=readText(journal/L"registry.txt");need(bytes.rfind("ABSENT\n",0)==0||bytes.rfind("PRESENT\n",0)==0,"Invalid registry journal");auto decode=[](const std::string&s){need(s.size()%2==0,"Invalid registry hex");std::vector<unsigned char>v;for(size_t n=0;n<s.size();n+=2)v.push_back(static_cast<unsigned char>(std::stoul(s.substr(n,2),nullptr,16)));return v;};const wchar_t*path=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GILLPRODUCTION.VST3";
  HKEY parent=nullptr;need(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,&parent)==ERROR_SUCCESS,"Cannot restore registration");RegDeleteTreeW(parent,L"GILLPRODUCTION.VST3");RegCloseKey(parent);if(bytes.rfind("ABSENT",0)==0)return;
  HKEY key=nullptr;need(RegCreateKeyExW(HKEY_LOCAL_MACHINE,path,0,nullptr,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,nullptr,&key,nullptr)==ERROR_SUCCESS,"Cannot restore key");std::istringstream stream(bytes);std::string line;std::getline(stream,line);while(std::getline(stream,line)){std::istringstream fields(line);std::string a,b,c;need(bool(std::getline(fields,a,'\t')&&std::getline(fields,b,'\t')&&std::getline(fields,c)),"Invalid registry record");auto name=decode(a),value=decode(c);name.push_back(0);name.push_back(0);need(RegSetValueExW(key,reinterpret_cast<wchar_t*>(name.data()),0,std::stoul(b),value.data(),DWORD(value.size()))==ERROR_SUCCESS,"Cannot restore value");}RegCloseKey(key);
#endif
 }
 void registerResult(){
#ifndef GILL_TEST_BUILD
  const wchar_t*path=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GILLPRODUCTION.VST3";if(remove){HKEY parent=nullptr;need(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,&parent)==ERROR_SUCCESS,"Cannot open uninstall parent");const auto r=RegDeleteTreeW(parent,L"GILLPRODUCTION.VST3");RegCloseKey(parent);need(r==ERROR_SUCCESS||r==ERROR_FILE_NOT_FOUND,"Cannot remove registration");return;}
  HKEY key=nullptr;need(RegCreateKeyExW(HKEY_LOCAL_MACHINE,path,0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,nullptr,&key,nullptr)==ERROR_SUCCESS,"Cannot register installation");auto set=[&](const wchar_t*n,const std::wstring&v){need(RegSetValueExW(key,n,0,REG_SZ,reinterpret_cast<const BYTE*>(v.c_str()),DWORD((v.size()+1)*2))==ERROR_SUCCESS,"Cannot write registration");};set(L"DisplayName",L"GILLPRODUCTION VST3 Bundle");set(L"DisplayVersion",L"0.14.0");set(L"Publisher",L"GILLPRODUCTION");set(L"InstallLocation",plugins.wstring());set(L"UninstallString",L"\""+(support/L"Uninstall.exe").wstring()+L"\"");set(L"QuietUninstallString",L"\""+(support/L"Uninstall.exe").wstring()+L"\" /S");RegCloseKey(key);
#endif
 }
 std::string serialize(const std::vector<Op>&ops){std::string s=std::string(gillPayloadSha)+"\n";for(const auto&o:ops)s+=std::string(1,o.row.root)+"\t"+o.row.rel+"\t"+o.before+"\t"+o.after+"\n";return s;}
 std::vector<Op> readJournal(){const auto text=readText(journal/L"operations.tsv");std::istringstream in(text);std::string line;std::getline(in,line);need(line==gillPayloadSha,"Journal belongs to another payload",24);std::vector<Op>ops;while(std::getline(in,line)){std::istringstream f(line);std::string a,b,c,d;need(bool(std::getline(f,a,'\t')&&std::getline(f,b,'\t')&&std::getline(f,c,'\t')&&std::getline(f,d)),"Invalid journal");auto found=std::find_if(rows.begin(),rows.end(),[&](const Row&r){return std::string(1,r.root)==a&&r.rel==b;});need(found!=rows.end(),"Journal has an unowned path",24);need((c=="-"||c.size()==64)&&(d=="-"||d.size()==64),"Journal hashes invalid");ops.push_back({*found,c,d});}return ops;}
 void cleanup(const std::vector<Op>&ops){for(size_t i=0;i<ops.size();++i){removeExact(backup(i));removeExact(staged(i));removeExact(fs::path(staged(i).wstring()+L".old"));const auto r=journal/L"restore"/std::to_wstring(i);removeExact(r);removeExact(fs::path(r.wstring()+L".old"));}removeExact(journal/L"ROLLBACK-READY");int point=0;for(auto n:{L"registry.txt",L"PREPARED",L"operations.tsv",L"COMMITTED"}){removeExact(journal/n);if(crashCleanup==point++)TerminateProcess(GetCurrentProcess(),98);}for(auto n:{L"backup",L"stage",L"restore"})RemoveDirectoryW((journal/n).c_str());RemoveDirectoryW(journal.c_str());}
 void rollback(const std::vector<Op>&ops){
  // Validate every target before rolling back any. Preserve concurrent edits.
  for(const auto&o:ops){auto now=current(target(o.row));need(now==o.before||now==o.after||now=="-","Rollback refused a concurrent file change",24);if(now!=o.before)unlockCheck(target(o.row));}
  if(pauseRollback){durable(journal/L"ROLLBACK-READY","1\n");Sleep(1000);}
  for(size_t i=ops.size();i-->0;){const auto&o=ops[i];auto dest=target(o.row);const auto now=current(dest);if(now==o.before)continue;need(now==o.after||now=="-","Concurrent edit during rollback preserved",24);if(o.before=="-")removeExact(dest,o.after);else{need(current(backup(i))==o.before,"Rollback backup differs",24);const auto tmp=journal/L"restore"/std::to_wstring(i);removeExact(tmp);copyExact(backup(i),tmp);replace(tmp,dest,now);need(current(dest)==o.before,"Restored bytes differ",24);}}
  restoreRegistry();cleanup(ops);std::cout<<"ROLLED BACK\n";
 }
 void recover(){if(!pathExists(journal))return;safePath(journal);if(!pathExists(journal/L"operations.tsv")){
  // Operations are removed only after all indexed backup/stage files. A crash
  // in final marker cleanup must not permanently strand a successful commit.
  need(!pathExists(journal/L"PREPARED")||pathExists(journal/L"COMMITTED"),"Incomplete prepared journal",24);
  for(auto n:{L"registry.txt",L"PREPARED",L"COMMITTED"})removeExact(journal/n);
  for(auto n:{L"backup",L"stage",L"restore"})RemoveDirectoryW((journal/n).c_str());
  need(RemoveDirectoryW(journal.c_str()),"Unrecognized journal files preserved",24);return;
 }auto ops=readJournal();if(pathExists(journal/L"COMMITTED"))cleanup(ops);else if(pathExists(journal/L"PREPARED"))rollback(ops);else cleanup(ops);}
 void run(){
  if(requireFLClosed)need(!flStudioRunning(),"FL Studio is open. Save your project and close FL Studio before installing or uninstalling.",28);
  // The two fixed control paths are owned explicitly; no arbitrary receipt paths.
  rows.push_back({'S',"Uninstall.exe","",0,"control"});rows.push_back({'S',"Install14/receipt.tsv","",0,"control"});recover();
  const fs::path receiptPath=support/L"Install14/receipt.tsv";
  std::vector<Op>ops;size_t kept=0;uint64_t needed=0;
  if(remove){
   need(pathExists(receiptPath),"Missing ownership receipt",24);const auto receipt=readText(receiptPath);std::istringstream in(receipt);std::string line;std::getline(in,line);need(line==gillPayloadSha,"Ownership receipt differs",24);std::set<std::string>owned;while(std::getline(in,line)){std::istringstream f(line);std::string a,b,c;need(bool(std::getline(f,a,'\t')&&std::getline(f,b,'\t')&&std::getline(f,c)),"Invalid receipt");auto it=std::find_if(rows.begin(),rows.end(),[&](const Row&r){return std::string(1,r.root)==a&&r.rel==b;});need(it!=rows.end()&&owned.insert(a+"/"+b).second,"Receipt path is not owned",24);auto now=current(target(*it));need(now=="-"||now==c,"Modified owned file preserved; uninstall stopped",24);if(now!="-")ops.push_back({*it,now,"-"});}
   need(owned.size()==rows.size()-1,"Incomplete ownership receipt",24);ops.push_back({rows.back(),hashFile(receiptPath),"-"});for(const auto&o:ops)needed+=2*fs::file_size(target(o.row));
  }else{
   need(pathExists(control),"Uninstaller control file missing");rows[rows.size()-2].sha=hashFile(control);rows[rows.size()-2].bytes=fs::file_size(control);
   std::string receipt=gillPayloadSha;receipt+='\n';for(size_t i=0;i+1<rows.size();++i){const auto&r=rows[i];receipt+=std::string(1,r.root)+"\t"+r.rel+"\t"+r.sha+"\n";}
   const auto generated=payloadRoot/L"receipt.generated.tsv";if(pathExists(generated))need(readText(generated)==receipt,"Generated receipt changed");else durable(generated,receipt);rows.back().sha=hashFile(generated);rows.back().bytes=fs::file_size(generated);
   // Reserve the additional rollback copy too: evicted originals and staged
   // replacements stay available until the complete transaction is resolved.
   for(size_t i=0;i<rows.size();++i){const auto&r=rows[i];const auto dest=target(r);safePath(dest);const auto now=current(dest);const auto source=i+1==rows.size()?generated:i+2==rows.size()?control:payloadRoot/fs::path(std::string(1,r.root))/wide(r.rel);need(fs::file_size(source)==r.bytes&&hashFile(source)==r.sha,"Payload bytes differ",25);if(now==r.sha){++kept;continue;}ops.push_back({r,now,r.sha});needed+=r.bytes;if(now!="-")needed+=2*fs::file_size(dest);}
  }
  for(const auto&o:ops)unlockCheck(target(o.row));
  if(ops.empty()){std::cout<<"UNCHANGED kept="<<kept<<"\n";return;}
  safePath(journal);parents(journal/L"operations.tsv");ULARGE_INTEGER free{};need(GetDiskFreeSpaceExW(support.c_str(),&free,nullptr,nullptr)&&free.QuadPart>needed+4*1024*1024,"Insufficient target disk space",27);
  try{
   durable(journal/L"operations.tsv",serialize(ops));
   for(size_t i=0;i<ops.size();++i){const auto&o=ops[i];if(o.before!="-"){copyExact(target(o.row),backup(i));need(hashFile(backup(i))==o.before,"Backup differs");}if(o.after!="-"){const auto src=o.row.rel=="Uninstall.exe"&&o.row.origin=="control"?control:o.row.rel=="Install14/receipt.tsv"&&o.row.origin=="control"?payloadRoot/L"receipt.generated.tsv":payloadRoot/fs::path(std::string(1,o.row.root))/wide(o.row.rel);copyExact(src,staged(i));need(hashFile(staged(i))==o.after,"Staging differs");}}
   saveRegistry();durable(journal/L"PREPARED","1\n");
   int written=0;for(size_t i=0;i<ops.size();++i){const auto&o=ops[i];need(current(target(o.row))==o.before,"Destination changed after preflight",24);if(failAfter==written)throw Error(29,"Injected test failure");if(crashAfter==written)TerminateProcess(GetCurrentProcess(),99);if(o.after=="-")removeExact(target(o.row),o.before);else replace(staged(i),target(o.row),o.before);need(current(target(o.row))==o.after,"Installed hash differs");++written;}
   if(failAfter==written)throw Error(29,"Injected end-of-commit failure");if(crashAfter==written)TerminateProcess(GetCurrentProcess(),99);
   for(const auto&o:ops)need(current(target(o.row))==o.after,"Final transaction hash differs");
   if(!remove)for(const auto&r:rows)need(current(target(r))==r.sha,"Final retained/payload hash differs");
   registerResult();durable(journal/L"COMMITTED","1\n");cleanup(ops);std::cout<<"COMMITTED changed="<<ops.size()<<" kept="<<kept<<"\n";
  }catch(...){auto error=std::current_exception();if(pathExists(journal/L"COMMITTED"))std::rethrow_exception(error);if(pathExists(journal/L"PREPARED"))rollback(ops);else cleanup(ops);std::rethrow_exception(error);}
 }
};
int wmain(int argc,wchar_t**argv){try{
#ifdef GILL_TEST_BUILD
 if(argc==2&&std::wstring(argv[1])==L"--wait-fl-fixture"){Sleep(60000);return 0;}
 if(argc==2&&std::wstring(argv[1])==L"--check-fl-process")return flStudioRunning()?28:0;
#endif
 App app;std::wstring testcase;bool inspectTestPath=false,printInstallLocks=false,testLockUnwind=false;for(int i=1;i<argc;++i){std::wstring a=argv[i];auto value=[&](){need(i+1<argc,"Missing argument");return std::wstring(argv[++i]);};if(a==L"--payload")app.payloadRoot=value();else if(a==L"--uninstaller")app.control=value();else if(a==L"--remove")app.remove=true;else if(a==L"--additive")app.additive=true;
#ifdef GILL_TEST_BUILD
 else if(a==L"--case")testcase=value();else if(a==L"--require-fl-closed")app.requireFLClosed=true;else if(a==L"--fail-after")app.failAfter=std::stoi(value());else if(a==L"--crash-after")app.crashAfter=std::stoi(value());else if(a==L"--crash-cleanup")app.crashCleanup=std::stoi(value());else if(a==L"--crash-eviction")testCrashEviction=true;else if(a==L"--pause-rollback")app.pauseRollback=true;else if(a==L"--inspect-reparse-case"){testcase=value();inspectTestPath=true;}else if(a==L"--print-install-locks")printInstallLocks=true;else if(a==L"--test-lock-unwind")testLockUnwind=true;
#endif
 else throw Error(22,"Unknown argument");}
#ifdef GILL_TEST_BUILD
 if(inspectTestPath){need(!testcase.empty()&&testcase.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::wstring::npos,"Invalid inspection case",22);const auto p=fs::path(gillTestInspectionRoot)/testcase/L"Plugins";safePath(p);DirectoryGuards readOnly(p,false);return 0;}
#endif
#ifdef GILL_TEST_BUILD
 if(printInstallLocks||testLockUnwind){
  app.configure(testcase);const auto names=app.mutexNames();
  if(printInstallLocks){for(const auto&name:names)std::cout<<utf8(name)<<"\n";return 0;}
  InstallMutex legacy;legacy.acquire(names[1]);bool refused=false;
  {App blocked;try{blocked.init(testcase);}catch(const Error&e){need(e.code==23,"Unexpected lock test error");refused=true;}}
  need(refused,"Previous-release mutex did not block setup");
  // Same process: successful acquisition proves exception unwinding released
  // the first mutex, without relying on process exit to discard leaked locks.
  InstallMutex afterFailure;afterFailure.acquire(names[0]);return 0;
 }
#endif
 app.init(testcase);app.run();return 0;}catch(const Error&e){std::cerr<<e.what()<<"\n";return e.code;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 30;}}
