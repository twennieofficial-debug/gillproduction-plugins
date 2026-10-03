#include "../Source/TextureDSP.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>

namespace {
std::atomic<bool> countAllocations { false };
std::atomic<std::size_t> allocations { 0 };
}
void* operator new(std::size_t bytes) {
    if (countAllocations.load(std::memory_order_relaxed)) allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace gill::texture;
namespace {
int failures = 0, checks = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
double energy(const std::vector<float>& x, int begin = 0, int end = -1) {
    if (end < 0) end = int(x.size());
    double sum = 0; for (int i = begin; i < end; ++i) sum += double(x[i]) * x[i];
    return sum / std::max(1, end - begin);
}
double peak(const std::vector<float>& x) {
    double value = 0; for (auto sample : x) value = std::max(value, double(std::abs(sample))); return value;
}
bool finite(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float sample) { return std::isfinite(sample) && std::abs(sample) <= 16; });
}
std::vector<float> tone(double fs, double seconds, double frequency, double amplitude = .3) {
    std::vector<float> x(std::size_t(std::llround(fs * seconds)));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(amplitude * std::sin(2 * pi * frequency * double(i) / fs));
    return x;
}
double spectralAmplitude(const std::vector<float>& x, double fs, double frequency, int begin) {
    double re = 0, im = 0;
    for (int i = begin; i < int(x.size()); ++i) {
        const double angle = 2 * pi * frequency * i / fs;
        re += x[i] * std::cos(angle); im += x[i] * std::sin(angle);
    }
    return 2 * std::hypot(re, im) / std::max(1, int(x.size()) - begin);
}
void render(Engine& engine, std::vector<float>& left, std::vector<float>* right, const Settings& s,
            double fs, int block = 127, const std::vector<float>* carrier = nullptr,
            double bpm = 120, double start = 0, bool sync = true, bool playing = true) {
    for (int offset = 0; offset < int(left.size()); offset += block) {
        const int count = std::min(block, int(left.size()) - offset);
        engine.process(left.data() + offset, right ? right->data() + offset : nullptr,
            carrier ? carrier->data() + offset : nullptr, nullptr, count, s, bpm,
            start + (playing ? offset / fs * bpm / 60. : 0.), sync, playing);
    }
}
void commonTests(double fs) {
    const auto rate = std::to_string(int(fs));
    for (Kind kind : { Kind::Vocode, Kind::Grain, Kind::Pulse }) {
        const auto label = rate + " kind=" + std::to_string(int(kind));
        Settings s; s.scatter = 0; s.density = 25; s.size = 140;
        Engine engine(kind); engine.prepare(fs, 2048);
        check(engine.latencySamples() == 0, label + " causal latency contract");
        std::vector<float> silence(std::size_t(fs * .2), 0.f), silenceRight = silence;
        render(engine, silence, &silenceRight, s, fs);
        check(peak(silence) == 0 && peak(silenceRight) == 0, label + " silence has no self-generated output");

        auto original = tone(fs, .7, 233.);
        auto a = original, b = original, ar = original, br = original;
        engine.reset(); Engine second(kind); second.prepare(fs, 17);
        render(engine, a, &ar, s, fs, 1);
        render(second, b, &br, s, fs, 509);
        double difference = 0;
        for (std::size_t i = 0; i < a.size(); ++i) difference = std::max(difference, double(std::abs(a[i] - b[i])));
        check(difference < 2.e-5, label + " independent of host block size");
        check(finite(a) && finite(ar) && energy(a) > 1.e-7, label + " default wet effect is audible and finite");

        auto repeat = original, repeatRight = original;
        engine.reset(); render(engine, repeat, &repeatRight, s, fs, 83);
        check(a == repeat && ar == repeatRight, label + " reset is deterministic");

        Settings extremes = s; extremes.note = 84; extremes.formant = 12; extremes.response = 0;
        extremes.brightness = 100; extremes.unvoiced = 100; extremes.size = 240; extremes.pitch = 12;
        extremes.density = 30; extremes.scatter = 100; extremes.feedback = 70; extremes.width = 100;
        extremes.depth = 100; extremes.smooth = 1; extremes.swing = 50; extremes.phase = 100;
        auto stress = tone(fs, .25, fs * .41, 15.9); stress[0] = std::numeric_limits<float>::infinity();
        stress[1] = std::numeric_limits<float>::quiet_NaN(); stress[2] = -std::numeric_limits<float>::infinity();
        render(engine, stress, nullptr, extremes, fs, 127);
        check(finite(stress), label + " extreme automation and nonfinite audio are contained");
        extremes.note = extremes.size = extremes.smooth = std::numeric_limits<float>::quiet_NaN();
        extremes.steps[0] = std::numeric_limits<float>::infinity();
        render(engine, stress, nullptr, extremes, fs, 41, nullptr, std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::infinity());
        check(finite(stress), label + " invalid parameters and transport are contained");

        // Check the complete real-time call, including a settings transition.
        std::array<float, 256> small {}, smallRight {}, external {};
        small.fill(.2f); smallRight.fill(-.1f); external.fill(.3f);
        const auto before = allocations.load(); countAllocations = true;
        engine.process(small.data(), smallRight.data(), external.data(), external.data(), int(small.size()), s, 173, 7, true, true);
        countAllocations = false;
        check(allocations.load() == before, label + " audio call makes zero heap allocations");
        engine.reset(); std::fill(silence.begin(), silence.end(), 0.f);
        render(engine, silence, nullptr, s, fs);
        check(peak(silence) == 0, label + " reset removes all audio memory");
    }
}
void vocoderTests(double fs) {
    const auto label = std::to_string(int(fs)) + " vocoder ";
    Settings s; s.carrier = 3; s.unvoiced = 0; s.response = 0;
    auto low = tone(fs, .6, 240), high = tone(fs, .6, 2400), carrier = tone(fs, .6, 240, .3);
    auto carrierHigh = tone(fs, .6, 2400, .3);
    for (std::size_t i = 0; i < carrier.size(); ++i) carrier[i] += carrierHigh[i];
    Engine lowEngine(Kind::Vocode), highEngine(Kind::Vocode);
    lowEngine.prepare(fs); highEngine.prepare(fs);
    render(lowEngine, low, nullptr, s, fs, 133, &carrier);
    render(highEngine, high, nullptr, s, fs, 133, &carrier);
    const int begin = int(fs * .25);
    const double lowLow = spectralAmplitude(low, fs, 240, begin), lowHigh = spectralAmplitude(low, fs, 2400, begin);
    const double highLow = spectralAmplitude(high, fs, 240, begin), highHigh = spectralAmplitude(high, fs, 2400, begin);
    check(lowLow > lowHigh * 4 && highHigh > highLow * 4, label + " independent band envelopes transfer the modulator spectrum");
    check(lowLow > .01 && highHigh > .01, label + " external-carrier output has useful level");

    auto noCarrier = tone(fs, .4, 240); s.unvoiced = 100;
    lowEngine.reset(); render(lowEngine, noCarrier, nullptr, s, fs);
    check(peak(noCarrier) == 0, label + " missing external carrier remains silent including consonants");
    for (int choice = 0; choice < 3; ++choice) {
        s.carrier = choice; s.unvoiced = 0;
        auto speech = tone(fs, .4, 330); lowEngine.reset(); render(lowEngine, speech, nullptr, s, fs);
        check(energy(speech, int(fs * .1)) > .00001, label + " internal carrier " + std::to_string(choice) + " works without sidechain");
    }
    s.carrier = 0;
    auto quick = tone(fs, .6, 240), slow = quick;
    std::fill(quick.begin() + int(fs * .25), quick.end(), 0.f); slow = quick;
    s.response = 0; lowEngine.reset(); render(lowEngine, quick, nullptr, s, fs);
    s.response = 100; highEngine.reset(); render(highEngine, slow, nullptr, s, fs);
    check(energy(slow, int(fs * .37), int(fs * .47)) > 10 * energy(quick, int(fs * .37), int(fs * .47)),
          label + " response controls the envelope release");

    // A silent stereo channel cannot acquire the other channel's modulator.
    auto left = tone(fs, .3, 240); std::vector<float> right(left.size(), 0.f);
    lowEngine.reset(); render(lowEngine, left, &right, s, fs);
    check(peak(right) == 0 && energy(left) > 1.e-6, label + " stereo modulator channels remain independent");
}
void grainTests(double fs) {
    const auto label = std::to_string(int(fs)) + " grain ";
    Settings s; s.scatter = 0; s.feedback = 0; s.width = 0; s.size = 120; s.density = 30;
    Engine engine(Kind::Grain); engine.prepare(fs);
    std::vector<float> impulse(std::size_t(fs * .45), 0.f);
    const int onset = int(fs * .1); impulse[onset] = 1;
    render(engine, impulse, nullptr, s, fs);
    int first = -1; for (int i = 0; i < int(impulse.size()); ++i) if (std::abs(impulse[i]) > 1.e-8f) { first = i; break; }
    check(first >= onset + int(fs * .011) && first <= onset + int(fs * .014), label + " creative grain delay is causal and matches 12-ms safety history");
    check(peak(impulse) > .05, label + " grains actually replay recorded audio");
    check(engine.activeGrainCount() >= 3, label + " concurrent overlapping windowed grains");

    for (int semitones : { -12, 12 }) {
        s.pitch = float(semitones); s.density = 10; s.size = 90;
        auto sound = tone(fs, .9, 220); engine.reset(); render(engine, sound, nullptr, s, fs);
        const double expected = 220 * std::exp2(semitones / 12.);
        const double shifted = spectralAmplitude(sound, fs, expected, int(fs * .2));
        const double original = spectralAmplitude(sound, fs, 220, int(fs * .2));
        check(shifted > .025 && shifted > original * 4, label + " grain playback transposes " + std::to_string(semitones) + " semitones");
    }

    // A fast grain's reader moves twice as fast as the recorder. At maximum
    // length it still cannot catch the writer or expose future/overwritten data.
    s.pitch = 12; s.size = 240; s.density = 30;
    std::vector<float> fastImpulse(std::size_t(fs * .8), 0.f); fastImpulse[onset] = 1;
    engine.reset(); render(engine, fastImpulse, nullptr, s, fs);
    check(energy(fastImpulse, 0, onset + int(fs * .01)) == 0 && peak(fastImpulse) > .001,
          label + " maximum pitch-up grains remain behind the recording head");

    s.pitch = 0; s.size = 240; s.density = 30; s.scatter = 50; s.width = 100;
    auto stereo = tone(fs, 1., 173); auto stereoRight = stereo;
    engine.reset(); render(engine, stereo, &stereoRight, s, fs);
    double difference = 0; for (std::size_t i = 0; i < stereo.size(); ++i) difference += std::abs(stereo[i] - stereoRight[i]);
    check(difference / stereo.size() > .002, label + " scatter and grain panning create stereo texture");

    s.width = 0; s.scatter = 0; s.freeze = false;
    auto capture = tone(fs, .6, 220); engine.reset(); render(engine, capture, nullptr, s, fs);
    s.freeze = true; std::vector<float> frozen(std::size_t(fs * .5), 0.f); render(engine, frozen, nullptr, s, fs);
    check(energy(frozen, int(fs * .1)) > .001, label + " freeze sustains captured sound without new input");
    engine.reset(); std::fill(frozen.begin(), frozen.end(), 0.f); render(engine, frozen, nullptr, s, fs);
    check(peak(frozen) == 0, label + " reset clears frozen audio");
}
void pulseTests(double fs) {
    const auto label = std::to_string(int(fs)) + " pulse ";
    Settings s; s.smooth = 1; s.depth = 100; s.steps.fill(0); s.steps[0] = 1;
    Engine engine(Kind::Pulse); engine.prepare(fs);
    std::vector<float> dc(std::size_t(fs * .4), .5f); render(engine, dc, nullptr, s, fs);
    check(dc[int(fs * .06)] > .499 && dc[int(fs * .19)] < .00001, label + " sixteenth gate follows host PPQ at 120 BPM");
    check(engine.currentStep() == 3, label + " PPQ advances the 16-step playhead");

    auto input = tone(fs, .3, 721); auto unchanged = input;
    s.depth = 0; engine.reset(); render(engine, unchanged, nullptr, s, fs, 13, nullptr, 77, 33);
    check(input == unchanged, label + " zero depth passes current audio sample-for-sample with no delay");
    s.depth = 100; s.steps.fill(1); s.smooth = 1;
    auto transparent = tone(fs, .15, 721); auto reference = transparent;
    engine.reset(); render(engine, transparent, nullptr, s, fs, 11);
    check(std::equal(transparent.begin() + int(fs * .04), transparent.end(), reference.begin() + int(fs * .04)),
          label + " fully open gate passes current samples and never retriggers stored audio");
    std::vector<float> open(std::size_t(fs * .03), 1.f); engine.reset(); render(engine, open, nullptr, s, fs);
    s.steps.fill(0); s.steps[0] = 1;
    std::vector<float> seek(std::size_t(fs * .03), 1.f); render(engine, seek, nullptr, s, fs, 127, nullptr, 120, 1.75);
    check(seek.front() > .9 && seek.back() < .00001, label + " transport seek changes step with a continuous de-click envelope");

    s.swing = 50; std::vector<float> swung(std::size_t(fs * .02), 1.f);
    engine.reset(); render(engine, swung, nullptr, s, fs, 17, nullptr, 120, .32, true, false);
    check(swung.back() > .999 && engine.currentStep() == 0, label + " swing delays the odd sixteenth boundary");
    s.swing = 0; std::fill(swung.begin(), swung.end(), 1.f);
    engine.reset(); render(engine, swung, nullptr, s, fs, 17, nullptr, 120, .32, true, false);
    check(peak(swung) == 0 && engine.currentStep() == 1, label + " stopped host holds its supplied position");

    std::vector<float> synced(std::size_t(fs * .55), 1.f), free = synced;
    engine.reset(); render(engine, synced, nullptr, s, fs, 73, nullptr, 143, 0, true);
    engine.reset(); render(engine, free, nullptr, s, fs, 401, nullptr, 143, 0, false);
    double difference = 0; for (std::size_t i = 0; i < free.size(); ++i) difference = std::max(difference, double(std::abs(free[i] - synced[i])));
    check(difference < 1.e-5, label + " missing-host fallback uses the same BPM clock");

    s.phase = 100; std::fill(free.begin(), free.end(), 1.f);
    engine.reset(); render(engine, free, nullptr, s, fs, 73, nullptr, 143);
    check(free == synced, label + " 100-percent phase is a full pattern cycle");
    s.phase = 0; s.steps.fill(0); s.steps[15] = 1;
    std::vector<float> negative(std::size_t(fs * .02), 1.f);
    engine.reset(); render(engine, negative, nullptr, s, fs, 17, nullptr, 120, -.1, true, false);
    check(negative.back() > .999 && engine.currentStep() == 15, label + " negative PPQ wraps before the downbeat");
    for (int division = 0; division < 4; ++division) {
        s.division = division; s.steps.fill(0); s.steps[5] = 1;
        std::vector<float> held(std::size_t(fs * .03), 1.f);
        engine.reset(); render(engine, held, nullptr, s, fs, 71, nullptr, 193,
            5.25 / double(1 << division), true, false);
        check(held.back() > .999 && engine.currentStep() == 5,
              label + " musical division " + std::to_string(division) + " maps to quarter-note PPQ");
    }
}
void feedbackStability() {
    const double fs = 44100;
    Settings s; s.size = 240; s.density = 30; s.feedback = 70; s.width = 100; s.scatter = 0; s.pitch = 0;
    Engine engine(Kind::Grain); engine.prepare(fs);
    auto x = tone(fs, 5., 220, .5);
    std::fill(x.begin() + int(fs * .5), x.end(), 0.f);
    render(engine, x, nullptr, s, fs);
    check(peak(x) < 1.7, "grain maximum feedback has bounded loop gain at dense overlap");
    check(energy(x, int(fs * 4)) < energy(x, int(fs * .5), int(fs)) * .001,
          "grain maximum feedback decays after excitation stops");
}
}
int main() {
    for (double fs : { 44100., 48000., 96000., 192000. }) {
        commonTests(fs); vocoderTests(fs); grainTests(fs); pulseTests(fs);
        std::cout << "Completed causal DSP checks at " << int(fs) << " Hz\n";
    }
    feedbackStability();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
