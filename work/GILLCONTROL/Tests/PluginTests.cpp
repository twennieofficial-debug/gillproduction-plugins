#include "PluginProcessor.h"
#include "../../GILLCommon/QualityUi.h"
#include <iostream>
#include <thread>

#if defined(__APPLE__)
extern "C" void gillInitialiseMacTestApplication();
#endif
namespace {
int passed = 0, failed = 0;
void check(bool ok, const char* text) { if (ok) ++passed; else { ++failed; std::cerr << "FAIL " << text << '\n'; } }
class Receiver final : public juce::AudioProcessor {
public:
    Receiver() : AudioProcessor(BusesProperties().withInput("IN", juce::AudioChannelSet::stereo(), true).withOutput("OUT", juce::AudioChannelSet::stereo(), true)),
                 state(*this, nullptr, "RECEIVER", layout()), quality(*this, state) {}
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() { juce::AudioProcessorValueTreeState::ParameterLayout p; p.add(gill::qualityParameter()); return p; }
    void prepareToPlay(double, int) override {} void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "QUALITY RECEIVER TEST"; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& block) override { if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, block); }
    void setStateInformation(const void* data, int size) override { if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName("RECEIVER")) state.replaceState(juce::ValueTree::fromXml(*xml)); }
    juce::AudioProcessorValueTreeState state;
    gill::QualityClient quality;
};
class HostListener final : public juce::AudioProcessorParameter::Listener {
public:
    void parameterValueChanged(int, float) override { ++values; }
    void parameterGestureChanged(int, bool starting) override {
        if (starting) ++begins; else ++ends;
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) ++wrongThread;
    }
    std::atomic<int> values{0}, begins{0}, ends{0}, wrongThread{0};
};
template <typename Sample> void passthrough(GillControlProcessor& processor) {
    for (const int channels : {1, 2}) for (const int count : {1, 16, 127, 512}) {
        juce::AudioBuffer<Sample> buffer(channels, count), reference(channels, count);
        for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i) buffer.setSample(c, i, static_cast<Sample>(std::sin(i * .83 + c) * 2));
        buffer.setSample(0, 0, -Sample(0)); reference.makeCopyOf(buffer); juce::MidiBuffer midi;
        for (int repeat = 0; repeat < 128; ++repeat) {
            processor.processBlock(buffer, midi);
            for (int c = 0; c < channels; ++c) check(std::memcmp(buffer.getReadPointer(c), reference.getReadPointer(c), sizeof(Sample) * static_cast<std::size_t>(count)) == 0, "dry samples bit-identical");
        }
        processor.processBlockBypassed(buffer, midi);
        for (int c = 0; c < channels; ++c) check(std::memcmp(buffer.getReadPointer(c), reference.getReadPointer(c), sizeof(Sample) * static_cast<std::size_t>(count)) == 0, "bypassed samples bit-identical");
    }
}
}
int main() {
    std::cout << std::unitbuf;
#if defined(__APPLE__)
    gillInitialiseMacTestApplication();
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    {
        Receiver first, second;
        GillControlProcessor controller;
        HostListener listener; first.state.getParameter(gill::qualityParameterId)->addListener(&listener);
        first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread(); controller.serviceControl();
        check(first.quality.mode() == 1 && second.quality.isPro(), "PRO default");
        check(controller.quality.registeredInstances() == 2, "controller sees exact receiver count");
        int callbacks = 0; bool callbackThread = true;
        first.quality.onModeChanged = [&](int) { ++callbacks; callbackThread = callbackThread && juce::MessageManager::getInstance()->isThisTheMessageThread(); };
        check(controller.selectGlobal(0), "controller GLOBAL LIVE action succeeds");
        first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread();
        check(first.quality.mode() == 0 && second.quality.mode() == 0, "global LIVE updates actual APVTS parameters");
        check(listener.begins == 1 && listener.ends == 1 && listener.wrongThread == 0 && callbacks == 1 && callbackThread, "global change notifies host with MessageThread gestures and callback");
        controller.selectGlobal(0); first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread();
        check(listener.begins == 2 && listener.ends == 2, "same-mode click still issues fresh host gesture");
        std::thread automation([&] { first.state.getParameter(gill::qualityParameterId)->setValueNotifyingHost(1.f); }); automation.join();
        check(first.quality.mode() == 1, "audio mode reads host automation immediately, without a timer");
        second.quality.pollOnMessageThread(); check(second.quality.mode() == 0, "local automation never broadcasts globally");
        first.quality.pollOnMessageThread(); check(callbacks == 2 && callbackThread, "automation callback deferred to MessageThread");
        std::thread latency([&] { first.quality.requestLatencySamples(137); }); latency.join();
        check(first.getLatencySamples() == 0, "audio request never calls host latency API");
        first.quality.pollOnMessageThread(); check(first.getLatencySamples() == 137, "MessageThread applies pending host latency");
        first.quality.requestLatencySamples(0); first.quality.pollOnMessageThread(); check(first.getLatencySamples() == 0, "latency returns to zero");
        juce::MemoryBlock receiverState; first.getStateInformation(receiverState);
        controller.selectGlobal(0); first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread();
        first.setStateInformation(receiverState.getData(), static_cast<int>(receiverState.getSize()));
        check(first.quality.mode() == 1, "per-instance state recalls mode immediately");
        second.quality.pollOnMessageThread(); check(second.quality.mode() == 0, "per-instance state does not change other plugins");
        controller.selectGlobal(1); first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread();
        { Receiver newlyInserted; newlyInserted.quality.pollOnMessageThread(); check(newlyInserted.quality.mode() == 1, "new instance follows active master"); }
        juce::MemoryBlock masterState; controller.getStateInformation(masterState);
        controller.selectGlobal(0); first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread();
        controller.setStateInformation(masterState.getData(), static_cast<int>(masterState.getSize())); controller.serviceControl();
        check(first.quality.mode() == 0, "controller recall waits for registration quiet period");
        juce::Thread::sleep(550); controller.serviceControl(); first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread();
        check(first.quality.mode() == 1 && second.quality.mode() == 1, "controller recalled mode synchronizes after quiet period");
        controller.apvts.getParameter(gill::qualityParameterId)->setValueNotifyingHost(0.f); controller.serviceControl();
        first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread(); check(first.quality.mode() == 0 && second.quality.mode() == 0, "controller automation propagates globally");
        { GillControlProcessor otherController; otherController.serviceControl(); otherController.selectGlobal(1); controller.serviceControl(); first.quality.pollOnMessageThread(); second.quality.pollOnMessageThread(); check(first.quality.mode() == 1 && second.quality.mode() == 1, "multiple controllers follow last action without rebroadcast feedback"); }
        first.state.getParameter(gill::qualityParameterId)->removeListener(&listener);
        for (const auto rate : {44100., 48000., 96000., 192000.}) {
            controller.prepareToPlay(rate, 512); check(controller.getLatencySamples() == 0, "control always has zero latency");
            passthrough<float>(controller); passthrough<double>(controller);
        }
        { gill::QualitySelector selector(first.state, first); check(selector.getWidth() == 110 && selector.getHeight() == 26, "compact selector size"); }
        std::unique_ptr<juce::AudioProcessorEditor> editor(controller.createEditor());
        check(editor && editor->getWidth() == 420 && editor->getHeight() == 220, "compact controller dimensions");
        auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile("GILLCONTROL-UI-Compact.png"); file.deleteFile();
        if (auto output = file.createOutputStream()) { juce::PNGImageFormat png; check(png.writeImageToStream(snapshot, *output), "real editor screenshot saved"); } else check(false, "screenshot path writable");
    }
    std::cout << passed << " checks passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
