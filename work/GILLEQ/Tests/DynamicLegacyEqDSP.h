#pragma once

// GILLEQ DSP. Original implementation of the public RBJ/W3C Audio EQ Cookbook
// equations: https://www.w3.org/TR/audio-eq-cookbook/ . No third-party plug-in code.
// The graph and audio path deliberately share designFilter(), including frequency
// clamping and every cascaded section. All audio state is double precision.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace gill {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr std::size_t numBands = 8;
enum FilterType { Bell = 0, LowShelf = 1, HighShelf = 2, LowCut = 3, HighCut = 4, Notch = 5 };
enum ChannelMode { Stereo = 0, Mid = 1, Side = 2, Left = 3, Right = 4 };

struct BandParams {
    bool enabled = true;
    int type = Bell, channel = Stereo, slope = 0;
    double frequency = 1000.0, gainDb = 0.0, q = 0.7071067811865476;
};
using Bands = std::array<BandParams, numBands>;

inline double validSampleRate(double fs) noexcept {
    return std::isfinite(fs) && fs >= 8000.0 && fs <= 768000.0 ? fs : 48000.0;
}
inline double finiteClamp(double x, double lo, double hi, double fallback) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
inline BandParams sanitize(BandParams p, double fs) noexcept {
    fs = validSampleRate(fs);
    p.type = std::clamp(p.type, 0, 5); p.channel = std::clamp(p.channel, 0, 4);
    p.slope = std::clamp(p.slope, 0, 2);
    p.frequency = finiteClamp(p.frequency, 20.0, std::min(20000.0, 0.49 * fs), 1000.0);
    p.gainDb = finiteClamp(p.gainDb, -24.0, 24.0, 0.0);
    p.q = finiteClamp(p.q, 0.1, 18.0, 0.7071067811865476);
    return p;
}
inline bool sameTopology(const BandParams& a, const BandParams& b) noexcept {
    return a.enabled == b.enabled && a.type == b.type && a.channel == b.channel && a.slope == b.slope;
}
struct Coefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    bool identity = true;
};
struct DesignedFilter {
    std::array<Coefficients, 4> sections{};
    int count = 0;
};

inline Coefficients designSection(int type, double frequency, double gainDb, double q, double fs) noexcept {
    if (type <= HighShelf && gainDb == 0.0) return {};
    const double w = 2.0 * pi * frequency / fs;
    const double c = std::cos(w), s = std::sin(w);
    const double alpha = s / (2.0 * q), A = std::pow(10.0, gainDb / 40.0);
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
        case LowShelf: {
            const double t = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * c + t);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c);
            b2 = A * ((A + 1.0) - (A - 1.0) * c - t);
            a0 = (A + 1.0) + (A - 1.0) * c + t;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c);
            a2 = (A + 1.0) + (A - 1.0) * c - t;
            break;
        }
        case HighShelf: {
            const double t = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * c + t);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
            b2 = A * ((A + 1.0) + (A - 1.0) * c - t);
            a0 = (A + 1.0) - (A - 1.0) * c + t;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
            a2 = (A + 1.0) - (A - 1.0) * c - t;
            break;
        }
        case LowCut:
            b0 = (1.0 + c) * 0.5; b1 = -(1.0 + c); b2 = b0;
            a0 = 1.0 + alpha; a1 = -2.0 * c; a2 = 1.0 - alpha; break;
        case HighCut:
            // sin(w/2)^2 avoids loss of precision at a low cutoff/high fs.
            b0 = std::sin(w * 0.5) * std::sin(w * 0.5); b1 = 2.0 * b0; b2 = b0;
            a0 = 1.0 + alpha; a1 = -2.0 * c; a2 = 1.0 - alpha; break;
        case Notch:
            b0 = 1.0; b1 = -2.0 * c; b2 = 1.0;
            a0 = 1.0 + alpha; a1 = -2.0 * c; a2 = 1.0 - alpha; break;
        default:
            b0 = 1.0 + alpha * A; b1 = -2.0 * c; b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A; a1 = -2.0 * c; a2 = 1.0 - alpha / A; break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0, false };
}

inline DesignedFilter designFilter(BandParams p, double fs) noexcept {
    fs = validSampleRate(fs); p = sanitize(p, fs);
    DesignedFilter result;
    if (!p.enabled || (p.type <= HighShelf && p.gainDb == 0.0)) return result;
    const bool cut = p.type == LowCut || p.type == HighCut;
    result.count = cut ? (1 << p.slope) : 1;
    for (int i = 0; i < result.count; ++i) {
        // At Q=sqrt(1/2), all cut slopes are Butterworth (-3.0103 dB at
        // cutoff). Q changes the most resonant section only: scaling every
        // section would multiply the resonance four times in a48dB cut.
        const double sectionQ = cut && result.count > 1
            ? (i == result.count - 1 ? p.q / std::sqrt(0.5) : 1.0)
                / (2.0 * std::cos(pi * (2.0 * i + 1.0) / (4.0 * result.count)))
            : p.q;
        result.sections[static_cast<std::size_t>(i)] = designSection(p.type, p.frequency, p.gainDb, sectionQ, fs);
    }
    return result;
}
inline std::complex<double> sectionResponse(const Coefficients& c, double fs, double frequency) noexcept {
    if (c.identity) return { 1.0, 0.0 };
    const auto z = std::polar(1.0, -2.0 * pi * frequency / validSampleRate(fs));
    return (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
}
inline std::complex<double> coefficientResponse(const BandParams& p, double fs, double frequency) noexcept {
    auto result = std::complex<double>(1.0, 0.0);
    const auto design = designFilter(p, fs);
    frequency = finiteClamp(frequency, 0.0, validSampleRate(fs) * 0.5, 1000.0);
    for (int i = 0; i < design.count; ++i) result *= sectionResponse(design.sections[static_cast<std::size_t>(i)], fs, frequency);
    return result;
}
struct ResponseMatrix {
    std::complex<double> ll{1.0, 0.0}, lr{}, rl{}, rr{1.0, 0.0};
};
inline ResponseMatrix responseMatrix(const Bands& bands, double fs, double frequency) noexcept {
    ResponseMatrix total;
    for (const auto& raw : bands) {
        const auto p = sanitize(raw, fs); const auto h = coefficientResponse(p, fs, frequency);
        ResponseMatrix b;
        if (p.channel == Stereo) { b.ll = b.rr = h; }
        else if (p.channel == Mid) { b.ll = b.rr = (h + 1.0) * 0.5; b.lr = b.rl = (h - 1.0) * 0.5; }
        else if (p.channel == Side) { b.ll = b.rr = (h + 1.0) * 0.5; b.lr = b.rl = (1.0 - h) * 0.5; }
        else if (p.channel == Left) b.ll = h;
        else b.rr = h;
        total = { b.ll * total.ll + b.lr * total.rl, b.ll * total.lr + b.lr * total.rr,
                  b.rl * total.ll + b.rr * total.rl, b.rl * total.lr + b.rr * total.rr };
    }
    return total;
}
// Normalized output energy for a specified coherent input mode. A stereo EQ
// that mixes Mid/Side and Left/Right cannot in general have one scalar curve.
// mode0/1: L=R; mode2: L=-R; mode3: left only; mode4: right only.
inline double magnitudeResponse(const Bands& bands, double fs, double frequency, int responseMode = Stereo) noexcept {
    const auto m = responseMatrix(bands, fs, frequency);
    if (responseMode == Left) return std::sqrt(std::norm(m.ll) + std::norm(m.rl));
    if (responseMode == Right) return std::sqrt(std::norm(m.lr) + std::norm(m.rr));
    const double polarity = responseMode == Side ? -1.0 : 1.0;
    return std::sqrt((std::norm(m.ll + polarity * m.lr) + std::norm(m.rl + polarity * m.rr)) * 0.5);
}

namespace detail {
struct BiquadState {
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
    double process(double x, const Coefficients& c) noexcept {
        // Direct Form I retains real input/output history when coefficients
        // change, instead of reinterpreting transposed-form internal states.
        const double y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = std::abs(y) < 1.0e-280 ? 0.0 : y;
        return y;
    }
};
struct Ramp {
    double value = 0.0, target = 0.0, step = 0.0;
    int remaining = 0;
    void reset(double x) noexcept { value = target = x; step = 0.0; remaining = 0; }
    void set(double x, int samples) noexcept {
        if (x == target) return;
        target = x; remaining = samples; step = (target - value) / static_cast<double>(samples);
    }
    bool advance() noexcept {
        if (remaining <= 0) return false;
        if (--remaining == 0) value = target; else value += step;
        return true;
    }
};
struct BandState {
    BandParams params{};
    Ramp logFrequency, gain, logQ;
    DesignedFilter design;
    std::array<std::array<BiquadState, 4>, 2> state{};
    int coefficientCountdown = 0;
    bool dirty = false;
    void clearHistory() noexcept { for (auto& channel : state) for (auto& s : channel) s.reset(); }
    void configure(const BandParams& p, double fs, bool clear) noexcept {
        params = p; logFrequency.reset(std::log(p.frequency)); gain.reset(p.gainDb); logQ.reset(std::log(p.q));
        design = designFilter(p, fs); coefficientCountdown = 0; dirty = false;
        if (clear) clearHistory();
    }
    void setContinuous(const BandParams& p, int samples) noexcept {
        logFrequency.set(std::log(p.frequency), samples); gain.set(p.gainDb, samples); logQ.set(std::log(p.q), samples);
        params.frequency = p.frequency; params.gainDb = p.gainDb; params.q = p.q;
    }
    void advance(double fs) noexcept {
        const bool a = logFrequency.advance(), b = gain.advance(), c = logQ.advance();
        dirty = dirty || a || b || c;
        if (--coefficientCountdown <= 0) {
            coefficientCountdown = 8;
            if (dirty) {
                auto p = params; p.frequency = std::exp(logFrequency.value); p.gainDb = gain.value; p.q = std::exp(logQ.value);
                const auto oldCount = design.count; design = designFilter(p, fs);
                if (oldCount != design.count) clearHistory();
                dirty = false;
            }
        }
    }
    double processChannel(double x, int channel) noexcept {
        for (int i = 0; i < design.count; ++i)
            x = state[static_cast<std::size_t>(channel)][static_cast<std::size_t>(i)].process(x, design.sections[static_cast<std::size_t>(i)]);
        return x;
    }
    void process(double& l, double& r, bool stereo) noexcept {
        if (design.count == 0) return;
        if (!stereo) {
            if (params.channel != Side && params.channel != Right) l = processChannel(l, 0);
            return;
        }
        switch (params.channel) {
            case Mid: {
                const double m = (l + r) * 0.5;
                const double delta = processChannel(m, 0) - m; l += delta; r += delta; break;
            }
            case Side: {
                const double s = (l - r) * 0.5;
                const double delta = processChannel(s, 0) - s; l += delta; r -= delta; break;
            }
            case Left: l = processChannel(l, 0); break;
            case Right: r = processChannel(r, 1); break;
            default: l = processChannel(l, 0); r = processChannel(r, 1); break;
        }
    }
};
struct Bank {
    std::array<BandState, numBands> bands{};
    void process(double& l, double& r, bool stereo, double fs, bool advanceRamps) noexcept {
        for (auto& b : bands) { if (advanceRamps) b.advance(fs); b.process(l, r, stereo); }
    }
};
} // detail

class EqEngine {
public:
    void prepare(double fs) noexcept {
        sampleRate = validSampleRate(fs); rampSamples = std::max(1, static_cast<int>(std::lround(sampleRate * 0.020)));
        initialized = false; fadeRemaining = 0; reset();
    }
    void reset() noexcept {
        fadeRemaining = 0;
        for (std::size_t i = 0; i < numBands; ++i) current.bands[i].configure(sanitize(requested[i], sampleRate), sampleRate, true);
        previous = current;
        // Next setBands after prepare/reset is loaded without a startup fade.
        initialized = false;
    }
    void setBands(const Bands& bands) noexcept {
        for (std::size_t i = 0; i < numBands; ++i) requested[i] = sanitize(bands[i], sampleRate);
        if (!initialized) {
            for (std::size_t i = 0; i < numBands; ++i) current.bands[i].configure(requested[i], sampleRate, true);
            initialized = true; fadeRemaining = 0; return;
        }
        applyRequested();
    }
    void process(float** channels, int nChannels, int nSamples) noexcept { processImpl(channels, nChannels, nSamples); }
    void process(double** channels, int nChannels, int nSamples) noexcept { processImpl(channels, nChannels, nSamples); }
    double getSampleRate() const noexcept { return sampleRate; }
    bool isTransitioning() const noexcept { return fadeRemaining > 0; }
private:
    double sampleRate = 48000.0;
    int rampSamples = 960, fadeRemaining = 0;
    bool initialized = false;
    Bands requested{};
    detail::Bank current{}, previous{};
    void applyRequested() noexcept {
        bool topologyChanged = false;
        for (std::size_t i = 0; i < numBands; ++i)
            topologyChanged = topologyChanged || !sameTopology(current.bands[i].params, requested[i]);
        // Coalesce very fast discrete automation. Finish a fade before starting
        // the next one, so an interrupted crossfade cannot create a discontinuity.
        if (topologyChanged && fadeRemaining > 0) return;
        if (topologyChanged) {
            previous = current; fadeRemaining = rampSamples;
            for (std::size_t i = 0; i < numBands; ++i)
                if (!sameTopology(current.bands[i].params, requested[i])) current.bands[i].configure(requested[i], sampleRate, true);
        }
        for (std::size_t i = 0; i < numBands; ++i) current.bands[i].setContinuous(requested[i], rampSamples);
    }
    template <class Sample> void processImpl(Sample** channels, int nChannels, int nSamples) noexcept {
        if (!channels || nChannels < 1 || nSamples <= 0 || !channels[0]) return;
        const bool stereo = nChannels > 1 && channels[1] != nullptr;
        for (int n = 0; n < nSamples; ++n) {
            double l = static_cast<double>(channels[0][n]), r = stereo ? static_cast<double>(channels[1][n]) : 0.0;
            // Nonfinite host input must not poison recursive state indefinitely.
            if (!std::isfinite(l)) l = 0.0; if (!std::isfinite(r)) r = 0.0;
            double oldL = l, oldR = r;
            current.process(l, r, stereo, sampleRate, true);
            if (fadeRemaining > 0) {
                previous.process(oldL, oldR, stereo, sampleRate, false);
                const double phase = 1.0 - static_cast<double>(fadeRemaining) / static_cast<double>(rampSamples);
                const double wet = phase * phase * (3.0 - 2.0 * phase);
                l = oldL + wet * (l - oldL); r = oldR + wet * (r - oldR);
                if (--fadeRemaining == 0) applyRequested();
            }
            channels[0][n] = static_cast<Sample>(l);
            if (stereo) channels[1][n] = static_cast<Sample>(r);
        }
    }
};
} // namespace gill
