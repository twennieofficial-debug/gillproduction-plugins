// Real VST3 integration: every processor comes from its independently loaded
// bundle. No product processor classes or QualityBus internals are linked here.
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <optional>
#include <set>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__APPLE__)
extern "C" void gillInitialiseMacQualityHost();
extern "C" bool gillPressMacQualityHost(void*, const char*);
#endif
namespace {
juce::String expectedVersion(const juce::String& name) {
    struct Version { const char* name; const char* version; };
    static constexpr Version versions[] {
    {"GILLEQ", "0.6.0"},
    {"GILL-DE-ESSER", "0.6.0"},
    {"GILLDEREVERB", "0.6.0"},
    {"GILLDECLICK", "0.6.0"},
    {"GILLDECRACKLE", "0.6.0"},
    {"GILLTUNE", "0.6.0"},
    {"GILLTUNE LIVE", "0.6.0"},
    {"GILLHEAT", "0.6.0"},
    {"GILLFLOW", "0.6.0"},
    {"GILLAIR", "0.6.0"},
    {"GILLSPACE", "0.6.0"},
    {"GILLECHO", "0.6.0"},
    {"GILLBALANCE", "0.6.0"},
    {"GILLSILK", "0.6.0"},
    {"GILLSPARK", "0.6.0"},
    {"GILLSTRIP", "0.6.0"},
    {"GILLGOLD", "0.6.0"},
    {"GILLDIVE", "0.6.0"},
    {"GILLVOX", "0.8.0"},
    {"GILLOPTA", "0.8.0"},
    {"GILLBUSS", "0.8.0"},
    {"GILLQUAD", "0.8.0"},
    {"GILLSTAGE", "0.8.0"},
    {"GILLRIDE", "0.8.0"},
    {"GILLCLEAN", "0.8.0"},
    {"GILLPOCKET", "0.8.0"},
    {"GILLALIGN", "0.8.0"},
    {"GILLFORM", "0.8.0"},
    {"GILLFINISH", "0.8.0"},
    {"GILLCONTROL", "0.8.0"},
    {"GILLPHRASE", "0.6.0"},
    {"GILLDIRECTOR", "0.6.0"},
    {"GILLREPLY", "0.6.0"},
    {"GILLSMARTDEESSER", "0.6.0"},
    {"GILLMIX", "0.7.0"},
    {"GILLLINK", "0.7.0"},
    {"GILLHARMONY", "0.7.0"},
    {"GILLREFERENCE", "0.7.0"},
    {"GILLRESCUE", "0.7.0"},
    };
    for (const auto& item : versions) if (name == item.name) return item.version;
    return {};
}
void phase(const juce::String& text) { std::cout << "PHASE " << juce::Time::getMillisecondCounterHiRes() << " " << text << '\n'; }
struct Item {
    juce::String path, name;
    juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    juce::AudioProcessorParameter* quality = nullptr;
    juce::AudioBuffer<float> buffer{2, 128};
    juce::MidiBuffer midi;
    int liveLatency = -1, proLatency = -1;
};
class Host final : public juce::Timer {
public:
    explicit Host(double rate) : sampleRate(rate) {}
    void check(bool good, const juce::String& message) {
        ++checks; if (!good) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    }
    bool create(Item& item) {
        phase("factory begin " + item.name);
        juce::String error;
        item.processor = format.createInstanceFromDescription(item.description, sampleRate, 128, error);
        check(item.processor != nullptr, item.name + " native factory: " + error);
        if (!item.processor) return false;
        item.quality = nullptr;
        for (auto* parameter : item.processor->getParameters()) if (parameter->getName(128) == "QUALITY") {
            check(item.quality == nullptr, item.name + " unique QUALITY parameter"); item.quality = parameter;
        }
        check(item.quality && item.quality->getNumSteps() == 2, item.name + " exposes two quality modes");
        item.processor->setPlayConfigDetails(2, 2, sampleRate, 128); item.processor->prepareToPlay(sampleRate, 128);
        phase("factory prepared " + item.name);
        return item.quality != nullptr;
    }
    bool load(const juce::StringArray& paths) {
        for (const auto& path : paths) {
            phase("scan begin " + juce::File(path).getFileName());
            auto item = std::make_unique<Item>(); item->path = path;
            juce::OwnedArray<juce::PluginDescription> descriptions; format.findAllTypesForFile(descriptions, path);
            check(descriptions.size() == 1, path + " exactly one VST3 factory"); if (descriptions.size() != 1) return false;
            item->description = *descriptions[0]; item->name = item->description.name;
            check(expectedVersion(item->name).isNotEmpty() && item->description.version == expectedVersion(item->name), item->name + " expected native factory version " + expectedVersion(item->name) + " (got " + item->description.version + ")");
            if (!create(*item)) return false;
            if (item->name == "GILLCONTROL") { check(master < 0, "one controller in input list"); master = static_cast<int>(items.size()); }
            items.push_back(std::move(item));
        }
        check(master >= 0 && items.size() >= 3, "controller and at least two real effect bundles loaded");
        for (int i = 0; i < static_cast<int>(items.size()); ++i) if (i != master) { local = i; break; }
        return master >= 0 && local >= 0;
    }
    std::optional<float> savedQuality(Item& item) {
        juce::MemoryBlock state; item.processor->getStateInformation(state);
        if (auto wrapper = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()))) {
            if (auto* component = wrapper->getChildByName("IComponent")) {
                juce::MemoryBlock decoded;
                if (!decoded.fromBase64Encoding(component->getAllSubText())) return std::nullopt;
                state.swapWith(decoded);
            }
        }
        const auto* bytes = static_cast<const std::uint8_t*>(state.getData());
        // The VST3 host wraps component/controller chunks. Locate JUCE's actual
        // processor-state XML block instead of assuming its hosted cache is right.
        for (std::size_t offset = 0; offset + 9 <= state.getSize(); ++offset) {
            if (juce::ByteOrder::littleEndianInt(bytes + offset) != 0x21324356u) continue;
            const auto length = juce::ByteOrder::littleEndianInt(bytes + offset + 4);
            if (length == 0 || length > state.getSize() - offset - 9) continue;
            if (auto xml = juce::AudioProcessor::getXmlFromBinary(bytes + offset, static_cast<int>(length + 9))) {
                auto tree = juce::ValueTree::fromXml(*xml);
                auto parameter = tree.getChildWithProperty("id", "gillQuality");
                if (parameter.isValid()) return static_cast<float>(parameter.getProperty("value").toString().getDoubleValue());
            }
        }
        return std::nullopt;
    }
    void verify(int expected, bool oneLocalException = false) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            auto& item = *items[static_cast<std::size_t>(i)]; const int mode = oneLocalException && i == local ? 1 : expected;
            const auto actual = savedQuality(item);
            check(actual && std::abs(*actual - mode) < .01f, item.name + " actual processor APVTS state matches " + juce::String(mode));
            check(std::abs(item.quality->getValue() - mode) < .01f, item.name + " host QUALITY cache matches processor");
            const auto latency = item.processor->getLatencySamples(); check(latency >= 0 && latency < sampleRate, item.name + " bounded actual host PDC");
            if (item.name == "GILLCONTROL") check(latency == 0, "controller has zero PDC in both modes");
            if (!oneLocalException) {
                if (mode == 0) item.liveLatency = latency; else item.proLatency = latency;
                if (mode == 0) {
                    if (item.name == "GILLTUNE" || item.name == "GILLTUNE LIVE")
                        check(latency == static_cast<int>(std::ceil(sampleRate * .016)), item.name + " LIVE PDC is its 16 ms analysis window");
                    else if (item.name == "GILLFORM")
                        check(latency > 0 && latency < sampleRate * .025, item.name + " LIVE PDC is a positive window below 25 ms");
                    else if (item.name == "GILLHARMONY")
                        check(latency == static_cast<int>(std::ceil(sampleRate * .025)) + 80, item.name + " LIVE PDC matches causal harmony window");
                    else if (item.name == "GILLRESCUE")
                        check(latency == static_cast<int>(std::ceil(sampleRate * .004)), item.name + " LIVE PDC matches 4 ms repair context");
                    else check(latency == 0, item.name + " LIVE PDC is zero");
                }
                if (mode == 1 && item.liveLatency >= 0) check(latency >= item.liveLatency, item.name + " PRO PDC is not below LIVE");
                if (mode == 1) {
                    if (item.name == "GILLHARMONY") check(latency == static_cast<int>(std::ceil(sampleRate * .068)) + 112, item.name + " PRO PDC matches harmony window");
                    if (item.name == "GILLRESCUE") check(latency == static_cast<int>(std::ceil(sampleRate * .012)), item.name + " PRO PDC matches 12 ms repair context");
                    if (item.name == "GILLMIX" || item.name == "GILLLINK" || item.name == "GILLREFERENCE") check(latency == 0, item.name + " PRO PDC is zero");
                }
            }
        }
    }
    void clickGlobal(int mode) {
        phase("controller click begin " + juce::String(mode));
        if (!editor) {
            phase("controller createEditor begin");
            editor.reset(items[static_cast<std::size_t>(master)]->processor->createEditor());
            phase("controller createEditor end");
            if (editor) {
                editor->setTopLeftPosition(-10000, -10000);
                phase("controller addToDesktop begin"); editor->addToDesktop(0); phase("controller addToDesktop end");
                phase("controller setVisible begin"); editor->setVisible(true); phase("controller setVisible end");
            }
        }
        // Hosted plugin components live across the native child-window boundary,
        // so a host cannot dynamic_cast their private JUCE Button tree. Activate
        // the real native button, then verify actual processor state afterwards.
        bool clicked = false;
        if (editor && editor->getPeer()) {
#if defined(_WIN32)
            const double x = mode == 0 ? 114.5 / 420.0 : 305.5 / 420.0, y = 114.0 / 220.0;
            auto window = static_cast<HWND>(editor->getPeer()->getNativeHandle());
            RECT bounds{}; GetClientRect(window, &bounds);
            POINT point{LONG(x * (bounds.right - bounds.left)), LONG(y * (bounds.bottom - bounds.top))};
            for (int depth = 0; depth < 16; ++depth) {
                auto child = ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
                if (!child || child == window) break;
                MapWindowPoints(window, child, &point, 1); window = child;
            }
            const auto coordinates = MAKELPARAM(point.x, point.y);
            SendMessageW(window, WM_MOUSEMOVE, 0, coordinates);
            SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, coordinates);
            SendMessageW(window, WM_LBUTTONUP, 0, coordinates); clicked = true;
#elif defined(__APPLE__)
            phase("controller AppKit accessibility press begin");
            clicked = gillPressMacQualityHost(editor->getPeer()->getNativeHandle(), mode == 0 ? "GLOBAL LIVE" : "GLOBAL PRO");
            phase("controller AppKit accessibility press end");
#endif
        }
#if defined(__APPLE__)
        check(clicked, "native accessibility press accepted by real controller button");
#else
        check(clicked, "native mouse click delivered to real controller editor");
#endif
        phase("controller click end");
    }
    void render() {
        bool finite = true, controlIdentical = true;
        std::thread audio([&] {
            for (auto& item : items) {
                for (int block = 0; block < 4; ++block) {
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i)
                        item->buffer.setSample(c, i, float(.1 * std::sin((sampleClock + block*128 + i) * .027 + c)));
                    float dry[2][128]{};
                    if (item->name == "GILLCONTROL") for (int c = 0; c < 2; ++c) std::memcpy(dry[c], item->buffer.getReadPointer(c), sizeof(dry[c]));
                    item->processor->processBlock(item->buffer, item->midi);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) finite = finite && std::isfinite(item->buffer.getSample(c, i));
                    if (item->name == "GILLCONTROL") for (int c = 0; c < 2; ++c) controlIdentical = controlIdentical && std::memcmp(dry[c], item->buffer.getReadPointer(c), sizeof(dry[c])) == 0;
                }
            }
        });
        audio.join(); sampleClock += 512;
        check(finite, "all real DLLs render finite audio on audio worker"); check(controlIdentical, "real controller DLL is bit-exact dry");
    }
    void timerCallback() override {
        if (ticks == 0) phase("render stage " + juce::String(stage) + " begin");
        render(); if (ticks == 0) phase("render stage " + juce::String(stage) + " end");
        if (++ticks < 20) return; ticks = 0;
        phase("action stage " + juce::String(stage));
        switch (stage++) {
            case 0: items[static_cast<std::size_t>(master)]->quality->setValueNotifyingHost(0); break;
            case 1: verify(0); items[static_cast<std::size_t>(local)]->quality->setValueNotifyingHost(1); break;
            case 2: verify(0, true); clickGlobal(0); break;
            case 3: verify(0); phase("controller editor close begin"); editor.reset(); phase("controller editor close end"); items[static_cast<std::size_t>(master)]->quality->setValueNotifyingHost(1); break;
            case 4: verify(1); items[static_cast<std::size_t>(master)]->processor->getStateInformation(savedMaster); clickGlobal(0); break;
            case 5: {
                verify(0); editor.reset(); auto& item = *items[static_cast<std::size_t>(master)];
                phase("controller recreate begin");
                item.processor->releaseResources(); item.processor.reset();
                if (!create(item)) { finish(); return; }
                item.processor->setStateInformation(savedMaster.getData(), static_cast<int>(savedMaster.getSize())); phase("controller recreate end"); break;
            }
            case 6: verify(1); finish(); break;
            default: finish(); break;
        }
    }
    void finish() { stopTimer(); finished = true; juce::MessageManager::getInstance()->stopDispatchLoop(); }
    void report(const juce::File& destination) {
        juce::Array<juce::var> products;
        for (const auto& item : items) {
            auto* p = new juce::DynamicObject; p->setProperty("name", item->name); p->setProperty("bundle", item->path);
            p->setProperty("factory_version", item->description.version);
            p->setProperty("factory_version_verified", expectedVersion(item->name).isNotEmpty() && item->description.version == expectedVersion(item->name));
            p->setProperty("factory_uid", juce::String::toHexString(item->description.uniqueId));
            p->setProperty("factory_deprecated_uid", juce::String::toHexString(item->description.deprecatedUid));
            p->setProperty("manufacturer", item->description.manufacturerName);
            p->setProperty("live_latency_samples", item->liveLatency); p->setProperty("pro_latency_samples", item->proLatency); products.add(juce::var(p));
        }
        auto* result = new juce::DynamicObject; result->setProperty("passed", failures == 0 && finished);
        result->setProperty("sample_rate", sampleRate);
        result->setProperty("checks", checks); result->setProperty("failures", failures); result->setProperty("actual_vst3_bundles", static_cast<int>(items.size())); result->setProperty("products", products);
        destination.getParentDirectory().createDirectory();
        if (!destination.replaceWithText(juce::JSON::toString(juce::var(result), true))) check(false, "report could not be saved");
        std::cout << "RESULT " << checks << " checks, " << failures << " failures\n";
    }
    ~Host() override { stopTimer(); editor.reset(); for (auto& item : items) if (item->processor) item->processor->releaseResources(); }
    int failures = 0, checks = 0; std::atomic<bool> finished{false};
private:
    juce::VST3PluginFormat format;
    std::vector<std::unique_ptr<Item>> items;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::MemoryBlock savedMaster;
    int master = -1, local = -1, ticks = 0, stage = 0, sampleClock = 0;
    double sampleRate;
};
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    std::cout << "START GillQualityHost main\n";
    if (argc < 2) { std::cerr << "Usage: GillQualityHost --list paths.json [--report report.json] [--sample-rate 48000] OR bundle.vst3 ...\n"; return 2; }
#if defined(__APPLE__)
    phase("NSApplication initialise begin");
    gillInitialiseMacQualityHost();
    phase("NSApplication initialise end");
#endif
    juce::ScopedJuceInitialiser_GUI gui; juce::StringArray paths;
    phase("JUCE GUI initialised");
    double sampleRate = 48000;
    juce::File report = juce::File::getCurrentWorkingDirectory().getChildFile("quality-real-vst3-host-report.json");
    for (int i = 1; i < argc; ++i) {
        const juce::String argument(argv[i]);
        if (argument == "--report" && i+1 < argc) { report = juce::File(juce::String(argv[++i])); continue; }
        if (argument == "--sample-rate" && i+1 < argc) { sampleRate = juce::String(argv[++i]).getDoubleValue(); continue; }
        if (argument == "--list" && i+1 < argc) {
            const auto data = juce::JSON::parse(juce::File(juce::String(argv[++i])));
            const auto entries = data.isArray() ? data : data.getProperty("plugins", juce::var{});
            if (auto* array = entries.getArray()) for (const auto& entry : *array) paths.add(entry.isString() ? entry.toString() : entry.getProperty("path", entry.getProperty("bundle", juce::var{})).toString());
        } else paths.add(argument);
    }
    if (sampleRate < 44100 || sampleRate > 192000) { std::cerr << "Sample rate must be 44100 through 192000\n"; return 2; }
    Host host(sampleRate);
    if (!host.load(paths)) { host.report(report); return 1; }
    phase("all " + juce::String(paths.size()) + " bundles prepared; dispatch loop begin");
    std::thread watchdog([&] {
        for (int i=0; i<1200 && !host.finished.load(); ++i) juce::Thread::sleep(100);
        if (!host.finished.load()) juce::MessageManager::callAsync([&] { host.check(false, "host test exceeded 120 seconds"); host.finish(); });
    });
    host.startTimer(60); juce::MessageManager::getInstance()->runDispatchLoop(); host.finished = true;
    watchdog.join(); host.report(report); return host.failures ? 1 : 0;
}
