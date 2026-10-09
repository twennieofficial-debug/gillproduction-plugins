#define main shared_core_main
#include "Core.cpp"
#undef main
#include "../Source/IrFit.h"
double energy(const std::vector<float> &x, size_t first, size_t end) {
  double sum = 0;
  for (size_t i = first; i < end; ++i)
    sum += double(x[i]) * x[i];
  return sum;
}
std::vector<float> room(bool freeze, bool extra, float decayColour = 1) {
  Setup s(Kind::Room);
  s.set("dry", 0);
  s.set("decay", 1);
  s.set("distance", 0);
  s.set("brightness", 100);
  s.set("decay2", decayColour, 7);
  auto e = std::make_unique<Engine>(Kind::Room);
  e->prepare(48000, 2, true);
  e->update(s.view());
  std::vector<float> a(192000, 0), b(a.size(), 0);
  a[0] = b[0] = .5f;
  for (size_t offset = 0; offset < a.size(); offset += 480) {
    if (offset == 24000 && freeze) {
      s.set("freeze", 1);
      e->update(s.view());
    }
    if (offset == 48000 && extra)
      a[offset] = b[offset] = .5f;
    float *ptr[]{a.data() + offset, b.data() + offset};
    e->process(ptr, 480);
  }
  return a;
}
void roomTests() {
  const auto plain = room(false, false), held = room(true, false),
             injected = room(true, true), longer = room(false, false, 3);
  const double heldLate = energy(held, 120000, 168000),
               heldEarly = energy(held, 48000, 96000);
  std::cout << "Frozen energy late/early " << heldLate / heldEarly << "\n";
  check(heldEarly > 1e-10 && heldLate / heldEarly > .75 &&
            heldLate / heldEarly < 1.3,
        "freeze does not maintain bounded tail energy");
  double difference = 0;
  for (size_t i = 50000; i < held.size(); ++i)
    difference = std::max(difference, std::abs(double(held[i] - injected[i])));
  check(difference < 1e-7, "freeze admits fresh input");
  check(energy(longer, 72000, 144000) > energy(plain, 72000, 144000) * 2,
        "decay color does not extend late tail");
  std::cout << "ROOM_FREEZE_DECAY_PASS\n";
}
std::vector<float> rackAudio(int module, bool swapped = false) {
  Setup s(Kind::Rack);
  s.set("module", float(module), 1);
  s.set("amount", 65, 1);
  s.set("low", 3, 1);
  s.set("high", 2, 1);
  s.set("threshold", -30, 1);
  s.set("attackamount", 8, 1);
  s.set("sustain", -6, 1);
  s.set("wet", 55, 1);
  s.set("module", 6, 2);
  s.set("amount", 70, 2);
  s.set("order", swapped ? 2.f : 1.f, 1);
  s.set("order", swapped ? 1.f : 2.f, 2);
  auto e = std::make_unique<Engine>(Kind::Rack);
  e->prepare(48000, 2, true);
  e->update(s.view());
  std::vector<float> a(12000), b(12000);
  for (size_t i = 0; i < a.size(); ++i)
    a[i] = b[i] = float(.3 * std::sin(i * .13) * (i % 2000 < 1300 ? 1 : .01));
  float *ptr[]{a.data(), b.data()};
  e->process(ptr, int(a.size()));
  return a;
}
void rackTests() {
  for (int m = 0; m < rackModelCount; ++m) {
    auto x = rackAudio(m);
    double peak = 0;
    for (float v : x) {
      check(std::isfinite(v), "rack module non-finite");
      peak = std::max(peak, std::abs(double(v)));
    }
    check(peak < 4, "rack module extreme unexpected output");
    std::cout << rackModels[m].name << " peak " << peak << "\n";
  }
  const auto before = rackAudio(1), after = rackAudio(1, true);
  double delta = 0;
  for (size_t i = 0; i < before.size(); ++i)
    delta += std::abs(before[i] - after[i]);
  check(delta > 1,
        "reordering EQ and saturation does not change processing order");
  std::cout << "RACK_ALL_MODULES_PASS\n";
}
void transientTests() {
  Setup s(Kind::Transient);
  s.set("amount", 80);
  s.set("sharpness", 0);
  auto soft = render(s, 2500, .3);
  s.set("sharpness", 100);
  auto sharp = render(s, 2500, .3);
  double delta = 0;
  for (size_t i = 0; i < soft.size(); ++i)
    delta += std::abs(soft[i] - sharp[i]);
  std::cout << "Transient sharpness change " << delta << "\n";
  check(delta > 1e-4, "transient sharpness is inert");
}
void irTests() {
  const double fs = 48000, target = 1.6;
  std::vector<float> ir(144000, 0);
  std::mt19937 random(724);
  std::uniform_real_distribution<float> noise(-1, 1);
  for (size_t i = 960; i < ir.size(); ++i)
    ir[i] = noise(random) * float(std::pow(10., -3.0 * double(i - 960) / fs / target));
  const auto fit = fitImpulse(ir, fs);
  std::cout << "IR fitted decay " << fit.decay << " pre-delay " << fit.predelay
            << "\n";
  check(fit.valid && std::abs(fit.decay - target) < .12 &&
            std::abs(fit.predelay - 20) < 2,
        "IR decay fit inaccurate");
  std::fill(ir.begin(), ir.end(), 0);
  check(!fitImpulse(ir, fs).valid, "silent IR accepted");
}
void loudnessTests() {
  Setup s(Kind::Max);
  auto e = std::make_unique<Engine>(Kind::Max);
  e->prepare(48000, 2, true);
  e->update(s.view());
  std::vector<float> a(192000), b(a.size());
  for (size_t i = 0; i < a.size(); ++i)
    a[i] = b[i] = float(.1 * std::sin(2 * pi * 1000 * i / 48000.));
  float *ptr[]{a.data(), b.data()};
  e->process(ptr, int(a.size()));
  const auto levels = e->loudness();
  std::cout << "1 kHz stereo -20 dBFS loudness " << levels[0] << " / "
            << levels[1] << "\n";
  check(std::abs(levels[0] + 20) < .2, "K-weighted loudness calibration");
  check(std::abs(levels[0] - levels[1]) < .15,
        "limiter unity loudness changed");
}
int main() {
  std::cout << std::unitbuf;
  try {
    roomTests();
    rackTests();
    transientTests();
    irTests();
    loudnessTests();
    std::cout << "STUDIO_MECHANISMS_PASS\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
