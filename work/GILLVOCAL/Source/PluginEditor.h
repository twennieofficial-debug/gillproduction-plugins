#pragma once
#include "PluginProcessor.h"
class GillVocalEditor final : public juce::AudioProcessorEditor {
public:
    explicit GillVocalEditor(GillVocalProcessor&);
    ~GillVocalEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
