#include "Processor.h"
namespace mobina {
juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  for (auto &s : specs)
    l.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{s.id, 1}, s.name,
        juce::NormalisableRange<float>(s.lo, s.hi, 0, s.skew), s.def));
  l.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"bypass", 1}, "BYPASS",
      juce::StringArray{"ACTIVE", "BYPASS"}, 0));
  l.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"sync", 1}, "SWIM SYNC",
      juce::StringArray{"FREE", "2 BARS"}, 0));
  l.add(gill::qualityParameter());
  return l;
}
Processor::Processor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, &undo, "MobinaState", layout()), quality(*this, state) {
  for (int i = 0; i < 12; ++i)
    raw[i] = state.getRawParameterValue(specs[i].id);
  setCurrentProgram(0);
}
void Processor::process(juce::AudioBuffer<float> &b, bool hostBypass) {
  juce::ScopedNoDenormals dn;
  Settings s;
  float *values[]{&s.dream,    &s.distance, &s.space,  &s.width,
                  &s.drift,    &s.rate,     &s.warmth, &s.shimmer,
                  &s.predelay, &s.duck,     &s.mix,    &s.output};
  for (int i = 0; i < 12; ++i) {
    float v = raw[i]->load();
    *values[i] = std::isfinite(v) ? std::clamp(v, specs[i].lo, specs[i].hi)
                                  : specs[i].def;
  }
  s.bypass = hostBypass ? 1 : state.getRawParameterValue("bypass")->load();
  if (state.getRawParameterValue("sync")->load() > .5)
    s.rate = -1;
  double bpm = 120;
  if (auto *ph = getPlayHead())
    if (auto p = ph->getPosition())
      if (auto v = p->getBpm())
        if (std::isfinite(*v) && *v > 0)
          bpm = std::clamp(*v, 20., 400.);
  dsp.set(s);
  dsp.process(b.getArrayOfWritePointers(), b.getNumSamples(), bpm,
              quality.isPro());
  meter = b.getNumSamples() ? b.getMagnitude(0, b.getNumSamples()) : 0;
  pan = dsp.pan;
}
const juce::String Processor::getProgramName(int i) {
  static const char *names[]{"VELVET R&B",  "BACKSEAT VOCAL", "MIDNIGHT BEAT",
                             "PINK HAZE",   "SOFT DOUBLES",   "SLOW SWIM",
                             "DREAM CLOUD", "BARELY THERE"};
  return names[std::clamp(i, 0, 7)];
}
void Processor::setCurrentProgram(int i) {
  program = std::clamp(i, 0, 7);
  static const float presets[8][12]{
      {42, 30, 3.6f, 125, 25, .12f, 35, 18, 24, 30, 100, 0},
      {48, 65, 3.2f, 135, 15, .09f, 38, 10, 16, 45, 100, 0},
      {28, 22, 2.6f, 125, 18, .08f, 42, 8, 38, 55, 100, 0},
      {78, 50, 7, 165, 35, .13f, 45, 45, 32, 20, 100, -1},
      {45, 35, 2.1f, 155, 12, .15f, 25, 28, 19, 50, 100, 0},
      {52, 25, 4.5f, 135, 55, .07f, 30, 25, 30, 30, 100, 0},
      {100, 65, 8, 180, 30, .09f, 45, 60, 45, 5, 100, -3},
      {12, 12, 1.8f, 115, 8, .1f, 15, 8, 25, 35, 100, 0}};
  for (int k = 0; k < 12; ++k)
    set(specs[k].id, presets[std::clamp(i, 0, 7)][k]);
  set("bypass", 0);
  set("sync", 0);
}
void Processor::getStateInformation(juce::MemoryBlock &b) {
  auto t = state.copyState();
  t.setProperty("schema", 1, nullptr);
  t.setProperty("program", program.load(), nullptr);
  juce::MemoryOutputStream o(b, false);
  t.writeToStream(o);
}
void Processor::setStateInformation(const void *d, int n) {
  if (!d || n <= 0 || n > 1024 * 1024)
    return;
  auto t = juce::ValueTree::readFromData(d, size_t(n));
  if (!t.isValid() || t.getType() != state.state.getType() ||
      !t["schema"].isInt() || int(t["schema"]) != 1)
    return;
  auto selected = t.getProperty("program", 0);
  if (!selected.isInt() || int(selected) < 0 || int(selected) > 7)
    return;
  auto fresh = state.copyState();
  for (auto c : fresh)
    if (auto *p = state.getParameter(c["id"].toString()))
      c.setProperty("value", p->convertFrom0to1(p->getDefaultValue()), nullptr);
  juce::StringArray seen;
  for (auto c : t) {
    auto id = c["id"].toString();
    auto *p = state.getParameter(id);
    if (!p)
      continue;
    const auto v = c["value"];
    if (seen.contains(id) || !(v.isInt() || v.isInt64() || v.isDouble()) ||
        !std::isfinite(double(v)))
      return;
    const auto r = p->getNormalisableRange();
    if (double(v) < r.start || double(v) > r.end)
      return;
    seen.add(id);
    for (auto target : fresh)
      if (target["id"].toString() == id)
        target.setProperty("value", v, nullptr);
  }
  program = int(selected);
  state.replaceState(fresh);
}
} // namespace mobina
