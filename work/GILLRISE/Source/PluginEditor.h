#pragma once
#include "PluginProcessor.h"
class GillRiseEditor final:public juce::AudioProcessorEditor {
public:explicit GillRiseEditor(GillRiseProcessor&);~GillRiseEditor()override;void resized()override;
private:struct Impl;std::unique_ptr<Impl>ui;
};
