#pragma once
#include "PluginProcessor.h"
class GillMasterEditor final:public juce::AudioProcessorEditor {
public:
    explicit GillMasterEditor(GillMasterProcessor&);
    ~GillMasterEditor()override;
    void paint(juce::Graphics&)override;
    void resized()override;
private:
    struct Impl;std::unique_ptr<Impl>impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillMasterEditor)
};
