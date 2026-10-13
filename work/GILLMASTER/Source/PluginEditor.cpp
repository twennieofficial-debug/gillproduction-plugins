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
        setName(spec.label);setComponentID("control:"+spec.id);slider.setComponentID(spec.id);combo.setComponentID(spec.id);button.setComponentID(spec.id);
    }
    void small(bool yes=true){compact=yes;if(!spec.flag&&spec.choices.isEmpty()){slider.setSliderStyle(yes?juce::Slider::LinearHorizontal:juce::Slider::Rotary);slider.setTextBoxStyle(yes?juce::Slider::TextBoxRight:juce::Slider::TextBoxBelow,false,yes?61:74,19);}resized();}
    void resized()override{auto r=getLocalBounds();if(spec.flag)button.setBounds(r);else{if(compact&&getHeight()<35)r.removeFromLeft(54);else r.removeFromTop(17);if(!spec.choices.isEmpty())combo.setBounds(r.withHeight(std::min(25,r.getHeight())));else slider.setBounds(r);}}
    void paint(juce::Graphics&g)override{if(!spec.flag){if(compact&&getHeight()<35)text(g,spec.label,{0,0,50,float(getHeight())},9,cream);else text(g,spec.label,{0,0,float(getWidth()),16},12,cream);}}
};

}
struct GillMasterEditor::Impl:juce::Component,private juce::Timer {
    GillMasterEditor&owner;GillMasterProcessor&p;MasterLook look;
    std::map<juce::String,std::unique_ptr<Control>>controls;
    juce::TextButton live{"LIVE"},pro{"PRO"},bypass{"BYPASS"},previous{"<"},next{">"},learn{"LEARN"},analyze{"ANALYZE"},stop{"STOP"},reset{"RESET"},report{"REPORT"};
    juce::ComboBox presets;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>bypassAttachment;
    std::unique_ptr<juce::FileChooser>chooser;juce::TooltipWindow tips{&owner,600};
    Impl(GillMasterEditor&o,GillMasterProcessor&processor):owner(o),p(processor){
        const auto info=masterInfo(p.kind);setSize(info.width,info.height);setLookAndFeel(&look);
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
        const int w=getWidth();
        live.setBounds(w-186,68,48,22);pro.setBounds(w-132,68,48,22);bypass.setBounds(w-78,68,62,22);
        previous.setBounds(23,112,28,24);next.setBounds(w-51,112,28,24);presets.setBounds(60,112,w-120,24);
        switch(p.kind){
        case MasterKind::Ceiling:
            place("drive",35,183,137,139);place("ceiling",190,183,137,139);
            place("boost",39,335,132,45,true);place("release",189,335,139,45,true);
            place("character",39,396,145,43);place("match",201,410,128,28);reset.setBounds(430,519,93,27);
            place("output",38,506,175,40,true);place("mix",231,506,177,40,true);break;
        case MasterKind::Low:
            place("frequency",37,182,133,135);place("amount",191,182,128,135);
            place("threshold",355,189,121,114);place("protect",490,190,127,45,true);place("width",490,253,127,45,true);place("listen",365,319,122,26);
            place("output",37,379,211,40,true);place("mix",385,379,231,40,true);break;
        case MasterKind::Glue:
            place("amount",38,182,155,151);place("attack",225,197,101,105);place("release",339,197,104,105);
            place("character",227,323,214,43);place("detector",39,347,155,45,true);place("match",467,370,117,26);
            place("output",38,413,238,31,true);place("mix",310,413,264,31,true);break;
        case MasterKind::Width:
            place("low",68,185,130,126);place("mid",305,185,130,126);place("high",540,185,130,126);
            place("lowHz",42,326,185,35,true);place("highHz",511,326,185,35,true);
            place("guard",260,332,99,26);place("mono",371,332,109,26);
            place("output",351,384,171,31,true);place("mix",540,384,164,31,true);break;
        case MasterKind::Punch:
            place("low",58,178,137,122);place("mid",281,178,137,122);place("high",503,178,137,122);
            place("lowHz",38,337,193,43,true);place("sustain",253,337,193,43,true);place("highHz",470,337,191,43,true);
            place("output",40,407,267,29,true);place("mix",364,407,295,29,true);break;
        case MasterKind::Weight:
            place("amount",38,173,170,161);place("frequency",242,175,122,111);
            place("speaker",242,292,208,26);place("colour",242,326,208,43);
            place("output",38,378,196,29,true);place("mix",259,378,194,29,true);break;
        case MasterKind::Delta:
            place("role",40,182,173,46);place("pair",245,182,105,46);learn.setBounds(381,199,126,29);
            place("audition",40,281,258,55);place("match",333,310,174,28);break;
        case MasterKind::Deliver:
            place("target",42,178,156,100);place("peakTarget",42,304,156,44,true);place("silence",42,384,156,44,true);
            analyze.setBounds(255,411,124,30);stop.setBounds(391,411,78,30);reset.setBounds(481,411,78,30);report.setBounds(571,411,88,30);break;
        }
    }
    void refresh(){const bool isPro=p.value("gillQuality")>.5f;live.setToggleState(!isPro,juce::dontSendNotification);pro.setToggleState(isPro,juce::dontSendNotification);const auto latency=juce::String(p.getLatencySamples())+" samples / "+juce::String(1000.*p.getLatencySamples()/p.rateView.load(),2)+" ms";live.setTooltip("LIVE: zero added algorithmic latency. "+latency);pro.setTooltip("PRO: full processing quality. "+latency);presets.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);if(p.delta){learn.setButtonText(p.delta->learning()?"LEARNING":"LEARN");learn.setEnabled(p.value("role")<.5f);}if(p.deliver){const auto s=p.deliver->snapshot();analyze.setToggleState(s.running,juce::dontSendNotification);report.setEnabled(s.duration>0);}repaint();}
    void timerCallback()override{refresh();}
    void saveReport(){const auto content=p.deliveryReport();chooser=std::make_unique<juce::FileChooser>("SAVE GILLDELIVER REPORT",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GILLDELIVER-REPORT.txt"),"*.txt");const juce::Component::SafePointer<Impl>safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,content](const juce::FileChooser&f){if(safe){const auto file=f.getResult();if(file!=juce::File{}&&!file.replaceWithText(content))juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"REPORT","The report could not be saved to this location.");}});}

    void panel(juce::Graphics&g,juce::Rectangle<float>r,const juce::String&title){surface(g,r,18);brass(g,title,{r.getX()+16,r.getY()+8,r.getWidth()-32,20},14);}
    void number(juce::Graphics&g,const juce::String&value,const juce::String&label,juce::Rectangle<float>r,float size=27){text(g,value,r.withHeight(r.getHeight()-17),size,gold);text(g,label,{r.getX(),r.getBottom()-17,r.getWidth(),16},9,cream.withAlpha(.7f));}
    void signal(juce::Graphics&g,juce::Rectangle<float>r){text(g,"IN "+dbLabel(p.inputPeak.load())+"    OUT "+dbLabel(p.outputPeak.load())+" dBFS",r,10,gold);}
    void paint(juce::Graphics&g)override{
        const float w=float(getWidth()),h=float(getHeight());chassis(g,w,h,p.getName());
        text(g,masterInfo(p.kind).subtitle,{108,65,w-308,19},9,champagne,juce::Justification::left);
        switch(p.kind){
        case MasterKind::Ceiling:
            panel(g,{22,151,317,306},"GAIN / LIMIT");meterWell(g,{357,151,180,255},p.inputPeak.load(),p.outputPeak.load(),p.reduction.load());
            number(g,p.integrated.load()>-99?juce::String(p.integrated.load(),1):"--","INTEGRATED LUFS",{355,413,91,49},23);
            number(g,p.peakDb.load()>-150?juce::String(p.peakDb.load(),1):"--",(p.peakDb.load()>0?"OVER 0 dBFS":p.quality.isPro()?"TRUE PEAK / dBTP":"SAMPLE PEAK / dBFS"),{449,413,89,49},23);
            panel(g,{22,474,515,87},"OUTPUT / BLEND");break;
        case MasterKind::Low:
            panel(g,{22,151,312,208},"BASS FOCUS");panel(g,{346,151,292,208},"DYNAMICS / LOW FIELD");
            text(g,"BAND BELOW "+juce::String(p.value("frequency"),0)+" Hz",{38,329,282,19},11,gold);
            text(g,"GR "+juce::String(p.reduction.load(),1)+" dB",{497,318,120,26},11,gold);
            surface(g,{22,372,616,54},16);signal(g,{256,385,120,21});break;
        case MasterKind::Glue:
            panel(g,{22,151,185,247},"BUS COMPRESSION");panel(g,{218,151,232,247},"ENVELOPE / CHARACTER");
            meterWell(g,{464,151,134,207},p.inputPeak.load(),p.outputPeak.load(),p.reduction.load());
            surface(g,{22,404,576,45},13);break;
        case MasterKind::Width:{
            for(int band=0;band<3;++band){const float x=22+band*237.f;panel(g,{x,151,222,171},band==0?"LOW FIELD":band==1?"MID FIELD":"HIGH FIELD");}
            const float cor=juce::jlimit(-1.f,1.f,p.correlation.load());surface(g,{22,370,696,52},15);
            text(g,"CORRELATION "+juce::String(cor,2),{37,376,285,16},10,gold);
            const juce::Rectangle<float>r{44,401,272,5};g.setColour(cream.withAlpha(.15f));g.fillRoundedRectangle(r,2);g.setColour(gold);g.fillEllipse(r.getX()+(cor+1)*.5f*r.getWidth()-4,r.getY()-2,8,9);text(g,"-1",{31,395,19,12},8);text(g,"+1",{316,395,19,12},8);break;}
        case MasterKind::Punch:
            for(int band=0;band<3;++band){const float x=22+band*223.f;panel(g,{x,151,210,176},band==0?"LOW TRANSIENT":band==1?"MID TRANSIENT":"HIGH TRANSIENT");text(g,"GAIN "+juce::String(p.bandMeter[band].load(),1)+" dB",{x+15,302,180,16},10,gold);}
            surface(g,{22,331,656,56},15);surface(g,{22,397,656,45},15);break;
        case MasterKind::Weight:
            panel(g,{22,151,198,211},"HARMONIC WEIGHT");panel(g,{232,151,246,216},"TUNE / TRANSLATE");
            signal(g,{37,338,170,19});surface(g,{22,371,456,42},13);break;
        case MasterKind::Delta:{
            panel(g,{22,151,496,96},"LINK THE SAME PAIR ACROSS YOUR CHAIN");panel(g,{22,259,496,90},"AUDITION / LEVEL MATCH");
            meterWell(g,{532,151,106,198},p.inputPeak.load(),p.outputPeak.load(),0);
            surface(g,{22,361,616,58},16);const auto a=p.delta->alignment();
            const juce::String status=p.delta->conflict()?"PAIR CONFLICT":p.value("role")>.5f?"SOURCE BEFORE CHAIN":!p.delta->linked()?"WAITING FOR SOURCE":p.delta->learning()?"LEARNING":a.valid?juce::String(a.delay)+" SAMPLES / "+juce::String(20*std::log10(a.gain),1)+" dB MATCH":"PLAY AUDIO + LEARN";
            text(g,status,{38,371,580,22},13,gold);text(g,"SOURCE before processing  /  RETURN after processing",{38,396,580,15},10);break;}
        case MasterKind::Deliver:{
            const auto s=p.deliver->snapshot();panel(g,{22,151,196,310},"DELIVERY TARGETS");panel(g,{233,151,445,235},"MEASURED PROGRAMME");
            number(g,s.integrated>-99?juce::String(s.integrated,1):"--","INTEGRATED LUFS",{252,188,200,77},44);
            number(g,s.hasAudio?juce::String(s.peakDb,1):"--",s.truePeak?"TRUE PEAK / dBTP":"SAMPLE PEAK / dBFS",{467,188,192,77},37);
            text(g,"DURATION  "+juce::String(s.duration,1)+" s",{252,280,200,22},12);
            text(g,"RANGE  "+(s.duration>=3?juce::String(s.range,1):"--")+" LU",{467,280,192,22},12);
            text(g,"CLIPPED SAMPLES  "+juce::String(juce::int64(s.clipped)),{252,313,407,22},12,s.clipped?juce::Colour(0xffffac86):gold);
            text(g,"START / END SILENCE  "+juce::String(s.leading,2)+" / "+juce::String(s.trailing,2)+" s",{252,345,407,22},11);
            surface(g,{233,397,445,64},17);text(g,s.running?"CAPTURING AUDIO":s.duration>0?"CAPTURE READY / REPORT AVAILABLE":"READY TO ANALYZE",{35,466,625,15},10,gold);break;}
        }
        text(g,juce::String(p.quality.isPro()?"PRO":"LIVE")+" / "+juce::String(p.getLatencySamples())+" SAMPLES",{108,84,w-307,11},8,cream.withAlpha(.65f),juce::Justification::left);
    }
};
GillMasterEditor::GillMasterEditor(GillMasterProcessor&p):AudioProcessorEditor(&p),impl(std::make_unique<Impl>(*this,p)){
    addAndMakeVisible(*impl);const auto info=masterInfo(p.kind);setResizable(true,false);setResizeLimits(info.width,info.height,info.width*2,info.height*2);getConstrainer()->setFixedAspectRatio(double(info.width)/info.height);setSize(info.width,info.height);
}
GillMasterEditor::~GillMasterEditor()=default;
void GillMasterEditor::paint(juce::Graphics&g){g.fillAll(ink);}
void GillMasterEditor::resized(){if(impl){const auto info=masterInfo(static_cast<GillMasterProcessor*>(getAudioProcessor())->kind);impl->setBounds(0,0,info.width,info.height);impl->setTransform(juce::AffineTransform::scale(float(getWidth())/info.width,float(getHeight())/info.height));}}
