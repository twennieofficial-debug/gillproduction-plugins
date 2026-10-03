#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"

namespace {
const juce::Colour cream(0xffe2ecf6),ink(0xff102237),sage(0xff54b8d5),dark(0xff091d31);
juce::Font font(float h,bool bold=true){return juce::Font(juce::FontOptions(gillInterfaceFontName(),h,bold?juce::Font::bold:juce::Font::plain));}
class MaterialLook final : public gill::prism::Look {  };
}
struct GillSmartDeEsserEditor::Impl:private juce::Timer{
    GillSmartDeEsserEditor& owner;GillSmartDeEsserProcessor& p;MaterialLook look;juce::Component canvas;juce::Slider amount;
    juce::TextButton learn{"LEARN"},apply{"APPLY"},undo{"UNDO"},listen{"LISTEN REMOVED"},bypass{"BYPASS"};juce::ComboBox preset;juce::Label status;
    gill::QualitySelector quality{p.apvts,p};juce::Image wood;juce::TooltipWindow tips;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment,bypassAttachment;
    Impl(GillSmartDeEsserEditor& o,GillSmartDeEsserProcessor& v):owner(o),p(v),tips(&o,600){
        owner.setLookAndFeel(&look);owner.addAndMakeVisible(canvas);canvas.setInterceptsMouseClicks(false,true);
        for(auto* c:std::initializer_list<juce::Component*>{&amount,&learn,&apply,&undo,&listen,&bypass,&preset,&status,&quality})canvas.addAndMakeVisible(*c);
        wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);
        amount.setName("AMOUNT");amount.setComponentID("amount");amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);amount.setTextBoxStyle(juce::Slider::TextBoxBelow,false,110,28);amount.setTextValueSuffix(" %");amount.setDoubleClickReturnValue(true,55);amount.setTooltip("Strength of sibilance reduction. Double-click: 55%.");amountAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"amount",amount);
        learn.setName("learn");learn.onClick=[this]{if(p.learnState.load()==GillSmartDeEsserProcessor::learning||p.learnState.load()==GillSmartDeEsserProcessor::armed)p.finishLearning();else p.startLearning();};learn.setTooltip("LEARN, play the whole isolated vocal, then stop. Up to 5 minutes; STOP finishes early. APPLY auditions the learned profile.");
        apply.onClick=[this]{p.applyLearned();};apply.setTooltip("Apply the measured range and threshold. The previous profile can be restored with UNDO.");undo.onClick=[this]{p.undoLearned();};
        listen.setName("listen");bypass.setName("bypass");listen.setClickingTogglesState(true);bypass.setClickingTogglesState(true);listenAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"listen",listen);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);listen.setTooltip("Audition exactly the audio removed by the de-esser. Switch off for normal playback.");
        for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.setName("PRESET");preset.onChange=[this]{if(preset.getSelectedId()>0)p.setCurrentProgram(preset.getSelectedId()-1);};
        status.setFont(font(12));status.setColour(juce::Label::textColourId,ink);status.setJustificationType(juce::Justification::centredLeft);refresh();startTimerHz(20);
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    void resized(){canvas.setBounds(0,0,480,360);canvas.setTransform(juce::AffineTransform::scale(owner.getWidth()/480.f));quality.setBounds(348,21,110,26);amount.setBounds(25,94,195,185);learn.setBounds(238,190,68,29);apply.setBounds(312,190,68,29);undo.setBounds(386,190,68,29);status.setBounds(238,227,216,48);listen.setBounds(25,287,156,28);bypass.setBounds(192,287,83,28);preset.setBounds(28,324,425,27);}
    void refresh(){const int state=p.learnState.load();learn.setButtonText(state==GillSmartDeEsserProcessor::learning?"STOP":state==GillSmartDeEsserProcessor::armed?"CANCEL":"LEARN");apply.setEnabled(state==GillSmartDeEsserProcessor::ready);undo.setEnabled(p.canUndo());
        status.setText(!p.supported.load()?"UNSUPPORTED RATE / BYPASS":state==GillSmartDeEsserProcessor::armed?"ARMED / PRESS PLAY":state==GillSmartDeEsserProcessor::learning?"LISTENING  "+juce::String(p.elapsedSeconds.load(),1)+" S / MAX 300":state==GillSmartDeEsserProcessor::ready?"PROFILE READY\nAPPLY TO HEAR":state==GillSmartDeEsserProcessor::insufficient?"MORE VOICE + S SOUNDS NEEDED":state==GillSmartDeEsserProcessor::applied?"LEARNED PROFILE ACTIVE":"PLAY VOCAL + LEARN",juce::dontSendNotification);
        preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);owner.repaint();}
    void timerCallback()override{refresh();}
    void paint(juce::Graphics& g){g.addTransform(juce::AffineTransform::scale(owner.getWidth()/480.f));
        gill::prism::chassis(g,480,360,67);gill::prism::title(g,"GILLSMARTDEESSER",{75,15,264,33},20);
        g.setColour(ink);g.setFont(font(13));g.drawText("AMOUNT",27,77,191,20,juce::Justification::centred);
        auto card=juce::Rectangle<float>(236,78,218,103);gill::material::panel(g,card,juce::Colour(0xffb9cada),12,true);
        gillsmart::Profile candidate;const bool pending=p.learnedCandidate(candidate);const double hz=pending?candidate.frequency:p.value("frequency"),q=pending?candidate.q:p.value("q"),level=pending?candidate.threshold:p.value("threshold");
        const double fs=p.uiRate.load(),warped=std::tan(gillsmart::pi*gillsmart::effectiveFrequency(hz,fs)/fs),root=std::sqrt(4*q*q+1),lo=fs/gillsmart::pi*std::atan(warped*(root-1)/(2*q)),hi=fs/gillsmart::pi*std::atan(warped*(root+1)/(2*q));
        g.setColour(ink);g.setFont(font(12));g.drawText(pending?"LEARNED RANGE":"SIBILANCE RANGE",247,87,198,17,juce::Justification::centredLeft);g.setFont(font(19));g.drawText(juce::String(lo/1000,1)+" - "+juce::String(hi/1000,1)+" KHZ",247,109,198,27,juce::Justification::centredLeft);g.setFont(font(12));g.drawText("THRESHOLD  "+juce::String(level,1)+" DBFS",247,148,198,18,juce::Justification::centredLeft);
        g.setColour(dark);g.fillRoundedRectangle(294,289,104,14,4);g.setColour(sage.brighter(.3f));g.fillRoundedRectangle(296,291,std::clamp(p.reduction.load()/18.f,0.f,1.f)*100,10,3);g.setColour(ink);g.setFont(font(12));g.drawText("-"+juce::String(p.reduction.load(),1)+" DB",402,287,51,21,juce::Justification::centredRight);
    }
};
GillSmartDeEsserEditor::GillSmartDeEsserEditor(GillSmartDeEsserProcessor& p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);setResizable(true,true);setResizeLimits(480,360,960,720);if(auto* c=getConstrainer())c->setFixedAspectRatio(480./360);setSize(480,360);}
GillSmartDeEsserEditor::~GillSmartDeEsserEditor()=default;void GillSmartDeEsserEditor::paint(juce::Graphics& g){impl->paint(g);}void GillSmartDeEsserEditor::resized(){if(impl)impl->resized();}
