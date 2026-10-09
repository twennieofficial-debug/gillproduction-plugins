#pragma once
#include "../../GILLSTUDIO/Suite/Source/RoomTank.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace mobina {
struct Settings {
  float dream = 42, distance = 30, space = 3.6f, width = 125, drift = 25,
        rate = .12f, warmth = 35, shimmer = 18, predelay = 24, duck = 30,
        mix = 100, output = 0, bypass = 0;
};
class Dsp {
  double fs = 48000, phase = 0, slow = 0;
  int channels = 2;
  size_t position = 0;
  std::array<std::vector<float>, 2> delay;
  std::array<float, 2> tone{}, low{}, dcX{}, dcY{};
  float envelope = 0, bypass = 0, qualityBlend = 1;
  Settings current{}, target{};
  bool initial = true;
  bool qualityInitial = true;
  studio::RoomTank room;
  static constexpr double pi = 3.141592653589793;
  static float sane(float x) {
    return std::isfinite(x) ? std::clamp(x, -8.f, 8.f) : 0.f;
  }
  float tap(int c, double ms) const {
    double index = double(position) - ms * .001 * fs;
    const double size = double(delay[c].size());
    while (index < 0)
      index += size;
    const auto a = size_t(index) % delay[c].size();
    const auto b = (a + 1) % delay[c].size();
    return delay[c][a] +
           float(index - std::floor(index)) * (delay[c][b] - delay[c][a]);
  }

public:
  float pan = 0;
  void prepare(double sampleRate, int numChannels) {
    fs = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000., 192000.) : 48000.;
    channels = std::clamp(numChannels, 1, 2);
    for (auto &d : delay)
      d.assign(size_t(fs * .07) + 4, 0);
    room.prepare(fs, 64, 2);
    reset();
  }
  void reset() {
    for (auto &d : delay)
      std::fill(d.begin(), d.end(), 0);
    position = 0;
    phase = slow = 0;
    tone = {};
    low = {};
    dcX = {};
    dcY = {};
    envelope = 0;
    initial = true;
    qualityInitial = true;
    room.reset();
  }
  void set(Settings value) {
    target = value;
    if (initial) {
      current = target;
      bypass = target.bypass;
      initial = false;
    }
  }
  void process(float *const *data, int n, double bpm, bool pro) {
    if (!data || n <= 0 || delay[0].empty()) return;
    // Reset starts at the requested quality. Later mode changes still crossfade.
    if (qualityInitial) {
      qualityBlend = pro ? 1.f : 0.f;
      qualityInitial = false;
    }
    const float smoothing = float(1 - std::exp(-1 / (.025 * fs))),
                envA = float(std::exp(-1 / (.008 * fs))),
                envR = float(std::exp(-1 / (.22 * fs)));
    auto smooth = [&](float &a, float b) {
      a += (b - a) * smoothing;
      if (std::abs(a - b) < 1e-6f)
        a = b;
    };
    for (int i = 0; i < n; ++i) {
      smooth(current.dream, target.dream);
      smooth(current.distance, target.distance);
      smooth(current.space, target.space);
      smooth(current.width, target.width);
      smooth(current.drift, target.drift);
      smooth(current.rate, target.rate);
      smooth(current.warmth, target.warmth);
      smooth(current.shimmer, target.shimmer);
      smooth(current.predelay, target.predelay);
      smooth(current.duck, target.duck);
      smooth(current.mix, target.mix);
      smooth(current.output, target.output);
      smooth(bypass, target.bypass);
      float x[2]{sane(data[0][i]), sane(data[channels - 1][i])}, coloured[2]{};
      const float amount = current.dream * .01f,
                  distance = current.distance * .01f,
                  warm = current.warmth * .01f;
      const float pole = float(
          std::exp(-2 * pi * (16000 - 12000 * distance - 1800 * warm) / fs));
      const float bassPole = float(std::exp(-2 * pi * 160 / fs));
      for (int c = 0; c < 2; ++c) {
        tone[c] = (1 - pole) * x[c] + pole * tone[c];
        const float shaped = x[c] + distance * (tone[c] - x[c]);
        const float gain = 1 + warm * 1.8f;
        const float saturated = std::tanh(gain * shaped) / gain;
        coloured[c] = shaped + warm * .3f * (saturated - shaped);
        low[c] = (1 - bassPole) * coloured[c] + bassPole * low[c];
        delay[c][position] = coloured[c] - low[c];
      }
      const float level = std::max(std::abs(x[0]), std::abs(x[1]));
      const float e = level > envelope ? envA : envR;
      envelope = level + e * (envelope - level);
      const double speed = current.rate < 0 ? bpm / 60.0 / 8.0 : current.rate;
      phase += speed / fs;
      phase -= std::floor(phase);
      slow += .17 / fs;
      slow -= std::floor(slow);
      float wet[2];
      smooth(qualityBlend, pro ? 1.f : 0.f);
      for (int c = 0; c < 2; ++c) {
        const double offset = c ? .31 : 0.;
        const float ch = tap(c, 18 + 3 * std::sin(2 * pi * (slow + offset)));
        const float ch2 =
            tap(c, 27 + 2.1 * std::sin(2 * pi * (slow * .83 + offset + .49)));
        wet[c] = ch + .4f * qualityBlend * (ch2 - ch);
      }
      float rL = coloured[0] * .7f + wet[0] * .3f,
            rR = coloured[1] * .7f + wet[1] * .3f;
      float *r[]{&rL, &rR};
      // Reconfigure at a fixed absolute cadence, independent of host blocks.
      if (position % 32 == 0)
        room.setParameters(100, current.space, current.predelay,
                           65 - current.warmth * .4f, 80, 100, 1, 0);
      room.process(r, 2, 1);
      position = (position + 1) % delay[0].size();
      const float duckGain = 1 / (1 + envelope * current.duck * .08f);
      const float m = .5f * (wet[0] + wet[1]), side = .5f * (wet[0] - wet[1]);
      const float air = current.shimmer * .01f;
      float y[2]{coloured[0] * (1 - amount * .36f) +
                     amount * duckGain *
                         (.48f * rL + .30f * (m + side) * (1 + air * .5f)),
                 coloured[1] * (1 - amount * .36f) +
                     amount * duckGain *
                         (.48f * rR + .30f * (m - side) * (1 + air * .5f))};
      const float mid = .5f * (y[0] + y[1]),
                  stereo = .5f * (y[0] - y[1]) * current.width * .01f;
      const float bass = .5f * (low[0] + low[1]);
      pan = std::sin(float(2 * pi * phase)) * current.drift * .006f;
      const float angle = pan * float(pi * .25);
      // Rotate stereo above the bass range. Gentle motion preserves stereo
      // energy.
      const float a = (mid - bass) * std::cos(angle) - stereo * std::sin(angle),
                  b = (mid - bass) * std::sin(angle) + stereo * std::cos(angle);
      y[0] = bass + a + b;
      y[1] = bass + a - b;
      const float gain = std::pow(10.f, current.output / 20.f),
                  mix = current.mix * .01f;
      for (int c = 0; c < channels; ++c) {
        const float effect = channels == 1 ? .5f * (y[0] + y[1]) : y[c];
        const float result = (x[c] + mix * (effect - x[c])) * gain;
        data[c][i] = sane(result + (x[c] - result) * bypass);
      }
    }
  }
};
} // namespace mobina
