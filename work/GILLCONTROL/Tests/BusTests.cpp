#include "../../GILLCommon/QualityBus.h"
#include <iostream>
#include <vector>
#if !defined(_WIN32)
 #include <dlfcn.h>
 #include <sys/wait.h>
#endif
namespace {
int passed = 0, failed = 0;
void check(bool ok, const char* text) { if (ok) ++passed; else { ++failed; std::cerr << "FAIL " << text << '\n'; } }
class Module {
public:
    explicit Module(const char* path) {
#if defined(_WIN32)
        handle = LoadLibraryA(path);
#else
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
        create = reinterpret_cast<decltype(create)>(symbol("qualityCreate"));
        destroy = reinterpret_cast<decltype(destroy)>(symbol("qualityDestroy"));
        poll = reinterpret_cast<decltype(poll)>(symbol("qualityPoll"));
        broadcast = reinterpret_cast<decltype(broadcast)>(symbol("qualityBroadcast"));
        expire = reinterpret_cast<decltype(expire)>(symbol("qualityExpire"));
    }
    ~Module() {
#if defined(_WIN32)
        if (handle) FreeLibrary(handle);
#else
        if (handle) dlclose(handle);
#endif
    }
    bool valid() const { return handle && create && destroy && poll && broadcast && expire; }
    void* (*create)(int) = nullptr; void (*destroy)(void*) = nullptr;
    int (*poll)(void*, int*, std::uint64_t*) = nullptr;
    int (*broadcast)(void*, int) = nullptr; void (*expire)(void*) = nullptr;
private:
    void* symbol(const char* name) {
#if defined(_WIN32)
        return handle ? reinterpret_cast<void*>(GetProcAddress(handle, name)) : nullptr;
#else
        return handle ? dlsym(handle, name) : nullptr;
#endif
    }
#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
};
bool isolatedChild(const char* exe, const std::string& mapping) {
#if defined(_WIN32)
    std::string args = std::string("\"") + exe + "\" --child \"" + mapping + "\"";
    STARTUPINFOA startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
    if (!CreateProcessA(nullptr, args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return false;
    const auto wait = WaitForSingleObject(child.hProcess, 10000); DWORD result = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &result); else TerminateProcess(child.hProcess, 2);
    CloseHandle(child.hThread); CloseHandle(child.hProcess); return wait == WAIT_OBJECT_0 && result == 0;
#else
    const auto pid = fork(); if (pid < 0) return false;
    if (pid == 0) { execl(exe, exe, "--child", mapping.c_str(), static_cast<char*>(nullptr)); _exit(2); }
    int result = 0; return waitpid(pid, &result, 0) == pid && WIFEXITED(result) && WEXITSTATUS(result) == 0;
#endif
}
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    if (argc == 3 && std::string(argv[1]) == "--child") {
        gill::quality_detail::Bus child;
        const auto result = child.poll();
        return result.valid && result.instances == 1 && result.sequence == 0 && result.mode == 1 && child.mappingNameForTest() != argv[2] ? 0 : 1;
    }
    if (argc != 3) return 2;
    Module a(argv[1]), b(argv[2]); check(a.valid() && b.valid(), "load two independently compiled shared libraries");
    if (!a.valid() || !b.valid()) return 2;
    {
        void* master = a.create(1); void* first = a.create(0); void* second = b.create(0);
        int mode = -1; std::uint64_t sequence = 0;
        check(b.poll(second, &mode, &sequence) == 2 && mode == 1, "two registered clients across DLLs, master excluded");
        check(a.broadcast(master, 0) == 1, "master broadcasts LIVE");
        check(b.poll(second, &mode, &sequence) == 2 && mode == 0 && sequence == 1, "DLL B receives DLL A command");
        check(a.poll(first, &mode, &sequence) == 2 && mode == 0, "DLL A receives same command");
        for (int i = 0; i < 1000; ++i) {
            const int target = i % 2;
            check(a.broadcast(master, target) == 1, "global command delivered");
            check(b.poll(second, &mode, &sequence) == 2 && mode == target && sequence == static_cast<std::uint64_t>(i + 2), "cross-DLL command ordering");
        }
        check(a.broadcast(master, 1) == 1, "repeat same mode");
        check(b.poll(second, &mode, &sequence) == 2 && sequence == 1002, "repeat still increments sequence");
        a.destroy(first); check(b.poll(second, &mode, &sequence) == 1, "unregister removes client immediately");
        a.destroy(master); master = b.create(1);
        check(b.poll(master, &mode, &sequence) == 1 && mode == 1 && sequence == 1002, "controller reopen retains shared command while clients live");
        b.expire(second); check(b.poll(master, &mode, &sequence) == 0, "stale heartbeat excluded from count");
        auto* replacement = a.create(0);
        check(a.poll(replacement, &mode, &sequence) == 1, "stale slot reclaimed");
        check(b.poll(second, &mode, &sequence) == 2, "stale but resumed client safely re-registers");
        { gill::quality_detail::Bus parent; parent.broadcast(0); check(isolatedChild(argv[0], parent.mappingNameForTest()), "native child DAW process is isolated from parent"); }
        a.destroy(replacement); b.destroy(second); b.destroy(master);
    }
    { gill::quality_detail::Bus fresh; const auto result = fresh.poll(); check(result.valid && result.instances == 1 && result.sequence == 0 && result.mode == 1, "last close clears stale state for a new session"); }
    std::cout << passed << " checks passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
