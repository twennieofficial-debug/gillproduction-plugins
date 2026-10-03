#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "../../GILLCommon/MaterialUi.h"

void GillControlLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour&, bool over, bool down) {
gill::prism::Look::drawButtonBackground(g,button,gill::prism::navy,over,down);
    }

GillControlEditor::GillControlEditor(GillControlProcessor& p) : AudioProcessorEditor(p), processor(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(live); addAndMakeVisible(pro); addAndMakeVisible(status);
    live.onClick = [this] { processor.selectGlobal(0); timerCallback(); };
    pro.onClick = [this] { processor.selectGlobal(1); timerCallback(); };
    for (auto* button : {&live, &pro}) {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff231c17));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd8c09c));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeadcc7));
        button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff211a14));
    }
    live.setTooltip("Switch registered GILL plugins in this DAW process to LIVE. All instances use zero added plug-in delay. Some processing is bypassed in LIVE.");
    pro.setTooltip("Switch registered GILL plugins in this DAW process to PRO. Click again to resynchronize all instances.");
    status.setJustificationType(juce::Justification::centred);
    status.setColour(juce::Label::textColourId, gill::prism::ink);
    setSize(420, 220); setResizable(false, false); timerCallback(); startTimer(150);
}
void GillControlEditor::paint(juce::Graphics& g) {
gill::prism::chassis(g,420,220,76);gill::prism::title(g,"GILLCONTROL",{75,15,310,34},23);
    g.setColour(gill::prism::ink);g.setFont(juce::FontOptions(11));g.drawText(processor.quality.mode()==0?"LIVE / ZERO PLUG-IN DELAY":"PRO / FULL PROCESSING QUALITY",25,186,370,16,juce::Justification::centred);
    }
void GillControlEditor::resized() { live.setBounds(26, 88, 177, 52); pro.setBounds(217, 88, 177, 52); status.setBounds(26, 150, 368, 25); }
void GillControlEditor::timerCallback() {
    const auto current = processor.quality.mode();
    live.setToggleState(current == 0, juce::dontSendNotification); pro.setToggleState(current == 1, juce::dontSendNotification);
    status.setText(processor.quality.connected() ? juce::String(processor.quality.registeredInstances()) + " GILL INSTANCES CONNECTED" : "GLOBAL CONNECTION UNAVAILABLE", juce::dontSendNotification);
    repaint();
}
