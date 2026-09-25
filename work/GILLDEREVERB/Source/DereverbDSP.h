#pragma once

// Original real-time late-reverberation suppressor. The delayed, exponentially
// decayed PSD model follows the statistical principle in Lebart, Boucher and
// Denbigh, Acta Acustica87 (2001),359-366. This is not a gate, neural model,
// exact room inverse, or an implementation of another commercial plug-in.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gilldereverb {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr int fftSize = 2048;
constexpr int hopSize = 512;
constexpr int numBins = fftSize / 2 + 1;

struct Params {
    double amount = 0.55;
    double roomMs = 450.0;
    double preserve = 0.75;
    double lowHz = 80.0;
    double highHz = 16000.0;
    //1.0 preserves the original model exactly. Automatic upper-range settings
    //can deliberately overestimate late energy for stronger suppression.
    double lateWeight = 1.0;
};
inline double finiteClamp(double x, double lo, double hi, double fallback) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
// Single-control mode for new instances. The original Params defaults and
// processing path remain available unchanged for saved legacy projects.
// Protection relaxes progressively near the top of the control, where stronger
// room removal is prioritised over transparent preservation of every phoneme.
inline Params autoParameters(double amount) noexcept {
    const double a = finiteClamp(amount, 0.0, 1.0, 0.55);
    Params p;
    p.amount = a;
    p.preserve = 0.97 - 0.82 * std::pow(a, 1.6);
    // Keep the guide fixed while Amount moves: room decay itself is learned
    // from the input, and a moving guide would repeatedly clear that learning.
    p.roomMs = 450.0;
    p.lowHz = 30.0;
    p.highHz = 20000.0;
    p.lateWeight = 1.0 + 0.9 * a * a * a;
    return p;
}
inline double validSampleRate(double fs) noexcept {
    return std::isfinite(fs) && fs >= 8000.0 && fs <= 768000.0 ? fs : 48000.0;
}
inline Params sanitize(Params p, double fs) noexcept {
    fs = validSampleRate(fs);
    p.amount = finiteClamp(p.amount, 0.0, 1.0, 0.55);
    p.roomMs = finiteClamp(p.roomMs, 80.0, 1500.0, 450.0);
    p.preserve = finiteClamp(p.preserve, 0.0, 1.0, 0.75);
    p.highHz = finiteClamp(p.highHz, 1000.0, std::min(20000.0, 0.49 * fs), std::min(16000.0, 0.49 * fs));
    p.lowHz = finiteClamp(p.lowHz, 20.0, std::min(1000.0, p.highHz), 80.0);
    p.lateWeight = finiteClamp(p.lateWeight, 1.0, 1.9, 1.0);
    return p;
}

namespace detail {
inline double smoothStep(double x) noexcept { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }
struct Ramp {
    double value = 0.0, target = 0.0, step = 0.0;
    int remaining = 0;
    void reset(double v) noexcept { value = target = v; step = 0.0; remaining = 0; }
    void set(double v, int samples) noexcept {
        if (v != target) { target = v; remaining = samples; step = (target - value) / static_cast<double>(samples); }
    }
    double next() noexcept {
        if (remaining > 0) { if (--remaining == 0) value = target; else value += step; }
        return value;
    }
};
struct FFT {
    std::array<std::uint16_t, fftSize> permutation{};
    std::array<std::complex<double>, fftSize / 2> twiddle{};
    void prepare() noexcept {
        for (int i = 0; i < fftSize; ++i) {
            int reversed = 0, v = i;
            for (int bit = 0; bit < 11; ++bit) { reversed = (reversed << 1) | (v & 1); v >>= 1; }
            permutation[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(reversed);
        }
        for (int i = 0; i < fftSize / 2; ++i) twiddle[static_cast<std::size_t>(i)] = std::polar(1.0, -2.0 * pi * i / fftSize);
    }
    void transform(std::array<std::complex<double>, fftSize>& x, bool inverse) const noexcept {
        for (int i = 0; i < fftSize; ++i) {
            const int j = permutation[static_cast<std::size_t>(i)];
            if (i < j) std::swap(x[static_cast<std::size_t>(i)], x[static_cast<std::size_t>(j)]);
        }
        for (int length = 2; length <= fftSize; length <<= 1) {
            const int half = length / 2, stride = fftSize / length;
            for (int start = 0; start < fftSize; start += length) for (int j = 0; j < half; ++j) {
                auto w = twiddle[static_cast<std::size_t>(j * stride)];
                if (inverse) w = std::conj(w);
                const auto a = x[static_cast<std::size_t>(start + j)];
                const auto b = x[static_cast<std::size_t>(start + j + half)] * w;
                x[static_cast<std::size_t>(start + j)] = a + b;
                x[static_cast<std::size_t>(start + j + half)] = a - b;
            }
        }
        if (inverse) for (auto& v : x) v /= static_cast<double>(fftSize);
    }
};
struct Storage {
    static constexpr int historyFrames = 128;
    FFT fft;
    std::array<double, fftSize> window{};
    std::array<std::array<double, fftSize>, 2> input{}, dryDelay{};
    std::array<std::array<double, fftSize * 2>, 2> output{};
    std::array<std::array<std::complex<double>, fftSize>, 2> spectra{};
    std::array<std::array<double, numBins>, historyFrames> history{};
    std::array<double, historyFrames> logPowerHistory{};
    std::array<double, numBins> power{}, smoothedPower{}, targetGain{}, gain{}, smoothedGain{}, focus{};
    int inputIndex = 0, outputIndex = 0, hopCounter = 0, historyIndex = 0;
    int activeChannels = 0;
    int framesReceived = 0;
    void clear() noexcept {
        for (auto& c : input) c.fill(0.0);
        for (auto& c : dryDelay) c.fill(0.0);
        for (auto& c : output) c.fill(0.0);
        for (auto& c : spectra) c.fill({0.0, 0.0});
        for (auto& h : history) h.fill(0.0);
        logPowerHistory.fill(-100.0); framesReceived = 0;
        power.fill(0.0); smoothedPower.fill(0.0);
        targetGain.fill(1.0); gain.fill(1.0); smoothedGain.fill(1.0);
        inputIndex = outputIndex = hopCounter = historyIndex = 0;
        activeChannels = 0;
    }
};
} // namespace detail

class DereverbEngine {
public:
    // The only allocation is here, before audio processing starts. Keeping the
    // spectral history on the heap also avoids large plug-in/audio-thread stacks.
    void prepare(double fs) {
        sampleRate = validSampleRate(fs);
        if (!storage) storage = std::make_unique<detail::Storage>();
        storage->fft.prepare();
        for (int i = 0; i < fftSize; ++i)
            storage->window[static_cast<std::size_t>(i)] = std::sqrt(0.5 - 0.5 * std::cos(2.0 * pi * i / fftSize));
        rampSamples = std::max(1, static_cast<int>(std::lround(0.020 * sampleRate)));
        predictionDelay = std::clamp(static_cast<int>(std::lround(0.050 * sampleRate / hopSize)), 1, detail::Storage::historyFrames - 1);
        powerAlpha = std::exp(-static_cast<double>(hopSize) / (0.018 * sampleRate));
        closeAlpha = std::exp(-static_cast<double>(hopSize) / (0.016 * sampleRate));
        openAlpha = std::exp(-static_cast<double>(hopSize) / (0.002 * sampleRate));
        frameParamAlpha = std::exp(-static_cast<double>(hopSize) / (0.030 * sampleRate));
        initialized = false;
        reset();
    }
    void reset() noexcept {
        if (storage) storage->clear();
        current = requested = sanitize(requested, sampleRate);
        amount.reset(requested.amount); reductionDb = 0.0; initialized = false;
        learnedRoomMs = requested.roomMs; observedPeakPower = 0.0;
        updateModel();
    }
    void setParameters(const Params& p) noexcept {
        const auto next = sanitize(p, sampleRate);
        const bool manualRoomChange = next.roomMs != requested.roomMs;
        requested = next;
        if (!initialized) {
            current = requested; learnedRoomMs = requested.roomMs; amount.reset(requested.amount); initialized = true; updateModel();
        } else {
            amount.set(requested.amount, rampSamples);
            // A manual adjustment must take effect promptly, not wait for a
            // previously learned room to decay. New learning needs a fresh fit.
            if (manualRoomChange) { learnedRoomMs = requested.roomMs; if (storage) storage->framesReceived = 0; }
        }
    }
    int getLatencySamples() const noexcept { return fftSize; }
    double getReductionDb() const noexcept { return reductionDb; }
    double getEstimatedRoomMs() const noexcept { return std::max(current.roomMs, learnedRoomMs); }
    double getSampleRate() const noexcept { return sampleRate; }
    void process(float** channels, int nChannels, int nSamples, float** alignedDry = nullptr) noexcept { processImpl(channels, nChannels, nSamples, alignedDry); }
    void process(double** channels, int nChannels, int nSamples, double** alignedDry = nullptr) noexcept { processImpl(channels, nChannels, nSamples, alignedDry); }
private:
    std::unique_ptr<detail::Storage> storage;
    Params requested{}, current{};
    detail::Ramp amount;
    double sampleRate = 48000.0, powerAlpha = 0.55, closeAlpha = 0.51, openAlpha = 0.005, frameParamAlpha = 0.7;
    double lateDecay = 0.25, reductionDb = 0.0, learnedRoomMs = 450.0, observedPeakPower = 0.0;
    int rampSamples = 960, predictionDelay = 5;
    bool initialized = false;

    void updateModel() noexcept {
        lateDecay = std::exp(-13.815510557964274 * (predictionDelay * static_cast<double>(hopSize) / sampleRate) / (getEstimatedRoomMs() * 0.001));
        if (!storage) return;
        for (int k = 0; k < numBins; ++k) {
            const double hz = k * sampleRate / fftSize;
            const double low = detail::smoothStep((hz - current.lowHz) / std::max(20.0, current.lowHz * 0.75));
            const double high = 1.0 - detail::smoothStep((hz - current.highHz * 0.85) / std::max(50.0, current.highHz * 0.15));
            storage->focus[static_cast<std::size_t>(k)] = low * high;
        }
    }
    void trackDecay() noexcept {
        auto& s = *storage;
        double power = 0.0;
        for (int k = 1; k < numBins; ++k) power += s.smoothedPower[static_cast<std::size_t>(k)];
        observedPeakPower = std::max(power, observedPeakPower * std::exp(-static_cast<double>(hopSize) / (8.0 * sampleRate)));
        s.logPowerHistory[static_cast<std::size_t>(s.historyIndex)] = std::log(std::max(1.0e-30, power));
        s.framesReceived = std::min(s.framesReceived + 1, detail::Storage::historyFrames);
        const int length = std::clamp(static_cast<int>(std::lround(0.130 * sampleRate / hopSize)), 4, detail::Storage::historyFrames - 1);
        learnedRoomMs = std::max(current.roomMs, learnedRoomMs * std::exp(-static_cast<double>(hopSize) / (12.0 * sampleRate)));
        if (s.framesReceived < length || power < observedPeakPower * 1.0e-9 || power < 1.0e-20) return;
        double sum = 0.0, weighted = 0.0, variance = 0.0; int falling = 0;
        const double centre = (length - 1) * 0.5;
        for (int j = 0; j < length; ++j) {
            const int index = (s.historyIndex - length + 1 + j + detail::Storage::historyFrames) % detail::Storage::historyFrames;
            const double y = s.logPowerHistory[static_cast<std::size_t>(index)]; sum += y; weighted += (j - centre) * y;
            if (j > 0) {
                const int previous = (index + detail::Storage::historyFrames - 1) % detail::Storage::historyFrames;
                if (y < s.logPowerHistory[static_cast<std::size_t>(previous)]) ++falling;
            }
        }
        const double mean = sum / length;
        const double xx = length * (length * length - 1.0) / 12.0;
        const double slope = weighted / xx;
        for (int j = 0; j < length; ++j) {
            const int index = (s.historyIndex - length + 1 + j + detail::Storage::historyFrames) % detail::Storage::historyFrames;
            const double d = s.logPowerHistory[static_cast<std::size_t>(index)] - mean; variance += d * d;
        }
        const double fit = slope * slope * xx / std::max(1.0e-30, variance);
        const double dropDb = -slope * (length - 1) * 10.0 / std::log(10.0);
        if (slope >= 0.0 || fit < 0.90 || falling < static_cast<int>(0.80 * (length - 1)) || dropDb < 4.0 || dropDb > 35.0) return;
        const double candidate = -13.815510557964274 * hopSize * 1000.0 / (sampleRate * slope);
        // Decays shorter than180ms can be the analysis smoother/dry articulation.
        // Only sustained, nearly exponential decay can extend the manual guide.
        if (candidate >= 180.0 && candidate <= 1500.0 && candidate > learnedRoomMs)
            learnedRoomMs += 0.35 * (candidate - learnedRoomMs);
    }
    void processFrame(int channels) noexcept {
        auto& s = *storage;
        const double oneMinus = 1.0 - frameParamAlpha;
        current.roomMs += oneMinus * (requested.roomMs - current.roomMs);
        current.preserve += oneMinus * (requested.preserve - current.preserve);
        current.lowHz += oneMinus * (requested.lowHz - current.lowHz);
        current.highHz += oneMinus * (requested.highHz - current.highHz);
        current.lateWeight += oneMinus * (requested.lateWeight - current.lateWeight);
        updateModel();
        for (int c = 0; c < channels; ++c) {
            auto& spectrum = s.spectra[static_cast<std::size_t>(c)];
            for (int i = 0; i < fftSize; ++i)
                spectrum[static_cast<std::size_t>(i)] = s.input[static_cast<std::size_t>(c)][static_cast<std::size_t>((s.inputIndex + i) % fftSize)] * s.window[static_cast<std::size_t>(i)];
            s.fft.transform(spectrum, false);
        }
        for (int k = 0; k < numBins; ++k) {
            double p = 0.0;
            for (int c = 0; c < channels; ++c) p += std::norm(s.spectra[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)]);
            s.power[static_cast<std::size_t>(k)] = p / channels;
        }
        // A modest neighbouring-bin PSD average reduces random holes. It is an
        // energy estimate only; the original complex spectrum/phase is retained.
        for (int k = 0; k < numBins; ++k) {
            const auto i = static_cast<std::size_t>(k);
            const double local = 0.25 * s.power[static_cast<std::size_t>(std::max(0, k - 1))]
                               + 0.50 * s.power[i]
                               + 0.25 * s.power[static_cast<std::size_t>(std::min(numBins - 1, k + 1))];
            s.smoothedPower[i] = powerAlpha * s.smoothedPower[i] + (1.0 - powerAlpha) * local;
        }
        trackDecay();
        lateDecay = std::exp(-13.815510557964274 * (predictionDelay * static_cast<double>(hopSize) / sampleRate) / (getEstimatedRoomMs() * 0.001));
        const int delayed = (s.historyIndex - predictionDelay + detail::Storage::historyFrames) % detail::Storage::historyFrames;
        for (int k = 0; k < numBins; ++k) {
            const auto i = static_cast<std::size_t>(k);
            const double now = s.smoothedPower[i];
            const double past = s.history[static_cast<std::size_t>(delayed)][i];
            const double late = lateDecay * past;
            const double directConfidence = detail::smoothStep((now / (late + 1.0e-24) - 1.15) / 2.5);
            const double onsetConfidence = detail::smoothStep((now / (past + 1.0e-24) - 1.1) / 1.5);
            double neighbouring = 0.0;
            for (int offset = -3; offset <= 3; ++offset)
                neighbouring += s.power[static_cast<std::size_t>(std::clamp(k + offset, 0, numBins - 1))];
            neighbouring /= 7.0;
            const double tonalConfidence = detail::smoothStep((s.power[i] / (neighbouring + 1.0e-24) - 1.0) / 2.5);
            const double protect = std::max(onsetConfidence, directConfidence * (0.4 + 0.6 * tonalConfidence));
            const double estimatedLate = late * (1.0 - 0.88 * current.preserve * protect) * current.lateWeight;
            const double residualFraction = 1.0 - estimatedLate / (now + 1.0e-24);
            // Nonzero floor plus smoothing prevents isolated musical-noise bins.
            const double rawGain = now > 1.0e-24 ? std::sqrt(std::max(0.0064, std::min(1.0, residualFraction))) : 1.0;
            s.targetGain[i] = 1.0 + s.focus[i] * (rawGain - 1.0);
        }
        double totalPower = 0.0, remainingPower = 0.0;
        for (int k = 0; k < numBins; ++k) {
            const auto i = static_cast<std::size_t>(k);
            const double target = 0.20 * s.targetGain[static_cast<std::size_t>(std::max(0, k - 1))]
                                + 0.60 * s.targetGain[i]
                                + 0.20 * s.targetGain[static_cast<std::size_t>(std::min(numBins - 1, k + 1))];
            const double alpha = target > s.smoothedGain[i] ? openAlpha : closeAlpha;
            s.smoothedGain[i] = alpha * s.smoothedGain[i] + (1.0 - alpha) * target;
            s.gain[i] = std::clamp(s.smoothedGain[i], 0.08, 1.0);
            const double mixGain = 1.0 + amount.value * (s.gain[i] - 1.0);
            totalPower += s.power[i]; remainingPower += s.power[i] * mixGain * mixGain;
        }
        const double frameReduction = totalPower > 1.0e-24 ? -10.0 * std::log10(std::max(1.0e-12, remainingPower / totalPower)) : 0.0;
        reductionDb += 0.25 * (frameReduction - reductionDb);
        s.history[static_cast<std::size_t>(s.historyIndex)] = s.smoothedPower;
        s.historyIndex = (s.historyIndex + 1) % detail::Storage::historyFrames;
        for (int c = 0; c < channels; ++c) {
            auto& spectrum = s.spectra[static_cast<std::size_t>(c)];
            for (int k = 0; k < numBins; ++k) spectrum[static_cast<std::size_t>(k)] *= s.gain[static_cast<std::size_t>(k)];
            for (int k = 1; k < fftSize / 2; ++k) spectrum[static_cast<std::size_t>(fftSize - k)] = std::conj(spectrum[static_cast<std::size_t>(k)]);
            s.fft.transform(spectrum, true);
            // Periodic sqrt-Hann windows at75%overlap have sum(window^2)=2.
            // This frame starts at the next output sample, giving exactlyN delay.
            for (int i = 0; i < fftSize; ++i) {
                const int out = (s.outputIndex + i) % (fftSize * 2);
                s.output[static_cast<std::size_t>(c)][static_cast<std::size_t>(out)] += spectrum[static_cast<std::size_t>(i)].real() * s.window[static_cast<std::size_t>(i)] * 0.5;
            }
        }
    }
    template <class Sample> void processImpl(Sample** channels, int nChannels, int nSamples, Sample** alignedDry) noexcept {
        if (!storage || !channels || nChannels < 1 || !channels[0] || nSamples <= 0) return;
        const int nc = nChannels > 1 && channels[1] ? 2 : 1;
        auto& s = *storage;
        // Hosts normally call prepare for a layout change. Also defend against a
        // direct stereo/mono switch, so an old channel/history cannot reappear.
        if (s.activeChannels != 0 && s.activeChannels != nc) { s.clear(); reductionDb = 0.0; learnedRoomMs = current.roomMs; observedPeakPower = 0.0; }
        s.activeChannels = nc;
        for (int n = 0; n < nSamples; ++n) {
            const double mix = amount.next();
            for (int c = 0; c < nc; ++c) {
                const auto ci = static_cast<std::size_t>(c), ii = static_cast<std::size_t>(s.inputIndex), oi = static_cast<std::size_t>(s.outputIndex);
                double x = static_cast<double>(channels[c][n]);
                if (!std::isfinite(x) || std::abs(x) > 1.0e100) x = 0.0;
                const double dry = s.dryDelay[ci][ii];
                s.dryDelay[ci][ii] = x; s.input[ci][ii] = x;
                const double wet = s.output[ci][oi]; s.output[ci][oi] = 0.0;
                double value = mix == 0.0 ? dry : (mix == 1.0 ? wet : dry + mix * (wet - dry));
                if (!std::isfinite(value)) value = 0.0;
                Sample converted = static_cast<Sample>(value);
                if (!std::isfinite(static_cast<double>(converted))) converted = Sample{};
                channels[c][n] = converted;
                if (alignedDry && alignedDry[c]) alignedDry[c][n] = static_cast<Sample>(dry);
            }
            s.inputIndex = (s.inputIndex + 1) % fftSize;
            s.outputIndex = (s.outputIndex + 1) % (fftSize * 2);
            if (++s.hopCounter == hopSize) { s.hopCounter = 0; processFrame(nc); }
        }
    }
};
} // namespace gilldereverb
