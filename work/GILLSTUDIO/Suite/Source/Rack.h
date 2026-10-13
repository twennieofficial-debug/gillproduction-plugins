#pragma once
#include "Dynamics.h"
#include "RackModel.h"
namespace studio {
class Rack {
  struct Slot {
    std::array<std::array<Filter, 8>, 2> eq{};
    std::array<std::array<Filter, 2>, 2> keyFilters{};
    dy::Processor dynamics, gate;
    std::array<CausalTanh19,2> causal{};
    std::array<double,2> gatePeak{}, gateDb{}, gateTimer{};
    std::array<bool,2> gateOpen{};
    double gateThreshold=-60,gateRange=24,gateRatio=4,gateAttack=1,gateLink=1;
    dy::GainEnvelope deessEnvelope;
    std::array<std::vector<double>, 2> delay;
    std::array<Low, 2> deessSplit;
    std::array<double, 2> last{}, dcX{}, dcY{};
    size_t pos = 0;
    int module = 0, order = 0, style = 0, chorusMode = 0;
    bool enabled = true, solo = false, external = false, listen = false,
         hfonly = true, feedback = false, clip = false;
    double in = 1, out = 1, wet = 1, polarity = 1, amount = .3, active = 1,
           fast = 0, slow = 0, phase = 0, rate = .5, depth = .3, spread = 1,
           regen = 0, bias = 0;
    double attack = 0, sustain = 0, release = 150, range = 12, threshold = -24,
           splitCoeff = .5, gateHold = 0, hold = 0, drumGain = 0, fullPower = 0,
           hfPower = 0;
  };
  std::array<Slot, 8> slots;
  std::array<int, 8> order{0, 1, 2, 3, 4, 5, 6, 7};
  double fs = 48000;
  bool anySolo = false, live=true;
  int channels = 2;
  static double interpolate(const std::vector<double> &buffer, size_t pos,
                            double delay) noexcept {
    const auto n = size_t(delay);
    const auto a = (pos + buffer.size() - n) % buffer.size(),
               b = (a + buffer.size() - 1) % buffer.size();
    return buffer[a] + (delay - double(n)) * (buffer[b] - buffer[a]);
  }
  double color(Slot &s, double value, int channel, double drive,
                      double skew) noexcept {
    const double output =
        live ? s.causal[size_t(channel)].tick(value,drive,skew) : (std::tanh(value * drive + skew) - std::tanh(skew)) / drive;
    const auto c = size_t(channel);
    const double dc = output - s.dcX[c] + std::exp(-2*pi*7.643/fs) * s.dcY[c];
    s.dcX[c] = output;
    s.dcY[c] = dc;
    return dc;
  }

public:
  std::array<float, 8> reduction{};
  void prepare(double rate, bool liveMode=true, int mainChannels=2) {
    fs = rate; live=liveMode; channels=mainChannels;
    for (auto &s : slots) {
      s.dynamics.prepare(fs, 512, 2);
      s.gate.prepare(fs, 512, 2);
      s.deessEnvelope.prepare(fs);
      for (auto &r : s.delay)
        r.assign(size_t(fs * .08) + 8, 0.);
    }
    reset();
  }
  void reset() noexcept {
    for (auto &s : slots) {
      s.dynamics.reset();
      s.gate.reset();
      s.causal={}; s.gatePeak={}; s.gateDb={}; s.gateTimer={}; s.gateOpen={};
      s.deessEnvelope.reset();
      for (auto &c : s.eq)
        for (auto &f : c)
          f.reset();
      for (auto &c : s.keyFilters)
        for (auto &f : c)
          f.reset();
      for (auto &r : s.delay)
        std::fill(r.begin(), r.end(), 0.);
      s.deessSplit = {};
      s.fast = s.slow = s.phase = s.fullPower = s.hfPower = s.hold =
          s.drumGain = 0;
      s.pos = 0;
      s.last = s.dcX = s.dcY = {};
      s.active = s.enabled ? 1 : 0;
    }
    reduction = {};
  }
  void update(const Values &v) noexcept {
    anySolo = false;
    for (int i = 0; i < 8; ++i) {
      const int g = i + 1;
      auto &s = slots[size_t(i)];
      s.module = std::clamp(int(v("module", g)), 0, rackModelCount - 1);
      const auto &m = rackModels[s.module];
      s.order = int(v("order", g));
      s.enabled = v("enable", g) > .5;
      s.solo = v("solo", g) > .5;
      anySolo |= s.solo && s.enabled && s.module != 0;
      s.in = gain(v("input", g));
      s.out = gain(v("output", g));
      s.wet = v("wet", g) * .01;
      s.polarity = v("polarity", g) > .5 ? -1 : 1;
      s.amount = v("amount", g) * .01;
      // Retain host values in state; apply only this model's capabilities.
      s.external = rackControlUsed(s.module, "external") && v("external", g) > .5;
      s.listen = rackControlUsed(s.module, "listen") && v("listen", g) > .5;
      s.hfonly = v("hfonly", g) > .5;
      s.feedback = rackControlUsed(s.module, "feedback") && v("feedback", g) > .5;
      s.clip = v("clip", g) > .5;
      s.rate = v("rate", g);
      s.depth = v("depth", g) * .01;
      s.spread = v("spread", g) * .01;
      s.regen = v("feedbackamount", g) * .01;
      s.bias = v("bias", g) * .002;
      s.chorusMode = int(v("chorusmode", g));
      s.style = int(v("style", g));
      s.attack = v("attackamount", g);
      s.sustain = v("sustain", g);
      s.release = v("release", g);
      s.range = v("range", g);
      s.threshold = v("threshold", g);
      s.gateHold = v("gatehold", g) * .001 * fs;
      s.splitCoeff = Low::coeff(v("frequency", g), fs);
      for (int c = 0; c < 2; ++c) {
        auto &e = s.eq[size_t(c)];
        const double q =
            v("q", g) * (s.module == 9 ? 1 + std::abs(v("mid", g)) / 18 : 1);
        e[0].set(gill::LowShelf, v("lowfreq", g), v("low", g), .707, fs);
        e[1].set(gill::Bell, v("frequency", g), v("mid", g), q, fs);
        e[2].set(gill::Bell, v("upperfreq", g), v("uppermid", g),
                 s.module == 11 ? q * .7 : q, fs);
        e[3].set(gill::HighShelf, v("highfreq", g), v("high", g), .707, fs);
        e[4].set(gill::LowCut, v("highpass", g), 0, .707, fs);
        e[5].set(gill::HighCut, v("lowpass", g), 0, .707, fs);
        e[6].set(gill::LowShelf, v("lowfreq", g) * 1.6, -v("lowcutamount", g),
                 .707, fs);
        e[7].set(gill::HighShelf, v("highfreq", g) * .6, -v("highcutamount", g),
                 .707, fs);
        if (m.family == RackFamily::Air || m.family == RackFamily::Earth ||
            m.family == RackFamily::Revive || m.family == RackFamily::Lift) {
          e[0].set(gill::LowShelf, 100,
                   v("low", g) + (m.family == RackFamily::Earth ||
                                          m.family == RackFamily::Revive
                                      ? s.amount * 12
                                      : 0),
                   .6, fs);
          e[3].set(gill::HighShelf, v("frequency", g),
                   v("high", g) + (m.family == RackFamily::Air ||
                                           m.family == RackFamily::Revive
                                       ? s.amount * 12
                                       : 0),
                   .6, fs);
        }
        s.keyFilters[size_t(c)][0].set(gill::LowCut, rackControlUsed(s.module, "keylow") ? v("keylow", g) : 20, 0, .707,
                                       fs);
        s.keyFilters[size_t(c)][1].set(gill::HighCut, rackControlUsed(s.module, "keyhigh") ? v("keyhigh", g) : 20000, 0, .707,
                                       fs);
      }
      dy::Parameters p;
      p.curve.thresholdDb = s.threshold;
      p.curve.ratio =
          s.module == 26 ? std::max(10.f, v("ratio", g)) : v("ratio", g);
      p.curve.kneeDb = std::clamp(m.knee + v("knee", g), 0., 48.);
      p.curve.rangeDb = s.range;
      p.curve.law = m.family == RackFamily::Gate ? dy::Law::DownwardExpansion
                                                 : dy::Law::DownwardCompression;
      p.envelope.attackMs = v("attack", g) * m.attackScale;
      p.envelope.holdMs = v("gatehold", g);
      p.envelope.releaseFastMs = s.release * m.releaseScale;
      p.envelope.releaseSlowMs = p.envelope.releaseFastMs * 6;
      p.envelope.adaptive = std::clamp(m.memory + v("memory", g) * .01, 0., 1.);
      p.detector = m.memory > .4 ? dy::Detector::RMS : dy::Detector::Peak;
      p.key = dy::KeySource::External;
      p.stereoLink = channels == 1 && s.external ? 1 : v("link", g) * .01;
      s.dynamics.setParameters(p);
      p.curve.law = dy::Law::DownwardExpansion;
      p.curve.thresholdDb = v("gatethreshold", g);
      p.curve.rangeDb = v("gaterange", g);
      p.curve.ratio = 4;
      p.envelope.attackMs = 1;
      s.gate.setParameters(p);
      const bool standalone=m.family==RackFamily::Gate;
      s.gateThreshold=standalone ? s.threshold : v("gatethreshold",g);
      s.gateRange=standalone ? s.range : v("gaterange",g);
      s.gateRatio=standalone ? v("ratio",g) : 4;
      s.gateAttack=standalone ? v("attack",g) : 1;
      s.gateLink=standalone || (channels == 1 && s.external) ? 1 : v("link",g)*.01;
      dy::EnvelopeParameters envelope;
      envelope.attackMs = v("attack", g);
      envelope.releaseFastMs = v("release", g);
      s.deessEnvelope.setParameters(envelope);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
      return slots[size_t(a)].order == slots[size_t(b)].order
                 ? a < b
                 : slots[size_t(a)].order < slots[size_t(b)].order;
    });
  }
  std::array<double, 2> sample(std::array<double, 2> x,
                               std::array<double, 2> external) noexcept {
    for (int index : order) {
      auto &s = slots[size_t(index)];
      const auto &m = rackModels[s.module];
      if (m.family == RackFamily::Empty)
        continue;
      const auto original = x;
      auto y = x;
      for (auto &v : y)
        v *= s.in * s.polarity;
      auto trigger = s.external ? external : s.feedback ? s.last : y;
      for (int c = 0; c < 2; ++c)
        trigger[size_t(c)] = s.keyFilters[size_t(c)][1].tick(
            s.keyFilters[size_t(c)][0].tick(trigger[size_t(c)]));
      const double power =
          std::max(trigger[0] * trigger[0], trigger[1] * trigger[1]);
      s.fast += (1 - pole(1, fs)) * (power - s.fast);
      s.slow +=
          (1 - pole(m.family == RackFamily::Transient ? s.release : 40, fs)) *
          (power - s.slow);
      const double onset =
          std::clamp((s.fast - s.slow) / (s.fast + s.slow + 1e-15), 0., 1.);
      reduction[size_t(index)] = 0;
      if (m.family == RackFamily::Eq || m.family == RackFamily::Passive) {
        for (int c = 0; c < 2; ++c) {
          for (int f = 0; f < 6; ++f)
            y[size_t(c)] = s.eq[size_t(c)][size_t(f)].tick(y[size_t(c)]);
          if (m.family == RackFamily::Passive)
            for (int f = 6; f < 8; ++f)
              y[size_t(c)] = s.eq[size_t(c)][size_t(f)].tick(y[size_t(c)]);
          if (s.amount > 0)
            y[size_t(c)] =
                color(s, y[size_t(c)], c, 1 + s.amount * 8, m.colour);
        }
      } else if (m.family == RackFamily::Comp || m.family == RackFamily::Gate ||
                 m.family == RackFamily::Channel) {
        std::array<float, 2> a{float(y[0]), float(y[1])},
            k{float(trigger[0]), float(trigger[1])};
        float *ap[]{&a[0], &a[1]};
        const float *kp[]{&k[0], &k[1]};
        if(m.family!=RackFamily::Gate) s.dynamics.process(ap, 1, kp, 2);
        if(m.family==RackFamily::Gate || m.family==RackFamily::Channel) {
          // Peak envelope does not turn each waveform zero crossing into a
          // gate close. Hold belongs to the open state, never attenuation.
          for(int c=0;c<2;++c) s.gatePeak[c]=std::max(std::abs(trigger[c]),s.gatePeak[c]*pole(30,fs));
          const double linked=std::max(s.gatePeak[0],s.gatePeak[1]);
          for(int c=0;c<2;++c) {
            const double detected=s.gatePeak[c]+s.gateLink*(linked-s.gatePeak[c]);
            const double level=20*std::log10(std::max(1e-12,detected));
            if(level>=s.gateThreshold) {s.gateOpen[c]=true; s.gateTimer[c]=s.gateHold;}
            else if(s.gateOpen[c]) {
              if(level>=s.gateThreshold-3) s.gateTimer[c]=s.gateHold;
              else if(s.gateTimer[c]>0) --s.gateTimer[c];
              else s.gateOpen[c]=false;
            }
            const double target=s.gateOpen[c] ? 0 : -std::clamp((s.gateThreshold-level)*std::max(0.,s.gateRatio-1),0.,s.gateRange);
            s.gateDb[c]+=(1-pole(target>s.gateDb[c]?s.gateAttack:s.release,fs))*(target-s.gateDb[c]);
            a[c]*=float(gain(s.gateDb[c]));
          }
        }
        for (int c = 0; c < 2; ++c)
          y[size_t(c)] =
              m.colour > 0 && s.amount > 0
                  ? color(s, a[size_t(c)], c, 1 + s.amount * 8, m.colour)
                  : a[size_t(c)];
        reduction[size_t(index)] = float(std::max(
            m.family==RackFamily::Gate ? -s.gateDb[0] : s.dynamics.gainReductionDb(0)-(m.family==RackFamily::Channel ? s.gateDb[0] : 0), m.family==RackFamily::Gate ? -s.gateDb[1] : s.dynamics.gainReductionDb(1)-(m.family==RackFamily::Channel ? s.gateDb[1] : 0)));
      } else if (m.family == RackFamily::Deess) {
        std::array<double, 2> hf{};
        double full = 0, high = 0;
        for (int c = 0; c < 2; ++c) {
          hf[size_t(c)] = y[size_t(c)] - s.deessSplit[size_t(c)].tick(
                                             y[size_t(c)], s.splitCoeff);
          full = std::max(full, y[size_t(c)] * y[size_t(c)]);
          high = std::max(high, hf[size_t(c)] * hf[size_t(c)]);
        }
        s.fullPower += (1 - pole(10, fs)) * (full - s.fullPower);
        s.hfPower += (1 - pole(10, fs)) * (high - s.hfPower);
        const double detected =
            s.module == 41
                ? 10 * std::log10((s.hfPower + 1e-20) / (s.fullPower + 1e-20))
                : 10 * std::log10(s.hfPower + 1e-20);
        const double target = -std::clamp((detected - s.threshold) * .75, 0.,
                                          s.range),
                     db = s.deessEnvelope.process(target);
        reduction[size_t(index)] = float(-db);
        for (int c = 0; c < 2; ++c)
          y[size_t(c)] = s.listen ? hf[size_t(c)]
                         : s.hfonly
                             ? y[size_t(c)] + hf[size_t(c)] * (gain(db) - 1)
                             : y[size_t(c)] * gain(db);
      } else if (m.family == RackFamily::Drive ||
                 m.family == RackFamily::Console) {
        const double d = gain(30 * s.amount), skew = m.colour + s.bias;
        for (int c = 0; c < 2; ++c) {
          double filtered =
              s.eq[size_t(c)][5].tick(s.eq[size_t(c)][4].tick(y[size_t(c)]));
          if (s.style == 1)
            filtered = filtered / (1 + std::abs(filtered) * d * .2);
          else if (s.style == 2)
            filtered =
                std::sin(std::clamp(filtered * d, -pi * .5, pi * .5)) / d;
          y[size_t(c)] = color(s, filtered, c, d, skew);
        }
      } else if (m.family == RackFamily::Chorus) {
        const double rate =
            s.module == 29 ? .25 + (.13 * s.chorusMode) : s.rate;
        for (int c = 0; c < 2; ++c) {
          const double filtered =
              s.eq[size_t(c)][5].tick(s.eq[size_t(c)][4].tick(y[size_t(c)]));
          auto &r = s.delay[size_t(c)];
          r[s.pos] = filtered + s.regen * s.last[1 - size_t(c)];
          const double phase = s.phase + c * pi * s.spread;
          const double modulation = s.chorusMode == 3
                                        ? 2 / pi * std::asin(std::sin(phase))
                                        : std::sin(phase);
          const double delay =
              fs * (.012 + (s.module == 29 ? .002 * (1 + s.chorusMode)
                                           : .008 * s.depth) *
                               modulation);
          y[size_t(c)] = interpolate(r, s.pos, delay);
        }
        s.pos = (s.pos + 1) % s.delay[0].size();
        s.phase += 2 * pi * rate / fs;
        if (s.phase > 2 * pi)
          s.phase -= 2 * pi;
      } else if (m.family == RackFamily::Drum) {
        if (onset > .12 && 10 * std::log10(s.fast + 1e-20) > s.threshold)
          s.hold = s.gateHold + fs * .005;
        if (s.hold > 0)
          --s.hold;
        const double target = s.hold > 0 ? 1 : gain(-s.range);
        s.drumGain += (1 - pole(target > s.drumGain ? .1 : s.release, fs)) *
                      (target - s.drumGain);
        for (int c = 0; c < 2; ++c) {
          const double body =
              s.deessSplit[size_t(c)].tick(y[size_t(c)], s.splitCoeff);
          y[size_t(c)] = (body + (y[size_t(c)] - body) *
                                     (1 - s.amount * (1 - s.drumGain))) *
                         s.drumGain;
        }
        reduction[size_t(index)] =
            float(-20 * std::log10(std::max(1e-8, s.drumGain)));
      } else if (m.family == RackFamily::Transient) {
        const double tail = std::clamp(
                         (s.slow - s.fast) / (s.slow + s.fast + 1e-15), 0., 1.),
                     boost = gain(s.attack * onset + s.sustain * tail);
        for (auto &value : y) {
          value *= boost;
          if (s.amount > 0)
            value = std::tanh(value * (1 + s.amount * 3)) / (1 + s.amount * 3);
          if (s.clip)
            value = std::clamp(value, -1., 1.);
        }
      } else if (m.family == RackFamily::Air || m.family == RackFamily::Earth ||
                 m.family == RackFamily::Revive ||
                 m.family == RackFamily::Lift) {
        for (int c = 0; c < 2; ++c) {
          y[size_t(c)] =
              s.eq[size_t(c)][3].tick(s.eq[size_t(c)][0].tick(y[size_t(c)]));
          if (m.family == RackFamily::Lift)
            y[size_t(c)] *= gain(s.amount * 6 * (1 - onset));
          if (m.colour > 0)
            y[size_t(c)] =
                color(s, y[size_t(c)], c, 1 + s.amount * 4, m.colour);
        }
      }
      if (s.listen && m.family != RackFamily::Deess) {
        // Audition remains signed audio, independent of magnitude detection.
        y = channels == 1 ? std::array<double, 2>{.5 * (trigger[0] + trigger[1]),
                                                 .5 * (trigger[0] + trigger[1])}
                          : trigger;
      }
      s.last = y;
      s.active += (1 - pole(5, fs)) *
                  ((s.enabled && (!anySolo || s.solo) ? 1. : 0.) - s.active);
      for (int c = 0; c < 2; ++c)
        x[size_t(c)] = clean(original[size_t(c)] +
                             s.active * s.wet *
                                 (y[size_t(c)] * s.out - original[size_t(c)]));
    }
    return x;
  }
};
} // namespace studio

