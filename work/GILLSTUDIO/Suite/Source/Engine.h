#pragma once
#include "Dynamics.h"
#include "Antialias19.h"
#include "Rack.h"
#include "RoomTank.h"
#include "TransientKernel.h"
namespace studio {
class Room {
  std::array<RoomTank, 6> tanks;
  Split split;
  std::array<std::array<Filter, 6>, 2> eq;
  std::array<Filter, 2> distanceFilter;
  std::array<bool, 6> enabled{};
  std::array<std::vector<float>, 2> diffusion;
  size_t pos = 0;
  double fs = 48000, duck = 0, gate = 0, dry = 1, distance = 0, thickness = 0,
         envelope = 0, duckGain = 1, gateGain = 1;
  int hold = 0, ch = 2;

public:
  void prepare(double rate, int channels) {
    fs = rate;
    ch = channels;
    for (auto &t : tanks)
      t.prepare(fs, 512, ch);
    split.set({160, 600, 2000, 6000, 12000}, 6, fs);
    for (auto &r : diffusion)
      r.assign(size_t(fs * .007) + 3, 0.f);
    reset();
  }
  void reset() noexcept {
    for (auto &t : tanks)
      t.reset();
    split.st = {};
    for (auto &c : eq)
      for (auto &f : c)
        f.reset();
    for (auto &f : distanceFilter)
      f.reset();
    for (auto &r : diffusion)
      std::fill(r.begin(), r.end(), 0.f);
    pos = 0;
    envelope = 0;
    duckGain = gateGain = 1;
    hold = 0;
  }
  void update(const Values &v) noexcept {
    constexpr const char *decays[] = {"decay0", "decay1", "decay2",
                                      "decay3", "decay4", "decay5"};
    for (int i = 0; i < 6; ++i) {
      auto &t = tanks[size_t(i)];
      t.setParameters(
          100,
          float(std::clamp(
              double(v("decay") * v("decaymult") * v(decays[i], 7)), .2, 15.)),
          v("predelay"), v("brightness"), v("space"), v("width"),
          int(v("style")), 0);
      t.setFreeze(v("freeze") > .5);
    }
    duck = v("duck") * .01;
    gate = v("gate");
    dry = v("dry") * .01;
    distance = v("distance") * .01;
    thickness = v("thickness") * .007;
    for (auto &f : distanceFilter)
      f.set(gill::HighCut, 20000 * std::pow(.15, distance), 0, .707, fs);
    for (int b = 0; b < 6; ++b) {
      enabled[size_t(b)] = v("enable", b + 1) > .5;
      for (auto &c : eq)
        c[size_t(b)].set(int(v("type", b + 1)), v("frequency", b + 1),
                         v("gain", b + 1), v("q", b + 1), fs);
    }
  }
  void process(float *const *data, int n) noexcept {
    for (int i = 0; i < n; ++i) {
      std::array<double, 2> input{}, original{};
      double peak = 0;
      for (int c = 0; c < ch; ++c) {
        original[size_t(c)] = data[c][i];
        peak = std::max(peak, std::abs(double(data[c][i])));
        double y = distanceFilter[size_t(c)].tick(data[c][i]);
        auto &r = diffusion[size_t(c)];
        const double out = r[pos] - thickness * y;
        r[pos] = float(y + thickness * out);
        input[size_t(c)] = out;
      }
      if (ch == 1)
        input[1] = input[0];
      pos = (pos + 1) % diffusion[0].size();
      const auto bands = split.tick(input);
      std::array<double, 2> wet{};
      for (int b = 0; b < 6; ++b) {
        std::array<float, 2> a{float(bands[size_t(b)][0]),
                               float(bands[size_t(b)][1])};
        float *ptr[]{&a[0], &a[1]};
        tanks[size_t(b)].process(ptr, ch, 1);
        for (int c = 0; c < ch; ++c)
          wet[size_t(c)] += a[size_t(c)];
      }
      envelope += (1 - pole(peak > envelope ? 5 : 200, fs)) * (peak - envelope);
      if (peak > .004)
        hold = int(gate * .001 * fs);
      else if (hold > 0)
        --hold;
      const double target = 1 / (1 + duck * 30 * envelope);
      duckGain +=
          (1 - pole(target < duckGain ? 5 : 200, fs)) * (target - duckGain);
      gateGain +=
          (1 - pole(10, fs)) * ((gate <= 0 || hold > 0 ? 1. : 0.) - gateGain);
      for (int c = 0; c < ch; ++c) {
        for (int b = 0; b < 6; ++b)
          if (enabled[size_t(b)])
            wet[size_t(c)] = eq[size_t(c)][size_t(b)].tick(wet[size_t(c)]);
        data[c][i] =
            float(dry * original[size_t(c)] +
                  wet[size_t(c)] * duckGain * gateGain * (.7 + .3 * distance));
      }
    }
  }
};
class Engine {
  Kind kind;
  Tone tone;
  Bands bands;
  Alpha alpha;
  Maximizer maximizer;
  Room room;
  Rack rack;
  gillstudio::spectral::Processor spectral;
  gillstudio::spectral::Parameters spectralParams;
  int ch = 2;
  bool live = true;
  double fs = 48000;
  Antialias19 antialias;
  Definition cachedDefinition;
  std::array<float,1024> cachedValues{};
  bool hasValues=false;
  bool nonlinear() const noexcept { return kind==Kind::Tape || kind==Kind::Vital || kind==Kind::Harmonic || kind==Kind::Rack; }
  int oversampling() const noexcept { return nonlinear() && !live ? (fs>96000 ? 2 : fs>48000 ? 4 : 8) : 1; }

public:
  explicit Engine(Kind k) : kind(k), tone(k), bands(k), cachedDefinition(definition(k)) {}
  void prepare(double rate, int channels, bool mode) {
    fs = rate;
    ch = channels;
    live = mode;
    antialias.prepare(oversampling());
    const double coreRate=fs*oversampling();
    if (kind == Kind::Tape || kind == Kind::Vital || kind == Kind::Eq825 ||
        kind == Kind::Harmonic)
      tone.prepare(coreRate, ch, live, oversampling());
    else if (kind == Kind::Multi || kind == Kind::Precision ||
             kind == Kind::Deess)
      bands.prepare(fs, ch);
    else if (kind == Kind::Alpha)
      alpha.prepare(fs, ch);
    else if (kind == Kind::Max)
      maximizer.prepare(fs, ch, live);
    else if (kind == Kind::Room)
      room.prepare(fs, ch);
    else if (kind == Kind::Rack)
      rack.prepare(coreRate, live, ch);
    else {
      spectral.prepare(fs, 512, size_t(ch), 2048);
      spectralParams.mode = live ? gillstudio::spectral::Mode::Live
                                 : gillstudio::spectral::Mode::Pro;
      spectral.setParameters(spectralParams);
    }
  }
  void mode(bool v) noexcept {
    if (live == v)
      return;
    if(nonlinear()) {
      prepare(fs,ch,v);
      if(hasValues) update({cachedDefinition,cachedValues});
      return;
    }
    live = v;
    if (kind == Kind::Tape || kind == Kind::Harmonic)
      tone.mode(v);
    if (kind == Kind::Max)
      maximizer.mode(v);
    if (kind == Kind::Transient) {
      spectralParams.mode = v ? gillstudio::spectral::Mode::Live
                              : gillstudio::spectral::Mode::Pro;
      spectral.setParameters(spectralParams);
    }
  }
  int latency() const noexcept {
    if(nonlinear()) return (kind==Kind::Tape ? tone.latency()/oversampling() : 0) + (live ? 0 : Antialias19::latency);
    return kind == Kind::Tape        ? tone.latency()
           : kind == Kind::Max       ? maximizer.latency()
           : kind == Kind::Transient ? int(spectral.latencySamples())
                                     : 0;
  }
  void update(const Values &v) noexcept {
    cachedValues=v.values; hasValues=true;
    if (kind == Kind::Tape || kind == Kind::Vital || kind == Kind::Eq825 ||
        kind == Kind::Harmonic)
      tone.update(v);
    else if (kind == Kind::Multi || kind == Kind::Precision ||
             kind == Kind::Deess)
      bands.update(v);
    else if (kind == Kind::Alpha)
      alpha.update(v);
    else if (kind == Kind::Max)
      maximizer.update(v);
    else if (kind == Kind::Room)
      room.update(v);
    else if (kind == Kind::Rack)
      rack.update(v);
    else {
      auto &p = spectralParams;
      p.transient = v("amount") * .01;
      p.maxReductionDb = 24;
      p.boost = v("action") > .5;
      p.sensitivity = v("sensitivity") * .01;
      p.sharpness = v("sharpness") * .01;
      p.transientExponent = .25 + v("sharpness") * .015;
      p.transientDecayMs = v("decay");
      p.attackMs = v("attack");
      p.releaseMs = v("release");
      p.stereoLink = v("link") * .01;
      p.routing = v("routing") > .5 ? gillstudio::spectral::Channels::MidSide
                                    : gillstudio::spectral::Channels::Stereo;
      for (int i = 0; i < 5; ++i) {
        auto &n = p.nodes[size_t(i)];
        n.enabled = v("enable", i + 1) > .5;
        n.shape = gillstudio::spectral::Shape(int(v("shape", i + 1)));
        n.frequency = v("frequency", i + 1);
        n.gainDb = v("gain", i + 1);
        n.q = v("q", i + 1);
        n.slope = v("slope", i + 1);
      }
      spectral.setParameters(p);
    }
  }
  void process(float *const *data, int n, const float *const *key = nullptr,
               int keys = 0) noexcept {
    if (kind == Kind::Transient) {
      spectral.process(data, size_t(n));
      return;
    }
    if (kind == Kind::Max) {
      maximizer.process(data, n);
      return;
    }
    if (kind == Kind::Room) {
      room.process(data, n);
      return;
    }
    for (int i = 0; i < n; ++i) {
      std::array<double, 2> x{data[0][i], ch == 2 ? data[1][i] : data[0][i]},
          k{key && keys ? key[0][i] : 0, key && keys > 1 ? key[1][i]
                                         : key && keys   ? key[0][i]
                                                         : 0};
      for (auto &v : k)
        v = clean(v);
      std::array<double, 2> y;
      if (kind == Kind::Multi || kind == Kind::Precision || kind == Kind::Deess)
        y = bands.sample(x, k);
      else if (kind == Kind::Alpha)
        y = alpha.sample(x, k);
      else if(nonlinear() && !live)
        y=antialias.tick(x,k,[&](auto a,auto b) noexcept {return kind==Kind::Rack ? rack.sample(a,b) : tone.sample(a);});
      else if (kind == Kind::Rack)
        y = rack.sample(x, k);
      else
        y = tone.sample(x);
      for (int c = 0; c < ch; ++c)
        data[c][i] = float(clean(y[size_t(c)]));
    }
  }
  void resetLoudness() noexcept {
    if (kind == Kind::Max)
      maximizer.resetLoudness();
  }
  std::array<float, 2> loudness() const noexcept {
    return kind == Kind::Max ? maximizer.loudness()
                             : std::array<float, 2>{-100, -100};
  }
  std::array<float, 6> meters() const noexcept {
    if (kind == Kind::Multi || kind == Kind::Precision || kind == Kind::Deess)
      return bands.reduction;
    return {kind == Kind::Transient ? float(spectral.reductionDb())
            : kind == Kind::Alpha   ? alpha.reduction
            : kind == Kind::Max     ? maximizer.reduction()
                                    : 0,
            0,
            0,
            0,
            0,
            0};
  }
};
} // namespace studio
