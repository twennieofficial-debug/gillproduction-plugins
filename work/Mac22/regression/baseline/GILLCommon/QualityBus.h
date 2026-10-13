#pragma once

// GILL quality protocol v6. Only MessageThread/constructor/destructor code may
// access the OS-backed bus. Audio processing reads QualityClient::mode() only.
// The mapping contains fixed-width POD, never pointers, STL objects or JUCE data.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <sddl.h>
 // Win16 compatibility macros must not leak into downstream DSP identifiers.
 #ifdef near
  #undef near
 #endif
 #ifdef far
  #undef far
 #endif
 #ifdef small
  #undef small
 #endif
 #if defined(_MSC_VER)
  #pragma comment(lib, "advapi32.lib")
 #endif
#else
 #include <cerrno>
 #include <cstdio>
 #include <fcntl.h>
 #include <sys/file.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <time.h>
 #include <unistd.h>
 #if defined(__APPLE__)
  #include <libproc.h>
 #endif
#endif

namespace gill {
inline constexpr const char* qualityParameterId = "gillQuality";
inline constexpr int liveQuality = 0, proQuality = 1;

namespace quality_detail {
inline std::uint64_t milliseconds() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetTickCount64());
#else
    timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000u + static_cast<std::uint64_t>(ts.tv_nsec / 1000000);
#endif
}
inline std::uint64_t hash(const std::string& s) noexcept {
    std::uint64_t h = UINT64_C(14695981039346656037);
    for (unsigned char c : s) { h ^= c; h *= UINT64_C(1099511628211); }
    return h;
}
inline std::string hex(std::uint64_t v) {
    const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[static_cast<std::size_t>(i)] = digits[v & 15]; v >>= 4; }
    return out;
}
struct Slot { std::uint64_t token, heartbeat; std::uint32_t controller, reserved; };
struct Shared {
    std::uint64_t magic, nextToken, sequence;
    std::uint32_t version, bytes, mode, reserved;
    Slot slots[512];
};
static_assert(sizeof(Slot) == 24 && sizeof(Shared) == 12328, "IPC layout must match across modules");
inline constexpr std::uint64_t magic = UINT64_C(0x47494c4c51554136);
inline constexpr std::uint64_t staleAfterMs = 120000;

// One mapping per participant deliberately avoids DLL-local singleton state.
// On POSIX, flock serializes distinct opens even inside the same process. The
// nlink check closes the last-close/new-open unlink race without split buses.
class Bus final {
public:
    struct Snapshot { bool valid = false; bool command = false; int mode = proQuality; int instances = 0; std::uint64_t sequence = 0; };
    explicit Bus(bool controller = false) : isController(controller) { open(); }
    ~Bus() { close(); }
    Bus(const Bus&) = delete; Bus& operator=(const Bus&) = delete;
    bool connected() const noexcept { return shared != nullptr && token != 0; }
    Snapshot poll() {
        Snapshot result;
        Lock lock(*this);
        if (!lock.ok || !shared) return result;
        if (shared->magic == 0) initialize();
        if (!valid()) return result;
        const auto now = milliseconds();
        auto* own = findOwn();
        if (!own) { registerSlot(now); own = findOwn(); }
        if (!own) return result;
        own->heartbeat = now;
        result.valid = true; result.mode = static_cast<int>(shared->mode);
        result.sequence = shared->sequence;
        result.command = shared->sequence != seenSequence;
        seenSequence = shared->sequence;
        for (const auto& slot : shared->slots)
            if (slot.token != 0 && now - slot.heartbeat <= staleAfterMs && slot.controller == 0) ++result.instances;
        return result;
    }
    bool broadcast(int mode) {
        Lock lock(*this);
        if (!lock.ok || !valid()) return false;
        shared->mode = mode == liveQuality ? liveQuality : proQuality;
        // Always increment, including repeated clicks on the already-selected mode.
        ++shared->sequence;
        if (shared->sequence == 0) ++shared->sequence;
        return true;
    }
#if defined(GILL_QUALITY_TESTING)
    void expireForTest() { Lock lock(*this); if (lock.ok) if (auto* own = findOwn()) own->heartbeat = milliseconds() - staleAfterMs - 1; }
    std::string mappingNameForTest() const { return name; }
#endif
private:
    struct Lock {
        explicit Lock(Bus& b) : bus(b), ok(bus.lock()) {}
        ~Lock() { if (ok) bus.unlock(); }
        Bus& bus; bool ok;
    };
    bool valid() const { return shared && shared->magic == magic && shared->version == 6 && shared->bytes == sizeof(Shared); }
    Slot* findOwn() { if (!shared || !token) return nullptr; for (auto& slot : shared->slots) if (slot.token == token) return &slot; return nullptr; }
    void registerSlot(std::uint64_t now) {
        bool controllerPresent = isController;
        for (const auto& slot : shared->slots) if (slot.token && slot.controller && now - slot.heartbeat <= staleAfterMs) controllerPresent = true;
        for (auto& slot : shared->slots) {
            if (slot.token && now - slot.heartbeat <= staleAfterMs) continue;
            token = ++shared->nextToken; if (token == 0) token = ++shared->nextToken;
            slot = {token, now, isController ? 1u : 0u, 0u};
            // A live master makes newly inserted instances follow its latest
            // command. Subsequent host state recall remains authoritative.
            seenSequence = shared->sequence;
            if (controllerPresent && seenSequence != 0) --seenSequence;
            return;
        }
    }
    void initialize() {
        if (shared->magic == 0) {
            std::memset(shared, 0, sizeof(Shared)); shared->version = 6;
            shared->bytes = sizeof(Shared); shared->mode = proQuality; shared->magic = magic;
        }
        if (valid()) registerSlot(milliseconds());
    }
    bool lock() {
#if defined(_WIN32)
        if (!mutex) return false;
        const auto r = WaitForSingleObject(mutex, 50);
        return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
#else
        return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;
#endif
    }
    void unlock() {
#if defined(_WIN32)
        ReleaseMutex(mutex);
#else
        flock(fd, LOCK_UN);
#endif
    }
    void open() {
#if defined(_WIN32)
        HANDLE processToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken)) return;
        DWORD bytes = 0; GetTokenInformation(processToken, TokenUser, nullptr, 0, &bytes);
        auto* user = static_cast<TOKEN_USER*>(LocalAlloc(LPTR, bytes));
        const bool gotUser = user && GetTokenInformation(processToken, TokenUser, user, bytes, &bytes);
        CloseHandle(processToken);
        if (!gotUser) { if (user) LocalFree(user); return; }
        LPSTR sid = nullptr;
        if (!ConvertSidToStringSidA(user->User.Sid, &sid)) { LocalFree(user); return; }
        std::string userSid(sid); LocalFree(sid); LocalFree(user);
        FILETIME created{}, exited{}, kernel{}, userTime{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &userTime)) return;
        const auto birth = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        name = "Local\\GillQuality06_" + hex(hash(userSid)) + "_" + std::to_string(GetCurrentProcessId()) + "_" + hex(birth);
        const auto sddl = std::string("D:P(A;;GA;;;") + userSid + ")";
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return;
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        mutex = CreateMutexA(&attributes, FALSE, (name + "_lock").c_str());
        mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, sizeof(Shared), name.c_str());
        LocalFree(descriptor);
        if (!mutex || !mapping) return;
        shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!shared) return;
        Lock lock(*this); if (lock.ok) initialize();
#else
        std::uint64_t birth = 0;
 #if defined(__APPLE__)
        proc_bsdinfo info{};
        if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) return;
        birth = static_cast<std::uint64_t>(info.pbi_start_tvsec) * 1000000u + info.pbi_start_tvusec;
 #else
        // Native Linux test support; production targets are Windows and macOS.
        FILE* stream = std::fopen("/proc/self/stat", "r");
        if (!stream) return;
        char line[4096]{}; const auto* read = std::fgets(line, sizeof(line), stream); std::fclose(stream);
        if (!read) return;
        const char* cursor = std::strrchr(line, ')'); if (!cursor) return; cursor += 2;
        for (int field = 3; field < 22; ++field) { cursor = std::strchr(cursor, ' '); if (!cursor) return; ++cursor; }
        birth = std::strtoull(cursor, nullptr, 10);
 #endif
        const auto identity = std::to_string(getuid()) + ":" + std::to_string(getpid()) + ":" + std::to_string(birth);
 #if defined(__APPLE__)
        char tempDirectory[4096]{};
        if (confstr(_CS_DARWIN_USER_TEMP_DIR, tempDirectory, sizeof(tempDirectory)) == 0) return;
        name = std::string(tempDirectory) + ".gillq6_" + hex(hash(identity));
 #else
        name = "/tmp/.gillq6_" + hex(hash(identity));
 #endif
        for (int attempt = 0; attempt < 8; ++attempt) {
            // Darwin POSIX shm descriptors do not offer portable flock/nlink
            // semantics. A private user-temp file gives a real mmap plus a
            // reliable cross-DLL lifetime lock. No file access occurs in DSP.
            fd = ::open(name.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW, S_IRUSR | S_IWUSR);
            if (fd < 0) return;
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            if (flock(fd, LOCK_EX) != 0) { ::close(fd); fd = -1; return; }
            struct stat state{};
            if (fstat(fd, &state) != 0 || state.st_uid != getuid() || !S_ISREG(state.st_mode) || (state.st_mode & 077) != 0) { unlock(); ::close(fd); fd = -1; return; }
            if (state.st_nlink == 0) { unlock(); ::close(fd); fd = -1; continue; }
            if (state.st_size == 0 && ftruncate(fd, sizeof(Shared)) != 0) { unlock(); ::close(fd); fd = -1; return; }
            if (state.st_size != 0 && state.st_size != sizeof(Shared)) { unlock(); ::close(fd); fd = -1; return; }
            void* memory = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (memory == MAP_FAILED) { unlock(); ::close(fd); fd = -1; return; }
            shared = static_cast<Shared*>(memory); initialize(); unlock(); return;
        }
#endif
    }
    void close() {
        if (shared) {
            Lock lock(*this);
            if (lock.ok && valid()) {
                if (auto* own = findOwn()) *own = {};
#if !defined(_WIN32)
                bool any = false;
                for (const auto& slot : shared->slots) if (slot.token) { any = true; break; }
                if (!any) ::unlink(name.c_str());
#endif
            }
        }
#if defined(_WIN32)
        if (shared) UnmapViewOfFile(shared);
        if (mapping) CloseHandle(mapping);
        if (mutex) CloseHandle(mutex);
#else
        if (shared) munmap(shared, sizeof(Shared));
        if (fd >= 0) ::close(fd);
#endif
        shared = nullptr; token = 0;
    }
    Shared* shared = nullptr;
    std::string name;
    std::uint64_t token = 0, seenSequence = 0;
    bool isController = false;
#if defined(_WIN32)
    HANDLE mapping = nullptr, mutex = nullptr;
#else
    int fd = -1;
#endif
};
} // namespace quality_detail
} // namespace gill

#if !defined(GILL_QUALITY_NO_JUCE)
#include <juce_audio_processors/juce_audio_processors.h>

namespace gill {
inline std::unique_ptr<juce::AudioParameterChoice> qualityParameter(int defaultMode = proQuality) {
    return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{qualityParameterId, 1}, "QUALITY", juce::StringArray{"LIVE", "PRO"}, defaultMode == liveQuality ? liveQuality : proQuality);
}

class QualityClient final : private juce::Timer {
public:
    QualityClient(juce::AudioProcessor& owner, juce::AudioProcessorValueTreeState& state, bool controller = false)
        : processor(owner), apvts(state), raw(state.getRawParameterValue(qualityParameterId)), bus(controller) {
        jassert(raw != nullptr);
        lastMode = mode(); startTimer(50);
    }
    ~QualityClient() override { stopTimer(); }
    // Safe on the audio thread: one relaxed APVTS atomic read; no bus, locks,
    // allocations, OS calls, dispatch or host notifications.
    int mode() const noexcept { return raw && raw->load(std::memory_order_relaxed) < 0.5f ? liveQuality : proQuality; }
    bool isPro() const noexcept { return mode() == proQuality; }
    void requestLatencySamples(int samples) noexcept { pendingLatency.store(samples < 0 ? 0 : samples, std::memory_order_relaxed); }
    bool connected() const noexcept { return bus.connected(); }
    bool isApplyingGlobalChange() const noexcept { return applyingGlobal.load(std::memory_order_relaxed); }
    int registeredInstances() const noexcept { return instances.load(std::memory_order_relaxed); }
    std::function<void(int)> onModeChanged;
    bool broadcast(int requestedMode) {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) return false;
        const bool sent = bus.broadcast(requestedMode);
        if (sent) pollOnMessageThread();
        return sent;
    }
    void pollOnMessageThread() {
        jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) return;
        const auto latency = pendingLatency.exchange(-1, std::memory_order_relaxed);
        if (latency >= 0 && processor.getLatencySamples() != latency) processor.setLatencySamples(latency);
        const auto snapshot = bus.poll();
        if (snapshot.valid) {
            instances.store(snapshot.instances, std::memory_order_relaxed);
            if (snapshot.command) {
                if (auto* parameter = apvts.getParameter(qualityParameterId)) {
                    const auto normalised = parameter->convertTo0to1(static_cast<float>(snapshot.mode));
                    applyingGlobal.store(true, std::memory_order_relaxed);
                    parameter->beginChangeGesture(); parameter->setValueNotifyingHost(normalised); parameter->endChangeGesture();
                    applyingGlobal.store(false, std::memory_order_relaxed);
                }
            }
        }
        const auto current = mode();
        if (current != lastMode) { lastMode = current; if (onModeChanged) onModeChanged(current); }
    }
private:
    void timerCallback() override { pollOnMessageThread(); }
    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    std::atomic<float>* raw = nullptr;
    quality_detail::Bus bus;
    std::atomic<int> instances{0};
    std::atomic<int> pendingLatency{-1};
    std::atomic<bool> applyingGlobal{false};
    int lastMode = proQuality;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QualityClient)
};
} // namespace gill
#endif
