#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace gill {

// Original clean/tape/ping-pong delay. The wrapper converts tempo divisions to
// milliseconds; this engine never owns or changes the host tempo. Configuration
// and processing belong to the audio thread (prepare while audio is stopped).
class EchoDSP {
public:
    static constexpr double maximumDelayMs = 8000.0;
    static constexpr double delayTransitionSeconds = 0.035;

    void prepare(double sampleRate, int maxBlock, int channels) {
        (void) maxBlock;
        rate_ = std::clamp(finite(sampleRate, 48000.0), 8000.0, 384000.0);
        channels_ = std::clamp(channels, 1, 2);
        const auto length = static_cast<std::size_t>(std::ceil(rate_ * maximumDelayMs / 1000.0)) + 4;
        for (auto& line : delay_) line.assign(length, 0.0f);
        smoothing_ = 1.0 - std::exp(-1.0 / (rate_ * 0.015));
        fadeLength_ = std::max(1, static_cast<int>(std::round(rate_ * delayTransitionSeconds)));
        updateColourCoefficient();
        prepared_ = true;
        reset();
    }

    void reset() noexcept {
        for (auto& line : delay_) std::fill(line.begin(), line.end(), 0.0f);
        write_ = 0;
        lowpass_.fill(0.0);
        previousTapeInput_.fill(0.0);
        feedback_ = targetFeedback_; mix_ = targetMix_; dry_ = targetDry_; colour_ = targetColour_;
        width_ = targetWidth_; alpha_ = targetAlpha_; style_ = targetStyle_;
        currentDelay_ = nextDelay_ = targetDelayMs_ * rate_ / 1000.0;
        fadeRemaining_ = 0;
        hasAudio_ = false;
    }

    // CLEAN=0, TAPE=1, PINGPONG=2. At full width ping-pong sends the mono sum
    // to the left on its first repeat, then crosses subsequent feedback repeats.
    // Width=0 centres the wet signal; mono always uses a single feedback path.
    void setParameters(float delayMs, float feedback0to90, float mix0to100,
                       float color0to100, float width0to100, int style, float dry0to100=100) noexcept {
        targetDry_ = std::clamp(finite(dry0to100,100.0),0.0,100.0)*.01;
        targetDelayMs_ = std::clamp(finite(delayMs, 250.0), 1.0, maximumDelayMs);
        targetFeedback_ = std::clamp(finite(feedback0to90, 0.0), 0.0, 90.0) * 0.01;
        targetMix_ = std::clamp(finite(mix0to100, 0.0), 0.0, 100.0) * 0.01;
        targetColour_ = std::clamp(finite(color0to100, 0.0), 0.0, 100.0) * 0.01;
        targetWidth_ = std::clamp(finite(width0to100, 100.0), 0.0, 100.0) * 0.01;
        targetStyle_.fill(0.0);
        targetStyle_[static_cast<std::size_t>(std::clamp(style, 0, 2))] = 1.0;
        updateColourCoefficient();
        if (!hasAudio_) {
            feedback_ = targetFeedback_; mix_ = targetMix_; dry_ = targetDry_; colour_ = targetColour_;
            width_ = targetWidth_; alpha_ = targetAlpha_; style_ = targetStyle_;
            currentDelay_ = nextDelay_ = targetDelayMs_ * rate_ / 1000.0;
            fadeRemaining_ = 0;
        }
    }

    int latencySamples() const noexcept { return 0; }

    // Conservative time for a unity impulse to decay below -80 dB. For a long
    // delay and 90% feedback this can intentionally exceed ten minutes.
    double tailSeconds() const noexcept {
        const double feedback = std::clamp(std::max(feedback_, targetFeedback_), 0.0, 0.9);
        const double repeats = feedback > 0.0 ? 1.0 + std::ceil(std::log(0.0001) / std::log(feedback)) : 1.0;
        const double longest = std::max({currentDelay_, nextDelay_, targetDelayMs_ * rate_ / 1000.0}) / rate_;
        return longest * repeats + delayTransitionSeconds + 0.05;
    }

    void process(float* const* buffers, int channels, int samples) noexcept {
        if (!prepared_ || !buffers || channels <= 0 || samples <= 0) return;
        const int active = std::min(channels, channels_);
        for (int c = 0; c < active; ++c) if (!buffers[c]) return;
        hasAudio_ = true;
        const auto length = delay_[0].size();
        for (int n = 0; n < samples; ++n) {
            smooth(feedback_, targetFeedback_); smooth(mix_, targetMix_); smooth(dry_,targetDry_);
            smooth(colour_, targetColour_); smooth(width_, targetWidth_); smooth(alpha_, targetAlpha_);
            for (std::size_t mode = 0; mode < style_.size(); ++mode) smooth(style_[mode], targetStyle_[mode]);
            const double requested = targetDelayMs_ * rate_ / 1000.0;
            if (fadeRemaining_ == 0 && std::abs(requested - currentDelay_) > 1.0e-7) {
                nextDelay_ = requested;
                fadeRemaining_ = fadeLength_;
            }
            // Read heads remain fixed during each fade. Rapid new requests are
            // coalesced until it ends; there is no unbounded playback-rate jump.
            const double fade = fadeRemaining_ > 0 ?
                1.0 - static_cast<double>(fadeRemaining_) / fadeLength_ : 0.0;
            std::array<double, 2> input{}, wet{};
            for (int c = 0; c < active; ++c) {
                input[static_cast<std::size_t>(c)] = std::clamp(finite(buffers[c][n], 0.0), -32.0, 32.0);
                double delayed = read(c, currentDelay_);
                if (fadeRemaining_ > 0) delayed += fade * (read(c, nextDelay_) - delayed);
                auto& low = lowpass_[static_cast<std::size_t>(c)];
                low = quiet(low + alpha_ * (delayed - low));
                // At COLOR=0 CLEAN repeats are sample-exact. The filter state
                // still runs so enabling colour does not expose stale history.
                const double coloured = delayed + colour_ * (low - delayed);
                const double driven = 1.8 * coloured;
                const double old = previousTapeInput_[static_cast<std::size_t>(c)];
                previousTapeInput_[static_cast<std::size_t>(c)] = driven;
                wet[static_cast<std::size_t>(c)] = coloured;
                if (style_[1] > 0.0) {
                    const double delta = driven - old;
                    // First-order antiderivative antialiasing of tanh. The
                    // average adds half a sample inside each TAPE repeat only.
                    const double tape = (std::abs(delta) > 1.0e-6 ?
                        (logCosh(driven) - logCosh(old)) / delta :
                        std::tanh(0.5 * (driven + old))) / 1.8;
                    wet[static_cast<std::size_t>(c)] += style_[1] * (tape - coloured);
                }
            }

            if (active == 2) {
                const double monoInput = 0.5 * (input[0] + input[1]);
                const double ping = style_[2];
                const double cross = ping * width_;
                const double leftInput = input[0] + ping * (monoInput - input[0]);
                const double rightInput = input[1] + ping * ((1.0 - width_) * monoInput - input[1]);
                const double feedLeft = wet[0] + cross * (wet[1] - wet[0]);
                const double feedRight = wet[1] + cross * (wet[0] - wet[1]);
                delay_[0][write_] = static_cast<float>(std::clamp(leftInput + feedback_ * feedLeft, -64.0, 64.0));
                delay_[1][write_] = static_cast<float>(std::clamp(rightInput + feedback_ * feedRight, -64.0, 64.0));
                const double mid = 0.5 * (wet[0] + wet[1]);
                const double side = 0.5 * (wet[0] - wet[1]) * width_;
                wet[0] = mid + side; wet[1] = mid - side;
            } else {
                delay_[0][write_] = static_cast<float>(std::clamp(input[0] + feedback_ * wet[0], -64.0, 64.0));
            }
            for (int c = 0; c < active; ++c) {
                const auto index = static_cast<std::size_t>(c);
                const double output = mix_ == 0.0 && dry_ == 1.0 ? input[index] :
                    (1.0 - mix_) * dry_ * input[index] + mix_ * wet[index];
                buffers[c][n] = static_cast<float>(std::clamp(finite(output, 0.0), -64.0, 64.0));
            }
            if (fadeRemaining_ > 0 && --fadeRemaining_ == 0) currentDelay_ = nextDelay_;
            if (++write_ >= length) write_ = 0;
        }
    }

private:
    static double finite(double value, double fallback) noexcept { return std::isfinite(value) ? value : fallback; }
    static double quiet(double value) noexcept { return std::abs(value) < 1.0e-30 ? 0.0 : value; }
    static double logCosh(double x) noexcept {
        const double magnitude = std::abs(x);
        return magnitude + std::log1p(std::exp(-2.0 * magnitude)) - 0.69314718055994530942;
    }
    void smooth(double& value, double target) const noexcept {
        value += smoothing_ * (target - value);
        if (std::abs(value - target) < 1.0e-12) value = target;
    }
    void updateColourCoefficient() noexcept {
        const double cutoff = std::min(rate_ * 0.45, 18000.0 * std::pow(600.0 / 18000.0, targetColour_));
        targetAlpha_ = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * cutoff / rate_);
    }
    double read(int channel, double delaySamples) const noexcept {
        const auto& line = delay_[static_cast<std::size_t>(channel)];
        const double position = static_cast<double>(write_) - std::clamp(delaySamples, 1.0, static_cast<double>(line.size() - 4));
        double wrapped = position;
        if (wrapped < 0.0) wrapped += static_cast<double>(line.size());
        const auto first = static_cast<std::size_t>(wrapped);
        const auto second = first + 1 < line.size() ? first + 1 : 0;
        const double fraction = wrapped - static_cast<double>(first);
        return static_cast<double>(line[first]) + fraction * (static_cast<double>(line[second]) - line[first]);
    }

    std::array<std::vector<float>, 2> delay_;
    std::array<double, 2> lowpass_{}, previousTapeInput_{};
    std::array<double, 3> style_{{1.0, 0.0, 0.0}}, targetStyle_{{1.0, 0.0, 0.0}};
    double rate_ = 48000.0, targetDelayMs_ = 250.0, currentDelay_ = 12000.0, nextDelay_ = 12000.0;
    double targetFeedback_ = 0.3, feedback_ = 0.3, targetMix_ = 0.25, mix_ = 0.25;
    double targetDry_=1.0,dry_=1.0;
    double targetColour_ = 0.0, colour_ = 0.0, targetWidth_ = 1.0, width_ = 1.0;
    double targetAlpha_ = 0.9, alpha_ = 0.9, smoothing_ = 0.0;
    std::size_t write_ = 0;
    int channels_ = 2, fadeLength_ = 1680, fadeRemaining_ = 0;
    bool prepared_ = false, hasAudio_ = false;
};
} // namespace gill
