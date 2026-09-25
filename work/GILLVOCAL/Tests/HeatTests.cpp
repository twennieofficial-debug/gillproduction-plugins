#include "../Source/HeatDSP.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t bytes) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
int failures = 0;
constexpr double pi = gill::heat_detail::pi;

void check(bool passed, const char* label, double metric = 0.0) {
    std::printf("%s %-58s %.9g\n", passed ? "PASS" : "FAIL", label, metric);
    if (!passed) ++failures;
}

void run(gill::HeatDSP& dsp, std::vector<float>& left, std::vector<float>& right, int block) {
    for (std::size_t offset = 0; offset < left.size(); offset += static_cast<std::size_t>(block)) {
        float* pointers[]{left.data() + offset, right.data() + offset};
        const auto count = std::min(static_cast<std::size_t>(block), left.size() - offset);
        dsp.process(pointers, 2, static_cast<int>(count));
    }
}

double toneAmplitude(const std::vector<float>& audio, std::size_t start,
                     std::size_t count, double frequency, double rate) {
    double re = 0.0, im = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double angle = 2.0 * pi * frequency * static_cast<double>(n) / rate;
        re += audio[start + n] * std::cos(angle);
        im -= audio[start + n] * std::sin(angle);
    }
    return 2.0 * std::hypot(re, im) / static_cast<double>(count);
}

std::vector<float> sine(std::size_t length, double frequency, double rate, double amplitude) {
    std::vector<float> output(length);
    for (std::size_t i = 0; i < length; ++i)
        output[i] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * i / rate));
    return output;
}

void latencyAndIdentity() {
    for (double rate : {8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        for (int block : {1, 7, 64, 511}) {
            for (int kind = 0; kind < 2; ++kind) {
                gill::HeatDSP dsp;
                dsp.setParameters(kind == 0 ? 24.0f : 0.0f, kind == 0 ? 24.0f : 0.0f,
                                  kind == 0 ? 24.0f : 0.0f, 2, kind == 0 ? 0.0f : 100.0f, 0.0f);
                dsp.prepare(rate, block, 2);
                auto left = sine(1536, 137.0, rate, 0.75);
                left[0] = 1.0f;
                auto right = left;
                const auto original = left;
                run(dsp, left, right, block);
                bool equal = dsp.latencySamples() == 24;
                for (std::size_t n = 0; n < left.size(); ++n) {
                    const float expected = n < 24 ? 0.0f : original[n - 24];
                    equal = equal && left[n] == expected && right[n] == expected;
                }
                if (!equal) {
                    check(false, kind == 0 ? "dry identity, delay 24" : "zero-drive wet identity, delay 24", rate);
                    return;
                }
            }
        }
    }
    check(true, "bit-exact dry/zero-drive identity: 7 rates x 4 blocks");
}

void crossoverNull() {
    double worst = 0.0;
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 384000.0}) {
        gill::heat_detail::Crossover split;
        split.prepare(rate * gill::HeatDSP::oversamplingFactor);
        unsigned state = 12345;
        for (int n = 0; n < 50000; ++n) {
            state = state * 1664525u + 1013904223u;
            const double input = (static_cast<double>(state) / 4294967295.0 - 0.5) * 4.0;
            const auto bands = split.split(input);
            worst = std::max(worst, std::abs(input - (bands[0] + bands[1] + bands[2])));
        }
    }
    check(worst < 2.0e-15, "complementary crossover reconstruction max error", worst);
}

void harmonicsAndStyles() {
    constexpr int count = 48000;
    std::array<std::vector<float>, 3> outputs;
    double warmSecond = 0.0;
    for (int style = 0; style < 3; ++style) {
        gill::HeatDSP dsp;
        dsp.setParameters(12.0f, 12.0f, 12.0f, style, 100.0f, 0.0f);
        dsp.prepare(48000.0, 128, 2);
        auto left = sine(count * 2, 750.0, 48000.0, 0.65);
        auto right = left;
        run(dsp, left, right, 128);
        const double second = toneAmplitude(left, count, count, 1500.0, 48000.0);
        const double third = toneAmplitude(left, count, count, 2250.0, 48000.0);
        check(third > 0.003, style == 0 ? "WARM creates measurable third harmonic" :
                              style == 1 ? "TAPE creates measurable third harmonic" :
                                           "EDGE creates measurable third harmonic", third);
        if (style == 0) warmSecond = second;
        if (style == 1) check(second < 1.0e-6, "TAPE preserves odd-symmetric transfer", second);
        double dc = 0.0;
        for (int n = count; n < count * 2; ++n) dc += left[static_cast<std::size_t>(n)];
        dc /= count;
        check(std::abs(dc) < 2.0e-6, "generated DC removed after settling", dc);
        outputs[static_cast<std::size_t>(style)] = std::move(left);
    }
    check(warmSecond > 0.001, "WARM adds deliberate even harmonics", warmSecond);
    for (int style = 1; style < 3; ++style) {
        double sum = 0.0, reference = 0.0;
        for (int n = count; n < count * 2; ++n) {
            const double diff = outputs[static_cast<std::size_t>(style)][static_cast<std::size_t>(n)] -
                                outputs[static_cast<std::size_t>(style - 1)][static_cast<std::size_t>(n)];
            sum += diff * diff;
            const double level=outputs[static_cast<std::size_t>(style)][static_cast<std::size_t>(n)];
            reference+=level*level;
        }
        // Relative difference remains meaningful after the deliberate reduction
        // in output makeup; an absolute amplitude threshold confounds character
        // with output gain.
        check(std::sqrt(sum/reference) > 0.01,
              "adjacent styles differ by >1% of output RMS",std::sqrt(sum/reference));
    }
}

void aliasSuppression() {
    constexpr double rate = 48000.0;
    constexpr int count = 48000;
    for(int style=0;style<3;++style){
    std::printf("ALIAS max-drive style %d\n",style);
    auto oversampled = sine(count * 2, 10000.0, rate, 0.8);
    auto right = oversampled;
    auto naive = oversampled;
    gill::HeatDSP dsp;
    dsp.setParameters(24.0f, 24.0f, 24.0f, style, 100.0f, 0.0f);
    dsp.prepare(rate, 256, 2);
    run(dsp, oversampled, right, 256);

    // Same band architecture and selected transfer, without oversampling filters.
    gill::heat_detail::Crossover crossover;
    crossover.prepare(rate);
    const double gain = std::pow(10.0, gill::heat_detail::effectiveDriveDb(24.0) / 20.0);
    const double pole = std::exp(-2.0 * pi * 8.0 / rate);
    double previous = 0.0, dc = 0.0;
    for (auto& sample : naive) {
        const auto bands = crossover.split(sample);
        double residual = 0.0;
        for (double band : bands)
            residual += gill::heat_detail::shape(band * gain, style) / gill::heat_detail::driveNormalisation(gain) - band;
        dc = residual - previous + pole * dc;
        previous = residual;
        sample = static_cast<float>(sample + dc);
    }
    // 3*10 kHz folds to 18 kHz at the base sample rate.
    const double naiveAlias = toneAmplitude(naive, count, count, 18000.0, rate);
    const double filteredAlias = toneAmplitude(oversampled, count, count, 18000.0, rate);
    const double improvement = 20.0 * std::log10(naiveAlias / std::max(filteredAlias, 1.0e-15));
    check(naiveAlias > 0.001, "naive reference has measurable 18-kHz alias", naiveAlias);
    check(improvement > 30.0, "32x oversampling alias suppression vs naive, dB", improvement);
    check(filteredAlias < 0.001, "oversampled 18-kHz alias absolute amplitude", filteredAlias);
    }
}

void calibratedWarmTransfer(){
    bool bounded=true,monotonic=true;double prior=-2;
    for(int i=0;i<=20000;++i){const double x=-10+i*.001;const auto value=gill::heat_detail::shape(x,0);bounded=bounded&&std::isfinite(value)&&std::abs(value)<1.2;monotonic=monotonic&&value>=prior-1e-12;prior=value;}
    check(bounded&&monotonic,"fast WARM curve is finite bounded and monotonic over full knee");
    check(gill::heat_detail::shape(0,0)==0,"fast WARM transfer preserves exact silence");
    const double slope=(gill::heat_detail::shape(1e-5,0)-gill::heat_detail::shape(-1e-5,0))/2e-5;
    check(std::abs(slope-1)<1e-8,"fast WARM small-signal derivative remains calibrated to unity",slope);
}

void stereoBlocksAndAutomation() {
    auto original = sine(20000, 233.0, 48000.0, 0.8);
    auto a = original, b = original, ar = original, br = original;
    gill::HeatDSP first, second;
    first.setParameters(4.0f, 9.0f, 16.0f, 0, 73.0f, -2.0f);
    second.setParameters(4.0f, 9.0f, 16.0f, 0, 73.0f, -2.0f);
    first.prepare(48000.0, 17, 2);
    second.prepare(48000.0, 1024, 2);
    run(first, a, ar, 17);
    run(second, b, br, 1024);
    check(a == b && ar == br && a == ar, "sample-exact block invariance and stereo matching");
    first.reset();
    a = original;
    ar.assign(a.size(), 0.0f);
    run(first, a, ar, 127);
    double leakage = 0.0;
    for (float value : ar) leakage = std::max(leakage, std::abs(static_cast<double>(value)));
    check(leakage < 1.0e-12, "stereo channels have independent state", leakage);

    std::array<float, 64> left{}, right{};
    float* buffers[]{left.data(), right.data()};
    gill::HeatDSP automate;
    automate.prepare(48000.0, 64, 2);
    bool finite = true;
    double maximumJump = 0.0, previous = 0.0;
    const auto before = allocations.load();
    for (int block = 0; block < 1200; ++block) {
        automate.setParameters(block % 2 ? 24.0f : 0.0f, block % 3 ? 18.0f : 1.0f,
                               block % 4 ? 24.0f : 0.0f, block % 3,
                               block % 2 ? 100.0f : 0.0f, block % 2 ? 0.0f : -12.0f);
        for (int n = 0; n < 64; ++n)
            left[static_cast<std::size_t>(n)] = right[static_cast<std::size_t>(n)] =
                static_cast<float>(0.4 * std::sin(2.0 * pi * 220.0 * (block * 64 + n) / 48000.0));
        automate.process(buffers, 2, 64);
        for (float value : left) {
            finite = finite && std::isfinite(value) && std::abs(value) <= 32.0f;
            maximumJump = std::max(maximumJump, std::abs(value - previous));
            previous = value;
        }
    }
    const auto after = allocations.load();
    check(before == after, "no allocation in parameter changes or process", static_cast<double>(after - before));
    check(finite && maximumJump < 0.05, "rapid automation stays finite without abrupt jumps", maximumJump);
}

void extendedSaturationRange() {
    constexpr int count=24000; // Whole 750-Hz periods at 48 kHz, excluding startup.
    constexpr std::array<double,3> referenceMaximumThd{{.682128351,.637362939,1.01085929}};
    for(int style=0;style<3;++style){double middleThd=0,extremeThd=0,previousThd=0;
        bool monotonic=true;
        for(int percent:{0,1,5,10,20,25,50,75,100}){
            const float drive=percent*.24f;
            gill::HeatDSP dsp;dsp.setParameters(drive,drive,drive,style,100,0);dsp.prepare(48000,127,2);
            auto left=sine(count*2,750,48000,.05),right=left;run(dsp,left,right,127);
            const double fundamental=toneAmplitude(left,count,count,750,48000);double harmonicPower=0,energy=0,peak=0;
            for(int h=2;h<=20;++h){const auto a=toneAmplitude(left,count,count,750.*h,48000);harmonicPower+=a*a;}
            for(int i=count;i<count*2;++i){energy+=left[i]*left[i];peak=std::max(peak,std::abs(static_cast<double>(left[i])));}
            const double thd=std::sqrt(harmonicPower)/std::max(1e-12,fundamental),rms=std::sqrt(energy/count);
            std::printf("MEASURE style %d %d percent branch %.1f dB: THD %.5f RMS %.5f peak %.5f\n",style,percent,gill::heat_detail::effectiveDriveDb(drive),thd,rms,peak);
            monotonic=monotonic&&thd>=previousThd-1e-6;previousThd=thd;
            if(percent==10)check(thd<.03,"low range retains subtle option on -26 dBFS tone",thd);
            if(percent==50){middleThd=thd;check(thd>.1&&thd<.35,"midpoint now supplies deliberate audible saturation",thd);}
            if(percent==100){extremeThd=thd;check(rms>.035&&rms<.25&&peak<.4,"maximum drive remains strong with reduced makeup",rms);}
        }
        check(monotonic,"quiet-tone THD rises through low/mid/extreme settings");
        check(extremeThd>.2&&extremeThd>middleThd*2,"maximum drive strongly distorts quiet recorded tone",extremeThd);
        const double ratio=extremeThd/referenceMaximumThd[style];
        check(ratio>.98&&ratio<1.02,"maximum THD retains v0.2 endpoint within 2 percent",ratio);
    }
}

void faultAndSilence() {
    bool valid = true;
    for (double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0}) {
        gill::HeatDSP dsp;
        dsp.setParameters(24.0f, 24.0f, 24.0f, 0, 100.0f, 12.0f);
        dsp.prepare(rate, 64, 2);
        std::array<float, 257> left{}, right{};
        float* buffers[]{left.data(), right.data()};
        dsp.process(buffers, 2, static_cast<int>(left.size()));
        for (float value : left) valid = valid && std::abs(value) < 1.0e-12f;
        for (std::size_t n = 0; n < left.size(); ++n) {
            left[n] = n % 5 == 0 ? std::numeric_limits<float>::quiet_NaN() :
                      n % 5 == 1 ? std::numeric_limits<float>::infinity() :
                      n % 5 == 2 ? std::numeric_limits<float>::max() :
                      n % 5 == 3 ? -std::numeric_limits<float>::max() : 1.0e-38f;
        }
        dsp.process(buffers, 2, static_cast<int>(left.size()));
        for (float value : left) valid = valid && std::isfinite(value) && std::abs(value) <= 32.0f;
        dsp.setParameters(std::numeric_limits<float>::infinity(), -500.0f,
                          std::numeric_limits<float>::quiet_NaN(), 900, -20.0f,
                          std::numeric_limits<float>::quiet_NaN());
        dsp.process(buffers, 2, static_cast<int>(left.size()));
        for (float value : left) valid = valid && std::isfinite(value) && std::abs(value) <= 32.0f;
        dsp.process(nullptr, 2, 100);
        dsp.process(buffers, 0, 100);
        dsp.process(buffers, 2, 0);
        float* nullBuffers[]{nullptr, right.data()};
        dsp.process(nullBuffers, 2, 3);
        dsp.reset();
        left.fill(0.0f);
        right.fill(0.0f);
        dsp.process(buffers, 2, static_cast<int>(left.size()));
        for (float value : left) valid = valid && value == 0.0f;
    }
    check(valid, "silence, reset, NaN/Inf/huge input and invalid parameters");
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    std::puts("GILLHEAT original DSP validation, C++17");
    latencyAndIdentity();
    crossoverNull();
    harmonicsAndStyles();
    calibratedWarmTransfer();
    extendedSaturationRange();
    aliasSuppression();
    stereoBlocksAndAutomation();
    faultAndSilence();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("RESULT: %d failure(s), %.3f seconds\n", failures, elapsed);
    return failures ? 1 : 0;
}
