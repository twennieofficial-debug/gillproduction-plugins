#pragma once
#include "PluginProcessor.h"
#include "../../GILLCommon/QualityUi.h"
#include "../../GILLCommon/MaterialUi.h"
class GillMixEditor final:public juce::AudioProcessorEditor,private juce::Timer,private juce::ListBoxModel {
public:
 explicit GillMixEditor(GillMixProcessor&);~GillMixEditor()override;
 void paint(juce::Graphics&)override;void resized()override;
private:
 int getNumRows()override;void paintListBoxItem(int,juce::Graphics&,int,int,bool)override;
 void listBoxItemClicked(int,const juce::MouseEvent&)override;
 void timerCallback()override;void editTrack(int);
 GillMixProcessor&processor;gill::QualitySelector quality;std::unique_ptr<juce::LookAndFeel>look;juce::Image wood;
 juce::TextButton connect{"CONNECT SELECTED"},learn{"LEARN"},apply{"APPLY"},undo{"UNDO"},identity{"NEW ID"},disconnect{"DISCONNECT"};
 juce::Label status,title,inMeter,outMeter,limitTitle;juce::TextEditor name;
 juce::ComboBox role,preset;juce::ToggleButton lock{"LOCK"};juce::Slider gain,limit;
 std::array<juce::Slider,3>offsets;std::array<juce::Label,3>offsetLabels;
 juce::ListBox list{"TRACKS",this};juce::TooltipWindow tooltip{this,500};
 std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>gainAttachment,limitAttachment;
 std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,3>offsetAttachments;
 bool updating=false;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GillMixEditor)
};
