#include "PluginEditor.h"
#include "../../GILLCommon/TwinLensUi.h"
#include "../../GILLCommon/PrismUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"

namespace {
const juce::Colour cream(0xffe2ecf6),ink(0xff102237),sage(0xff54b8d5),dark(0xff091d31);
juce::Font font(float h,bool bold=true){return juce::Font(juce::FontOptions(gillInterfaceFontName(),h,bold?juce::Font::bold:juce::Font::plain));}
class MaterialLook final : public gill::twin::Look {  };
}
struct GillSmartDeEsserEditor::Impl:private juce::Timer{
    GillSmartDeEsserEditor& owner;GillSmartDeEsserProcessor& p;MaterialLook look;juce::Component canvas;juce::Slider amount;gill::twin::History detector,reduction;
    juce::TextButton learn{"LEARN"},apply{"APPLY"},undo{"UNDO"},listen{"LISTEN REMOVED"},bypass{"BYPASS"};juce::ComboBox preset;juce::Label status;
    gill::QualitySelector quality{p.apvts,p};juce::Image wood;juce::TooltipWindow tips;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment,bypassAttachment;
    Impl(GillSmartDeEsserEditor& o,GillSmartDeEsserProcessor& v):owner(o),p(v),tips(&o,600){
        reduction.values.fill(0);owner.setLookAndFeel(&look);owner.addAndMakeVisible(canvas);canvas.setInterceptsMouseClicks(false,true);
        for(auto* c:std::initializer_list<juce::Component*>{&amount,&learn,&apply,&undo,&listen,&bypass,&preset,&status,&quality})canvas.addAndMakeVisible(*c);
        wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);
        amount.setName("AMOUNT");amount.setComponentID("amount");amount.setSliderStyle(juce::Slider::LinearVertical);amount.setTextBoxStyle(juce::Slider::TextBoxBelow,false,110,28);amount.setTextValueSuffix(" %");amount.setDoubleClickReturnValue(true,55);amount.setTooltip("Strength of sibilance reduction. Double-click: 55%.");amountAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"amount",amount);
        learn.setName("learn");learn.onClick=[this]{if(p.learnState.load()==GillSmartDeEsserProcessor::learning||p.learnState.load()==GillSmartDeEsserProcessor::armed)p.finishLearning();else p.startLearning();};learn.setTooltip("LEARN, play the whole isolated vocal, then stop. Up to 5 minutes; STOP finishes early. APPLY auditions the learned profile.");
        apply.onClick=[this]{p.applyLearned();};apply.setTooltip("Apply the measured range and threshold. The previous profile can be restored with UNDO.");undo.onClick=[this]{p.undoLearned();};
        listen.setName("listen");bypass.setName("bypass");listen.setClickingTogglesState(true);bypass.setClickingTogglesState(true);listenAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"listen",listen);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);listen.setTooltip("Audition exactly the audio removed by the de-esser. Switch off for normal playback.");
        for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.setName("PRESET");preset.onChange=[this]{if(preset.getSelectedId()>0)p.setCurrentProgram(preset.getSelectedId()-1);};
        status.setFont(font(12));status.setColour(juce::Label::textColourId,ink);status.setJustificationType(juce::Justification::centredLeft);refresh();startTimerHz(20);
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    void resized(){canvas.setBounds(0,0,740,450);canvas.setTransform(juce::AffineTransform::scale(owner.getWidth()/740.f));quality.setBounds(508,21,110,26);bypass.setBounds(635,20,83,28);amount.setBounds(322,162,96,154);learn.setBounds(325,100,90,40);apply.setBounds(284,356,80,29);undo.setBounds(375,356,80,29);status.setBounds(100,326,540,26);listen.setBounds(29,402,160,28);preset.setBounds(206,402,509,28);}
    void refresh(){detector.push(p.sibilance.load());reduction.push(-p.reduction.load());const int state=p.learnState.load();learn.setButtonText(state==GillSmartDeEsserProcessor::learning?"STOP":state==GillSmartDeEsserProcessor::armed?"CANCEL":"LEARN");apply.setEnabled(state==GillSmartDeEsserProcessor::ready);undo.setEnabled(p.canUndo());
        status.setText(!p.supported.load()?"UNSUPPORTED RATE / BYPASS":state==GillSmartDeEsserProcessor::armed?"ARMED / PRESS PLAY":state==GillSmartDeEsserProcessor::learning?"LISTENING  "+juce::String(p.elapsedSeconds.load(),1)+" S / MAX 300":state==GillSmartDeEsserProcessor::ready?"PROFILE READY\nAPPLY TO HEAR":state==GillSmartDeEsserProcessor::insufficient?"MORE VOICE + S SOUNDS NEEDED":state==GillSmartDeEsserProcessor::applied?"LEARNED PROFILE ACTIVE":"PLAY VOCAL + LEARN",juce::dontSendNotification);
        preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);owner.repaint();}
    void timerCallback()override{refresh();}
    void paint(juce::Graphics& g){g.addTransform(juce::AffineTransform::scale(owner.getWidth()/740.f));gill::twin::chassis(g,740,450,"GILLSMARTDEESSER");gill::twin::levelLens(g,{22,65,270,270},"DETECTOR",detector,"SIBILANCE DBFS / 5 S");auto plot=gill::twin::lens(g,{448,65,270,270},"REDUCTION","ACTUAL GAIN REDUCTION / DB");gill::twin::trace(g,plot,reduction.values,-18,0);g.setColour(juce::Colours::white);g.setFont(font(15));g.drawText("-"+juce::String(p.reduction.load(),1)+" dB",plot.withY(plot.getBottom()+3).withHeight(20),juce::Justification::centred);
        gillsmart::Profile candidate;const bool pending=p.learnedCandidate(candidate);const double hz=pending?candidate.frequency:p.value("frequency"),level=pending?candidate.threshold:p.value("threshold"),q=pending?candidate.q:p.value("q");const double fs=p.uiRate.load(),warped=std::tan(gillsmart::pi*gillsmart::effectiveFrequency(hz,fs)/fs),root=std::sqrt(4*q*q+1),lo=fs/gillsmart::pi*std::atan(warped*(root-1)/(2*q)),hi=fs/gillsmart::pi*std::atan(warped*(root+1)/(2*q));g.setColour(gill::twin::ink);g.setFont(font(11));g.drawText("AMOUNT",322,143,96,18,juce::Justification::centred);g.drawText((pending?"CANDIDATE  ":"ACTIVE  ")+juce::String(lo/1000,1)+" - "+juce::String(hi/1000,1)+" kHz / THR "+juce::String(level,1)+" dB",115,384,510,16,juce::Justification::centred);
    }
};
GillSmartDeEsserEditor::GillSmartDeEsserEditor(GillSmartDeEsserProcessor& p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);setResizable(true,true);setResizeLimits(740,450,1480,900);if(auto* c=getConstrainer())c->setFixedAspectRatio(740./450);setSize(740,450);}
GillSmartDeEsserEditor::~GillSmartDeEsserEditor()=default;void GillSmartDeEsserEditor::paint(juce::Graphics& g){impl->paint(g);}void GillSmartDeEsserEditor::resized(){if(impl)impl->resized();}


