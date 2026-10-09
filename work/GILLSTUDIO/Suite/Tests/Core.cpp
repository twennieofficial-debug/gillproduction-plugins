#include "../Source/Engine.h"
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
using namespace studio;
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
struct Setup {
  Kind kind;
  Definition defs;
  std::array<float, 1024> data{};
  explicit Setup(Kind k) : kind(k), defs(definition(k)) {
    for (size_t i = 0; i < defs.params.size(); ++i)
      data[i] = defs.params[i].def;
  }
  void set(const char *key, float value, int group = 0) {
    for (size_t i = 0; i < defs.params.size(); ++i)
      if (defs.params[i].key == key && defs.params[i].group == group) {
        data[i] = value;
        return;
      }
    throw std::runtime_error(std::string("missing parameter ") + key);
  }
  Values view() const { return {defs, data}; }
};
std::vector<float> render(const Setup &s, double hz, double amplitude,
                          bool live = true, int length = 24000) {
  auto engine = std::make_unique<Engine>(s.kind);
  engine->prepare(48000, 2, live);
  engine->update(s.view());
  std::vector<float> l(size_t(length), 0), r(size_t(length), 0);
  for (int i = 0; i < length; ++i)
    l[size_t(i)] = r[size_t(i)] =
        float(amplitude * std::sin(2 * pi * hz * i / 48000.));
  float *ptr[]{l.data(), r.data()};
  engine->process(ptr, length);
  return l;
}
double amplitude(const std::vector<float> &x, double hz) {
  double a = 0, b = 0;
  const size_t start = x.size() / 2;
  for (size_t i = start; i < x.size(); ++i) {
    a += x[i] * std::sin(2 * pi * hz * i / 48000.);
    b += x[i] * std::cos(2 * pi * hz * i / 48000.);
  }
  return 2 * std::hypot(a, b) / double(x.size() - start);
}
void bands() {
  for (Kind k : {Kind::Multi, Kind::Precision, Kind::Deess}) {
    Setup s(k);
    const int count = k == Kind::Multi ? 6 : k == Kind::Precision ? 5 : 2;
    for (int b = 1; b <= count; ++b) {
      s.set("ratio", 1, b);
      s.set("gain", 0, b);
    }
    for (double f : {80., 300., 1000., 3000., 8000., 16000.}) {
      auto neutral = render(s, f, .2);
      check(std::abs(amplitude(neutral, f) - .2) < .0001,
            "unity bands alter level");
    }
    for (int b = 1; b <= count; ++b) {
      s.set("ratio", 20, b);
      s.set("threshold", -48, b);
      s.set("attack", .05f, b);
      s.set("release", 10, b);
    }
    for (double f : {80., 300., 1000., 3000., 8000., 16000.}) {
      auto reduced = render(s, f, .2);
      const auto ratio = amplitude(reduced, f) / .2;
      std::cout << names[int(k)] << " " << f << " ratio " << ratio << "\n";
      check(ratio < 1.01, "compression creates a settled frequency boost");
    }
  }
}
void eq() {
  Setup s(Kind::Eq825);
  auto a = render(s, 2500, .1);
  s.set("mid", 6, 1);
  s.set("mid", 6, 2);
  auto b = render(s, 2500, .1);
  const double db = 20 * std::log10(amplitude(b, 2500) / amplitude(a, 2500));
  std::cout << "EQ center gain " << db << " dB\n";
  check(std::abs(db - 6) < .04, "EQ center gain is not calibrated");
}
void excite() {
  for (Kind k : {Kind::Harmonic, Kind::Vital, Kind::Tape}) {
    Setup s(k);
    if (k == Kind::Vital) {
      s.set("amount", 100, 3);
      s.set("drive", 24, 3);
    }
    if (k == Kind::Harmonic) {
      s.set("prelow", 20);
      s.set("postlow", 20);
      s.set("drive", 24);
      s.set("amount", 100);
    }
    if (k == Kind::Tape)
      s.set("drive", 24);
    auto x = render(s, 1000, .3);
    const double harmonic = amplitude(x, 3000);
    std::cout << names[int(k)] << " harmonic " << harmonic << "\n";
    check(harmonic > .0001, "saturator generates no measured harmonic");
  }
}
void isolatedBands() {
  for (int active = 1; active <= 6; ++active) {
    Setup s(Kind::Multi);
    for (int b = 1; b <= 6; ++b) {
      s.set("ratio", b == active ? 20 : 1, b);
      s.set("threshold", -60, b);
      s.set("attack", .05f, b);
      s.set("release", 10, b);
      s.set("rms", 0, b);
    }
    for (double f : {60., 180., 500., 1200., 3500., 8000., 16000.}) {
      const double ratio = amplitude(render(s, f, .2), f) / .2;
      if (ratio > 1.01)
        std::cerr << "Isolated band " << active << " at " << f
                  << " Hz: " << ratio << "\n";
      check(ratio < 1.01, "isolated band compression creates a settled boost");
    }
  }
}
int main() {
  std::cout << std::unitbuf;
  try {
    bands();
    isolatedBands();
    eq();
    excite();
    std::cout << "STUDIO_CORE_AUDIO_PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
