#pragma once
#include "../../../GILLEQ/Source/EqDSP.h"
#include "Parameters.h"
#include "Antialias19.h"
#include <cstdint>
namespace studio {
constexpr double pi = 3.14159265358979323846;
inline double clean(double v) noexcept {
  return std::isfinite(v) ? std::clamp(v, -1e6, 1e6) : 0;
}
inline double gain(double v) noexcept { return std::pow(10., v / 20); }
inline double pole(double ms, double fs) noexcept {
  return std::exp(-1 / (std::max(.01, ms) * .001 * fs));
}
struct Low {
  double z = 0;
  double tick(double x, double k) noexcept {
    const double v = (x - z) * k, y = v + z;
    z = std::abs(y + v) < 1e-25 ? 0 : y + v;
    return y;
  }
  static double coeff(double hz, double fs) noexcept {
    const auto t = std::tan(pi * std::clamp(hz, 5., fs * .45) / fs);
    return t / (1 + t);
  }
};
struct Filter {
  gill::Coefficients c;
  gill::detail::BiquadState state;
  void set(int type, double hz, double db, double q, double fs) noexcept {
    c = gill::designSection(type, std::clamp(hz, 5., fs * .49), db,
                            std::clamp(q, .1, 18.), fs);
  }
  double tick(double x) noexcept { return state.process(x, c); }
  void reset() noexcept { state.reset(); }
};
// Ordered complementary residual bands sum exactly to the input at unity.
struct Split {
  std::array<std::array<Low, 5>, 2> st{};
  std::array<double, 5> k{};
  int count = 5;
  void set(const std::array<double, 5> &f, int bands, double fs) noexcept {
    count = bands;
    auto sorted = f;
    std::sort(sorted.begin(), sorted.begin() + bands - 1);
    for (int i = 0; i < bands - 1; ++i)
      k[size_t(i)] = Low::coeff(sorted[size_t(i)], fs);
  }
  std::array<std::array<double, 2>, 6> tick(std::array<double, 2> x) noexcept {
    std::array<std::array<double, 2>, 6> out{};
    for (int c = 0; c < 2; ++c) {
      double last = 0;
      for (int b = 0; b < count - 1; ++b) {
        const double low =
            st[size_t(c)][size_t(b)].tick(x[size_t(c)], k[size_t(b)]);
        out[size_t(b)][size_t(c)] = low - last;
        last = low;
      }
      out[size_t(count - 1)][size_t(c)] = x[size_t(c)] - last;
    }
    return out;
  }
};
inline std::array<double, 2> width(std::array<double, 2> x, double w) noexcept {
  const double m = .5 * (x[0] + x[1]), s = .5 * (x[0] - x[1]) * w;
  return {m + s, m - s};
}
inline double shape(double x, int mode) noexcept {
  switch (mode) {
  case 1:
    return std::clamp(x, -1., 1.);
  case 2: {
    const double y = std::clamp(x, -1.5, 1.5);
    return y - y * y * y / 6.;
  }
  case 3: {
    const double y = std::clamp(x, -1.5, 1.5);
    return y - y * y * y / 6. + y * y * y * y * y / 120.;
  }
  default:
    return std::tanh(x);
  }
}
class Tone {
public:
  explicit Tone(Kind k) : kind(k) {}
  void prepare(double rate, int channels, bool liveMode, int factor=1) {
    fs = rate;
    oversampleFactor=factor;
    ch = channels;
    live = liveMode;
    for (auto &r : ring)
      r.assign(size_t(std::ceil(fs * .03)) + 8, 0);
    reset();
  }
  void reset() noexcept {
    for (auto &r : ring)
      std::fill(r.begin(), r.end(), 0);
    pos = 0;
    phase = 0;
    fast = slow = envelope = 0;
    split.st = {};
    low = {};
    for (auto &c : eq)
      for (auto &f : c)
        f.reset();
    for (auto &f : pre)
      f.reset();
    for (auto &f : post)
      f.reset();
    last = {};
    causal = {};
    dcIn = {};
    dcOut = {};
    subSign = 1;
    previousBand = 0;
  }
  int latency() const noexcept {
    return kind == Kind::Tape && !live ? oversampleFactor*int(std::ceil(fs/oversampleFactor * .004)) : 0;
  }
  void mode(bool v) noexcept {
    if (live != v) {
      live = v;
      reset();
    }
  }
  void update(const Values &v) noexcept {
    if (kind == Kind::Tape) {
      drive = gain(v("drive"));
      compensate = v("compensate") > .5;
      machine = int(v("machine"));
      form = int(v("tape"));
      speed = int(v("speed"));
      bias = (v("bias") - 1) * .08;
      calibration = gain(-18 - v("calibration"));
      wow = v("wow") * .01;
      flutter = v("flutter") * .01;
      noise = gain(v("noise"))*std::sqrt(double(oversampleFactor));
      hiss = v("hiss") > .5;
      automute = v("automute") > .5;
      for (int c = 0; c < 2; ++c) {
        eq[size_t(c)][0].set(gill::LowShelf, 70, v("bass"), .707, fs);
        eq[size_t(c)][1].set(gill::HighCut, speed ? 19000 : 14500, 0, .707, fs);
      }
    }
    if (kind == Kind::Vital) {
      direct = v("directoff") > .5 ? 0 : gain(v("direct"));
      punch = v("punch");
      std::array<double, 5> edges{};
      anySolo = false;
      for (int i = 0; i < 5; ++i) {
        const int g = i + 1;
        amounts[size_t(i)] = v("amount", g) * .01;
        drives[size_t(i)] = gain(v("drive", g));
        wetgains[size_t(i)] = gain(v("gain", g));
        widths[size_t(i)] = v("width", g) * .01;
        wetoff[size_t(i)] = v("bypass", g) > .5;
        solos[size_t(i)] = v("solo", g) > .5;
        anySolo |= solos[size_t(i)];
        if (i < 4)
          edges[size_t(i)] = v("edge", g);
      }
      split.set(edges, 5, fs);
    }
    if (kind == Kind::Harmonic) {
      amount = v("amount") * .01;
      drive = gain(v("drive"));
      shapeMode = int(v("shape"));
      subtle = v("subtle") > .5;
      subtleGain = gain(v("subtlegain"));
      even = v("even") * .01;
      adaptive = v("adaptive") > .5;
      mastering = v("mastering") > .5;
      wetWidth = v("width") * .01;
      channel = int(v("channel"));
      wetOnly = v("wetonly") > .5;
      const double a = std::min(v("prelow"), v("prehigh")),
                   b = std::max(v("prelow"), v("prehigh")),
                   c = std::min(v("postlow"), v("posthigh")),
                   d = std::max(v("postlow"), v("posthigh"));
      for (int i = 0; i < 2; ++i) {
        pre[size_t(i) * 2].set(gill::LowCut, a, 0, .707, fs);
        pre[size_t(i) * 2 + 1].set(gill::HighCut, b, 0, .707, fs);
        post[size_t(i) * 2].set(gill::LowCut, c, 0, .707, fs);
        post[size_t(i) * 2 + 1].set(gill::HighCut, d, 0, .707, fs);
      }
    }
    if (kind == Kind::Eq825) {
      ms = v("routing") > .5;
      thd = v("thd") * .01;
      calibration = gain(-18 - v("calibration"));
      cuts = v("cuts") > .5;
      monoBass = v("monobass") > .5;
      fill = v("filler") * .01;
      fillCoefficient = Low::coeff(v("fillfreq"), fs);
      const char *keys[] = {"low", "lowmid", "mid", "highmid", "high"};
      const char *freqs[] = {"lowfreq", "lowmidfreq", "midfreq", "highmidfreq",
                             "highfreq"};
      for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < 5; ++i) {
          const double db = v(keys[i], c + 1),
                       q = v("q", c + 1) * (1 + std::abs(db) / 12);
          eq[size_t(c)][size_t(i)].set(i == 0   ? gill::LowShelf
                                       : i == 4 ? gill::HighShelf
                                                : gill::Bell,
                                       v(freqs[i], c + 1), db, q, fs);
        }
        eq[size_t(c)][5].set(gill::LowCut, v("highpass"), 0, .707, fs);
        eq[size_t(c)][6].set(gill::HighCut, v("lowpass"), 0, .707, fs);
      }
    }
  }
  std::array<double, 2> sample(std::array<double, 2> x) noexcept {
    if (ch == 1)
      x[1] = x[0];
    for (auto &v : x)
      v = clean(v);
    const double peak = std::max(std::abs(x[0]), std::abs(x[1]));
    envelope += (1 - pole(20, fs)) * (peak * peak - envelope);
    if (kind == Kind::Tape) {
      const double delay =
          latency() + (!live ? (wow * fs * .0018 * std::sin(phase) +
                                flutter * fs * .00012 * std::sin(phase * 27.17))
                             : 0);
      phase += 2 * pi * .63 / fs;
      if (phase > 2 * pi * 1000)
        phase -= 2 * pi * 1000;
      std::array<double, 2> y{};
      for (int c = 0; c < 2; ++c) {
        auto &r = ring[size_t(c)];
        r[pos] = x[size_t(c)];
        double value = x[size_t(c)];
        if (!live) {
          const auto whole = size_t(delay);
          const double f = delay - whole;
          value = r[(pos + r.size() - whole) % r.size()] * (1 - f) +
                  r[(pos + r.size() - whole - 1) % r.size()] * f;
        }
        const double driveGain =
            drive * calibration * (machine ? .8 : 1.15) * (form ? .8 : 1.1);
        double out = live ? causal[size_t(c)].tick(value,driveGain,bias) :
          (std::tanh(value*driveGain+bias)-std::tanh(bias))/driveGain;
        if(!compensate) out*=driveGain/calibration;
        out = eq[size_t(c)][0].tick(out);
        out = eq[size_t(c)][1].tick(out);
        if (hiss && (!automute || envelope > 1e-10)) {
          rng ^= rng << 13;
          rng ^= rng >> 17;
          rng ^= rng << 5;
          out += (double(rng) / 2147483648. - 1) * noise;
        }
        y[size_t(c)] = dc(out, c);
      }
      pos = (pos + 1) % ring[0].size();
      return y;
    }
    if (kind == Kind::Vital) {
      auto bands = split.tick(x);
      std::array<double, 2> y{};
      fast += (1 - pole(1, fs)) * (peak * peak - fast);
      slow += (1 - pole(40, fs)) * (peak * peak - slow);
      const double transient =
          std::clamp((fast - slow) / (fast + slow + 1e-15), 0., 1.);
      for (int b = 0; b < 5; ++b) {
        if (anySolo && !solos[size_t(b)])
          continue;
        auto band = width(bands[size_t(b)], ch == 1 ? 1 : widths[size_t(b)]);
        for (int c = 0; c < 2; ++c) {
          const double linearReference = live ? .5*(band[size_t(c)]+causal[size_t(b*2+c)].previous) : band[size_t(c)];
          const double saturated = live ? causal[size_t(b*2+c)].tick(band[size_t(c)],drives[size_t(b)]) : std::tanh(band[size_t(c)]*drives[size_t(b)])/drives[size_t(b)];
          const double wet = saturated - linearReference;
          y[size_t(c)] +=
              direct * band[size_t(c)] +
              (wetoff[size_t(b)] ? 0
                                 : amounts[size_t(b)] * wetgains[size_t(b)] *
                                       (1 + punch * .4 * transient) * wet);
        }
      }
      return y;
    }
    if (kind == Kind::Harmonic) {
      auto source = x;
      const bool side = channel == 3 || channel == 4;
      if (side)
        source = {(x[0] + x[1]) * .7071067811865476,
                  (x[0] - x[1]) * .7071067811865476};
      std::array<double, 2> wet{};
      const double adapt =
          adaptive
              ? std::clamp(.15 / std::max(.01, std::sqrt(envelope)), .25, 4.)
              : 1;
      const double d = drive * adapt * (subtle ? .25 : 1);
      for (int c = 0; c < 2; ++c) {
        const double t = pre[size_t(c) * 2 + 1].tick(
            pre[size_t(c) * 2].tick(source[size_t(c)]));
        double generated = shape(t * d, shapeMode) / d - (mastering ? t : 0);
        generated += even * std::clamp(t * t * d, 0., 8.);
        if(live) {
          const double previous=last[size_t(c)],a=previous*d,b=t*d;
          const auto primitive=[&](double z) {
            const double u=std::abs(z);
            if(shapeMode==0) return u+std::log1p(std::exp(-2*u));
            if(shapeMode==1) return u<=1 ? .5*u*u : u-.5;
            const double q=std::min(u,1.5),q2=q*q;
            const double integral=.5*q2-q2*q2/24+(shapeMode==3 ? q2*q2*q2/720 : 0);
            return integral+(u-q)*shape(q,shapeMode);
          };
          const double averaged=std::abs(b-a)>1e-5 ? (primitive(b)-primitive(a))/(b-a) : shape(.5*(a+b),shapeMode);
          const auto evenPrimitive=[&](double z) {
            const double u=std::abs(z),limit=std::sqrt(8/d),q=std::min(u,limit);
            return std::copysign(d*q*q*q/3+8*(u-q),z);
          };
          const double averagedEven=std::abs(t-previous)>1e-6 ? (evenPrimitive(t)-evenPrimitive(previous))/(t-previous) : std::clamp(.25*(t+previous)*(t+previous)*d,0.,8.);
          generated=averaged/d-(mastering ? .5*(t+previous) : 0)+even*averagedEven;
        }
        last[size_t(c)] = t;
        generated = dc(generated, c);
        wet[size_t(c)] =
            post[size_t(c) * 2 + 1].tick(post[size_t(c) * 2].tick(generated)) *
            amount * (subtle ? subtleGain : 1);
      }
      if (channel == 1 || channel == 3)
        wet[1] = 0;
      if (channel == 2 || channel == 4)
        wet[0] = 0;
      if (side)
        wet = {(wet[0] + wet[1]) * .7071067811865476,
               (wet[0] - wet[1]) * .7071067811865476};
      wet = width(wet, ch == 1 ? 1 : wetWidth);
      return {(wetOnly ? 0 : x[0]) + wet[0], (wetOnly ? 0 : x[1]) + wet[1]};
    }
    auto y = x;
    if (ms)
      y = {(x[0] + x[1]) * .7071067811865476,
           (x[0] - x[1]) * .7071067811865476};
    for (int c = 0; c < 2; ++c) {
      for (int b = 0; b < (cuts ? 7 : 5); ++b)
        y[size_t(c)] = eq[size_t(c)][size_t(b)].tick(y[size_t(c)]);
      const double value = y[size_t(c)];
      if (thd > 0)
        y[size_t(c)] +=
            thd *
            (std::tanh(value * calibration * 3) / (calibration * 3) - value);
    }
    if (ms)
      y = {(y[0] + y[1]) * .7071067811865476,
           (y[0] - y[1]) * .7071067811865476};
    if (monoBass) {
      const double side = .5 * (y[0] - y[1]),
                   lo = low[0].tick(side, Low::coeff(200, fs));
      y[0] -= lo;
      y[1] += lo;
    }
    const double bass = low[1].tick(.5 * (y[0] + y[1]), fillCoefficient);
    if (bass >= 0 && previousBand < 0)
      subSign = -subSign;
    previousBand = bass;
    const double octave =
        low[2].tick(std::abs(bass) * subSign, fillCoefficient) * fill * .5;
    for (auto &v : y)
      v += octave;
    return y;
  }

private:
  double dc(double x, int c) noexcept {
    const double y =
        x - dcIn[size_t(c)] + std::exp(-2 * pi * 8 / fs) * dcOut[size_t(c)];
    dcIn[size_t(c)] = x;
    dcOut[size_t(c)] = std::abs(y) < 1e-25 ? 0 : y;
    return y;
  }
  Kind kind;
  double fs = 48000;
  int ch = 2, oversampleFactor=1;
  bool live = true;
  std::array<std::vector<double>, 2> ring;
  size_t pos = 0;
  double phase = 0, drive = 1, calibration = 1, bias = 0, wow = 0, flutter = 0,
         noise = 0, envelope = 0;
  int machine = 0, form = 0, speed = 0;
  bool compensate = true, hiss = false, automute = true;
  uint32_t rng = 99123;
  std::array<std::array<Filter, 7>, 2> eq;
  std::array<Low, 3> low;
  Split split;
  std::array<double, 5> amounts{}, drives{}, wetgains{}, widths{};
  std::array<bool, 5> wetoff{}, solos{};
  double direct = 1, punch = 0, fast = 0, slow = 0;
  bool anySolo = false;
  std::array<Filter, 4> pre, post;
  double amount = 0, subtleGain = 1, even = 0, wetWidth = 1;
  bool subtle = false, adaptive = true, mastering = true, wetOnly = false;
  int shapeMode = 0, channel = 0;
  std::array<double, 2> last{}, dcIn{}, dcOut{};
  std::array<CausalTanh19,10> causal{};
  bool ms = false, cuts = false, monoBass = false;
  double thd = 0, fill = 0, fillCoefficient = .01, subSign = 1,
         previousBand = 0;
};
} // namespace studio
