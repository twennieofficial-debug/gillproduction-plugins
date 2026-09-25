#include "../../GILLCommon/QualityTests.h"
#include "PluginProcessor.h"
#include "Presets.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0, failures = 0, audioConfigurations = 0, uiEdits = 0, screenshots = 0;
std::vector<std::string> failedCases;

std::string product;

void check(bool passed, const std::string& description) {
    ++checks;
    std::cout << (passed ? "PASS " : "FAIL ") << product << ": " << description << '\n';
    if (!passed) { ++failures; failedCases.push_back(product + ": " + description); }
}

std::vector<juce::String> ids(GillKind kind) {
    if (kind == GillKind::Air) return {"midair","highair","mix","output","bypass"};
    if (kind == GillKind::Space) return {"mix","decay","predelay","tone","size","width","style","mixlock","bypass","dry"};
    if (kind == GillKind::Echo) return {"time","feedback","mix","color","width","style","sync","division","bpm","mixlock","bypass","dry"};
    return {"amount","target","bypass"};
}
std::vector<float> values(GillEffectProcessor& p) {
    std::vector<float> result;
    for (const auto& id : ids(p.kind)) result.push_back(p.value(id));
    return result;
}
void set(GillEffectProcessor& p, const juce::String& id, float value) { p.setValue(id, value, false); }
void prepare(GillEffectProcessor& p, double rate = 48000.0, int block = 128, int channels = 2) {
    p.setPlayConfigDetails(channels, channels, rate, block);
    p.prepareToPlay(rate, block);
}
void stateFromTree(GillEffectProcessor& p, const juce::ValueTree& tree) {
    juce::MemoryBlock state;
    if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, state);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}
void silence(GillEffectProcessor& p, int samples, bool hostBypass = false) {
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 128);
    juce::MidiBuffer midi;
    for (int at = 0; at < samples; at += 128) {
        buffer.clear();
        if (hostBypass) p.processBlockBypassed(buffer, midi); else p.processBlock(buffer, midi);
    }
}
// Periodic voiced test signal; no external recording or fabricated meter state.
float vocal(double seconds, int channel = 0) {
    const double envelope = 0.35 + 0.25 * std::sin(2.0 * pi * 2.7 * seconds);
    return static_cast<float>(envelope * (std::sin(2.0 * pi * 227.0 * seconds) +
        0.23 * std::sin(2.0 * pi * 454.0 * seconds) +
        0.10 * std::sin(2.0 * pi * 681.0 * seconds)) * (channel == 0 ? 1.0 : 0.87));
}
void feedVocal(GillEffectProcessor& p, double seconds, bool tick = false) {
    const double rate = p.uiRate.load();
    const int total = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 128);
    juce::MidiBuffer midi;
    for (int at = 0; at < total; at += 128) {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            for (int n = 0; n < 128; ++n) buffer.setSample(c, n, vocal((at + n) / rate, c));
        p.processBlock(buffer, midi);
        if (tick && (at / 128) % 24 == 0) {
            juce::Thread::sleep(41);
            juce::Timer::callPendingTimersSynchronously();
        }
    }
}

void metadataAndState(GillKind kind) {
    GillEffectProcessor p(kind);
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
    GillEffectProcessor other(kind == GillKind::Air ? GillKind::Balance : GillKind::Air);
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
            const float expected=range.snapToLegalValue(direction<0?range.start:range.end);
            if(p.value(id)!=expected)std::cout<<std::setprecision(12)<<"DIAG STATE "<<product<<" id="<<id<<" direction="<<direction<<" actual="<<p.value(id)<<" legalEndpoint="<<expected<<" interval="<<range.interval<<'\n';
            clamped = clamped && p.value(id) == expected;
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

void automation(GillKind kind) {
    GillEffectProcessor p(kind); product = p.getName().toStdString();
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
            p.processBlock(buffer, midi);
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

void programs(GillKind kind) {
    GillEffectProcessor p(kind); product=p.getName().toStdString();
    check(p.getNumPrograms()==12,"twelve native factory programs");
    set(p,"bypass",1);if(kind==GillKind::Echo)set(p,"bpm",137);
    bool all=true,locked=true;
    for(bool lock:{false,true}) {
        set(p,"mixlock",lock?1.f:0.f);set(p,"mix",37);
        for(int i=0;i<12;++i) {
            p.setCurrentProgram(i);
            all=all&&p.getCurrentProgram()==i&&p.presetMatches()&&p.value("bypass")==1;
            if(kind==GillKind::Space){const auto& q=gill::spacePresets[i];all=all&&p.getProgramName(i)==q.name&&std::abs(p.value("decay")-q.decay)<.011&&p.value("style")==q.style;locked=locked&&std::abs(p.value("mix")-(lock?37:q.mix))<.001;}
            else {const auto& q=gill::echoPresets[i];all=all&&p.getProgramName(i)==q.name&&p.value("division")==q.division&&p.value("sync")==(q.sync?1.f:0.f)&&p.value("bpm")==137;locked=locked&&std::abs(p.value("mix")-(lock?37:q.mix))<.001;}
        }
    }
    check(all,"all programs apply real parameter banks and preserve bypass/manual tempo");
    check(locked,"MIX LOCK preserves user mix across every factory program");
    set(p,kind==GillKind::Space?"tone":"color",13.7f);check(!p.presetMatches(),"edited preset recognized as CUSTOM");
    juce::MemoryBlock state;p.getStateInformation(state);GillEffectProcessor restored(kind);restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    check(values(restored)==values(p)&&restored.getCurrentProgram()==11&&!restored.presetMatches(),"custom factory program, lock and all controls restore in fresh instance");
    p.setCurrentProgram(-300);bool clamped=p.getCurrentProgram()==0;p.setCurrentProgram(999);clamped=clamped&&p.getCurrentProgram()==11;
    auto tree=p.apvts.copyState();tree.setProperty("program",1e250,nullptr);stateFromTree(p,tree);clamped=clamped&&p.getCurrentProgram()==11;
    check(clamped,"program indices and huge state numbers clamp before integer conversion");
}

void dryRoutes(GillKind kind) {
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}) {
        bool exact=true,latencies=true;
        for(int channels:{1,2})for(int route=0;route<(kind==GillKind::Air?4:3);++route) {
            GillEffectProcessor p(kind);product=p.getName().toStdString();
            if(route==0)set(p,kind==GillKind::Balance?"amount":"mix",0);
            if(route==1)set(p,"bypass",1);
            if(route==3){set(p,"midair",0);set(p,"highair",0);}
            prepare(p,rate,257,channels);const int delay=p.getLatencySamples();
            latencies=latencies&&delay==(kind==GillKind::Air?24:0)&&std::isfinite(p.getTailLengthSeconds())&&p.getTailLengthSeconds()>=0;
            silence(p,static_cast<int>(rate*.1)+delay,route==2);
            const int total=delay+1025;std::array<std::vector<float>,2> source{std::vector<float>(total),std::vector<float>(total)};
            for(int c=0;c<channels;++c)for(int i=0;i<total;++i)source[c][i]=static_cast<float>(.2*std::sin((.1+.05*c)*i)+(i==21?.4:0));
            juce::AudioBuffer<float> audio(channels,257);juce::MidiBuffer midi;
            for(int at=0;at<total;){const int n=std::min(257,total-at);audio.setSize(channels,n,false,false,true);for(int c=0;c<channels;++c)audio.copyFrom(c,0,source[c].data()+at,n);
                if(route==2)p.processBlockBypassed(audio,midi);else p.processBlock(audio,midi);
                for(int c=0;c<channels;++c)for(int i=0;i<n;++i)exact=exact&&audio.getSample(c,i)==(at+i<delay?0:source[c][at+i-delay]);at+=n;
            }++audioConfigurations;
        }
        check(exact&&latencies,"exact dry/native bypass/host bypass in mono+stereo at "+std::to_string(static_cast<int>(rate))+" Hz");
    }
    for(double rate:{7999.,384001.}){GillEffectProcessor p(kind);prepare(p,rate);const int delay=p.getLatencySamples();juce::AudioBuffer<float> audio(2,128);audio.clear();audio.setSample(0,0,.25f);juce::MidiBuffer midi;p.processBlock(audio,midi);bool correct=!p.rateSupported.load();for(int i=0;i<128;++i)correct=correct&&audio.getSample(0,i)==(i==delay?.25f:0)&&audio.getSample(1,i)==0;check(correct,"unsupported sample rate gives explicit latency-aligned pass-through");}
}

bool sameProfile(const gill::LearnBalanceProfile& a,const gill::LearnBalanceProfile& b){return a.version==b.version&&a.valid==b.valid&&a.sampleRate==b.sampleRate&&a.bandDb==b.bandDb&&a.validBands==b.validBands&&a.activeRmsDb==b.activeRmsDb&&a.fine.db==b.fine.db&&a.fine.frames==b.fine.frames&&a.fine.analysisRate==b.fine.analysisRate;}
void dryMigration(){for(auto kind:{GillKind::Space,GillKind::Echo}){GillEffectProcessor p(kind);product=p.getName().toStdString();set(p,"mix",37.2f);auto old=p.apvts.copyState();old.removeChild(old.getChildWithProperty("id","dry"),nullptr);old.setProperty("version",1,nullptr);set(p,"dry",0);stateFromTree(p,old);check(p.value("dry")==100&&std::abs(p.value("mix")-37.2f)<.001,"v1 state explicitly restores old DRY100 routing even after a wet-only session");set(p,"dry",0);bool all=true;for(int i=0;i<12;++i){p.selectPreset(i,false);all&=p.value("dry")==0;}check(all,"all factory presets retain user's wet-only DRY setting");juce::MemoryBlock state;p.getStateInformation(state);GillEffectProcessor restored(kind);restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));check(restored.value("dry")==0,"v2 state recalls wet-only routing");}}
void learning() {
    GillEffectProcessor p(GillKind::Balance);product=p.getName().toStdString();prepare(p,8000);
    p.requestLearning(true);silence(p,4000);check(p.learnState==1&&p.learnProgress==0&&!p.savedProfile().valid,"learning command ignores silence and exposes actual progress");
    feedVocal(p,10.1);const auto learned=p.savedProfile();check(learned.valid&&p.learnState==2&&p.learnProgress==1,"wrapper collects and publishes completed tonal statistics");
    juce::MemoryBlock state;p.getStateInformation(state);GillEffectProcessor restored(GillKind::Balance);restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    check(restored.learnState==2&&restored.learnProgress==1&&sameProfile(learned,restored.savedProfile()),"restored learned state is visibly READY even while transport is stopped");
    restored.releaseResources();
    check(restored.learnState==2&&restored.learnProgress==1&&sameProfile(learned,restored.savedProfile()),"release after stopped profile restore preserves pending READY status");
    prepare(restored);silence(restored,128);
    check(sameProfile(learned,restored.savedProfile())&&restored.learnState==2,"learned profile restores into a new processor across sample rates");
    restored.requestLearning(true);silence(restored,128);restored.requestLearning(false);silence(restored,128);check(sameProfile(learned,restored.savedProfile())&&restored.learnState==2,"cancelled relearn retains saved profile");
    for(const auto* badField:{"learnVersion","learnRate","learnRms","learnBand2","learnUse3","fineRate","fineFrames","fineBin127"}){auto tree=juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize())));tree.setProperty(badField,"NaN",nullptr);stateFromTree(restored,tree);silence(restored,128);check(sameProfile(learned,restored.savedProfile()),std::string("malformed ")+badField+" cannot poison saved profile");}
    auto tree=juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize())));tree.removeProperty("learnBand0",nullptr);stateFromTree(restored,tree);silence(restored,128);check(sameProfile(learned,restored.savedProfile()),"partial learned profile is rejected atomically");
    p.releaseResources();prepare(p,96000);silence(p,128);check(sameProfile(learned,p.savedProfile())&&p.learnState==2,"release/prepare preserves learned profile");
    feedVocal(restored,.5);const auto view=restored.balanceView();bool finite=true,real=false;for(size_t i=0;i<8;++i){finite=finite&&std::isfinite(view.gains[i])&&std::isfinite(view.pre[i])&&std::isfinite(view.post[i]);real=real||view.pre[i]>-100;}check(finite&&real,"coherent graph snapshot contains real audio meters and finite gains");
}

struct TempoHead final:juce::AudioPlayHead {
    double bpm=120;bool available=true;mutable int calls=0;
    juce::Optional<PositionInfo> getPosition()const override {++calls;if(!available)return {};PositionInfo p;p.setBpm(bpm);return p;}
};
void tempo() {
    GillEffectProcessor p(GillKind::Echo);product=p.getName().toStdString();TempoHead head;p.setPlayHead(&head);prepare(p);check(head.calls==0,"prepare does not query audio-thread-only host position");set(p,"sync",1);bool all=true;
    for(double bpm:{30.,87.,137.,300.})for(int division=0;division<8;++division){head.bpm=bpm;set(p,"division",static_cast<float>(division));silence(p,1);const double expected=std::clamp(60000./bpm*GillEffectProcessor::divisionBeats[division],1.,8000.);all=all&&p.hostTempoAvailable&&std::abs(p.delayMs-expected)<.002;}
    check(all,"all eight note divisions use actual host tempo without rounding triplets");
    head.bpm=10;set(p,"division",7);silence(p,1);check(p.delayLimited&&p.delayMs==8000,"tempo-derived delay beyond storage range is capped and visibly flagged");
    head.bpm=std::numeric_limits<double>::quiet_NaN();set(p,"bpm",150);set(p,"division",4);silence(p,1);check(!p.hostTempoAvailable&&p.tempoBpm==150&&p.delayMs==400,"invalid host BPM falls back to saved manual tempo");
    head.available=false;set(p,"sync",0);set(p,"time",731.2f);silence(p,1);check(!p.hostTempoAvailable&&std::abs(p.delayMs-731.2f)<.001,"free-time delay uses milliseconds when transport has no BPM");p.setPlayHead(nullptr);
}


void bypassTransitions(GillKind kind) {
    GillEffectProcessor named(kind); product = named.getName().toStdString();
    for (bool host : {false, true}) {
        GillEffectProcessor wet(kind), actual(kind);
        if(kind==GillKind::Balance) {
            auto learnedFixture=[](GillEffectProcessor& instance) {
                auto tree=instance.apvts.copyState();tree.setProperty("learnVersion",1,nullptr);tree.setProperty("learnValid",1,nullptr);tree.setProperty("learnRate",48000.,nullptr);tree.setProperty("learnRms",-20.,nullptr);
                for(int b=0;b<8;++b){tree.setProperty("learnBand"+juce::String(b),b==2?-8.:-10.-b*2.,nullptr);tree.setProperty("learnUse"+juce::String(b),1,nullptr);}stateFromTree(instance,tree);
            };
            learnedFixture(wet);learnedFixture(actual);
        }
        prepare(wet); prepare(actual);
        const int delay = actual.getLatencySamples(), blocks = 120;
        std::array<std::vector<float>, 2> input{std::vector<float>(blocks * 128), std::vector<float>(blocks * 128)};
        for (int c = 0; c < 2; ++c) for (int n = 0; n < blocks * 128; ++n) input[c][n] = vocal(n / 48000.0, c);
        juce::SmoothedValue<float> blend; blend.reset(48000.0, 0.005); blend.setCurrentAndTargetValue(0.0f);
        juce::AudioBuffer<float> a(2, 128), b(2, 128); juce::MidiBuffer midi;
        double error = 0.0, wetDryDifference = 0.0; bool latencyStable = true;
        for (int block = 0; block < blocks; ++block) {
            const bool bypassed = block >= 50 && block < 75;
            if (!host) set(actual, "bypass", bypassed ? 1.0f : 0.0f);
            blend.setTargetValue(bypassed ? 1.0f : 0.0f);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n) {
                a.setSample(c, n, input[c][block * 128 + n]); b.setSample(c, n, input[c][block * 128 + n]);
            }
            wet.processBlock(a, midi);
            if (host && bypassed) actual.processBlockBypassed(b, midi); else actual.processBlock(b, midi);
            for (int n = 0; n < 128; ++n) {
                const float mix = blend.getNextValue();
                for (int c = 0; c < 2; ++c) {
                    const int index = block * 128 + n - delay;
                    const float dry = index < 0 ? 0.0f : input[c][index];
                    wetDryDifference = std::max(wetDryDifference, std::abs(static_cast<double>(a.getSample(c, n) - dry)));
                    const float expected = mix >= 1.0f ? dry : a.getSample(c, n) + mix * (dry - a.getSample(c, n));
                    error = std::max(error, std::abs(static_cast<double>(expected - b.getSample(c, n))));
                }
            }
            latencyStable = latencyStable && actual.getLatencySamples() == delay;
        }
        check(error < 2.0e-6 && latencyStable && wetDryDifference > 1.0e-4, std::string(host ? "host" : "native") + " bypass transitions match nontrivial aligned 5-ms wet/dry fade");
    }
}

void ui(GillKind kind) {
    GillEffectProcessor p(kind);product=p.getName().toStdString();prepare(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());check(editor!=nullptr,"native editor constructs");if(!editor)return;editor->setVisible(true);
    Components components;components.collect(*editor);
    const int width=kind==GillKind::Air?440:kind==GillKind::Space?660:680;
    const int height=kind==GillKind::Air?300:kind==GillKind::Echo?440:kind==GillKind::Balance?420:410;
    const int sliders=kind==GillKind::Air?4:kind==GillKind::Balance?1:7;
    const auto* limits=editor->getConstrainer();
    check(editor->getWidth()==width&&editor->getHeight()==height&&limits&&limits->getMinimumWidth()==width&&limits->getMaximumWidth()==2*width&&std::abs(limits->getFixedAspectRatio()-static_cast<double>(width)/height)<1e-9&&components.sliders.size()==static_cast<size_t>(sliders),"real control count, intended dimensions and fixed-aspect 2x resizing");
    HostListener listener;p.addListener(&listener);
    for(auto* slider:components.sliders) {
        juce::String id=slider->getName().toLowerCase().removeCharacters(" ");if(id=="tempo")id="bpm";
        auto* parameter=p.apvts.getParameter(id);check(parameter!=nullptr,"visible "+slider->getName().toStdString()+" binds a real parameter");if(!parameter)continue;
        if(kind==GillKind::Echo){set(p,"sync",id=="time"?0.f:1.f);silence(p,128);check(waitForVisualState([&]{return slider->isEnabled();}),slider->getName().toStdString()+" is interactable before sending user events");}
        const auto& range=parameter->getNormalisableRange();check(slider->getMinimum()==range.start&&slider->getMaximum()==range.end,slider->getName().toStdString()+" range matches DSP");
        set(p,id,parameter->convertFrom0to1(.4f));const float before=p.value(id);const int begins=listener.begins,ends=listener.ends;
        const bool handled=slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));++uiEdits;
        check(handled&&p.value(id)>before&&listener.begins==begins+1&&listener.ends==ends+1,slider->getName().toStdString()+" actual keyboard edit updates DSP in one balanced host gesture");
        const int hostBegins=listener.begins,hostChanges=listener.changes;const float hostValue=range.snapToLegalValue(parameter->convertFrom0to1(.62f));set(p,id,hostValue);
        check(std::abs(slider->getValue()-hostValue)<.002&&listener.begins==hostBegins&&listener.changes==hostChanges+1,slider->getName().toStdString()+" automation updates UI without echo gesture");
        juce::Label* text=nullptr;for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))text=label;
        if(text){text->setText(juce::String(range.start),juce::sendNotificationSync);++uiEdits;}
        check(text&&p.value(id)==range.start,slider->getName().toStdString()+" actual numeric edit updates DSP");
        set(p,id,range.end);slider->mouseDoubleClick(mouse(*slider,{30,30},2));++uiEdits;
        if(std::abs(p.value(id)-parameter->convertFrom0to1(parameter->getDefaultValue()))>=.002)std::cout<<std::setprecision(12)<<"DIAG RESET "<<product<<" id="<<id<<" actual="<<p.value(id)<<" expected="<<parameter->convertFrom0to1(parameter->getDefaultValue())<<" slider="<<slider->getValue()<<" sliderDefault="<<slider->getDoubleClickReturnValue()<<" interval="<<range.interval<<" enabled="<<slider->isEnabled()<<" sync="<<p.value("sync")<<'\n';
        check(std::abs(p.value(id)-parameter->convertFrom0to1(parameter->getDefaultValue()))<.002,slider->getName().toStdString()+" double-click returns factory default");
        set(p,id,range.end);slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));++uiEdits;const float endpoint=range.snapToLegalValue(range.end);if(p.value(id)!=endpoint||listener.begins!=listener.ends)std::cout<<std::setprecision(12)<<"DIAG ENDPOINT "<<product<<" id="<<id<<" actual="<<p.value(id)<<" legalEndpoint="<<endpoint<<" slider="<<slider->getValue()<<" begins="<<listener.begins<<" ends="<<listener.ends<<'\n';check(p.value(id)==endpoint&&listener.begins==listener.ends,slider->getName().toStdString()+" endpoint key is bounded with balanced gesture");
    }
    if(kind==GillKind::Space||kind==GillKind::Echo) {
        const char* spaceNames[]{"ROOM","HALL","PLATE"};const char* echoNames[]{"CLEAN","TAPE","PINGPONG"};bool correct=true;
        for(int style=0;style<3;++style){auto* button=components.button(kind==GillKind::Space?spaceNames[style]:echoNames[style]);if(button)click(*button);correct=correct&&waitForVisualState([&]{return button&&p.value("style")==style&&button->getToggleState();});}
        check(correct,"all three actual style buttons change DSP and selected state");
        auto* preset=components.combo("PRESET");auto* lock=components.button("MIX LOCK");if(lock)click(*lock);set(p,"mix",37);bool selected=preset&&lock&&p.value("mixlock")==1;
        if(preset)for(int i=1;i<=12;++i){preset->setSelectedId(i,juce::sendNotificationSync);++uiEdits;selected=selected&&p.getCurrentProgram()==i-1&&p.presetMatches()&&p.value("mix")==37;}
        check(selected,"actual preset menu selects all twelve programs while MIX LOCK preserves mix");
        auto* previous=components.button("PREVIOUS PRESET");auto* next=components.button("NEXT PRESET");if(next)click(*next);bool wrap=next&&p.getCurrentProgram()==0;if(previous)click(*previous);wrap=wrap&&previous&&p.getCurrentProgram()==11;check(wrap,"actual preset arrow buttons wrap both directions");
        set(p,kind==GillKind::Space?"tone":"color",13.7f);waitForVisualState([&]{return preset&&preset->getSelectedId()==0;});check(preset&&preset->getSelectedId()==0&&preset->getTextWhenNothingSelected()=="CUSTOM","edited preset is visibly marked CUSTOM");
    }
    if(kind==GillKind::Echo) {
        auto* sync=components.button("SYNC");auto* division=components.combo("DIVISION");set(p,"sync",0);if(sync)click(*sync);bool correct=sync&&division&&p.value("sync")==1;
        if(division)for(int i=1;i<=8;++i){division->setSelectedId(i,juce::sendNotificationSync);++uiEdits;correct=correct&&p.value("division")==i-1;}
        check(correct,"actual SYNC and all eight division selections change processor values");
        juce::Slider* time=nullptr;juce::Slider* bpm=nullptr;for(auto* s:components.sliders){if(s->getName()=="TIME")time=s;if(s->getName()=="TEMPO")bpm=s;}
        set(p,"sync",1);bool enabled=waitForVisualState([&]{return time&&bpm&&!time->isEnabled()&&bpm->isEnabled()&&division&&division->isEnabled();});
        TempoHead head;p.setPlayHead(&head);silence(p,128);enabled=waitForVisualState([&]{return bpm&&!bpm->isEnabled();})&&enabled;set(p,"sync",0);enabled=waitForVisualState([&]{return time&&time->isEnabled()&&division&&!division->isEnabled();})&&enabled;p.setPlayHead(nullptr);
        check(enabled,"synced/free/host-tempo UI enables only relevant time controls");
    }
    if(kind==GillKind::Balance) {
        auto* target=components.combo("TARGET");bool correct=target&&target->getNumItems()==5;if(target)for(int i=1;i<=5;++i){target->setSelectedId(i,juce::sendNotificationSync);++uiEdits;correct=correct&&p.value("target")==i-1;}check(correct,"all five actual tonal targets change real DSP choice");
        auto* learn=components.button("LEARN");if(learn)click(*learn);silence(p,128);check(waitForVisualState([&]{return learn&&p.learnState==1&&learn->getButtonText()=="CANCEL";}),"actual LEARN click begins analysis and exposes CANCEL");
        if(learn)click(*learn);silence(p,128);check(waitForVisualState([&]{return learn&&p.learnState==0&&learn->getButtonText()=="LEARN";}),"actual CANCEL click stops incomplete learning");
        if(learn)click(*learn);feedVocal(p,10.1);tick();check(p.savedProfile().valid&&p.learnState==2,"actual LEARN button reaches READY from audio");set(p,"amount",60);set(p,"target",2);
    }
    auto* bypass=components.button("BYPASS");if(bypass)click(*bypass);check(bypass&&p.value("bypass")==1,"actual BYPASS click changes native host parameter");if(bypass)click(*bypass);
    check(listener.begins==listener.ends&&listener.balancedOrder&&listener.begins>0,"all actual UI gestures are balanced and never nested");p.removeListener(&listener);
    if(kind==GillKind::Space||kind==GillKind::Echo){set(p,"mixlock",0);p.setCurrentProgram(kind==GillKind::Space?5:1);}
    if(kind==GillKind::Air){set(p,"midair",25);set(p,"highair",30);set(p,"mix",100);set(p,"output",0);}
    if(kind==GillKind::Echo)set(p,"bpm",120);
    set(p,"bypass",0);feedVocal(p,3.2,true);tick();check(p.inputPeak>0&&p.outputPeak>0,"native screenshot meters are based on actual processed synthetic audio");
    bool contained=true;for(int scale:{1,2}){screenshot(*editor,width*scale,height*scale,p.getName());for(auto* component:components.all)if(component->getParentComponent()==editor.get())contained=contained&&editor->getLocalBounds().contains(component->getBounds());}
    check(contained,"all direct controls remain inside default and doubled editor bounds");editor.reset();p.releaseResources();
}
} // namespace

int main() {
    const auto start=std::chrono::steady_clock::now();juce::ScopedJuceInitialiser_GUI gui;
    // Update06: exercise real LIVE/PRO host state, audio timing and UI.
    gill::testing::qualityRoutes([]{return std::make_unique<GillEffectProcessor>(GillKind::Air);},[](bool ok,const std::string& why){check(ok,why);});
    gill::testing::qualityRoutes([]{return std::make_unique<GillEffectProcessor>(GillKind::Space);},[](bool ok,const std::string& why){check(ok,why);});
    gill::testing::qualityRoutes([]{return std::make_unique<GillEffectProcessor>(GillKind::Echo);},[](bool ok,const std::string& why){check(ok,why);});
    gill::testing::qualityRoutes([]{return std::make_unique<GillEffectProcessor>(GillKind::Balance);},[](bool ok,const std::string& why){check(ok,why);});

    for(auto kind:{GillKind::Air,GillKind::Space,GillKind::Echo,GillKind::Balance}){metadataAndState(kind);dryRoutes(kind);bypassTransitions(kind);automation(kind);}
    programs(GillKind::Space);programs(GillKind::Echo);learning();tempo();dryMigration();
    for(auto kind:{GillKind::Air,GillKind::Space,GillKind::Echo,GillKind::Balance})ui(kind);
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::ofstream report("effects-plugin-integration-report.json");
    report<<"{\n\"passed\":"<<(failures?"false":"true")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_configurations\":"<<audioConfigurations<<",\"ui_edits\":"<<uiEdits<<",\"native_screenshots\":"<<screenshots<<",\"elapsed_seconds\":"<<elapsed<<",\"failed_cases\":[";
    for(size_t i=0;i<failedCases.size();++i)report<<(i?",":"")<<'"'<<failedCases[i]<<'"';report<<"]\n}\n";
    std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures, "<<audioConfigurations<<" audio configurations, "<<uiEdits<<" UI edits, "<<screenshots<<" native screenshots\n";return failures?1:0;
}


