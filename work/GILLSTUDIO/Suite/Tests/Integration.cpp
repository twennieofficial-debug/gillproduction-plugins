#include "MacTestSupport.h"
#include "Processor.h"
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
using studio::Kind;
using studio::Processor;
void require(bool v, const std::string &m) {
  if (!v)
    throw std::runtime_error(m);
}
void run(Processor &p, juce::AudioBuffer<float> &b) {
  juce::MidiBuffer m;
  allocations = 0;
  counting = true;
  p.processBlock(b, m);
  counting = false;
  require(allocations == 0, std::string(p.getName().toRawUTF8()) +
                                " allocates in audio callback");
  for (int c = 0; c < b.getNumChannels(); ++c)
    for (int i = 0; i < b.getNumSamples(); ++i)
      require(std::isfinite(b.getSample(c, i)), "non-finite output");
}
std::vector<float> render(Kind k, int quality, int block, int program = 2) {
  auto p = std::make_unique<Processor>(k);
  p->setCurrentProgram(program);
  p->set("gillQuality", float(quality));
  p->prepareToPlay(48000, block);
  std::vector<float> out;
  std::mt19937 rng(9123);
  std::uniform_real_distribution<float> d(-.015f, .015f);
  for (int start = 0; start < 12000; start += block) {
    const int n = std::min(block, 12000 - start);
    juce::AudioBuffer<float> b(2, n);
    for (int i = 0; i < n; ++i) {
      const int t = start + i;
      float x =
          float(.12 * std::sin(t * .17) + .07 * std::sin(t * .91)) + d(rng);
      if (t % 1500 < 20)
        x += .2f;
      for (int c = 0; c < 2; ++c)
        b.setSample(c, i, x * (c ? .8f : 1.f));
    }
    run(*p, b);
    for (int i = 0; i < n; ++i)
      out.push_back(b.getSample(0, i));
  }
  return out;
}
void partition() {
  for (int k = 0; k < 12; ++k)
    for (int q = 0; q < 2; ++q) {
      auto a = render(Kind(k), q, 31), b = render(Kind(k), q, 257);
      double error = 0, energy = 0;
      for (size_t i = 0; i < a.size(); ++i) {
        error = std::max(error, std::abs(double(a[i] - b[i])));
        energy += a[i] * a[i];
      }
      require(error < 3e-6, std::string(studio::names[k]) +
                                " block-size dependence " +
                                std::to_string(error));
      require(energy > 1e-5, "unexpected silence");
      std::cout << studio::names[k] << " " << q << " blocks error " << error
                << "\n";
    }
}
void recallAndLayouts() {
  for (int k = 0; k < 12; ++k) {
    auto p = std::make_unique<Processor>(Kind(k));
    for (int program = 0; program < 6; ++program) {
      p->setCurrentProgram(program);
      juce::MemoryBlock bytes;
      p->getStateInformation(bytes);
      auto r = std::make_unique<Processor>(Kind(k));
      r->setStateInformation(bytes.getData(), int(bytes.getSize()));
      require(r->getCurrentProgram() == program, "program recall");
      for (const auto &s : p->definition.params)
        require(std::abs(p->value(s.id) - r->value(s.id)) < .001f,
                "parameter recall " + s.id);
    }
    for (double fs : {44100., 48000., 96000.})
      for (int channels : {1, 2})
        for (int mode : {0, 1}) {
          auto l = p->getBusesLayout();
          l.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono()
                                            : juce::AudioChannelSet::stereo());
          l.outputBuses.set(0, l.inputBuses[0]);
          l.inputBuses.set(1, juce::AudioChannelSet::stereo());
          require(p->setBusesLayout(l), "main/key layout");
          p->setCurrentProgram(0);
          p->set("gillQuality", float(mode));
          p->set("mix", 0);
          p->prepareToPlay(fs, 127);
          require(mode == 1 || p->getLatencySamples() == 0,
                  "LIVE adds buffered latency");
          const int delay = p->getLatencySamples();
          juce::AudioBuffer<float> b(channels + 2, delay + 300);
          b.clear();
          for (int c = 0; c < channels; ++c)
            b.setSample(c, 0, .25f);
          for (int c = channels; c < channels + 2; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
              b.setSample(c, i, .1f);
          run(*p, b);
          require(std::abs(b.getSample(0, delay) - .25f) < 1e-7,
                  "mix zero delay differs from reported latency");
          for (int i = 0; i < b.getNumSamples(); ++i) {
            if (i != delay)
              require(std::abs(b.getSample(0, i)) < 1e-7,
                      "dry latency leaks or duplicates input");
            for (int c = channels; c < channels + 2; ++c)
              require(b.getSample(c, i) == .1f, "key buffer changed");
          }
        }
    std::cout << studio::names[k] << " state/layout/latency passed\n";
  }
}
void hostileAndUi() {
  for (int k = 0; k < 12; ++k) {
    auto p = std::make_unique<Processor>(Kind(k));
    p->set("gillQuality", 0);
    p->prepareToPlay(48000, 127);
    juce::AudioBuffer<float> b(2, 257);
    for (int variant = 0; variant < 4; ++variant) {
      for (const auto &s : p->definition.params)
        if (s.group >= 0)
          p->set(s.id, variant % 2 ? s.hi : s.lo);
      for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 257; ++i) {
          const float bad[]{0, .1f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::max()};
          b.setSample(c, i, bad[i % 5]);
        }
      run(*p, b);
    }
    p->setCurrentProgram(1);
    p->prepareToPlay(48000, 127);
    std::unique_ptr<juce::AudioProcessorEditor> e(p->createEditor());
    const bool advanced = k==6 || k==7 || k==8 || k==10;
    require(e->getWidth() == (k==11?1160:advanced?620:760) && e->getHeight() == (k==11?700:advanced?660:548),
            "compact dimensions changed");
    require(!e->getWantsKeyboardFocus(), "editor intercepts transport focus");
    juce::ComboBox *pages = nullptr;
    for (auto *c : e->getChildren()) {
      if (auto *box = dynamic_cast<juce::ComboBox *>(c))
        if (box->getComponentID() == "pages")
          pages = box;
    }
    require(pages, "missing parameter pages");
    for (int page = 1; page <= pages->getNumItems(); ++page) {
      pages->setSelectedId(page, juce::sendNotificationSync);
      for (auto *c : e->getChildren())
        if (c->isVisible() && !dynamic_cast<juce::TooltipWindow *>(c))
          require(e->getLocalBounds().contains(c->getBounds()),
                  "control outside window");
    }
    pages->setSelectedId(1, juce::sendNotificationSync);
    const auto image = e->createComponentSnapshot(e->getLocalBounds());
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(
        p->getName() + "-UI-default.png");
    juce::FileOutputStream out(file);
    out.setPosition(0);
    out.truncate();
    require(juce::PNGImageFormat().writeImageToStream(image, out),
            "editor image write");
    std::cout << studio::names[k] << " hostile input and all UI pages passed\n";
  }
}
int main() {
  gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;
  std::cout << std::unitbuf;
  try {
    counting=true;allocations=0;void* probe=::operator new(123);counting=false;
    require(allocations>0,"allocation counter cannot observe allocations");::operator delete(probe);
    partition();
    recallAndLayouts();
    hostileAndUi();
    std::cout << "STUDIO_SUITE_INTEGRATION_PASS\n";
  } catch (const std::exception &e) {
    counting = false;
    std::cerr << e.what() << "\n";
    return 1;
  }
}
