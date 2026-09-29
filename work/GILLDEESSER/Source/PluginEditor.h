#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
class GillDeEsserAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit GillDeEsserAudioProcessorEditor(GillDeEsserAudioProcessor&);
    ~GillDeEsserAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};

    struct Impl;std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillDeEsserAudioProcessorEditor)
};
