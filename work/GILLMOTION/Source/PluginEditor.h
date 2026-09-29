#pragma once
#include "PluginProcessor.h"
#include "../../GILLCommon/HostKeyboardPolicy.h"
class GillMotionEditor final:public juce::AudioProcessorEditor {
public:explicit GillMotionEditor(GillMotionProcessor&);~GillMotionEditor()override;void resized()override;
private:gill::HostKeyboardPolicy hostKeys{*this};struct Impl;std::unique_ptr<Impl>ui;
};
