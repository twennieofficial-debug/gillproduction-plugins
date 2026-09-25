#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include <cmath>
#include <complex>
namespace {
const juce::Colour ink(0xff293d31),sage(0xff65836b),cream(0xfff7f3e9),dark(0xff263e32);
juce::Font font(float h,bool bold=false){return juce::Font(juce::FontOptions(gillInterfaceFontName(),std::max(12.f,h),bold?juce::Font::bold:juce::Font::plain));}
class GestureSlider final:public juce::Slider{
public:bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey||k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){juce::Slider::ScopedDragNotification g(*this);return Slider::keyPressed(k);}return Slider::keyPressed(k);}
};
class OakLook final:public juce::LookAndFeel_V4{
public:OakLook(){setColour(juce::Slider::textBoxTextColourId,ink);setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.72f));setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);setColour(juce::TextButton::buttonColourId,cream.withAlpha(.8f));setColour(juce::TextButton::buttonOnColourId,sage);setColour(juce::TextButton::textColourOffId,ink);setColour(juce::TextButton::textColourOnId,cream);setColour(juce::ComboBox::backgroundColourId,cream);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::outlineColourId,sage.withAlpha(.6f));setColour(juce::ComboBox::arrowColourId,ink);setColour(juce::PopupMenu::backgroundColourId,cream);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,sage);setColour(juce::TooltipWindow::backgroundColourId,cream);setColour(juce::TooltipWindow::textColourId,ink);}
    juce::Font getTextButtonFont(juce::TextButton&,int height)override{return font(std::clamp(height*.42f,12.f,14.f),true);}
    juce::Font getComboBoxFont(juce::ComboBox&)override{return font(13,true);}
    juce::Font getLabelFont(juce::Label& l)override{return l.getFont();}
    juce::Label* createSliderTextBox(juce::Slider& slider)override{auto* label=juce::LookAndFeel_V4::createSliderTextBox(slider);label->setFont(font(13,true));label->setColour(juce::Label::textColourId,ink);label->setColour(juce::Label::backgroundColourId,cream.withAlpha(.9f));label->setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);label->setColour(juce::TextEditor::textColourId,ink);label->setColour(juce::TextEditor::backgroundColourId,cream);return label;}
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);if(b.getName()=="LEARN"){g.setColour(juce::Colours::black.withAlpha(.22f));g.fillRoundedRectangle(r.translated(0,3),8);g.setGradientFill(juce::ColourGradient(juce::Colour(0xffdfbf8f),0,0,juce::Colour(0xff987249),0,r.getHeight(),false));g.fillRoundedRectangle(r,8);g.setColour(ink.withAlpha(.16f));for(int y=4;y<r.getHeight();y+=5)g.drawLine(7,static_cast<float>(y),r.getRight()-7,static_cast<float>(y)+1,.6f);g.setColour(cream.withAlpha(.7f));g.drawRoundedRectangle(r.reduced(1),7,1);return;}g.setColour(juce::Colours::black.withAlpha(.13f));g.fillRoundedRectangle(r.translated(0,2),6);g.setColour(b.getToggleState()?sage:cream.withAlpha(over?.98f:.88f));g.fillRoundedRectangle(r,6);g.setColour(down?ink:sage.withAlpha(.35f));g.drawRoundedRectangle(r,6,1);}
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,const juce::Slider::SliderStyle style,juce::Slider&)override{
        const bool vertical=style==juce::Slider::LinearVertical;const float cx=x+w*.5f,cy=y+h*.5f;
        auto track=vertical?juce::Rectangle<float>(cx-5,static_cast<float>(y),10,static_cast<float>(h)):juce::Rectangle<float>(static_cast<float>(x),cy-4,static_cast<float>(w),8);
        g.setColour(juce::Colour(0xff756956));g.fillRoundedRectangle(track.expanded(3),7);g.setColour(dark);g.fillRoundedRectangle(track,4);
        g.setColour(sage.brighter(.25f));if(vertical)g.fillRoundedRectangle(cx-2,pos,4,std::max(0.f,y+h-pos),2);else g.fillRoundedRectangle(static_cast<float>(x),cy-2,std::max(0.f,pos-x),4,2);
        auto thumb=vertical?juce::Rectangle<float>(cx-29,pos-14,58,28):juce::Rectangle<float>(pos-10,cy-18,20,36);
        g.setColour(juce::Colours::black.withAlpha(.23f));g.fillRoundedRectangle(thumb.translated(2,4),5);g.setGradientFill(juce::ColourGradient(juce::Colours::white,thumb.getX(),thumb.getY(),juce::Colour(0xffd1d0c5),thumb.getRight(),thumb.getBottom(),false));g.fillRoundedRectangle(thumb,4);g.setColour(cream);g.drawRoundedRectangle(thumb.reduced(.5f),4,1);g.setColour(sage);if(vertical)g.fillRect(thumb.reduced(9,13));else g.fillRect(thumb.reduced(9,8));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&)override{
        const auto r=juce::Rectangle<float>(static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)).reduced(7);const float size=std::min(r.getWidth(),r.getHeight()),radius=size*.5f,cx=r.getCentreX(),cy=r.getCentreY();auto body=juce::Rectangle<float>(size-15,size-15).withCentre({cx,cy});
        juce::Path a,b;a.addCentredArc(cx,cy,radius,radius,0,start,end,true);b.addCentredArc(cx,cy,radius,radius,0,start,start+value*(end-start),true);g.setColour(ink.withAlpha(.2f));g.strokePath(a,juce::PathStrokeType(4));g.setColour(sage);g.strokePath(b,juce::PathStrokeType(5));g.setColour(juce::Colours::black.withAlpha(.2f));g.fillEllipse(body.translated(2,4));g.setGradientFill(juce::ColourGradient(juce::Colours::white,body.getX(),body.getY(),juce::Colour(0xffcac8bb),body.getRight(),body.getBottom(),false));g.fillEllipse(body);g.setColour(cream);g.drawEllipse(body.reduced(1),1.2f);
        const float angle=start+value*(end-start);g.setColour(sage);g.drawLine(cx+std::sin(angle)*radius*.36f,cy-std::cos(angle)*radius*.36f,cx+std::sin(angle)*radius*.64f,cy-std::cos(angle)*radius*.64f,4);
    }
};
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
public:explicit Display(GillEffectProcessor& processor):p(processor){}
    void tick(){std::move(pre.begin()+1,pre.end(),pre.begin());std::move(post.begin()+1,post.end(),post.begin());pre.back()=p.inputPeak.load();post.back()=p.outputPeak.load();if(p.kind==GillKind::Balance)view=p.balanceView();repaint();}
    void paint(juce::Graphics& g)override{
        const float sx=getWidth()/720.f,sy=getHeight()/270.f;
        if(p.kind==GillKind::Balance){g.addTransform(juce::AffineTransform::scale(sx,sy));paintBalance(g);return;}
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
    static void background(juce::Graphics& g,juce::Rectangle<float> r){g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff192c23),0,r.getHeight(),false));g.fillRoundedRectangle(r,12);g.setColour(cream.withAlpha(.22f));g.drawRoundedRectangle(r.reduced(1),11,1);}
    void paintBalance(juce::Graphics& g){
        background(g,{0,0,720,270});g.setFont(font(15,true));g.setColour(cream.withAlpha(.78f));g.drawText("EQ / DB",17,10,120,17,juce::Justification::left);g.drawText(view.fine.enabled?juce::String(view.fine.count)+" RESONANCES  /  8 DYNAMIC BANDS":"LEGACY TONAL PROFILE",255,10,438,17,juce::Justification::right);g.drawText("IN / OUT BAND LEVEL",17,198,220,16,juce::Justification::left);
        if(!p.rateSupported.load()){g.drawText("SAMPLE RATE UNSUPPORTED / BYPASS",100,70,530,50,juce::Justification::centred);return;}
        const double rate=p.uiRate.load(),maximum=std::min(20000.,rate*.49),pi=3.14159265358979323846;
        const juce::Rectangle<float> area(48,34,648,135);
        auto xFor=[&](double hz){return area.getX()+static_cast<float>(std::log(hz/20)/std::log(maximum/20))*area.getWidth();};
        auto yFor=[&](double db){return area.getCentreY()-static_cast<float>(db)*area.getHeight()/48;};
        for(int db:{-18,-12,-6,0,6,12}){g.setColour(cream.withAlpha(db==0?.22f:.09f));g.drawHorizontalLine(static_cast<int>(yFor(db)),area.getX(),area.getRight());g.setColour(cream.withAlpha(.7f));g.drawText(juce::String(db),9,static_cast<int>(yFor(db))-7,29,15,juce::Justification::right);}
        for(int hz:{50,100,200,500,1000,2000,5000,10000})if(hz<maximum){float x=xFor(hz);g.setColour(cream.withAlpha(.08f));g.drawVerticalLine(static_cast<int>(x),area.getY(),area.getBottom());g.setColour(cream.withAlpha(.6f));g.drawText(hz>=1000?juce::String(hz/1000)+"K":juce::String(hz),static_cast<int>(x)-22,175,44,16,juce::Justification::centred);}
        auto response=[&](double frequency){const auto z=std::polar(1.,-2*pi*frequency/rate);double magnitude=1;for(size_t b=0;b<8;++b)if(view.active[b]){const double w=2*pi*gill::BalanceDSP::centersHz[b]/rate,a=std::pow(10.,view.gains[b]/40.),alpha=std::sin(w)/(2*gill::BalanceDSP::bellQ),cosine=std::cos(w);const auto numerator=(1+alpha*a)-2*cosine*z+(1-alpha*a)*z*z,denominator=(1+alpha/a)-2*cosine*z+(1-alpha/a)*z*z;magnitude*=std::abs(numerator/denominator);}if(view.fine.enabled){auto extra=[&](double hz,double gain,double q){if(hz<=0||hz>rate*.45||q<=0)return;const double w=2*pi*hz/rate,a=std::pow(10.,gain/40.),alpha=std::sin(w)/(2*q),co=std::cos(w);magnitude*=std::abs(((1+alpha*a)-2*co*z+(1-alpha*a)*z*z)/((1+alpha/a)-2*co*z+(1-alpha/a)*z*z));};for(size_t b=0;b<8;++b)extra(gill::BalanceDSP::centersHz[b],view.fine.dynamics[b],.9);for(int b=0;b<view.fine.count;++b)extra(view.fine.frequency[b],view.fine.gain[b],view.fine.q[b]);}return 20*std::log10(std::max(1e-12,magnitude));};
        if(view.fine.enabled){juce::Path spectrum;bool started=false;for(int i=0;i<256;++i){const double hz=gill::FineBalanceProfile::frequency(i);if(hz>=maximum)break;const float x=xFor(hz),y=area.getBottom()-juce::jlimit(0.f,1.f,(view.fine.spectrum[i]+75)/75)*area.getHeight()*.85f;if(!started){spectrum.startNewSubPath(x,y);started=true;}else spectrum.lineTo(x,y);}g.setColour(cream.withAlpha(.25f));g.strokePath(spectrum,juce::PathStrokeType(1));g.setFont(font(15));g.drawText("LEARNED SPECTRUM",505,146,180,15,juce::Justification::right);}
        juce::Path curve;for(int i=0;i<=400;++i){const double hz=20*std::pow(maximum/20,i/400.);const float x=area.getX()+i*area.getWidth()/400,y=yFor(response(hz));if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}g.setColour(sage.brighter(.75f));g.strokePath(curve,juce::PathStrokeType(2.2f));
        for(size_t b=0;b<8;++b)if(view.active[b]&&gill::BalanceDSP::centersHz[b]<maximum){const float x=xFor(gill::BalanceDSP::centersHz[b]),y=yFor(response(gill::BalanceDSP::centersHz[b]));g.setColour(cream);g.fillEllipse(x-3,y-3,6,6);const float in=juce::jlimit(0.f,1.f,(view.pre[b]+60)/60),out=juce::jlimit(0.f,1.f,(view.post[b]+60)/60);g.setColour(cream.withAlpha(.4f));g.fillRoundedRectangle(x-8,253-in*35,6,in*35,1);g.setColour(sage.brighter(.7f));g.fillRoundedRectangle(x+2,253-out*35,6,out*35,1);}
        if(view.fine.enabled)for(int b=0;b<view.fine.count;++b){const auto hz=view.fine.frequency[b];if(hz<20||hz>maximum)continue;const float x=xFor(hz),y=yFor(response(hz));g.setColour(juce::Colour(0xffe3b884));g.drawEllipse(x-4,y-4,8,8,1.2f);}
        g.setFont(font(15));g.setColour(cream.withAlpha(.6f));g.drawText("0",9,213,29,14,juce::Justification::right);g.drawText("-60",9,242,29,14,juce::Justification::right);g.drawText("DBFS",658,199,40,15,juce::Justification::right);
    }
    GillEffectProcessor& p;BalanceView view;std::array<float,100> pre{},post{};
};
}
struct GillEffectEditor::Impl:private juce::Timer {
    GillEffectEditor& owner;GillEffectProcessor& p;OakLook look;juce::Image wood;Display display;juce::TooltipWindow tooltip;
    std::vector<std::unique_ptr<Binding>> controls;std::array<juce::TextButton,3> modes;
    juce::TextButton learn{"LEARN"},bypass{"BYPASS"},mixlock{"MIX LOCK"},sync{"SYNC"},previous{"<"},next{">"};
    juce::ComboBox preset,target,division;juce::Label status;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment,lockAttachment,syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAttachment,divisionAttachment;
    int baseWidth=790,baseHeight=550;
    Impl(GillEffectEditor& o,GillEffectProcessor& v):owner(o),p(v),display(v),tooltip(&o,600){
        owner.setLookAndFeel(&look);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);owner.addAndMakeVisible(display);
        auto add=[&](const char* id,const char* title,const char* suffix){auto b=std::make_unique<Binding>(p,id,title,suffix);owner.addAndMakeVisible(b->slider);owner.addAndMakeVisible(b->label);controls.push_back(std::move(b));};
        bypass.setName("BYPASS");bypass.setClickingTogglesState(true);owner.addAndMakeVisible(bypass);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);
        if(p.kind==GillKind::Air){baseWidth=440;baseHeight=300;add("midair","MID AIR"," %");add("highair","HIGH AIR"," %");add("mix","MIX"," %");add("output","OUTPUT"," DB");}
        else if(p.kind==GillKind::Space){baseWidth=660;baseHeight=410;add("decay","DECAY"," S");add("predelay","PREDELAY"," MS");add("size","SIZE"," %");add("tone","TONE"," %");add("width","WIDTH"," %");add("mix","MIX"," %");add("dry","DRY"," %");}
        else if(p.kind==GillKind::Echo){baseWidth=680;baseHeight=440;add("time","TIME"," MS");add("feedback","FEEDBACK"," %");add("color","COLOR"," %");add("width","WIDTH"," %");add("mix","MIX"," %");add("bpm","TEMPO"," BPM");add("dry","DRY"," %");sync.setName("SYNC");sync.setClickingTogglesState(true);owner.addAndMakeVisible(sync);syncAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"sync",sync);auto* dp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("division"));division.addItemList(dp->choices,1);division.setName("DIVISION");owner.addAndMakeVisible(division);divisionAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"division",division);division.setTooltip("D = DOTTED / T = TRIPLET; MAXIMAL 8 SEKUNDEN");sync.setTooltip("SONGTEMPO VERWENDEN; OHNE HOSTTEMPO GILT DER TEMPO-REGLER");}
        else{baseWidth=680;baseHeight=420;add("amount","AMOUNT"," %");auto* tp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("target"));target.addItemList(tp->choices,1);target.setName("TARGET");owner.addAndMakeVisible(target);targetAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"target",target);learn.setName("LEARN");owner.addAndMakeVisible(learn);owner.addAndMakeVisible(status);learn.onClick=[this]{p.requestLearning(p.learnState.load()!=1);};learn.setTooltip("VOCAL ABSPIELEN: 10 SEKUNDEN AKTIVES AUDIO ANALYSIEREN; STILLE ZAEHLT NICHT");status.setFont(font(12,true));status.setColour(juce::Label::textColourId,ink);}
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
        if(p.kind==GillKind::Balance){const int state=p.learnState.load();learn.setButtonText(state==1?"CANCEL":"LEARN");status.setText(state==1?"LISTENING  "+juce::String(p.learnProgress.load()*100,0)+" %":state==2?(p.balanceView().fine.enabled?"SPECTRAL + DYNAMIC READY":"LEGACY PROFILE / LEARN TO UPGRADE"):state==3?"MORE VOCAL NEEDED":"PLAY VOCAL + LEARN",juce::dontSendNotification);}owner.repaint();
    }
    void bounds(juce::Component& c,float x,float y,float w,float h){const float s=owner.getWidth()/static_cast<float>(baseWidth);c.setBounds(juce::Rectangle<float>(x*s,y*s,w*s,h*s).toNearestInt());}
    void control(int i,float x,float y,float w,float h){bounds(controls[i]->label,x,y,w,21);bounds(controls[i]->slider,x,y+24,w,h-24);}
    void resized(){bounds(bypass,baseWidth-105.f,19,84,29);
        if(p.kind==GillKind::Air){bounds(display,20,65,400,75);control(0,22,151,106,133);control(1,133,151,106,133);control(2,250,169,80,112);control(3,337,169,80,112);}
        else if(p.kind==GillKind::Balance){bounds(display,22,65,636,220);bounds(learn,25,316,106,34);bounds(target,145,316,301,34);bounds(status,25,364,435,29);control(0,515,293,124,109);}
        else{const bool echo=p.kind==GillKind::Echo;bounds(display,22,65,baseWidth-44.f,echo?121:115);
            const float modeY=echo?198:192;const float modeX=echo?297:169,modeW=echo?113:104;for(int i=0;i<3;++i)bounds(modes[i],modeX+i*(modeW+4),modeY,modeW,29);
            if(echo){bounds(sync,22,198,78,29);bounds(division,107,198,179,29);}
            const float cw=(baseWidth-44.f)/7;for(int i=0;i<7;++i)control(i,22+i*cw,echo?240:233,cw-5,echo?131:119);
            const float y=baseHeight-43.f;bounds(previous,22,y,30,29);bounds(preset,61,y,baseWidth-242.f,29);bounds(next,baseWidth-172.f,y,30,29);bounds(mixlock,baseWidth-133.f,y,111,29);
        }
    }
    void paint(juce::Graphics& g){const float s=owner.getWidth()/static_cast<float>(baseWidth),w=static_cast<float>(owner.getWidth()),h=static_cast<float>(owner.getHeight());g.fillAll(juce::Colour(0xffc9ac85));if(wood.isValid()){g.drawImage(wood,0,0,static_cast<int>(w),static_cast<int>(h),950,285,170,805);g.setColour(cream.withAlpha(.16f));g.fillAll();g.setOpacity(1.f);g.drawImage(wood,juce::roundToInt(21*s),juce::roundToInt(15*s),juce::roundToInt(46*s),juce::roundToInt(33*s),224,146,174,122);}
        g.setColour(juce::Colour(0xff65543c).withAlpha(.52f));g.drawRoundedRectangle({3*s,3*s,w-6*s,h-6*s},15*s,5*s);g.setColour(cream.withAlpha(.5f));g.drawRoundedRectangle({7*s,7*s,w-14*s,h-14*s},12*s,s);g.setColour(ink.withAlpha(.4f));g.drawLine(80*s,17*s,80*s,49*s,s);g.setColour(ink);g.setFont(font(22*s));g.drawText(p.getName(),juce::Rectangle<float>(94*s,17*s,(baseWidth-210)*s,33*s),juce::Justification::centredLeft,false);
        if(p.kind==GillKind::Space||p.kind==GillKind::Echo){g.setColour(ink.withAlpha(.22f));g.drawLine(35*s,(baseHeight-54)*s,(baseWidth-35)*s,(baseHeight-54)*s,s);}
        if(p.kind==GillKind::Balance){g.setFont(font(12*s,true));g.setColour(ink.withAlpha(.72f));g.drawText("VOCAL PROFILE",juce::Rectangle<float>(26*s,292*s,110*s,18*s),juce::Justification::left,false);g.drawText("TARGET",juce::Rectangle<float>(146*s,292*s,290*s,18*s),juce::Justification::left,false);}
    }
};
GillEffectEditor::GillEffectEditor(GillEffectProcessor& p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);const int w=impl->baseWidth,h=impl->baseHeight;setResizable(true,true);setResizeLimits(w,h,w*2,h*2);if(auto* c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(w)/h);setSize(w,h);}
GillEffectEditor::~GillEffectEditor()=default;
void GillEffectEditor::paint(juce::Graphics& g){impl->paint(g);}
void GillEffectEditor::resized(){if(impl)impl->resized();}
