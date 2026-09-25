#pragma once
#include "../../GILLCommon/LiveCausal.h"
// Original GILLPRODUCTION DSP. SPDX-License-Identifier: AGPL-3.0-only
// No JUCE or third-party DSP code. See work/spectral-notes.md for the algorithm.
#include <algorithm>
#include <type_traits>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace gillfinish {

struct SilkParameters {
    float depth = 50.f, sensitivity = 50.f;
    float lowHz = 120.f, highHz = 12000.f;
    float attackMs = 8.f, releaseMs = 140.f;
    float mix = 1.f, outputDb = 0.f;
};
struct SparkParameters {
    float depth = 50.f, sensitivity = 50.f;
    float lowHz = 1000.f, highHz = 16000.f;
    float attackMs = 1.f, decayMs = 70.f;
    float mix = 1.f, outputDb = 0.f;
    int mode = 0; // 0 = CUT, 1 = BOOST
};

namespace spectral_detail {
constexpr double pi = 3.1415926535897932384626433832795;
inline float finiteClamp(float x, float lo, float hi, float fallback) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
inline float clean(float x) noexcept {
    return std::isfinite(x) ? std::clamp(x, -32.f, 32.f) : 0.f;
}
struct Settings {
    float depth = .5f, sensitivity = .5f, low = 120.f, high = 12000.f;
    float attack = 8.f, release = 140.f, mix = 1.f, output = 1.f;
    bool boost = false;
};

// Storage is fixed: even prepare() does not allocate. One instance per processor.
template<int Capacity, bool Transient>
class Core {
public:
    void prepare(double rate, int, int channels) noexcept {
        sampleRate = std::isfinite(rate) ? std::clamp(rate, 8000., 192000.) : 48000.;
        channelCount = std::clamp(channels, 1, 2);
        size = Transient ? 128 : 256;
        const double minimum = sampleRate * (Transient ? .008 : .032);
        while (size < Capacity && size < minimum) size *= 2;
        hop = size / 4;
        const int bits = static_cast<int>(std::log2(size));
        for (int i = 0; i < size; ++i) {
            unsigned value = static_cast<unsigned>(i), reversed = 0;
            for (int bit = 0; bit < bits; ++bit) { reversed = (reversed << 1) | (value & 1U); value >>= 1; }
            reversal[static_cast<std::size_t>(i)] = static_cast<int>(reversed);
            window[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(pi * i / size));
        }
        for (int i = 0; i < size / 2; ++i) {
            const double phase = -2. * pi * i / size;
            twiddles[static_cast<std::size_t>(i)] = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
        }
        for (int bin = 0; bin <= size / 2; ++bin) {
            const float hz = static_cast<float>(bin * sampleRate / size);
            frequencies[static_cast<std::size_t>(bin)] = hz;
            viewIndex[static_cast<std::size_t>(bin)] = std::clamp(static_cast<int>(127. * std::log(std::max(40.f, hz) / 40.) / std::log(500.)), 0, 127);
        }
        sampleSmooth = std::exp(-1. / (.005 * sampleRate));
        prepared = true;
        reset();
    }

    void set(Settings next) noexcept { settings = next; }
    void reset() noexcept {
        for (auto& a : input) a.fill(0.f);
        for (auto& a : overlap) a.fill(0.f);
        envelopeDb.fill(0.f); history.fill(0.f); power.fill(0.f); targetDb.fill(0.f);
        view.fill(0.f); peakDb = 0.f;
        inputPosition = outputPosition = untilFrame = 0;
        currentMix = settings.mix; currentOutput = settings.output;
    }
    int latencySamples() const noexcept { return size; }
    float reductionDb() const noexcept { return peakDb; }
    std::array<float, 128> reductionView() const noexcept { return view; }

    void process(float* const* data, int numSamples, int channels) noexcept {
        if (!prepared || data == nullptr || numSamples <= 0) return;
        const int active = std::clamp(channels, 0, channelCount);
        if (active == 0) return;
        const int mask = size - 1, outputMask = size * 2 - 1;
        for (int n = 0; n < numSamples; ++n) {
            currentMix = settings.mix + sampleSmooth * (currentMix - settings.mix);
            currentOutput = settings.output + sampleSmooth * (currentOutput - settings.output);
            // Snap only below float roundoff; the settled unity/dry routes are exact.
            if (std::abs(currentMix - settings.mix) < 1.e-9) currentMix = settings.mix;
            if (std::abs(currentOutput - settings.output) < 1.e-9) currentOutput = settings.output;
            for (int c = 0; c < channelCount; ++c) {
                const auto ch = static_cast<std::size_t>(c);
                const float dry = input[ch][static_cast<std::size_t>(inputPosition)];
                input[ch][static_cast<std::size_t>(inputPosition)] = c < active && data[c] != nullptr ? clean(data[c][n]) : 0.f;
                const float residual = overlap[ch][static_cast<std::size_t>(outputPosition)];
                overlap[ch][static_cast<std::size_t>(outputPosition)] = 0.f;
                if (c < active && data[c] != nullptr) data[c][n] = clean(static_cast<float>((dry + currentMix * residual) * currentOutput));
            }
            inputPosition = (inputPosition + 1) & mask;
            if (++untilFrame == hop) { untilFrame = 0; frame(active); }
            outputPosition = (outputPosition + 1) & outputMask;
        }
    }

private:
    using Complex = std::complex<float>;
    void fft(std::array<Complex, Capacity>& a, bool inverse) noexcept {
        for (int i = 0; i < size; ++i) {
            const int j = reversal[static_cast<std::size_t>(i)];
            if (i < j) std::swap(a[static_cast<std::size_t>(i)], a[static_cast<std::size_t>(j)]);
        }
        for (int length = 2; length <= size; length *= 2) {
            const int half = length / 2, stride = size / length;
            for (int at = 0; at < size; at += length) for (int j = 0; j < half; ++j) {
                Complex w = twiddles[static_cast<std::size_t>(j * stride)];
                if (inverse) w = std::conj(w);
                const Complex a0 = a[static_cast<std::size_t>(at + j)];
                const Complex a1 = a[static_cast<std::size_t>(at + j + half)] * w;
                a[static_cast<std::size_t>(at + j)] = a0 + a1;
                a[static_cast<std::size_t>(at + j + half)] = a0 - a1;
            }
        }
        if (inverse) { const float reciprocal = 1.f / size; for (int i = 0; i < size; ++i) a[static_cast<std::size_t>(i)] *= reciprocal; }
    }

    float focus(int bin) const noexcept {
        const float hz = frequencies[static_cast<std::size_t>(bin)];
        const float low = std::min(settings.low, settings.high), high = std::max(settings.low, settings.high);
        // Soft skirts avoid an abrupt brickwall edge. DC and Nyquist stay unchanged.
        const float lower = std::clamp((hz - low * .75f) / std::max(1.f, low * .5f), 0.f, 1.f);
        const float upper = std::clamp((high * 1.15f - hz) / std::max(1.f, high * .3f), 0.f, 1.f);
        return bin == 0 || bin == size / 2 ? 0.f : lower * upper;
    }

    void frame(int active) noexcept {
        const int bins = size / 2, mask = size - 1;
        power.fill(0.f);
        for (int c = 0; c < channelCount; ++c) {
            const auto ch = static_cast<std::size_t>(c);
            for (int i = 0; i < size; ++i) spectrum[ch][static_cast<std::size_t>(i)] = input[ch][static_cast<std::size_t>((inputPosition + i) & mask)] * window[static_cast<std::size_t>(i)];
            fft(spectrum[ch], false);
            if (c < active) for (int i = 0; i <= bins; ++i) power[static_cast<std::size_t>(i)] = std::max(power[static_cast<std::size_t>(i)], std::norm(spectrum[ch][static_cast<std::size_t>(i)]) * (4.f / (size * size)));
        }
        const float attack = static_cast<float>(std::exp(-hop / (.001 * settings.attack * sampleRate)));
        const float release = static_cast<float>(std::exp(-hop / (.001 * settings.release * sampleRate)));
        const float historyUp = static_cast<float>(std::exp(-hop / (.030 * sampleRate)));
        const float historyDown = static_cast<float>(std::exp(-hop / (.150 * sampleRate)));
        const float threshold = Transient ? 16.f - 14.f * settings.sensitivity : 18.f - 15.f * settings.sensitivity;
        for (int bin = 0; bin <= bins; ++bin) {
            const auto k = static_cast<std::size_t>(bin);
            float amount = 0.f;
            if constexpr (Transient) {
                const float magnitude = std::sqrt(power[k]);
                // An absolute floor stops sub-audible noise from being called a click.
                const float novelty = 20.f * std::log10((magnitude + 0.00004f) / (history[k] + 0.00004f));
                const float limit = settings.boost ? 12.f : 24.f;
                amount = std::clamp((novelty - threshold) * (settings.boost ? .6f : 1.f), 0.f, limit) * settings.depth;
                if (settings.boost) amount = -amount;
                const float coefficient = magnitude > history[k] ? historyUp : historyDown;
                history[k] = magnitude + coefficient * (history[k] - magnitude);
                if (history[k] < 1.e-20f) history[k] = 0.f;
            } else {
                // Estimate the surrounding spectrum, excluding the peak's main lobe.
                // A broad hump is retained; a narrow excess above its neighbours is cut.
                const int radius = std::clamp(static_cast<int>(bin * .15f), 5, 48);
                float surroundings = 0.f; int neighbours = 0;
                for (int j = std::max(1, bin - radius); j <= std::min(bins - 1, bin + radius); ++j) if (std::abs(j - bin) > 2) { surroundings += power[static_cast<std::size_t>(j)]; ++neighbours; }
                const float baseline = neighbours > 0 ? surroundings / neighbours : power[k];
                const float prominence = 10.f * std::log10((power[k] + 1.e-10f) / (baseline + 1.e-10f));
                amount = std::clamp((prominence - threshold) * .85f, 0.f, 18.f) * settings.depth;
            }
            targetDb[k] = amount * focus(bin);
        }
        // A three-bin mask retains spectral detail while reducing isolated-bin flutter.
        view.fill(0.f); peakDb = 0.f;
        for (int bin = 0; bin <= bins; ++bin) {
            const auto k = static_cast<std::size_t>(bin);
            float target = targetDb[k];
            if (bin > 0 && bin < bins) target = target * .75f + .125f * (targetDb[k - 1] + targetDb[k + 1]);
            const bool attacking = target * envelopeDb[k] < 0.f || std::abs(target) > std::abs(envelopeDb[k]);
            const float coefficient = attacking ? attack : release;
            envelopeDb[k] = target + coefficient * (envelopeDb[k] - target);
            if (std::abs(envelopeDb[k]) < 1.e-7f) envelopeDb[k] = 0.f;
            if (std::abs(envelopeDb[k]) > std::abs(peakDb)) peakDb = envelopeDb[k];
            float& slot = view[static_cast<std::size_t>(viewIndex[k])];
            if (std::abs(envelopeDb[k]) > std::abs(slot)) slot = envelopeDb[k];
            const float residualGain = envelopeDb[k] == 0.f ? 0.f : std::pow(10.f, -envelopeDb[k] / 20.f) - 1.f;
            for (int c = 0; c < channelCount; ++c) {
                spectrum[static_cast<std::size_t>(c)][k] *= residualGain;
                if (bin > 0 && bin < bins) spectrum[static_cast<std::size_t>(c)][static_cast<std::size_t>(size - bin)] *= residualGain;
            }
        }
        const int outputMask = size * 2 - 1;
        for (int c = 0; c < channelCount; ++c) {
            auto& spec = spectrum[static_cast<std::size_t>(c)]; fft(spec, true);
            for (int i = 0; i < size; ++i) {
                // sqrt-Hann analysis/synthesis at N/4 hop: sum(window^2) == 2.
                // Frame starts at the next output sample, exactly N after its input.
                overlap[static_cast<std::size_t>(c)][static_cast<std::size_t>((outputPosition + 1 + i) & outputMask)] += spec[static_cast<std::size_t>(i)].real() * window[static_cast<std::size_t>(i)] * .5f;
            }
        }
    }

    Settings settings;
    double sampleRate = 48000.;
    int size = Transient ? 512 : 2048, hop = size / 4, channelCount = 2;
    int inputPosition = 0, outputPosition = 0, untilFrame = 0;
    double currentMix = 1., currentOutput = 1., sampleSmooth = 0.;
    float peakDb = 0.f;
    bool prepared = false;
    std::array<std::array<float, Capacity>, 2> input{};
    std::array<std::array<float, Capacity * 2>, 2> overlap{};
    std::array<std::array<Complex, Capacity>, 2> spectrum{};
    std::array<Complex, Capacity / 2> twiddles{};
    std::array<float, Capacity> window{};
    std::array<int, Capacity> reversal{};
    std::array<float, Capacity / 2 + 1> frequencies{}, power{}, history{}, targetDb{}, envelopeDb{};
    std::array<int, Capacity / 2 + 1> viewIndex{};
    std::array<float, 128> view{};
};

inline Settings convert(const SilkParameters& p) noexcept {
    Settings s;
    s.depth = finiteClamp(p.depth, 0.f, 100.f, 50.f) * .01f; s.sensitivity = finiteClamp(p.sensitivity, 0.f, 100.f, 50.f) * .01f;
    s.low = finiteClamp(p.lowHz, 20.f, 20000.f, 120.f); s.high = finiteClamp(p.highHz, 20.f, 20000.f, 12000.f);
    s.attack = finiteClamp(p.attackMs, .1f, 100.f, 8.f); s.release = finiteClamp(p.releaseMs, 5.f, 1000.f, 140.f);
    s.mix = finiteClamp(p.mix, 0.f, 1.f, 1.f); s.output = std::pow(10.f, finiteClamp(p.outputDb, -18.f, 6.f, 0.f) / 20.f);
    return s;
}
inline Settings convert(const SparkParameters& p) noexcept {
    Settings s;
    s.depth = finiteClamp(p.depth, 0.f, 100.f, 50.f) * .01f; s.sensitivity = finiteClamp(p.sensitivity, 0.f, 100.f, 50.f) * .01f;
    s.low = finiteClamp(p.lowHz, 20.f, 20000.f, 1000.f); s.high = finiteClamp(p.highHz, 20.f, 20000.f, 16000.f);
    s.attack = finiteClamp(p.attackMs, .1f, 40.f, 1.f); s.release = finiteClamp(p.decayMs, 5.f, 400.f, 70.f);
    s.mix = finiteClamp(p.mix, 0.f, 1.f, 1.f); s.output = std::pow(10.f, finiteClamp(p.outputDb, -18.f, 6.f, 0.f) / 20.f); s.boost = p.mode == 1;
    return s;
}
} // namespace spectral_detail

template<bool IsSpark>class QualitySpectral {
public:
 using Parameters=std::conditional_t<IsSpark,SparkParameters,SilkParameters>;
 QualitySpectral()noexcept{setParameters(Parameters{});}
 void setLiveMode(bool v)noexcept{if(v!=liveMode_){liveMode_=v;reset();}}
 void prepare(double rate,int block,int channels)noexcept{core.prepare(rate,block,channels);live.prepare(rate,channels,IsSpark?gill::live::Kind::Spark:gill::live::Kind::Silk);}
 void reset()noexcept{core.reset();live.reset();}
 void setParameters(const Parameters&p)noexcept{const auto s=spectral_detail::convert(p);core.set(s);gill::live::Settings v;v.amount=s.depth;v.sensitivity=s.sensitivity;v.low=s.low;v.high=s.high;v.attack=s.attack*.001;v.release=s.release*.001;v.mix=s.mix;v.output=s.output;v.boost=s.boost;live.set(v);}
 void process(float*const*data,int count,int channels)noexcept{if(liveMode_)live.process(data,channels,count);else core.process(data,count,channels);}
 int latencySamples()const noexcept{return liveMode_?0:core.latencySamples();}
 float reductionDb()const noexcept{return liveMode_?float(live.reductionDb()):core.reductionDb();}
 std::array<float,128>reductionView()const noexcept{return liveMode_?live.reductionView():core.reductionView();}
private:spectral_detail::Core<IsSpark?2048:8192,IsSpark>core;gill::live::CausalBands live;bool liveMode_=false;
};
using SilkDSP=QualitySpectral<false>;
using SparkDSP=QualitySpectral<true>;
} // namespace gillfinish
