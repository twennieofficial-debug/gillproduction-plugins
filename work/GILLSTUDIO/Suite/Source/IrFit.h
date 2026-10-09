#pragma once
#include "Tone.h"
#include <numeric>
#include <string>
namespace studio {
struct IrFit {
  bool valid = false;
  double decay = 1, predelay = 0;
  std::array<double, 6> ratios{1, 1, 1, 1, 1, 1};
  std::string status;
};
// Fit broadband and six band-limited energy-decay slopes to an impulse
// response. This estimates settings for our algorithmic tank; it does not
// convolve the IR.
inline IrFit fitImpulse(const std::vector<float> &mono, double fs) {
  IrFit result;
  if (fs < 8000 || fs > 192000 || mono.size() < size_t(fs * .08) ||
      mono.size() > size_t(fs * 30)) {
    result.status = "IR: use an 80 ms to 30 s WAV";
    return result;
  }
  double peak = 0;
  for (float x : mono)
    if (std::isfinite(x))
      peak = std::max(peak, std::abs(double(x)));
  if (peak < 1e-7) {
    result.status = "IR: no usable signal";
    return result;
  }
  size_t onset = 0;
  while (onset < mono.size() && std::abs(double(mono[onset])) < peak * .02)
    ++onset;
  if (onset + size_t(fs * .08) >= mono.size()) {
    result.status = "IR: tail too short";
    return result;
  }
  result.predelay = std::clamp(1000. * onset / fs, 0., 200.);
  Split split;
  split.set({160, 600, 2000, 6000, 12000}, 6, fs);
  constexpr size_t hop = 64;
  const size_t points = (mono.size() - onset + hop - 1) / hop;
  std::array<std::vector<double>, 7> energy;
  for (auto &v : energy)
    v.assign(points, 0.);
  for (size_t i = onset; i < mono.size(); ++i) {
    const double x = std::isfinite(mono[i]) ? mono[i] : 0;
    const auto bands = split.tick({x, x});
    const size_t at = (i - onset) / hop;
    energy[6][at] += x * x;
    for (int b = 0; b < 6; ++b)
      energy[size_t(b)][at] += bands[size_t(b)][0] * bands[size_t(b)][0];
  }
  auto estimate = [&](std::vector<double> e) {
    double total = 0;
    for (auto it = e.rbegin(); it != e.rend(); ++it) {
      total += *it;
      *it = total;
    }
    if (total < 1e-14)
      return 0.;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (size_t i = 0; i < e.size(); ++i) {
      const double db = 10 * std::log10(std::max(1e-30, e[i] / total));
      if (db > -5 || db < -30)
        continue;
      const double t = i * hop / fs;
      sx += t;
      sy += db;
      sxx += t * t;
      sxy += t * db;
      ++count;
    }
    const double denom = count * sxx - sx * sx;
    if (count < 5 || denom < 1e-10)
      return 0.;
    const double slope = (count * sxy - sx * sy) / denom;
    return slope < -1 ? std::clamp(-60 / slope, .2, 15.) : 0.;
  };
  result.decay = estimate(energy[6]);
  if (result.decay == 0) {
    result.status = "IR: no clear decay found";
    return result;
  }
  for (int b = 0; b < 6; ++b) {
    const double decay = estimate(energy[size_t(b)]);
    if (decay > 0)
      result.ratios[size_t(b)] = std::clamp(decay / result.decay, .2, 3.);
  }
  result.valid = true;
  result.status = "IR fitted: decay, pre-delay, color";
  return result;
}
} // namespace studio
