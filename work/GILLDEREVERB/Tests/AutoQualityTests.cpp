// Standalone one-control quality/regression suite. It reuses only the tested
// WAV reader/render/measurement helpers, not the legacy suite's entry point.
#define main legacyRegressionEntryPoint
#include "DspTests.cpp"
#undef main
#define gilldereverb gilldereverb_v01
#include "fixtures/legacy-v0.1-DereverbDSP.h"
#undef gilldereverb

namespace {
struct AutoMeasure {
    std::string room;
    double amount = 0, drySdr = 0, dryLevel = 0, activeSdr = 0, activeImprovement = 0, activeLevel = 0, tailRaw = 0, tailNormalized = 0;
};
std::vector<AutoMeasure> autoResults;
std::vector<AutoMeasure> legacyResults;
std::vector<double> renderLegacy(const std::vector<double>& input, int fs) {
    gilldereverb_v01::DereverbEngine e; e.prepare(fs); e.setParameters(gilldereverb_v01::Params{});
    auto signal = input; signal.resize(signal.size() + 3072, 0);
    for (std::size_t offset = 0; offset < signal.size();) {
        const auto count = std::min<std::size_t>(127, signal.size() - offset); double* c[]{signal.data() + offset};
        e.process(c, 1, static_cast<int>(count)); offset += count; audioChannelSamples += count;
    }
    return {signal.begin() + 2048, signal.begin() + 2048 + static_cast<std::ptrdiff_t>(input.size())};
}
void offlineFFT(std::vector<std::complex<double>>& x, bool inverse) {
    const auto n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const auto step = std::polar(1.0, (inverse ? 2.0 : -2.0) * gilldereverb::pi / len);
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto a = x[i + j], b = x[i + j + len / 2] * w;
                x[i + j] = a + b; x[i + j + len / 2] = a - b; w *= step;
            }
        }
    }
    if (inverse) for (auto& v : x) v /= static_cast<double>(n);
}
std::vector<double> convolution(const std::vector<double>& x, const std::vector<double>& h) {
    std::size_t size = 1; while (size < x.size() + h.size() - 1) size *= 2;
    std::vector<std::complex<double>> a(size), b(size);
    for (std::size_t i = 0; i < x.size(); ++i) a[i] = x[i];
    for (std::size_t i = 0; i < h.size(); ++i) b[i] = h[i];
    offlineFFT(a, false); offlineFFT(b, false);
    for (std::size_t i = 0; i < size; ++i) a[i] *= b[i]; offlineFFT(a, true);
    std::vector<double> y(x.size()); for (std::size_t i = 0; i < y.size(); ++i) y[i] = a[i].real(); return y;
}
struct Fixture { std::string label; std::vector<double> wet, early; };
Fixture heldOutRoom(const std::vector<double>& dry, int fs, double rt60, double drr, std::uint64_t seed, const std::string& label) {
    std::mt19937_64 local(seed); std::normal_distribution<double> normal(0.0, 1.0);
    const auto size = static_cast<std::size_t>(fs * std::max(1.5, rt60 * 1.8)); std::vector<double> h(size, 0.0); h[0] = 1;
    const std::array<double, 5> delays{0.0067, 0.0143, 0.0239, 0.0351, 0.0473};
    const std::array<double, 5> gains{0.36, -0.23, 0.31, 0.13, -0.16};
    for (std::size_t i = 0; i < delays.size(); ++i) h[static_cast<std::size_t>(std::lround(delays[i] * fs))] = gains[i];
    const auto lateStart = static_cast<std::size_t>(0.050 * fs);
    double colour = 0;
    for (auto i = lateStart; i < size; ++i) {
        colour = 0.76 * colour + 0.24 * normal(local);
        h[i] = colour * std::exp(-6.907755278982137 * static_cast<double>(i - lateStart) / (fs * rt60)) * 0.045;
    }
    double e = 0; for (std::size_t i = 1; i < size; ++i) e += h[i] * h[i];
    const double scale = std::sqrt(std::pow(10.0, -drr / 10.0) / e);
    for (std::size_t i = 1; i < size; ++i) h[i] *= scale;
    auto early = h; std::fill(early.begin() + static_cast<std::ptrdiff_t>(lateStart), early.end(), 0.0);
    return {label, convolution(dry, h), convolution(dry, early)};
}
AutoMeasure measure(const std::string& room, double amountValue, const std::vector<double>& input, const std::vector<double>& output, const std::vector<double>& target, int fs) {
    const auto start = static_cast<std::size_t>(0.5 * fs), end = static_cast<std::size_t>(5.42 * fs), tail = static_cast<std::size_t>(5.50 * fs);
    AutoMeasure m; m.room = room; m.amount = amountValue;
    m.activeSdr = siSdr(output, target, start, end);
    m.activeImprovement = m.activeSdr - siSdr(input, target, start, end);
    m.activeLevel = 10.0 * std::log10(energy(output, start, end) / energy(input, start, end));
    m.tailRaw = 10.0 * std::log10(std::max(1e-30, energy(input, tail, input.size())) / std::max(1e-30, energy(output, tail, output.size())));
    m.tailNormalized = m.tailRaw + m.activeLevel;
    return m;
}
void quality(const std::filesystem::path& fixtureDir, const std::filesystem::path& outputDir, bool quick) {
    const auto dry = readWav(fixtureDir / "dry_48k.wav");
    const auto originalDryOutput = renderLegacy(dry.samples, dry.rate);
    const auto dryStart = static_cast<std::size_t>(0.5 * dry.rate), dryEnd = static_cast<std::size_t>(5.42 * dry.rate);
    const double oldDrySdr = siSdr(originalDryOutput, dry.samples, dryStart, dryEnd);
    const double oldDryLevel = 10.0 * std::log10(energy(originalDryOutput, dryStart, dryEnd) / energy(dry.samples, dryStart, dryEnd));
    std::vector<Fixture> fixtures;
    for (const std::string room : {"small", "medium", "large"}) fixtures.push_back({room, readWav(fixtureDir / ("wet_" + room + "_48k.wav")).samples, readWav(fixtureDir / ("early_target_" + room + "_48k.wav")).samples});
    if (!quick) {
        fixtures.push_back(heldOutRoom(dry.samples, dry.rate, 0.32, 3.0, 315019, "holdout_320ms_seed315019"));
        fixtures.push_back(heldOutRoom(dry.samples, dry.rate, 0.68, -1.0, 681131, "holdout_680ms_seed681131"));
        fixtures.push_back(heldOutRoom(dry.samples, dry.rate, 1.20, -4.0, 1207349, "holdout_1200ms_seed1207349"));
    }
    for (const auto& fixture : fixtures) {
        const auto oldOutput = renderLegacy(fixture.wet, dry.rate);
        const auto legacyApiOutput = render(fixture.wet, dry.rate, gilldereverb::Params{});
        require(legacyApiOutput == oldOutput, "current legacy Params API is bit-identical to archived0.1 engine");
        auto old = measure(fixture.label, 0.55, fixture.wet, oldOutput, fixture.early, dry.rate); old.drySdr = oldDrySdr; old.dryLevel = oldDryLevel; legacyResults.push_back(old);
    }
    for (double amountValue : {0.0, 0.25, 0.55, 0.65, 0.85, 1.0}) {
        const auto p = gilldereverb::autoParameters(amountValue);
        const auto dryOutput = render(dry.samples, dry.rate, p);
        const auto start = static_cast<std::size_t>(0.5 * dry.rate), end = static_cast<std::size_t>(5.42 * dry.rate);
        const double dryMetric = siSdr(dryOutput, dry.samples, start, end);
        const double dryLevel = 10.0 * std::log10(energy(dryOutput, start, end) / energy(dry.samples, start, end));
        const auto suffix = std::to_string(static_cast<int>(std::lround(amountValue * 100)));
        writeWav(outputDir / ("auto_dry_" + suffix + "_48k.wav"), dryOutput, dry.rate);
        if (amountValue == 0) require(dryOutput == dry.samples, "auto0 exact delayed identity");
        if (amountValue <= 0.65) { require(dryMetric >= 18.0, "automatic dry voice preservation through65%"); require(std::abs(dryLevel) <= 1.5, "automatic dry voice level through65%"); }
        else { require(dryMetric >= 10.0, "upper range keeps intelligible dry signal structure"); require(std::abs(dryLevel) <= 4.5, "upper range bounds dry voice level change"); }
        for (const auto& fixture : fixtures) {
            const auto output = render(fixture.wet, dry.rate, p);
            auto m = measure(fixture.label, amountValue, fixture.wet, output, fixture.early, dry.rate); m.drySdr = dryMetric; m.dryLevel = dryLevel;
            autoResults.push_back(m);
            if (amountValue > 0) require(m.activeImprovement > -0.05, "automatic active-room SI-SDR does not regress");
            if (amountValue >= 0.65) require(m.tailNormalized >= 2.0, "automatic65+ normalized tail reduction at least2dB");
            if (amountValue == 1.0) require(m.tailNormalized >= 4.0, "automatic100 normalized tail reduction at least4dB");
            writeWav(outputDir / ("auto_" + fixture.label + "_" + suffix + "_48k.wav"), output, dry.rate);
        }
    }
    for (const auto& fixture : fixtures) {
        double previousRaw = -1e10, previousNormalized = -1e10;
        for (const auto& m : autoResults) if (m.room == fixture.label) {
            require(m.tailRaw + 0.05 >= previousRaw, "amount curve monotonically increases raw tail reduction");
            require(m.tailNormalized + 0.10 >= previousNormalized, "amount curve monotonically increases normalized tail reduction");
            previousRaw = m.tailRaw; previousNormalized = m.tailNormalized;
        }
        const auto old = std::find_if(legacyResults.begin(), legacyResults.end(), [&](const auto& m) { return m.room == fixture.label; });
        const auto top = std::find_if(autoResults.begin(), autoResults.end(), [&](const auto& m) { return m.room == fixture.label && m.amount == 1.0; });
        require(top->tailNormalized >= old->tailNormalized + 2.0, "top control delivers material improvement over0.1default");
    }
}
struct ToneResult { double hz, amount, levelChange, siSdrDb; };
std::vector<ToneResult> toneResults;
void tones() {
    constexpr int fs = 48000, n = 2 * fs;
    for (double hz : {55.0, 110.0, 440.0, 1000.0, 4000.0, 12000.0, 17000.0}) {
        std::vector<double> x(n);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs;
            // Vibrato and slow level motion exercise a sustained singing-like
            // source instead of only a perfectly stationary test oscillator.
            const double phase = 2 * gilldereverb::pi * hz * t + (hz < 2000 ? 0.06 * std::sin(2 * gilldereverb::pi * 5.0 * t) : 0.0);
            const double envelope = std::min(1.0, t / 0.02) * std::min(1.0, (2.0 - t) / 0.02) * (0.15 + 0.015 * std::sin(2 * gilldereverb::pi * 2.3 * t));
            x[static_cast<std::size_t>(i)] = envelope * std::sin(phase);
        }
        for (double amountValue : {0.25, 0.65, 1.0}) {
            const auto y = render(x, fs, gilldereverb::autoParameters(amountValue));
            const auto start = static_cast<std::size_t>(0.3 * fs), end = static_cast<std::size_t>(1.8 * fs);
            const double change = 10.0 * std::log10(energy(y, start, end) / energy(x, start, end));
            const double metric = siSdr(y, x, start, end); toneResults.push_back({hz, amountValue, change, metric});
            require(std::abs(change) < (amountValue < 1 ? 1.6 : 3.5), "sustained vocal/air tone avoids collapse");
            require(metric > 25.0, "sustained tone modulation/phase preserved");
        }
    }
}
void automationMapping() {
    auto previous = gilldereverb::autoParameters(0);
    for (int i = 1; i <= 10000; ++i) {
        const auto p = gilldereverb::autoParameters(i / 10000.0);
        require(p.amount >= previous.amount && p.preserve <= previous.preserve && p.lateWeight >= previous.lateWeight && p.roomMs == previous.roomMs && p.lowHz == previous.lowHz && p.highHz == previous.highHz, "auto mapping continuous monotonic independent controls");
        require(std::isfinite(p.amount) && std::isfinite(p.preserve) && p.preserve >= 0 && p.preserve <= 1 && p.lateWeight >= 1 && p.lateWeight <= 1.9, "auto mapping finite safe ranges"); previous = p;
    }
    for (double x : {-999.0, 999.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        const auto p = gilldereverb::autoParameters(x); require(std::isfinite(p.amount) && p.amount >= 0 && p.amount <= 1, "automatic invalid control sanitized");
    }
    gilldereverb::DereverbEngine e; e.prepare(48000); std::array<double, 512> l{}, r{}; double* c[]{l.data(), r.data()};
    for (int i = 0; i < 3000; ++i) {
        e.setParameters(gilldereverb::autoParameters((i % 1001) / 1000.0));
        for (std::size_t j = 0; j < l.size(); ++j) { l[j] = uniform(-0.1, 0.1); r[j] = -l[j]; }
        e.process(c, 2, 512); audioChannelSamples += 1024;
        bool valid = true; for (std::size_t j = 0; j < l.size(); ++j) valid = valid && std::isfinite(l[j]) && std::isfinite(r[j]) && l[j] == -r[j];
        require(valid, "real-time auto-control sweep finite and stereo-linked");
    }
}
void printRow(const AutoMeasure& m) {
    std::cout << "{\"room\":\"" << m.room << "\",\"amount\":" << m.amount << ",\"dry_si_sdr_db\":" << m.drySdr << ",\"dry_level_db\":" << m.dryLevel
              << ",\"active_si_sdr_db\":" << m.activeSdr << ",\"active_improvement_db\":" << m.activeImprovement << ",\"active_level_db\":" << m.activeLevel
              << ",\"tail_raw_db\":" << m.tailRaw << ",\"tail_normalized_db\":" << m.tailNormalized << "}";
}
} // namespace

int main(int argc, char** argv) {
    const auto started = std::chrono::steady_clock::now(); bool quick = false; std::filesystem::path fixtures = "fixtures", outputDir = "auto-quality";
    for (int i = 1; i < argc; ++i) { if (std::string(argv[i]) == "--quick") quick = true; else fixtures = argv[i]; }
    try {
        std::filesystem::create_directories(outputDir); quality(fixtures, outputDir, quick); tones(); if (!quick) automationMapping();
    } catch (const std::exception& e) { require(false, e.what()); }
    std::cout << std::setprecision(12) << "{\n\"suite\":\"GILLDEREVERB0.2 automatic control\",\n\"passed\":" << (failures == 0 ? "true" : "false") << ",\n\"checks\":" << checks << ",\n\"failures\":" << failures << ",\n\"audio_channel_samples\":" << audioChannelSamples << ",\n\"legacy_default_reference\":[\n";
    for (std::size_t i = 0; i < legacyResults.size(); ++i) { if (i) std::cout << ",\n"; printRow(legacyResults[i]); }
    std::cout << "\n],\n\"automatic_results\":[\n";
    for (std::size_t i = 0; i < autoResults.size(); ++i) { if (i) std::cout << ",\n"; printRow(autoResults[i]); }
    std::cout << "\n],\n\"sustained_tones\":[\n";
    for (std::size_t i = 0; i < toneResults.size(); ++i) { if (i) std::cout << ",\n"; const auto& t = toneResults[i]; std::cout << "{\"hz\":" << t.hz << ",\"amount\":" << t.amount << ",\"level_change_db\":" << t.levelChange << ",\"si_sdr_db\":" << t.siSdrDb << "}"; }
    std::cout << "\n],\n\"elapsed_seconds\":" << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << ",\n\"scope\":\"One real clean voice plus deterministic original/new synthetic rooms and tones; no universal quality or human-listening claim\"\n}\n";
    return failures == 0 ? 0 : 1;
}
