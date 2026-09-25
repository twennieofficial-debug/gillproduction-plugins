#pragma once
#include "QualityBus.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace gill {
// Parent editor owns this component. Its attachments observe host automation
// and state recall; it never owns a second bus registration.
class QualitySelector final : public juce::Component, private juce::Timer {
public:
    QualitySelector(juce::AudioProcessorValueTreeState& state, juce::AudioProcessor& owner)
        : processor(owner), apvts(state) {
        setLookAndFeel(&lookAndFeel);
        addAndMakeVisible(live); addAndMakeVisible(pro);
        live.setButtonText("LIVE"); pro.setButtonText("PRO");
        for (auto* button : {&live, &pro}) {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27201b));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd9c3a1));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe8dac7));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff211b16));
        }
        live.onClick = [this] { select(liveQuality); };
        pro.onClick = [this] { select(proQuality); };
        setSize(110, 26); refresh(); startTimer(100);
    }
    ~QualitySelector() override { stopTimer(); setLookAndFeel(nullptr); }
    void resized() override { auto area = getLocalBounds(); live.setBounds(area.removeFromLeft(area.getWidth()/2)); pro.setBounds(area); }
private:
    void select(int mode) {
        if (auto* parameter = apvts.getParameter(qualityParameterId)) {
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(mode))); parameter->endChangeGesture();
        }
        refresh();
    }
    void refresh() {
        const auto* raw = apvts.getRawParameterValue(qualityParameterId);
        const bool isLive = raw && raw->load(std::memory_order_relaxed) < .5f;
        live.setToggleState(isLive, juce::dontSendNotification); pro.setToggleState(!isLive, juce::dontSendNotification);
        const auto samples = processor.getLatencySamples(); const auto rate = processor.getSampleRate();
        const auto latency = juce::String(samples) + " samples" + (rate > 0 ? " / " + juce::String(samples * 1000.0 / rate, 2) + " ms" : juce::String{});
        live.setTooltip("LIVE: recording mode. Reported latency: " + latency);
        pro.setTooltip("PRO: processing quality. Reported latency: " + latency);
    }
    void timerCallback() override { refresh(); }
    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::TextButton live, pro;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QualitySelector)
};
} // namespace gill
