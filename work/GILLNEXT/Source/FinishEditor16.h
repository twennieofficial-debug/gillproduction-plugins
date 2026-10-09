#pragma once
#include "PluginProcessor.h"
#include "../../GILLCommon/MonolithUi.h"
#include "../../GILLCommon/QualityUi.h"
#include "../../GILLCommon/HostKeyboardPolicy.h"
#include <map>

// The mastering branch has a separate compact editor; all processor IDs stay intact.
class FinishEditor16 final : public juce::AudioProcessorEditor, private juce::Timer {
    GillNextProcessor& p;
    gill::monolith::Look look;
    gill::QualitySelector quality;
    juce::ComboBox page, preset, style;
    juce::TextButton learn{"LEARN"}, apply{"APPLY"}, revert{"REVERT"}, reset{"RESET METERS"};
    juce::Label status;
    struct Knob { juce::Slider slider; juce::Label label; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment; };
    struct Toggle { juce::TextButton button; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment; };
    std::map<juce::String,std::unique_ptr<Knob>> knobs;
    std::map<juce::String,std::unique_ptr<Toggle>> toggles;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment;
    std::unique_ptr<gill::HostKeyboardPolicy> keys;
    juce::TooltipWindow tooltip{this,650};
    void position(const char* id,int x,int y,int w,int h,bool horizontal=false) {
        auto& k=*knobs.at(id);const bool labelled=horizontal||juce::String(id)=="drive"||juce::String(id)=="ceiling";k.label.setVisible(labelled);k.slider.setVisible(true);
        k.label.setBounds(x,y,w,16);k.slider.setBounds(x,y+(labelled?17:0),w,h-(labelled?17:0));
        k.slider.setSliderStyle(horizontal?juce::Slider::LinearHorizontal:juce::Slider::Rotary);
        k.slider.setTextBoxStyle(horizontal?juce::Slider::TextBoxRight:juce::Slider::TextBoxBelow,false,horizontal?70:82,20);
    }
    void button(const char* id,int x,int y,int w,int h=28) {auto& b=toggles.at(id)->button;b.setVisible(true);b.setBounds(x,y,w,h);}
    void update() {
        const int s=p.learnState.load();learn.setButtonText(s==1?"FINISH":s==4?"CANCEL":"LEARN");
        apply.setEnabled(s==2);revert.setEnabled(s==3);
        juce::String message=p.statusText();
        if(s==1)message="LISTENING  "+juce::String(p.learnProgress.load()*300,1)+" S / 300 S";
        else if(s==4)message="ARMED / START THE SONG";
        else if(s==5)message="NOT ENOUGH AUDIO / PLAY & LEARN AGAIN";
        else if(s==2)message="READY  LOW "+juce::String(p.learnLow.load(),1)+"  MID "+juce::String(p.learnMid.load(),1)+"  HIGH "+juce::String(p.learnHigh.load(),1)+" dB";
        status.setText(message,juce::dontSendNotification);status.setTooltip(message);
        preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);
        preset.setText(p.getProgramName(p.getCurrentProgram())+(p.presetMatches()?"":" *"),juce::dontSendNotification);repaint();
    }
    void timerCallback() override {update();}
public:
    explicit FinishEditor16(GillNextProcessor& processor):AudioProcessorEditor(processor),p(processor),quality(p.apvts,p) {
        setLookAndFeel(&look);quality.setLookAndFeel(&look);setOpaque(true);setWantsKeyboardFocus(false);
        for(auto& d:p.definitions){const juce::String id(d.id);if(d.choices.empty()){
            auto k=std::make_unique<Knob>();k->slider.setComponentID(id);k->slider.setName(d.name);k->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);k->slider.setTextValueSuffix(d.unit);
            auto* parameter=p.apvts.getParameter(id);k->slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
            k->label.setText(d.name,juce::dontSendNotification);k->label.setJustificationType(juce::Justification::centred);k->label.setColour(juce::Label::textColourId,gill::monolith::cream);k->label.setFont(juce::FontOptions(11));k->label.setInterceptsMouseClicks(false,false);
            addAndMakeVisible(k->slider);addAndMakeVisible(k->label);k->attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,k->slider);knobs.emplace(id,std::move(k));
        }else if(d.choices.size()==2){auto t=std::make_unique<Toggle>();t->button.setComponentID(id);t->button.setName(d.name);t->button.setButtonText(d.name);t->button.setClickingTogglesState(true);addAndMakeVisible(t->button);t->attachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,id,t->button);toggles.emplace(id,std::move(t));}
        else if(id=="style"){style.setComponentID(id);style.setName(d.name);for(size_t i=0;i<d.choices.size();++i)style.addItem(d.choices[i],int(i)+1);styleAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,id,style);}}
        for(auto* c:std::array<juce::Component*,9>{&quality,&page,&preset,&style,&learn,&apply,&revert,&reset,&status})addAndMakeVisible(c);
        page.setComponentID("finishPage");page.addItemList({"TONE","DYNAMICS","STEREO / OUTPUT"},1);page.setSelectedId(1);page.onChange=[this]{resized();repaint();};
        for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.setComponentID("preset");preset.setName("PRESET");preset.onChange=[this]{if(preset.getSelectedId()>0)p.selectPreset(preset.getSelectedId()-1);};
        learn.setComponentID("learn");learn.onClick=[this]{if(p.learnState==1||p.learnState==4)p.stopLearn();else p.startLearn();};
        apply.setComponentID("apply");apply.onClick=[this]{p.applyLearn();};revert.setComponentID("revert");revert.onClick=[this]{p.revertLearn();};reset.setComponentID("resetMeters");reset.onClick=[this]{p.resetMeters();};
        status.setColour(juce::Label::textColourId,gill::monolith::gold);status.setJustificationType(juce::Justification::centred);status.setFont(juce::FontOptions(11));
        toggles.at("match")->button.setTooltip("Matches output loudness for comparison. Switch off to hear final mastering loudness.");
        knobs.at("boost")->slider.setTooltip("Additional gain before the limiter. CEILING sets the peak limit.");
        setResizable(true,true);setResizeLimits(620,660,1240,1320);if(auto* c=getConstrainer())c->setFixedAspectRatio(620./660.);setSize(620,660);keys=std::make_unique<gill::HostKeyboardPolicy>(*this);update();startTimerHz(25);
    }
    ~FinishEditor16() override {stopTimer();keys.reset();setLookAndFeel(nullptr);}
    void resized() override {
        quality.setBounds(372,66,110,26);button("bypass",493,66,105,26);
        learn.setBounds(22,111,103,28);apply.setBounds(137,111,103,28);revert.setBounds(252,111,103,28);reset.setBounds(449,111,149,28);status.setBounds(22,146,576,22);
        for(auto& item:knobs){item.second->slider.setVisible(false);item.second->label.setVisible(false);}for(auto& item:toggles)if(item.first!="bypass")item.second->button.setVisible(false);
        const int selected=page.getSelectedId();
        if(selected==1){button("toneon",39,194,131);position("low",264,184,123,99);position("mid",264,299,123,99);position("high",264,414,123,99);}
        else if(selected==2){button("compon",39,191,132);position("comp",279,178,109,90);button("clipon",39,298,132);position("clip",279,285,109,90);button("limiteron",32,386,140,24);position("drive",181,384,96,92);position("ceiling",289,384,111,92);position("boost",35,472,369,43,true);}
        else{button("stereoon",39,194,138);position("width",228,189,164,82,true);position("bassmono",228,304,164,82,true);button("match",39,441,156);button("mono",211,441,156);}
        page.setBounds(22,602,169,29);style.setBounds(202,602,150,29);preset.setBounds(363,602,235,29);
        for(auto* c:getChildren())if(!dynamic_cast<juce::ResizableCornerComponent*>(c))c->setTransform(juce::AffineTransform::scale(getWidth()/620.f));
    }
    void paint(juce::Graphics& g) override {
        using namespace gill::monolith;g.addTransform(juce::AffineTransform::scale(getWidth()/620.f));chassis(g,620,660,"GILLFINISH");frame(g,620,660,176,398,525);const int selected=page.getSelectedId();
        if(selected==2){surface(g,{22,176,398,96},20);surface(g,{22,282,398,96},20);surface(g,{22,388,398,137},20);}
        else for(int i=0;i<3;++i)surface(g,{22,float(176+i*115),398,107},20);
        if(selected==1){brass(g,"LOW",{42,235,130,25},20);brass(g,"MID",{42,318,130,25},20);brass(g,"HIGH",{42,433,130,25},20);}
        else if(selected==2){brass(g,"GLUE",{42,231,150,24},17);brass(g,"CLIP",{42,338,150,24},17);}
        else{brass(g,"WIDTH",{42,236,130,25},20);brass(g,"MONO BASS",{42,327,170,25},19);text(g,"COMPARE / MONITOR",{35,408,365,22},11);}
        meterWell(g,{439,176,159,349},p.inputPeak.load(),p.outputPeak.load(),p.limiterReduction.load(),115);
        const auto number=[](float v){return v<=-90?juce::String("--"):juce::String(v,1);};
        text(g,"LUFS I  "+number(p.integratedLufs.load()),{451,397,135,20},12,gold);
        text(g,"LUFS M  "+number(p.momentaryLufs.load()),{451,422,135,20},12,gold);
        text(g,number(p.truePeakDb.load())+" dBTP",{451,450,135,20},12,gold);
        text(g,"GLUE "+juce::String(p.compressorReduction.load(),1)+" dB",{451,476,135,18},10,cream);
        if(p.value("ceiling")>0)text(g,"OVER 0 dBFS",{451,531,135,15},10,juce::Colour(0xffffadb4));
        auto r=juce::Rectangle<float>(22,539,576,45);g.setColour(juce::Colour(0xff031014));g.fillRoundedRectangle(r,8);auto data=p.graph();juce::Path curve;for(size_t i=0;i<data.size();++i){float x=r.getX()+float(i)*r.getWidth()/float(data.size()-1),y=juce::jlimit(r.getY()+2,r.getBottom()-2,r.getCentreY()-data[i]*r.getHeight()/24);if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}g.setColour(gold);g.strokePath(curve,juce::PathStrokeType(1.3f));text(g,"TONE RESPONSE",{28,542,132,12},9,cream.withAlpha(.6f),juce::Justification::left);
        if(!p.rateSupported.load())text(g,"UNSUPPORTED RATE / BYPASS",{22,635,576,17},11,juce::Colours::orange);
    }
};
