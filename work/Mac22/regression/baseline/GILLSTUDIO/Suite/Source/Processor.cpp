#include "Processor.h"
namespace studio {
Processor::Processor(Kind k)
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      kind(k), definition(studio::definition(k)),
      state(*this, &undo, juce::String(names[int(k)]) + "State", layout(k)),
      quality(*this, state), engine(std::make_unique<Engine>(k)) {
  count = definition.params.size();
  jassert(count < raw.size());
  for (size_t i = 0; i < count; ++i)
    raw[i] = state.getRawParameterValue(definition.params[i].id);
  setCurrentProgram(0);
  undo.clearUndoHistory();
  startTimerHz(20);
}
juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout(Kind k) {
  juce::AudioProcessorValueTreeState::ParameterLayout result;
  const auto d = studio::definition(k);
  for (const auto &s : d.params) {
    if (s.choices.empty())
      result.add(std::make_unique<juce::AudioParameterFloat>(
          juce::ParameterID{s.id, 1},
          s.group > 0 ? juce::String(d.groups[size_t(s.group)]) + " " + s.name
                      : juce::String(s.name),
          juce::NormalisableRange<float>(s.lo, s.hi, 0, s.skew), s.def));
    else {
      juce::StringArray a;
      for (const auto &v : s.choices)
        a.add(v);
      result.add(std::make_unique<juce::AudioParameterChoice>(
          juce::ParameterID{s.id, 1},
          s.group > 0 ? juce::String(d.groups[size_t(s.group)]) + " " + s.name
                      : juce::String(s.name),
          a, int(s.def)));
    }
  }
  result.add(gill::qualityParameter());
  return result;
}
void Processor::set(const juce::String &id, float v) {
  if (auto *p = state.getParameter(id))
    p->setValueNotifyingHost(p->convertTo0to1(v));
}
float Processor::value(const juce::String &id) const {
  auto *p = state.getRawParameterValue(id);
  return p ? p->load() : 0;
}
bool Processor::isBusesLayoutSupported(const BusesLayout &l) const {
  if (l.inputBuses.size() != 2 || l.outputBuses.size() != 1)
    return false;
  const auto m = l.getMainOutputChannelSet(), k = l.inputBuses[1];
  return (m == juce::AudioChannelSet::mono() ||
          m == juce::AudioChannelSet::stereo()) &&
         l.getMainInputChannelSet() == m &&
         (k.isDisabled() || k == juce::AudioChannelSet::mono() ||
          k == juce::AudioChannelSet::stereo());
}
void Processor::update(bool snap) {
  bool change = false;
  for (size_t i = 0; i < count; ++i) {
    const auto &s = definition.params[i];
    const float v = raw[i]->load();
    const float valid = std::isfinite(v) ? juce::jlimit(s.lo, s.hi, v) : s.def;
    if (valid != targets[i]) {
      targets[i] = valid;
      change = true;
    }
    if (snap || !s.choices.empty() || s.key == "order")
      values[i] = targets[i];
  }
  if (snap) {
    paramRamp = 0;
    engine->update({definition, values});
  } else if (change)
    paramRamp = std::max(1, int(rate.load() * .02 / 32));
}
void Processor::prepareToPlay(double fs, int) {
  const juce::ScopedLock lock(getCallbackLock());
  rate = std::isfinite(fs) ? juce::jlimit(8000., 192000., fs) : 48000.;
  channels = getMainBusNumInputChannels();
  applied = quality.mode();
  engine->prepare(rate, channels, applied.load() == 0);
  update(true);
  for (auto &r : dry)
    r.assign(size_t(rate.load() * .05) + 8192, 0);
  dryPos = sampleClock = 0;
  bypassFade = values[3] > .5f ? 1.f : 0.f;
  deltaFade = values[4] > .5f ? 1.f : 0.f;
  known = false;
  playing = true;
  prepared = true;
  inPeak = outPeak = 0;
  for (auto &x : wave)
    x = 0;
  for (auto &x : reduction)
    x = 0;
  setLatencySamples(engine->latency());
}
void Processor::timerCallback() {
  if (!pending())
    return;
  const juce::ScopedLock lock(getCallbackLock());
  if (!prepared || !known.load() || playing.load())
    return;
  const int q = quality.mode();
  engine->mode(q == 0);
  applied = q;
  for (auto &r : dry)
    std::fill(r.begin(), r.end(), 0.f);
  dryPos = 0;
  setLatencySamples(engine->latency());
}
void Processor::process(juce::AudioBuffer<float> &buffer, bool off) {
  juce::ScopedNoDenormals guard;
  auto main = getBusBuffer(buffer, false, 0);
  auto key = getBusBuffer(buffer, true, 1);
  if (!prepared) {
    main.clear();
    return;
  }
  bool transportKnown = false, transportPlaying = true;
  if (auto *h = getPlayHead())
    if (auto pos = h->getPosition()) {
      transportKnown = true;
      transportPlaying = pos->getIsPlaying() || pos->getIsRecording();
    }
  playing = transportPlaying;
  known = transportKnown;
  update(false);
  if (resetLoudness.exchange(false))
    engine->resetLoudness();
  float peakIn = 0, peakOut = 0;
  const int latency = engine->latency();
  for (int start = 0; start < main.getNumSamples();) {
    const int n = std::min(
        {32 - int(sampleClock & 31), main.getNumSamples() - start, 32});
    if ((sampleClock & 31) == 0 && paramRamp > 0) {
      for (size_t i = 0; i < count; ++i)
        values[i] += (targets[i] - values[i]) / float(paramRamp);
      --paramRamp;
      engine->update({definition, values});
    }
    std::array<std::array<float, 32>, 2> original{};
    std::array<float *, 2> ptr{};
    std::array<const float *, 2> kp{};
    for (int c = 0; c < channels; ++c)
      ptr[size_t(c)] = main.getWritePointer(c) + start;
    for (int c = 0; c < key.getNumChannels(); ++c)
      kp[size_t(c)] = key.getReadPointer(c) + start;
    const double input = gain(values[0]), output = gain(values[1]),
                 mix = values[2] * .01;
    for (int i = 0; i < n; ++i) {
      for (int c = 0; c < channels; ++c) {
        const float rawSample = float(clean(ptr[size_t(c)][i]));
        peakIn = std::max(peakIn, std::abs(rawSample));
        dry[size_t(c)][dryPos] = rawSample;
        original[size_t(c)][size_t(i)] =
            dry[size_t(c)][(dryPos + dry[size_t(c)].size() - size_t(latency)) %
                           dry[size_t(c)].size()];
        ptr[size_t(c)][i] = float(clean(rawSample * input));
      }
      dryPos = (dryPos + 1) % dry[0].size();
    }
    engine->process(ptr.data(), n, key.getNumChannels() ? kp.data() : nullptr,
                    key.getNumChannels());
    for (int i = 0; i < n; ++i) {
      const float target = off || targets[3] > .5f ? 1.f : 0.f;
      bypassFade += juce::jlimit(-1.f / 128, 1.f / 128, target - bypassFade);
      deltaFade += juce::jlimit(-1.f / 128, 1.f / 128,
                                (targets[4] > .5f ? 1.f : 0.f) - deltaFade);
      for (int c = 0; c < channels; ++c) {
        const float drySample = original[size_t(c)][size_t(i)],
                    wet = float(clean(ptr[size_t(c)][i]));
        const double mixed = (drySample * (1 - mix) + wet * mix) * output;
        const double monitored =
            mixed + deltaFade * ((drySample - wet) - mixed);
        ptr[size_t(c)][i] =
            float(clean(monitored + (drySample - monitored) * bypassFade));
        peakOut = std::max(peakOut, std::abs(ptr[size_t(c)][i]));
      }
      wave[(sampleClock + size_t(i)) % wave.size()].store(
          ptr[0][i], std::memory_order_relaxed);
    }
    sampleClock += size_t(n);
    start += n;
  }
  inPeak = std::max(peakIn, inPeak.load() * .9f);
  outPeak = std::max(peakOut, outPeak.load() * .9f);
  const auto levels = engine->loudness();
  inputLufs = levels[0];
  outputLufs = levels[1];
  const auto r = engine->meters();
  for (size_t i = 0; i < r.size(); ++i)
    reduction[i] = r[i];
}
const juce::String Processor::getProgramName(int index) {
  static const char *common[]{"INIT",    "SOFT VOCAL", "RAP FOCUS",
                              "MIX BUS", "PARALLEL",   "EXTREME FX"};
  static const char *spaces[]{"SMALL ROOM", "VOCAL PLATE",  "RAP SLAP ROOM",
                              "WIDE HALL",  "DUCKED CLOUD", "GATED SPACE"};
  return (kind == Kind::Room ? spaces : common)[juce::jlimit(0, 5, index)];
}
void Processor::setCurrentProgram(int p) {
  if (p < 0 || p >= 6)
    return;
  for (const auto &s : definition.params)
    set(s.id, s.def);
  auto change = [&](const char *key, float v, int group = 0) {
    for (const auto &s : definition.params)
      if (s.key == key && s.group == group)
        set(s.id, v);
  };
  if (p > 0) {
    if (kind == Kind::Tape) {
      change("drive", std::array<float, 6>{0, 3, 8, 2, 15, 27}[size_t(p)]);
      change("machine", p == 3 ? 1.f : 0.f);
      change("wow", p == 5 ? 50.f : 0.f);
    }
    if (kind == Kind::Vital)
      for (int i = 1; i <= 5; ++i) {
        change("amount", float(10 + p * 8), i);
        change("drive", float(4 + p * 4), i);
        change("gain", float(p * 2), i);
      }
    if (kind == Kind::Harmonic) {
      change("amount", float(10 + p * 10));
      change("drive", float(4 + p * 4));
      change("even", p == 5 ? 35.f : 0.f);
    }
    if (kind == Kind::Eq825)
      for (int c = 1; c <= 2; ++c) {
        change("low", p == 3 ? 1.f : -1.f, c);
        change("lowmid", p == 2 ? -3.f : -1.f, c);
        change("high", float(p), c);
        change("thd", float(p * 6));
      }
    if (kind == Kind::Multi || kind == Kind::Precision || kind == Kind::Deess) {
      const int bands = kind == Kind::Multi       ? 6
                        : kind == Kind::Precision ? 5
                                                  : 2;
      for (int i = 1; i <= bands; ++i) {
        change("threshold", float(-18 - p * 3), i);
        change("ratio", 1.5f + p * .7f, i);
        change("range", float(3 + p * 2), i);
      }
      if (p == 4)
        change("mix", 50, -1);
    }
    if (kind == Kind::Alpha) {
      for (int c = 1; c <= 2; ++c) {
        change("threshold", float(-15 - p * 3), c);
        change("ratio", 1.5f + p * .5f, c);
      }
      if (p == 4)
        change("mix", 50, -1);
    }
    if (kind == Kind::Max) {
      change("drive", float(p * 2));
      change("release", float(60 + p * 30));
      change("clip", p == 5 ? 40.f : 0.f);
    }
    if (kind == Kind::Transient) {
      change("amount", float(15 + p * 12));
      change("action", p >= 4 ? 1.f : 0.f);
      change("enable", 1, 1);
      change("shape", 2, 1);
      change("frequency", p == 1 ? 4500.f : 2500.f, 1);
      change("gain", 8, 1);
    }

    if (kind == Kind::Rack) {
      change("module", 1, 1);
      change("low", -2, 1);
      change("high", 2, 1);
      change("module", 2, 2);
      change("threshold", -24, 2);
      change("module", 5, 3);
      change("frequency", 6500, 3);
      change("module", 6, 4);
      change("amount", float(p * 8), 4);
    }
  }
  if (kind == Kind::Room) {
    const float decay[]{.7f, 1.6f, .5f, 4.5f, 7.f, 2.f};
    change("decay", decay[p]);
    change("style", p == 1 ? 2.f : p == 2 ? 1.f : 0.f);
    change("predelay", p == 2 ? 65.f : 25.f);
    change("duck", p == 4 ? 75.f : 0.f);
    change("gate", p == 5 ? 450.f : 0.f);
    change("mix", p == 4 ? 28.f : 22.f, -1);
  }
  program = p;
}
void Processor::getStateInformation(juce::MemoryBlock &dest) {
  auto tree = state.copyState();
  tree.setProperty("schema", 1, nullptr);
  tree.setProperty("program", program.load(), nullptr);
  juce::MemoryOutputStream out(dest, false);
  tree.writeToStream(out);
}
void Processor::setStateInformation(const void *data, int n) {
  if (!data || n <= 0 || n > 4 * 1024 * 1024)
    return;
  auto tree = juce::ValueTree::readFromData(data, size_t(n));
  const auto schema = tree.getProperty("schema");
  if (!tree.isValid() || tree.getType() != state.state.getType() ||
      !schema.isInt() || int(schema) != 1)
    return;
  auto normalized = state.copyState();
  for (auto child : normalized) {
    const auto id = child.getProperty("id").toString();
    if (auto *parameter = state.getParameter(id))
      child.setProperty(
          "value", parameter->convertFrom0to1(parameter->getDefaultValue()),
          nullptr);
  }
  juce::StringArray seen;
  for (auto child : tree) {
    const auto id = child.getProperty("id").toString();
    auto *parameter = state.getParameter(id);
    if (!parameter)
      continue;
    const auto property = child.getProperty("value");
    if (seen.contains(id) ||
        !(property.isInt() || property.isInt64() || property.isDouble()))
      return;
    const double value = double(property);
    const auto range = parameter->getNormalisableRange();
    if (!std::isfinite(value) || value < range.start || value > range.end)
      return;
    seen.add(id);
    for (auto target : normalized)
      if (target.getProperty("id").toString() == id)
        target.setProperty("value", value, nullptr);
  }
  const auto selected = tree.getProperty("program", 0);
  if (!selected.isInt() || int(selected) < 0 || int(selected) > 5)
    return;
  normalized.removeProperty("schema", nullptr);
  normalized.removeProperty("program", nullptr);
  program = int(selected);
  state.replaceState(normalized);
}
} // namespace studio
juce::AudioProcessor* makeStudioProcessor(int kind) {
  return new studio::Processor(studio::Kind(kind));
}
