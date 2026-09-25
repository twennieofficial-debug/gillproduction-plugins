#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace gill {
namespace air_detail {
constexpr double pi = 3.1415926535897932384626433832795;
inline double finite(double x, double fallback = 0.0) noexcept { return std::isfinite(x) ? x : fallback; }
inline double quiet(double x) noexcept { return std::abs(x) < 1.0e-30 ? 0.0 : x; }

// Even and odd harmonic generation. The soft rectifier creates even harmonics;
// the cubic-like soft-knee residual adds odd harmonics. This is not a linear EQ.
inline double harmonics(double input, double amount) noexcept {
    const double drive = 1.0 + 6.0 * amount;
    const double x = input * drive;
    // A rounded rectifier avoids the very high partials of an almost absolute-
    // value corner when the new extreme range is used.
    const double even = std::sqrt(x * x + 0.36) - 0.6;
    const double odd = x - std::tanh(x);
    // Preserve fine control near zero and expand the top end sixfold.
    return amount * (1.0 + 5.0*amount*amount*amount) * (0.28 * even + 0.45 * odd) / drive;
}

struct Core {
    double aLow = 0.0, aHigh = 0.0, aPresence = 0.0, aAir = 0.0;
    double low1 = 0.0, low2 = 0.0, high1 = 0.0, high2 = 0.0;
    std::array<double, 2> presenceHp{}, airHp{};
    void prepare(double processingRate, double baseRate) noexcept {
        const auto alpha = [processingRate](double hz) { return 1.0 - std::exp(-2.0 * pi * hz / processingRate); };
        aLow = alpha(std::min(1800.0, baseRate * 0.12));
        aHigh = alpha(std::min(6500.0, baseRate * 0.32));
        aPresence = alpha(std::min(1800.0, baseRate * 0.12));
        aAir = alpha(std::min(5500.0, baseRate * 0.30));
        reset();
    }
    void reset() noexcept { low1 = low2 = high1 = high2 = 0.0; presenceHp.fill(0.0); airHp.fill(0.0); }
    static double highpass(double input, double alpha, std::array<double, 2>& states) noexcept {
        for (auto& state : states) { state = quiet(state + alpha * (input - state)); input -= state; }
        return input;
    }
    double process(double input, double midAir, double highAir) noexcept {
        low1 = quiet(low1 + aLow * (input - low1));
        low2 = quiet(low2 + aLow * (low1 - low2));
        high1 = quiet(high1 + aHigh * (input - high1));
        high2 = quiet(high2 + aHigh * (high1 - high2));
        const double presenceBand = high2 - low2;
        const double airBand = input - high2;
        // Only the additional harmonic signals are high-passed. Their DC and
        // low-frequency components never alter the original clean path.
        return highpass(harmonics(presenceBand, midAir) + .85*midAir*midAir*presenceBand, aPresence, presenceHp) +
               highpass(harmonics(airBand, highAir) + .85*highAir*highAir*airBand, aAir, airHp);
    }
};
} // namespace air_detail

// Original presence/high-band harmonic exciter. Not a model or copy of any
// commercial processor. All audio-path storage is fixed; prepare never allocates.
class AirDSP {
public:
    static constexpr int oversamplingFactor = 8;
    static constexpr int filterTaps = 193;
    static constexpr int fixedLatencySamples = 24;

    void prepare(double sampleRate, int maxBlock, int channels) noexcept {
        (void) maxBlock;
        rate_ = std::clamp(air_detail::finite(sampleRate, 48000.0), 8000.0, 384000.0);
        channels_ = std::clamp(channels, 1, 2);
        smoothing_ = 1.0 - std::exp(-1.0 / (rate_ * 0.015));
        makeFilter();
        for (auto& state : state_) state.core.prepare(rate_ * (liveMode_ ? 1 : oversamplingFactor), rate_);
        prepared_ = true;
        reset();
    }
    void reset() noexcept {
        for (auto& state : state_) {
            state.input.fill(0.0); state.excitation.fill(0.0); state.dry.fill(0.0f);
            state.inputPosition = state.excitationPosition = state.dryPosition = 0;
            state.core.reset();
        }
        mid_ = targetMid_; high_ = targetHigh_; mix_ = targetMix_; output_ = targetOutput_;
        hasAudio_ = false;
    }
    void setParameters(float midAir0to100, float highAir0to100, float mix0to100, float outputDb) noexcept {
        targetMid_ = std::clamp(air_detail::finite(midAir0to100), 0.0, 100.0) * 0.01;
        targetHigh_ = std::clamp(air_detail::finite(highAir0to100), 0.0, 100.0) * 0.01;
        targetMix_ = std::clamp(air_detail::finite(mix0to100), 0.0, 100.0) * 0.01;
        targetOutput_ = std::pow(10.0, std::clamp(air_detail::finite(outputDb), -18.0, 6.0) / 20.0);
        if (!hasAudio_) { mid_ = targetMid_; high_ = targetHigh_; mix_ = targetMix_; output_ = targetOutput_; }
    }
    void setLiveMode(bool live) noexcept {
        if (liveMode_ == live) return;
        liveMode_ = live;
        for (auto& state : state_) state.core.prepare(rate_ * (liveMode_ ? 1 : oversamplingFactor), rate_);
        reset();
    }
    int latencySamples() const noexcept { return liveMode_ ? 0 : fixedLatencySamples; }

    void process(float* const* buffers, int channels, int samples) noexcept {
        if (!prepared_ || !buffers || channels <= 0 || samples <= 0) return;
        const int active = std::min(channels, channels_);
        for (int c = 0; c < active; ++c) if (!buffers[c]) return;
        hasAudio_ = true;
        for (int n = 0; n < samples; ++n) {
            smooth(mid_, targetMid_); smooth(high_, targetHigh_);
            smooth(mix_, targetMix_); smooth(output_, targetOutput_);
            for (int c = 0; c < active; ++c) {
                auto& state = state_[static_cast<std::size_t>(c)];
                const float input = static_cast<float>(std::clamp(air_detail::finite(buffers[c][n]), -32.0, 32.0));
                const float dry = liveMode_ ? input : state.dry[state.dryPosition];
                state.dry[state.dryPosition] = input;
                state.dryPosition = (state.dryPosition + 1) % fixedLatencySamples;
                state.input[state.inputPosition] = input;
                double extra = 0.0;
                if (liveMode_) extra = state.core.process(input, mid_, high_);
                for (int phase = 0; !liveMode_ && phase < oversamplingFactor; ++phase) {
                    double upsampled = 0.0;
                    int position = state.inputPosition;
                    for (int tap = phase; tap < filterTaps; tap += oversamplingFactor) {
                        upsampled += filter_[static_cast<std::size_t>(tap)] * state.input[static_cast<std::size_t>(position)];
                        if (--position < 0) position = inputHistory - 1;
                    }
                    const double excitation = state.core.process(upsampled * oversamplingFactor, mid_, high_);
                    state.excitation[state.excitationPosition] = excitation;
                    if (phase == 0) {
                        int index = state.excitationPosition;
                        for (int tap = 0; tap < filterTaps; ++tap) {
                            extra += filter_[static_cast<std::size_t>(tap)] * state.excitation[static_cast<std::size_t>(index)];
                            if (--index < 0) index = filterTaps - 1;
                        }
                    }
                    state.excitationPosition = (state.excitationPosition + 1) % filterTaps;
                }
                state.inputPosition = (state.inputPosition + 1) % inputHistory;
                const double result = (static_cast<double>(dry) + mix_ * extra) * output_;
                buffers[c][n] = static_cast<float>(std::clamp(air_detail::finite(result), -64.0, 64.0));
            }
        }
    }

private:
    bool liveMode_ = false;
    static constexpr int inputHistory = (filterTaps + oversamplingFactor - 1) / oversamplingFactor;
    struct State {
        std::array<double, inputHistory> input{};
        std::array<double, filterTaps> excitation{};
        std::array<float, fixedLatencySamples> dry{};
        air_detail::Core core;
        int inputPosition = 0, excitationPosition = 0, dryPosition = 0;
    };
    void smooth(double& value, double target) const noexcept {
        value += smoothing_ * (target - value);
        if (std::abs(value - target) < 1.0e-12) value = target;
    }
    void makeFilter() noexcept {
        constexpr double cutoff = 0.45 / oversamplingFactor;
        constexpr int center = (filterTaps - 1) / 2;
        double sum = 0.0;
        for (int tap = 0; tap < filterTaps; ++tap) {
            const double offset = tap - center;
            const double sinc = tap == center ? 2.0 * cutoff :
                std::sin(2.0 * air_detail::pi * cutoff * offset) / (air_detail::pi * offset);
            const double angle = 2.0 * air_detail::pi * tap / (filterTaps - 1);
            const double coefficient = sinc * (0.42 - 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle));
            filter_[static_cast<std::size_t>(tap)] = coefficient;
            sum += coefficient;
        }
        for (auto& coefficient : filter_) coefficient /= sum;
    }
    std::array<State, 2> state_{};
    std::array<double, filterTaps> filter_{};
    double rate_ = 48000.0, smoothing_ = 0.0;
    double mid_ = 0.0, high_ = 0.0, mix_ = 1.0, output_ = 1.0;
    double targetMid_ = 0.0, targetHigh_ = 0.0, targetMix_ = 1.0, targetOutput_ = 1.0;
    int channels_ = 2;
    bool prepared_ = false, hasAudio_ = false;
};
} // namespace gill
