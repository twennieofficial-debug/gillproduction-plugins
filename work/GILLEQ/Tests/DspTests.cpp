#include "../Source/EqDSP.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {
using Complex = std::complex<double>;
constexpr std::array<double, 5> rates{44100, 48000, 88200, 96000, 192000};
std::mt19937_64 rng(0x47494c4c4551ULL);
std::size_t checks = 0, failures = 0, configurations = 0, fftConfigurations = 0, sineConfigurations = 0, audioSamples = 0;
double maxReferenceDbError = 0, maxFftDbError = 0, maxFftComplexError = 0, maxSineDbError = 0, maxPoleRadius = 0;
double maxAutomationAmplitude = 0;
double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }
void require(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { if (failures < 20) std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
double db(double x) { return 20.0 * std::log10(std::max(x, 1e-300)); }
gill::Bands emptyBands() { gill::Bands b{}; for (auto& p : b) p.enabled = false; return b; }
double poleRadius(const gill::Coefficients& c) {
    const auto d = std::sqrt(Complex(c.a1 * c.a1 - 4.0 * c.a2, 0.0));
    return std::max(std::abs((-c.a1 + d) * 0.5), std::abs((-c.a1 - d) * 0.5));
}
// Independent analog-prototype evaluation, bilinear-mapped to digital frequency.
// This does not call the production coefficient designer or response evaluator.
Complex referenceSection(int type, double f0, double gain, double q, double fs, double frequency) {
    const double A = std::pow(10.0, gain / 40.0);
    const Complex s(0.0, std::tan(gill::pi * frequency / fs) / std::tan(gill::pi * f0 / fs));
    const Complex s2 = s * s;
    switch (type) {
        case gill::LowShelf: return A * (s2 + (std::sqrt(A) / q) * s + A) / (A * s2 + (std::sqrt(A) / q) * s + 1.0);
        case gill::HighShelf: return A * (A * s2 + (std::sqrt(A) / q) * s + 1.0) / (s2 + (std::sqrt(A) / q) * s + A);
        case gill::LowCut: return s2 / (s2 + s / q + 1.0);
        case gill::HighCut: return 1.0 / (s2 + s / q + 1.0);
        case gill::Notch: return (s2 + 1.0) / (s2 + s / q + 1.0);
        default: return (s2 + (A / q) * s + 1.0) / (s2 + s / (A * q) + 1.0);
    }
}
Complex reference(gill::BandParams p, double fs, double frequency) {
    p = gill::sanitize(p, fs);
    if (!p.enabled) return {1, 0};
    const bool cut = p.type == gill::LowCut || p.type == gill::HighCut;
    const int n = cut ? (1 << p.slope) : 1;
    Complex answer(1.0, 0.0);
    for (int i = 0; i < n; ++i) {
        const double q = cut && n > 1 ? (i == n - 1 ? p.q / std::sqrt(0.5) : 1.0)
            / (2.0 * std::cos(gill::pi * (2.0 * i + 1.0) / (4.0 * n))) : p.q;
        answer *= referenceSection(p.type, p.frequency, p.gainDb, q, fs, frequency);
    }
    return answer;
}
void fft(std::vector<Complex>& x) {
    const auto n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const auto step = std::polar(1.0, -2.0 * gill::pi / static_cast<double>(len));
        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto a = x[i + j], b = x[i + j + len / 2] * w;
                x[i + j] = a + b; x[i + j + len / 2] = a - b; w *= step;
            }
        }
    }
}
void randomizedParameters() {
    for (int test = 0; test < 12000; ++test) {
        const double fs = rates[static_cast<std::size_t>(test) % rates.size()];
        gill::BandParams p;
        p.type = test % 6; p.channel = (test / 6) % 5; p.slope = (test / 30) % 3;
        p.enabled = test % 31 != 0;
        p.frequency = std::exp(uniform(std::log(20.0), std::log(20000.0)));
        p.gainDb = uniform(-24.0, 24.0); p.q = std::exp(uniform(std::log(0.1), std::log(18.0)));
        if (test % 11 == 0) p.frequency = 20.0;
        if (test % 13 == 0) p.frequency = 20000.0;
        if (test % 17 == 0) p.q = 18.0;
        if (test % 19 == 0) p.q = 0.1;
        if (test % 23 == 0) p.gainDb = 24.0;
        if (test % 29 == 0) p.gainDb = -24.0;
        const auto design = gill::designFilter(p, fs);
        for (int section = 0; section < design.count; ++section) {
            const auto& c = design.sections[static_cast<std::size_t>(section)];
            const double radius = poleRadius(c); maxPoleRadius = std::max(maxPoleRadius, radius);
            require(std::isfinite(radius) && radius < 1.0, "strict pole stability config " + std::to_string(test));
        }
        for (int point = 0; point < 9; ++point) {
            const double frequency = std::exp(uniform(std::log(20.0), std::log(std::min(20000.0, fs * 0.49))));
            const auto actual = gill::coefficientResponse(p, fs, frequency), expected = reference(p, fs, frequency);
            require(std::abs(actual - expected) <= 2e-6 * std::max(1.0, std::abs(expected)), "independent complex reference " + std::to_string(test));
            if (std::abs(expected) > 1e-6 && std::abs(actual) > 1e-6) {
                const double error = std::abs(db(std::abs(actual)) - db(std::abs(expected)));
                maxReferenceDbError = std::max(maxReferenceDbError, error);
                require(error < 0.00005, "independent dB reference " + std::to_string(test));
            }
        }
        auto bands = emptyBands(); bands[0] = p;
        gill::EqEngine engine; engine.prepare(fs); engine.setBands(bands);
        std::array<double, 256> left{}, right{};
        for (std::size_t i = 0; i < left.size(); ++i) { left[i] = uniform(-0.25, 0.25); right[i] = uniform(-0.25, 0.25); }
        double* channels[]{left.data(), right.data()}; engine.process(channels, 2, 256); audioSamples += 512;
        bool finite = true; for (std::size_t i = 0; i < left.size(); ++i) finite = finite && std::isfinite(left[i]) && std::isfinite(right[i]);
        require(finite, "actual audio finite config " + std::to_string(test));
        ++configurations;
    }
}
void knownResponses() {
    for (double fs : rates) for (double f0 : {20.0, 100.0, 1000.0, 10000.0, 20000.0}) {
        f0 = std::min(f0, fs * 0.49);
        for (double gain : {-24.0, -12.0, -1.0, 0.0, 1.0, 12.0, 24.0}) for (double q : {0.1, 0.7071067811865476, 3.0, 18.0}) {
            gill::BandParams p; p.frequency = f0; p.gainDb = gain; p.q = q;
            require(std::abs(db(std::abs(gill::coefficientResponse(p, fs, f0))) - gain) < 0.00001, "bell center exact gain");
            const auto boost = gill::coefficientResponse(p, fs, f0 * 0.8); p.gainDb = -gain;
            require(std::abs(boost * gill::coefficientResponse(p, fs, f0 * 0.8) - Complex(1, 0)) < 1e-7, "bell boost/cut reciprocal");
            p.gainDb = gain;
            for (int type : {gill::LowShelf, gill::HighShelf}) {
                p.type = type;
                const double low = db(std::abs(gill::coefficientResponse(p, fs, 0))), high = db(std::abs(gill::coefficientResponse(p, fs, fs * 0.5)));
                require(std::abs(low - (type == gill::LowShelf ? gain : 0)) < 0.00001, "shelf DC gain");
                require(std::abs(high - (type == gill::HighShelf ? gain : 0)) < 0.00001, "shelf Nyquist gain");
                require(std::abs(db(std::abs(gill::coefficientResponse(p, fs, f0))) - gain * 0.5) < 0.00001, "shelf midpoint gain");
            }
        }
        for (int type : {gill::LowCut, gill::HighCut}) for (int slope = 0; slope < 3; ++slope) {
            gill::BandParams p; p.type = type; p.frequency = f0; p.slope = slope;
            require(std::abs(db(std::abs(gill::coefficientResponse(p, fs, f0))) + 3.010299956639812) < 0.00001, "Butterworth cutoff all slopes");
        }
    }
}
template<class Sample> void flatAndIsolation() {
    for (double fs : rates) {
        gill::Bands bands{};
        for (std::size_t i = 0; i < bands.size(); ++i) { bands[i].frequency = 20.0 * std::pow(2.0, static_cast<double>(i)); bands[i].channel = static_cast<int>(i) % 5; bands[i].type = static_cast<int>(i) % 3; bands[i].q = 18.0; }
        std::array<Sample, 2048> l{}, r{};
        for (std::size_t i = 0; i < l.size(); ++i) { l[i] = static_cast<Sample>(uniform(-1, 1)); r[i] = static_cast<Sample>(uniform(-1, 1)); }
        const auto oldL = l, oldR = r;
        gill::EqEngine e; e.prepare(fs); e.setBands(bands); Sample* c[]{l.data(), r.data()}; e.process(c, 2, static_cast<int>(l.size()));
        require(std::memcmp(l.data(), oldL.data(), sizeof(l)) == 0 && std::memcmp(r.data(), oldR.data(), sizeof(r)) == 0, "flat bit-exact stereo null");
        e.reset(); e.setBands(bands); l = oldL; e.process(c, 1, static_cast<int>(l.size()));
        require(std::memcmp(l.data(), oldL.data(), sizeof(l)) == 0, "flat bit-exact mono null");
        for (int mode : {gill::Left, gill::Right, gill::Mid, gill::Side}) {
            bands = emptyBands(); bands[0].enabled = true; bands[0].gainDb = 24; bands[0].q = 18; bands[0].channel = mode;
            l = oldL; r = oldR;
            if (mode == gill::Mid) for (std::size_t i = 0; i < r.size(); ++i) r[i] = -l[i];
            if (mode == gill::Side) r = l;
            const auto refL = l, refR = r;
            e.prepare(fs); e.setBands(bands); e.process(c, 2, static_cast<int>(l.size()));
            if (mode != gill::Left) require(std::memcmp(l.data(), refL.data(), sizeof(l)) == 0, "channel mode preserves excluded left signal");
            if (mode != gill::Right) require(std::memcmp(r.data(), refR.data(), sizeof(r)) == 0, "channel mode preserves excluded right signal");
        }
        audioSamples += l.size() * 11;
    }
}
void impulseFft() {
    constexpr std::size_t n = 131072;
    for (int test = 0; test < 120; ++test) {
        const double fs = rates[static_cast<std::size_t>(test) % rates.size()];
        auto bands = emptyBands();
        const int active = test < 90 ? 1 : 8;
        for (int band = 0; band < active; ++band) {
            auto& p = bands[static_cast<std::size_t>(band)]; p.enabled = true; p.type = (test + band) % 6;
            p.channel = test < 90 ? (test / 6) % 5 : (test + band * 3) % 5; p.slope = (test / 30) % 3;
            p.frequency = std::exp(uniform(std::log(300.0), std::log(14000.0)));
            p.gainDb = uniform(-12.0, 12.0); p.q = uniform(0.3, 2.0);
        }
        const bool exciteRight = test % 2 != 0;
        std::vector<double> l(n, 0), r(n, 0); (exciteRight ? r : l)[0] = 1.0;
        gill::EqEngine engine; engine.prepare(fs); engine.setBands(bands);
        double* c[]{l.data(), r.data()}; engine.process(c, 2, static_cast<int>(n)); audioSamples += n * 2;
        std::vector<Complex> left(n), right(n); for (std::size_t i = 0; i < n; ++i) { left[i] = l[i]; right[i] = r[i]; }
        fft(left); fft(right);
        for (int point = 0; point < 96; ++point) {
            const double f = std::exp(std::log(20.0) + (std::log(std::min(20000.0, fs * 0.49)) - std::log(20.0)) * point / 95.0);
            const auto bin = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(f * n / fs)));
            const auto matrix = gill::responseMatrix(bands, fs, bin * fs / n);
            const std::array<Complex, 2> expected{exciteRight ? matrix.lr : matrix.ll, exciteRight ? matrix.rr : matrix.rl};
            const std::array<Complex, 2> measured{left[bin], right[bin]};
            for (int channel = 0; channel < 2; ++channel) {
                const auto a = measured[static_cast<std::size_t>(channel)], b = expected[static_cast<std::size_t>(channel)];
                const double error = std::abs(a - b); maxFftComplexError = std::max(maxFftComplexError, error);
                require(error < 2e-7 * std::max(1.0, std::abs(b)), "real impulse FFT complex match " + std::to_string(test));
                if (std::abs(b) > 1e-5 && std::abs(a) > 1e-5) {
                    const double errorDb = std::abs(db(std::abs(a)) - db(std::abs(b))); maxFftDbError = std::max(maxFftDbError, errorDb);
                    require(errorDb < 0.001, "real impulse FFT dB match " + std::to_string(test));
                }
            }
        }
        ++fftConfigurations;
    }
}
void sineMeasurements() {
    for (double fs : rates) for (int type = 0; type < 6; ++type) for (int slope : {0, 2}) {
        auto bands = emptyBands(); auto& p = bands[0]; p.enabled = true; p.type = type; p.slope = slope; p.frequency = 1000; p.gainDb = type % 2 ? -18 : 18; p.q = 3;
        const double frequency = type == gill::Notch ? 1379.0 : 1000.0;
        const int settle = static_cast<int>(fs), measure = static_cast<int>(fs);
        gill::EqEngine e; e.prepare(fs); e.setBands(bands);
        std::array<double, 256> buffer{}; double* channels[]{buffer.data()};
        Complex measured(0, 0), input(0, 0);
        for (int offset = 0; offset < settle + measure; offset += 256) {
            const int count = std::min(256, settle + measure - offset);
            for (int i = 0; i < count; ++i) buffer[static_cast<std::size_t>(i)] = std::sin(2.0 * gill::pi * frequency * (offset + i) / fs) * 0.01;
            e.process(channels, 1, count); audioSamples += static_cast<std::size_t>(count);
            for (int i = 0; i < count; ++i) if (offset + i >= settle) {
                const double w = 2.0 * gill::pi * frequency * (offset + i) / fs;
                const auto rotation = std::polar(1.0, -w);
                measured += buffer[static_cast<std::size_t>(i)] * rotation; input += std::sin(w) * 0.01 * rotation;
            }
        }
        const double error = std::abs(db(std::abs(measured / input)) - db(std::abs(gill::coefficientResponse(p, fs, frequency))));
        maxSineDbError = std::max(maxSineDbError, error); require(error < 0.00001, "measured sine dB accuracy"); ++sineConfigurations;
    }
}
void automationAndRobustness() {
    for (double fs : rates) {
        gill::EqEngine e; e.prepare(fs); auto bands = emptyBands(); e.setBands(bands);
        std::array<double, 64> l{}, r{}; double* c[]{l.data(), r.data()};
        bool finite = true;
        for (int block = 0; block < 3000; ++block) {
            auto& p = bands[static_cast<std::size_t>(block / 31) % bands.size()];
            p.enabled = block % 29 != 0; p.frequency = block % 2 ? 20 : 20000; p.gainDb = block % 2 ? -24 : 24; p.q = block % 3 ? 18 : 0.1;
            if (block % 7 == 0) { p.type = (block / 7) % 6; p.channel = (block / 11) % 5; p.slope = (block / 13) % 3; }
            e.setBands(bands);
            for (std::size_t i = 0; i < l.size(); ++i) { l[i] = uniform(-0.00001, 0.00001); r[i] = uniform(-0.00001, 0.00001); }
            e.process(c, 2, 64); audioSamples += 128;
            for (std::size_t i = 0; i < l.size(); ++i) {
                finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
                maxAutomationAmplitude = std::max({maxAutomationAmplitude, std::abs(l[i]), std::abs(r[i])});
                require(std::isfinite(static_cast<float>(l[i])) && std::isfinite(static_cast<float>(r[i])), "automation output representable in float host");
            }
        }
        require(finite, "aggressive frequency/Q/gain/topology automation finite");
        bands = emptyBands(); bands[0].enabled = true; bands[0].gainDb = 24; bands[0].q = 18;
        e.prepare(fs); e.setBands(bands); l.fill(0); r.fill(0); e.process(c, 2, 64);
        require(std::all_of(l.begin(), l.end(), [](double x) { return x == 0; }), "exact silence no noise injection");
        l[0] = std::numeric_limits<double>::quiet_NaN(); r[0] = std::numeric_limits<double>::infinity(); e.process(c, 2, 64);
        require(std::all_of(l.begin(), l.end(), [](double x) { return std::isfinite(x); }) && std::all_of(r.begin(), r.end(), [](double x) { return std::isfinite(x); }), "nonfinite host input does not poison state");
        bands[0].frequency = std::numeric_limits<double>::quiet_NaN(); bands[0].gainDb = std::numeric_limits<double>::infinity(); bands[0].q = -100; bands[0].type = 999; bands[0].channel = -8; bands[0].slope = 100;
        e.setBands(bands); l.fill(0); r.fill(0); e.process(c, 2, 64);
        require(std::all_of(l.begin(), l.end(), [](double x) { return std::isfinite(x); }), "invalid state parameters sanitized");
        e.process(static_cast<double**>(nullptr), 2, 64); e.process(c, 0, 64); e.process(c, 2, 0);
        audioSamples += 384;
    }
    // A discrete switch must start from exactly the old output (smoothstep fade).
    auto bands = emptyBands(); gill::EqEngine switching, control; switching.prepare(48000); control.prepare(48000); switching.setBands(bands); control.setBands(bands);
    std::array<double, 1> a{0.125}, b{0.125}; double* ac[]{a.data()}; double* bc[]{b.data()};
    bands[0].enabled = true; bands[0].gainDb = 24; bands[0].type = gill::HighShelf; switching.setBands(bands);
    switching.process(ac, 1, 1); control.process(bc, 1, 1); require(a[0] == b[0], "topology crossfade starts continuously");
    // Continuous ramps settle to the exact new coefficients within20ms+8samples.
    bands = emptyBands(); bands[0].enabled = true; bands[0].gainDb = 1;
    switching.prepare(48000); switching.setBands(bands); bands[0].gainDb = 12; bands[0].frequency = 3000; bands[0].q = 2; switching.setBands(bands);
    std::vector<double> silence(1024, 0); double* s[]{silence.data()}; switching.process(s, 1, 1024);
    silence.assign(8192, 0); silence[0] = 1; double* impulse[]{silence.data()}; switching.process(impulse, 1, static_cast<int>(silence.size()));
    Complex actual(0, 0); for (std::size_t i = 0; i < silence.size(); ++i) actual += silence[i] * std::polar(1.0, -2.0 * gill::pi * 3000.0 * i / 48000.0);
    require(std::abs(actual - gill::coefficientResponse(bands[0], 48000, 3000)) < 1e-8, "parameter smoothing lands exactly on target response");
    audioSamples += 1 + 1 + 1024 + 8192;
}
void precisionBoundariesAndBlocks() {
    for (double fs : {8000.0, 11025.0, 22050.0, 32000.0, 44100.0, 768000.0}) {
        for (int type = 0; type < 6; ++type) for (double q : {0.1, 18.0}) {
            gill::BandParams p; p.type = type; p.frequency = 1000000; p.q = q; p.gainDb = 24; p.slope = 2;
            const auto bounded = gill::sanitize(p, fs);
            require(bounded.frequency == std::min(20000.0, fs * 0.49), "frequency clamp at host Nyquist guard");
            const auto design = gill::designFilter(p, fs);
            for (int i = 0; i < design.count; ++i) require(poleRadius(design.sections[static_cast<std::size_t>(i)]) < 1.0, "near Nyquist high Q stability");
            const auto a = gill::coefficientResponse(p, fs, bounded.frequency * 0.999), b = reference(p, fs, bounded.frequency * 0.999);
            require(std::abs(a - b) < 2e-6 * std::max(1.0, std::abs(b)), "near Nyquist high Q response");
        }
    }
    for (int type = 0; type < 6; ++type) {
        gill::Bands bands{};
        for (auto& p : bands) { p.type = type; p.q = 18; p.gainDb = 24; p.slope = 2; p.frequency = 1000; }
        gill::EqEngine e; e.prepare(48000); e.setBands(bands);
        std::vector<double> signal(65536, 0.0); signal[0] = 1.0; double* c[]{signal.data()}; e.process(c, 1, static_cast<int>(signal.size()));
        require(std::all_of(signal.begin(), signal.end(), [](double x) { return std::isfinite(x) && std::isfinite(static_cast<float>(x)); }), "eight coincident maximum gain/resonance bands stay float representable");
        audioSamples += signal.size();
    }
    constexpr std::size_t n = 16384;
    gill::Bands bands{};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        auto& p = bands[i]; p.type = static_cast<int>(i) % 6; p.channel = static_cast<int>(i) % 5; p.slope = static_cast<int>(i) % 3;
        p.frequency = 35.0 * std::pow(2.3, static_cast<double>(i)); p.q = 0.8; p.gainDb = 6;
    }
    std::vector<float> fl(n), fr(n); std::vector<double> dl(n), dr(n), splitL(n), splitR(n);
    for (std::size_t i = 0; i < n; ++i) { fl[i] = static_cast<float>(uniform(-0.25, 0.25)); fr[i] = static_cast<float>(uniform(-0.25, 0.25)); dl[i] = fl[i]; dr[i] = fr[i]; }
    splitL = dl; splitR = dr;
    gill::EqEngine f, d, split; f.prepare(48000); d.prepare(48000); split.prepare(48000); f.setBands(bands); d.setBands(bands); split.setBands(bands);
    // Include real continuous automation in the block-size invariance check.
    for (auto& p : bands) { p.frequency *= 1.2; p.q = 1.3; p.gainDb = -6; }
    f.setBands(bands); d.setBands(bands); split.setBands(bands);
    float* fc[]{fl.data(), fr.data()}; double* dc[]{dl.data(), dr.data()}; f.process(fc, 2, static_cast<int>(n)); d.process(dc, 2, static_cast<int>(n));
    for (std::size_t offset = 0; offset < n;) {
        const auto size = std::min<std::size_t>(n - offset, 1 + (offset * 17) % 257);
        double* sc[]{splitL.data() + offset, splitR.data() + offset}; split.process(sc, 2, static_cast<int>(size)); offset += size;
    }
    require(dl == splitL && dr == splitR, "automation and DSP invariant to host block partition");
    bool exact = true; for (std::size_t i = 0; i < n; ++i) exact = exact && fl[i] == static_cast<float>(dl[i]) && fr[i] == static_cast<float>(dr[i]);
    require(exact, "float host path equals rounded double path"); audioSamples += n * 6;
}
} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    randomizedParameters(); knownResponses(); flatAndIsolation<float>(); flatAndIsolation<double>(); impulseFft(); sineMeasurements(); automationAndRobustness(); precisionBoundariesAndBlocks();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << std::setprecision(12)
              << "{\n  \"suite\": \"GILLEQ DSP\",\n  \"seed\": \"0x47494c4c4551\",\n  \"passed\": " << (failures == 0 ? "true" : "false")
              << ",\n  \"checks\": " << checks << ",\n  \"failures\": " << failures
              << ",\n  \"randomized_real_audio_configurations\": " << configurations << ",\n  \"impulse_fft_configurations\": " << fftConfigurations
              << ",\n  \"sine_configurations\": " << sineConfigurations << ",\n  \"audio_channel_samples_processed\": " << audioSamples
              << ",\n  \"sample_rates\": [44100, 48000, 88200, 96000, 192000]"
              << ",\n  \"max_independent_reference_db_error\": " << maxReferenceDbError << ",\n  \"max_impulse_fft_db_error\": " << maxFftDbError
              << ",\n  \"max_impulse_fft_complex_error\": " << maxFftComplexError << ",\n  \"max_sine_db_error\": " << maxSineDbError
              << ",\n  \"max_pole_radius\": " << maxPoleRadius << ",\n  \"elapsed_seconds\": " << seconds
              << ",\n  \"max_aggressive_automation_output_for_1e_minus5_input\": " << maxAutomationAmplitude
              << ",\n  \"scope\": \"DSP verification; not proof of all possible host, GUI, automation or audio conditions\"\n}\n";
    return failures == 0 ? 0 : 1;
}
