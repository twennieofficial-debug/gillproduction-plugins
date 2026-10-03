#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "../../GILLCommon/QualityUi.h"

namespace {
void text(juce::Graphics& g,const juce::String& value,juce::Rectangle<float> bounds,float size,juce::Colour colour=gill::prism::white,int align=juce::Justification::centred){
    g.setColour(colour);g.setFont(gill::prism::font(size));g.drawFittedText(value,bounds.toNearestInt(),align,1);
}
struct Control final : juce::Component {
    TextureParam spec;
    juce::Slider slider;
    juce::ComboBox choice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
    Control(GillTextureProcessor& p,TextureParam s):spec(std::move(s)) {
        if(spec.choices.isEmpty()){
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,92,19);slider.setTextValueSuffix(spec.suffix);slider.setName(spec.label);slider.setComponentID(spec.id);
            slider.setDoubleClickReturnValue(true,spec.initial);addAndMakeVisible(slider);
            sliderAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,spec.id,slider);
        }else{
            choice.setName(spec.label);choice.setComponentID(spec.id);choice.addItemList(spec.choices,1);addAndMakeVisible(choice);
            choiceAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,spec.id,choice);
        }
    }
    void paint(juce::Graphics& g)override{text(g,spec.label,{0,0,float(getWidth()),17},10,gill::prism::ink);}
    void resized()override{slider.setBounds(getLocalBounds().withTrimmedTop(17));choice.setBounds(7,41,getWidth()-14,29);}
};
}

struct GillTextureEditor::Impl final : juce::Component, private juce::Timer {
    GillTextureProcessor& p;
    gill::prism::Look look;
    gill::QualitySelector quality;
    juce::TextButton bypass{"BYPASS"},previous{"<"},next{">"};
    juce::ComboBox preset,pattern;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;
    std::vector<std::unique_ptr<Control>> controls;
    std::array<juce::Slider,16> steps;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,16> stepAttachments;
    juce::TooltipWindow tips{this,600};
    explicit Impl(GillTextureProcessor& processor):p(processor),quality(p.apvts,p),bypassAttachment(p.apvts,"bypass",bypass) {
        setLookAndFeel(&look);const auto info=textureInfo(p.kind);setSize(info.width,info.height);
        for(auto* c:std::initializer_list<juce::Component*>{&quality,&bypass,&preset,&previous,&next})addAndMakeVisible(c);
        bypass.setClickingTogglesState(true);bypass.setComponentID("bypass");preset.setComponentID("preset");preset.setName("PRESET");
        for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);
        preset.onChange=[this]{if(preset.getSelectedId()>0)p.selectPreset(preset.getSelectedId()-1);};
        previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+5)%6);};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%6);};
        for(const auto& spec:p.specs)if(!spec.hidden){auto c=std::make_unique<Control>(p,spec);addAndMakeVisible(*c);controls.push_back(std::move(c));}
        if(p.kind==TextureKind::Pulse){
            pattern.addItemList({"STRAIGHT","OFFBEAT","SYNCOPATED","RAMP"},1);pattern.setTextWhenNothingSelected("LOAD PATTERN");pattern.setName("PATTERN");pattern.setComponentID("pattern");
            pattern.onChange=[this]{if(pattern.getSelectedId()>0){p.applyPattern(pattern.getSelectedId()-1);pattern.setSelectedId(0,juce::dontSendNotification);}};addAndMakeVisible(pattern);
            for(int i=0;i<16;++i){auto& step=steps[i];step.setSliderStyle(juce::Slider::LinearVertical);step.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);step.setName("STEP "+juce::String(i+1));step.setComponentID("step"+juce::String(i+1));step.setDoubleClickReturnValue(true,1);step.setTooltip("Step "+juce::String(i+1)+": drag to set its volume. Double-click restores full level.");addAndMakeVisible(step);stepAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,step.getComponentID(),step);}
        }
        if(p.kind==TextureKind::Vocode){for(auto& c:controls){if(c->spec.id=="carrier")c->choice.setTooltip("SAW, PULSE and CHORD use an internal fixed-note carrier. EXTERNAL uses the CARRIER sidechain; connect an instrument in the host.");if(c->spec.id=="note")c->slider.setTooltip("Fixed carrier note in MIDI note numbers. This effect does not track or correct the singer's pitch.");}}
        if(p.kind==TextureKind::Grain)for(auto& c:controls)if(c->spec.id=="mix")c->slider.setTooltip("Blend original voice with grains from its recent history. Grains have intentional timing offsets; the direct path has no buffer delay.");
        resized();refresh();startTimerHz(24);
    }
    ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
    void refresh(){
        preset.setSelectedId(p.presetMatches()?p.getCurrentProgram()+1:0,juce::dontSendNotification);preset.setTextWhenNothingSelected("CUSTOM");
        for(auto& c:controls){if(c->spec.id=="bpm")c->setEnabled(!p.hostTempo.load());if(c->spec.id=="note")c->setEnabled(p.value("carrier")<2.5f);}
        repaint();
    }
    void timerCallback()override{refresh();}
    void resized()override{
        const int w=getWidth(),h=getHeight();quality.setBounds(w-206,17,117,28);bypass.setBounds(w-82,17,65,28);
        previous.setBounds(18,h-39,30,25);next.setBounds(w-48,h-39,30,25);preset.setBounds(55,h-39,w-110,25);
        const int first=p.kind==TextureKind::Vocode?191:p.kind==TextureKind::Grain?151:232;
        const int row=p.kind==TextureKind::Vocode?110:p.kind==TextureKind::Grain?110:93;
        for(std::size_t i=0;i<controls.size();++i){const int width=(w-42)/4;controls[i]->setBounds(21+int(i%4)*width,first+int(i/4)*row,width,row-5);}
        if(p.kind==TextureKind::Pulse){const int width=(w-48)/16;for(int i=0;i<16;++i)steps[i].setBounds(24+i*width,94,width,86);pattern.setBounds(w-220,197,196,27);}
    }
    void signalPanel(juce::Graphics& g,juce::Rectangle<float> r){
        gill::material::panel(g,r,gill::prism::dark,7,true);
        const auto area=r.reduced(12,10).withTrimmedTop(19);const auto end=p.scopePosition.load();
        g.setColour(gill::prism::white.withAlpha(.10f));g.drawHorizontalLine(int(area.getCentreY()),area.getX(),area.getRight());
        float maximum=.15f;for(auto& x:p.scope)maximum=std::max(maximum,std::abs(x.load()));
        juce::Path path;for(unsigned i=0;i<p.scope.size();++i){const auto value=p.scope[(end+i)%p.scope.size()].load();const float x=area.getX()+area.getWidth()*i/(p.scope.size()-1),y=area.getCentreY()-value/maximum*area.getHeight()*.44f;if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}
        g.setColour(gill::prism::cyan.withAlpha(.13f));g.strokePath(path,juce::PathStrokeType(6));g.setColour(gill::prism::cyan);g.strokePath(path,juce::PathStrokeType(1.4f));
        const juce::String title=p.kind==TextureKind::Vocode?"PROCESSED VOICE / OUTPUT":"OUTPUT TEXTURE / GRAIN CLOUD";
        text(g,title,{r.getX()+10,r.getY()+8,r.getWidth()-20,14},9,gill::prism::white,juce::Justification::left);
    }
    void paint(juce::Graphics& g)override{
        const float w=float(getWidth()),h=float(getHeight());const float deck=p.kind==TextureKind::Vocode?181:p.kind==TextureKind::Grain?141:230;
        gill::prism::chassis(g,w,h,deck);gill::prism::title(g,p.getName(),{65,13,w-280,30},23);
        text(g,textureInfo(p.kind).subtitle,{68,44,w-100,10},8,gill::prism::white.withAlpha(.70f),juce::Justification::left);
        if(p.kind!=TextureKind::Pulse){
            const float panelHeight=p.kind==TextureKind::Vocode?79:63;
            signalPanel(g,{22,68,w-158,panelHeight});gill::material::panel(g,{w-125,68,103,panelHeight},gill::prism::dark,7,true);
            const auto peak=[](float v){return v>1.e-6f?juce::String(20*std::log10(v),1):juce::String("--");};
            text(g,"OUTPUT",{w-118,77,88,14},9);text(g,peak(p.outputPeak.load())+" dB",{w-118,99,88,23},17,gill::prism::cyan);
            if(p.kind==TextureKind::Vocode){const bool ext=p.value("carrier")>2.5f;const auto status=ext?(p.carrierConnected?"EXTERNAL CARRIER CONNECTED":"CONNECT INSTRUMENT TO CARRIER SIDECHAIN"):"SING OR SPEAK / SET A FIXED CARRIER NOTE";text(g,status,{23,155,w-46,16},10,ext&&!p.carrierConnected?juce::Colour(0xffe8c47a):gill::prism::cyan);}
        }else{
            gill::material::panel(g,{18,65,w-36,124},gill::prism::dark,7,true);
            text(g,"EDIT 16 STEP LEVELS",{27,73,220,16},10,gill::prism::white,juce::Justification::left);
            text(g,juce::String(p.hostTempo?"HOST ":"MANUAL ")+juce::String(p.tempo.load(),1)+" BPM",{w-237,73,207,16},10,gill::prism::cyan,juce::Justification::right);
            const float sw=(w-48)/16;for(int i=0;i<16;++i){if(i==p.currentStep.load()){g.setColour(gill::prism::cyan.withAlpha(.11f));g.fillRoundedRectangle(25+i*sw,94,sw-2,85,3);}text(g,juce::String(i+1),{24+i*sw,174,sw,12},8,i%4==0?gill::prism::cyan:gill::prism::white.withAlpha(.65f));}
            text(g,"PATTERNS CHANGE STEP LEVELS / DRAG TO CUSTOMIZE",{25,200,w-268,22},9,gill::prism::cyan,juce::Justification::left);
        }
        text(g,juce::String(p.quality.isPro()?"PRO":"LIVE")+" / 0 SAMPLES ADDED BUFFER",{24,h-57,w-48,14},8,gill::prism::ink.withAlpha(.76f));
    }
};
GillTextureEditor::GillTextureEditor(GillTextureProcessor& p):AudioProcessorEditor(&p),impl(std::make_unique<Impl>(p)){
    addAndMakeVisible(*impl);const auto info=textureInfo(p.kind);setResizable(true,false);setResizeLimits(int(info.width*.85),int(info.height*.85),info.width*2,info.height*2);getConstrainer()->setFixedAspectRatio(double(info.width)/info.height);setSize(info.width,info.height);
}
GillTextureEditor::~GillTextureEditor()=default;
void GillTextureEditor::paint(juce::Graphics& g){g.fillAll(gill::prism::dark);}
void GillTextureEditor::resized(){if(impl){const auto info=textureInfo(impl->p.kind);impl->setBounds(0,0,info.width,info.height);impl->setTransform(juce::AffineTransform::scale(float(getWidth())/info.width,float(getHeight())/info.height));}}
