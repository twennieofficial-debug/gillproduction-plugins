#pragma once
#include "RackModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
namespace studio {
enum class Kind {
  Transient,
  Room,
  Multi,
  Tape,
  Vital,
  Eq825,
  Alpha,
  Max,
  Deess,
  Harmonic,
  Precision,
  Rack
};
inline constexpr const char *names[] = {
    "GILLTRANSIENT", "GILLROOM",     "GILLMULTI",     "GILLTAPE",
    "GILLVITAL",     "GILL825",      "GILLALPHA",     "GILLMAX",
    "GILLDEESSPRO",  "GILLHARMONIC", "GILLPRECISION", "GILLRACK"};
struct Spec {
  std::string id, key, name, unit;
  int group = 0;
  float lo = 0, hi = 1, def = 0, skew = 1;
  std::vector<std::string> choices;
};
struct Definition {
  std::vector<Spec> params;
  std::vector<std::string> groups;
};
inline Definition definition(Kind kind) {
  Definition d;
  d.groups = {"MAIN"};
  auto num = [&](const char *k, const char *n, float lo, float hi, float v,
                 const char *u = "", int group = 0, float skew = 1.f) {
    Spec s;
    s.key = k;
    s.id = group > 0 ? "b" + std::to_string(group) + "_" + k : k;
    s.name = n;
    s.unit = u;
    s.group = group;
    s.lo = lo;
    s.hi = hi;
    s.def = v;
    s.skew = skew;
    d.params.push_back(s);
  };
  auto choice = [&](const char *k, const char *n,
                    std::initializer_list<const char *> items, int value = 0,
                    int group = 0) {
    num(k, n, 0, float(items.size() - 1), float(value), "", group);
    for (auto *item : items)
      d.params.back().choices.emplace_back(item);
  };
  auto on = [&](const char *k, const char *n, bool value = false,
                int group = 0) {
    choice(k, n, {"OFF", "ON"}, value ? 1 : 0, group);
  };
  num("input", "INPUT", -24, 24, 0, "dB", -1);
  num("output", "OUTPUT", -24, 24, 0, "dB", -1);
  num("mix", "MIX", 0, 100, 100, "%", -1);
  on("bypass", "BYPASS", false, -1);
  on("delta", "DIFFERENCE", false, -1);
  auto band = [&](int g, float low, float high) {
    num("low", "LOW EDGE", 20, 20000, low, "Hz", g, .25f);
    num("high", "HIGH EDGE", 20, 20000, high, "Hz", g, .25f);
    on("enable", "ENABLED", true, g);
    on("solo", "SOLO", false, g);
    num("threshold", "THRESHOLD", -80, 12, -24, "dB", g);
    num("ratio", "RATIO", 1, 20, 2, ":1", g, .5f);
    num("range", "RANGE", 0, 48, 12, "dB", g);
    num("knee", "KNEE", 0, 36, 6, "dB", g);
    num("attack", "ATTACK", .05f, 1000, 10, "ms", g, .3f);
    num("hold", "HOLD", 0, 1000, 0, "ms", g, .3f);
    num("release", "FAST RELEASE", 1, 4000, 120, "ms", g, .3f);
    num("slow", "SLOW RELEASE", 1, 8000, 500, "ms", g, .3f);
    num("adapt", "ADAPTIVE", 0, 100, 40, "%", g);
    num("gain", "GAIN", -24, 24, 0, "dB", g);
    choice("law", "ACTION", {"COMPRESS", "LIFT", "EXPAND DOWN", "EXPAND UP"}, 0,
           g);
    choice("channel", "CHANNEL", {"STEREO", "LEFT", "RIGHT", "MID", "SIDE"}, 0,
           g);
    num("link", "LINK", 0, 100, 100, "%", g);
    num("rms", "RMS WINDOW", 0, 200, 10, "ms", g, .4f);
    on("external", "EXTERNAL KEY", false, g);
    num("keylow", "KEY LOW", 20, 20000, low, "Hz", g, .25f);
    num("keyhigh", "KEY HIGH", 20, 20000, high, "Hz", g, .25f);
    on("listen", "LISTEN KEY", false, g);
  };
  switch (kind) {
  case Kind::Tape:
    num("drive", "TAPE DRIVE", 0, 30, 3, "dB");
    choice("machine", "MACHINE", {"MULTITRACK", "MASTER"});
    choice("tape", "FORMULATION", {"WARM", "CLEAN"});
    choice("speed", "SPEED", {"15 IPS", "30 IPS"});
    choice("bias", "BIAS", {"LOW", "NORMAL", "HIGH"}, 1);
    num("calibration", "CALIBRATION", -24, -6, -18, "dBFS");
    num("bass", "BASS ALIGN", -6, 6, 0, "dB");
    num("wow", "WOW", 0, 100, 0, "%");
    num("flutter", "FLUTTER", 0, 100, 0, "%");
    num("noise", "HISS", -100, -30, -80, "dBFS");
    on("hiss", "HISS ENABLE");
    on("automute", "HISS AUTOMUTE", true);
    on("compensate", "DRIVE COMPENSATION", true);
    break;
  case Kind::Vital:
    num("direct", "DIRECT", -60, 0, 0, "dB");
    on("directoff", "DIRECT OFF");
    num("punch", "PUNCH", 0, 5, 1);
    for (int i = 1; i <= 5; ++i) {
      d.groups.push_back("BAND " + std::to_string(i));
      num("amount", "ENHANCE", 0, 100, 0, "%", i);
      num("drive", "DRIVE", 0, 36, 12, "dB", i);
      num("gain", "WET GAIN", 0, 48, 0, "dB", i);
      num("width", "WIDTH", 0, 300, 100, "%", i);
      on("bypass", "WET BYPASS", false, i);
      on("solo", "SOLO", false, i);
      if (i < 5)
        num("edge", "UPPER CROSSOVER", 20, 18000,
            std::array<float, 4>{90, 500, 2594, 9310}[size_t(i - 1)], "Hz", i,
            .25f);
    }
    break;
  case Kind::Harmonic:
    num("amount", "AMOUNT", 0, 200, 25, "%");
    num("drive", "DRIVE", 0, 36, 12, "dB");
    choice("shape", "HARMONICS", {"SOFT", "HARD", "POLY 3", "POLY 5"});
    on("subtle", "SUBTLE");
    num("subtlegain", "SUBTLE GAIN", -24, 12, 0, "dB");
    num("even", "SECOND HARMONIC", 0, 100, 0, "%");
    on("adaptive", "ADAPTIVE", true);
    on("mastering", "DRY SUBTRACTION", true);
    num("prelow", "PRE HIGH PASS", 20, 20000, 1500, "Hz", 0, .25f);
    num("prehigh", "PRE LOW PASS", 20, 20000, 18000, "Hz", 0, .25f);
    num("postlow", "POST HIGH PASS", 20, 20000, 1500, "Hz", 0, .25f);
    num("posthigh", "POST LOW PASS", 20, 20000, 18000, "Hz", 0, .25f);
    num("width", "WET WIDTH", 0, 300, 100, "%");
    choice("channel", "GENERATOR", {"STEREO", "LEFT", "RIGHT", "MID", "SIDE"});
    on("wetonly", "WET ONLY");
    break;
  case Kind::Eq825:
    choice("routing", "ROUTING", {"L/R", "M/S"});
    num("thd", "HARMONICS", 0, 100, 0, "%");
    num("calibration", "CALIBRATION", -24, -6, -18, "dBFS");
    num("highpass", "HIGH PASS", 15, 300, 15, "Hz", 0, .4f);
    num("lowpass", "LOW PASS", 3000, 40000, 40000, "Hz", 0, .4f);
    on("cuts", "FILTERS ON");
    on("monobass", "MONO BELOW 200");
    num("filler", "OCTAVE FILL", 0, 100, 0, "%");
    num("fillfreq", "FILL CENTER", 40, 400, 120, "Hz", 0, .5f);
    for (int c = 1; c <= 2; ++c) {
      d.groups.push_back(c == 1 ? "LEFT / MID" : "RIGHT / SIDE");
      const char *keys[] = {"low", "lowmid", "mid", "highmid", "high"};
      const char *frequencyNames[] = {"LOW FREQUENCY", "LOW MID FREQUENCY",
                                      "MID FREQUENCY", "HIGH MID FREQUENCY",
                                      "HIGH FREQUENCY"};
      const float fr[] = {60, 350, 2500, 9000, 10000},
                  lo[] = {20, 60, 1500, 6000, 3000},
                  hi[] = {100, 1000, 5000, 16000, 20000};
      for (int b = 0; b < 5; ++b) {
        const std::string f = std::string(keys[b]) + "freq";
        num(keys[b], keys[b], -12, 12, 0, "dB", c);
        num(f.c_str(), frequencyNames[b], lo[b], hi[b], fr[b], "Hz", c, .5f);
      }
      num("q", "PROPORTIONAL Q", .2f, 3, 1, "", c);
    }
    break;
  case Kind::Multi:
  case Kind::Precision:
  case Kind::Deess:
    choice("routing", "ROUTING", {"L/R", "M/S"});
    num("depth", "DYNAMICS DEPTH", 0, 200, 100, "%");
    num("coupling", "GLOBAL LINK", 0, 100, 100, "%");
    {
      const int count = kind == Kind::Multi       ? 6
                        : kind == Kind::Precision ? 5
                                                  : 2;
      const float edges[] = {20, 100, 400, 1600, 5000, 10000, 20000};
      for (int i = 1; i <= count; ++i) {
        d.groups.push_back("BAND " + std::to_string(i));
        band(i, kind == Kind::Deess ? (i == 1 ? 2500.f : 6500.f) : edges[i - 1],
             kind == Kind::Deess ? (i == 1 ? 6500.f : 20000.f)
             : i == count        ? 20000.f
                                 : edges[i]);
      }
    }
    break;
  case Kind::Alpha:
    choice("routing", "ROUTING", {"L/R", "M/S"});
    choice("topology", "DETECTION", {"FEEDFORWARD", "FEEDBACK"});
    num("link", "STEREO LINK", 0, 100, 100, "%");
    num("sclow", "KEY HIGH PASS", 20, 1000, 60, "Hz", 0, .3f);
    num("schigh", "KEY LOW PASS", 1000, 20000, 20000, "Hz", 0, .3f);
    on("external", "EXTERNAL KEY");
    num("tilt", "TILT EQ", -6, 6, 0, "dB");
    num("tiltfreq", "TILT CENTER", 100, 10000, 1000, "Hz", 0, .3f);
    num("drive", "TRANSFORMER", 0, 24, 0, "dB");
    num("clip", "SOFT CLIP", -18, 6, 0, "dBFS");
    on("clipon", "CLIP ON");
    num("width", "WIDTH", 0, 200, 100, "%");
    on("monobass", "MONO BELOW 120");
    for (int i = 1; i <= 2; ++i) {
      d.groups.push_back(i == 1 ? "LEFT / MID" : "RIGHT / SIDE");
      num("threshold", "THRESHOLD", -60, 12, -18, "dB", i);
      num("ratio", "RATIO", 1, 20, 2, ":1", i);
      num("knee", "KNEE", 0, 24, 6, "dB", i);
      num("range", "RANGE", 0, 36, 12, "dB", i);
      num("attack", "ATTACK", .05f, 200, 30, "ms", i, .3f);
      num("release", "RELEASE", 10, 3000, 200, "ms", i, .3f);
      on("autoattack", "AUTO ATTACK", false, i);
      num("adaptive", "AUTO RELEASE", 0, 100, 50, "%", i);
      num("makeup", "MAKEUP", -18, 18, 0, "dB", i);
    }
    break;
  case Kind::Max:
    num("drive", "DRIVE", 0, 24, 0, "dB");
    num("ceiling", "CEILING", -12, 6, -1, "dBFS");
    num("release", "CHARACTER", 20, 500, 90, "ms");
    num("clip", "SOFT CLIP", 0, 100, 0, "%");
    num("upward", "QUIET LIFT", 0, 12, 0, "dB");
    num("transient", "TRANSIENT EMPHASIS", 0, 100, 0, "%");
    num("target", "LEARN TARGET", -24, -5, -12, "LUFS");
    break;
  case Kind::Room:
    choice("style", "STYLE", {"MODERN", "VINTAGE", "PLATE"});
    num("space", "SPACE", 0, 100, 50, "%");
    num("decay", "DECAY", .2f, 15, 2.5f, "s", 0, .4f);
    num("predelay", "PREDELAY", 0, 200, 20, "ms");
    num("brightness", "BRIGHTNESS", 0, 100, 55, "%");
    num("distance", "DISTANCE", 0, 100, 20, "%");
    num("thickness", "THICKNESS", 0, 100, 40, "%");
    num("width", "WIDTH", 0, 100, 100, "%");
    num("duck", "DUCKING", 0, 100, 0, "%");
    num("gate", "GATE LENGTH", 0, 4000, 0, "ms");
    num("dry", "DIRECT", 0, 100, 100, "%");
    on("freeze", "FREEZE");
    num("decaymult", "DECAY MULTIPLIER", .2f, 3, 1, "x");
    for (int i = 1; i <= 6; ++i) {
      d.groups.push_back("POST EQ " + std::to_string(i));
      on("enable", "ENABLED", false, i);
      choice("type", "FILTER",
             {"BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT"}, 0, i);
      num("frequency", "FREQUENCY", 20, 20000,
          float(60 * std::pow(200., (i - 1) / 5.)), "Hz", i, .25f);
      num("gain", "GAIN", -24, 24, 0, "dB", i);
      num("q", "Q", .1f, 12, 1, "", i, .4f);
    }
    d.groups.push_back("DECAY COLOR");
    for (int i = 0; i < 6; ++i) {
      const std::string key = "decay" + std::to_string(i);
      const char *labels[] = {"SUB / 80 HZ", "BODY / 300 HZ",
                              "MID / 1 KHZ", "PRESENCE / 3 KHZ",
                              "AIR / 8 KHZ", "TOP / 14 KHZ"};
      num(key.c_str(), labels[i], .2f, 3, 1, "x", 7);
    }
    break;
  case Kind::Transient:
    num("amount", "DEPTH", 0, 100, 0, "%");
    choice("action", "ACTION", {"CUT", "BOOST"});
    num("sensitivity", "SENSITIVITY", 0, 100, 50, "%");
    num("sharpness", "SHARPNESS", 0, 100, 50, "%");
    num("decay", "DECAY", 5, 2000, 100, "ms", 0, .3f);
    num("attack", "ATTACK", .05f, 100, 1, "ms", 0, .3f);
    num("release", "RELEASE", 1, 1000, 80, "ms", 0, .3f);
    num("link", "STEREO LINK", 0, 100, 100, "%");
    choice("routing", "ROUTING", {"L/R", "M/S"});
    for (int i = 1; i <= 5; ++i) {
      d.groups.push_back("WEIGHT " + std::to_string(i));
      on("enable", "ENABLED", false, i);
      choice("shape", "SHAPE",
             {"BELL", "LOW SHELF", "HIGH SHELF", "TILT", "NOTCH", "LOW CUT",
              "HIGH CUT", "BANDPASS", "GAIN"},
             0, i);
      num("frequency", "FREQUENCY", 20, 20000,
          float(80 * std::pow(200., (i - 1) / 4.)), "Hz", i, .25f);
      num("gain", "WEIGHT", -48, 24, 0, "dB", i);
      num("q", "Q", .1f, 20, 1, "", i, .4f);
      num("slope", "SLOPE", 6, 96, 12, "dB/oct", i);
    }
    break;
  case Kind::Rack:
    for (int i = 1; i <= 8; ++i) {
      d.groups.push_back("SLOT " + std::to_string(i));
      num("module", "MODULE", 0, float(rackModelCount - 1), 0, "", i);
      for (const auto &m : rackModels)
        d.params.back().choices.emplace_back(m.name);
      on("enable", "ENABLED", true, i);
      on("solo", "SOLO", false, i);
      num("order", "POSITION", 1, 8, float(i), "", i);
      num("input", "INPUT", -24, 24, 0, "dB", i);
      num("output", "OUTPUT", -24, 24, 0, "dB", i);
      num("low", "LOW", -18, 18, 0, "dB", i);
      num("mid", "MID", -18, 18, 0, "dB", i);
      num("high", "HIGH", -18, 18, 0, "dB", i);
      num("frequency", "FREQUENCY", 100, 16000, 3500, "Hz", i, .3f);
      num("threshold", "THRESHOLD", -72, 0, -24, "dB", i);
      num("ratio", "RATIO", 1, 20, 4, ":1", i);
      num("attack", "ATTACK", .05f, 200, 10, "ms", i, .3f);
      num("release", "RELEASE", 5, 2000, 150, "ms", i, .3f);
      num("amount", "AMOUNT", 0, 100, 0, "%", i);
      num("wet", "SLOT MIX", 0, 100, 100, "%", i);
      on("polarity", "INVERT PHASE", false, i);
      num("uppermid", "UPPER MID", -18, 18, 0, "dB", i);
      num("upperfreq", "UPPER FREQUENCY", 600, 16000, 6000, "Hz", i, .3f);
      num("lowfreq", "LOW FREQUENCY", 20, 500, 100, "Hz", i, .3f);
      num("highfreq", "HIGH FREQUENCY", 1500, 20000, 10000, "Hz", i, .3f);
      num("q", "BAND Q", .2f, 6, 1, "", i);
      num("highpass", "HIGH PASS", 10, 3000, 10, "Hz", i, .25f);
      num("lowpass", "LOW PASS", 1000, 40000, 40000, "Hz", i, .25f);
      num("lowcutamount", "LOW ATTENUATION", 0, 18, 0, "dB", i);
      num("highcutamount", "HIGH ATTENUATION", 0, 18, 0, "dB", i);
      num("keylow", "DETECTOR LOW", 20, 10000, 20, "Hz", i, .25f);
      num("keyhigh", "DETECTOR HIGH", 100, 20000, 20000, "Hz", i, .25f);
      on("external", "EXTERNAL KEY", false, i);
      on("listen", "LISTEN KEY", false, i);
      num("link", "STEREO LINK", 0, 100, 100, "%", i);
      num("range", "MAX REDUCTION", 0, 72, 24, "dB", i);
      on("feedback", "FEEDBACK DETECTOR", false, i);
      num("knee", "EXTRA KNEE", 0, 24, 0, "dB", i);
      num("memory", "EXTRA MEMORY", 0, 100, 0, "%", i);
      num("gatethreshold", "GATE THRESHOLD", -90, -6, -60, "dB", i);
      num("gatehold", "GATE HOLD", 0, 500, 20, "ms", i);
      num("gaterange", "GATE RANGE", 0, 72, 24, "dB", i);
      on("hfonly", "HF ONLY", true, i);
      num("rate", "CHORUS RATE", .05f, 5, .5f, "Hz", i, .4f);
      num("depth", "CHORUS DEPTH", 0, 100, 40, "%", i);
      num("spread", "STEREO SPREAD", 0, 100, 80, "%", i);
      num("feedbackamount", "CHORUS FEEDBACK", -70, 70, 0, "%", i);
      choice("chorusmode", "CHORUS MODE", {"I", "II", "III", "IV"}, 0, i);
      num("bias", "ASYMMETRY", -100, 100, 0, "%", i);
      choice("style", "DRIVE STYLE", {"SMOOTH", "DENSE", "FOLDED"}, 0, i);
      num("attackamount", "TRANSIENT", -18, 18, 0, "dB", i);
      num("sustain", "SUSTAIN", -18, 18, 0, "dB", i);
      on("clip", "SAFETY CLIP", false, i);
    }
    break;
  }
  return d;
}
struct Values {
  const Definition &definition;
  const std::array<float, 1024> &values;
  float operator()(std::string_view key, int group = 0) const noexcept {
    for (size_t i = 0; i < definition.params.size(); ++i)
      if (definition.params[i].group == group &&
          definition.params[i].key == key)
        return values[i];
    return 0;
  }
};
} // namespace studio
