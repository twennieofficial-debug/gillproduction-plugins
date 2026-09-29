#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include <JuceHeader.h>
#include "PluginProcessor.h"

class GillDereverbAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GillDereverbAudioProcessorEditor (GillDereverbAudioProcessor&);
    ~GillDereverbAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};

    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GillDereverbAudioProcessorEditor)
};
