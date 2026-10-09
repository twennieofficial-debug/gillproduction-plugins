#include "MacTestSupport.h"
#include "../Source/Processor.h"
#include <iostream>
#include <random>
#include <stdexcept>
thread_local bool counting = false;
thread_local size_t allocations = 0;
void *operator new(size_t n) {
  if (counting)
    ++allocations;
  if (auto *p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void *p) noexcept { std::free(p); }
void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
void run(mobina::Processor &p, juce::AudioBuffer<float> &b) {
  juce::MidiBuffer m;
  allocations = 0;
  counting = true;
  p.processBlock(b, m);
  counting = false;
  check(allocations == 0, "callback allocation");
  for (int c = 0; c < b.getNumChannels(); ++c)
    for (int i = 0; i < b.getNumSamples(); ++i)
      check(std::isfinite(b.getSample(c, i)), "nonfinite");
}
std::vector<float> render(int block, int preset, int quality) {
  mobina::Processor p;
  p.setCurrentProgram(preset);
  p.set("gillQuality", float(quality));
  p.prepareToPlay(48000, block);
  std::vector<float> out;
  for (int s = 0; s < 48000; s += block) {
    int n = std::min(block, 48000 - s);
    juce::AudioBuffer<float> b(2, n);
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < n; ++i)
        b.setSample(
            c, i,
            float(.2 * std::sin((s + i) * .057) * (s + i < 36000 ? 1 : 0)));
    run(p, b);
    for (int i = 0; i < n; ++i)
      out.push_back(b.getSample(0, i));
  }
  return out;
}
int main() {
  gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;
  std::cout << std::unitbuf;
  try {
    for (int preset = 0; preset < 8; ++preset)
      for (int q = 0; q < 2; ++q) {
        auto a = render(31, preset, q), b = render(257, preset, q);
        double error = 0;
        for (size_t i = 0; i < a.size(); ++i)
          error = std::max(error, std::abs(double(a[i] - b[i])));
        check(error < 2e-6, "block partition mismatch");
        std::cout << "PRESET " << preset << " QUALITY " << q << " BLOCK ERROR "
                  << error << "\n";
      }
    for (double fs : {44100., 48000., 96000.})
      for (int channels : {1, 2})
        for (int q : {0, 1}) {
          mobina::Processor p;
          auto l = p.getBusesLayout();
          l.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono()
                                            : juce::AudioChannelSet::stereo());
          l.outputBuses.set(0, l.inputBuses[0]);
          check(p.setBusesLayout(l), "layout");
          p.set("gillQuality", float(q));
          p.set("mix", 0);
          p.prepareToPlay(fs, 128);
          check(p.getLatencySamples() == 0, "latency");
          juce::AudioBuffer<float> b(channels, 128);
          b.clear();
          for (int c = 0; c < channels; ++c)
            b.setSample(c, 0, .5f);
          run(p, b);
          for (int c = 0; c < channels; ++c) {
            check(b.getSample(c, 0) == .5f, "dry onset");
            for (int i = 1; i < 128; ++i)
              check(b.getSample(c, i) == 0, "dry leakage");
          }
        }
    mobina::Processor p;
    for (int preset = 0; preset < 8; ++preset) {
      p.setCurrentProgram(preset);
      juce::MemoryBlock bytes;
      p.getStateInformation(bytes);
      mobina::Processor restored;
      restored.setStateInformation(bytes.getData(), int(bytes.getSize()));
      for (auto &s : mobina::specs)
        check(std::abs(p.state.getRawParameterValue(s.id)->load() -
                       restored.state.getRawParameterValue(s.id)->load()) <
                  1e-6f,
              "state mismatch");
    }
    p.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> b(2, 128);
    for (auto &s : mobina::specs)
      for (float value : {s.lo, s.hi}) {
        p.set(s.id, value);
        for (int z = 0; z < 12; ++z) {
          for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 128; ++i)
              b.setSample(c, i, float(.2 * std::sin((i + z * 128) * .07)));
          run(p, b);
        }
      }
    b.clear();
    b.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
    b.setSample(1, 0, std::numeric_limits<float>::infinity());
    run(p, b);
    p.setCurrentProgram(0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    check(editor->getWidth() == 660 && editor->getHeight() == 490,
          "editor size");
    check(!editor->getWantsKeyboardFocus(), "editor steals keyboard");
    auto image =
        editor->createComponentSnapshot(editor->getLocalBounds());
    juce::File target =
        juce::File::getCurrentWorkingDirectory().getChildFile("MOBINA-UI-default.png");
    auto out = target.createOutputStream();
    check(out != nullptr && out->openedOk(), "editor image stream");
    check(image.getWidth()==660 && image.getHeight()==490, "compact editor dimensions");
    out->setPosition(0);
    out->truncate();
    juce::PNGImageFormat png;
    check(png.writeImageToStream(image, *out), "editor image write");
    std::cout << "MOBINA_TESTS_PASS\n";
  } catch (const std::exception &e) {
    counting = false;
    std::cerr << e.what() << "\n";
    return 1;
  }
}
