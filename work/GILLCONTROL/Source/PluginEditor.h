#pragma once
#include "PluginProcessor.h"

class GillControlEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit GillControlEditor(GillControlProcessor&);
    ~GillControlEditor() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    GillControlProcessor& processor;
    juce::TextButton live{"GLOBAL LIVE"}, pro{"GLOBAL PRO"};
    juce::Label status;
    juce::TooltipWindow tooltip{this, 500};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillControlEditor)
};
