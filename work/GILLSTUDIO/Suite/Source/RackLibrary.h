#pragma once
#include "RackModel.h"
#include <vector>
namespace studio {
// A balanced front-page library. Every older module ID remains valid in saved
// sessions.
inline constexpr int rackCollection[]{9,  10, 11, 12, 13, 14, 15, 16, 17, 20,
                                      23, 25, 26, 27, 24, 4,  28, 5,  8,  29,
                                      30, 31, 32, 33, 34, 35, 36, 38, 40, 41};
inline const char *rackRole(int m) {
  switch (rackModels[std::clamp(m, 0, rackModelCount - 1)].family) {
  case RackFamily::Eq:
    return "TONE EQ";
  case RackFamily::Passive:
    return "PASSIVE EQ";
  case RackFamily::Comp:
    return "COMPRESSION";
  case RackFamily::Channel:
    return "COMP + GATE";
  case RackFamily::Gate:
    return "EXPAND / GATE";
  case RackFamily::Drum:
    return "DRUM CLEANUP";
  case RackFamily::Deess:
    return "SIBILANCE";
  case RackFamily::Drive:
    return "PREAMP COLOR";
  case RackFamily::Console:
    return "CONSOLE DRIVE";
  case RackFamily::Chorus:
    return "STEREO MOTION";
  case RackFamily::Transient:
    return "ATTACK / TAIL";
  case RackFamily::Air:
    return "AIR / SHELF";
  case RackFamily::Earth:
    return "LOW END";
  case RackFamily::Revive:
    return "BASS + AIR";
  case RackFamily::Lift:
    return "BODY LIFT";
  case RackFamily::Trim:
    return "GAIN / PHASE";
  default:
    return "EMPTY";
  }
}

// Front-panel order is an editor-only projection. Host IDs and saved values are unchanged.
inline std::vector<const char*> rackPanelOrder(int m) {
  const auto f = rackModels[std::clamp(m, 0, rackModelCount - 1)].family;
  std::vector<const char*> keys;
  if (f == RackFamily::Eq || f == RackFamily::Passive)
    keys = {"low", "lowfreq", "mid", "frequency", "uppermid", "upperfreq", "high", "highfreq",
            "q", "highpass", "lowpass", "amount", "lowcutamount", "highcutamount"};
  else if (f == RackFamily::Comp || f == RackFamily::Channel)
    keys = {"threshold", "ratio", "attack", "release", "knee", "range", "memory", "amount",
            "keylow", "keyhigh", "external", "listen", "link", "feedback", "gatethreshold", "gatehold", "gaterange"};
  else if (f == RackFamily::Gate || f == RackFamily::Drum)
    keys = {"threshold", "ratio", "attack", "release", "gatehold", "range", "amount", "frequency",
            "keylow", "keyhigh", "external", "listen"};
  else if (f == RackFamily::Deess)
    keys = {"threshold", "frequency", "range", "hfonly", "attack", "release", "listen"};
  else if (f == RackFamily::Drive || f == RackFamily::Console)
    keys = {"amount", "bias", "style", "highpass", "lowpass"};
  else if (f == RackFamily::Chorus)
    keys = {"chorusmode", "rate", "depth", "spread", "feedbackamount", "highpass", "lowpass"};
  else if (f == RackFamily::Transient)
    keys = {"attackamount", "sustain", "release", "amount", "clip", "keylow", "keyhigh", "external", "listen"};
  else if (f != RackFamily::Empty && f != RackFamily::Trim)
    keys = {"amount", "low", "high", "frequency"};
  if (f != RackFamily::Empty) keys.insert(keys.end(), {"input", "output", "wet", "polarity"});
  return keys;
}
inline int rackControlSection(int m, std::string_view key) {
  if (key == "input" || key == "output" || key == "wet" || key == "polarity") return 2;
  const auto f = rackModels[m].family;
  if (f == RackFamily::Eq || f == RackFamily::Passive)
    return key == "q" || key == "highpass" || key == "lowpass" || key == "amount" || key == "lowcutamount" || key == "highcutamount" ? 1 : 0;
  if (key == "keylow" || key == "keyhigh" || key == "external" || key == "listen" || key == "link" || key == "feedback" || key == "gatethreshold" || key == "gatehold" || key == "gaterange") return 1;
  return 0;
}
inline const char* rackPanelHint(int m) {
  switch (rackModels[m].family) {
  case RackFamily::Eq: return "4 BANDS  /  SHARED BELL Q  /  FILTERS + DRIVE";
  case RackFamily::Passive: return "4 BANDS  /  BOOST + ATTENUATION  /  FILTERS + DRIVE";
  case RackFamily::Comp: return "DYNAMICS  /  DETECTOR + KEY  /  SLOT LEVELS";
  case RackFamily::Channel: return "COMPRESSION  /  KEY + GATE  /  SLOT LEVELS";
  case RackFamily::Gate: return "EXPANSION  /  HOLD + RANGE  /  FILTERED KEY";
  case RackFamily::Drum: return "ONSET GATE  /  TAIL + SPILL  /  FILTERED KEY";
  case RackFamily::Deess: return m == 41 ? "HF / FULL-BAND RATIO DETECTOR  /  SPLIT OR WIDE" : "ABSOLUTE HF DETECTOR  /  SPLIT OR WIDE";
  case RackFamily::Chorus: return m == 29 ? "MODE SETS RATE + DEPTH  /  STEREO + FEEDBACK" : "VARIABLE RATE + DEPTH  /  MODE IV TRIANGLE";
  case RackFamily::Drive: case RackFamily::Console: return "DRIVE + ASYMMETRY  /  TRANSFER STYLE  /  FILTERS";
  case RackFamily::Transient: return "ATTACK + TAIL  /  SATURATION + CLIP  /  KEY";
  case RackFamily::Air: return "AIR SHELF + COLOR  /  LOW + HIGH TRIM";
  case RackFamily::Earth: return "100 Hz BODY SHELF  /  HIGH TRIM + FREQUENCY";
  case RackFamily::Revive: return "BASS + AIR + COLOR  /  SHELF TRIMS";
  case RackFamily::Lift: return "BODY GAIN FOLLOWS ENVELOPE  /  SHELF TRIMS";
  case RackFamily::Trim: return "INPUT + OUTPUT  /  PARALLEL MIX  /  POLARITY";
  default: return "";
  }
}
inline const char* rackControlLabel(int m, std::string_view key, const char* fallback) {
  const auto f = rackModels[m].family;
  if (key == "amount") {
    if (f == RackFamily::Drum) return "SPILL REDUCE";
    if (f == RackFamily::Air) return "AIR + COLOR";
    if (f == RackFamily::Earth) return "BODY BOOST";
    if (f == RackFamily::Revive) return "BASS + AIR";
    if (f == RackFamily::Lift) return "BODY LIFT";
    return "DRIVE";
  }
  if (key == "frequency") {
    if (f == RackFamily::Eq || f == RackFamily::Passive) return "MID FREQ";
    if (f == RackFamily::Drum) return "SPILL SPLIT";
    if (f == RackFamily::Deess) return "HF SPLIT";
    return "HIGH FREQ";
  }
  if (key == "lowfreq") return "LOW FREQ";
  if (key == "upperfreq") return "UPPER FREQ";
  if (key == "highfreq") return "HIGH FREQ";
  if (key == "lowcutamount") return "LOW ATTEN";
  if (key == "highcutamount") return "HIGH ATTEN";
  if (key == "feedback") return "FEEDBACK KEY";
  if (key == "keylow") return "KEY HIGH PASS";
  if (key == "keyhigh") return "KEY LOW PASS";
  if (key == "feedbackamount") return "FEEDBACK";
  if (key == "rate") return "RATE";
  if (key == "depth") return "DEPTH";
  if (key == "chorusmode") return "MODE";
  return fallback;
}
inline const char* rackControlHint(int m, std::string_view key) {
  if (key == "memory") return "Extra adaptive release memory, added to the model's base memory and limited to 100%.";
  if (key == "knee") return "Extra knee width, added to the model's base knee.";
  if (key == "ratio" && m == 26) return "Crush has a minimum ratio of 10:1; values below 10 retain 10:1 for saved-state compatibility.";
  if (key == "feedback") return "Use the previous wet output as detector input. External Key overrides this when enabled.";
  if (key == "external") return "Use the host sidechain input. Connect a sidechain source in your host.";
  if (key == "listen") return "Audition the filtered detector signal; temporarily overrides normal processed output.";
  if (key == "q") return "Shared Q for both mid bells. A bell's frequency and Q become audible when its gain is nonzero.";
  if (key == "chorusmode" && m == 30) return "Modes I-III use the same sine waveform with freely adjustable rate/depth. Mode IV uses a triangle waveform.";
  if (key == "frequency" && (m == 14 || m == 38)) return "High shelf frequency. Audible when High gain is nonzero; the bass shelf stays at 100 Hz.";
  if (key == "clip") return "Limits wet signal to +/-1 before slot output gain. Audible only when the signal reaches that level.";
  if (key == "amount") return "Module-specific drive or enhancement. See the front-panel label and role.";
  return "";
}
} // namespace studio

