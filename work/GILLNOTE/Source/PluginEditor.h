#pragma once
#include "PluginProcessor.h"
#include "../../GILLCommon/HostKeyboardPolicy.h"
class GillNoteEditor final:public juce::AudioProcessorEditor {
public:explicit GillNoteEditor(GillNoteProcessor&);~GillNoteEditor()override;void resized()override;
private:gill::HostKeyboardPolicy hostKeys{*this};struct Impl;std::unique_ptr<Impl>ui;
};
