#include "PluginEditor.h"
#include "../../GILLCommon/MonolithUi.h"
#include <map>
namespace {
using namespace gill::monolith;
using MasterLook=gill::monolith::Look;
struct Control:juce::Component {
    MasterParam spec;juce::Slider slider;juce::ComboBox combo;juce::TextButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>sa;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>ca;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>ba;
    bool compact=false;
    Control(GillMasterProcessor&p,MasterParam s):spec(std::move(s)){
        if(spec.flag){button.setButtonText(spec.label);button.setClickingTogglesState(true);addAndMakeVisible(button);ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,spec.id,button);}
        else if(!spec.choices.isEmpty()){combo.addItemList(spec.choices,1);addAndMakeVisible(combo);ca=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,spec.id,combo);}
        else {slider.setSliderStyle(juce::Slider::Rotary);slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,74,20);slider.setTextValueSuffix(spec.suffix);slider.setDoubleClickReturnValue(true,spec.initial);slider.setName(spec.label);slider.setTooltip(spec.label+". Drag the dial, scroll, or enter a value. Double-click restores the default.");addAndMakeVisible(slider);sa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,spec.id,slider);}
        setName(spec.label);
    }
    void small(bool yes=true){compact=yes;if(!spec.flag&&spec.choices.isEmpty()){slider.setSliderStyle(yes?juce::Slider::LinearHorizontal:juce::Slider::Rotary);slider.setTextBoxStyle(yes?juce::Slider::TextBoxRight:juce::Slider::TextBoxBelow,false,yes?61:74,19);}resized();}
    void resized()override{auto r=getLocalBounds();if(spec.flag)button.setBounds(r);else{if(compact&&getHeight()<35)r.removeFromLeft(54);else if(compact||!spec.choices.isEmpty())r.removeFromTop(17);if(!spec.choices.isEmpty())combo.setBounds(r.withHeight(std::min(25,r.getHeight())));else slider.setBounds(r);}}
    void paint(juce::Graphics&g)override{if(!spec.flag&&(compact||!spec.choices.isEmpty())){if(compact&&getHeight()<35)text(g,spec.label,{0,0,50,float(getHeight())},9,cream);else text(g,spec.label,{0,0,float(getWidth()),16},12,cream);}}
};

}
struct GillMasterEditor::Impl:juce::Component,private juce::Timer {
    GillMasterEditor&owner;GillMasterProcessor&p;MasterLook look;
    std::map<juce::String,std::unique_ptr<Control>>controls;
    juce::TextButton live{"LIVE"},pro{"PRO"},bypass{"BYPASS"},previous{"<"},next{">"},learn{"LEARN"},analyze{"ANALYZE"},stop{"STOP"},reset{"RESET"},report{"REPORT"};
    juce::ComboBox presets;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>bypassAttachment;
    std::unique_ptr<juce::FileChooser>chooser;juce::TooltipWindow tips{&owner,600};
    Impl(GillMasterEditor&o,GillMasterProcessor&processor):owner(o),p(processor){
        const auto info=masterInfo(p.kind);setSize(600,780);setLookAndFeel(&look);
        for(const auto&s:p.specs)addControl(s);
        if(int(p.kind)<6){addControl({"output","OUTPUT",-18,12.f,.01f,0," dB",{},false});addControl({"mix","MIX",0,100,.1f,100," %",{},false});}
        for(auto*b:{&live,&pro,&bypass,&previous,&next})addAndMakeVisible(*b);
        live.onClick=[this]{p.setValue("gillQuality",0);};pro.onClick=[this]{p.setValue("gillQuality",1);};bypass.setClickingTogglesState(true);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);
        for(int i=0;i<6;++i)presets.addItem(p.getProgramName(i),i+1);presets.onChange=[this]{p.selectPreset(presets.getSelectedId()-1);};addAndMakeVisible(presets);
        previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+5)%6);};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%6);};
        if(p.delta){addAndMakeVisible(learn);learn.onClick=[this]{p.delta->learn();};learn.setTooltip("Put SOURCE before the chain and RETURN after it, select the same PAIR, then play audio and LEARN. Up to 500 ms delay. Links stay inside one host process. Offline export always uses AFTER.");}
        if(p.deliver){for(auto*b:{&analyze,&stop,&reset,&report})addAndMakeVisible(*b);analyze.onClick=[this]{p.deliver->start();};stop.onClick=[this]{p.deliver->stop();};reset.onClick=[this]{p.deliver->clear();};report.onClick=[this]{saveReport();};}
        if(p.kind==MasterKind::Ceiling){addControl({"boost","BOOST",0,18,.1f,0," dB",{},false});controls.at("boost")->slider.setTooltip("Additional gain BEFORE the limiter. DRIVE + BOOST reaches +42 dB. Start low; compare with GAIN MATCH off when judging final loudness.");controls.at("ceiling")->slider.setTooltip("Peak ceiling up to +6 dBFS. Positive values exceed digital full scale; use BOOST for more limiting and watch the OVER 0 indicator.");controls.at("drive")->slider.setTooltip("Raises loudness into the limiter. Use BOOST for extra input reserve. CEILING sets the peak limit, not the loudness target.");controls.at("match")->button.setTooltip("Level-matched comparison only. Switch OFF to hear and export the full loudness increase.");addAndMakeVisible(reset);reset.onClick=[this]{p.resetMeterRequested=true;};}
        if(p.kind==MasterKind::Width){controls.at("guard")->button.setButtonText("GUARD");controls.at("guard")->button.setTooltip("Correlation guard: constrain widening when stereo correlation would become negative.");}
        refresh();startTimerHz(25);resized();
    }
    ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
    void addControl(const MasterParam&s){auto c=std::make_unique<Control>(p,s);addAndMakeVisible(*c);controls.emplace(s.id,std::move(c));}
    void place(const char*id,int x,int y,int w,int h,bool small=false){auto it=controls.find(id);if(it==controls.end())return;it->second->small(small);it->second->setBounds(x,y,w,h);}
    void resized()override{
        const int w=getWidth(),h=getHeight();
        live.setBounds(w-186,68,48,22);pro.setBounds(w-132,68,48,22);bypass.setBounds(w-78,68,62,22);
        previous.setBounds(21,100,28,23);next.setBounds(w-49,100,28,23);presets.setBounds(56,100,w-112,23);
        auto main=[&](const char*id,int row){place(id,130,134+row*132,130,118);};
        auto side=[&](const char*id,int row,int line){place(id,268,157+row*132+line*45,105,40,true);};
        if(int(p.kind)<6){place("output",159,h-218,176,160);place("mix",394,h-178,174,48,true);}
        switch(p.kind){
        case MasterKind::Ceiling:main("drive",0);main("release",1);main("ceiling",2);side("boost",0,0);side("character",1,0);place("match",271,332,99,26);reset.setBounds(424,h-99,116,25);break;
        case MasterKind::Low:main("amount",0);main("threshold",1);main("width",2);side("frequency",0,0);side("protect",1,0);place("listen",270,465,100,27);break;
        case MasterKind::Glue:main("amount",0);main("attack",1);main("release",2);side("character",0,0);side("detector",1,0);place("match",270,465,100,27);break;
        case MasterKind::Width:main("low",0);main("mid",1);main("high",2);side("lowHz",0,0);side("highHz",2,0);place("guard",268,307,106,28);place("mono",270,343,100,27);break;
        case MasterKind::Punch:main("low",0);main("mid",1);main("high",2);side("lowHz",0,0);side("sustain",1,0);side("highHz",2,0);break;
        case MasterKind::Weight:main("amount",0);main("frequency",1);place("colour",149,438,213,53);place("speaker",268,305,106,28);break;
        case MasterKind::Delta:place("role",150,171,213,48);place("pair",150,303,105,48);learn.setBounds(268,320,106,29);place("audition",150,435,213,48);place("match",394,h-164,174,28);break;
        case MasterKind::Deliver:main("target",0);main("peakTarget",1);main("silence",2);analyze.setBounds(394,h-187,174,30);stop.setBounds(394,h-148,80,28);reset.setBounds(483,h-148,85,28);report.setBounds(394,h-106,174,28);break;
        }
    }
    void refresh(){const bool isPro=p.value("gillQuality")>.5f;live.setToggleState(!isPro,juce::dontSendNotification);pro.setToggleState(isPro,juce::dontSendNotification);const auto latency=juce::String(p.getLatencySamples())+" samples / "+juce::String(1000.*p.getLatencySamples()/p.rateView.load(),2)+" ms";live.setTooltip("LIVE: zero added algorithmic latency. "+latency);pro.setTooltip("PRO: full processing quality. "+latency);presets.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);if(p.delta){learn.setButtonText(p.delta->learning()?"LEARNING":"LEARN");learn.setEnabled(p.value("role")<.5f);}if(p.deliver){const auto s=p.deliver->snapshot();analyze.setToggleState(s.running,juce::dontSendNotification);report.setEnabled(s.duration>0);}repaint();}
    void timerCallback()override{refresh();}
    void saveReport(){const auto content=p.deliveryReport();chooser=std::make_unique<juce::FileChooser>("SAVE GILLDELIVER REPORT",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GILLDELIVER-REPORT.txt"),"*.txt");const juce::Component::SafePointer<Impl>safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,content](const juce::FileChooser&f){if(safe){const auto file=f.getResult();if(file!=juce::File{}&&!file.replaceWithText(content))juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"REPORT","The report could not be saved to this location.");}});}

    void paint(juce::Graphics&g)override{
        const float w=float(getWidth()),h=float(getHeight());chassis(g,w,h,p.getName());frame(g,w,h,132,362,518);
        const char* labels[8][3]={{"DRIVE","RELEASE","LIMIT"},{"TIGHT","CONTROL","WIDTH"},{"GLUE","ATTACK","RELEASE"},{"LOW","MID","HIGH"},{"LOW","MID","HIGH"},{"WEIGHT","TUNE","TONE"},{"ROUTE","ALIGN","AUDITION"},{"LOUDNESS","PEAK","SILENCE"}};
        const char* captions[8][3]={{"BUILD LEVEL","RECOVER","CONTROL PEAKS"},{"FOCUS BASS","DYNAMICS","LOW FIELD"},{"BRING TOGETHER","KEEP PUNCH","RECOVER"},{"BASS FIELD","BODY FIELD","AIR FIELD"},{"LOW ATTACK","MID ATTACK","HIGH ATTACK"},{"HARMONICS","FUNDAMENTAL","CHARACTER"},{"CHAIN POSITION","SOURCE PAIR","COMPARE"},{"TARGET LUFS","TARGET DBTP","TRIM CHECK"}};
        g.setColour(champagne);g.drawLine(49,151,49,529,1.4f);
        for(int row=0;row<3;++row){const float y=132.f+row*132;surface(g,{21,y,362,122},22);brass(g,labels[int(p.kind)][row],{36,y+29,91,23},row==0&&p.deliver?15.f:19.f,champagne,juce::Justification::left);text(g,captions[int(p.kind)][row],{36,y+60,92,24},9,cream.withAlpha(.7f),juce::Justification::left);g.setColour(gold.withAlpha(.15f));g.fillEllipse(25,y+13,13,13);g.setColour(gold);g.fillEllipse(29,y+17,5,5);}
        if(p.deliver){const auto s=p.deliver->snapshot();text(g,s.running?"CAPTURING":"READY",{268,176,105,24},11,gold);text(g,s.hasAudio&&s.peakDb<=p.value("peakTarget")?"PEAK OK":"CHECK PEAK",{268,308,105,22},10,gold);text(g,juce::String(juce::int64(s.clipped))+" CLIPS",{268,337,105,20},10);text(g,"START / END",{265,430,110,20},10);text(g,juce::String(s.leading,2)+" / "+juce::String(s.trailing,2)+" S",{258,454,119,21},10,gold);}
        meterWell(g,{397,132,w-418,386},p.inputPeak.load(),p.outputPeak.load(),p.reduction.load());
        surface(g,{21,h-241,w-42,210},34);brass(g,int(p.kind)<6?"OUTPUT":p.delta?"MONITOR":"MEASURE",{37,h-178,116,31},21,champagne,juce::Justification::left);
        if(p.delta){const auto a=p.delta->alignment();text(g,dbLabel(p.outputPeak.load()),{159,h-191,179,57},43,gold);text(g,"OUTPUT PEAK / DBFS",{159,h-131,179,21},10);const juce::String status=p.delta->conflict()?"PAIR CONFLICT":p.value("role")>.5f?"SOURCE BEFORE CHAIN":!p.delta->linked()?"WAITING FOR SOURCE":p.delta->learning()?"LEARNING":a.valid?juce::String(a.delay)+" SAMPLES / "+juce::String(20*std::log10(a.gain),1)+" DB":"PLAY AUDIO + LEARN";text(g,status,{37,h-78,w-74,20},11,gold);}
        else if(p.deliver){const auto s=p.deliver->snapshot();text(g,s.integrated>-99?juce::String(s.integrated,1):"--",{159,h-198,179,48},36,gold);text(g,"INTEGRATED LUFS",{159,h-149,179,18},10);text(g,(s.hasAudio?juce::String(s.peakDb,1):"--")+(s.truePeak?" DBTP":" DBFS"),{159,h-124,179,24},17,gold);text(g,juce::String(s.duration,1)+" S / LRA "+(s.duration>=3?juce::String(s.range,1):"--"),{35,h-70,330,18},10);}
        else{text(g,"MASTER LEVEL",{37,h-140,116,18},10,cream.withAlpha(.65f),juce::Justification::left);const auto measure=p.kind==MasterKind::Width?"CORRELATION "+juce::String(p.correlation.load(),2):"REDUCTION "+juce::String(p.reduction.load(),1)+" DB";text(g,measure,{392,h-219,178,20},10,gold);if(p.kind==MasterKind::Ceiling)text(g,p.integrated.load()>-99?juce::String(p.integrated.load(),1)+" LUFS":"-- LUFS",{394,h-126,174,22},17,gold);}
        if(int(p.kind)<6)for(int n=0;n<15;++n){juce::Path wave;wave.startNewSubPath(343,710+n*2.2f);wave.cubicTo(409,692+n*3.1f,462,765-n*.5f,572,712+n*2.2f);g.setColour(champagne.withAlpha(.09f+.003f*n));g.strokePath(wave,juce::PathStrokeType(.55f));}
        text(g,juce::String(p.quality.isPro()?"PRO":"LIVE")+" / "+juce::String(p.getLatencySamples())+" SAMPLES",{26,h-24,w-52,12},9,cream.withAlpha(.65f));
    }
};
GillMasterEditor::GillMasterEditor(GillMasterProcessor&p):AudioProcessorEditor(&p),impl(std::make_unique<Impl>(*this,p)){
    addAndMakeVisible(*impl);const auto info=masterInfo(p.kind);setResizable(true,false);setResizeLimits(info.width,info.height,info.width*2,info.height*2);getConstrainer()->setFixedAspectRatio(double(info.width)/info.height);setSize(info.width,info.height);
}
GillMasterEditor::~GillMasterEditor()=default;
void GillMasterEditor::paint(juce::Graphics&g){g.fillAll(ink);}
void GillMasterEditor::resized(){if(impl){impl->setBounds(0,0,600,780);impl->setTransform(juce::AffineTransform::scale(float(getWidth())/600,float(getHeight())/780));}}
