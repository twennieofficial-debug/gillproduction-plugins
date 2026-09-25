#pragma once
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
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GillDereverbAudioProcessorEditor)
};
