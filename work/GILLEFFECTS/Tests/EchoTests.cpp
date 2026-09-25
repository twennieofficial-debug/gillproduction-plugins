#include "../Source/EchoDSP.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t size) { ++allocations; if (void* p = std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
namespace {
constexpr double pi = 3.14159265358979323846;
int failures = 0, checks = 0;
void check(bool good, const char* label, double metric = 0.0) { ++checks; failures += !good; std::printf("%s %-65s %.9g\n", good ? "PASS" : "FAIL", label, metric); }
void run(gill::EchoDSP& dsp, std::vector<float>& left, std::vector<float>& right, int block = 127, int channels = 2) {
    for (std::size_t at = 0; at < left.size(); at += static_cast<std::size_t>(block)) {
        float* p[]{left.data() + at, right.data() + at};
        dsp.process(p, channels, static_cast<int>(std::min(static_cast<std::size_t>(block), left.size() - at)));
    }
}
double tone(const std::vector<float>& data, int start, int count, double hz) {
    double re = 0.0, im = 0.0;
    for (int n = 0; n < count; ++n) { const double a = 2.0 * pi * hz * n / 48000.0; re += data[static_cast<std::size_t>(start + n)] * std::cos(a); im += data[static_cast<std::size_t>(start + n)] * std::sin(a); }
    return 2.0 * std::hypot(re, im) / count;
}
void identity() {
    bool exact = true;
    for (double rate : {8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) for (int channels : {1, 2}) {
        gill::EchoDSP dsp; dsp.setParameters(147, 90, 0, 83, 94, 2); dsp.prepare(rate, 511, channels);
        std::vector<float> left(2048), right(2048);
        for (std::size_t n = 0; n < left.size(); ++n) left[n] = right[n] = static_cast<float>(0.7 * std::sin(n * 0.049));
        const auto original = left; run(dsp, left, right, 17, channels);
        exact = exact && left == original && (channels == 1 || right == original) && dsp.latencySamples() == 0;
    }
    check(exact, "mix=0 bit-exact dry, zero latency, 7 rates and mono/stereo");
}
void impulses() {
    bool correct = true;
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0}) {
        gill::EchoDSP dsp; dsp.setParameters(200, 50, 100, 0, 100, 0); dsp.prepare(rate, 128, 2);
        const int delay = static_cast<int>(rate * 0.2), length = delay * 6 + 10;
        std::vector<float> left(static_cast<std::size_t>(length)), right(left.size()); left[0] = 1.0f;
        run(dsp, left, right, 257);
        for (int n = 0; n < length; ++n) {
            const float expected = n >= delay && n % delay == 0 ? static_cast<float>(std::pow(0.5, n / delay - 1)) : 0.0f;
            correct = correct && left[static_cast<std::size_t>(n)] == expected && right[static_cast<std::size_t>(n)] == 0.0f;
        }
    }
    check(correct, "clean impulse timing and 50% feedback decay are sample-exact");
    gill::EchoDSP dsp; constexpr float ms = 125.01041667f;
    dsp.setParameters(ms, 0, 100, 0, 100, 0); dsp.prepare(48000, 128, 1);
    std::vector<float> left(7000), right(7000); left[0] = 1; run(dsp, left, right, 1, 1);
    const double delay = ms * 48.0, fraction = delay - std::floor(delay); const auto first = static_cast<std::size_t>(delay);
    check(std::abs(left[first] - (1.0 - fraction)) < 1.0e-6 && std::abs(left[first + 1] - fraction) < 1.0e-6,
          "fractional delay uses correct two-sample interpolation", fraction);
}
void stereo() {
    bool alternating = true, centred = true, mono = true;
    for (int layout : {0, 1, 2}) {
        gill::EchoDSP dsp; dsp.setParameters(50, 50, 100, 0, layout == 1 ? 0.0f : 100.0f, 2); dsp.prepare(48000, 128, layout == 2 ? 1 : 2);
        std::vector<float> left(15000), right(left.size()); left[0] = right[0] = 1; run(dsp, left, right, 127, layout == 2 ? 1 : 2);
        for (int repeat = 1; repeat <= 6; ++repeat) {
            const auto n = static_cast<std::size_t>(repeat * 2400); const float level = static_cast<float>(std::pow(0.5, repeat - 1));
            if (layout == 0) alternating = alternating && left[n] == (repeat % 2 ? level : 0) && right[n] == (repeat % 2 ? 0 : level);
            if (layout == 1) centred = centred && left[n] == level && right[n] == level;
            if (layout == 2) mono = mono && left[n] == level;
        }
    }
    check(alternating, "full-width ping-pong alternates L/R with correct repeat gain");
    check(centred, "zero-width ping-pong centres every repeat");
    check(mono, "ping-pong in mono uses sane single-channel feedback");
}
void colourAndTape() {
    std::array<double, 2> high{}, third{};
    for (int color = 0; color < 2; ++color) {
        gill::EchoDSP dsp; dsp.setParameters(20, 0, 100, color ? 100.0f : 0.0f, 100, 0); dsp.prepare(48000, 128, 2);
        std::vector<float> left(48000), right(left.size());
        for (std::size_t n = 0; n < left.size(); ++n) left[n] = right[n] = static_cast<float>(0.8 * std::sin(2.0 * pi * 10000.0 * n / 48000));
        run(dsp, left, right); high[static_cast<std::size_t>(color)] = tone(left, 24000, 24000, 10000);
    }
    check(20.0 * std::log10(high[0] / high[1]) > 15.0, "COLOR audibly damps high frequencies, measured dB", 20.0 * std::log10(high[0] / high[1]));
    for (int style = 0; style < 2; ++style) {
        gill::EchoDSP dsp; dsp.setParameters(20, 0, 100, 0, 100, style); dsp.prepare(48000, 128, 2);
        std::vector<float> left(48000), right(left.size());
        for (std::size_t n = 0; n < left.size(); ++n) left[n] = right[n] = static_cast<float>(0.95 * std::sin(2.0 * pi * 750.0 * n / 48000));
        run(dsp, left, right); third[static_cast<std::size_t>(style)] = tone(left, 24000, 24000, 2250);
    }
    check(third[0] < 1.0e-6 && third[1] > 0.005, "TAPE generates harmonics; CLEAN remains linear", third[1]);
}
void tailsAndAutomation() {
    bool safe = true; double lastPeak = 0.0;
    for (int style = 0; style < 3; ++style) {
        gill::EchoDSP dsp; dsp.setParameters(20, 90, 100, 50, 100, style); dsp.prepare(48000, 128, 2);
        const int length = static_cast<int>((dsp.tailSeconds() + 0.2) * 48000);
        std::vector<float> left(static_cast<std::size_t>(length)), right(left.size()); left[0] = right[0] = 1;
        run(dsp, left, right);
        for (std::size_t n = 0; n < left.size(); ++n) safe = safe && std::isfinite(left[n]) && std::abs(left[n]) <= 1.01f && std::isfinite(right[n]) && std::abs(right[n]) <= 1.01f;
        for (std::size_t n = left.size() - 4800; n < left.size(); ++n) lastPeak = std::max({lastPeak, std::abs(static_cast<double>(left[n])), std::abs(static_cast<double>(right[n]))});
    }
    check(safe && lastPeak < 0.0001, "90% feedback stable and below -80 dB after reported tail", lastPeak);
    gill::EchoDSP dsp; dsp.setParameters(100, 50, 65, 30, 100, 0); dsp.prepare(48000, 128, 2);
    std::array<float, 128> left{}, right{}; float* buffers[]{left.data(), right.data()};
    double jump = 0.0, previous = 0.0; bool finite = true;
    const auto before = allocations.load();
    for (int block = 0; block < 1600; ++block) {
        dsp.setParameters(block % 2 ? 500.0f : 17.0f, 70, 65, static_cast<float>(block % 101), static_cast<float>(block % 101), block % 3);
        for (int n = 0; n < 128; ++n) left[static_cast<std::size_t>(n)] = right[static_cast<std::size_t>(n)] = static_cast<float>(0.3 * std::sin(2.0 * pi * 220.0 * (block * 128 + n) / 48000));
        dsp.process(buffers, 2, 128);
        for (float x : left) { jump = std::max(jump, std::abs(x - previous)); previous = x; finite = finite && std::isfinite(x) && std::abs(x) < 4.0f; }
    }
    check(before == allocations.load(), "process and time/feedback/style automation allocate nothing");
    check(finite && jump < 0.1, "rapid large delay-time changes avoid discontinuous read-head jumps", jump);
    dsp.setParameters(8000, 90, 100, 100, 100, 2);
    check(dsp.tailSeconds() > 700 && dsp.tailSeconds() < 720, "maximum delay/feedback reports an honest finite long tail", dsp.tailSeconds());
}
void blockAndFaults() {
    gill::EchoDSP a, b; a.setParameters(37, 47, 55, 67, 88, 1); b.setParameters(37, 47, 55, 67, 88, 1); a.prepare(48000, 1, 2); b.prepare(48000, 1024, 2);
    std::vector<float> left(18000), right(left.size());
    for (std::size_t n = 0; n < left.size(); ++n) left[n] = right[n] = static_cast<float>(0.8 * std::sin(n * 0.028));
    auto other = left, otherRight = right; run(a, left, right, 1); run(b, other, otherRight, 1024);
    check(left == other && right == otherRight, "block-size independence is sample-exact");
    bool safe = true;
    for (double rate : {8000.0, 44100.0, 192000.0, 384000.0}) {
        gill::EchoDSP dsp; dsp.setParameters(1, 90, 100, 50, 100, 1); dsp.prepare(rate, 256, 2);
        std::array<float, 512> l{}, r{}; float* p[]{l.data(), r.data()}; dsp.process(p, 2, 512);
        for (float x : l) safe = safe && x == 0.0f;
        for (std::size_t n = 0; n < l.size(); ++n) l[n] = n % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : n % 3 == 1 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::max();
        dsp.process(p, 2, 512); for (float x : l) safe = safe && std::isfinite(x) && std::abs(x) <= 64;
        dsp.setParameters(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -500, 900, -500, 100);
        dsp.process(p, 2, 512); for (float x : l) safe = safe && std::isfinite(x) && std::abs(x) <= 64;
        dsp.process(nullptr, 2, 8); dsp.process(p, 0, 8); dsp.process(p, 2, 0); dsp.reset(); l.fill(0); r.fill(0); dsp.process(p, 2, 512);
        for (float x : l) safe = safe && x == 0;
    }
    check(safe, "silence/reset, low/high rates and pathological inputs remain safe");
}
}
int main() {
    const auto start = std::chrono::steady_clock::now();
    std::puts("GILLECHO original DSP validation"); identity(); impulses(); stereo(); colourAndTape(); tailsAndAutomation(); blockAndFaults();
    std::printf("RESULT %d behavior checks, %d failures, %.3f seconds\n", checks, failures, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return failures ? 1 : 0;
}
