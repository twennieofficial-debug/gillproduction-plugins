#include "../Source/DereverbDSP.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::mt19937_64 rng(0x47494c4c4452ULL);
std::size_t checks = 0, failures = 0, configurations = 0, audioChannelSamples = 0;
double maximumReconstructionError = 0, minimumRoomImprovement = 1000, minimumTailReduction = 1000, drySiSdr = 0, dryGain = 0;
double uniform(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }
void require(bool result, const std::string& label) {
    ++checks; if (!result) { if (failures < 25) std::cerr << "FAIL: " << label << '\n'; ++failures; }
}
double db(double x) { return 20.0 * std::log10(std::max(x, 1.0e-150)); }
struct Audio { int rate = 48000; std::vector<double> samples; };
std::uint16_t u16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t u32(const unsigned char* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
Audio readWav(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary); if (!f) throw std::runtime_error("Cannot read " + path.string());
    std::array<unsigned char, 12> header{}; f.read(reinterpret_cast<char*>(header.data()), 12);
    if (std::memcmp(header.data(), "RIFF", 4) || std::memcmp(header.data() + 8, "WAVE", 4)) throw std::runtime_error("Expected RIFF WAVE: " + path.string());
    int format = 0, channels = 0, rate = 0, bits = 0; std::vector<unsigned char> data;
    while (f) {
        std::array<unsigned char, 8> chunk{}; if (!f.read(reinterpret_cast<char*>(chunk.data()), 8)) break;
        const auto size = u32(chunk.data() + 4); if (size > 1000000000u) throw std::runtime_error("Invalid WAV chunk");
        std::vector<unsigned char> bytes(size); f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!std::memcmp(chunk.data(), "fmt ", 4) && size >= 16) {
            format = u16(bytes.data()); channels = u16(bytes.data() + 2); rate = static_cast<int>(u32(bytes.data() + 4)); bits = u16(bytes.data() + 14);
            if (format == 65534 && size >= 40) format = u16(bytes.data() + 24);
        } else if (!std::memcmp(chunk.data(), "data", 4)) data = std::move(bytes);
        if (size & 1) f.get();
    }
    if (channels < 1 || channels > 8 || rate < 8000 || data.empty() || (format != 1 && format != 3)) throw std::runtime_error("Unsupported WAV");
    const int bytesPer = bits / 8, stride = channels * bytesPer;
    if (bytesPer < 1 || bytesPer > 8) throw std::runtime_error("Unsupported WAV sample size");
    Audio audio; audio.rate = rate; audio.samples.resize(data.size() / static_cast<std::size_t>(stride));
    for (std::size_t i = 0; i < audio.samples.size(); ++i) for (int c = 0; c < channels; ++c) {
        const auto* p = data.data() + i * static_cast<std::size_t>(stride) + static_cast<std::size_t>(c * bytesPer); double v = 0;
        if (format == 3 && bits == 32) { float q; std::memcpy(&q, p, 4); v = q; }
        else if (format == 3 && bits == 64) std::memcpy(&v, p, 8);
        else if (format == 1 && bits == 16) v = static_cast<std::int16_t>(u16(p)) / 32768.0;
        else if (format == 1 && bits == 24) { std::int32_t q = p[0] | (p[1] << 8) | (p[2] << 16); if (q & 0x800000) q |= ~0xffffff; v = q / 8388608.0; }
        else if (format == 1 && bits == 32) v = static_cast<std::int32_t>(u32(p)) / 2147483648.0;
        else if (format == 1 && bits == 8) v = (p[0] - 128.0) / 128.0;
        else throw std::runtime_error("Unsupported WAV encoding");
        audio.samples[i] += v / channels;
    }
    return audio;
}
void writeU16(std::ofstream& f, std::uint16_t v) { char p[]{static_cast<char>(v), static_cast<char>(v >> 8)}; f.write(p, 2); }
void writeU32(std::ofstream& f, std::uint32_t v) { char p[]{static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24)}; f.write(p, 4); }
void writeWav(const std::filesystem::path& path, const std::vector<double>& x, int fs) {
    std::ofstream f(path, std::ios::binary); if (!f) throw std::runtime_error("Cannot write WAV");
    const auto bytes = static_cast<std::uint32_t>(x.size() * 4); f.write("RIFF", 4); writeU32(f, 36 + bytes); f.write("WAVEfmt ", 8); writeU32(f, 16); writeU16(f, 3); writeU16(f, 1); writeU32(f, static_cast<std::uint32_t>(fs)); writeU32(f, static_cast<std::uint32_t>(fs * 4)); writeU16(f, 4); writeU16(f, 32); f.write("data", 4); writeU32(f, bytes);
    for (double v : x) { const float sample = static_cast<float>(v); f.write(reinterpret_cast<const char*>(&sample), 4); }
}
std::vector<double> render(const std::vector<double>& input, int fs, const gilldereverb::Params& params) {
    gilldereverb::DereverbEngine e; e.prepare(fs); e.setParameters(params);
    auto signal = input; signal.resize(signal.size() + static_cast<std::size_t>(e.getLatencySamples()) + 1024, 0);
    for (std::size_t offset = 0; offset < signal.size();) {
        const auto count = std::min<std::size_t>(127, signal.size() - offset); double* channel[]{signal.data() + offset};
        e.process(channel, 1, static_cast<int>(count)); offset += count; audioChannelSamples += count;
    }
    return std::vector<double>(signal.begin() + e.getLatencySamples(), signal.begin() + e.getLatencySamples() + static_cast<std::ptrdiff_t>(input.size()));
}
double energy(const std::vector<double>& x, std::size_t start, std::size_t end) {
    double sum = 0; for (auto i = start; i < std::min(end, x.size()); ++i) sum += x[i] * x[i]; return sum;
}
double siSdr(const std::vector<double>& x, const std::vector<double>& reference, std::size_t start, std::size_t end) {
    end = std::min({end, x.size(), reference.size()}); double cross = 0, referencePower = 0;
    for (auto i = start; i < end; ++i) { cross += x[i] * reference[i]; referencePower += reference[i] * reference[i]; }
    const double scale = cross / std::max(1.0e-30, referencePower); double noise = 0;
    for (auto i = start; i < end; ++i) { const double d = x[i] - scale * reference[i]; noise += d * d; }
    return 10.0 * std::log10(std::max(1e-30, scale * scale * referencePower) / std::max(1e-30, noise));
}
struct RoomResult { std::string room; double activeBefore = 0, activeAfter = 0, totalBefore = 0, totalAfter = 0, tailReduction = 0, activeGain = 0; };
std::vector<RoomResult> roomResults;
struct RateQuality { int rate; double drySdr, wetImprovement, tailReduction; };
std::vector<RateQuality> rateResults;
std::vector<double> resampleFixture(const std::vector<double>& x, int sourceRate, int targetRate) {
    // Fixtures originated at16kHz and are band-limited well below every target
    // Nyquist. Aligned linear interpolation is sufficient for these tests.
    const auto n = static_cast<std::size_t>(std::llround(x.size() * static_cast<double>(targetRate) / sourceRate));
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double position = i * static_cast<double>(sourceRate) / targetRate;
        const auto a = std::min(x.size() - 1, static_cast<std::size_t>(position)), b = std::min(x.size() - 1, a + 1);
        const double fraction = position - static_cast<double>(a); y[i] = x[a] + fraction * (x[b] - x[a]);
    }
    return y;
}
void speechQuality(const std::filesystem::path& directory) {
    const auto dry = readWav(directory / "dry_48k.wav"); require(dry.rate == 48000, "speech fixture sample rate");
    gilldereverb::Params params;
    const auto processedDry = render(dry.samples, dry.rate, params);
    writeWav(directory / "processed_dry_48k.wav", processedDry, dry.rate);
    const auto start = static_cast<std::size_t>(0.5 * dry.rate), end = static_cast<std::size_t>(5.42 * dry.rate), tail = static_cast<std::size_t>(5.50 * dry.rate);
    drySiSdr = siSdr(processedDry, dry.samples, start, end);
    dryGain = 10.0 * std::log10(energy(processedDry, start, end) / energy(dry.samples, start, end));
    require(drySiSdr >= 18.0, "real dry voice SI-SDR >=18dB"); require(std::abs(dryGain) <= 1.5, "real dry voice level change <=1.5dB");
    for (const std::string room : {"small", "medium", "large"}) {
        const auto wet = readWav(directory / ("wet_" + room + "_48k.wav"));
        const auto targetPath = directory / ("early_target_" + room + "_48k.wav");
        const auto target = std::filesystem::exists(targetPath) ? readWav(targetPath) : dry;
        const auto processed = render(wet.samples, wet.rate, params);
        writeWav(directory / ("processed_" + room + "_48k.wav"), processed, wet.rate);
        RoomResult m; m.room = room;
        m.activeBefore = siSdr(wet.samples, target.samples, start, end); m.activeAfter = siSdr(processed, target.samples, start, end);
        m.totalBefore = siSdr(wet.samples, target.samples, 0, wet.samples.size()); m.totalAfter = siSdr(processed, target.samples, 0, wet.samples.size());
        m.tailReduction = 10.0 * std::log10(std::max(1e-30, energy(wet.samples, tail, wet.samples.size())) / std::max(1e-30, energy(processed, tail, processed.size())));
        m.activeGain = 10.0 * std::log10(energy(processed, start, end) / energy(wet.samples, start, end));
        minimumRoomImprovement = std::min(minimumRoomImprovement, m.activeAfter - m.activeBefore); minimumTailReduction = std::min(minimumTailReduction, m.tailReduction);
        roomResults.push_back(m);
        require(m.activeAfter - m.activeBefore > 0.10, room + " active-speech quality improves beyond broadband level change");
        require(m.tailReduction > 2.0, room + " tail reduction >2dB");
    }
    const auto medium = readWav(directory / "wet_medium_48k.wav");
    const auto early = readWav(directory / "early_target_medium_48k.wav");
    for (int fs : {44100, 96000, 192000}) {
        const auto dryAtRate = resampleFixture(dry.samples, dry.rate, fs);
        const auto wetAtRate = resampleFixture(medium.samples, medium.rate, fs);
        const auto targetAtRate = resampleFixture(early.samples, early.rate, fs);
        const auto dryOutput = render(dryAtRate, fs, params), wetOutput = render(wetAtRate, fs, params);
        const auto a = static_cast<std::size_t>(0.5 * fs), b = static_cast<std::size_t>(5.42 * fs), tailStart = static_cast<std::size_t>(5.50 * fs);
        RateQuality r{fs, siSdr(dryOutput, dryAtRate, a, b),
            siSdr(wetOutput, targetAtRate, a, b) - siSdr(wetAtRate, targetAtRate, a, b),
            10.0 * std::log10(std::max(1e-30, energy(wetAtRate, tailStart, wetAtRate.size())) / std::max(1e-30, energy(wetOutput, tailStart, wetOutput.size())))};
        rateResults.push_back(r);
        require(r.drySdr >= 18.0, "real dry voice preserved at " + std::to_string(fs));
        require(r.wetImprovement > 0.10, "active wet voice improves at " + std::to_string(fs));
        require(r.tailReduction > 2.0, "room tail reduction >2dB at " + std::to_string(fs));
    }
}

template<class Sample> void delayAndStereo() {
    constexpr int n = 20000;
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        gilldereverb::DereverbEngine e; e.prepare(fs); gilldereverb::Params p; p.amount = 0; e.setParameters(p);
        std::vector<Sample> l(n), r(n), dl(n), dr(n);
        for (int i = 0; i < n; ++i) { l[static_cast<std::size_t>(i)] = static_cast<Sample>(uniform(-0.5, 0.5)); r[static_cast<std::size_t>(i)] = static_cast<Sample>(uniform(-0.5, 0.5)); }
        const auto oldL = l, oldR = r;
        for (int offset = 0; offset < n;) {
            const int count = std::min(n - offset, 1 + (offset * 17) % 777); Sample* c[]{l.data() + offset, r.data() + offset}, *dry[]{dl.data() + offset, dr.data() + offset};
            e.process(c, 2, count, dry); offset += count; audioChannelSamples += static_cast<std::size_t>(count) * 2;
        }
        bool exact = true;
        for (int i = 0; i < n; ++i) {
            const auto expectedL = i < gilldereverb::fftSize ? Sample{} : oldL[static_cast<std::size_t>(i - gilldereverb::fftSize)];
            const auto expectedR = i < gilldereverb::fftSize ? Sample{} : oldR[static_cast<std::size_t>(i - gilldereverb::fftSize)];
            exact = exact && l[static_cast<std::size_t>(i)] == expectedL && r[static_cast<std::size_t>(i)] == expectedR && dl[static_cast<std::size_t>(i)] == expectedL && dr[static_cast<std::size_t>(i)] == expectedR;
        }
        require(exact, "amount0 exact delayed samples and alignedDry"); require(e.getLatencySamples() == 2048, "reported latency exact2048");
        p.amount = 1; e.prepare(fs); e.setParameters(p); l.assign(n, 0); r.assign(n, 0); l[0] = Sample{1}; Sample* c[]{l.data(), r.data()}; e.process(c, 2, n); audioChannelSamples += n * 2;
        double error = 0; bool rightSilent = true;
        for (int i = 0; i < n; ++i) { error = std::max(error, std::abs(static_cast<double>(l[static_cast<std::size_t>(i)]) - (i == 2048 ? 1.0 : 0.0))); rightSilent = rightSilent && r[static_cast<std::size_t>(i)] == 0; }
        maximumReconstructionError = std::max(maximumReconstructionError, error); require(error < 1e-7, "single impulse perfect WOLA reconstruction"); require(rightSilent, "zero cross-channel leakage");
        e.prepare(fs); e.setParameters(p);
        for (int i = 0; i < n; ++i) { const auto x = static_cast<Sample>(0.2 * std::sin(2 * gilldereverb::pi * 440.0 * i / fs) * (i % 7000 < 4500 ? 1.0 : 0.2)); l[static_cast<std::size_t>(i)] = x; r[static_cast<std::size_t>(i)] = -x; }
        e.process(c, 2, n); audioChannelSamples += n * 2;
        bool linked = true; for (int i = 0; i < n; ++i) linked = linked && l[static_cast<std::size_t>(i)] == -r[static_cast<std::size_t>(i)];
        require(linked, "linked stereo mask preserves exact anti-phase image");
        p.lowHz = p.highHz = 1000; e.prepare(fs); e.setParameters(p); l = oldL; r = oldR;
        e.process(c, 2, n); audioChannelSamples += n * 2; double allpassError = 0;
        for (int i = 0; i < n; ++i) {
            const double expected = i < 2048 ? 0.0 : static_cast<double>(oldL[static_cast<std::size_t>(i - 2048)]);
            allpassError = std::max(allpassError, std::abs(static_cast<double>(l[static_cast<std::size_t>(i)]) - expected));
        }
        require(allpassError < 1e-7, "equal LOW/HIGH gives perfect full-signal FFT reconstruction");
    }
}
void blockAndPrecision() {
    constexpr std::size_t n = 18000; std::vector<float> fl(n), fr(n); std::vector<double> dl(n), dr(n), sl(n), sr(n);
    for (std::size_t i = 0; i < n; ++i) { fl[i] = static_cast<float>(uniform(-0.2, 0.2)); fr[i] = static_cast<float>(uniform(-0.2, 0.2)); dl[i] = fl[i]; dr[i] = fr[i]; }
    sl = dl; sr = dr;
    gilldereverb::DereverbEngine f, d, split; f.prepare(48000); d.prepare(48000); split.prepare(48000); gilldereverb::Params p;
    f.setParameters(p); d.setParameters(p); split.setParameters(p); p.amount = 0.9; p.roomMs = 1200; p.preserve = 0.5; f.setParameters(p); d.setParameters(p); split.setParameters(p);
    float* fc[]{fl.data(), fr.data()}; double* dc[]{dl.data(), dr.data()}; f.process(fc, 2, static_cast<int>(n)); d.process(dc, 2, static_cast<int>(n));
    for (std::size_t offset = 0; offset < n;) {
        const auto count = std::min<std::size_t>(n - offset, 1 + offset % 513); double* c[]{sl.data() + offset, sr.data() + offset}; split.setParameters(p); split.process(c, 2, static_cast<int>(count)); offset += count;
    }
    require(dl == sl && dr == sr, "host block partition and repeated unchanged setParameters invariant");
    bool exact = true; for (std::size_t i = 0; i < n; ++i) exact = exact && fl[i] == static_cast<float>(dl[i]) && fr[i] == static_cast<float>(dr[i]);
    require(exact, "float path equals rounded double path"); audioChannelSamples += n * 6;
}
void randomizedRobustness() {
    std::array<double, 512> l{}, r{}, dl{}, dr{};
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        gilldereverb::DereverbEngine e; e.prepare(fs); double* channels[]{l.data(), r.data()}, *dry[]{dl.data(), dr.data()};
        for (int trial = 0; trial < 2400; ++trial) {
            gilldereverb::Params p; p.amount = uniform(0, 1); p.roomMs = uniform(80, 1500); p.preserve = uniform(0, 1); p.lowHz = uniform(20, 1000); p.highHz = uniform(1000, 20000);
            if (trial % 13 == 0) p.amount = 0; if (trial % 17 == 0) p.amount = 1;
            if (trial % 23 == 0) p.preserve = 1; if (trial % 29 == 0) p.preserve = 0;
            e.setParameters(p);
            const double amplitude = std::pow(10.0, uniform(-8.0, 0.5));
            for (std::size_t i = 0; i < l.size(); ++i) { l[i] = amplitude * (0.7 * std::sin(2.0 * gilldereverb::pi * (250 + trial % 3000) * (trial * 512.0 + i) / fs) + uniform(-0.3, 0.3)); r[i] = 0.37 * l[i]; }
            e.process(channels, 2, 512, dry); audioChannelSamples += 1024;
            bool finite = true; for (std::size_t i = 0; i < l.size(); ++i) finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]) && std::isfinite(dl[i]) && std::isfinite(dr[i]);
            require(finite, "randomized actual spectral audio finite"); require(std::isfinite(e.getReductionDb()) && e.getReductionDb() >= -1e-10 && e.getReductionDb() <= 24.0, "reduction meter bounded"); ++configurations;
        }
        e.reset(); gilldereverb::Params p; p.amount = std::numeric_limits<double>::quiet_NaN(); p.roomMs = std::numeric_limits<double>::infinity(); p.highHz = -300; p.lowHz = 1e10; p.preserve = -5; e.setParameters(p);
        l.fill(0); r.fill(0); l[0] = std::numeric_limits<double>::quiet_NaN(); r[0] = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 12; ++i) { e.process(channels, 2, 512); audioChannelSamples += 1024; }
        require(std::all_of(l.begin(), l.end(), [](double v) { return v == 0; }) && std::all_of(r.begin(), r.end(), [](double v) { return v == 0; }), "invalid parameters/nonfinite input/silence recover");
        e.process(static_cast<double**>(nullptr), 2, 512); e.process(channels, 0, 512); e.process(channels, 2, 0);
    }
}
void simultaneousSpeechAndTail() {
    // A previous low-frequency phoneme leaves a tail while a new high-frequency
    // phoneme remains loud. Broadband gating cannot selectively remove this tail.
    constexpr int fs = 48000, n = fs * 3;
    std::vector<double> signal(n), reference(n), tailOnly(n);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double earlier = t > 0.2 && t < 0.7 ? 0.15 * std::sin(2 * gilldereverb::pi * 400 * t) : 0;
        const double tail = t >= 0.7 ? 0.15 * std::exp(-6.907755278982137 * (t - 0.7) / 0.6) * std::sin(2 * gilldereverb::pi * 400 * t) : 0;
        const double voice = t > 0.75 && t < 1.35 ? 0.15 * std::sin(2 * gilldereverb::pi * 2200 * t) : 0;
        reference[static_cast<std::size_t>(i)] = earlier + voice; signal[static_cast<std::size_t>(i)] = earlier + voice + tail; tailOnly[static_cast<std::size_t>(i)] = tail;
    }
    gilldereverb::Params p; p.roomMs = 600;
    const auto output = render(signal, fs, p);
    auto measure = [&](const std::vector<double>& x, double hz) {
        std::complex<double> value{};
        for (int i = static_cast<int>(0.85 * fs); i < static_cast<int>(1.05 * fs); ++i) value += x[static_cast<std::size_t>(i)] * std::polar(1.0, -2.0 * gilldereverb::pi * hz * i / fs);
        return std::abs(value);
    };
    const double lowReduction = db(measure(signal, 400) / measure(output, 400));
    const double voiceChange = db(measure(output, 2200) / measure(signal, 2200));
    std::cerr << "OVERLAP tail_reduction_db=" << lowReduction << " voice_change_db=" << voiceChange << '\n';
    require(lowReduction > 2.0, "old phoneme tail suppressed while new voice remains present");
    require(std::abs(voiceChange) < 1.0, "simultaneous new voice preserved within1dB");
}
} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now(); bool quick = false; std::filesystem::path fixtures = "fixtures";
    for (int i = 1; i < argc; ++i) { if (std::string(argv[i]) == "--quick") quick = true; else fixtures = argv[i]; }
    try {
        speechQuality(fixtures); simultaneousSpeechAndTail();
        if (!quick) { delayAndStereo<float>(); delayAndStereo<double>(); blockAndPrecision(); randomizedRobustness(); }
    } catch (const std::exception& e) { require(false, e.what()); }
    std::cout << std::setprecision(12) << "{\n  \"suite\": \"GILLDEREVERB DSP\",\n  \"passed\": " << (failures == 0 ? "true" : "false")
              << ",\n  \"checks\": " << checks << ",\n  \"failures\": " << failures << ",\n  \"randomized_real_audio_configurations\": " << configurations
              << ",\n  \"audio_channel_samples\": " << audioChannelSamples << ",\n  \"latency_samples\": 2048"
              << ",\n  \"dry_voice_si_sdr_db\": " << drySiSdr << ",\n  \"dry_voice_level_change_db\": " << dryGain
              << ",\n  \"minimum_active_room_si_sdr_improvement_db\": " << minimumRoomImprovement << ",\n  \"minimum_tail_reduction_db\": " << minimumTailReduction
              << ",\n  \"maximum_wola_impulse_error\": " << maximumReconstructionError << ",\n  \"rooms\": [";
    for (std::size_t i = 0; i < roomResults.size(); ++i) {
        const auto& r = roomResults[i];
        std::cout << (i == 0 ? "\n" : ",\n") << "    {\"name\":\"" << r.room << "\",\"active_si_sdr_before_db\":" << r.activeBefore << ",\"active_si_sdr_after_db\":" << r.activeAfter
                  << ",\"total_si_sdr_before_db\":" << r.totalBefore << ",\"total_si_sdr_after_db\":" << r.totalAfter << ",\"tail_reduction_db\":" << r.tailReduction << ",\"active_level_change_db\":" << r.activeGain << "}";
    }
    std::cout << "\n  ],\n  \"additional_sample_rate_quality\": [";
    for (std::size_t i = 0; i < rateResults.size(); ++i) {
        const auto& r = rateResults[i]; std::cout << (i == 0 ? "\n" : ",\n") << "    {\"rate\":" << r.rate << ",\"dry_voice_si_sdr_db\":" << r.drySdr << ",\"active_wet_si_sdr_improvement_db\":" << r.wetImprovement << ",\"tail_reduction_db\":" << r.tailReduction << "}";
    }
    std::cout << "\n  ],\n  \"elapsed_seconds\": " << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << ",\n  \"scope\":\"Deterministic DSP, clean speech and synthetic room fixtures; no claim of perfect separation or universal room performance\"\n}\n";
    return failures == 0 ? 0 : 1;
}
