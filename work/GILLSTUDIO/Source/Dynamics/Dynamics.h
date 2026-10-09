#pragma once
// Original causal dynamics primitives. No vendor algorithms or model claims.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gillstudio { namespace dynamics {
inline double bounded(double x, double lo, double hi, double fallback) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
inline double amplitudeToDb(double x) noexcept { return 20.0 * std::log10(std::max(1.e-8, std::abs(x))); }
inline double dbToAmplitude(double x) noexcept { return std::pow(10.0, x / 20.0); }
inline double coefficient(double ms, double sampleRate) noexcept {
    return ms <= 0.0 ? 0.0 : std::exp(-1000.0 / (ms * sampleRate));
}
enum class Law { DownwardCompression, UpwardCompression, DownwardExpansion, UpwardExpansion };
struct CurveParameters {
    Law law = Law::DownwardCompression;
    double thresholdDb = -18.0, ratio = 1.0, kneeDb = 0.0, rangeDb = 24.0;
};
inline CurveParameters sanitize(CurveParameters p) noexcept {
    if (static_cast<unsigned>(p.law) > 3) p.law = Law::DownwardCompression;
    p.thresholdDb = bounded(p.thresholdDb, -160, 24, -18);
    p.ratio = bounded(p.ratio, 1, 64, 1);
    p.kneeDb = bounded(p.kneeDb, 0, 48, 0);
    p.rangeDb = bounded(p.rangeDb, 0, 96, 24);
    return p;
}
// Integral of a linear knee transition; value and first derivative are continuous.
inline double softPositive(double x, double width) noexcept {
    if (width <= 0) return std::max(x, 0.0);
    if (x <= -0.5 * width) return 0;
    if (x >= 0.5 * width) return x;
    const double t = x + 0.5 * width;
    return t * t / (2.0 * width);
}
inline double gainDb(double inputDb, CurveParameters parameters) noexcept {
    const auto p = sanitize(parameters);
    const double over = bounded(inputDb, -160, 400, -160) - p.thresholdDb;
    const bool below = p.law == Law::UpwardCompression || p.law == Law::DownwardExpansion;
    const bool expansion = p.law == Law::DownwardExpansion || p.law == Law::UpwardExpansion;
    const bool positive = p.law == Law::UpwardCompression || p.law == Law::UpwardExpansion;
    const double slope = expansion ? p.ratio - 1 : 1 - 1 / p.ratio;
    const double amount = std::min(p.rangeDb, slope * softPositive(below ? -over : over, p.kneeDb));
    return positive ? amount : -amount;
}

struct EnvelopeParameters {
    // Attack/release are one-pole time constants: 63.2% travel in the named time.
    double attackMs = 10, holdMs = 0, releaseFastMs = 100, releaseSlowMs = 500;
    double adaptive = 0, averagingMs = 100;
};
inline EnvelopeParameters sanitize(EnvelopeParameters p) noexcept {
    p.attackMs = bounded(p.attackMs, 0, 5000, 10);
    p.holdMs = bounded(p.holdMs, 0, 5000, 0);
    p.releaseFastMs = bounded(p.releaseFastMs, 0, 10000, 100);
    p.releaseSlowMs = bounded(p.releaseSlowMs, 0, 10000, 500);
    p.adaptive = bounded(p.adaptive, 0, 1, 0);
    p.averagingMs = bounded(p.averagingMs, 0, 10000, 100);
    return p;
}
class GainEnvelope {
public:
    void prepare(double rate) noexcept { rate_ = bounded(rate, 8000, 768000, 48000); refresh(); reset(); }
    void reset() noexcept { gain_ = activity_ = 0; hold_ = 0; }
    void setParameters(EnvelopeParameters p) noexcept {
        p = sanitize(p);
        if (p.attackMs != p_.attackMs || p.holdMs != p_.holdMs || p.releaseFastMs != p_.releaseFastMs ||
            p.releaseSlowMs != p_.releaseSlowMs || p.adaptive != p_.adaptive || p.averagingMs != p_.averagingMs) {
            p_ = p; refresh();
        }
    }
    double process(double targetDb) noexcept {
        targetDb = bounded(targetDb, -96, 96, 0);
        activity_ = avg_ * activity_ + (1 - avg_) * std::abs(targetDb);
        if (std::abs(targetDb) >= std::abs(gain_) || targetDb * gain_ < 0) {
            gain_ = attack_ * gain_ + (1 - attack_) * targetDb;
            hold_ = holdSamples_;
        } else if (hold_ > 0) {
            --hold_;
        } else {
            // Slow memory depends on recent absolute requested gain, calibrated at 12 dB.
            const double blend = p_.adaptive * std::clamp(activity_ / 12.0, 0.0, 1.0);
            const double c = fast_ + blend * (slow_ - fast_);
            gain_ = c * gain_ + (1 - c) * targetDb;
        }
        if (std::abs(gain_) < 1.e-14) gain_ = 0;
        return gain_;
    }
    double valueDb() const noexcept { return gain_; }
    double activityDb() const noexcept { return activity_; }
private:
    void refresh() noexcept {
        attack_ = coefficient(p_.attackMs, rate_); fast_ = coefficient(p_.releaseFastMs, rate_);
        slow_ = coefficient(p_.releaseSlowMs, rate_); avg_ = coefficient(p_.averagingMs, rate_);
        holdSamples_ = static_cast<std::size_t>(std::llround(p_.holdMs * rate_ / 1000));
        hold_ = std::min(hold_, holdSamples_);
    }
    EnvelopeParameters p_{};
    double rate_ = 48000, gain_ = 0, activity_ = 0;
    double attack_ = coefficient(10,48000), fast_ = coefficient(100,48000), slow_ = coefficient(500,48000), avg_ = coefficient(100,48000);
    std::size_t hold_ = 0, holdSamples_ = 0;
};

enum class Detector { Peak, RMS };
enum class ChannelMode { LeftRight, MidSide, MidOnly, SideOnly };
enum class KeySource { Internal, External };
enum class KeyRoute { FollowChannels, Left, Right, Mid, Side };
struct Parameters {
    CurveParameters curve{};
    EnvelopeParameters envelope{};
    Detector detector = Detector::Peak;
    ChannelMode channels = ChannelMode::LeftRight;
    KeySource key = KeySource::Internal;
    KeyRoute keyRoute = KeyRoute::FollowChannels;
    double rmsMs = 10, stereoLink = 1, makeupDb = 0, mix = 1, rampMs = 5;
};
inline Parameters sanitize(Parameters p) noexcept {
    p.curve = sanitize(p.curve); p.envelope = sanitize(p.envelope);
    if (static_cast<unsigned>(p.detector) > 1) p.detector = Detector::Peak;
    if (static_cast<unsigned>(p.channels) > 3) p.channels = ChannelMode::LeftRight;
    if (static_cast<unsigned>(p.key) > 1) p.key = KeySource::Internal;
    if (static_cast<unsigned>(p.keyRoute) > 4) p.keyRoute = KeyRoute::FollowChannels;
    p.rmsMs = bounded(p.rmsMs, 0, 5000, 10); p.stereoLink = bounded(p.stereoLink, 0, 1, 1);
    p.makeupDb = bounded(p.makeupDb, -36, 36, 0); p.mix = bounded(p.mix, 0, 1, 1);
    p.rampMs = bounded(p.rampMs, 0, 1000, 5); return p;
}
inline bool equal(const Parameters& a, const Parameters& b) noexcept {
    return a.curve.law == b.curve.law && a.curve.thresholdDb == b.curve.thresholdDb &&
        a.curve.ratio == b.curve.ratio && a.curve.kneeDb == b.curve.kneeDb && a.curve.rangeDb == b.curve.rangeDb &&
        a.envelope.attackMs == b.envelope.attackMs && a.envelope.holdMs == b.envelope.holdMs &&
        a.envelope.releaseFastMs == b.envelope.releaseFastMs && a.envelope.releaseSlowMs == b.envelope.releaseSlowMs &&
        a.envelope.adaptive == b.envelope.adaptive && a.envelope.averagingMs == b.envelope.averagingMs &&
        a.detector == b.detector && a.channels == b.channels && a.key == b.key && a.keyRoute == b.keyRoute &&
        a.rmsMs == b.rmsMs && a.stereoLink == b.stereoLink && a.makeupDb == b.makeupDb &&
        a.mix == b.mix && a.rampMs == b.rampMs;
}
class Processor {
public:
    void prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::size_t channels) noexcept {
        rate_ = bounded(sampleRate, 8000, 768000, 48000); channels_ = channels >= 2 ? 2 : 1;
        for (auto& e : envelopes_) e.prepare(rate_);
        reset();
    }
    // Reset clears detector/envelope histories and snaps continuous parameters to target.
    void reset() noexcept {
        for (auto& e : envelopes_) e.reset();
        power_ = {}; gains_ = {}; current_ = target_; remaining_ = 0;
    }
    // Audio-thread API. Discrete switches apply on next sample; reset or crossfade graphs
    // externally for click-free topology changes. Continuous parameters have finite ramps.
    void setParameters(Parameters p) noexcept {
        p = sanitize(p);
        if (equal(p, target_)) return; // Repeated host block updates must not restart a ramp.
        target_ = p;
        remaining_ = static_cast<std::size_t>(std::llround(target_.rampMs * rate_ / 1000));
        if (!remaining_) current_ = target_;
    }
    const Parameters& parameters() const noexcept { return target_; }
    static constexpr int latencySamples() noexcept { return 0; }
    double gainReductionDb(std::size_t channel) const noexcept { return -gains_[channel > 0 ? 1 : 0]; }
    // External buffers are read-only, with frames samples each. Missing external key is
    // silence (never internal fallback). One key channel is dual mono before M/S encoding.
    // main/key may alias exactly, but partially overlapping sample ranges are unsupported.
    void process(float* const* audio, std::size_t frames, const float* const* external = nullptr,
                 std::size_t keyChannels = 0) noexcept {
        if (!audio || !audio[0] || (channels_ == 2 && !audio[1])) return;
        constexpr double norm = 0.7071067811865475244;
        for (std::size_t n = 0; n < frames; ++n) {
            advance();
            const auto& p = current_;
            const double left = clean(audio[0][n]), right = channels_ == 2 ? clean(audio[1][n]) : left;
            double kl = left, kr = right;
            if (p.key == KeySource::External) {
                kl = external && keyChannels && external[0] ? clean(external[0][n]) : 0;
                kr = keyChannels >= 2 ? (external && external[1] ? clean(external[1][n]) : 0) : kl;
            }
            const bool ms = channels_ == 2 && p.channels != ChannelMode::LeftRight;
            std::array<double,2> signal = ms ? std::array<double,2>{(left+right)*norm,(left-right)*norm}
                                              : std::array<double,2>{left,right};
            std::array<double,2> key = ms ? std::array<double,2>{(kl+kr)*norm,(kl-kr)*norm}
                                           : std::array<double,2>{kl,kr};
            switch (p.keyRoute) {
                case KeyRoute::Left: key = {kl,kl}; break;
                case KeyRoute::Right: key = {kr,kr}; break;
                case KeyRoute::Mid: key = {(kl+kr)*norm,(kl+kr)*norm}; break;
                case KeyRoute::Side: key = {(kl-kr)*norm,(kl-kr)*norm}; break;
                default: break;
            }
            const double rmsCoeff = coefficient(p.rmsMs,rate_);
            for (unsigned ch=0; ch<2; ++ch) {
                power_[ch] = rmsCoeff * power_[ch] + (1-rmsCoeff) * key[ch]*key[ch];
                if (power_[ch] < 1.e-30) power_[ch] = 0;
                key[ch] = p.detector == Detector::RMS ? std::sqrt(power_[ch]) : std::abs(key[ch]);
            }
            const double maximum = std::max(key[0],key[1]);
            for (unsigned ch=0; ch<2; ++ch) {
                key[ch] += p.stereoLink * (maximum-key[ch]);
                envelopes_[ch].setParameters(p.envelope);
                const bool selected = !ms || (p.channels != ChannelMode::MidOnly && p.channels != ChannelMode::SideOnly) ||
                                      (p.channels == ChannelMode::MidOnly ? ch == 0 : ch == 1);
                const double g = envelopes_[ch].process(gainDb(amplitudeToDb(key[ch]),p.curve));
                gains_[ch] = selected ? g : 0;
                if (selected) signal[ch] *= dbToAmplitude(g+p.makeupDb);
            }
            double wetL = signal[0], wetR = signal[1];
            if (ms) { wetL = (signal[0]+signal[1])*norm; wetR = (signal[0]-signal[1])*norm; }
            // Exact identity endpoints, including an M/S matrix round trip.
            if (p.mix == 0 || (gains_[0] == 0 && gains_[1] == 0 && p.makeupDb == 0)) {
                audio[0][n] = static_cast<float>(left);
                if (channels_ == 2) audio[1][n] = static_cast<float>(right);
            } else {
                audio[0][n] = representable(left+p.mix*(wetL-left));
                if (channels_ == 2) audio[1][n] = representable(right+p.mix*(wetR-right));
            }
        }
    }
private:
    static double clean(float x) noexcept { return std::isfinite(x) ? x : 0; }
    // Numeric overflow protection only, at float's representable limit; no audio ceiling.
    static float representable(double x) noexcept {
        constexpr double max = std::numeric_limits<float>::max();
        return static_cast<float>(std::clamp(x,-max,max));
    }
    void advance() noexcept {
        current_.curve.law = target_.curve.law; current_.detector = target_.detector;
        current_.channels = target_.channels; current_.key = target_.key; current_.keyRoute = target_.keyRoute;
        if (!remaining_) return;
        const double fraction = 1.0 / static_cast<double>(remaining_);
        auto step = [fraction](double& a,double b) { a += (b-a)*fraction; };
        step(current_.curve.thresholdDb,target_.curve.thresholdDb); step(current_.curve.ratio,target_.curve.ratio);
        step(current_.curve.kneeDb,target_.curve.kneeDb); step(current_.curve.rangeDb,target_.curve.rangeDb);
        step(current_.envelope.attackMs,target_.envelope.attackMs); step(current_.envelope.holdMs,target_.envelope.holdMs);
        step(current_.envelope.releaseFastMs,target_.envelope.releaseFastMs); step(current_.envelope.releaseSlowMs,target_.envelope.releaseSlowMs);
        step(current_.envelope.adaptive,target_.envelope.adaptive); step(current_.envelope.averagingMs,target_.envelope.averagingMs);
        step(current_.rmsMs,target_.rmsMs); step(current_.stereoLink,target_.stereoLink);
        step(current_.makeupDb,target_.makeupDb); step(current_.mix,target_.mix);
        if (--remaining_ == 0) current_ = target_;
    }
    double rate_ = 48000;
    std::size_t channels_ = 2, remaining_ = 0;
    Parameters current_{}, target_{};
    std::array<GainEnvelope,2> envelopes_{};
    std::array<double,2> power_{}, gains_{};
};
}} // namespace gillstudio::dynamics
