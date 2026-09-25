#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../../GILLCommon/QualityBus.h"

class GillControlProcessor final : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::Timer {
public:
    GillControlProcessor();
    ~GillControlProcessor() override;
    void prepareToPlay(double, int) override { setLatencySamples(0); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void processBlockBypassed(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "GILLCONTROL"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "GLOBAL QUALITY"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool selectGlobal(int mode);
    // Public pump enables deterministic native tests without a hidden UI.
    void serviceControl();
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient quality;
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void parameterChanged(const juce::String&, float) override;
    void timerCallback() override { serviceControl(); }
    std::atomic<bool> restoring{false};
    std::atomic<int> pendingAutomation{-1}, pendingRestore{-1};
    int restoreTarget = -1, stableInstances = -1;
    double restoreStarted = 0, stableSince = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillControlProcessor)
};
