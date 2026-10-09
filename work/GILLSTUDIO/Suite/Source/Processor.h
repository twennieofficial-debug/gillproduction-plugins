#pragma once
#include "../../../GILLCommon/QualityBus.h"
#include "Engine.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace studio {
class Processor final : public juce::AudioProcessor, private juce::Timer {
public:
  explicit Processor(Kind);
  ~Processor() override { stopTimer(); }
  const Kind kind;
  const Definition definition;
  juce::UndoManager undo;
  juce::AudioProcessorValueTreeState state;
  gill::QualityClient quality;
  static juce::AudioProcessorValueTreeState::ParameterLayout layout(Kind);
  const juce::String getName() const override { return names[int(kind)]; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override {
    if (kind == Kind::Room)
      return value("freeze") > .5f ? 3600 : 31;
    // Chorus feedback is an effect tail, independent of zero FIFO latency.
    // Conservative allowance also covers several chorus slots in series.
    if (kind == Kind::Rack)
      return 4.0;
    return double(getLatencySamples()) / rate.load();
  }
  void prepareToPlay(double, int) override;
  void releaseResources() override {};
  bool isBusesLayoutSupported(const BusesLayout &) const override;
  void processBlock(juce::AudioBuffer<float> &b, juce::MidiBuffer &) override {
    process(b, false);
  }
  void processBlockBypassed(juce::AudioBuffer<float> &b,
                            juce::MidiBuffer &) override {
    process(b, true);
  }
  bool hasEditor() const override { return true; }
  juce::AudioProcessorEditor *createEditor() override;
  int getNumPrograms() override { return 6; }
  int getCurrentProgram() override { return program.load(); }
  void setCurrentProgram(int) override;
  const juce::String getProgramName(int) override;
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &) override;
  void setStateInformation(const void *, int) override;
  void set(const juce::String &id, float value);
  float value(const juce::String &id) const;
  int appliedQuality() const noexcept { return applied.load(); }
  bool pending() const noexcept { return quality.mode() != appliedQuality(); }
  juce::AudioProcessorParameter *getBypassParameter() const override {
    return state.getParameter("bypass");
  }
  std::atomic<bool> resetLoudness{false};
  std::atomic<float> inputLufs{-100}, outputLufs{-100};
  std::atomic<float> inPeak{0}, outPeak{0};
  std::array<std::atomic<float>, 6> reduction{};
  std::array<std::atomic<float>, 256> wave{};
  std::atomic<double> rate{48000};

private:
  std::unique_ptr<Engine> engine;
  std::array<std::atomic<float> *, 1024> raw{};
  std::array<float, 1024> values{}, targets{};
  size_t count = 0;
  std::atomic<int> program{0}, applied{1};
  std::atomic<bool> known{false}, playing{true};
  bool prepared = false;
  int channels = 2;
  size_t sampleClock = 0, dryPos = 0;
  std::array<std::vector<float>, 2> dry;
  float bypassFade = 0, deltaFade = 0;
  int paramRamp = 0;
  void process(juce::AudioBuffer<float> &, bool);
  void update(bool snap);
  void timerCallback() override;
};
} // namespace studio
