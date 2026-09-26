#pragma once
#include "PluginProcessor.h"

class GillControlLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

class GillControlEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit GillControlEditor(GillControlProcessor&);
    ~GillControlEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    GillControlProcessor& processor;
    GillControlLookAndFeel look;
    juce::TextButton live{"GLOBAL LIVE"}, pro{"GLOBAL PRO"};
    juce::Label status;
    juce::TooltipWindow tooltip{this, 500};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillControlEditor)
};
