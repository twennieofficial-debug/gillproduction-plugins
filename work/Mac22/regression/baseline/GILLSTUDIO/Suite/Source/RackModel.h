#pragma once
#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>
namespace studio {
enum class RackFamily {
  Empty,
  Eq,
  Comp,
  Gate,
  Deess,
  Drive,
  Air,
  Transient,
  Earth,
  Revive,
  Channel,
  Drum,
  Chorus,
  Passive,
  Console,
  Lift,
  Trim
};
struct RackModel {
  const char *name;
  RackFamily family;
  double colour = 0, knee = 6, attackScale = 1, releaseScale = 1, memory = 0;
};
// These are independently designed GILL response families, not calibrated
// copies of third-party hardware, microphone captures, presets, or proprietary
// circuits.
inline constexpr RackModel rackModels[]{
    {"EMPTY", RackFamily::Empty},
    {"TONE EQ", RackFamily::Eq},
    {"FAST COMP", RackFamily::Comp, 0, 3},
    {"OPTO COMP", RackFamily::Comp, 0, 9, 1, 1, .8},
    {"GATE", RackFamily::Gate},
    {"DEESS", RackFamily::Deess},
    {"DRIVE", RackFamily::Drive},
    {"AIR", RackFamily::Air},
    {"TRANSIENT", RackFamily::Transient},
    {"BRIT EQ", RackFamily::Eq, .12},
    {"CONSOLE EQ", RackFamily::Eq, .04},
    {"AMERICAN EQ", RackFamily::Eq, .08},
    {"CUSTOM EQ", RackFamily::Eq},
    {"PASSIVE EQ", RackFamily::Passive, .1},
    {"EARTH", RackFamily::Earth},
    {"SHIMMER", RackFamily::Air, .05},
    {"REVIVE", RackFamily::Revive, .08},
    {"FET VINTAGE", RackFamily::Comp, .16, 2, .08, 1, .12},
    {"FET MODERN", RackFamily::Comp, .03, 1, .05, .6, .08},
    {"FET CLASSIC", RackFamily::Comp, .1, 3, .1, .9, .18},
    {"TUBE OPTO", RackFamily::Comp, .1, 10, 1.4, 2, .9},
    {"FAST OPTO", RackFamily::Comp, .05, 8, .7, 1.2, .7},
    {"CUSTOM OPTO", RackFamily::Comp, 0, 12, 1, 1, .5},
    {"VCA BUS", RackFamily::Comp, .03, 4, .5, 1, .15},
    {"CHANNEL DYNAMICS", RackFamily::Channel, .04, 3, .2, 1, .2},
    {"STRESS", RackFamily::Comp, .18, 4, .3, .7, .3},
    {"CRUSH", RackFamily::Comp, .25, 1, .03, .4, .1},
    {"VARI MU", RackFamily::Comp, .12, 18, 1.7, 3, .95},
    {"DRUM GATE", RackFamily::Drum},
    {"CLASSIC CHORUS", RackFamily::Chorus},
    {"CUSTOM CHORUS", RackFamily::Chorus},
    {"PRE DISCRETE", RackFamily::Drive, .09},
    {"PRE TUBE", RackFamily::Drive, .22},
    {"PRE BRIGHT", RackFamily::Drive, .07},
    {"PRE SILK", RackFamily::Drive, .03},
    {"PRE THICK", RackFamily::Drive, .15},
    {"CONSOLE CHANNEL", RackFamily::Console, .04},
    {"CONSOLE BUS", RackFamily::Console, .025},
    {"LIFT", RackFamily::Lift},
    {"PUNCH", RackFamily::Transient, .1},
    {"TRIM", RackFamily::Trim},
    {"RELATIVE DEESS", RackFamily::Deess}};
inline constexpr int rackModelCount =
    int(sizeof(rackModels) / sizeof(rackModels[0]));
inline bool rackControlUsed(int module, std::string_view key) noexcept {
  module = std::clamp(module, 0, rackModelCount - 1);
  const auto family = rackModels[module].family;
  if (key == "module" || key == "order" || key == "enable" || key == "solo")
    return true;
  if (family == RackFamily::Empty)
    return false;
  if (key == "input" || key == "output" || key == "wet" || key == "polarity")
    return true;
  auto is = [&](std::initializer_list<std::string_view> keys) {
    for (auto k : keys)
      if (k == key)
        return true;
    return false;
  };
  if (family == RackFamily::Eq || family == RackFamily::Passive)
    return is({"low", "mid", "high", "uppermid", "frequency", "upperfreq",
               "lowfreq", "highfreq", "q", "highpass", "lowpass", "amount"}) ||
           (family == RackFamily::Passive &&
            is({"lowcutamount", "highcutamount"}));
  if (family == RackFamily::Comp || family == RackFamily::Channel)
    return (key == "amount" && rackModels[module].colour > 0) ||
           is({"threshold", "ratio", "attack", "release", "keylow",
               "keyhigh", "external", "listen", "link", "range", "feedback",
               "knee", "memory"}) ||
           (family == RackFamily::Channel &&
            is({"gatethreshold", "gatehold", "gaterange"}));
  if (family == RackFamily::Gate || family == RackFamily::Drum)
    return is({"threshold", "release", "keylow", "keyhigh", "external",
               "listen", "range", "gatehold"}) ||
           (family == RackFamily::Gate && is({"ratio", "attack"})) ||
           (family == RackFamily::Drum && is({"amount", "frequency"}));
  if (family == RackFamily::Deess)
    return is({"frequency", "threshold", "attack", "release", "range", "listen",
               "hfonly"});
  if (family == RackFamily::Drive || family == RackFamily::Console)
    return is({"amount", "highpass", "lowpass", "bias", "style"});
  if (family == RackFamily::Chorus)
    return is({"spread", "feedbackamount", "highpass", "lowpass",
               "chorusmode"}) ||
           (module == 30 && is({"rate", "depth"}));
  if (family == RackFamily::Transient)
    return is({"attackamount", "sustain", "release", "keylow", "keyhigh",
               "external", "listen", "amount", "clip"});
  if (family == RackFamily::Air || family == RackFamily::Earth ||
      family == RackFamily::Revive || family == RackFamily::Lift)
    return is({"amount", "low", "high", "frequency"});
  return false;
}
} // namespace studio
