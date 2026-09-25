#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>

namespace
{
const juce::Colour ink (0xff25382b), sage (0xff567462);

juce::Font coreFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (gillInterfaceFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

// Ordinary parameter synchronization never enters this user-interaction path.
// JUCE already scopes mouse drags, numeric commits, double clicks and accessible
// value edits; arrow keys need the same gesture explicitly.
class AmountSlider final : public juce::Slider
{
public:
    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool up = key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey;
        const bool down = key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey;
        if (isEnabled() && ! key.getModifiers().isAnyModifierKeyDown()
            && ((up && getValue() < getMaximum()) || (down && getValue() > getMinimum())))
        {
            juce::Slider::ScopedDragNotification gesture (*this);
            return juce::Slider::keyPressed (key);
        }
        return juce::Slider::keyPressed (key);
    }
};

class CoreLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    float scale = 1.0f;
    CoreLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, sage.withAlpha (0.24f));
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffffbef));
        setColour (juce::TextEditor::outlineColourId, sage.withAlpha (0.7f));
        setColour (juce::TextEditor::focusedOutlineColourId, sage);
        setColour (juce::TextEditor::highlightColourId, sage.withAlpha (0.24f));
        setColour (juce::TextEditor::highlightedTextColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xfff4efe0));
        setColour (juce::TooltipWindow::textColourId, ink);
    }
    juce::Font getLabelFont (juce::Label&) override { return coreFont (32.0f * scale, true); }
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        const float s = (float) slider.getWidth() / 372.0f;
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = juce::Rectangle<float> (60*s, 3*s, 223*s, 223*s).toNearestInt();
        layout.textBoxBounds = juce::Rectangle<float> (88*s, 264*s, 196*s, 50*s).toNearestInt();
        return layout;
    }
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setColour (juce::Label::textColourId, ink);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::TextEditor::textColourId, ink);
        label->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffffbef));
        label->setColour (juce::TextEditor::outlineColourId, sage.withAlpha (0.7f));
        label->setColour (juce::TextEditor::highlightColourId, sage.withAlpha (0.24f));
        label->setColour (juce::TextEditor::highlightedTextColourId, ink);
        return label;
    }
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (label.isBeingEdited()) return;
        const auto area = label.getLocalBounds().toFloat();
        g.setFont (getLabelFont (label));
        // A restrained inset highlight keeps the live readout in the wood theme.
        g.setColour (juce::Colour (0xffffedcd).withAlpha (0.70f));
        g.drawText (label.getText(), area.translated (0, 0.9f * scale), juce::Justification::centred, false);
        g.setColour (ink);
        g.drawText (label.getText(), area, juce::Justification::centred, false);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float proportion, float start, float end, juce::Slider&) override
    {
        gill::material::rotary(g, {static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)}, proportion, start, end, sage);
    }
};
}

struct GillDereverbAudioProcessorEditor::Impl
{
    GillDereverbAudioProcessorEditor& owner;
    GillDereverbAudioProcessor& processor;
    CoreLookAndFeel look;
    juce::Image reference;
    AmountSlider amount;
    juce::TooltipWindow tooltip;
    juce::RangedAudioParameter* amountParameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool applyingHostValue = false;
    int gestureDepth = 0;

    gill::QualitySelector quality{processor.apvts, processor};
    Impl (GillDereverbAudioProcessorEditor& editor, GillDereverbAudioProcessor& p)
        : owner (editor), processor (p), tooltip (&editor, 650)
    {
        owner.setLookAndFeel (&look);owner.addAndMakeVisible(quality);
        reference = juce::ImageCache::getFromMemory (BinaryData::core_reference_png, BinaryData::core_reference_pngSize);
        amount.setName ("AMOUNT");
        amount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        amount.setRotaryParameters (juce::MathConstants<float>::pi*1.25f, juce::MathConstants<float>::pi*2.75f, true);
        amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 196, 50);
        amount.setScrollWheelEnabled (false);
        amount.setPopupMenuEnabled (false);
        amount.setMouseDragSensitivity (240);
        amount.setWantsKeyboardFocus (true);
        amount.setTooltip ("HALL REDUZIEREN | ZIEHEN ODER PROZENTWERT EINGEBEN | DOPPELKLICK: 55 %");
        owner.addAndMakeVisible (amount);
        amount.setColour (juce::Slider::textBoxTextColourId, ink);
        amount.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        amount.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        amountParameter = processor.apvts.getParameter ("amount");
        jassert (amountParameter != nullptr);
        if (amountParameter != nullptr)
        {
            const auto& range = amountParameter->getNormalisableRange();
            amount.setRange (range.start, range.end, range.interval);
            attachment = std::make_unique<juce::ParameterAttachment> (*amountParameter, [this] (float value)
            {
                const juce::ScopedValueSetter<bool> hostSync (applyingHostValue, true);
                amount.setValue (value, juce::sendNotificationSync);
            });
            amount.onDragStart = [this]
            {
                if (gestureDepth++ == 0) attachment->beginGesture();
            };
            amount.onDragEnd = [this]
            {
                if (gestureDepth > 0 && --gestureDepth == 0) attachment->endGesture();
            };
            amount.onValueChange = [this]
            {
                if (applyingHostValue) return;
                const auto value = (float) amount.getValue();
                if (std::abs (amountParameter->convertTo0to1 (value) - amountParameter->getValue()) <= 1.0e-7f) return;
                // This is a real local edit, not opening, clicking or host sync.
                // Switch mode before publishing the new Amount to the processor.
                processor.beginUserAmountGesture();
                if (gestureDepth > 0) attachment->setValueAsPartOfGesture (value);
                else attachment->setValueAsCompleteGesture (value);
            };
            attachment->sendInitialUpdate();
        }
        amount.textFromValueFunction = [] (double value) { return juce::String (value, 0) + " %"; };
        amount.valueFromTextFunction = [] (const juce::String& value) { return value.replaceCharacter (',', '.').getDoubleValue(); };
        amount.setDoubleClickReturnValue (true, 55.0);
        amount.updateText();
    }
    ~Impl()
    {
        amount.onValueChange = {}; amount.onDragStart = {}; amount.onDragEnd = {};
        if (attachment != nullptr && gestureDepth > 0) attachment->endGesture();
        attachment.reset();
        owner.setLookAndFeel (nullptr);
    }
    void resized()
    {
        const float s = (float) owner.getWidth()/320.0f;
        look.scale = s;
        quality.setBounds(juce::Rectangle<float>(103*s, 39*s, 110*s, 26*s).toNearestInt());
        amount.setBounds (juce::Rectangle<float> (30*s, 68*s, 260*s, 220*s).toNearestInt());
        amount.setMouseDragSensitivity (juce::roundToInt (240*s));
        amount.resized();
        amount.repaint();
    }
    void paint (juce::Graphics& g)
    {
        const float w = (float) owner.getWidth(), h = (float) owner.getHeight(), s = w/320.0f;
        g.fillAll (juce::Colour (0xffc5aa84));
        if (reference.isValid())
        {
            // Only quiet wood, the original small emblem and the outside frame
            // are sampled. No reference knob, label or fixed readout is drawn.
            g.drawImage (reference, 0, 0, (int) w, (int) h, 950, 285, 170, 805);
            const int b = juce::roundToInt (17*s), corner = juce::roundToInt (24*s);
            g.drawImage (reference, corner, 0, (int) w-corner*2, b, 150, 82, 955, 42);
            g.drawImage (reference, corner, (int) h-b, (int) w-corner*2, b, 150, 1115, 955, 42);
            g.drawImage (reference, 0, corner, b, (int) h-corner*2, 90, 142, 42, 955);
            g.drawImage (reference, (int) w-b, corner, b, (int) h-corner*2, 1123, 142, 42, 955);
            g.drawImage (reference, 0, 0, corner, corner, 90, 82, 60, 60);
            g.drawImage (reference, (int) w-corner, 0, corner, corner, 1105, 82, 60, 60);
            g.drawImage (reference, 0, (int) h-corner, corner, corner, 90, 1097, 60, 60);
            g.drawImage (reference, (int) w-corner, (int) h-corner, corner, corner, 1105, 1097, 60, 60);
            g.drawImage (reference, juce::roundToInt (23*s), juce::roundToInt (16*s), juce::roundToInt (48*s), juce::roundToInt (34*s), 224, 146, 174, 122);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdfc09a), 0, 0, juce::Colour (0xffc9a67b), w, h, false));
            g.fillRoundedRectangle (2*s, 2*s, w-4*s, h-4*s, 18*s);
            g.setFont (coreFont (38*s, true)); g.setColour (juce::Colour (0xff5b432b));
            g.drawText ("GP", juce::Rectangle<float> (23*s,16*s,48*s,34*s), juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff5b4027).withAlpha (0.85f));
        g.drawLine (87*s, 16*s, 87*s, 51*s, 0.8f*s);
        g.setColour (juce::Colour (0xffffe7c4).withAlpha (0.75f));
        g.drawLine (88*s, 16*s, 88*s, 51*s, 0.7f*s);
        const auto titleBounds = juce::Rectangle<float> (103*s, 15*s, 199*s, 22*s);
        g.setFont (coreFont (20*s));
        g.setColour (juce::Colour (0xffffe8c7).withAlpha (0.8f)); g.drawText ("GILLDEREVERB", titleBounds.translated (0,0.8f*s), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff392b1a)); g.drawText ("GILLDEREVERB", titleBounds, juce::Justification::centredLeft, false);
        const auto captionBounds = juce::Rectangle<float> (36*s, 229*s, 248*s, 18*s);
        auto captionFont = coreFont (16*s, true); captionFont.setExtraKerningFactor (0.10f); g.setFont (captionFont);
        g.setColour (juce::Colour (0xffffedcf).withAlpha (0.85f)); g.drawText ("HALL REDUZIEREN", captionBounds.translated (0,0.8f*s), juce::Justification::centred, false);
        g.setColour (ink); g.drawText ("HALL REDUZIEREN", captionBounds, juce::Justification::centred, false);
    }
};

GillDereverbAudioProcessorEditor::GillDereverbAudioProcessorEditor (GillDereverbAudioProcessor& processor)
    : juce::AudioProcessorEditor (&processor)
{
    impl = std::make_unique<Impl> (*this, processor);
    setResizable (true, true);
    setResizeLimits (320, 300, 640, 600);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio (320.0/300.0);
    setSize (320, 300);
}
GillDereverbAudioProcessorEditor::~GillDereverbAudioProcessorEditor() = default;
void GillDereverbAudioProcessorEditor::paint (juce::Graphics& g) { impl->paint (g); }
void GillDereverbAudioProcessorEditor::resized() { if (impl) impl->resized(); }
