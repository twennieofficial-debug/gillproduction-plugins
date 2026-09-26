#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
class GillAssistEditor final:public juce::AudioProcessorEditor{
public:explicit GillAssistEditor(GillAssistProcessor&);~GillAssistEditor()override;void resized()override;
private:struct Impl;std::unique_ptr<Impl>impl;
};
