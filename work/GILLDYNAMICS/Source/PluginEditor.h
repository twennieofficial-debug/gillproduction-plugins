#pragma once
#include "PluginProcessor.h"
class GillDynamicsEditor final:public juce::AudioProcessorEditor {public:explicit GillDynamicsEditor(GillDynamicsProcessor&);~GillDynamicsEditor()override;void paint(juce::Graphics&)override;void resized()override;bool hitTest(int,int)override;private:struct Impl;std::unique_ptr<Impl>impl;};
