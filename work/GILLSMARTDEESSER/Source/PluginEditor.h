#pragma once
#include "PluginProcessor.h"
class GillSmartDeEsserEditor final : public juce::AudioProcessorEditor {
public:
    explicit GillSmartDeEsserEditor(GillSmartDeEsserProcessor&);
    ~GillSmartDeEsserEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
