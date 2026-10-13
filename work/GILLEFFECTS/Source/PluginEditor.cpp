#include "PluginEditor.h"
#include "../../GILLCommon/TwinLensUi.h"
#include "../../GILLCommon/PrismUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>
#include <complex>
namespace {
const juce::Colour ink(0xff102237),sage(0xff54b8d5),cream(0xffe2ecf6),dark(0xff091d31);
juce::Font font(float h,bool bold=false){return juce::Font(juce::FontOptions(gillInterfaceFontName(),std::max(12.f,h),bold?juce::Font::bold:juce::Font::plain));}
class GestureSlider final:public juce::Slider{
public:bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey||k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){juce::Slider::ScopedDragNotification g(*this);return Slider::keyPressed(k);}return Slider::keyPressed(k);}
};
class OakLook final : public gill::prism::Look {  };
struct Binding {
    GestureSlider slider;juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    Binding(GillEffectProcessor& p,const juce::String& id,const juce::String& caption,const juce::String& suffix,bool vertical=false){
        slider.setName(caption);slider.setSliderStyle(vertical?juce::Slider::LinearVertical:juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,80,24);slider.setTextValueSuffix(suffix);auto* parameter=p.apvts.getParameter(id);slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));slider.setWantsKeyboardFocus(true);
        slider.setTooltip(caption+": ZIEHEN ODER ZAHL EINGEBEN | DOPPELKLICK: STANDARDWERT");attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,slider);
        slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.94f));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        label.setText(caption,juce::dontSendNotification);label.setJustificationType(juce::Justification::centred);label.setColour(juce::Label::textColourId,ink);label.setFont(font(12,true));
    }
};

class Display final:public juce::Component {
public:explicit Display(GillEffectProcessor& processor):p(processor){if(p.kind==GillKind::Balance){setName("BALANCE EQ GRAPH");setWantsKeyboardFocus(true);}}
    ~Display() override {endDrag();}
    void mouseDown(const juce::MouseEvent& e)override{
        if(p.kind!=GillKind::Balance||!p.rateSupported.load())return;
        endDrag();
        const auto point=juce::Point<float>(e.position.x*696/getWidth(),e.position.y*270/getHeight());
        float distance=18;selected=-1;
        for(int i=0;i<8;++i)if(view.active[i]){const auto node=nodePosition(i);const auto d=node.getDistanceFrom(point);if(d<distance){distance=d;selected=i;}}
        if(selected>=0){grabKeyboardFocus();startY=e.position.y;startGain=p.value("trim"+juce::String(selected));drag=p.apvts.getParameter("trim"+juce::String(selected));drag->beginChangeGesture();}repaint();
    }
    void mouseDrag(const juce::MouseEvent& e)override{if(drag){const float delta=(startY-e.position.y)*270/getHeight()*36/172;drag->setValueNotifyingHost(drag->convertTo0to1(juce::jlimit(-6.f,6.f,startGain+delta)));repaint();}}
    void mouseUp(const juce::MouseEvent&)override{endDrag();}
    void mouseDoubleClick(const juce::MouseEvent&)override{endDrag();if(selected>=0)p.setValue("trim"+juce::String(selected),0);}
    bool keyPressed(const juce::KeyPress& key)override{
        if(p.kind!=GillKind::Balance)return false;
        if(key==juce::KeyPress::leftKey||key==juce::KeyPress::rightKey){const int step=key==juce::KeyPress::leftKey?-1:1;for(int next=juce::jlimit(0,7,selected+step);next>=0&&next<8;next+=step)if(view.active[next]){selected=next;break;}repaint();return true;}
        if(selected>=0&&(key==juce::KeyPress::upKey||key==juce::KeyPress::downKey)){const auto id="trim"+juce::String(selected);p.setValue(id,juce::jlimit(-6.f,6.f,p.value(id)+(key==juce::KeyPress::upKey?.1f:-.1f)));repaint();return true;}return false;
    }
    void tick(){std::move(pre.begin()+1,pre.end(),pre.begin());std::move(post.begin()+1,post.end(),post.begin());pre.back()=p.inputPeak.load();post.back()=p.outputPeak.load();if(p.kind==GillKind::Balance)view=p.balanceView();repaint();}
    void paint(juce::Graphics& g)override{
        const float sx=getWidth()/720.f,sy=getHeight()/270.f;
        if(p.kind==GillKind::Balance){g.addTransform(juce::AffineTransform::scale(getWidth()/696.f,getHeight()/270.f));paintBalance(g);return;}
        auto r=getLocalBounds().toFloat();background(g,r);
        if(!p.rateSupported.load()){g.setColour(cream);g.setFont(font(15,true));g.drawText("SAMPLE RATE UNSUPPORTED / BYPASS",getLocalBounds(),juce::Justification::centred);return;}
        g.setFont(font(11,true));g.setColour(cream.withAlpha(.78f));
        const juce::String title=p.kind==GillKind::Air?"PRESENCE + AIR":p.kind==GillKind::Space?"ROOM / HALL / PLATE":"TEMPO / REPEATS";
        g.drawText(title,16,10,getWidth()-32,18,juce::Justification::left);
        if(p.kind==GillKind::Echo){g.setFont(font(25,true));g.setColour(cream);g.drawText(juce::String(p.delayMs.load(),1)+" MS",getWidth()-255,12,230,30,juce::Justification::right);g.setFont(font(10));g.setColour(cream.withAlpha(.7f));const auto text=p.value("sync")<.5f?"FREE TIME":(p.hostTempoAvailable.load()?"HOST ":"MANUAL ")+juce::String(p.tempoBpm.load(),1)+" BPM";g.drawText(text+(p.delayLimited.load()?" / 8 S LIMIT":""),getWidth()-285,44,260,17,juce::Justification::right);}
        auto area=r.reduced(16).withTrimmedTop(p.kind==GillKind::Echo?52:24).withTrimmedBottom(21);
        for(int db:{-12,-24,-48}){g.setColour(cream.withAlpha(.08f));g.drawHorizontalLine(static_cast<int>(area.getBottom()-(db+60)/60.f*area.getHeight()),area.getX(),area.getRight());}
        auto line=[&](const auto& data,juce::Colour c){juce::Path path;for(size_t i=0;i<data.size();++i){const float x=area.getX()+i*area.getWidth()/(data.size()-1),db=juce::Decibels::gainToDecibels(std::max(1e-6f,data[i]),-60.f),y=area.getBottom()-juce::jlimit(0.f,1.f,(db+60)/60)*area.getHeight();if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}g.setColour(c);g.strokePath(path,juce::PathStrokeType(1.7f));};line(pre,cream.withAlpha(.4f));line(post,sage.brighter(.65f));
        g.setFont(font(10,true));g.setColour(cream.withAlpha(.8f));g.drawText("IN "+juce::String(juce::Decibels::gainToDecibels(p.inputPeak.load(),-90.f),1)+" DBFS",16,getHeight()-25,170,20,juce::Justification::left);g.drawText("OUT "+juce::String(juce::Decibels::gainToDecibels(p.outputPeak.load(),-90.f),1)+" DBFS",getWidth()-186,getHeight()-25,170,20,juce::Justification::right);
    }
private:
    static void background(juce::Graphics& g,juce::Rectangle<float> r){g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff071727),0,r.getHeight(),false));g.fillRoundedRectangle(r,12);g.setColour(cream.withAlpha(.22f));g.drawRoundedRectangle(r.reduced(1),11,1);}
    void endDrag(){if(drag){drag->endChangeGesture();drag=nullptr;}}
    double maximum()const{return std::min(20000.,p.uiRate.load()*.49);}
    float xFor(double hz)const{return 46+float(std::log(hz/20)/std::log(maximum()/20))*600;}
    float yFor(double db)const{return 134-float(db)*172/36;}
    double response(double frequency)const{
        if(!std::any_of(view.active.begin(),view.active.end(),[](bool b){return b;}))return 0;
        const double rate=p.uiRate.load(),pi=juce::MathConstants<double>::pi;const auto z=std::polar(1.,-2*pi*frequency/rate);double magnitude=1;
        auto cascade=[&](const auto& coefficients){std::complex<double> h=1;for(const auto& c:coefficients)h*=(c[0]+c[1]*z+c[2]*z*z)/(1.+c[3]*z+c[4]*z*z);return h;};
        auto fine=cascade(view.fine.coefficients);if(view.fine.transitionBlend<1)fine=cascade(view.fine.previousCoefficients)*(1-view.fine.transitionBlend)+fine*view.fine.transitionBlend;
        fine=1.+(fine-1.)*view.fine.wetBlend;
        magnitude=std::abs(cascade(view.coefficients)*fine);
        return 20*std::log10(std::max(1e-12,magnitude));
    }
    juce::Point<float> nodePosition(int i)const{return {xFor(gill::BalanceDSP::centersHz[i]),juce::jlimit(48.f,220.f,yFor(response(gill::BalanceDSP::centersHz[i])))};}
    void paintBalance(juce::Graphics& g){
        const auto white=juce::Colour(0xffeee8f8),purple=gill::twin::purple;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe9e2d8),0,0,juce::Colour(0xff68616c),696,270,false));g.fillRoundedRectangle(0,0,696,270,14);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff292733),0,0,gill::twin::glass,696,270,false));g.fillRoundedRectangle(3,3,690,264,12);
        g.setColour(purple.withAlpha(.45f));g.drawRoundedRectangle(5,5,686,260,10,1);
        g.setFont(font(12,true));g.setColour(white);g.drawText(p.value("bypass")>.5f?"EQ / BYPASSED":"EQ RESPONSE",16,12,180,20,juce::Justification::left);
        g.setFont(font(12));g.setColour(purple);g.drawText("STATIC + FINE + DYNAMIC",316,12,330,20,juce::Justification::right);
        if(!p.rateSupported.load()){g.drawText("SAMPLE RATE UNSUPPORTED / BYPASS",46,100,600,60,juce::Justification::centred);return;}
        for(int db:{-18,-12,-6,0,6,12,18}){const float y=yFor(db);g.setColour(white.withAlpha(db==0?.30f:.09f));g.drawHorizontalLine(int(y),46,646);g.setColour(white.withAlpha(.72f));g.drawText((db>0?"+":"")+juce::String(db),7,int(y)-8,32,16,juce::Justification::right);}
        for(int hz:{20,50,100,200,500,1000,2000,5000,10000,20000})if(hz<=maximum()){const float x=xFor(hz);g.setColour(white.withAlpha(.08f));g.drawVerticalLine(int(x),48,220);g.setColour(white.withAlpha(.7f));g.drawText(hz>=1000?juce::String(hz/1000)+"k":juce::String(hz),int(x)-21,222,42,16,juce::Justification::centred);}
        g.setColour(white.withAlpha(.7f));g.drawText("dB",8,31,30,16,juce::Justification::right);g.drawText("Hz",669,222,22,16,juce::Justification::left);
        {juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(46,48,601,173);
        if(view.fine.enabled){juce::Path spectrum;bool first=true;for(int i=0;i<256;++i){const double hz=gill::FineBalanceProfile::frequency(i);if(hz>maximum())break;const float x=xFor(hz),y=220-juce::jlimit(0.f,1.f,(view.fine.spectrum[i]+90)/90)*172;if(first){spectrum.startNewSubPath(x,y);first=false;}else spectrum.lineTo(x,y);}g.setColour(white.withAlpha(.25f));g.strokePath(spectrum,juce::PathStrokeType(1.2f));}
        juce::Path curve;for(int i=0;i<=600;++i){const float x=46.f+i,y=yFor(response(20*std::pow(maximum()/20,i/600.)));if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}g.setColour(purple.withAlpha(.10f));g.strokePath(curve,juce::PathStrokeType(8));g.setColour(purple);g.strokePath(curve,juce::PathStrokeType(2.2f));}
        for(int i=0;i<8;++i)if(view.active[i]){const auto pt=nodePosition(i);g.setColour(gill::twin::glass);g.fillEllipse(pt.x-6,pt.y-6,12,12);g.setColour(i==selected?white:purple);g.drawEllipse(pt.x-6,pt.y-6,12,12,i==selected?2.5f:1.5f);}
        g.setFont(font(12));g.setColour(white.withAlpha(.6f));g.drawText(view.fine.enabled?"LEARNED SPECTRUM / -90..0 dB (relative)":"NO LEARNED SPECTRUM",16,245,320,16,juce::Justification::left);
        g.setColour(purple);g.drawText(selected>=0?juce::String(gill::BalanceDSP::centersHz[selected],0)+" Hz / TRIM "+juce::String(p.value("trim"+juce::String(selected)),1)+" dB":"DRAG A POINT / +/-6 dB TRIM",340,245,340,16,juce::Justification::right);
    }
    juce::RangedAudioParameter* drag=nullptr;int selected=-1;float startY=0,startGain=0;
    GillEffectProcessor& p;BalanceView view;std::array<float,100> pre{},post{};
};
}
struct GillEffectEditor::Impl:private juce::Timer {
    GillEffectEditor& owner;GillEffectProcessor& p;OakLook look;gill::twin::Look twinLook;juce::Image wood;Display display;juce::TooltipWindow tooltip;
    std::vector<std::unique_ptr<Binding>> controls;std::array<juce::TextButton,3> modes;
    juce::TextButton learn{"LEARN"},bypass{"BYPASS"},mixlock{"MIX LOCK"},sync{"SYNC"},previous{"<"},next{">"};
    juce::ComboBox preset,target,division;juce::Label status;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment,lockAttachment,syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment,divisionAttachment;
    int baseWidth=790,baseHeight=550;
    gill::QualitySelector quality{p.apvts, p};
    Impl(GillEffectEditor& o,GillEffectProcessor& v):owner(o),p(v),display(v),tooltip(&o,600){
        owner.setLookAndFeel(p.kind==GillKind::Balance?static_cast<juce::LookAndFeel*>(&twinLook):&look);owner.addAndMakeVisible(quality);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);owner.addAndMakeVisible(display);
        auto add=[&](const char* id,const char* title,const char* suffix){auto b=std::make_unique<Binding>(p,id,title,suffix);owner.addAndMakeVisible(b->slider);owner.addAndMakeVisible(b->label);controls.push_back(std::move(b));};
        bypass.setName("BYPASS");bypass.setClickingTogglesState(true);owner.addAndMakeVisible(bypass);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);
        if(p.kind==GillKind::Air){baseWidth=440;baseHeight=300;add("midair","MID AIR"," %");add("highair","HIGH AIR"," %");add("mix","MIX"," %");add("output","OUTPUT"," DB");}
        else if(p.kind==GillKind::Space){baseWidth=660;baseHeight=410;add("decay","DECAY"," S");add("predelay","PREDELAY"," MS");add("size","SIZE"," %");add("tone","TONE"," %");add("width","WIDTH"," %");add("mix","MIX"," %");add("dry","DRY"," %");}
        else if(p.kind==GillKind::Echo){baseWidth=680;baseHeight=440;add("time","TIME"," MS");add("feedback","FEEDBACK"," %");add("color","COLOR"," %");add("width","WIDTH"," %");add("mix","MIX"," %");add("bpm","TEMPO"," BPM");add("dry","DRY"," %");sync.setName("SYNC");sync.setClickingTogglesState(true);owner.addAndMakeVisible(sync);syncAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"sync",sync);auto* dp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("division"));division.addItemList(dp->choices,1);division.setName("DIVISION");owner.addAndMakeVisible(division);divisionAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"division",division);division.setTooltip("D = DOTTED / T = TRIPLET; MAXIMAL 8 SEKUNDEN");sync.setTooltip("SONGTEMPO VERWENDEN; OHNE HOSTTEMPO GILT DER TEMPO-REGLER");}
        else{baseWidth=740;baseHeight=450;add("amount","AMOUNT"," %");controls[0]->label.setText("LEARN AMOUNT",juce::dontSendNotification);controls[0]->slider.setSliderStyle(juce::Slider::LinearHorizontal);auto* tp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("target"));target.addItemList(tp->choices,1);target.setName("TARGET");owner.addAndMakeVisible(target);targetAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"target",target);learn.setName("LEARN");owner.addAndMakeVisible(learn);owner.addAndMakeVisible(status);learn.onClick=[this]{const auto state=p.learnState.load();p.requestLearning(state!=1&&state!=4);};learn.setTooltip("LEARN, SONG ABSPIELEN, DANN STOP. BIS 5 MINUTEN. ERNEUTER KLICK SCHLIESST DIE ANALYSE AB.");status.setFont(font(12,true));status.setColour(juce::Label::textColourId,ink);}
        if(p.kind==GillKind::Space||p.kind==GillKind::Echo){
            const char* spaceNames[]{"ROOM","HALL","PLATE"};const char* echoNames[]{"CLEAN","TAPE","PINGPONG"};for(int i=0;i<3;++i){modes[i].setButtonText(p.kind==GillKind::Space?spaceNames[i]:echoNames[i]);modes[i].setName(modes[i].getButtonText());modes[i].onClick=[this,i]{p.setValue("style",static_cast<float>(i));};owner.addAndMakeVisible(modes[i]);}
            preset.setName("PRESET");for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.setTextWhenNothingSelected("CUSTOM");preset.onChange=[this]{if(preset.getSelectedId()>0)p.selectPreset(preset.getSelectedId()-1);};owner.addAndMakeVisible(preset);mixlock.setName("MIX LOCK");mixlock.setClickingTogglesState(true);owner.addAndMakeVisible(mixlock);lockAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"mixlock",mixlock);mixlock.setTooltip("MIX BEIM PRESETWECHSEL BEIBEHALTEN. DRY BLEIBT IMMER ERHALTEN.");previous.setName("PREVIOUS PRESET");next.setName("NEXT PRESET");owner.addAndMakeVisible(previous);owner.addAndMakeVisible(next);previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+11)%12);};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%12);};
        }
        startTimerHz(25);timerCallback();
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    void timerCallback()override{
        display.tick();if(p.kind==GillKind::Space||p.kind==GillKind::Echo)controls[6]->slider.setTooltip("DRY: DIREKTE STIMME. 0 % = NUR EFFEKT; MIX STEUERT DEN EFFEKTANTEIL. FUER SENDS: DRY 0 %, MIX 100 %.");if(p.kind==GillKind::Space||p.kind==GillKind::Echo){for(int i=0;i<3;++i)modes[i].setToggleState(i==juce::roundToInt(p.value("style")),juce::dontSendNotification);preset.setSelectedId(p.presetMatches()?p.getCurrentProgram()+1:0,juce::dontSendNotification);}
        if(p.kind==GillKind::Echo){const bool synced=p.value("sync")>.5f;controls[0]->slider.setEnabled(!synced);division.setEnabled(synced);controls[5]->slider.setEnabled(synced&&!p.hostTempoAvailable.load());}
        if(p.kind==GillKind::Balance){const int state=p.learnState.load();learn.setButtonText(state==1?"FINISH":state==4?"CANCEL":"LEARN");status.setText(state==4?"ARMED / PRESS PLAY":state==1?"LISTENING  "+juce::String(p.learnProgress.load()*300,0)+" S / STOP TO FINISH":state==2?(p.balanceView().fine.enabled?"APPLIED / SPECTRAL + DYNAMIC":"LEGACY PROFILE / LEARN TO UPGRADE"):state==3?(p.savedProfile().valid?"MORE VOCAL / PREVIOUS PROFILE ACTIVE":"MORE VOCAL NEEDED"):"PLAY VOCAL + LEARN",juce::dontSendNotification);}owner.repaint();
    }
    void bounds(juce::Component& c,float x,float y,float w,float h){const float s=owner.getWidth()/static_cast<float>(baseWidth);c.setBounds(juce::Rectangle<float>(x*s,y*s,w*s,h*s).toNearestInt());}
    void control(int i,float x,float y,float w,float h){bounds(controls[i]->label,x,y,w,21);bounds(controls[i]->slider,x,y+24,w,h-24);}
    void resized(){bounds(bypass,baseWidth-105.f,19,84,29);bounds(quality,baseWidth-228.f,21,110,26);
        if(p.kind==GillKind::Air){bounds(display,20,65,400,75);control(0,22,151,106,133);control(1,133,151,106,133);control(2,250,169,80,112);control(3,337,169,80,112);}
        else if(p.kind==GillKind::Balance){bounds(display,22,68,696,270);bounds(learn,24,363,98,36);bounds(target,141,365,238,32);bounds(status,22,411,696,25);control(0,402,345,308,64);}
        else{const bool echo=p.kind==GillKind::Echo;bounds(display,22,65,baseWidth-44.f,echo?121:115);
            const float modeY=echo?198:192;const float modeX=echo?297:169,modeW=echo?113:104;for(int i=0;i<3;++i)bounds(modes[i],modeX+i*(modeW+4),modeY,modeW,29);
            if(echo){bounds(sync,22,198,78,29);bounds(division,107,198,179,29);}
            const float cw=(baseWidth-44.f)/7;for(int i=0;i<7;++i)control(i,22+i*cw,echo?240:233,cw-5,echo?131:119);
            const float y=baseHeight-43.f;bounds(previous,22,y,30,29);bounds(preset,61,y,baseWidth-242.f,29);bounds(next,baseWidth-172.f,y,30,29);bounds(mixlock,baseWidth-133.f,y,111,29);
        }
    }
    void paint(juce::Graphics& g){
g.addTransform(juce::AffineTransform::scale(owner.getWidth()/static_cast<float>(baseWidth)));
 if(p.kind==GillKind::Balance){gill::twin::chassis(g,740,450,p.getName());g.setFont(font(12,true));g.setColour(gill::twin::ink);g.drawText("TARGET",142,345,236,20,juce::Justification::centredLeft);return;}
 const float top=p.kind==GillKind::Air?140.f:p.kind==GillKind::Balance?255.f:202.f;
 gill::prism::chassis(g,float(baseWidth),float(baseHeight),top);
 gill::prism::title(g,p.getName(),{75,16,baseWidth-313.f,32},22);
    }
};
GillEffectEditor::GillEffectEditor(GillEffectProcessor& p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);const int w=impl->baseWidth,h=impl->baseHeight;setResizable(true,true);setResizeLimits(w,h,w*2,h*2);if(auto* c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(w)/h);setSize(w,h);}
GillEffectEditor::~GillEffectEditor()=default;
void GillEffectEditor::paint(juce::Graphics& g){impl->paint(g);}
void GillEffectEditor::resized(){if(impl)impl->resized();}



