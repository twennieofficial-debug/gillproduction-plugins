#include "../Source/AirDSP.h"
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
constexpr double pi = gill::air_detail::pi;
int failures = 0, checks = 0;
void check(bool good, const char* label, double metric = 0.0) { ++checks; failures += !good; std::printf("%s %-65s %.9g\n", good ? "PASS" : "FAIL", label, metric); }
void run(gill::AirDSP& dsp, std::vector<float>& left, std::vector<float>& right, int block = 127, int channels = 2) {
    for (std::size_t at = 0; at < left.size(); at += static_cast<std::size_t>(block)) { float* p[]{left.data() + at, right.data() + at}; dsp.process(p, channels, static_cast<int>(std::min(static_cast<std::size_t>(block), left.size() - at))); }
}
double tone(const std::vector<float>& audio, int start, int count, double frequency) {
    double re = 0.0, im = 0.0;
    for (int n = 0; n < count; ++n) { const double angle = 2.0 * pi * frequency * n / 48000; re += audio[static_cast<std::size_t>(start + n)] * std::cos(angle); im += audio[static_cast<std::size_t>(start + n)] * std::sin(angle); }
    return 2.0 * std::hypot(re, im) / count;
}
std::vector<float> signal(int count, double frequency, double amplitude = 0.6) {
    std::vector<float> result(static_cast<std::size_t>(count)); for (int n = 0; n < count; ++n) result[static_cast<std::size_t>(n)] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * n / 48000)); return result;
}
void neutral() {
    bool exact = true;
    for (double rate : {8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) for (int channels : {1, 2}) for (int mode : {0, 1}) {
        gill::AirDSP dsp; dsp.setParameters(mode ? 100.0f : 0.0f, mode ? 100.0f : 0.0f, mode ? 0.0f : 100.0f, 0); dsp.prepare(rate, 511, channels);
        auto left = signal(3000, 712); auto right = left; const auto original = left; run(dsp, left, right, channels == 1 ? 1 : 511, channels);
        for (std::size_t n = 0; n < left.size(); ++n) { const float expected = n < 24 ? 0.0f : original[n - 24]; exact = exact && left[n] == expected && (channels == 1 || right[n] == expected); }
        exact = exact && dsp.latencySamples() == 24;
    }
    check(exact, "0 AIR / 0 MIX exact delayed dry across 7 rates, mono/stereo");
}
void harmonics() {
    double largestDc = 0.0;
    for (int band = 0; band < 2; ++band) {
        const double frequency = band == 0 ? 2250.0 : 7000.0;
        gill::AirDSP dsp; dsp.setParameters(band == 0 ? 100.0f : 0.0f, band == 1 ? 100.0f : 0.0f, 100, 0); dsp.prepare(48000, 128, 2);
        auto left = signal(48000, frequency); auto right = left; run(dsp, left, right);
        const double second = tone(left, 24000, 24000, 2.0 * frequency), third = tone(left, 24000, 24000, 3.0 * frequency);
        check(second > 0.003, band == 0 ? "MID AIR generates new even harmonics" : "HIGH AIR generates new even harmonics", second);
        check(third > 0.0003, band == 0 ? "MID AIR generates new odd harmonics" : "HIGH AIR generates new odd harmonics", third);
        double dc = 0.0; for (std::size_t n = 24000; n < left.size(); ++n) dc += left[n]; largestDc = std::max(largestDc, std::abs(dc / 24000));
    }
    check(largestDc < 1.0e-6, "excitation contains no persistent generated DC", largestDc);
    // Nonlinearity proof: halving the input does not simply halve the new harmonic.
    std::array<double, 2> third{};
    for (int i = 0; i < 2; ++i) { gill::AirDSP dsp; dsp.setParameters(80, 0, 100, 0); dsp.prepare(48000, 128, 2); auto left = signal(48000, 2250, i == 0 ? 0.2 : 0.1); auto right = left; run(dsp, left, right); third[static_cast<std::size_t>(i)] = tone(left, 24000, 24000, 6750); }
    check(third[0] / third[1] > 3.0, "low-level odd harmonic grows nonlinearly when input doubles", third[0] / third[1]);
}
void aliases() {
    gill::AirDSP dsp; dsp.setParameters(0, 100, 100, 0); dsp.prepare(48000, 128, 2);
    auto left = signal(96000, 14000); auto right = left; auto naive = left; run(dsp, left, right);
    gill::air_detail::Core core; core.prepare(48000, 48000);
    for (auto& value : naive) value = static_cast<float>(value + core.process(value, 0, 1));
    const double reference = tone(naive, 48000, 48000, 20000), filtered = tone(left, 48000, 48000, 20000);
    const double reduction = 20.0 * std::log10(reference / std::max(1.0e-15, filtered));
    check(reference > 0.003, "unfiltered reference has folded 28-kHz second harmonic", reference);
    check(reduction > 30.0, "8x anti-alias filters suppress folded excitation versus naive, dB", reduction);
    check(filtered < 0.0003, "absolute residual folded-harmonic amplitude", filtered);
}
void automationAndStereo() {
    gill::AirDSP a, b; a.setParameters(37, 67, 83, -1.7f); b.setParameters(37, 67, 83, -1.7f); a.prepare(48000, 1, 2); b.prepare(48000, 1024, 2);
    auto left = signal(12000, 1562.5); auto right = left; auto other = left; auto otherRight = left; run(a, left, right, 1); run(b, other, otherRight, 1024);
    check(left == other && right == otherRight && left == right, "block independence and stereo matching are sample-exact");
    a.reset(); left = signal(12000, 1562.5); right.assign(left.size(), 0); run(a, left, right);
    bool isolated = true; for (float value : right) isolated = isolated && value == 0.0f; check(isolated, "stereo channel states do not leak");
    gill::AirDSP dsp; dsp.prepare(48000, 128, 2);
    std::array<float, 128> l{}, r{}; float* pointers[]{l.data(), r.data()}; const auto before = allocations.load(); double jump = 0.0, previous = 0.0; bool finite = true;
    for (int block = 0; block < 900; ++block) {
        dsp.setParameters(block % 2 ? 100.0f : 0.0f, block % 3 ? 100.0f : 0.0f, block % 2 ? 100.0f : 0.0f, block % 5 ? 6.0f : -18.0f);
        for (int n = 0; n < 128; ++n) l[static_cast<std::size_t>(n)] = r[static_cast<std::size_t>(n)] = static_cast<float>(0.4 * std::sin(2.0 * pi * 440.0 * (block * 128 + n) / 48000));
        dsp.process(pointers, 2, 128); for (float value : l) { finite = finite && std::isfinite(value) && std::abs(value) < 4.0f; jump = std::max(jump, std::abs(value - previous)); previous = value; }
    }
    check(before == allocations.load(), "parameter automation and process allocate nothing");
    check(finite && jump < 0.1, "rapid knob automation is finite and smoothed", jump);
}
void faults() {
    bool safe = true;
    for (double rate : {8000.0, 11025.0, 44100.0, 96000.0, 192000.0, 384000.0}) {
        gill::AirDSP dsp; dsp.setParameters(100, 100, 100, 6); dsp.prepare(rate, 512, 2); std::array<float, 512> l{}, r{}; float* p[]{l.data(), r.data()};
        dsp.process(p, 2, 512); for (float value : l) safe = safe && value == 0;
        for (std::size_t n = 0; n < l.size(); ++n) l[n] = n % 4 == 0 ? std::numeric_limits<float>::quiet_NaN() : n % 4 == 1 ? std::numeric_limits<float>::infinity() : n % 4 == 2 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
        dsp.process(p, 2, 512); for (float value : l) safe = safe && std::isfinite(value) && std::abs(value) <= 64;
        dsp.setParameters(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -999, 999); dsp.process(p, 2, 512); for (float value : l) safe = safe && std::isfinite(value) && std::abs(value) <= 64;
        dsp.process(nullptr, 2, 100); dsp.process(p, 0, 100); dsp.process(p, 2, 0); dsp.reset(); l.fill(0); r.fill(0); dsp.process(p, 2, 512); for (float value : l) safe = safe && value == 0;
    }
    check(safe, "silence/reset, low Nyquist/high Fs, NaN/Inf/huge-input containment");
}
}
int main() {
    const auto start = std::chrono::steady_clock::now(); std::puts("GILLAIR original DSP validation"); neutral(); harmonics(); aliases(); automationAndStereo(); faults();
    std::printf("RESULT %d behavior checks, %d failures, %.3f seconds\n", checks, failures, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    return failures ? 1 : 0;
}
