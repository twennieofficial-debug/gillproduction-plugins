#pragma once
#include "PluginProcessor.h"
class GillFinishEditor final:public juce::AudioProcessorEditor{
public:explicit GillFinishEditor(GillFinishProcessor&);~GillFinishEditor()override;void paint(juce::Graphics&)override;void resized()override;
private:struct Impl;std::unique_ptr<Impl>impl;
};
