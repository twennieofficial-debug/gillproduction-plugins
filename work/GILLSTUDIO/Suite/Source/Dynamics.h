#pragma once
#include "../../../GILLMASTER/Source/Foundation/FinishDSP.h"
#include "../../../GILLMASTER/Source/Foundation/Loudness.h"
#include "../../Source/Dynamics/Dynamics.h"
#include "Tone.h"
namespace studio {
namespace dy = gillstudio::dynamics;
class Bands {
  struct Band {
    dy::Processor dynamics;
    std::array<Low, 4> audio, key;
    double al = 0, ah = 0, kl = 0, kh = 0;
    bool enabled = true, solo = false, external = false, listen = false;
    int channel = 0;
    double makeup = 0, morph = 1;
  };
  std::array<Band, 6> band;
  int count = 6, ch = 2;
  double fs = 48000, depth = 1, link = 1;
  bool ms = false, anySolo = false;

public:
  std::array<float, 6> reduction{};
  explicit Bands(Kind k)
      : count(k == Kind::Multi       ? 6
              : k == Kind::Precision ? 5
                                     : 2) {}
  void prepare(double rate, int channels) {
    fs = rate;
    ch = channels;
    for (auto &b : band)
      b.dynamics.prepare(fs, 512, 2);
    reset();
  }
  void reset() noexcept {
    for (auto &b : band) {
      b.dynamics.reset();
      b.audio = {};
      b.key = {};
      b.morph = b.enabled ? 1 : 0;
    }
    reduction = {};
  }
  void update(const Values &v) noexcept {
    ms = v("routing") > .5;
    depth = v("depth") * .01;
    link = v("coupling") * .01;
    anySolo = false;
    for (int i = 0; i < count; ++i) {
      auto &b = band[size_t(i)];
      const int group = i + 1;
      b.enabled = v("enable", group) > .5;
      b.solo = v("solo", group) > .5;
      anySolo |= b.solo;
      b.external = v("external", group) > .5;
      b.listen = v("listen", group) > .5;
      b.channel = int(v("channel", group));
      b.makeup = v("gain", group);
      const auto lo = std::min(v("low", group), v("high", group)),
                 hi = std::max(v("low", group), v("high", group)),
                 kl = std::min(v("keylow", group), v("keyhigh", group)),
                 kh = std::max(v("keylow", group), v("keyhigh", group));
      b.al = Low::coeff(lo, fs);
      b.ah = Low::coeff(hi, fs);
      b.kl = Low::coeff(kl, fs);
      b.kh = Low::coeff(kh, fs);
      dy::Parameters p;
      p.curve.thresholdDb = v("threshold", group);
      p.curve.ratio = v("ratio", group);
      p.curve.kneeDb = v("knee", group);
      p.curve.rangeDb = v("range", group);
      p.curve.law = dy::Law(int(v("law", group)));
      p.envelope.attackMs = v("attack", group);
      p.envelope.holdMs = v("hold", group);
      p.envelope.releaseFastMs = v("release", group);
      p.envelope.releaseSlowMs = v("slow", group);
      p.envelope.adaptive = v("adapt", group) * .01;
      p.rmsMs = v("rms", group);
      p.detector = p.rmsMs > 0 ? dy::Detector::RMS : dy::Detector::Peak;
      p.stereoLink = v("link", group) * .01 * link;
      p.key = dy::KeySource::External;
      b.dynamics.setParameters(p);
    }
  }
  std::array<double, 2> sample(std::array<double, 2> x,
                               std::array<double, 2> external) noexcept {
    if (ch == 1) {
      x[1] = x[0];
      external[1] = external[0];
    }
    auto y = anySolo ? std::array<double, 2>{} : x;
    bool listening = false;
    std::array<double, 2> listen{};
    for (int i = 0; i < count; ++i) {
      auto &b = band[size_t(i)];
      const bool basis = ms || b.channel >= 3;
      auto signal = anySolo ? x : y, key = b.external ? external : x;
      if (basis) {
        signal = {(signal[0] + signal[1]) * .7071067811865476,
                  (signal[0] - signal[1]) * .7071067811865476};
        key = {(key[0] + key[1]) * .7071067811865476,
               (key[0] - key[1]) * .7071067811865476};
      }
      std::array<float, 2> audio{}, trigger{};
      std::array<double, 2> dry{};
      for (int c = 0; c < 2; ++c) {
        dry[size_t(c)] =
            b.audio[size_t(c) * 2 + 1].tick(signal[size_t(c)], b.ah) -
            b.audio[size_t(c) * 2].tick(signal[size_t(c)], b.al);
        audio[size_t(c)] = float(dry[size_t(c)]);
        trigger[size_t(c)] =
            float(b.key[size_t(c) * 2 + 1].tick(key[size_t(c)], b.kh) -
                  b.key[size_t(c) * 2].tick(key[size_t(c)], b.kl));
      }
      float *ap[]{&audio[0], &audio[1]};
      const float *kp[]{&trigger[0], &trigger[1]};
      b.dynamics.process(ap, 1, kp, 2);
      b.morph += (1 - pole(5, fs)) * ((b.enabled ? 1. : 0.) - b.morph);
      std::array<double, 2> change{}, heard{};
      for (int c = 0; c < 2; ++c) {
        const bool selected = !(b.channel == 1 || b.channel == 3   ? c == 1
                                : b.channel == 2 || b.channel == 4 ? c == 0
                                                                   : false);
        const double db =
            selected
                ? (-b.dynamics.gainReductionDb(size_t(c)) * depth + b.makeup) *
                      b.morph
                : 0;
        change[size_t(c)] = dry[size_t(c)] * (gain(db) - (anySolo ? 0 : 1));
        heard[size_t(c)] = selected ? trigger[size_t(c)] : 0;
      }
      reduction[size_t(i)] = float(std::max(b.dynamics.gainReductionDb(0),
                                            b.dynamics.gainReductionDb(1)) *
                                   depth * b.morph);
      if (basis) {
        change = {(change[0] + change[1]) * .7071067811865476,
                  (change[0] - change[1]) * .7071067811865476};
        heard = {(heard[0] + heard[1]) * .7071067811865476,
                 (heard[0] - heard[1]) * .7071067811865476};
      }
      if (!anySolo || b.solo)
        for (int c = 0; c < 2; ++c)
          y[size_t(c)] += change[size_t(c)];
      if (b.listen) {
        listening = true;
        for (int c = 0; c < 2; ++c)
          listen[size_t(c)] += heard[size_t(c)];
      }
    }
    return listening ? listen : y;
  }
};
class Alpha {
  std::array<dy::Processor, 2> dyn;
  std::array<dy::Parameters, 2> params;
  std::array<Filter, 4> keyFilter, tilt;
  std::array<double, 2> last{}, power{};
  Low sideLow;
  double fs = 48000, link = 1, drive = 1, clip = 1, w = 1;
  bool ms = false, feedback = false, external = false, clipOn = false,
       monoBass = false;
  std::array<bool, 2> autoAttack{};
  std::array<double, 2> makeup{};
  uint32_t clock = 0;

public:
  float reduction = 0;
  void prepare(double rate, int) {
    fs = rate;
    for (auto &d : dyn)
      d.prepare(fs, 512, 1);
    reset();
  }
  void reset() noexcept {
    for (auto &d : dyn)
      d.reset();
    for (auto &f : keyFilter)
      f.reset();
    for (auto &f : tilt)
      f.reset();
    sideLow = {};
    last = power = {};
    clock = 0;
  }
  void update(const Values &v) noexcept {
    ms = v("routing") > .5;
    feedback = v("topology") > .5;
    external = v("external") > .5;
    link = v("link") * .01;
    drive = gain(v("drive"));
    clip = gain(v("clip"));
    clipOn = v("clipon") > .5;
    monoBass = v("monobass") > .5;
    w = v("width") * .01;
    for (int c = 0; c < 2; ++c) {
      keyFilter[size_t(c) * 2].set(gill::LowCut, v("sclow"), 0, .707, fs);
      keyFilter[size_t(c) * 2 + 1].set(gill::HighCut, v("schigh"), 0, .707, fs);
      tilt[size_t(c) * 2].set(gill::LowShelf, v("tiltfreq"), -v("tilt") * .5,
                              .707, fs);
      tilt[size_t(c) * 2 + 1].set(gill::HighShelf, v("tiltfreq"),
                                  v("tilt") * .5, .707, fs);
      auto &p = params[size_t(c)];
      p.curve.thresholdDb = v("threshold", c + 1);
      p.curve.ratio = v("ratio", c + 1);
      p.curve.kneeDb = v("knee", c + 1);
      p.curve.rangeDb = v("range", c + 1);
      p.envelope.attackMs = v("attack", c + 1);
      p.envelope.releaseFastMs = v("release", c + 1);
      p.envelope.releaseSlowMs = p.envelope.releaseFastMs * 4;
      p.envelope.adaptive = v("adaptive", c + 1) * .01;
      p.detector = dy::Detector::Peak;
      p.key = dy::KeySource::External;
      autoAttack[size_t(c)] = v("autoattack", c + 1) > .5;
      makeup[size_t(c)] = gain(v("makeup", c + 1));
      dyn[size_t(c)].setParameters(p);
    }
  }
  std::array<double, 2> sample(std::array<double, 2> x,
                               std::array<double, 2> key) noexcept {
    if (!external)
      key = feedback ? last : x;
    if (ms) {
      x = {(x[0] + x[1]) * .7071067811865476,
           (x[0] - x[1]) * .7071067811865476};
      key = {(key[0] + key[1]) * .7071067811865476,
             (key[0] - key[1]) * .7071067811865476};
    }
    for (int c = 0; c < 2; ++c) {
      key[size_t(c)] = keyFilter[size_t(c) * 2 + 1].tick(
          keyFilter[size_t(c) * 2].tick(key[size_t(c)]));
      power[size_t(c)] += (1 - pole(5, fs)) *
                          (key[size_t(c)] * key[size_t(c)] - power[size_t(c)]);
    }
    const double loudest = std::max(std::sqrt(power[0]), std::sqrt(power[1]));
    for (int c = 0; c < 2; ++c) {
      float a = float(x[size_t(c)]),
            k = float(std::sqrt(power[size_t(c)]) +
                      link * (loudest - std::sqrt(power[size_t(c)])));
      if ((clock & 31) == 0) {
        auto p = params[size_t(c)];
        if (autoAttack[size_t(c)])
          p.envelope.attackMs /=
              1 + std::clamp(std::abs(key[size_t(c)]) /
                                     (std::sqrt(power[size_t(c)]) + 1e-9) -
                                 1,
                             0., 4.);
        dyn[size_t(c)].setParameters(p);
      }
      float *ap[]{&a};
      const float *kp[]{&k};
      dyn[size_t(c)].process(ap, 1, kp, 1);
      double out = a * makeup[size_t(c)];
      out = tilt[size_t(c) * 2 + 1].tick(tilt[size_t(c) * 2].tick(out));
      if (drive > 1)
        out = std::tanh(out * drive) / drive;
      if (clipOn)
        out = clip * std::tanh(out / clip);
      x[size_t(c)] = out;
    }
    ++clock;
    reduction =
        float(std::max(dyn[0].gainReductionDb(0), dyn[1].gainReductionDb(0)));
    if (ms)
      x = {(x[0] + x[1]) * .7071067811865476,
           (x[0] - x[1]) * .7071067811865476};
    x = width(x, w);
    if (monoBass) {
      const double sub = sideLow.tick(.5 * (x[0] - x[1]), Low::coeff(120, fs));
      x[0] -= sub;
      x[1] += sub;
    }
    last = x;
    return x;
  }
};
class Maximizer {
  gillnext::FinishDSP limiter;
  gillnext::Loudness inputLoudness, outputLoudness;
  double fs = 48000, power = 0, fast = 0, slow = 0, upward = 0, transient = 0;
  int ch = 2;
  bool live = true;

public:
  void prepare(double rate, int channels, bool mode) {
    fs = rate;
    ch = channels;
    live = mode;
    limiter.setLiveMode(live);
    limiter.prepare(fs, 512, ch);
    inputLoudness.prepare(fs, ch);
    outputLoudness.prepare(fs, ch);
    reset();
  }
  void reset() noexcept {
    limiter.reset();
    resetLoudness();
    power = fast = slow = 0;
  }
  void resetLoudness() noexcept {
    inputLoudness.reset();
    outputLoudness.reset();
  }
  std::array<float, 2> loudness() const noexcept {
    return {float(inputLoudness.integrated), float(outputLoudness.integrated)};
  }
  void mode(bool value) noexcept {
    live = value;
    limiter.setLiveMode(value);
  }
  int latency() const noexcept { return limiter.latencySamples(); }
  float reduction() const noexcept { return limiter.limiterReductionDb(); }
  void update(const Values &v) noexcept {
    gillnext::FinishParameters p;
    p.toneEnabled = p.compEnabled = p.stereoEnabled = false;
    p.clipEnabled = true;
    p.clip = v("clip");
    p.driveDb = v("drive");
    p.ceilingDb = v("ceiling");
    p.releaseMs = v("release");
    limiter.setParameters(p);
    upward = v("upward");
    transient = v("transient") * .01;
  }
  void process(float *const *data, int n) noexcept {
    for (int i = 0; i < n; ++i) {
      inputLoudness.sample({data[0][i], ch == 2 ? data[1][i] : 0.f}, ch);
      double e = 0;
      for (int c = 0; c < ch; ++c)
        e = std::max(e, double(data[c][i]) * data[c][i]);
      power += (1 - pole(40, fs)) * (e - power);
      fast += (1 - pole(1, fs)) * (e - fast);
      slow += (1 - pole(40, fs)) * (e - slow);
      const double db = 10 * std::log10(std::max(1e-16, power)),
                   lift = upward * std::clamp((-24 - db) / 24., 0., 1.) *
                          std::clamp((db + 90) / 30., 0., 1.),
                   onset = std::clamp((fast - slow) / (fast + slow + 1e-15), 0.,
                                      1.);
      const double g = gain(lift + transient * 6 * onset);
      for (int c = 0; c < ch; ++c)
        data[c][i] = float(clean(data[c][i] * g));
    }
    limiter.process(data, ch, n);
    for (int i = 0; i < n; ++i)
      outputLoudness.sample({data[0][i], ch == 2 ? data[1][i] : 0.f}, ch);
  }
};
} // namespace studio
