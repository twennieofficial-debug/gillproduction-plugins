#include "MacTestSupport.h"
#include "Processor.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
int main() {
  gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;
  std::cout << std::unitbuf;
  try {
    for (int k = 0; k < 12; ++k) {
      auto p = std::make_unique<studio::Processor>(studio::Kind(k));
      size_t cases = 0;
      double peak = 0;
      auto started = std::chrono::steady_clock::now();
      for (int quality = 0; quality < 2; ++quality) {
        p->setCurrentProgram(2);
        p->set("gillQuality", float(quality));
        p->prepareToPlay(48000, 64);
        juce::AudioBuffer<float> b(2, 64);
        juce::MidiBuffer midi;
        long long clock = 0;
        for (auto &s : p->definition.params) {
          if (s.group < 0)
            continue;
          const float original = p->value(s.id);
          for (float value : {s.lo, s.hi}) {
            p->set(s.id, value);
            for (int chunk = 0; chunk < 18; ++chunk) {
              for (int i = 0; i < 64; ++i) {
                const double t = double(clock + i);
                const float x =
                    float(.12 * std::sin(t * .017) + .06 * std::sin(t * .271) +
                          .03 * std::sin(t * 1.27)) *
                    (clock % 12000 < 9000 ? 1.f : .01f);
                b.setSample(0, i, x);
                b.setSample(1, i, x * .72f);
              }
              p->processBlock(b, midi);
              for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 64; ++i) {
                  const float x = b.getSample(c, i);
                  if (!std::isfinite(x))
                    throw std::runtime_error(std::string(studio::names[k]) +
                                             " nonfinite at " + s.id);
                  peak = std::max(peak, std::abs(double(x)));
                }
              clock += 64;
            }
            ++cases;
          }
          p->set(s.id, original);
        }
      }
      std::cout << studio::names[k] << " parameter endpoints " << cases
                << " peak " << peak << " seconds "
                << std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - started)
                       .count()
                << "\n";
    }
    std::cout << "STUDIO_PARAMETER_SWEEP_PASS\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
