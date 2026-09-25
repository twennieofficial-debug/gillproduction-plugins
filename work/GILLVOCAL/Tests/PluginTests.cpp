#include "../../GILLCommon/QualityTests.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <new>

namespace { thread_local bool watchNativeAudio=false;std::uint64_t nativeAudioAllocations=0; }
void* operator new(std::size_t size){if(watchNativeAudio)++nativeAudioAllocations;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0, failures = 0, audioConfigurations = 0, uiEdits = 0, screenshots = 0;
std::vector<std::string> failedCases;
std::array<double, 3> heatCpuPercent{};
std::string product;

void check(bool passed, const std::string& description) {
    ++checks;
    std::cout << (passed ? "PASS " : "FAIL ") << product << ": " << description << '\n';
    if (!passed) { ++failures; failedCases.push_back(product + ": " + description); }
}

std::vector<juce::String> ids(GillKind kind) {
    if (kind == GillKind::Flow) return {"amount", "mode", "autogain", "bypass"};
    if (kind == GillKind::Heat) return {"low", "mid", "high", "style", "mix", "output", "bypass"};
    return {"key", "scale", "retune", "humanize", "mix", "bypass"};
}
std::vector<float> values(GillVocalProcessor& p) {
    std::vector<float> result;
    for (const auto& id : ids(p.kind)) result.push_back(p.value(id));
    return result;
}
void set(GillVocalProcessor& p, const juce::String& id, float value) { p.setValue(id, value, false); }
void prepare(GillVocalProcessor& p, double rate = 48000.0, int block = 128, int channels = 2) {
    p.setPlayConfigDetails(channels, channels, rate, block);
    p.prepareToPlay(rate, block);
}
void stateFromTree(GillVocalProcessor& p, const juce::ValueTree& tree) {
    juce::MemoryBlock state;
    if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, state);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}
void audioCall(GillVocalProcessor& p,juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi,bool bypass=false){
    watchNativeAudio=true;
    if(bypass)p.processBlockBypassed(audio,midi);else p.processBlock(audio,midi);
    watchNativeAudio=false;
}
void silence(GillVocalProcessor& p, int samples, bool hostBypass = false) {
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 128);
    juce::MidiBuffer midi;
    for (int at = 0; at < samples; at += 128) {
        buffer.clear();
        if (hostBypass) audioCall(p, buffer, midi, true); else audioCall(p, buffer, midi);
    }
}
// Periodic voiced test signal; no external recording or fabricated meter state.
float vocal(double seconds, int channel = 0) {
    const double envelope = 0.35 + 0.25 * std::sin(2.0 * pi * 2.7 * seconds);
    return static_cast<float>(envelope * (std::sin(2.0 * pi * 227.0 * seconds) +
        0.23 * std::sin(2.0 * pi * 454.0 * seconds) +
        0.10 * std::sin(2.0 * pi * 681.0 * seconds)) * (channel == 0 ? 1.0 : 0.87));
}
void feedVocal(GillVocalProcessor& p, double seconds, bool tick = false) {
    const double rate = p.uiRate.load();
    const int total = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 128);
    juce::MidiBuffer midi;
    for (int at = 0; at < total; at += 128) {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int n = 0; n < 128; ++n) buffer.setSample(c, n, vocal((at + n) / rate, c));
        audioCall(p, buffer, midi);
        if (tick && (at / 128) % 24 == 0) {
            juce::Thread::sleep(41);
            juce::Timer::callPendingTimersSynchronously();
        }
    }
}

void metadataAndState(GillKind kind, bool liveTune = false) {
    GillVocalProcessor p(kind,liveTune);
    product = p.getName().toStdString();
    const auto parameterIds = ids(kind);
    check(p.getParameters().size() == static_cast<int>(parameterIds.size()) + 1 &&
          p.getBypassParameter() == p.apvts.getParameter("bypass") && p.hasEditor() &&
          !p.acceptsMidi() && !p.producesMidi(), "parameter count, host bypass and effect metadata");
    auto layout = p.getBusesLayout();
    bool correctLayouts = true;
    for (const auto channels : {juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo()}) {
        layout.inputBuses.set(0, channels); layout.outputBuses.set(0, channels);
        correctLayouts = correctLayouts && p.isBusesLayoutSupported(layout);
    }
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    correctLayouts = correctLayouts && !p.isBusesLayoutSupported(layout);
    layout.inputBuses.set(0, juce::AudioChannelSet::create5point1());
    layout.outputBuses.set(0, juce::AudioChannelSet::create5point1());
    correctLayouts = correctLayouts && !p.isBusesLayoutSupported(layout);
    check(correctLayouts, "mono/stereo supported; mismatched and surround layouts rejected");

    int index = 0;
    for (const auto& id : parameterIds) {
        auto* parameter = p.apvts.getParameter(id);
        set(p, id, parameter->convertFrom0to1(index++ % 2 ? 0.71f : 0.37f));
    }
    const auto saved = values(p);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    for (const auto& id : parameterIds) set(p, id, p.apvts.getParameter(id)->getNormalisableRange().start);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(values(p) == saved, "every parameter survives state roundtrip");
    p.setStateInformation(nullptr, 100);
    p.setStateInformation("junk", 4);
    p.setStateInformation(state.getData(), 1024 * 1024 + 1);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize() / 2));
    check(values(p) == saved, "null, malformed, oversized and truncated state ignored");
    GillVocalProcessor other(kind == GillKind::Flow ? GillKind::Heat : GillKind::Flow);
    juce::MemoryBlock otherState; other.getStateInformation(otherState);
    p.setStateInformation(otherState.getData(), static_cast<int>(otherState.getSize()));
    check(values(p) == saved, "another product state cannot overwrite settings");
    for (const char* poison : {"NaN", "inf", "-inf", "12broken"}) {
        auto tree = p.apvts.copyState();
        for (auto child : tree) child.setProperty("value", poison, nullptr);
        stateFromTree(p, tree);
    }
    check(values(p) == saved, "non-finite and malformed numeric parameter values ignored");
    auto missing = p.apvts.copyState();
    for (auto child : missing) child.removeProperty("value", nullptr);
    stateFromTree(p, missing);
    check(values(p) == saved, "missing values preserve current parameters");
    bool clamped = true;
    for (int direction : {-1, 1}) {
        auto tree = p.apvts.copyState();
        for (auto child : tree) child.setProperty("value", direction * 1000000.0, nullptr);
        stateFromTree(p, tree);
        for (const auto& id : parameterIds) {
            const auto& range = p.apvts.getParameter(id)->getNormalisableRange();
            clamped = clamped && p.value(id) == (direction < 0 ? range.start : range.end);
        }
    }
    check(clamped, "every state parameter clamps to its legal range");
    const auto beforePartial = values(p);
    juce::ValueTree partial(p.apvts.state.getType());
    auto child = p.apvts.copyState().getChildWithProperty("id", parameterIds.front()).createCopy();
    child.setProperty("value", p.apvts.getParameter(parameterIds.front())->getNormalisableRange().start, nullptr);
    partial.addChild(child, -1, nullptr);
    juce::ValueTree unknown("PARAM"); unknown.setProperty("id", "unknown_parameter", nullptr); unknown.setProperty("value", 42, nullptr);
    partial.addChild(unknown, -1, nullptr);
    stateFromTree(p, partial);
    const auto afterPartial = values(p);
    bool partialCorrect = afterPartial[0] == p.apvts.getParameter(parameterIds.front())->getNormalisableRange().start;
    for (std::size_t n = 1; n < beforePartial.size(); ++n) partialCorrect = partialCorrect && beforePartial[n] == afterPartial[n];
    check(partialCorrect, "partial state changes only the supplied known parameter");
}

void programs(bool liveTune = false) {
    GillVocalProcessor p(GillKind::Tune,liveTune); product = p.getName().toStdString();
    constexpr float retune[]{100, 40, 15, 5, 0}, human[]{80, 45, 20, 5, 0};
    const char* names[]{"NATURAL", "POP", "RAP", "TRAP", "ROBOT"};
    set(p, "key", 9); set(p, "scale", 2); set(p, "bypass", 1);
    bool correct = p.getNumPrograms() == 5;
    for (int program = 0; program < 5; ++program) {
        p.setCurrentProgram(program);
        correct = correct && p.getCurrentProgram() == program && p.getProgramName(program) == names[program] &&
            p.value("retune") == retune[program] && p.value("humanize") == human[program] && p.value("mix") == 100 &&
            p.value("key") == 9 && p.value("scale") == 2 && p.value("bypass") == 1 && p.presetMatches();
    }
    check(correct, "five real programs apply documented values and preserve key/scale/bypass");
    set(p, "retune", 23.7f);
    check(!p.presetMatches(), "edited preset is recognized as CUSTOM");
    juce::MemoryBlock state; p.getStateInformation(state);
    GillVocalProcessor restored(GillKind::Tune,liveTune);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(restored.getCurrentProgram() == 4 && !restored.presetMatches() && values(restored) == values(p),
          "custom program and all controls survive a new project instance");
    p.setCurrentProgram(-50); bool bounded = p.getCurrentProgram() == 0;
    p.setCurrentProgram(500); bounded = bounded && p.getCurrentProgram() == 4;
    check(bounded, "program API clamps invalid indices");
}

void dryRoutes(GillKind kind, bool liveTune = false) {
    product = kind == GillKind::Flow ? "GILLFLOW" : kind == GillKind::Heat ? "GILLHEAT" : liveTune ? "GILLTUNE LIVE" : "GILLTUNE";
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) {
        bool exact = true, validLatency = true;
        for (int channels : {1, 2}) for (int route = 0; route < (kind == GillKind::Heat ? 4 : 3); ++route) {
            GillVocalProcessor p(kind,liveTune);
            if (route == 0) set(p, kind == GillKind::Flow ? "amount" : "mix", 0);
            if (route == 1) set(p, "bypass", 1);
            if (route == 3) { set(p, "low", 0); set(p, "mid", 0); set(p, "high", 0); set(p, "mix", 100); }
            prepare(p, rate, 512, channels);
            const int delay = p.getLatencySamples();
            validLatency = validLatency && delay >= 0 && p.getTailLengthSeconds() >= delay / rate &&
                (kind != GillKind::Flow || delay == 0) && (kind != GillKind::Heat || delay == 24) &&
                (!liveTune || std::abs(delay-rate*.016)<=1.0);
            silence(p, static_cast<int>(rate * 0.03) + delay, route == 2);
            const int total = delay + 4096;
            std::vector<std::vector<float>> source(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(total)));
            for (int c = 0; c < channels; ++c) for (int n = 0; n < total; ++n)
                source[c][n] = static_cast<float>(0.3 * std::sin((0.047 + c * 0.01) * n) + (n == 73 ? 0.4 : 0.0));
            juce::AudioBuffer<float> buffer(channels, 511); juce::MidiBuffer midi;
            const int sizes[]{1, 17, 64, 127, 257, 511}; int block = 0;
            for (int at = 0; at < total;) {
                const int count = std::min(sizes[block++ % 6], total - at);
                buffer.setSize(channels, count, false, false, true);
                for (int c = 0; c < channels; ++c) for (int n = 0; n < count; ++n) buffer.setSample(c, n, source[c][at + n]);
                if (route == 2) audioCall(p, buffer, midi, true); else audioCall(p, buffer, midi);
                for (int c = 0; c < channels; ++c) for (int n = 0; n < count; ++n)
                    exact = exact && buffer.getSample(c, n) == (at + n < delay ? 0.0f : source[c][at + n - delay]);
                at += count;
            }
            ++audioConfigurations;
            p.releaseResources();
        }
        check(exact && validLatency, "exact neutral/native-bypass/host-bypass, mono+stereo and six block sizes at " + std::to_string(static_cast<int>(rate)) + " Hz");
    }
}

void bypassTransitions(GillKind kind, bool liveTune = false) {
    product = kind == GillKind::Flow ? "GILLFLOW" : kind == GillKind::Heat ? "GILLHEAT" : liveTune ? "GILLTUNE LIVE" : "GILLTUNE";
    for (bool host : {false, true}) {
        GillVocalProcessor wet(kind,liveTune), actual(kind,liveTune);
        prepare(wet); prepare(actual);
        const int delay = actual.getLatencySamples(), blocks = 120;
        std::array<std::vector<float>, 2> input{std::vector<float>(blocks * 128), std::vector<float>(blocks * 128)};
        for (int c = 0; c < 2; ++c) for (int n = 0; n < blocks * 128; ++n) input[c][n] = vocal(n / 48000.0, c);
        juce::SmoothedValue<float> blend; blend.reset(48000.0, 0.005); blend.setCurrentAndTargetValue(0.0f);
        juce::AudioBuffer<float> a(2, 128), b(2, 128); juce::MidiBuffer midi;
        double error = 0.0; bool latencyStable = true;
        for (int block = 0; block < blocks; ++block) {
            const bool bypassed = block >= 50 && block < 75;
            if (!host) set(actual, "bypass", bypassed ? 1.0f : 0.0f);
            blend.setTargetValue(bypassed ? 1.0f : 0.0f);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n) {
                a.setSample(c, n, input[c][block * 128 + n]); b.setSample(c, n, input[c][block * 128 + n]);
            }
            audioCall(wet, a, midi);
            if (host && bypassed) audioCall(actual, b, midi, true); else audioCall(actual, b, midi);
            for (int n = 0; n < 128; ++n) {
                const float mix = blend.getNextValue();
                for (int c = 0; c < 2; ++c) {
                    const int index = block * 128 + n - delay;
                    const float dry = index < 0 ? 0.0f : input[c][index];
                    const float expected = mix >= 1.0f ? dry : a.getSample(c, n) + mix * (dry - a.getSample(c, n));
                    error = std::max(error, std::abs(static_cast<double>(expected - b.getSample(c, n))));
                }
            }
            latencyStable = latencyStable && actual.getLatencySamples() == delay;
        }
        check(error < 2.0e-6 && latencyStable, std::string(host ? "host" : "native") + " bypass transitions match aligned 5-ms wet/dry fade");
    }
}

bool sameProfile(const gill::LearnProfile& a, const gill::LearnProfile& b) {
    return a.version == b.version && a.valid == b.valid && a.rmsDb == b.rmsDb && a.peakDb == b.peakDb &&
        a.crestDb == b.crestDb && a.thresholdDb == b.thresholdDb && a.dynamicRangeDb == b.dynamicRangeDb &&
        a.motionDb == b.motionDb && a.attackMs == b.attackMs && a.releaseMs == b.releaseMs;
}
void learning() {
    GillVocalProcessor p(GillKind::Flow); product = p.getName().toStdString(); prepare(p);
    p.requestLearning(true); silence(p, 24000);
    check(p.learnState.load() == 1 && p.learnProgress.load() == 0.0f && !p.savedProfile().valid,
          "LEARN ignores silence and has no fabricated progress");
    feedVocal(p, 10.1);
    const auto learned = p.savedProfile();
    check(learned.valid && p.learnState.load() == 2 && p.learnProgress.load() == 1.0f &&
          learned.rmsDb < 0 && learned.peakDb >= learned.rmsDb, "ten seconds of real processed active signal creates a measured profile");
    p.releaseResources(); prepare(p, 96000.0, 257); silence(p, 257);
    check(sameProfile(learned, p.savedProfile()) && p.learnState.load() == 2, "completed profile survives reset and sample-rate change");
    juce::MemoryBlock state; p.getStateInformation(state);
    GillVocalProcessor restored(GillKind::Flow);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    prepare(restored); silence(restored, 128);
    check(sameProfile(learned, restored.savedProfile()) && restored.learnState.load() == 2, "completed profile restores into a new project instance");
    restored.requestLearning(true); feedVocal(restored, 0.1); restored.requestLearning(false); silence(restored, 128);
    check(sameProfile(learned, restored.savedProfile()) && restored.learnState.load() == 2, "cancelled relearning preserves completed profile");
    auto bad = restored.apvts.copyState(); bad.setProperty("learnValid", true, nullptr);
    bad.setProperty("learnRms", "NaN", nullptr); bad.setProperty("learnPeak", 8, nullptr);
    bad.setProperty("learnCrest", -50, nullptr); bad.setProperty("learnThreshold", 100, nullptr);
    stateFromTree(restored, bad); silence(restored, 128);
    check(sameProfile(learned, restored.savedProfile()), "malformed learned profile cannot poison valid state");
    auto legacy = p.apvts.copyState();legacy.setProperty("learnValid",true,nullptr);legacy.setProperty("learnRms",-25,nullptr);legacy.setProperty("learnPeak",-12,nullptr);legacy.setProperty("learnCrest",13,nullptr);legacy.setProperty("learnThreshold",-22,nullptr);
    for(const auto* field:{"learnVersion","learnRange","learnMotion","learnAttack","learnRelease"})legacy.removeProperty(field,nullptr);
    stateFromTree(restored,legacy);silence(restored,128);
    check(restored.savedProfile().valid&&restored.savedProfile().version==1&&restored.savedProfile().thresholdDb==-22,"v0.1 learned state without new fields restores as the original v1 profile");
}

void automation(GillKind kind, bool liveTune = false) {
    GillVocalProcessor p(kind,liveTune); product = p.getName().toStdString();
    const auto parameterIds = ids(kind);
    bool finite = true, stable = true, realMeters = false, midiCleared = true;
    unsigned random = 7512;
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0}) for (int channels : {1, 2}) {
        prepare(p, rate, 128, channels); const int latency = p.getLatencySamples();
        juce::AudioBuffer<float> buffer(channels, 1025); juce::MidiBuffer midi;
        const int sizes[]{1, 17, 128, 257, 1025};
        for (int block = 0; block < 120; ++block) {
            for (const auto& id : parameterIds) {
                random = random * 1664525u + 1013904223u;
                set(p, id, p.apvts.getParameter(id)->convertFrom0to1(static_cast<float>((random >> 8) / 16777215.0)));
            }
            const int count = sizes[block % 5]; buffer.setSize(channels, count, false, false, true);
            for (int c = 0; c < channels; ++c) for (int n = 0; n < count; ++n) buffer.setSample(c, n, vocal((block * 1025 + n) / rate, c));
            if (block % 11 == 0) buffer.setSample(0, count - 1, std::numeric_limits<float>::quiet_NaN());
            if (block % 13 == 0) buffer.setSample(0, count / 2, std::numeric_limits<float>::infinity());
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            audioCall(p, buffer, midi);
            for (int c = 0; c < channels; ++c) for (int n = 0; n < count; ++n)
                finite = finite && std::isfinite(buffer.getSample(c, n)) && std::abs(buffer.getSample(c, n)) < 1000.0f;
            stable = stable && p.getLatencySamples() == latency;
            realMeters = realMeters || (p.inputPeak.load() > 0 && p.outputPeak.load() > 0);
            midiCleared = midiCleared && midi.isEmpty();
        }
        ++audioConfigurations; p.releaseResources();
    }
    check(finite && stable, "all-parameter automation remains finite with NaN/Inf input across 8 rate/layout configurations");
    check(realMeters && midiCleared, "processed audio updates meters and effect clears unsupported MIDI");
}

struct HostListener final : juce::AudioProcessorListener {
    int begins = 0, ends = 0, changes = 0; bool balancedOrder = true;
    std::array<int, 32> depth{};
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++changes; }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override {
        ++begins; if (index >= 0 && index < 32) { ++depth[static_cast<std::size_t>(index)]; balancedOrder = balancedOrder && depth[static_cast<std::size_t>(index)] == 1; }
    }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index) override {
        ++ends; if (index >= 0 && index < 32) { --depth[static_cast<std::size_t>(index)]; balancedOrder = balancedOrder && depth[static_cast<std::size_t>(index)] == 0; }
    }
};
struct Components {
    std::vector<juce::Slider*> sliders; std::vector<juce::Button*> buttons;
    std::vector<juce::ComboBox*> combos; std::vector<juce::Component*> all;
    void collect(juce::Component& c) {
        all.push_back(&c);
        if (auto* s = dynamic_cast<juce::Slider*>(&c)) sliders.push_back(s);
        if (auto* b = dynamic_cast<juce::Button*>(&c)) buttons.push_back(b);
        if (auto* box = dynamic_cast<juce::ComboBox*>(&c)) combos.push_back(box);
        for (auto* child : c.getChildren()) collect(*child);
    }
    juce::Button* button(const juce::String& name) const {
        for (auto* b : buttons) if (b->getName() == name || b->getButtonText() == name) return b;
        return nullptr;
    }
    juce::ComboBox* combo(const juce::String& name) const {
        for (auto* b : combos) if (b->getName() == name) return b;
        return nullptr;
    }
};
juce::MouseEvent mouse(juce::Component& component, juce::Point<float> point,
                       int clicks = 1, bool dragged = false, juce::ModifierKeys modifiers = juce::ModifierKeys::leftButtonModifier) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point, modifiers, 1, 0, 0, 0, 0,
            &component, &component, now, point, now, clicks, dragged};
}
void click(juce::Button& b) {
    const auto point = b.getLocalBounds().getCentre().toFloat();
    // Component's public virtual interface dispatches the real Button handlers.
    juce::Component& component = b;
    component.mouseDown(mouse(b, point)); component.mouseUp(mouse(b, point, 1, false, juce::ModifierKeys())); ++uiEdits;
}
void tick() { juce::Thread::sleep(45); juce::Timer::callPendingTimersSynchronously(); }
template <typename Predicate> bool waitForVisualState(Predicate&& predicate) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (predicate()) return true;
        tick();
    }
    return predicate();
}
void screenshot(juce::AudioProcessorEditor& editor, int width, int height, const juce::String& name) {
    editor.setSize(width, height); tick();
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    const juce::String fileName = name + "-UI-" + juce::String(width) + "x" + juce::String(height) + ".png";
    juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile(fileName));
    bool written = output.openedOk();
    if (written) { output.setPosition(0); output.truncate(); juce::PNGImageFormat png; written = png.writeImageToStream(image, output); }
    check(written && image.getWidth() == width && image.getHeight() == height, "native editor PNG captured at " + std::to_string(width) + "x" + std::to_string(height));
    if (written) ++screenshots;
}

void ui(GillKind kind, bool liveTune = false) {
    GillVocalProcessor p(kind,liveTune); product = p.getName().toStdString(); prepare(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    check(editor != nullptr, "native editor constructs"); if (!editor) return;
    editor->setVisible(true);
    Components components; components.collect(*editor);
    const int width = kind == GillKind::Flow ? 340 : kind == GillKind::Heat ? 580 : 600;
    const int height = kind == GillKind::Flow ? 480 : kind == GillKind::Heat ? 380 : 560;
    const int expectedSliders = kind == GillKind::Flow ? 1 : kind == GillKind::Heat ? 5 : 3;
    const auto* limits = editor->getConstrainer();
    check(editor->getWidth() == width && editor->getHeight() == height && limits &&
          limits->getMinimumWidth() == width && limits->getMaximumWidth() == 2 * width &&
          std::abs(limits->getFixedAspectRatio() - static_cast<double>(width) / height) < 1.0e-9 &&
          components.sliders.size() == static_cast<std::size_t>(expectedSliders), "intended control count, native dimensions and 2x resize limits");
    HostListener listener; p.addListener(&listener);
    for (auto* slider : components.sliders) {
        juce::String id = slider->getName().toLowerCase();
        auto* parameter = p.apvts.getParameter(id);
        check(parameter != nullptr, "visible " + slider->getName().toStdString() + " slider has a real processor parameter");
        if (!parameter) continue;
        const auto& range = parameter->getNormalisableRange();
        check(slider->getMinimum() == range.start && slider->getMaximum() == range.end,
              slider->getName().toStdString() + " range matches processor");
        set(p, id, parameter->convertFrom0to1(0.4f));
        const float before = p.value(id); const int begins = listener.begins, ends = listener.ends;
        const bool handled = slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)); ++uiEdits;
        check(handled && p.value(id) > before && listener.begins == begins + 1 && listener.ends == ends + 1,
              slider->getName().toStdString() + " actual keyboard edit updates parameter with balanced gesture");
        const int hostBegins = listener.begins, hostChanges = listener.changes;
        const float hostValue = range.snapToLegalValue(parameter->convertFrom0to1(0.62f));
        set(p, id, hostValue);
        check(std::abs(slider->getValue() - hostValue) < 0.002 && listener.begins == hostBegins && listener.changes == hostChanges + 1,
              slider->getName().toStdString() + " host automation updates UI without echo gesture");
        juce::Label* text = nullptr;
        for (auto* child : slider->getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child)) text = label;
        if (text) { text->setText(juce::String(range.start), juce::sendNotificationSync); ++uiEdits; }
        check(text && p.value(id) == range.start, slider->getName().toStdString() + " editable numeric value changes DSP parameter");
        set(p, id, range.end);
        slider->mouseDoubleClick(mouse(*slider, {30, 30}, 2)); ++uiEdits;
        check(std::abs(p.value(id) - parameter->convertFrom0to1(parameter->getDefaultValue())) < 0.002,
              slider->getName().toStdString() + " actual double-click restores default");
    }
    bool modesCorrect = true;
    if (kind != GillKind::Tune) {
        const char* flowNames[]{"NATURAL", "FOCUS", "CRUSH"}; const char* heatNames[]{"WARM", "TAPE", "EDGE"};
        const char* id = kind == GillKind::Flow ? "mode" : "style";
        for (int mode = 0; mode < 3; ++mode) {
            auto* b = components.button(kind == GillKind::Flow ? flowNames[mode] : heatNames[mode]);
            if (b) click(*b);
            const bool selected = waitForVisualState([&] { return b && p.value(id) == mode && b->getToggleState(); });
            if (!selected) std::cout << "DIAG MODE expected=" << mode << " parameter=" << p.value(id)
                                     << " selected=" << (b && b->getToggleState()) << '\n';
            modesCorrect = modesCorrect && selected;
        }
        check(modesCorrect, "all three actual mode-button events update parameter and selected state");
    } else {
        auto* key = components.combo("KEY"); auto* scale = components.combo("SCALE");
        if (key) { key->setSelectedId(10, juce::sendNotificationSync); ++uiEdits; }
        if (scale) { scale->setSelectedId(3, juce::sendNotificationSync); ++uiEdits; }
        bool combosCorrect = key && scale && p.value("key") == 9 && p.value("scale") == 2;
        for (int index = 0; index < 5; ++index) {
            auto* preset=components.button("PRESET "+p.getProgramName(index));if(preset)click(*preset);
            combosCorrect = combosCorrect && waitForVisualState([&]{return preset&&preset->getToggleState();}) && p.getCurrentProgram() == index && p.presetMatches() && p.value("key") == 9 && p.value("scale") == 2;
        }
        check(combosCorrect, "real key/scale controls and five preset buttons apply all programs without changing musical key");
        set(p, "retune", 21.1f);
        check(waitForVisualState([&]{for(int i=0;i<5;++i){auto* b=components.button("PRESET "+p.getProgramName(i));if(!b||b->getToggleState())return false;}return true;}),"custom settings clear every preset highlight");
        for(auto* ring:components.sliders)if(ring->getName()=="RETUNE"){
            const auto area=ring->getLookAndFeel().getSliderLayout(*ring).sliderBounds;const auto centre=area.getCentre();
            check(!ring->hitTest(centre.x,centre.y)&&ring->hitTest(centre.x,area.getY()+18),"command wheel hit testing excludes the cents display and includes the outer retune ring");
            for(int scaleFactor:{1,2}){
                editor->setSize(width*scaleFactor,height*scaleFactor);tick();
                const auto bounds=ring->getLookAndFeel().getSliderLayout(*ring).sliderBounds;
                const auto c=bounds.getCentre().toFloat();const float radius=(std::min(bounds.getWidth(),bounds.getHeight())-14)*.5f*.865f;
                const auto pos=[&](double fraction){const double a=pi*(1.2+1.6*fraction);return c+juce::Point<float>(static_cast<float>(std::sin(a)*radius),static_cast<float>(-std::cos(a)*radius));};
                check(ring->getSliderStyle()==juce::Slider::Rotary,"command wheel uses true angular tracking at "+std::to_string(scaleFactor)+"x");
                set(p,"retune",20);tick();ring->mouseDown(mouse(*ring,pos(.1)));
                check(std::abs(p.value("retune")-20)<.11f,"grabbing the visible ring marker preserves the current value");
                bool clockwise=true,counterclockwise=true;
                for(int step=11;step<=100;++step){ring->mouseDrag(mouse(*ring,pos(step*.01),1,true));++uiEdits;clockwise=clockwise&&std::abs(p.value("retune")-step*2)<.11f;}
                ring->mouseDrag(mouse(*ring,pos(1.025),1,true));++uiEdits;
                check(clockwise&&p.value("retune")==200,"clockwise circular drag follows each angle and stops at 200 ms without wrapping");
                for(int step=100;step>=0;--step){ring->mouseDrag(mouse(*ring,pos(step*.01),1,true));++uiEdits;counterclockwise=counterclockwise&&std::abs(p.value("retune")-step*2)<.11f;}
                ring->mouseDrag(mouse(*ring,pos(-.025),1,true));++uiEdits;
                check(counterclockwise&&p.value("retune")==0,"counterclockwise circular drag follows each angle and stops at zero without wrapping");
                ring->mouseUp(mouse(*ring,pos(-.025),1,false,juce::ModifierKeys()));
            }
            editor->setSize(width,height);tick();
        }
    }
    auto* bypass = components.button("BYPASS"); if (bypass) click(*bypass);
    check(bypass && p.value("bypass") == 1.0f, "actual BYPASS mouse click updates host parameter");
    if (bypass) click(*bypass);
    if (kind == GillKind::Flow) {
        auto* gain = components.button("AUTO GAIN"); const float before = p.value("autogain"); if (gain) click(*gain);
        check(gain && p.value("autogain") != before, "actual AUTO GAIN mouse click updates parameter");
        auto* learn = components.button("LEARN"); if (learn) click(*learn); silence(p, 128);
        check(waitForVisualState([&] { return learn && p.learnState.load() == 1 && learn->getButtonText() == "CANCEL"; }),
              "actual LEARN click starts analysis and exposes CANCEL");
        if (learn) click(*learn); silence(p, 128);
        check(waitForVisualState([&] { return learn && p.learnState.load() == 0 && learn->getButtonText() == "LEARN"; }),
              "actual CANCEL click stops unfinished analysis");
        if (learn) click(*learn); feedVocal(p, 10.1); tick();
        check(p.learnState.load() == 2 && p.savedProfile().valid, "real LEARN button reaches READY from processed signal");
    }
    check(listener.begins == listener.ends && listener.balancedOrder && listener.begins > 0,
          "all UI edits emit balanced non-nested host gestures");
    p.removeListener(&listener);
    set(p, "bypass", 0);
    if (kind == GillKind::Flow) { set(p, "amount", 65); set(p, "mode", 1); set(p, "autogain", 1); }
    if (kind == GillKind::Heat) { set(p, "low", 5); set(p, "mid", 9); set(p, "high", 4); set(p, "style", 0); set(p, "mix", 85); set(p, "output", 0); }
    if (kind == GillKind::Tune) { p.setCurrentProgram(1); set(p, "key", 0); set(p, "scale", 1); }
    // Fill the display's 100-point history using its real timer and meters.
    feedVocal(p, 6.4, true); tick();
    check(p.inputPeak.load() > 0.0f && p.outputPeak.load() > 0.0f &&
          (kind != GillKind::Tune || (p.pitchHz.load() > 70 && p.targetHz.load() > 70 && p.pitchConfidence.load() > 0.5f)),
          "screenshot meters derive from actually processed synthetic vocal");
    bool contained = true;
    for (int multiplier : {1, 2}) {
        screenshot(*editor, width * multiplier, height * multiplier, p.getName());
        for (auto* c : components.all) if (c->getParentComponent() == editor.get())
            contained = contained && editor->getLocalBounds().contains(c->getBounds());
    }
    check(contained, "all top-level controls remain inside native and doubled editor bounds");
    editor.reset(); p.releaseResources();
}

void heatBenchmark() {
    product = "GILLHEAT";
    constexpr int blockSize = 128, blocks = 3750; // Ten seconds, stereo, 48 kHz.
    std::array<float, blockSize> left{}, right{}, source{};
    for (int n = 0; n < blockSize; ++n) source[static_cast<std::size_t>(n)] = vocal(n / 48000.0);
    float* buffers[]{left.data(), right.data()};
    volatile double checksum = 0.0;
    for (int style = 0; style < 3; ++style) {
        std::array<double, 3> elapsed{};
        for (int repetition = 0; repetition < 3; ++repetition) {
            gill::HeatDSP dsp; dsp.setParameters(8, 12, 7, style, 85, 0); dsp.prepare(48000, blockSize, 2);
            const auto start = std::chrono::steady_clock::now();
            for (int block = 0; block < blocks; ++block) {
                left = source; right = source; dsp.process(buffers, 2, blockSize); checksum += left[static_cast<std::size_t>(block % blockSize)];
            }
            elapsed[static_cast<std::size_t>(repetition)] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
        std::sort(elapsed.begin(), elapsed.end());
        heatCpuPercent[static_cast<std::size_t>(style)] = elapsed[1] * 10.0;
        std::cout << "BENCH Heat style=" << style << " stereo 48000Hz/128 median_seconds_per_10s=" << elapsed[1]
                  << " realtime_cpu_percent=" << heatCpuPercent[static_cast<std::size_t>(style)] << '\n';
    }
    check(std::isfinite(checksum) && *std::max_element(heatCpuPercent.begin(), heatCpuPercent.end()) < 100.0,
          "three saturation styles sustain real-time stereo 48-kHz/128-sample throughput");
}
} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    juce::ScopedJuceInitialiser_GUI gui;
    // Update06: exercise real LIVE/PRO host state, audio timing and UI.
    gill::testing::qualityRoutes([]{return std::make_unique<GillVocalProcessor>(GillKind::Flow);},[](bool ok,const std::string& why){check(ok,why);});
    gill::testing::qualityRoutes([]{return std::make_unique<GillVocalProcessor>(GillKind::Heat);},[](bool ok,const std::string& why){check(ok,why);});
    gill::testing::qualityRoutes([]{return std::make_unique<GillVocalProcessor>(GillKind::Tune);},[](bool ok,const std::string& why){check(ok,why);});
    gill::testing::qualityRoutes([]{return std::make_unique<GillVocalProcessor>(GillKind::Tune,true);},[](bool ok,const std::string&why){check(ok,why);});

    for (auto kind : {GillKind::Flow, GillKind::Heat, GillKind::Tune}) {
        metadataAndState(kind); dryRoutes(kind); bypassTransitions(kind); automation(kind);
    }
    programs(); learning();
    for (auto kind : {GillKind::Flow, GillKind::Heat, GillKind::Tune}) ui(kind);
    metadataAndState(GillKind::Tune,true);programs(true);dryRoutes(GillKind::Tune,true);
    bypassTransitions(GillKind::Tune,true);automation(GillKind::Tune,true);ui(GillKind::Tune,true);
    heatBenchmark();
    check(nativeAudioAllocations==0,"no allocations during actual processor audio calls, including automation and learning");
    std::cout << "NATIVE_PROCESS_ALLOCATIONS " << nativeAudioAllocations << std::endl;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::ofstream report("vocal-plugin-integration-report.json");
    report << "{\n  \"passed\": " << (failures == 0 ? "true" : "false") << ",\n  \"checks\": " << checks
           << ",\n  \"failures\": " << failures << ",\n  \"audio_thread_allocations\": " << nativeAudioAllocations << ",\n  \"audio_configurations\": " << audioConfigurations
           << ",\n  \"ui_edits\": " << uiEdits << ",\n  \"native_screenshots\": " << screenshots
           << ",\n  \"elapsed_seconds\": " << elapsed << ",\n  \"heat_stereo_48k_128_median_cpu_percent\": ["
           << heatCpuPercent[0] << ", " << heatCpuPercent[1] << ", " << heatCpuPercent[2] << "],\n  \"failed_cases\": [";
    for (std::size_t i = 0; i < failedCases.size(); ++i) report << (i ? ", " : "") << '"' << failedCases[i] << '"';
    report << "]\n}\n";
    std::cout << "RESULT " << checks << " behavior checks, " << failures << " failures, " << audioConfigurations
              << " audio configurations, " << uiEdits << " UI edits, " << screenshots << " native screenshots, " << elapsed << " seconds\n";
    return failures ? 1 : 0;
}
