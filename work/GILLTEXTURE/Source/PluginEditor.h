#pragma once
#include "PluginProcessor.h"
#include "../../GILLCommon/HostKeyboardPolicy.h"
class GillTextureEditor final : public juce::AudioProcessorEditor {
public:
    explicit GillTextureEditor(GillTextureProcessor&);
    ~GillTextureEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillTextureEditor)
};
