#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class GillRestorationAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GillRestorationAudioProcessorEditor (GillRestorationAudioProcessor&);
    ~GillRestorationAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};

    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GillRestorationAudioProcessorEditor)
};
