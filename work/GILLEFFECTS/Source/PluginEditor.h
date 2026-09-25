#pragma once
#include "PluginProcessor.h"
class GillEffectEditor final : public juce::AudioProcessorEditor {
public:
    explicit GillEffectEditor(GillEffectProcessor&);
    ~GillEffectEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
