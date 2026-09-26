#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gill::tools {

struct RescueParameters {
    float repair = 100.0f;
    float clipDb = 0.0f;
    float negativeClipDb = 0.0f;
    float maxRepairDb = 6.0f;
    float outputDb = -6.0f;
    bool listenRepairs = false;
    bool bypass = false;
};

// Short hard-clipping repair. Intact samples are never fitted or filtered.
// A causal delay supplies the right boundary for a bounded Hermite estimate.
// This estimates missing peaks; it cannot recover arbitrary lost audio.
class RescueDSP {
public:
    static constexpr size_t scopeSize = 256;
    struct Scope { std::array<float,scopeSize> before {},after {}; };
    RescueDSP() noexcept { for(auto& x:scopeBefore_)x.store(0);for(auto& x:scopeAfter_)x.store(0); }
    Scope scope() const noexcept {
        Scope result;const auto head=scopeHead_.load(std::memory_order_acquire);
        for(size_t i=0;i<scopeSize;++i){const auto at=(head+i)%scopeSize;result.before[i]=scopeBefore_[at].load(std::memory_order_relaxed);result.after[i]=scopeAfter_[at].load(std::memory_order_relaxed);}return result;
    }
    void prepare(double rate, int, int channels) {
        rate_ = std::isfinite(rate) && rate >= 8000 && rate <= 192000 ? rate : 48000;
        channels_ = std::clamp(channels, 1, 2);
        liveLatency_ = std::max(32, static_cast<int>(std::ceil(rate_ * .004)));
        proLatency_ = std::max(64, static_cast<int>(std::ceil(rate_ * .012)));
        capacity_ = proLatency_ * 4 + 256;
        for (auto& channel : channel_) {
            channel.raw.assign(static_cast<size_t>(capacity_), 0);
            channel.repaired.assign(static_cast<size_t>(capacity_), 0);
        }
        gainStep_ = 1.0 / std::max(1.0, rate_ * .01);
        scopeStep_=std::max(1,int(rate_/800));
        reset();
    }

    void reset() noexcept {
        clock_ = 0;
        for (auto& channel : channel_) {
            std::fill(channel.raw.begin(), channel.raw.end(), 0.0f);
            std::fill(channel.repaired.begin(), channel.repaired.end(), 0.0f);
            channel.plateauStart = -1;
            channel.pending.fill({});
        }
        repairCurrent_ = std::clamp(clean(parameters_.repair) * .01, 0.0, 1.0);
        gainCurrent_ = gain(parameters_.outputDb);
        bypassCurrent_ = parameters_.bypass ? 1.0 : 0.0;
        deltaCurrent_ = parameters_.listenRepairs ? 1.0 : 0.0;
        repairs_.store(0);
        rejected_.store(0);
        inputPeak_.store(0);
        outputPeak_.store(0);
        learnSamples_ = 0;
        elapsedView_=0;learnRequest_=0;
        learning_ = false;
        clearLearn();
        for(auto& x:scopeBefore_)x.store(0);for(auto& x:scopeAfter_)x.store(0);scopeHead_.store(0);scopeCounter_=0;
    }

    void setLiveMode(bool live) noexcept {
        if (live_ == live) return;
        live_ = live;
        // Do not allow a repair detected under the old delay budget to write
        // samples that already left the new, shorter audio path.
        for (auto& channel : channel_) {
            channel.plateauStart = -1;
            channel.pending.fill({});
        }
    }
    int latencySamples() const noexcept { return live_ ? liveLatency_ : proLatency_; }
    int maximumLatencySamples() const noexcept { return proLatency_; }
    void setParameters(const RescueParameters& parameters) noexcept { parameters_ = parameters; }
    void requestLearn() noexcept { learnRequest_.store(1, std::memory_order_release); }
    void requestFinish() noexcept { learnRequest_.store(2, std::memory_order_release); }
    void startLearning(bool = true) noexcept {clearLearn();learning_=true;learnSamples_=0;elapsedView_=0;learningView_=true;}
    void finishLearning() noexcept {if(learning_)finishLearn();}
    void cancelLearning() noexcept {learning_=false;learnSamples_=0;elapsedView_=0;learningView_=false;clearLearn();}
    int learningState() const noexcept {return learning_?1:learnRevision_.load()>0?(learnedPositive_.load()<-24&&learnedNegative_.load()<-24?3:2):0;}
    float learningSeconds() const noexcept {return elapsedView_.load(std::memory_order_relaxed);}
    bool isLearning() const noexcept { return learningView_.load(std::memory_order_relaxed); }
    float learnedPositiveDb() const noexcept { return learnedPositive_.load(std::memory_order_relaxed); }
    float learnedNegativeDb() const noexcept { return learnedNegative_.load(std::memory_order_relaxed); }
    unsigned learnRevision() const noexcept { return learnRevision_.load(std::memory_order_acquire); }
    unsigned repairs() const noexcept { return repairs_.load(std::memory_order_relaxed); }
    unsigned rejected() const noexcept { return rejected_.load(std::memory_order_relaxed); }
    float inputPeak() const noexcept { return inputPeak_.load(std::memory_order_relaxed); }
    float outputPeak() const noexcept { return outputPeak_.load(std::memory_order_relaxed); }

    void process(float* const* audio, int channels, int frames) noexcept {
        if (!audio || frames <= 0 || capacity_ == 0) return;
        const int active = std::min(channels_, channels);
        if (active <= 0) return;
        for (int c = 0; c < active; ++c) if (!audio[c]) return;
        const int learnCommand=learnRequest_.exchange(0,std::memory_order_acq_rel);
        if(learnCommand==1)startLearning();else if(learnCommand==2)finishLearning();
        const double repairTarget = std::clamp(clean(parameters_.repair) * .01, 0.0, 1.0);
        const double outputTarget = gain(std::clamp(clean(parameters_.outputDb), -18.0, 0.0));
        const double positive = gain(std::clamp(clean(parameters_.clipDb), -24.0, 0.0));
        const double negative = gain(std::clamp(clean(parameters_.negativeClipDb), -24.0, 0.0));
        const double boost = gain(std::clamp(clean(parameters_.maxRepairDb), 0.0, 12.0));
        double inPeak = 0, outPeak = 0;
        for (int n = 0; n < frames; ++n, ++clock_) {
            slew(repairCurrent_, repairTarget, gainStep_);
            slew(gainCurrent_, outputTarget, gainStep_);
            slew(bypassCurrent_, parameters_.bypass ? 1.0 : 0.0, gainStep_);
            slew(deltaCurrent_, parameters_.listenRepairs ? 1.0 : 0.0, gainStep_);
            for (int c = 0; c < active; ++c) {
                auto& channel = channel_[c];
                const float input = static_cast<float>(std::clamp(clean(audio[c][n]), -32.0, 32.0));
                const auto index = slot(clock_);
                channel.raw[index] = channel.repaired[index] = input;
                detect(channel, positive, negative);
                for (auto& pending : channel.pending) {
                    if (pending.start < 0 || clock_ < pending.end + 3) continue;
                    repair(channel, pending, boost);
                    pending = {};
                }
                const auto outputAt = clock_ - latencySamples();
                const double dry = outputAt >= 0 ? channel.raw[slot(outputAt)] : 0;
                const double restored = outputAt >= 0 ? channel.repaired[slot(outputAt)] : 0;
                const double difference = repairCurrent_ * (restored - dry);
                const double wet = (dry * (1.0 - deltaCurrent_) + difference) * gainCurrent_;
                const double result = wet + bypassCurrent_ * (dry - wet);
                if(c==0&&++scopeCounter_>=scopeStep_){scopeCounter_=0;const auto head=scopeHead_.load(std::memory_order_relaxed);scopeBefore_[head].store(float(dry),std::memory_order_relaxed);scopeAfter_[head].store(float(dry+difference),std::memory_order_relaxed);scopeHead_.store((head+1)%scopeSize,std::memory_order_release);}
                audio[c][n] = static_cast<float>(result);
                inPeak = std::max(inPeak, std::abs(static_cast<double>(input)));
                outPeak = std::max(outPeak, std::abs(result));
            }
            if (learning_ && ++learnSamples_ >= static_cast<std::int64_t>(rate_ * 300.0)) finishLearn();
        }
        inputPeak_.store(static_cast<float>(inPeak), std::memory_order_relaxed);
        outputPeak_.store(static_cast<float>(outPeak), std::memory_order_relaxed);
        elapsedView_.store(static_cast<float>(learnSamples_/rate_),std::memory_order_relaxed);
    }

private:
    struct Pending {
        std::int64_t start = -1, end = -1;
        float level = 0;
        bool allowed = false;
    };
    struct Channel {
        std::vector<float> raw, repaired;
        std::int64_t plateauStart = -1;
        float plateauValue = 0;
        std::array<Pending, 8> pending {};
    };
    struct LearnBin { double sum = 0; unsigned count = 0; };
    static double clean(double value) noexcept { return std::isfinite(value) ? value : 0; }
    static double gain(double db) noexcept { return std::pow(10.0, clean(db) / 20.0); }
    static void slew(double& value, double target, double step) noexcept {
        value += std::clamp(target - value, -step, step);
    }
    size_t slot(std::int64_t index) const noexcept { return static_cast<size_t>(index % capacity_); }
    float raw(const Channel& channel, std::int64_t index) const noexcept {
        return index >= 0 ? channel.raw[slot(index)] : 0;
    }
    void detect(Channel& channel, double positive, double negative) noexcept {
        if (clock_ < 4) return;
        const float current = raw(channel, clock_);
        const float previous = raw(channel, clock_ - 1);
        const double tolerance = std::max(1e-8, std::abs(static_cast<double>(previous)) * 2e-6);
        // Exact flat samples distinguish digital hard clipping from a clean
        // low-frequency peak. Near-flat high-resolution audio is left alone.
        const bool equal = current == previous && std::abs(previous) >= .04;
        if (channel.plateauStart < 0) {
            if (equal) {
                channel.plateauStart = clock_ - 1;
                channel.plateauValue = previous;
            }
            return;
        }
        if (equal && current == channel.plateauValue) return;
        const auto start = channel.plateauStart;
        const auto end = clock_ - 1;
        const double plateau = std::abs(static_cast<double>(channel.plateauValue));
        const double threshold = channel.plateauValue >= 0 ? positive : negative;
        const int length = static_cast<int>(end - start + 1);
        const double sign = channel.plateauValue >= 0 ? 1.0 : -1.0;
        const bool peak = sign * raw(channel, start - 1) < plateau - tolerance
                       && sign * current < plateau - tolerance;
        if (peak && learning_ && length >= 3) recordLearn(plateau, sign > 0);
        if (peak && length >= 3 && start >= 3) {
            bool placed = false;
            for (auto& pending : channel.pending) if (pending.start < 0) {
                pending = {start, end, channel.plateauValue,
                           std::abs(plateau - threshold) <= threshold * .005};
                placed = true; break;
            }
            if (!placed) rejected_.fetch_add(1, std::memory_order_relaxed);
        }
        channel.plateauStart = -1;
    }
    void repair(Channel& channel, const Pending& pending, double boost) noexcept {
        if (!pending.allowed) return;
        const auto a = pending.start - 1, b = pending.end + 1;
        if (clock_ - pending.start >= latencySamples() || b - a > latencySamples() - 8) {
            rejected_.fetch_add(1, std::memory_order_relaxed); return;
        }
        const double sign = pending.level >= 0 ? 1.0 : -1.0;
        const double level = std::abs(static_cast<double>(pending.level));
        const double y0 = sign * raw(channel, a), y1 = sign * raw(channel, b);
        const double m0 = sign * (3.0 * raw(channel, a) - 4.0 * raw(channel, a - 1) + raw(channel, a - 2)) * .5;
        const double m1 = sign * (-3.0 * raw(channel, b) + 4.0 * raw(channel, b + 1) - raw(channel, b + 2)) * .5;
        if (!(m0 > 0 && m1 < 0) || y0 < level * .15 || y1 < level * .15) {
            rejected_.fetch_add(1, std::memory_order_relaxed); return;
        }
        const double distance = static_cast<double>(b - a);
        const double ceiling = level * boost;
        bool changed = false;
        for (auto at = pending.start; at <= pending.end; ++at) {
            const double t = static_cast<double>(at - a) / distance, t2 = t * t, t3 = t2 * t;
            const double estimate = (2*t3-3*t2+1)*y0 + (t3-2*t2+t)*distance*m0
                                  + (-2*t3+3*t2)*y1 + (t3-t2)*distance*m1;
            const double original = sign * raw(channel, at);
            // Boundary slopes cannot uniquely determine the missing waveform.
            // Shrink the estimate toward the observed plateau, limiting the
            // harm from a cubic model on complex, rapidly changing harmonics.
            const double candidate = original + .5 * (estimate - original);
            const double replacement = std::clamp(candidate, original, std::max(original, ceiling));
            channel.repaired[slot(at)] = static_cast<float>(sign * replacement);
            changed = changed || replacement > original + 1e-8;
        }
        if (changed) repairs_.fetch_add(1, std::memory_order_relaxed);
    }
    void clearLearn() noexcept {
        for (auto& side : learned_) for (auto& bin : side) bin = {};
        learningView_.store(false);
    }
    void recordLearn(double level, bool positive) noexcept {
        const double db = 20 * std::log10(level);
        if (db < -24 || db > .05) return;
        const auto index = static_cast<size_t>(std::clamp(static_cast<int>((db + 24) * 20), 0, 480));
        auto& bin = learned_[positive ? 0 : 1][index];
        bin.sum += level; ++bin.count;
    }
    void finishLearn() noexcept {
        for (int side = 0; side < 2; ++side) {
            const LearnBin* best = nullptr;
            unsigned total = 0;
            for (const auto& bin : learned_[side]) {
                total += bin.count;
                if (!best || bin.count > best->count) best = &bin;
            }
            float db = -100;
            if (best && best->count >= 6 && best->count * 3 >= total * 2)
                db = static_cast<float>(20 * std::log10(best->sum / best->count));
            (side == 0 ? learnedPositive_ : learnedNegative_).store(db, std::memory_order_relaxed);
        }
        learning_ = false; learningView_.store(false);
        learnRevision_.fetch_add(1, std::memory_order_release);
    }
    std::array<Channel, 2> channel_;
    std::array<std::atomic<float>,scopeSize> scopeBefore_ {},scopeAfter_ {};
    std::atomic<size_t> scopeHead_ {0};
    int scopeCounter_=0,scopeStep_=60;
    std::array<std::array<LearnBin, 481>, 2> learned_ {};
    RescueParameters parameters_;
    double rate_ = 48000, gainStep_ = 1.0/480, repairCurrent_ = 1, gainCurrent_ = .501187, bypassCurrent_ = 0, deltaCurrent_ = 0;
    int channels_ = 2, liveLatency_ = 192, proLatency_ = 576, capacity_ = 0;
    std::int64_t clock_ = 0, learnSamples_ = 0;
    bool live_ = false, learning_ = false;
    std::atomic<int> learnRequest_ {0};std::atomic<bool> learningView_ {false};
    std::atomic<float> elapsedView_ {0};
    std::atomic<unsigned> repairs_ {0}, rejected_ {0}, learnRevision_ {0};
    std::atomic<float> inputPeak_ {0}, outputPeak_ {0}, learnedPositive_ {-100}, learnedNegative_ {-100};
};
}
