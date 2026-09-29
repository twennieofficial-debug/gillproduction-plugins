#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include "PluginProcessor.h"
class GillFinishEditor final:public juce::AudioProcessorEditor{
public:explicit GillFinishEditor(GillFinishProcessor&);~GillFinishEditor()override;void paint(juce::Graphics&)override;void resized()override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};
struct Impl;std::unique_ptr<Impl>impl;
};
