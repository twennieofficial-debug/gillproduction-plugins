#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include "PluginProcessor.h"
class GillEffectEditor final : public juce::AudioProcessorEditor {
public:
    explicit GillEffectEditor(GillEffectProcessor&);
    ~GillEffectEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};

    struct Impl;
    std::unique_ptr<Impl> impl;
};
