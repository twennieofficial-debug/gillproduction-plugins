#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "../../GILLCommon/ScalpelUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>

namespace
{
const juce::Colour ink=gill::scalpel::ink, sage=gill::scalpel::cyan;

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

class CoreLookAndFeel final : public gill::scalpel::Look { };
}

struct GillRestorationAudioProcessorEditor::Impl
{
    GillRestorationAudioProcessorEditor& owner;
    GillRestorationAudioProcessor& processor;
    CoreLookAndFeel look;
    juce::Image reference;
    AmountSlider amount;
    juce::TextButton bypass{"BYPASS"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    gill::scalpel::Levels levels{[this]{return processor.getInputDb();},[this]{return processor.getOutputDb();}};
    juce::TooltipWindow tooltip;
    juce::RangedAudioParameter* amountParameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool applyingHostValue = false;
    int gestureDepth = 0;

    gill::QualitySelector quality{processor.apvts, processor};
    Impl (GillRestorationAudioProcessorEditor& editor, GillRestorationAudioProcessor& p)
        : owner (editor), processor (p), tooltip (&editor, 650)
    {
        owner.setLookAndFeel (&look);owner.addAndMakeVisible(quality);quality.setLookAndFeel(&look);owner.addAndMakeVisible(levels);
        bypass.setName("BYPASS");bypass.setComponentID("bypass");bypass.setClickingTogglesState(true);owner.addAndMakeVisible(bypass);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts,"bypass",bypass);
        reference = juce::ImageCache::getFromMemory (BinaryData::core_reference_png, BinaryData::core_reference_pngSize);
        amount.setName ("AMOUNT");amount.setComponentID("amount");
        amount.setSliderStyle (juce::Slider::LinearVertical);
        amount.setRotaryParameters (juce::MathConstants<float>::pi*1.25f, juce::MathConstants<float>::pi*2.75f, true);
        amount.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 140, 25);
        amount.setScrollWheelEnabled (false);
        amount.setPopupMenuEnabled (false);
        amount.setMouseDragSensitivity (240);
        amount.setWantsKeyboardFocus (true);
        amount.setTooltip (processor.getCaption() + " | ZIEHEN ODER PROZENTWERT EINGEBEN | DOPPELKLICK: 55 %");
        owner.addAndMakeVisible (amount);
        amount.setColour (juce::Slider::textBoxTextColourId, gill::prism::white);
        amount.setColour (juce::Slider::textBoxBackgroundColourId, gill::prism::dark);
        amount.setColour (juce::Slider::textBoxOutlineColourId, gill::prism::cyan.withAlpha (.25f));
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
        const float s=owner.getWidth()/380.f;look.scale=s;
        quality.setBounds(juce::Rectangle<float>(244*s,73*s,110*s,26*s).toNearestInt());bypass.setBounds(juce::Rectangle<float>(25*s,73*s,84*s,26*s).toNearestInt());
        amount.setBounds(juce::Rectangle<float>(31*s,153*s,105*s,377*s).toNearestInt());
        levels.setBounds(juce::Rectangle<float>(153*s,121*s,202*s,391*s).toNearestInt());
    }
    void paint(juce::Graphics& g)
    {
        g.addTransform(juce::AffineTransform::scale(owner.getWidth()/380.f));
        gill::scalpel::chassis(g,380,620,processor.getName());
        gill::scalpel::panel(g,{22,121,120,427});
        gill::scalpel::text(g,"C L E A N",{29,126,106,25},14);
        gill::scalpel::text(g,"AMOUNT",{29,530,106,17},10);
        gill::scalpel::text(g,processor.getCaption(),{151,529,207,20},12);
        gill::scalpel::text(g,"INPUT / OUTPUT / DBFS",{151,554,207,18},10);
    }
};

GillRestorationAudioProcessorEditor::GillRestorationAudioProcessorEditor (GillRestorationAudioProcessor& processor)
    : juce::AudioProcessorEditor (&processor)
{
    impl = std::make_unique<Impl> (*this, processor);
    setResizable (false, false);
    setResizeLimits (380, 620, 380, 620);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio (380.0/620.0);
    setSize (380, 620);
}
GillRestorationAudioProcessorEditor::~GillRestorationAudioProcessorEditor() = default;
void GillRestorationAudioProcessorEditor::paint (juce::Graphics& g) { impl->paint (g); }
void GillRestorationAudioProcessorEditor::resized() { if (impl) impl->resized(); }
