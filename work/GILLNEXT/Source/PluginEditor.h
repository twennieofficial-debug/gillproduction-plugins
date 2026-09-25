#pragma once
#include "PluginProcessor.h"
class GillNextEditor final:public juce::AudioProcessorEditor {
public:explicit GillNextEditor(GillNextProcessor&);~GillNextEditor()override;
void paint(juce::Graphics&)override;void resized()override;bool hitTest(int,int)override;
private:struct Impl;std::unique_ptr<Impl>impl;
};
