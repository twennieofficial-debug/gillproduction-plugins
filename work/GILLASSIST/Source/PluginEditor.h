#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
class GillAssistEditor final:public juce::AudioProcessorEditor{
public:explicit GillAssistEditor(GillAssistProcessor&);~GillAssistEditor()override;void resized()override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};
struct Impl;std::unique_ptr<Impl>impl;
};
