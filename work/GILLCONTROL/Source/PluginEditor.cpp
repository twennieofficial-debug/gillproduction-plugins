#include "PluginEditor.h"

GillControlEditor::GillControlEditor(GillControlProcessor& p) : AudioProcessorEditor(p), processor(p) {
    addAndMakeVisible(live); addAndMakeVisible(pro); addAndMakeVisible(status);
    live.onClick = [this] { processor.selectGlobal(0); timerCallback(); };
    pro.onClick = [this] { processor.selectGlobal(1); timerCallback(); };
    for (auto* button : {&live, &pro}) {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff231c17));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd8c09c));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeadcc7));
        button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff211a14));
    }
    live.setTooltip("Switch registered GILL plugins in this DAW process to LIVE. Individual latency depends on the effect.");
    pro.setTooltip("Switch registered GILL plugins in this DAW process to PRO. Click again to resynchronize all instances.");
    status.setJustificationType(juce::Justification::centred);
    status.setColour(juce::Label::textColourId, juce::Colour(0xffd7c5ac));
    setSize(420, 220); setResizable(false, false); timerCallback(); startTimer(150);
}
void GillControlEditor::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff191512));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5b3c27), 0, 0, juce::Colour(0xff251b15), 0, bounds.getHeight(), false));
    g.fillRect(bounds);
    // Restrained procedural walnut grain remains crisp at Retina scale.
    g.saveState(); g.reduceClipRegion(getLocalBounds());
    for (int i = 0; i < 40; ++i) {
        juce::Path grain; const float y = 8.f + i * 5.5f;
        grain.startNewSubPath(0, y); grain.cubicTo(120, y + 5 + std::sin(i * .8f) * 3, 280, y - 4, 420, y + 1);
        g.setColour(juce::Colour(i % 3 == 0 ? 0x1430180a : 0x0cffe6b5)); g.strokePath(grain, juce::PathStrokeType(.7f));
    }
    g.restoreState();
    g.setColour(juce::Colour(0xffab8a5c)); g.drawRect(bounds.reduced(1), 1);
    g.setColour(juce::Colour(0xfff0dfc2)); g.setFont(juce::FontOptions(25, juce::Font::bold));
    g.drawText("GP", 23, 18, 49, 40, juce::Justification::centred);
    g.setFont(juce::FontOptions(23, juce::Font::bold)); g.drawText("GILLCONTROL", 83, 20, 310, 29, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(11)); g.setColour(juce::Colour(0xffc5ae8e));
    g.drawText("ONE SESSION. ONE QUALITY SWITCH.", 84, 48, 304, 18, juce::Justification::centredLeft);
    g.drawText("DRY PASS-THROUGH / 0 SAMPLES", 25, 186, 370, 16, juce::Justification::centred);
}
void GillControlEditor::resized() { live.setBounds(26, 88, 177, 52); pro.setBounds(217, 88, 177, 52); status.setBounds(26, 150, 368, 25); }
void GillControlEditor::timerCallback() {
    const auto current = processor.quality.mode();
    live.setToggleState(current == 0, juce::dontSendNotification); pro.setToggleState(current == 1, juce::dontSendNotification);
    status.setText(processor.quality.connected() ? juce::String(processor.quality.registeredInstances()) + " GILL INSTANCES CONNECTED" : "GLOBAL CONNECTION UNAVAILABLE", juce::dontSendNotification);
}
