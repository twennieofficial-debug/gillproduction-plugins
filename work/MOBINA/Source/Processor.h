#pragma once
#include "../../GILLCommon/QualityBus.h"
#include "Dsp.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace mobina {
struct Spec {
  const char *id;
  const char *name;
  float lo, hi, def, skew;
  const char *unit;
};
inline constexpr Spec specs[]{{"dream", "DREAM", 0, 100, 42, 1, "%"},
                              {"distance", "DISTANCE", 0, 100, 30, 1, "%"},
                              {"space", "SPACE", .3f, 12, 3.6f, .5f, "s"},
                              {"width", "WIDTH", 0, 200, 125, 1, "%"},
                              {"drift", "DRIFT", 0, 100, 25, 1, "%"},
                              {"rate", "SWIM RATE", .03f, 1, .12f, .5f, "Hz"},
                              {"warmth", "WARMTH", 0, 100, 35, 1, "%"},
                              {"shimmer", "BLOOM", 0, 100, 18, 1, "%"},
                              {"predelay", "PREDELAY", 0, 120, 24, 1, "ms"},
                              {"duck", "DUCK", 0, 100, 30, 1, "%"},
                              {"mix", "MIX", 0, 100, 100, 1, "%"},
                              {"output", "OUTPUT", -24, 6, 0, 1, "dB"}};
class Processor final : public juce::AudioProcessor {
  Dsp dsp;
  std::array<std::atomic<float> *, 12> raw{};
  std::atomic<int> program{0};
  void process(juce::AudioBuffer<float> &, bool);

public:
  juce::UndoManager undo;
  juce::AudioProcessorValueTreeState state;
  gill::QualityClient quality;
  std::atomic<float> meter{0}, pan{0};
  Processor();
  static juce::AudioProcessorValueTreeState::ParameterLayout layout();
  const juce::String getName() const override { return "MOBINA"; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 30; }
  bool isBusesLayoutSupported(const BusesLayout &l) const override {
    return l.inputBuses.size() == 1 && l.outputBuses.size() == 1 &&
           l.getMainInputChannelSet() == l.getMainOutputChannelSet() &&
           (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() ||
            l.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
  }
  void prepareToPlay(double fs, int) override {
    dsp.prepare(fs, getTotalNumOutputChannels());
    setLatencySamples(0);
  }
  void releaseResources() override {}
  void reset() override { dsp.reset(); }
  void processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) override {
    process(b, false);
  }
  void processBlockBypassed(juce::AudioBuffer<float> &b,
                            juce::MidiBuffer &) override {
    process(b, true);
  }
  juce::AudioProcessorParameter *getBypassParameter() const override {
    return state.getParameter("bypass");
  }
  bool hasEditor() const override { return true; }
  juce::AudioProcessorEditor *createEditor() override;
  int getNumPrograms() override { return 8; }
  int getCurrentProgram() override { return program.load(); }
  void setCurrentProgram(int) override;
  const juce::String getProgramName(int) override;
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override;
  void setStateInformation(const void *, int) override;
  void set(const juce::String &id, float v) {
    if (auto *p = state.getParameter(id))
      p->setValueNotifyingHost(p->convertTo0to1(v));
  }
};
} // namespace mobina
