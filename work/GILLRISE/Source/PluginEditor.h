#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include "PluginProcessor.h"
class GillRiseEditor final:public juce::AudioProcessorEditor {
public:explicit GillRiseEditor(GillRiseProcessor&);~GillRiseEditor()override;void resized()override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};
struct Impl;std::unique_ptr<Impl>ui;
};
