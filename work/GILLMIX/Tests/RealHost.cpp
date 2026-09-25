// A real host: product processors are loaded only through independent VST3
// bundles. This executable links no MIX processor, DSP or IPC implementation.
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <iostream>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
extern "C" void gillInitialiseMacMixHost();
extern "C" bool gillPressMacMixHost(void*, const char*);
#endif
namespace {
struct SavedState {
    std::unique_ptr<juce::XmlElement> wrapper;
    juce::MemoryBlock component;
    juce::ValueTree tree;
    size_t xmlBytes = 0;
    explicit SavedState(juce::AudioProcessor& processor) {
        juce::MemoryBlock block; processor.getStateInformation(block);
        wrapper = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
        if (!wrapper) return;
        auto* child = wrapper->getChildByName("IComponent");
        if (!child || !component.fromBase64Encoding(child->getAllSubText()) || component.getSize() < 9) return;
        auto* bytes = static_cast<const uint8_t*>(component.getData());
        if (juce::ByteOrder::littleEndianInt(bytes) != 0x21324356u) return;
        xmlBytes = static_cast<size_t>(juce::ByteOrder::littleEndianInt(bytes + 4)) + 9;
        if (xmlBytes > component.getSize()) return;
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(bytes, static_cast<int>(xmlBytes))) tree = juce::ValueTree::fromXml(*xml);
    }
    bool restore(juce::AudioProcessor& processor) {
        if (!tree.isValid() || !wrapper || !xmlBytes) return false;
        auto xml = tree.createXml(); if (!xml) return false;
        juce::MemoryBlock next; juce::AudioProcessor::copyXmlToBinary(*xml, next);
        // Preserve the actual JUCE VST3 private-state trailer verbatim. Its
        // length is relative to the end, independent of processor XML length.
        next.append(static_cast<const uint8_t*>(component.getData()) + xmlBytes, component.getSize() - xmlBytes);
        auto* child = wrapper->getChildByName("IComponent"); child->deleteAllChildElements(); child->addTextElement(next.toBase64Encoding());
        juce::MemoryBlock block; juce::AudioProcessor::copyXmlToBinary(*wrapper, block);
        processor.setStateInformation(block.getData(), static_cast<int>(block.getSize())); return true;
    }
};
struct PlayHead : juce::AudioPlayHead {
    int64_t position = 0; double rate = 48000;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setTimeInSamples(position); p.setTimeInSeconds(position / rate); p.setIsPlaying(true); return p; }
};
struct Item {
    juce::String path; juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    juce::AudioProcessorParameter* gain = nullptr;
    juce::AudioProcessorParameter* quality = nullptr;
    juce::AudioBuffer<float> buffer{2, 256}; juce::MidiBuffer midi;
};
class Host final : public juce::Timer {
public:
    explicit Host(double rate) : sampleRate(rate) { play.rate = rate; }
    ~Host() override { stopTimer(); editor.reset(); for (auto& item : items) if (item.processor) item.processor->releaseResources(); }
    void check(bool good, const juce::String& text) { ++checks; if (!good) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
    bool load(const juce::String& masterPath, const juce::String& linkPath) {
        for (int i = 0; i < 4; ++i) {
            auto& item = items[static_cast<size_t>(i)]; item.path = i ? linkPath : masterPath;
            juce::OwnedArray<juce::PluginDescription> descriptions; format.findAllTypesForFile(descriptions, item.path);
            check(descriptions.size() == 1, "one native VST3 factory"); if (descriptions.size() != 1) return false;
            item.description = *descriptions[0];
            check(item.description.name == (i ? "GILLLINK" : "GILLMIX"), "expected factory identity");
            check(item.description.version == "0.7.0", "actual factory version 0.7.0");
            juce::String error; item.processor = format.createInstanceFromDescription(item.description, sampleRate, 256, error);
            check(item.processor != nullptr, "native VST3 instance: " + error); if (!item.processor) return false;
            for (auto* p : item.processor->getParameters()) { if (p->getName(128) == "TRACK LEVEL") item.gain = p; if (p->getName(128) == "QUALITY") item.quality = p; }
            check(item.quality && (i == 0 || item.gain), "real host parameters exposed");
            if (!item.quality || (i && !item.gain)) return false;
            item.processor->setPlayConfigDetails(2, 2, sampleRate, 256); item.processor->prepareToPlay(sampleRate, 256); item.processor->setPlayHead(&play);
            check(item.processor->getLatencySamples() == 0, "zero reported PDC");
        }
        // A normal project state assigns deliberate roles and persisted selected
        // UUIDs. This does not establish runtime bindings: CONNECT remains an
        // actual native editor action, and LEARN/APPLY/UNDO use the same buttons.
        const char* names[]{"MAIN", "BEAT", "DOUBLE"}; const int roles[]{1, 4, 2};
        SavedState master(*items[0].processor); check(master.tree.isValid(), "native controller state readable"); if (!master.tree.isValid()) return false;
        for (int i = 1; i < 4; ++i) {
            SavedState link(*items[static_cast<size_t>(i)].processor); check(link.tree.isValid(), "native LINK state readable"); if (!link.tree.isValid()) return false;
            link.tree.setProperty("name", names[i - 1], nullptr); link.tree.setProperty("role", roles[i - 1], nullptr); link.tree.setProperty("manual", true, nullptr);
            juce::ValueTree pick("PICK"); pick.setProperty("id", link.tree["uuid"], nullptr); pick.setProperty("role", roles[i - 1], nullptr); pick.setProperty("manual", true, nullptr); pick.setProperty("selected", true, nullptr); pick.setProperty("locked", false, nullptr); master.tree.addChild(pick, -1, nullptr);
            check(link.restore(*items[static_cast<size_t>(i)].processor), "LINK project state restored through VST3 host");
        }
        master.tree.setProperty("program", 1, nullptr); check(master.restore(*items[0].processor), "controller selected UUIDs recalled without runtime connection");
        editor.reset(items[0].processor->createEditor()); check(editor != nullptr, "real hosted controller editor constructed"); if (!editor) return false;
        editor->setTopLeftPosition(-10000, -10000); editor->addToDesktop(0); editor->setVisible(true);
        check(editor->getWidth() == 760 && editor->getHeight() == 480, "real hosted compact controller dimensions"); return true;
    }
    void press(const char* label, int x, int y) {
        bool accepted = false; std::cout << "UI " << label << '\n';
        if (editor && editor->getPeer()) {
#if defined(_WIN32)
            auto window = static_cast<HWND>(editor->getPeer()->getNativeHandle()); RECT r{}; GetClientRect(window, &r);
            POINT p{LONG(x * (r.right - r.left) / 760), LONG(y * (r.bottom - r.top) / 480)};
            for (int depth = 0; depth < 16; ++depth) { auto child = ChildWindowFromPointEx(window, p, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT); if (!child || child == window) break; MapWindowPoints(window, child, &p, 1); window = child; }
            const auto point = MAKELPARAM(p.x, p.y); SendMessageW(window, WM_MOUSEMOVE, 0, point); SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point); SendMessageW(window, WM_LBUTTONUP, 0, point); accepted = true;
#elif defined(__APPLE__)
            accepted = gillPressMacMixHost(editor->getPeer()->getNativeHandle(), label);
#endif
        }
        check(accepted, "native editor action delivered: " + juce::String(label));
    }
    void verifyGain(int i, float expected) {
        auto& item = items[static_cast<size_t>(i)]; SavedState state(*item.processor);
        const auto param = state.tree.getChildWithProperty("id", "trackLevel");
        check(param.isValid() && std::abs(static_cast<float>(param["value"]) - expected) < .011f, "actual LINK processor gain matches " + juce::String(expected));
        check(std::abs(item.gain->getValue() * 30.f - 24.f - expected) < .011f, "host gain cache agrees with actual LINK processor");
    }
    void render(int blocks, bool measure = false) {
        bool finite = true, dry = true, exact = true;
        std::thread audio([&] {
            for (int b = 0; b < blocks; ++b) {
                for (int i = 0; i < 4; ++i) {
                    auto& item = items[static_cast<size_t>(i)]; const auto expected = std::pow(10.f, (i ? item.gain->getValue() * 30.f - 24.f : 0.f) * .05f);
                    float original[2][256]{};
                    for (int c = 0; c < 2; ++c) for (int n = 0; n < 256; ++n) original[c][n] = item.buffer.getWritePointer(c)[n] = (i == 2 ? .1f : .05f) * static_cast<float>(std::sin((play.position + n) * 2 * juce::MathConstants<double>::pi * 220 / sampleRate));
                    item.processor->processBlock(item.buffer, item.midi);
                    for (int c = 0; c < 2; ++c) for (int n = 0; n < 256; ++n) { const auto out = item.buffer.getSample(c, n); finite &= std::isfinite(out); if (!i) dry &= out == original[c][n]; if (measure) exact &= std::abs(out - original[c][n] * expected) < 2e-6f; }
                }
                play.position += 256;
            }
        }); audio.join();
        check(finite && dry, "real foreign DLL audio is finite; MIX is exact dry"); if (measure) check(exact, "actual zero-latency settled gain agrees sample for sample");
    }
    void timerCallback() override {
        ++ticks;
        if (stage == 2) { render(static_cast<int>(std::ceil(sampleRate * .10 / 256))); if (play.position < static_cast<int64_t>(sampleRate * 10.5)) return; press("LEARN / STOP", 501, 452); stage = 3; ticks = 0; return; }
        if (ticks < 20) return; ticks = 0;
        std::cout << "STAGE " << stage << '\n';
        switch (stage) {
            case 0: for (int i = 1; i < 4; ++i) verifyGain(i, 0); press("CONNECT SELECTED", 650, 80); stage = 1; break;
            case 1: {
                SavedState master(*items[0].processor);
                for (int i = 1; i < 4; ++i) { SavedState link(*items[static_cast<size_t>(i)].processor); check(link.tree["controller"] == master.tree["uuid"], "CONNECT button bound this real LINK to this controller"); }
                press("LEARN / STOP", 501, 452); stage = 2; break;
            }
            case 3: press("APPLY", 598, 452); stage = 4; break;
            case 4: verifyGain(1, 0); verifyGain(2, -3); verifyGain(3, -3); render(static_cast<int>(sampleRate / 256 * .1)); render(4, true); items[2].processor->getStateInformation(appliedState); press("UNDO", 694, 452); stage = 5; break;
            case 5: for (int i = 1; i < 4; ++i) verifyGain(i, 0); for (auto& item : items) item.quality->setValueNotifyingHost(0); render(24); stage = 6; break;
            case 6: for (auto& item : items) check(item.processor->getLatencySamples() == 0 && item.quality->getValue() == 0, "LIVE host automation preserves zero PDC"); render(4, true); for (auto& item : items) item.quality->setValueNotifyingHost(1); render(24); stage = 7; break;
            case 7: for (auto& item : items) check(item.processor->getLatencySamples() == 0 && item.quality->getValue() == 1, "PRO host automation preserves zero PDC"); items[2].processor->setStateInformation(appliedState.getData(), static_cast<int>(appliedState.getSize())); items[2].processor->releaseResources(); items[2].processor->prepareToPlay(sampleRate, 256); verifyGain(2, -3); render(1, true); editor.reset(); finish(); break;
            default: check(false, "unexpected host test stage"); finish(); break;
        }
    }
    void finish() { stopTimer(); finished = true; juce::MessageManager::getInstance()->stopDispatchLoop(); }
    void report(const juce::File& file) {
        auto* object = new juce::DynamicObject; object->setProperty("passed", failures == 0 && finished); object->setProperty("checks", checks); object->setProperty("failures", failures); object->setProperty("sample_rate", sampleRate); object->setProperty("native_instances", 4);
        juce::Array<juce::var> products; for (const auto& item : items) { auto* p = new juce::DynamicObject; p->setProperty("name", item.description.name); p->setProperty("version", item.description.version); p->setProperty("factory_uid", juce::String::toHexString(item.description.uniqueId)); p->setProperty("bundle", item.path); products.add(juce::var(p)); } object->setProperty("products", products);
        check(file.replaceWithText(juce::JSON::toString(juce::var(object), true)), "host report saved"); std::cout << "RESULT " << checks << " checks, " << failures << " failures\n";
    }
    int checks = 0, failures = 0; std::atomic<bool> finished{false};
private:
    double sampleRate; PlayHead play; juce::VST3PluginFormat format; std::array<Item, 4> items; std::unique_ptr<juce::AudioProcessorEditor> editor; juce::MemoryBlock appliedState; int ticks = 0, stage = 0;
};
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    if (argc < 5) { std::cerr << "Usage: GillMixRealHost MIX.vst3 LINK.vst3 sampleRate report.json\n"; return 2; }
    const double rate = juce::String(argv[3]).getDoubleValue(); if (rate < 44100 || rate > 192000) return 2;
#if defined(__APPLE__)
    gillInitialiseMacMixHost();
#endif
    juce::ScopedJuceInitialiser_GUI gui; Host host(rate); const juce::File report{juce::String(argv[4])};
    if (!host.load(juce::String(argv[1]), juce::String(argv[2]))) { host.report(report); return 1; }
    std::thread watchdog([&] { for (int i = 0; i < 1200 && !host.finished; ++i) juce::Thread::sleep(100); if (!host.finished) juce::MessageManager::callAsync([&] { host.check(false, "native test exceeded 120 seconds"); host.finish(); }); });
    host.startTimer(60); juce::MessageManager::getInstance()->runDispatchLoop(); host.finished = true; watchdog.join(); host.report(report); return host.failures ? 1 : 0;
}
