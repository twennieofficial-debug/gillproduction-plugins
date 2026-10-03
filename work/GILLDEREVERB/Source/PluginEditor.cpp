#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>

namespace
{
const juce::Colour ink(0xff102237), sage(0xff54b8d5);

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

class CoreLookAndFeel final : public gill::prism::Look {  };
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
        amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 108, 25);
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
        quality.setBounds(juce::Rectangle<float>(102*s, 38*s, 110*s, 26*s).toNearestInt());
        amount.setBounds (juce::Rectangle<float> (68*s, 85*s, 207*s, 161*s).toNearestInt());
        amount.setMouseDragSensitivity (juce::roundToInt (240*s));
        amount.resized();
        amount.repaint();
    }
    void paint (juce::Graphics& g)
    {
const float scale=owner.getWidth()/320.f;g.addTransform(juce::AffineTransform::scale(scale));
        gill::prism::chassis(g,320,300,73,67);gill::prism::title(g,"GILLDEREVERB",{71,14,229,23},19);
        g.setColour(ink);g.setFont(coreFont(13,true));g.drawText("HALL REDUZIEREN",juce::Rectangle<float>(52,254,230,18),juce::Justification::centred,false);
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
