#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <cstdint>
#include "TuneLiveDSP.h"

namespace gill {

// Monophonic vocal correction. prepare/reset/setParameters/process belong to the
// audio thread (prepare while stopped); meter getters may be read by the UI.
// Supported layouts: mono/stereo. Dry, wet and unvoiced paths share fixed latency.
class TuneDSP {
public:
    // Fixed per instance. Call before prepare, never to switch latency mid-stream.
    void setQualityMode(int mode) noexcept { requestedQuality_=std::clamp(mode,0,1); }
    int qualityMode() const noexcept { return quality_; }
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
        quality_=requestedQuality_;
        (void)maxBlock; // Sample-based control is independent of host block sizes.
        int spectralSize = 256;
        while (spectralSize < sampleRate_ * 0.042) spectralSize *= 2;
        // STUDIO preserves the original reported delay for existing projects,
        // and uses a 64-tap interpolator; LIVE uses 24 taps and fixed 16 ms.
        // Both now use the phase-coherent monophonic engine, not a phase vocoder.
        latency_=quality_==0?spectralSize:static_cast<int>(std::ceil(sampleRate_*.016));
        live_.prepare(sampleRate_,channels_,latency_,quality_==0);
        for (auto& d : dryDelay_) d.assign(static_cast<size_t>(latency_), 0.0f);
        int controlLength=1024;while(controlLength<sampleRate_*.20)controlLength*=2;controlHistory_.assign(controlLength,Control{});controlMask_=controlLength-1;
        decimation_ = std::max(1, static_cast<int>(std::round(sampleRate_ / 12000.0)));
        detectorRate_ = sampleRate_ / decimation_;
        maxLag_ = static_cast<int>(std::ceil(detectorRate_ / 70.0));
        minLag_ = std::max(2, static_cast<int>(std::floor(detectorRate_ / 1000.0)));
        detectorRing_.assign(static_cast<size_t>(maxLag_ * 2 + 32), 0.0f);
        ordered_.resize(detectorRing_.size());
        difference_.resize(static_cast<size_t>(maxLag_ + 2));
        detectHop_ = std::max(1, static_cast<int>(std::round(detectorRate_ * 0.005)));
        lowpassCoefficient_ = static_cast<float>(1.0 - std::exp(-2.0 * pi * 2500.0 / sampleRate_));
        prepared_ = true;
        reset();
    }

    void reset() {
        live_.reset();
        for (auto& d : dryDelay_) std::fill(d.begin(), d.end(), 0.0f);
        std::fill(controlHistory_.begin(),controlHistory_.end(),Control{});sampleClock_=0;lastControlTime_=-1;lastControl_={};detectorPower_.fill(0);detectorChannel_=0;
        std::fill(detectorRing_.begin(), detectorRing_.end(), 0.0f);
        delayIndex_ = detectorIndex_ = detectorFilled_ = decimationCounter_ = detectCounter_ = 0;
        lowpass1_ = lowpass2_ = shiftSemitones_ = 0.0f;voiceBlend_=0;
        correctionActive_=false;unitySamples_=0;pendingVoiceFrames_=0;pendingVoiceHz_=0;
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
        const float detectorDecay=static_cast<float>(std::exp(-1/(sampleRate_*.02)));
        const double blendStep=1/(sampleRate_*.003);
        for (int n=0;n<samples;++n) {
            std::array<float,2> current{},liveOutput{};
            for(int c=0;c<channels_;++c){const float raw=c<active?buffers[c][n]:0;current[c]=std::isfinite(raw)?std::clamp(raw,-32.f,32.f):0;detectorPower_[c]=detectorDecay*detectorPower_[c]+(1-detectorDecay)*current[c]*current[c];}
            if(active==2){const int other=1-detectorChannel_;if(detectorPower_[other]>detectorPower_[detectorChannel_]*2)detectorChannel_=other;}else detectorChannel_=0;
                lowpass1_ += lowpassCoefficient_ * (current[detectorChannel_] - lowpass1_);
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
            // Detector results carry the centre time of the samples measured.
            // Pitch control is read at the delayed audio position, not at the
            // newest input sample or an arbitrary host-chunk boundary.
            const auto gate=controlAt(sampleClock_-latency_);
            // A nearly in-tune noisy vowel may cross 0.5 cent hundreds of times
            // per second. Do not repeatedly mix two differently phased paths.
            // A settled near-unity note still returns to the exact dry route.
            if(gate.voice<=.5f){correctionActive_=false;unitySamples_=0;}
            else if(!correctionActive_){if(std::abs(gate.shift)>.005f)correctionActive_=true;}
            else if(std::abs(gate.shift)<.002f){
                if(++unitySamples_>=static_cast<int>(sampleRate_*.080))correctionActive_=false;
            }else unitySamples_=0;
            const bool wantsCorrection=correctionActive_;
            voiceBlend_+=std::clamp((wantsCorrection?1.:0.)-voiceBlend_,-blendStep,blendStep);
            live_.processSample(current,liveOutput,gate.hz,gate.shift,voiceBlend_>0);
                if (mixRampRemaining_ > 0) {
                    mixCurrent_ += mixStep_;
                    if (--mixRampRemaining_ == 0) mixCurrent_ = mix_; // Exact 0/1 endpoints.
                }
                const float wetAmount = static_cast<float>(mixCurrent_*voiceBlend_);
                for (int c = 0; c < channels_; ++c) {
                    const float dry = dryDelay_[c][delayIndex_];
                    dryDelay_[c][delayIndex_] = current[c];
                    if (c < active) {
                        const float wet = std::isfinite(liveOutput[c]) ? std::clamp(liveOutput[c],-32.f,32.f) : dry;
                        buffers[c][n] = wetAmount==0?dry:wetAmount==1?wet:dry + wetAmount * (wet - dry);
                    }
                }
                if (++delayIndex_ >= latency_) delayIndex_ = 0;
                ++sampleClock_;
        }
    }

    int latencySamples() const noexcept { return latency_; }
    float detectedHz() const noexcept { return detected_.load(std::memory_order_relaxed); }
    float targetHz() const noexcept { return target_.load(std::memory_order_relaxed); }
    float confidence() const noexcept { return confidence_.load(std::memory_order_relaxed); }

private:
    struct Control{float shift=0,hz=0,voice=0;};
    Control controlAt(std::int64_t when)const noexcept{return when<0?Control{}:when>=lastControlTime_?lastControl_:controlHistory_[static_cast<int>(when)&controlMask_];}
    void publishControl(double lag,int comparison)noexcept{
        const double centreDelay=(comparison-1+lag)*decimation_*.5+2*(1-lowpassCoefficient_)/lowpassCoefficient_;
        const auto when=std::max(lastControlTime_+1,sampleClock_-static_cast<std::int64_t>(std::lround(centreDelay)));
        if(when<0)return;const Control next{shiftSemitones_,detectedHz(),voiced_?1.f:0.f};
        const auto first=std::max<std::int64_t>(0,lastControlTime_+1);const auto distance=std::max<std::int64_t>(1,when-lastControlTime_);
        for(auto t=first;t<=when;++t){const float p=static_cast<float>(t-lastControlTime_)/static_cast<float>(distance);auto& c=controlHistory_[static_cast<int>(t)&controlMask_];c.voice=p<.5f?lastControl_.voice:next.voice;
            if(lastControl_.voice>.5f&&next.voice>.5f){c.shift=lastControl_.shift+p*(next.shift-lastControl_.shift);c.hz=lastControl_.hz+p*(next.hz-lastControl_.hz);}else{c.shift=next.shift;c.hz=next.hz;}}
        lastControlTime_=when;lastControl_=next;
    }
    static constexpr double pi = 3.14159265358979323846;
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
    void unvoiced(int comparison=0) noexcept {
        voiced_ = false;
        detected_.store(0.0f, std::memory_order_relaxed);
        target_.store(0.0f, std::memory_order_relaxed);
        confidence_.store(0.0f, std::memory_order_relaxed);
        sustainedSeconds_ = 0.0;
        lastTargetNote_ = -1000;
        pendingVoiceFrames_=0;pendingVoiceHz_=0;
        publishControl(comparison>0?comparison:maxLag_,comparison>0?comparison:maxLag_);
        // Keep the last shifter ratio for the end of the vowel still in its window.
        // The latency-aligned output gate returns unvoiced audio to the exact dry path.
    }
    void analysePitch() noexcept {
        const int comparison=std::min(maxLag_,detectorFilled_/2-10);
        if(comparison<=minLag_+1){unvoiced();return;}
        double power = 0.0;
        for (int i = 0; i < static_cast<int>(ordered_.size()); ++i) {
            const float v = detectorRing_[(detectorIndex_+static_cast<int>(detectorRing_.size())-1-i) % static_cast<int>(detectorRing_.size())];
            ordered_[i] = v;
            if(i<comparison*2)power += static_cast<double>(v) * v;
        }
        if (power / (comparison*2) < 0.000004) { unvoiced(comparison); return; }
        difference_[0] = 1.0f;
        double sum = 0.0;
        for (int tau = 1; tau <= comparison + 1; ++tau) {
            double d = 0.0;
            for (int i = 0; i < comparison; ++i) {
                const double delta = ordered_[i] - ordered_[i + tau];
                d += delta * delta;
            }
            sum += d;
            difference_[tau] = sum > 1.0e-20 ? static_cast<float>(d * tau / sum) : 1.0f;
        }
        int lag = -1;
        for (int tau = minLag_; tau <= comparison; ++tau) {
            if (difference_[tau] < 0.14f) {
                while (tau < comparison && difference_[tau + 1] < difference_[tau]) ++tau;
                lag = tau;
                break;
            }
        }
        if (lag < 0 || lag >= comparison) { unvoiced(comparison); return; }
        const float a = difference_[lag - 1], b = difference_[lag], c = difference_[lag + 1];
        const float denominator = a - 2.0f * b + c;
        const float adjustment = std::abs(denominator) > 1.0e-12f ?
            std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f) : 0.0f;
        double refined=lag+adjustment;
        // Refine fractional periods with a band-limited waveform comparison.
        // A parabola through integer YIN bins alone biases short/high-note lags.
        for(double step:{.2,.05,.01}){const double a0=periodError(refined-step,comparison),b0=periodError(refined,comparison),c0=periodError(refined+step,comparison);const double curvature=a0-2*b0+c0;if(curvature>1e-18)refined+=std::clamp(.5*(a0-c0)/curvature,-1.,1.)*step;}
        const float hz = static_cast<float>(detectorRate_/refined);
        if (hz < 70.0f || hz > 1000.0f) { unvoiced(comparison); return; }
        // Clean periodic vowels enter immediately. Borderline candidates need
        // consecutive, consistent evidence; once voiced the existing threshold
        // remains unchanged. This rejects isolated noise detections without an
        // amplitude gate, preserving breaths/consonants on the delayed dry path.
        if(!voiced_ && b>.06f){
            const bool consistent=pendingVoiceHz_>0 && std::abs(12.f*std::log2(hz/pendingVoiceHz_))<.8f;
            const int evidence=consistent?pendingVoiceFrames_+1:1;
            if(b>.10f||evidence<2){
                unvoiced(comparison);
                pendingVoiceHz_=hz;pendingVoiceFrames_=evidence;
                return;
            }
        }
        pendingVoiceFrames_=0;pendingVoiceHz_=0;
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
        publishControl(refined,comparison);
    }

    double periodError(double lag,int length)const noexcept{
        constexpr int taps=16;const int whole=static_cast<int>(std::floor(lag));const double fraction=lag-whole;std::array<double,taps> kernel{};double sum=0;
        for(int k=0;k<taps;++k){const double x=k-(taps/2-1)-fraction;const double sinc=std::abs(x)<1e-12?1:std::sin(pi*x)/(pi*x);const double w=std::abs(x)<taps*.5?.42+.5*std::cos(pi*x/(taps*.5))+.08*std::cos(2*pi*x/(taps*.5)):0;kernel[k]=sinc*w;sum+=kernel[k];}
        for(auto& v:kernel)v/=sum;double error=0;for(int i=0;i<length;++i){double shifted=0;for(int k=0;k<taps;++k)shifted+=ordered_[i+whole+k-(taps/2-1)]*kernel[k];const double delta=ordered_[i]-shifted;error+=delta*delta;}return error;
    }

    TuneLiveDSP live_;
    std::array<std::vector<float>, 2> dryDelay_;
    std::vector<float> detectorRing_, ordered_, difference_;std::vector<Control> controlHistory_;
    std::atomic<float> detected_{0.0f}, target_{0.0f}, confidence_{0.0f};
    double sampleRate_ = 48000.0, detectorRate_ = 12000.0, sustainedSeconds_ = 0.0;
    int channels_ = 1, latency_ = 0, key_ = 0, scale_ = 0, lastTargetNote_ = -1000;
    int delayIndex_ = 0, decimation_ = 4, decimationCounter_ = 0, detectorIndex_ = 0;
    int detectorFilled_ = 0, detectCounter_ = 0, detectHop_ = 60, minLag_ = 12, maxLag_ = 172;
    int mixRampLength_ = 240, mixRampRemaining_ = 0;
    float lowpassCoefficient_ = 0.25f, lowpass1_ = 0.0f, lowpass2_ = 0.0f;
    float shiftSemitones_ = 0.0f, retuneMs_ = 35.0f, humanize_ = 0.25f, mix_ = 1.0f;
    double voiceBlend_=0;std::array<float,2> detectorPower_{};int detectorChannel_=0,requestedQuality_=0,quality_=0,controlMask_=1023;
    std::int64_t sampleClock_=0,lastControlTime_=-1;Control lastControl_{};
    double mixCurrent_ = 1.0, mixStep_ = 0.0;
    bool correctionActive_=false;int unitySamples_=0,pendingVoiceFrames_=0;float pendingVoiceHz_=0;
    bool prepared_ = false, voiced_ = false, hasProcessed_ = false;
};

} // namespace gill
