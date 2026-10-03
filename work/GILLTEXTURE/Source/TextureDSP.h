#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Original, causal wet-effect engines. There is no look-ahead or block buffering.
// A grain deliberately reads earlier sound; that creative delay is not PDC.
namespace gill::texture {
constexpr double pi = 3.1415926535897932384626433832795;
enum class Kind { Vocode, Grain, Pulse };
struct Settings {
    int carrier = 0;
    float note = 48, formant = 0, response = 50, brightness = 60, unvoiced = 25;
    float size = 80, density = 8, pitch = 0, scatter = 45, feedback = 15, width = 75;
    int division = 2;
    float depth = 100, smooth = 8, swing = 0, phase = 0;
    std::array<float, 16> steps { 1,0,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0 };
    bool pro = true, freeze = false;
};
inline float clean(float x) noexcept { return std::isfinite(x) ? std::clamp(x, -16.f, 16.f) : 0.f; }
inline double bounded(double x, double lo, double hi, double fallback) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}

class Engine {
public:
    explicit Engine(Kind type) noexcept : kind(type) {}
    void prepare(double rate, int /*maxBlock*/ = 512) {
        fs = bounded(rate, 8000., 384000., 48000.);
        // Only prepare() allocates. Two seconds of stereo history at every rate.
        if (kind == Kind::Grain)
            for (auto& channel : history) channel.assign(std::size_t(std::ceil(fs * 2.1)) + 8, 0.f);
        for (std::size_t i = 0; i < window.size(); ++i)
            window[i] = float(.5 - .5 * std::cos(2 * pi * double(i) / double(window.size() - 1)));
        parameterCoefficient = 1 - std::exp(-1 / (fs * .012));
        reset();
    }
    void reset() noexcept {
        for (auto& channel : history) std::fill(channel.begin(), channel.end(), 0.f);
        analysis = {}; synthesis = {}; envelopes = {}; carrierEnvelopes = {};
        grains = {}; oscillatorPhase = {}; carrierBlend = {}; breathLow = {};
        recordLow = {}; feedbackLow = {};
        writePosition = 0; recorded = 0; untilGrain = 0; random = 0x7157ab19u;
        freeBeats = 0; gate = 0; pulseStep = 0; controlCounter = 0;
        initialized = false; frozen = false; previousCarrier = -1;
    }
    int currentStep() const noexcept { return pulseStep; }
    int activeGrainCount() const noexcept {
        int count = 0; for (const auto& grain : grains) if (grain.age < grain.length) ++count; return count;
    }
    int latencySamples() const noexcept { return 0; }

    // Main input = modulator for Vocode, record input for Grain, current input for
    // Pulse. External carrier pointers may be null; external mode then stays silent.
    // ppq is the first sample's host quarter-note position, not a block-end value.
    void process(float* left, float* right, const float* carrierLeft, const float* carrierRight,
                 int count, const Settings& settings, double bpm, double ppq,
                 bool ppqValid, bool playing) noexcept {
        if (left == nullptr || count <= 0) return;
        const auto target = sanitize(settings);
        if (!initialized) {
            state = target; initialized = true;
            previousCarrier = target.carrier;
            carrierBlend[std::size_t(target.carrier)] = 1;
        }
        bpm = bounded(bpm, 20., 400., 120.);
        ppqValid = ppqValid && std::isfinite(ppq);
        const double beatIncrement = bpm / (60 * fs);
        for (int sample = 0; sample < count; ++sample) {
            smoothParameters(target);
            const std::array<double, 2> input { clean(left[sample]), right ? clean(right[sample]) : clean(left[sample]) };
            std::array<double, 2> wet {};
            if (kind == Kind::Vocode) {
                const std::array<double, 2> external { carrierLeft ? clean(carrierLeft[sample]) : 0.,
                    carrierRight ? clean(carrierRight[sample]) : carrierLeft ? clean(carrierLeft[sample]) : 0. };
                wet = vocode(input, external, target.carrier);
            } else if (kind == Kind::Grain) {
                wet = granular(input, target.freeze, target.pro);
            } else {
                // Host seeks/loops affect the pattern immediately, but the gain
                // envelope remains continuous, avoiding an abrupt audio splice.
                const double beats = ppqValid ? ppq + (playing ? sample * beatIncrement : 0.) : freeBeats;
                wet = pulse(input, beats, target);
                if (ppqValid) freeBeats = beats + beatIncrement;
                else freeBeats += beatIncrement;
                if (std::abs(freeBeats) > 1.e8) freeBeats = std::fmod(freeBeats, 16.);
            }
            left[sample] = clean(float(right ? wet[0] : .5 * (wet[0] + wet[1])));
            if (right) right[sample] = clean(float(wet[1]));
        }
    }

private:
    static constexpr int bands = 16, maxGrains = 32;
    // Topology-preserving state-variable bandpass: bounded state and stable
    // coefficient changes without clearing the resonators during automation.
    struct Bandpass {
        double s1 = 0, s2 = 0;
        double process(double x, double g, double damping) noexcept {
            const double v3 = x - s2;
            const double v1 = (s1 + g * v3) / (1 + g * (g + damping));
            const double v2 = s2 + g * v1;
            s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
            if (std::abs(s1) < 1.e-24) s1 = 0;
            if (std::abs(s2) < 1.e-24) s2 = 0;
            return v1 * damping; // Unity gain at centre frequency.
        }
    };
    struct Grain {
        double position = 0, ratio = 1, leftGain = 1, rightGain = 1;
        int age = 0, length = 0, release = 0, releaseLength = 0;
    };
    static Settings sanitize(const Settings& s) noexcept {
        Settings t = s;
        auto clamp = [](float x, float lo, float hi, float fallback) { return float(bounded(x, lo, hi, fallback)); };
        t.carrier = std::clamp(s.carrier, 0, 3);
        t.note = clamp(s.note, 24, 84, 48); t.formant = clamp(s.formant, -12, 12, 0);
        t.response = clamp(s.response, 0, 100, 50); t.brightness = clamp(s.brightness, 0, 100, 60);
        t.unvoiced = clamp(s.unvoiced, 0, 100, 25); t.size = clamp(s.size, 15, 240, 80);
        t.density = clamp(s.density, 2, 30, 8); t.pitch = clamp(s.pitch, -12, 12, 0);
        t.scatter = clamp(s.scatter, 0, 100, 45); t.feedback = clamp(s.feedback, 0, 70, 15);
        t.width = clamp(s.width, 0, 100, 75); t.division = std::clamp(s.division, 0, 3);
        t.depth = clamp(s.depth, 0, 100, 100); t.smooth = clamp(s.smooth, 1, 50, 8);
        t.swing = clamp(s.swing, 0, 50, 0); t.phase = clamp(s.phase, 0, 100, 0);
        for (std::size_t i = 0; i < t.steps.size(); ++i) t.steps[i] = clamp(s.steps[i], 0, 1, 0);
        return t;
    }
    void smoothParameters(const Settings& t) noexcept {
        auto approach = [this](float& value, float destination) {
            value = float(double(value) + parameterCoefficient * (double(destination) - value));
            if (std::abs(value - destination) < 1.e-5f) value = destination;
        };
        approach(state.note, t.note); approach(state.formant, t.formant);
        approach(state.response, t.response); approach(state.brightness, t.brightness);
        approach(state.unvoiced, t.unvoiced); approach(state.size, t.size);
        approach(state.density, t.density); approach(state.pitch, t.pitch);
        approach(state.scatter, t.scatter); approach(state.feedback, t.feedback);
        approach(state.width, t.width); approach(state.depth, t.depth);
        approach(state.smooth, t.smooth); approach(state.swing, t.swing); approach(state.phase, t.phase);
    }
    static double polyBlep(double phase, double step) noexcept {
        if (phase < step) { const double t = phase / step; return t + t - t * t - 1; }
        if (phase > 1 - step) { const double t = (phase - 1) / step; return t * t + t + t + 1; }
        return 0;
    }
    double saw(int oscillator, double frequency, bool square = false) noexcept {
        const double step = std::clamp(frequency / fs, .000001, .4);
        auto& phase = oscillatorPhase[std::size_t(oscillator)];
        double value = square ? (phase < .5 ? 1. : -1.) : 2 * phase - 1;
        if (square) {
            value += polyBlep(phase, step);
            value -= polyBlep(phase < .5 ? phase + .5 : phase - .5, step);
        } else value -= polyBlep(phase, step);
        phase += step; phase -= std::floor(phase);
        return value;
    }
    void updateVocoder() noexcept {
        const double formantRatio = std::exp2(state.formant / 12.);
        for (int band = 0; band < bands; ++band) {
            const double frequency = 100 * std::pow(80., double(band) / (bands - 1));
            analysisG[band] = std::tan(pi * std::min(frequency, fs * .43) / fs);
            synthesisG[band] = std::tan(pi * std::min(frequency * formantRatio, fs * .43) / fs);
            tilt[band] = std::exp2((state.brightness - 50.) / 70. * (double(band) / (bands - 1) - .5));
        }
        attack = 1 - std::exp(-1 / (fs * (.001 + .00024 * state.response)));
        release = 1 - std::exp(-1 / (fs * (.015 + .00165 * state.response)));
        carrierFollow = 1 - std::exp(-1 / (fs * .025));
        breathCoefficient = 1 - std::exp(-2 * pi * std::min(3500., fs * .3) / fs);
    }
    std::array<double, 2> vocode(const std::array<double, 2>& input,
                                const std::array<double, 2>& external, int carrier) noexcept {
        if ((controlCounter++ & 31u) == 0) updateVocoder();
        const double frequency = 440 * std::exp2((state.note - 69.) / 12.);
        // All oscillators continue through carrier changes so the 12-ms crossfade
        // never reveals a reset phase. The fifth/octave chord is root-note based.
        const double single = saw(0, frequency);
        const double square = saw(1, frequency, true);
        const double chord = (saw(2, frequency * .998) + saw(3, frequency * 1.498307) + saw(4, frequency * 2.002)) / 3.;
        for (int type = 0; type < 4; ++type)
            carrierBlend[type] += parameterCoefficient * ((type == carrier ? 1. : 0.) - carrierBlend[type]);
        std::array<double, 2> output {};
        for (int channel = 0; channel < 2; ++channel) {
            const double source = .65 * (single * carrierBlend[0] + square * carrierBlend[1] + chord * carrierBlend[2])
                                + external[channel] * carrierBlend[3];
            for (int band = 0; band < bands; ++band) {
                const double modulator = std::abs(analysis[channel][band].process(input[channel], analysisG[band], .32));
                auto& envelope = envelopes[channel][band];
                envelope += (modulator > envelope ? attack : release) * (modulator - envelope);
                if (envelope < 1.e-20) envelope = 0;
                const double excitation = synthesis[channel][band].process(source, synthesisG[band], .32);
                auto& carrierEnvelope = carrierEnvelopes[channel][band];
                carrierEnvelope += carrierFollow * (std::abs(excitation) - carrierEnvelope);
                // Bounded per-band compensation maintains upper formants without
                // amplifying vanishing carrier energy or synthesising a carrier.
                const double compensation = std::min(10., 1. / (.055 + carrierEnvelope));
                output[channel] += excitation * envelope * compensation * tilt[band];
            }
            breathLow[channel] += breathCoefficient * (input[channel] - breathLow[channel]);
            // External mode with no signal must not leak modulator through the
            // consonant path. Internally voiced modes can retain speech detail.
            const double voicedFraction = carrierBlend[0] + carrierBlend[1] + carrierBlend[2];
            output[channel] = 1.5 * output[channel] + (input[channel] - breathLow[channel]) * (.008 * state.unvoiced) * voicedFraction;
        }
        previousCarrier = carrier;
        return output;
    }
    double unitRandom() noexcept {
        random = random * 1664525u + 1013904223u;
        return double(random >> 8) / 16777216.;
    }
    void startGrain(bool freeze) noexcept {
        auto available = std::find_if(grains.begin(), grains.end(), [](const Grain& grain) { return grain.age >= grain.length; });
        if (available == grains.end() || history[0].empty()) return;
        auto& grain = *available;
        grain = Grain{};
        grain.length = std::max(4, int(std::llround(fs * state.size * .001)));
        grain.ratio = std::exp2(state.pitch / 12.);
        // Pitch-up grains must never overtake the recording head. Frozen grains
        // must fit completely before its stationary boundary as well.
        const double safety = (freeze ? grain.ratio : std::max(0., grain.ratio - 1)) * grain.length + fs * .012 + 4;
        // Frozen grains need a small independent start spread even at zero
        // scatter; identical periodic starts can otherwise cancel a held tone
        // when their windows overlap at evenly spaced phases.
        const double jitter = unitRandom() * fs * std::max(freeze ? .02 : 0., 1.2 * state.scatter * .01);
        const double delay = std::min(double(history[0].size()) - 5, safety + jitter);
        grain.position = double(writePosition) - delay;
        while (grain.position < 0) grain.position += double(history[0].size());
        const double pan = (unitRandom() * 2 - 1) * state.width * .01;
        grain.leftGain = std::sqrt(2.) * std::cos((pan + 1) * pi * .25);
        grain.rightGain = std::sqrt(2.) * std::sin((pan + 1) * pi * .25);
    }
    double readHistory(int channel, double position, bool highQuality) const noexcept {
        const auto& buffer = history[channel];
        const int size = int(buffer.size()), first = int(position);
        const double fraction = position - first;
        const int second = first + 1 < size ? first + 1 : 0;
        const double b = buffer[first], c = buffer[second];
        if (!highQuality) return b + fraction * (c - b);
        const double a = buffer[first > 0 ? first - 1 : size - 1];
        const double d = buffer[second + 1 < size ? second + 1 : 0];
        const double cubic = b + .5 * fraction * (c - a + fraction * (2 * a - 5 * b + 4 * c - d + fraction * (3 * (b - c) + d - a)));
        // Monotonic bound prevents interpolation overshoot entering feedback.
        return std::clamp(cubic, std::min({ a,b,c,d }), std::max({ a,b,c,d }));
    }
    std::array<double, 2> granular(const std::array<double, 2>& input, bool freeze, bool highQuality) noexcept {
        if (history[0].empty()) return {};
        if (freeze != frozen) {
            // Retire running grains over 8 ms when the record head changes state;
            // new grains begin at zero under their own Hann windows.
            for (auto& grain : grains) if (grain.age < grain.length) {
                grain.release = grain.releaseLength = std::max(1, int(fs * .008));
            }
            frozen = freeze; untilGrain = 0;
        }
        if (untilGrain <= 0) { startGrain(freeze); untilGrain += fs / state.density; }
        untilGrain -= 1;
        std::array<double, 2> output {}, weight {};
        for (auto& grain : grains) {
            if (grain.age >= grain.length) continue;
            const double windowPosition = double(grain.age) / (grain.length - 1) * (window.size() - 1);
            const int index = std::min(int(window.size()) - 2, int(windowPosition));
            double amplitude = window[index] + (windowPosition - index) * (window[index + 1] - window[index]);
            if (grain.releaseLength > 0) {
                amplitude *= double(grain.release) / grain.releaseLength;
                if (--grain.release <= 0) { grain.age = grain.length; continue; }
            }
            const double gainLeft = amplitude * grain.leftGain, gainRight = amplitude * grain.rightGain;
            output[0] += readHistory(0, grain.position, highQuality) * gainLeft;
            output[1] += readHistory(1, grain.position, highQuality) * gainRight;
            weight[0] += gainLeft; weight[1] += gainRight;
            grain.position += grain.ratio;
            if (grain.position >= double(history[0].size())) grain.position -= double(history[0].size());
            ++grain.age;
        }
        // Per-channel convex weighting caps feedback loop gain independently of
        // density, window overlap, panning and pitch. Maximum feedback is 0.70.
        for (int channel = 0; channel < 2; ++channel) output[channel] /= std::max(1., weight[channel]);
        if (!freeze) {
            const double ratio = std::max(1., std::exp2(state.pitch / 12.));
            const double cutoff = std::min(18000., fs * .42 / ratio);
            const double recordCoefficient = 1 - std::exp(-2 * pi * cutoff / fs);
            const double feedbackCoefficient = 1 - std::exp(-2 * pi * std::min(8000., fs * .3) / fs);
            for (int channel = 0; channel < 2; ++channel) {
                feedbackLow[channel] += feedbackCoefficient * (output[channel] - feedbackLow[channel]);
                const double recordInput = input[channel] + state.feedback * .01 * feedbackLow[channel];
                recordLow[channel] += recordCoefficient * (recordInput - recordLow[channel]);
                history[channel][writePosition] = clean(float(recordLow[channel]));
            }
            if (++writePosition >= int(history[0].size())) writePosition = 0;
            recorded = std::min(recorded + 1, int(history[0].size()));
        }
        return output;
    }
    std::array<double, 2> pulse(const std::array<double, 2>& input, double beats, const Settings& target) noexcept {
        const double stepBeats = 1. / double(1 << target.division);
        double position = std::fmod(beats / stepBeats + state.phase * .16, 16.);
        if (position < 0) position += 16;
        const int pair = int(position / 2.);
        const double pairPosition = position - pair * 2;
        pulseStep = std::clamp(pair * 2 + (pairPosition >= 1 + state.swing * .01 ? 1 : 0), 0, 15);
        const double destination = target.steps[std::size_t(pulseStep)];
        const double coefficient = 1 - std::exp(-1 / (fs * state.smooth * .001));
        gate += coefficient * (destination - gate);
        if (std::abs(gate - destination) < 1.e-12) gate = destination;
        const double gain = 1 - .01 * state.depth * (1 - gate);
        return { input[0] * gain, input[1] * gain };
    }

    Kind kind;
    double fs = 48000, parameterCoefficient = .001, untilGrain = 0, freeBeats = 0, gate = 0;
    double attack = .01, release = .001, carrierFollow = .001, breathCoefficient = .2;
    bool initialized = false, frozen = false;
    int writePosition = 0, recorded = 0, pulseStep = 0, previousCarrier = -1;
    std::uint32_t random = 0x7157ab19u, controlCounter = 0;
    Settings state;
    std::array<std::vector<float>, 2> history;
    std::array<Grain, maxGrains> grains {};
    std::array<float, 4097> window {};
    std::array<std::array<Bandpass, bands>, 2> analysis {}, synthesis {};
    std::array<std::array<double, bands>, 2> envelopes {}, carrierEnvelopes {};
    std::array<double, bands> analysisG {}, synthesisG {}, tilt {};
    std::array<double, 5> oscillatorPhase {};
    std::array<double, 4> carrierBlend {};
    std::array<double, 2> breathLow {}, recordLow {}, feedbackLow {};
};
} // namespace gill::texture
