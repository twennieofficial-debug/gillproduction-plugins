#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout GillControlProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout parameters;
    parameters.add(gill::qualityParameter()); return parameters;
}
GillControlProcessor::GillControlProcessor()
    : AudioProcessor(BusesProperties().withInput("INPUT", juce::AudioChannelSet::stereo(), true).withOutput("OUTPUT", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GILLCONTROL_STATE", layout()), quality(*this, apvts, true) {
    apvts.addParameterListener(gill::qualityParameterId, this); startTimer(50);
}
GillControlProcessor::~GillControlProcessor() { stopTimer(); apvts.removeParameterListener(gill::qualityParameterId, this); }
bool GillControlProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto channels = layout.getMainOutputChannelSet();
    return (channels == juce::AudioChannelSet::mono() || channels == juce::AudioChannelSet::stereo()) && channels == layout.getMainInputChannelSet();
}
void GillControlProcessor::parameterChanged(const juce::String&, float value) {
    if (!restoring.load(std::memory_order_relaxed) && !quality.isApplyingGlobalChange())
        pendingAutomation.store(value < .5f ? 0 : 1, std::memory_order_relaxed);
}
bool GillControlProcessor::selectGlobal(int mode) {
    restoreTarget = -1; pendingRestore.store(-1); pendingAutomation.store(-1);
    return quality.broadcast(mode);
}
void GillControlProcessor::serviceControl() {
    quality.pollOnMessageThread();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto restored = pendingRestore.exchange(-1, std::memory_order_relaxed);
    if (restored >= 0) { restoreTarget = restored; stableInstances = -1; restoreStarted = stableSince = now; }
    const auto automated = pendingAutomation.exchange(-1, std::memory_order_relaxed);
    if (automated >= 0) { restoreTarget = -1; if (!quality.broadcast(automated)) pendingAutomation.store(automated); }
    if (restoreTarget >= 0) {
        const auto count = quality.registeredInstances();
        if (count != stableInstances) { stableInstances = count; stableSince = now; }
        // Wait for project instances/state to load before restoring the master.
        // No project identifier exists in VST3: scope is this DAW process only.
        if ((now - restoreStarted >= 500 && now - stableSince >= 300) || now - restoreStarted >= 3000)
            if (quality.broadcast(restoreTarget)) restoreTarget = -1;
    }
}
void GillControlProcessor::getStateInformation(juce::MemoryBlock& out) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, out);
}
void GillControlProcessor::setStateInformation(const void* data, int bytes) {
    if (!data || bytes <= 0 || bytes > 65536) return;
    const auto xml = getXmlFromBinary(data, bytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const auto incoming = juce::ValueTree::fromXml(*xml);
    const auto value = incoming.getChildWithProperty("id", gill::qualityParameterId).getProperty("value");
    const auto text = value.toString().trim(); char* end = nullptr;
    if (text.isEmpty()) return;
    const auto numeric = std::strtod(text.toRawUTF8(), &end);
    if (!end || *end != '\0' || !std::isfinite(numeric) || numeric < 0 || numeric > 1) return;
    const auto mode = numeric < .5 ? 0 : 1;
    restoring.store(true, std::memory_order_relaxed);
    if (auto* parameter = apvts.getParameter(gill::qualityParameterId)) parameter->setValueNotifyingHost(static_cast<float>(mode));
    restoring.store(false, std::memory_order_relaxed);
    pendingAutomation.store(-1, std::memory_order_relaxed); pendingRestore.store(mode, std::memory_order_relaxed);
}
juce::AudioProcessorEditor* GillControlProcessor::createEditor() { return new GillControlEditor(*this); }
