#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace gill {
namespace heat_detail {

constexpr double pi = 3.1415926535897932384626433832795;

inline double finiteOr(double value, double fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}
inline double quiet(double value) noexcept {
    return std::abs(value) < 1.0e-30 ? 0.0 : value;
}

template<int length> inline double dot(const double* samples,const double* coefficients) noexcept {
#if defined(_M_X64) || defined(__SSE2__)
    __m128d a=_mm_setzero_pd(),b=a,c=a,d=a;int i=0;
    for(;i+7<length;i+=8){a=_mm_add_pd(a,_mm_mul_pd(_mm_loadu_pd(samples+i),_mm_loadu_pd(coefficients+i)));b=_mm_add_pd(b,_mm_mul_pd(_mm_loadu_pd(samples+i+2),_mm_loadu_pd(coefficients+i+2)));c=_mm_add_pd(c,_mm_mul_pd(_mm_loadu_pd(samples+i+4),_mm_loadu_pd(coefficients+i+4)));d=_mm_add_pd(d,_mm_mul_pd(_mm_loadu_pd(samples+i+6),_mm_loadu_pd(coefficients+i+6)));}
    a=_mm_add_pd(_mm_add_pd(a,b),_mm_add_pd(c,d));double result=_mm_cvtsd_f64(_mm_add_sd(a,_mm_unpackhi_pd(a,a)));
    for(;i<length;++i)result+=samples[i]*coefficients[i];return result;
#else
    double result=0;for(int i=0;i<length;++i)result+=samples[i]*coefficients[i];return result;
#endif
}

// C1-continuous bounded rational soft knee. No transcendental call in the
// 32x loop; zero and the derivative at the WARM bias are calibrated below.
inline double softKnee(double x) noexcept {
    if(x>=3)return 1;if(x<=-3)return -1;const double square=x*x;
    return x*(27+square)/(27+9*square);
}

// Keep the stored 0..24 range, but distribute the audible change over its full
// travel. The nonlinear branch starts at 24 dB and reaches the same 48 dB knee
// at maximum; its contribution fades continuously from zero to full strength.
// Starting the branch above unity avoids an almost inactive first half on vocals.
inline double effectiveDriveDb(double controlDb) noexcept {
    return 24.0 + std::clamp(controlDb, 0.0, 24.0);
}
inline double driveNormalisation(double gain) noexcept {
    // Continuous reference-level compensation, without the former 12 dB stop.
    // This bounds the saturated branch around an 0.08 peak band reference;
    // it is a fixed calibration, not an AGC or a guarantee of equal loudness.
    constexpr double reference = 0.08;
    return gain / std::sqrt(1.0 + reference * reference * gain * gain);
}

// Two low-passed copies provide three algebraically complementary bands.
// Subtracting the copies (rather than summing unrelated filters) is important:
// low + mid + high reconstructs the input at every sample, including transients.
struct Crossover {
    double aLow = 0.0, aHigh = 0.0;
    double low1 = 0.0, low2 = 0.0, high1 = 0.0, high2 = 0.0;

    void prepare(double sampleRate) noexcept {
        aLow = 1.0 - std::exp(-2.0 * pi * 180.0 / sampleRate);
        aHigh = 1.0 - std::exp(-2.0 * pi * 3000.0 / sampleRate);
        reset();
    }
    void reset() noexcept { low1 = low2 = high1 = high2 = 0.0; }
    std::array<double, 3> split(double input) noexcept {
        low1 = quiet(low1 + aLow * (input - low1));
        low2 = quiet(low2 + aLow * (low1 - low2));
        high1 = quiet(high1 + aHigh * (input - high1));
        high2 = quiet(high2 + aHigh * (high1 - high2));
        return {{low2, high2 - low2, input - high2}};
    }
};

inline double shape(double input, int style) noexcept {
    if (style == 1) // TAPE: smooth, symmetric soft saturation.
        return input / std::sqrt(1.0 + input * input);
    if (style == 2) { // EDGE: a rounded, finite-slope clipping knee.
        if (input >= 1.0) return 2.0 / 3.0;
        if (input <= -1.0) return -2.0 / 3.0;
        return input - (input * input * input) / 3.0;
    }
    // WARM: a slightly asymmetric soft knee; unity derivative at the origin.
    constexpr double bias = 0.12;
    constexpr double denominator=27+9*bias*bias;
    constexpr double biasValue=bias*(27+bias*bias)/denominator;
    constexpr double derivative=((27+3*bias*bias)*denominator-bias*(27+bias*bias)*18*bias)/(denominator*denominator);
    return (softKnee(input + bias) - biasValue) / derivative;
}

} // namespace heat_detail

// Original three-band saturation engine. WARM/TAPE/EDGE describe sonic styles,
// not physical circuit models. No external DSP code or JUCE dependencies.
// Call setParameters on the audio thread (or synchronize it externally).
class HeatDSP {
public:
    static constexpr int maximumChannels = 8;
    static constexpr int oversamplingFactor = 32;
    static constexpr int filterTaps = 769;
    static constexpr int fixedLatencySamples = 24;

    void prepare(double sampleRate, int maxBlock, int channels) noexcept {
        sampleRate_ = std::clamp(heat_detail::finiteOr(sampleRate, 48000.0),
                                 8000.0, 384000.0);
        (void) maxBlock; // Fixed state; arbitrary block sizes are supported.
        channels_ = std::clamp(channels, 1, maximumChannels);
        smoothing_ = 1.0 - std::exp(-1.0 / (0.015 * sampleRate_));
        dcPole_ = std::exp(-2.0 * heat_detail::pi * 8.0 /
                           (sampleRate_ * (liveMode_ ? 1 : oversamplingFactor)));
        makeFilter();
        for (auto& channel : state_)
            channel.crossover.prepare(sampleRate_ * (liveMode_ ? 1 : oversamplingFactor));
        prepared_ = true;
        reset();
    }

    void reset() noexcept {
        for (auto& channel : state_) {
            channel.input.fill(0.0);
            channel.residual.fill(0.0);
            channel.dry.fill(0.0f);
            channel.inputPosition = channel.residualPosition = channel.dryPosition = 0;
            channel.previousResidual = channel.dcState = 0.0;
            channel.crossover.reset();
        }
        gain_ = targetGain_;
        amount_ = targetAmount_;
        style_ = targetStyle_;
        mix_ = targetMix_;
        output_ = targetOutput_;
    }

    void setParameters(float lowDriveDb, float midDriveDb, float highDriveDb,
                       int style, float mix0to100, float outputDb) noexcept {
        const std::array<float, 3> drive{{lowDriveDb, midDriveDb, highDriveDb}};
        for (std::size_t band = 0; band < drive.size(); ++band) {
            const double db = std::clamp(heat_detail::finiteOr(drive[band], 0.0),
                                         0.0, 24.0);
            targetGain_[band] = std::pow(10.0, heat_detail::effectiveDriveDb(db) / 20.0);
            targetAmount_[band] = db / 24.0;
        }
        targetStyle_.fill(0.0);
        targetStyle_[static_cast<std::size_t>(std::clamp(style, 0, 2))] = 1.0;
        targetMix_ = std::clamp(heat_detail::finiteOr(mix0to100, 0.0), 0.0, 100.0) / 100.0;
        targetOutput_ = std::pow(10.0,
            std::clamp(heat_detail::finiteOr(outputDb, 0.0), -24.0, 12.0) / 20.0);
    }

    // LIVE is the same waveshaper/crossover at the base rate: zero buffering,
    // with less alias rejection. Switching allocates nothing.
    void setLiveMode(bool live) noexcept {
        if (liveMode_ == live) return;
        liveMode_ = live;
        dcPole_ = std::exp(-2.0 * heat_detail::pi * 8.0 /
                           (sampleRate_ * (liveMode_ ? 1 : oversamplingFactor)));
        for (auto& channel : state_)
            channel.crossover.prepare(sampleRate_ * (liveMode_ ? 1 : oversamplingFactor));
        reset();
    }
    int latencySamples() const noexcept { return liveMode_ ? 0 : fixedLatencySamples; }

    void process(float* const* buffers, int channels, int samples) noexcept {
        if (!buffers || samples <= 0 || channels <= 0) return;
        if (!prepared_) prepare(48000.0, samples, channels);
        const int activeChannels = std::min({channels, channels_, maximumChannels});

        for (int sample = 0; sample < samples; ++sample) {
            std::array<double, 3> amount{},wetScale{};
            for (std::size_t band = 0; band < gain_.size(); ++band) {
                smooth(gain_[band], targetGain_[band]);
                smooth(amount_[band], targetAmount_[band]);
                amount[band] = amount_[band];
                smooth(style_[band], targetStyle_[band]);
                wetScale[band]=amount[band]/heat_detail::driveNormalisation(gain_[band]);
            }
            smooth(mix_, targetMix_);
            smooth(output_, targetOutput_);
            const int activeMode=style_[0]==1?0:style_[1]==1?1:style_[2]==1?2:-1;

            for (int channel = 0; channel < activeChannels; ++channel) {
                if (!buffers[channel]) continue;
                auto& s = state_[static_cast<std::size_t>(channel)];
                const float input = static_cast<float>(std::clamp(
                    heat_detail::finiteOr(buffers[channel][sample], 0.0), -32.0, 32.0));
                // This direct delay makes dry=0% mix and all-zero-drive exact.
                const float dry = liveMode_ ? input : s.dry[s.dryPosition];
                s.dry[s.dryPosition] = input;
                s.dryPosition = (s.dryPosition + 1) % fixedLatencySamples;
                s.input[s.inputPosition] = s.input[s.inputPosition+inputHistory] = input;

                double downsampledResidual = 0.0;
                for (int phase = 0; phase < (liveMode_ ? 1 : oversamplingFactor); ++phase) {
                    const double upsampled=liveMode_ ? input : heat_detail::dot<inputHistory>(s.input.data()+s.inputPosition,interpolation_[phase].data());
                    const auto bands = s.crossover.split(upsampled);
                    double nonlinearResidual = 0.0;
                    for (std::size_t band = 0; band < bands.size(); ++band) {
                        if (amount[band] == 0.0) continue;
                        const double driven = bands[band] * gain_[band];
                        double shaped = 0.0;
                        if(activeMode>=0)shaped=heat_detail::shape(driven,activeMode);
                        else for (int mode = 0; mode < 3; ++mode) {
                            const double weight = style_[static_cast<std::size_t>(mode)];
                            if (weight > 0.0)
                                shaped += weight * heat_detail::shape(driven, mode);
                        }
                        nonlinearResidual += shaped * wetScale[band] - bands[band]*amount[band];
                    }
                    // Remove DC generated by asymmetric saturation. The clean
                    // direct path is deliberately untouched at zero drive.
                    const double dcFiltered = nonlinearResidual - s.previousResidual +
                                              dcPole_ * s.dcState;
                    s.previousResidual = nonlinearResidual;
                    s.dcState = heat_detail::quiet(dcFiltered);
                    s.residual[s.residualPosition] = s.residual[s.residualPosition+filterTaps] = s.dcState;

                    if (phase == 0) {
                        downsampledResidual=liveMode_ ? s.dcState : heat_detail::dot<filterTaps>(s.residual.data()+s.residualPosition,filter_.data());
                    }
                    if(--s.residualPosition<0)s.residualPosition=filterTaps-1;
                }
                if(--s.inputPosition<0)s.inputPosition=inputHistory-1;
                const double result = (static_cast<double>(dry) +
                                       mix_ * downsampledResidual) * output_;
                // Fault containment only; normal audio is never clipped here.
                buffers[channel][sample] = static_cast<float>(std::clamp(
                    heat_detail::finiteOr(result, 0.0), -32.0, 32.0));
            }
        }
    }

private:
    bool liveMode_ = false;
    static constexpr int inputHistory = (filterTaps + oversamplingFactor - 1) /
                                         oversamplingFactor;
    struct ChannelState {
        std::array<double, inputHistory*2> input{};
        std::array<double, filterTaps*2> residual{};
        std::array<float, fixedLatencySamples> dry{};
        heat_detail::Crossover crossover;
        int inputPosition = 0, residualPosition = 0, dryPosition = 0;
        double previousResidual = 0.0, dcState = 0.0;
    };

    void smooth(double& current, double target) const noexcept {
        current += smoothing_ * (target - current);
        if (std::abs(current - target) < 1.0e-12) current = target;
    }
    void makeFilter() noexcept {
        // Oversampled Blackman-windowed sinc. Cutoff is 0.45 of the base rate;
        // interpolation and decimation each contribute 12 base-rate samples.
        constexpr double cutoff = 0.45 / oversamplingFactor;
        constexpr int center = (filterTaps - 1) / 2;
        double sum = 0.0;
        for (int tap = 0; tap < filterTaps; ++tap) {
            const double offset = static_cast<double>(tap - center);
            const double sinc = tap == center ? 2.0 * cutoff :
                std::sin(2.0 * heat_detail::pi * cutoff * offset) /
                (heat_detail::pi * offset);
            const double angle = 2.0 * heat_detail::pi * tap / (filterTaps - 1);
            const double window = 0.42 - 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle);
            filter_[static_cast<std::size_t>(tap)] = sinc * window;
            sum += sinc * window;
        }
        for (auto& coefficient : filter_) coefficient /= sum;
        for(int phase=0;phase<oversamplingFactor;++phase)for(int i=0;i<inputHistory;++i){const int tap=phase+i*oversamplingFactor;interpolation_[phase][i]=tap<filterTaps?filter_[tap]*oversamplingFactor:0;}
    }

    std::array<ChannelState, maximumChannels> state_{};
    std::array<double, filterTaps> filter_{};
    std::array<std::array<double,inputHistory>,oversamplingFactor> interpolation_{};
    std::array<double, 3> gain_{{15.848931924611133, 15.848931924611133, 15.848931924611133}};
    std::array<double, 3> targetGain_ = gain_;
    std::array<double, 3> amount_{{0.0, 0.0, 0.0}};
    std::array<double, 3> targetAmount_ = amount_;
    std::array<double, 3> style_{{1.0, 0.0, 0.0}};
    std::array<double, 3> targetStyle_{{1.0, 0.0, 0.0}};
    double sampleRate_ = 48000.0, smoothing_ = 0.0, dcPole_ = 0.0;
    double mix_ = 1.0, targetMix_ = 1.0, output_ = 1.0, targetOutput_ = 1.0;
    int channels_ = 2;
    bool prepared_ = false;
};

} // namespace gill
