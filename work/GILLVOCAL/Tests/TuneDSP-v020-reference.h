#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "../ThirdParty/signalsmith-stretch/signalsmith-stretch.h"

namespace gill {

// Monophonic vocal correction. prepare/reset/setParameters/process belong to the
// audio thread (prepare while stopped); meter getters may be read by the UI.
// Supported layouts: mono/stereo. Dry, wet and unvoiced paths share fixed latency.
class TuneDSP {
public:
    void prepare(double sampleRate, int maxBlock, int channels) {
        // Do not silently retime audio. Unsupported rates pass through with zero latency.
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 768000.0) {
            prepared_ = false;
            latency_ = 0;
            reset();
            return;
        }
        sampleRate_ = sampleRate;
        mixRampLength_ = std::max(1, static_cast<int>(std::round(sampleRate_ * 0.005)));
        channels_ = std::clamp(channels, 1, 2);
        (void)maxBlock; // Internal fixed chunks also handle larger host blocks.
        int spectralSize = 256;
        while (spectralSize < sampleRate_ * 0.042) spectralSize *= 2;
        // High overlap substantially improves small-shift precision on low notes.
        stretch_.configure(channels_, spectralSize, spectralSize / 32, false);
        stretch_.setFormantFactor(1.0f, true); // Approximate spectral envelope preservation.
        latency_ = stretch_.inputLatency() + stretch_.outputLatency();
        for (auto& d : dryDelay_) d.assign(static_cast<size_t>(latency_), 0.0f);
        voiceDelay_.assign(static_cast<size_t>(latency_), 0.0f);
        decimation_ = std::max(1, static_cast<int>(std::round(sampleRate_ / 12000.0)));
        detectorRate_ = sampleRate_ / decimation_;
        maxLag_ = static_cast<int>(std::ceil(detectorRate_ / 70.0));
        minLag_ = std::max(2, static_cast<int>(std::floor(detectorRate_ / 1000.0)));
        detectorRing_.assign(static_cast<size_t>(maxLag_ * 2 + 4), 0.0f);
        ordered_.resize(detectorRing_.size());
        difference_.resize(static_cast<size_t>(maxLag_ + 2));
        detectHop_ = std::max(1, static_cast<int>(std::round(detectorRate_ * 0.005)));
        lowpassCoefficient_ = static_cast<float>(1.0 - std::exp(-2.0 * pi * 2500.0 / sampleRate_));
        prepared_ = true;
        reset();
    }

    void reset() {
        if (prepared_) stretch_.reset();
        stretch_.setTransposeFactor(1.0f);
        stretch_.setFormantBase(static_cast<float>(200.0 / sampleRate_));
        for (auto& d : dryDelay_) std::fill(d.begin(), d.end(), 0.0f);
        std::fill(voiceDelay_.begin(), voiceDelay_.end(), 0.0f);
        std::fill(detectorRing_.begin(), detectorRing_.end(), 0.0f);
        delayIndex_ = detectorIndex_ = detectorFilled_ = decimationCounter_ = detectCounter_ = 0;
        lowpass1_ = lowpass2_ = shiftSemitones_ = voiceBlend_ = 0.0f;
        sustainedSeconds_ = 0.0;
        lastTargetNote_ = -1000;
        voiced_ = false;
        mixCurrent_ = mix_;
        mixStep_ = 0.0f;
        mixRampRemaining_ = 0;
        hasProcessed_ = false;
        detected_.store(0.0f, std::memory_order_relaxed);
        target_.store(0.0f, std::memory_order_relaxed);
        confidence_.store(0.0f, std::memory_order_relaxed);
    }

    // key: C=0..B=11. scale: chromatic=0, major=1, natural minor=2.
    // retuneMs is a time constant; humanize relaxes sustained notes; mix is dry/wet.
    // MIX automation uses a finite 5 ms ramp; initial settings/reset snap to target.
    void setParameters(int key0Cto11B, int scale0Chrom1Maj2Min,
                       float retuneMs, float humanize, float mix) noexcept {
        const int key = std::clamp(key0Cto11B, 0, 11);
        const int scale = std::clamp(scale0Chrom1Maj2Min, 0, 2);
        if (key != key_ || scale != scale_) lastTargetNote_ = -1000;
        key_ = key;
        scale_ = scale;
        retuneMs_ = clampFinite(retuneMs, 0.0f, 200.0f);
        humanize_ = clampFinite(humanize, 0.0f, 100.0f) * 0.01f;
        const float newMix = clampFinite(mix, 0.0f, 100.0f) * 0.01f;
        if (newMix != mix_) {
            mix_ = newMix;
            if (!hasProcessed_) {
                // Initial configuration/reset must retain exact delayed-dry startup.
                mixCurrent_ = mix_;
                mixStep_ = 0.0f;
                mixRampRemaining_ = 0;
            } else {
                mixRampRemaining_ = mixRampLength_;
                mixStep_ = (static_cast<double>(mix_) - mixCurrent_) / mixRampRemaining_;
            }
        }
    }

    void process(float* const* buffers, int channels, int samples) noexcept {
        if (!prepared_ || !buffers || samples <= 0 || channels <= 0) return;
        const int active = std::min(channels_, channels);
        for (int c = 0; c < active; ++c) if (!buffers[c]) return;
        hasProcessed_ = true;
        for (int offset = 0; offset < samples; offset += chunkSize) {
            const int count = std::min(chunkSize, samples - offset);
            double powers[2]{};
            for (int c = 0; c < channels_; ++c) {
                for (int n = 0; n < count; ++n) {
                    const float raw = c < active ? buffers[c][offset + n] : 0.0f;
                    const float v = std::isfinite(raw) ? raw : 0.0f;
                    input_[c][n] = v;
                    powers[c] += static_cast<double>(v) * v;
                }
                inputPointers_[c] = input_[c].data();
                outputPointers_[c] = wet_[c].data();
            }
            const int detectorChannel = active == 2 && powers[1] > powers[0] * 1.05 ? 1 : 0;
            for (int n = 0; n < count; ++n) {
                lowpass1_ += lowpassCoefficient_ * (input_[detectorChannel][n] - lowpass1_);
                lowpass2_ += lowpassCoefficient_ * (lowpass1_ - lowpass2_);
                if (++decimationCounter_ >= decimation_) {
                    decimationCounter_ = 0;
                    detectorRing_[detectorIndex_] = lowpass2_;
                    detectorIndex_ = (detectorIndex_ + 1) % static_cast<int>(detectorRing_.size());
                    detectorFilled_ = std::min(detectorFilled_ + 1, static_cast<int>(detectorRing_.size()));
                    if (++detectCounter_ >= detectHop_) {
                        detectCounter_ = 0;
                        analysePitch();
                    }
                }
            }
            stretch_.setTransposeSemitones(shiftSemitones_);
            const float pitch = detectedHz();
            if (pitch > 0.0f) stretch_.setFormantBase(pitch / static_cast<float>(sampleRate_));
            stretch_.process(inputPointers_.data(), count, outputPointers_.data(), count);
            const float blendStep = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate_ * 0.008)));
            for (int n = 0; n < count; ++n) {
                const float delayedVoice = voiceDelay_[delayIndex_];
                voiceDelay_[delayIndex_] = voiced_ ? 1.0f : 0.0f;
                voiceBlend_ += blendStep * (delayedVoice - voiceBlend_);
                if (mixRampRemaining_ > 0) {
                    mixCurrent_ += mixStep_;
                    if (--mixRampRemaining_ == 0) mixCurrent_ = mix_; // Exact 0/1 endpoints.
                }
                const float wetAmount = static_cast<float>(mixCurrent_) * voiceBlend_;
                for (int c = 0; c < channels_; ++c) {
                    const float dry = dryDelay_[c][delayIndex_];
                    dryDelay_[c][delayIndex_] = input_[c][n];
                    if (c < active) {
                        const float wet = std::isfinite(wet_[c][n]) ? wet_[c][n] : dry;
                        buffers[c][offset + n] = dry + wetAmount * (wet - dry);
                    }
                }
                if (++delayIndex_ >= latency_) delayIndex_ = 0;
            }
        }
    }

    int latencySamples() const noexcept { return latency_; }
    float detectedHz() const noexcept { return detected_.load(std::memory_order_relaxed); }
    float targetHz() const noexcept { return target_.load(std::memory_order_relaxed); }
    float confidence() const noexcept { return confidence_.load(std::memory_order_relaxed); }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr int chunkSize = 128;
    static float clampFinite(float v, float lo, float hi) noexcept {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : lo;
    }
    bool allowedNote(int note) const noexcept {
        if (scale_ == 0) return true;
        const int degree = ((note - key_) % 12 + 12) % 12;
        constexpr unsigned major = (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11);
        constexpr unsigned minor = (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10);
        return ((scale_ == 1 ? major : minor) & (1u << degree)) != 0;
    }
    int nearestNote(float midi) const noexcept {
        const int middle = static_cast<int>(std::floor(midi));
        int best = middle;
        float distance = 100.0f;
        for (int note = middle - 3; note <= middle + 3; ++note) {
            const float d = std::abs(static_cast<float>(note) - midi);
            if (allowedNote(note) && d < distance) { best = note; distance = d; }
        }
        if (lastTargetNote_ > -100 && allowedNote(lastTargetNote_) &&
            std::abs(static_cast<float>(lastTargetNote_) - midi) < distance + 0.12f)
            return lastTargetNote_;
        return best;
    }
    void unvoiced() noexcept {
        voiced_ = false;
        detected_.store(0.0f, std::memory_order_relaxed);
        target_.store(0.0f, std::memory_order_relaxed);
        confidence_.store(0.0f, std::memory_order_relaxed);
        sustainedSeconds_ = 0.0;
        lastTargetNote_ = -1000;
        // Keep the last shifter ratio for the end of the vowel still in its window.
        // The latency-aligned output gate returns unvoiced audio to the exact dry path.
    }
    void analysePitch() noexcept {
        if (detectorFilled_ < static_cast<int>(detectorRing_.size())) { unvoiced(); return; }
        double power = 0.0;
        for (int i = 0; i < static_cast<int>(ordered_.size()); ++i) {
            const float v = detectorRing_[(detectorIndex_ + i) % static_cast<int>(detectorRing_.size())];
            ordered_[i] = v;
            power += static_cast<double>(v) * v;
        }
        if (power / ordered_.size() < 0.000004) { unvoiced(); return; }
        difference_[0] = 1.0f;
        double sum = 0.0;
        for (int tau = 1; tau <= maxLag_ + 1; ++tau) {
            double d = 0.0;
            for (int i = 0; i < maxLag_; ++i) {
                const double delta = ordered_[i] - ordered_[i + tau];
                d += delta * delta;
            }
            sum += d;
            difference_[tau] = sum > 1.0e-20 ? static_cast<float>(d * tau / sum) : 1.0f;
        }
        int lag = -1;
        for (int tau = minLag_; tau <= maxLag_; ++tau) {
            if (difference_[tau] < 0.14f) {
                while (tau < maxLag_ && difference_[tau + 1] < difference_[tau]) ++tau;
                lag = tau;
                break;
            }
        }
        if (lag < 0 || lag >= maxLag_) { unvoiced(); return; }
        const float a = difference_[lag - 1], b = difference_[lag], c = difference_[lag + 1];
        const float denominator = a - 2.0f * b + c;
        const float adjustment = std::abs(denominator) > 1.0e-12f ?
            std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f) : 0.0f;
        const float hz = static_cast<float>(detectorRate_) / (lag + adjustment);
        if (hz < 70.0f || hz > 1000.0f) { unvoiced(); return; }
        const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
        const int note = nearestNote(midi);
        const double dt = detectHop_ / detectorRate_;
        sustainedSeconds_ = voiced_ && note == lastTargetNote_ ? sustainedSeconds_ + dt : 0.0;
        const float sustain = static_cast<float>(std::clamp((sustainedSeconds_ - 0.20) / 0.30, 0.0, 1.0));
        float desiredShift = std::clamp(static_cast<float>(note) - midi, -2.0f, 2.0f);
        const float deadband = 0.20f * humanize_ * sustain;
        desiredShift = std::copysign(std::max(0.0f, std::abs(desiredShift) - deadband), desiredShift);
        const double timeConstant = (retuneMs_ + 160.0f * humanize_ * sustain) * 0.001;
        const float alpha = timeConstant > 0.00001 ? static_cast<float>(1.0 - std::exp(-dt / timeConstant)) : 1.0f;
        shiftSemitones_ += alpha * (desiredShift - shiftSemitones_);
        lastTargetNote_ = note;
        voiced_ = true;
        detected_.store(hz, std::memory_order_relaxed);
        target_.store(440.0f * std::exp2((note - 69) / 12.0f), std::memory_order_relaxed);
        confidence_.store(std::clamp(1.0f - b, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch_{173};
    std::array<std::vector<float>, 2> dryDelay_;
    std::vector<float> voiceDelay_, detectorRing_, ordered_, difference_;
    std::array<std::array<float, chunkSize>, 2> input_{}, wet_{};
    std::array<float*, 2> inputPointers_{}, outputPointers_{};
    std::atomic<float> detected_{0.0f}, target_{0.0f}, confidence_{0.0f};
    double sampleRate_ = 48000.0, detectorRate_ = 12000.0, sustainedSeconds_ = 0.0;
    int channels_ = 1, latency_ = 0, key_ = 0, scale_ = 0, lastTargetNote_ = -1000;
    int delayIndex_ = 0, decimation_ = 4, decimationCounter_ = 0, detectorIndex_ = 0;
    int detectorFilled_ = 0, detectCounter_ = 0, detectHop_ = 60, minLag_ = 12, maxLag_ = 172;
    int mixRampLength_ = 240, mixRampRemaining_ = 0;
    float lowpassCoefficient_ = 0.25f, lowpass1_ = 0.0f, lowpass2_ = 0.0f;
    float shiftSemitones_ = 0.0f, retuneMs_ = 35.0f, humanize_ = 0.25f, mix_ = 1.0f, voiceBlend_ = 0.0f;
    double mixCurrent_ = 1.0, mixStep_ = 0.0;
    bool prepared_ = false, voiced_ = false, hasProcessed_ = false;
};

} // namespace gill
