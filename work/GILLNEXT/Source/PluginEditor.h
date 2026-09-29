#pragma once
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include "PluginProcessor.h"
class GillNextEditor final:public juce::AudioProcessorEditor {
public:explicit GillNextEditor(GillNextProcessor&);~GillNextEditor()override;
void paint(juce::Graphics&)override;void resized()override;bool hitTest(int,int)override;
private:
    gill::HostKeyboardPolicy hostKeyboard{*this};
struct Impl;std::unique_ptr<Impl>impl;
};
