#pragma once
#include "../../GILLCommon/ModeTransition.h"
#include "../../GILLCommon/QualityBus.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "RestorationDSP.h"
#include <array>
#include <atomic>

class GillRestorationAudioProcessor final : public juce::AudioProcessor {
public:
    explicit GillRestorationAudioProcessor(gillrestoration::Mode);
    ~GillRestorationAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override;
    juce::String getCaption() const;
    gillrestoration::Mode getMode() const noexcept { return mode; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return mode == gillrestoration::Mode::Declick ? 0.004125 : 0.008125; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "DEFAULT"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    gill::QualityClient qualityClient{*this, apvts};
private:
    gill::ModeTransition qualityTransition;
    template<class T> void process(juce::AudioBuffer<T>&, bool);
    const gillrestoration::Mode mode;
    std::atomic<float> *amountParam{}, *bypassParam{};
    gillrestoration::RestorationEngine engine;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> bypassSmooth;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillRestorationAudioProcessor)
};
