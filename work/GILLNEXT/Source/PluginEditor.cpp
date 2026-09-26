#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>

namespace {
const juce::Colour ink(0xff293a30),sage(0xff668d79),cream(0xfff4eddf),dark(0xff213a33),copper(0xffb18052);
const std::array<juce::Colour,4> bandColours{{juce::Colour(0xff8ed1b4),juce::Colour(0xffe1bc79),juce::Colour(0xff80b8d6),juce::Colour(0xffc69dd5)}};
constexpr float pi=juce::MathConstants<float>::pi;
juce::Font face(float size,bool bold=false){return juce::Font(juce::FontOptions(gillInterfaceFontName(),std::max(12.f,size),bold?juce::Font::bold:juce::Font::plain));}
void text(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size,juce::Colour colour=ink,int alignment=juce::Justification::centred){g.setColour(colour);g.setFont(face(size,true));g.drawText(s,r,alignment);}
void disc(juce::Graphics& g,juce::Rectangle<float> r){
    gill::material::disc(g,r,cream);
    }
float levelDb(float gain){return juce::Decibels::gainToDecibels(gain,-90.f);}
juce::Image extractLogo(const juce::Image& source){
    if(source.getWidth()<398||source.getHeight()<268)return {};
    constexpr int w=174,h=122,count=w*h;
    std::vector<float> alpha(count);std::vector<int> label(count,-1),queue;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto colour=source.getPixelAt(x+224,y+146);const float brightness=.2126f*colour.getFloatRed()+.7152f*colour.getFloatGreen()+.0722f*colour.getFloatBlue();alpha[static_cast<size_t>(y*w+x)]=std::clamp((.54f-brightness)/.25f,0.f,1.f);}
    int component=0,largest=-1,largestSize=0;
    for(int seed=0;seed<count;++seed){if(label[static_cast<size_t>(seed)]>=0||alpha[static_cast<size_t>(seed)]<.12f)continue;queue.clear();queue.push_back(seed);label[static_cast<size_t>(seed)]=component;
        for(size_t at=0;at<queue.size();++at){const int pixel=queue[at],x=pixel%w,y=pixel/w;for(int direction=0;direction<4;++direction){const int nx=x+(direction==0?-1:direction==1?1:0),ny=y+(direction==2?-1:direction==3?1:0);if(nx<0||nx>=w||ny<0||ny>=h)continue;const int n=ny*w+nx;if(label[static_cast<size_t>(n)]<0&&alpha[static_cast<size_t>(n)]>=.12f){label[static_cast<size_t>(n)]=component;queue.push_back(n);}}}
        if(static_cast<int>(queue.size())>largestSize){largestSize=static_cast<int>(queue.size());largest=component;}++component;
    }
    if(largestSize<1000)return {};
    juce::Image mask(juce::Image::ARGB,w,h,true);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int at=y*w+x;if(label[static_cast<size_t>(at)]==largest)mask.setPixelAt(x,y,juce::Colours::white.withAlpha(alpha[static_cast<size_t>(at)]));}
    return mask;
}

class GestureSlider final:public juce::Slider {
public:
    bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey||k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){juce::Slider::ScopedDragNotification gesture(*this);return juce::Slider::keyPressed(k);}return juce::Slider::keyPressed(k);}
    void mouseDoubleClick(const juce::MouseEvent& e)override{if(getSliderStyle()==juce::Slider::IncDecButtons){juce::Slider::ScopedDragNotification gesture(*this);setValue(getDoubleClickReturnValue(),juce::sendNotificationSync);}else juce::Slider::mouseDoubleClick(e);}
};
class ResetSliderButton final:public juce::TextButton {
public:
    ResetSliderButton(juce::Slider& slider,bool increment):juce::TextButton(increment?"+":"-"),owner(slider){}
    void mouseUp(const juce::MouseEvent& e)override{
        const bool reset=e.getNumberOfClicks()>1&&!e.mouseWasDraggedSinceMouseDown()&&isEnabled();
        juce::Component::SafePointer<juce::Slider> slider(&owner);
        // Let the ordinary step complete first, so the second release cannot
        // increment again after restoring the default. Text editing is separate.
        juce::TextButton::mouseUp(e);
        if(reset&&slider!=nullptr&&slider->getSliderStyle()==juce::Slider::IncDecButtons&&slider->isDoubleClickReturnEnabled()){
            juce::Slider::ScopedDragNotification gesture(*slider);
            slider->setValue(slider->getDoubleClickReturnValue(),juce::sendNotificationSync);
        }
    }
private:juce::Slider& owner;
};
class SculptureLook final:public juce::LookAndFeel_V4 {
public:
    explicit SculptureLook(GillNextProcessor& processor):p(processor){
        setColour(juce::Slider::textBoxTextColourId,ink);setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.9f));setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        setColour(juce::TextButton::textColourOffId,ink);setColour(juce::TextButton::textColourOnId,cream);
        setColour(juce::ComboBox::backgroundColourId,dark);setColour(juce::ComboBox::textColourId,cream);setColour(juce::ComboBox::outlineColourId,copper);setColour(juce::ComboBox::arrowColourId,cream);
        setColour(juce::PopupMenu::backgroundColourId,cream);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,sage);
        setColour(juce::TooltipWindow::backgroundColourId,cream);setColour(juce::TooltipWindow::textColourId,ink);
    }
    juce::Font getTextButtonFont(juce::TextButton&,int h)override{return face(std::clamp(h*.43f,12.f,14.f),true);}
    juce::Font getComboBoxFont(juce::ComboBox&)override{return face(13,true);}
    juce::Font getLabelFont(juce::Label& l)override{return l.getFont();}
    juce::Button*createSliderButton(juce::Slider& slider,bool increment)override{return new ResetSliderButton(slider,increment);}
    juce::Label*createSliderTextBox(juce::Slider& s)override{auto* l=LookAndFeel_V4::createSliderTextBox(s);l->setFont(face(12,true));l->setColour(juce::Label::textColourId,ink);l->setColour(juce::Label::backgroundColourId,cream.withAlpha(.94f));l->setColour(juce::TextEditor::textColourId,ink);l->setColour(juce::TextEditor::backgroundColourId,cream);return l;}
    void drawComboBox(juce::Graphics& g,int w,int h,bool,int,int,int,int,juce::ComboBox&)override{const auto r=juce::Rectangle<float>(1,1,w-2.f,h-2.f);g.setColour(juce::Colours::black.withAlpha(.2f));g.fillRoundedRectangle(r.translated(0,2),h*.45f);g.setColour(dark);g.fillRoundedRectangle(r,h*.45f);g.setColour(copper);g.drawRoundedRectangle(r,h*.45f,1);juce::Path a;a.addTriangle(w-16.f,h*.44f,w-8.f,h*.44f,w-12.f,h*.60f);g.setColour(cream);g.fillPath(a);}
    void positionComboBoxText(juce::ComboBox& box,juce::Label& label)override{label.setBounds(12,1,box.getWidth()-34,box.getHeight()-2);label.setFont(getComboBoxFont(box));label.setJustificationType(juce::Justification::centred);}
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);g.setColour(juce::Colours::black.withAlpha(.2f));g.fillRoundedRectangle(r.translated(0,2),r.getHeight()*.48f);g.setGradientFill(juce::ColourGradient(b.getToggleState()?sage.brighter(.12f):cream.brighter(.08f),0,0,b.getToggleState()?dark:cream.darker(over?.08f:.14f),0,r.getBottom(),false));g.fillRoundedRectangle(r,r.getHeight()*.48f);g.setColour(down?ink:copper.withAlpha(.7f));g.drawRoundedRectangle(r,r.getHeight()*.48f,1);}
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle style,juce::Slider&)override{const bool vertical=style==juce::Slider::LinearVertical;const float cx=x+w*.5f,cy=y+h*.5f;const auto track=vertical?juce::Rectangle<float>(cx-3.f,static_cast<float>(y),6.f,static_cast<float>(h)):juce::Rectangle<float>(static_cast<float>(x),cy-3,static_cast<float>(w),6.f);g.setColour(juce::Colours::black.withAlpha(.6f));g.fillRoundedRectangle(track.expanded(2),5);g.setColour(sage);g.fillRoundedRectangle(track,3);auto thumb=vertical?juce::Rectangle<float>(cx-14,pos-11,28,22):juce::Rectangle<float>(pos-9,cy-13,18,26);gill::material::fader(g,thumb,vertical,cream,sage);}
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float end,juce::Slider& s)override{
        const float size=std::min(static_cast<float>(w),static_cast<float>(h))-12.f,radius=size*.5f,cx=x+w*.5f,cy=y+h*.5f;
        gill::material::rotary(g, {static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)}, value, start, end, sage);
        if(static_cast<bool>(s.getProperties()["ringMeter"])){
            const float inner=radius*.56f;const auto m=juce::Rectangle<float>(inner*2,inner*2).withCentre({cx,cy+7});g.setGradientFill(juce::ColourGradient(dark.brighter(.10f),m.getX(),m.getY(),juce::Colour(0xff142923),m.getRight(),m.getBottom(),false));g.fillEllipse(m);g.setColour(copper.withAlpha(.8f));g.drawEllipse(m,1.5f);
            const float gr=std::max(0.f,p.reduction.load()),fraction=std::clamp(gr/24.f,0.f,1.f);for(int i=0;i<32;++i){const float a=-2.2f+4.4f*i/31.f;g.setColour(i<juce::roundToInt(fraction*32)?juce::Colour(0xffb7e0bb):sage.withAlpha(.25f));g.drawLine(cx+std::sin(a)*inner*.78f,cy+7-std::cos(a)*inner*.78f,cx+std::sin(a)*inner*.90f,cy+7-std::cos(a)*inner*.90f,4.f);}
            text(g,"GR",{cx-inner,cy-inner*.47f,inner*2,18},12,cream);text(g,juce::String(gr,1),{cx-inner,cy-6,inner*2,30},std::clamp(inner*.54f,20.f,31.f),cream);text(g,"DB",{cx-inner,cy+inner*.53f,inner*2,16},12,cream.withAlpha(.8f));
        }
    }
private:GillNextProcessor& p;
};

struct Binding {
    GestureSlider slider;juce::Label label;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    Binding(GillNextProcessor& p,const gillnext::ParamSpec& d){const juce::String id(d.id);slider.setName(d.name);slider.setComponentID(id);slider.setSliderStyle(juce::Slider::Rotary);slider.setRotaryParameters(pi*1.25f,pi*2.75f,true);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,84,22);slider.setTextValueSuffix(d.unit);slider.setWantsKeyboardFocus(true);slider.setTooltip(juce::String(d.name)+": IM KREIS ZIEHEN, MAUSRAD ODER ZAHL EINGEBEN. DOPPELKLICK: STANDARD.");auto* param=p.apvts.getParameter(id);slider.setDoubleClickReturnValue(true,param->convertFrom0to1(param->getDefaultValue()));
        
        attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,slider);if(id=="gate"){slider.setTextValueSuffix("");slider.textFromValueFunction=[](double v){return v<=-89.95?juce::String("OFF"):juce::String(v,1)+" DB";};slider.valueFromTextFunction=[](const juce::String& v){return v.trim().equalsIgnoreCase("OFF")?-90.:v.getDoubleValue();};slider.updateText();}slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.96f));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);label.setText(d.name,juce::dontSendNotification);label.setColour(juce::Label::textColourId,ink);label.setFont(face(12,true));label.setJustificationType(juce::Justification::centred);label.setInterceptsMouseClicks(false,false);
    }
};

class ValueReadout final:public juce::Label {
public:
    ValueReadout(GillNextProcessor& owner,const gillnext::ParamSpec& spec):p(owner),d(spec){
        setName(d.name);setComponentID(d.id);setEditable(false,true,false);setJustificationType(juce::Justification::centred);setFont(face(13,true));setColour(textColourId,cream);setColour(backgroundColourId,dark);setColour(juce::TextEditor::textColourId,ink);setColour(juce::TextEditor::backgroundColourId,cream);setTooltip(juce::String(d.name)+": DOPPELKLICK AUF DIE ZAHL ZUM EINGEBEN.");
        onTextChange=[this]{if(!refreshing){const auto input=getText().replaceCharacter(',','.');const float v=input.getFloatValue();if(std::isfinite(v))p.setValue(d.id,v);refresh();}};refresh();
    }
    void refresh(){if(isBeingEdited())return;const int decimals=d.step>=1?0:d.step<.1f?2:1;const auto t=juce::String(p.value(d.id),decimals)+juce::String(d.unit);if(t!=getText()){refreshing=true;setText(t,juce::dontSendNotification);refreshing=false;}}
private:GillNextProcessor&p;gillnext::ParamSpec d;bool refreshing=false;
};

class VoicePad final:public juce::Component,public juce::SettableTooltipClient {
public:
    explicit VoicePad(GillNextProcessor& processor):p(processor){setName("PITCH / FORMANT PAD");setComponentID("voicePad");setWantsKeyboardFocus(true);setTooltip("PUNKT ZIEHEN: LINKS/RECHTS = PITCH, OBEN/UNTEN = FORMANT. DOPPELKLICK: NEUTRAL. LINK: FORMANT FOLGT PITCH.");}
    ~VoicePad()override{finish();}
    juce::Rectangle<float> area()const{return getLocalBounds().toFloat().reduced(25,25);}
    void mouseDown(const juce::MouseEvent&e)override{finish();grabKeyboardFocus();linked=p.value("link")>.5f;dragging=true;p.apvts.getParameter("pitch")->beginChangeGesture();if(!linked)p.apvts.getParameter("formant")->beginChangeGesture();mouseDrag(e);}
    void mouseDrag(const juce::MouseEvent&e)override{if(!dragging)return;const auto a=area();p.setValue("pitch",std::clamp((e.position.x-a.getX())/a.getWidth()*24-12,-12.f,12.f),false);if(!linked)p.setValue("formant",std::clamp(12-(e.position.y-a.getY())/a.getHeight()*24,-12.f,12.f),false);repaint();}
    void mouseUp(const juce::MouseEvent&)override{finish();}
    void mouseDoubleClick(const juce::MouseEvent&)override{finish();p.resetForm();repaint();}
    bool keyPressed(const juce::KeyPress&k)override{if(k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){p.setValue("pitch",p.value("pitch")+(k==juce::KeyPress::leftKey?-.1f:.1f));return true;}if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey){const juce::String id=p.value("link")>.5f?"pitch":"formant";p.setValue(id,p.value(id)+(k==juce::KeyPress::upKey?.1f:-.1f));return true;}return false;}
    void paint(juce::Graphics&g)override{const auto r=getLocalBounds().toFloat(),a=area();g.setGradientFill(juce::ColourGradient(dark.brighter(.08f),0,0,juce::Colour(0xff192723),r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,31);g.setColour(copper);g.drawRoundedRectangle(r.reduced(1),30,1);for(int semitone=-12;semitone<=12;semitone+=3){const float x=a.getCentreX()+semitone*a.getWidth()/24,y=a.getCentreY()-semitone*a.getHeight()/24;g.setColour(cream.withAlpha(semitone==0?.35f:.08f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());g.drawHorizontalLine(juce::roundToInt(y),a.getX(),a.getRight());}g.setColour(cream.withAlpha(.5f));g.drawEllipse(juce::Rectangle<float>(15,15).withCentre(a.getCentre()),1);const float pitch=p.value("pitch"),formant=p.value("link")>.5f?pitch:p.value("formant");const juce::Point<float> point(a.getCentreX()+pitch*a.getWidth()/24,a.getCentreY()-formant*a.getHeight()/24);disc(g,juce::Rectangle<float>(24,24).withCentre(point));g.setColour(juce::Colour(0xff8f8396));g.fillEllipse(juce::Rectangle<float>(7,7).withCentre(point));text(g,"FORMANT",{0,4,r.getWidth(),18},12,cream);text(g,"-12    PITCH    +12",{0,r.getBottom()-20,r.getWidth(),18},12,cream);}
private:void finish(){if(dragging){p.apvts.getParameter("pitch")->endChangeGesture();if(!linked)p.apvts.getParameter("formant")->endChangeGesture();dragging=false;}}GillNextProcessor&p;bool dragging=false,linked=false;
};

class NextGraph final:public juce::Component,public juce::SettableTooltipClient {
public:
    explicit NextGraph(GillNextProcessor&processor):p(processor){input.fill(-90);output.fill(-90);gain.fill(0);setName("SIGNAL DISPLAY");setComponentID("signalGraph");setWantsKeyboardFocus(true);if(p.kind==NextKind::Ride)setTooltip("OBERE LINIE: TARGET ZIEHEN. UNTERE GAIN-ANSICHT: MAXIMALE ANHEBUNG/ABSENKUNG AN DEN PUNKTEN ZIEHEN. GELB = EINGANG, HELL = AUSGANG, GRUEN = TATSAECHLICHE VERSTAERKUNG.");if(p.kind==NextKind::Pocket)setTooltip("AUF DEN BEAT LEGEN UND DIE VOCAL ALS EXTERNEN SIDECHAIN ZUFUEHREN. LOW/HIGH-PUNKTE BEGRENZEN DEN FOKUSBEREICH. DIE LINIE ZEIGT DEN TATSAECHLICHEN FREQUENZGANG.");}
    ~NextGraph()override{finish();}
    void tick(){for(auto*a:{&input,&output,&gain})std::move(a->begin()+1,a->end(),a->begin());input.back()=levelDb(p.inputRms.load());output.back()=levelDb(p.outputRms.load());gain.back()=p.appliedGain.load();repaint();}
    juce::Rectangle<float> plot()const{return getLocalBounds().toFloat().reduced(30,29).withTrimmedBottom(7);}
    float maxHz()const{return static_cast<float>(std::max(80.,std::min(20000.,p.uiRate.load()*.45)));}
    float xFor(float hz)const{auto a=plot();return a.getX()+std::log(std::clamp(hz,20.f,maxHz())/20.f)/std::log(maxHz()/20.f)*a.getWidth();}
    float hzAt(float x)const{auto a=plot();return 20*std::pow(maxHz()/20,std::clamp((x-a.getX())/a.getWidth(),0.f,1.f));}
    juce::Rectangle<float> rideLevels()const{return {12,25,getWidth()-45.f,getHeight()*.47f};}
    juce::Rectangle<float> rideGain()const{return {12,getHeight()*.70f,getWidth()-45.f,getHeight()*.23f};}
    float levelY(float db)const{const auto a=rideLevels();return a.getBottom()-std::clamp((db+48)/48,0.f,1.f)*a.getHeight();}
    float gainY(float db)const{const auto a=rideGain();return a.getCentreY()-std::clamp(db/18,-1.f,1.f)*a.getHeight()*.5f;}
    void mouseDown(const juce::MouseEvent&e)override{finish();if(p.kind==NextKind::Ride){if(e.position.y<rideGain().getY()-5)active="target";else active=std::abs(e.position.y-gainY(p.value("up")))<std::abs(e.position.y-gainY(-p.value("down")))?"up":"down";}else if(p.kind==NextKind::Pocket)active=std::abs(e.position.x-xFor(p.value("low")))<std::abs(e.position.x-xFor(p.value("high")))?"low":"high";if(active.isEmpty())return;grabKeyboardFocus();p.apvts.getParameter(active)->beginChangeGesture();mouseDrag(e);}
    void mouseDrag(const juce::MouseEvent&e)override{if(active.isEmpty())return;if(p.kind==NextKind::Ride){if(active=="target"){auto a=rideLevels();p.setValue(active,(a.getBottom()-e.position.y)*48/a.getHeight()-48,false);}else{auto a=rideGain();const float db=(a.getCentreY()-e.position.y)*36/a.getHeight();p.setValue(active,active=="up"?db:-db,false);}}else{float hz=hzAt(e.position.x);hz=active=="low"?std::min(hz,p.value("high")/1.25f):std::max(hz,p.value("low")*1.25f);p.setValue(active,hz,false);}repaint();}
    void mouseUp(const juce::MouseEvent&)override{finish();}
    bool keyPressed(const juce::KeyPress&k)override{if(p.kind!=NextKind::Ride&&p.kind!=NextKind::Pocket)return false;if(k!=juce::KeyPress::leftKey&&k!=juce::KeyPress::rightKey&&k!=juce::KeyPress::upKey&&k!=juce::KeyPress::downKey)return false;const bool up=k==juce::KeyPress::rightKey||k==juce::KeyPress::upKey;const juce::String id=p.kind==NextKind::Ride?"target":"low";p.setValue(id,p.kind==NextKind::Ride?p.value(id)+(up?.1f:-.1f):p.value(id)*(up?1.02f:.98f));return true;}
    void paint(juce::Graphics&g)override{auto r=getLocalBounds().toFloat();g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff142521),r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,22);g.setColour(copper);g.drawRoundedRectangle(r.reduced(1),21,1);
        if(p.kind==NextKind::Ride){paintRide(g);return;}if(p.kind==NextKind::Align){paintAlign(g);return;}
        auto a=plot();for(int db:{-12,-6,0,6}){const float y=a.getCentreY()-db*a.getHeight()/30;g.setColour(cream.withAlpha(db==0?.25f:.07f));g.drawHorizontalLine(juce::roundToInt(y),a.getX(),a.getRight());text(g,juce::String(db),{1,y-8,26,16},12,cream.withAlpha(.7f));}for(float hz:{100.f,1000.f,10000.f})if(hz<maxHz()){const float x=xFor(hz);g.setColour(cream.withAlpha(.1f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());if(p.kind!=NextKind::Pocket||hz==1000)text(g,hz==100?"100":hz==1000?"1K":"10K",{x-22,r.getBottom()-23,44,16},12,cream.withAlpha(.75f));}
        auto data=p.graph();juce::Path curve;for(size_t i=0;i<data.size();++i){const float x=a.getX()+a.getWidth()*i/(data.size()-1),y=std::clamp(a.getCentreY()-data[i]*a.getHeight()/30,a.getY(),a.getBottom());if(i==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);}g.setColour(sage.brighter(.7f));g.strokePath(curve,juce::PathStrokeType(2));
        text(g,p.kind==NextKind::Pocket?"BEAT RESPONSE":"TONE RESPONSE",{12,5,190,17},12,cream,juce::Justification::left);if(p.kind==NextKind::Pocket){text(g,"CUT "+juce::String(p.reduction.load(),1)+" DB",{r.getWidth()-154,5,140,17},12,cream,juce::Justification::right);for(const char*id:{"low","high"}){const float x=xFor(p.value(id));g.setColour(cream.withAlpha(.4f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());disc(g,{x-6,a.getY()-4,12,12});}}
    }
private:
    template<class A,class F>void trace(juce::Graphics&g,const A&v,juce::Rectangle<float>a,F y,juce::Colour c){juce::Path line;for(size_t i=0;i<v.size();++i){const float x=a.getX()+a.getWidth()*i/(v.size()-1),yy=y(v[i]);if(i==0)line.startNewSubPath(x,yy);else line.lineTo(x,yy);}g.setColour(c);g.strokePath(line,juce::PathStrokeType(1.3f));}
    void paintRide(juce::Graphics&g){auto a=rideLevels(),b=rideGain();text(g,"IN / OUT",{12,5,100,17},12,cream,juce::Justification::left);text(g,(p.appliedGain.load()>=0?"+":"")+juce::String(p.appliedGain.load(),1)+" DB",{getWidth()-116.f,5,104,17},13,sage.brighter(.8f),juce::Justification::right);for(float db:{-36.f,-24.f,-12.f}){g.setColour(cream.withAlpha(.1f));g.drawHorizontalLine(juce::roundToInt(levelY(db)),a.getX(),a.getRight());}trace(g,input,a,[this](float v){return levelY(v);},juce::Colour(0xffd0a569));trace(g,output,a,[this](float v){return levelY(v);},cream.withAlpha(.75f));const float target=levelY(p.value("target"));g.setColour(cream.withAlpha(.8f));g.drawHorizontalLine(juce::roundToInt(target),a.getX(),a.getRight());disc(g,{a.getX()+10,target-5,10,10});text(g,"T",{a.getRight()+5,target-8,21,17},12,cream);
        const float upper=gainY(p.value("up")),lower=gainY(-p.value("down"));g.setColour(sage.withAlpha(.24f));g.fillRect(b.getX(),upper,b.getWidth(),std::max(1.f,lower-upper));g.setColour(cream.withAlpha(.23f));g.drawHorizontalLine(juce::roundToInt(b.getCentreY()),b.getX(),b.getRight());trace(g,gain,b,[this](float v){return gainY(v);},sage.brighter(.8f));for(float y:{upper,lower}){g.setColour(cream.withAlpha(.6f));g.drawHorizontalLine(juce::roundToInt(y),b.getX(),b.getRight());disc(g,{b.getRight()-4,y-4,8,8});}text(g,"+18",{b.getRight()+3,b.getY()-6,28,15},12,cream.withAlpha(.65f));text(g,"-18",{b.getRight()+3,b.getBottom()-10,28,15},12,cream.withAlpha(.65f));}
    void paintAlign(juce::Graphics&g){const float guide=p.guideSeconds.load(),dub=p.doubleSeconds.load(),duration=std::max(.001f,std::max(guide,dub));const auto r=getLocalBounds().toFloat();for(int lane=0;lane<2;++lane){const float top=lane*r.getHeight()*.5f;auto a=juce::Rectangle<float>(9,top+22,r.getWidth()-18,r.getHeight()*.5f-30);g.setColour(cream.withAlpha(.13f));g.drawHorizontalLine(juce::roundToInt(a.getCentreY()),a.getX(),a.getRight());auto samples=p.waveform(lane);const float seconds=lane==0?guide:dub;juce::Path wave;for(size_t i=0;i<samples.size();++i){const float x=a.getX()+a.getWidth()*seconds/duration*i/(samples.size()-1),amplitude=std::clamp(std::abs(samples[i]),0.f,1.f)*a.getHeight()*.46f;g.setColour(lane==0?cream:sage.brighter(.8f));g.drawVerticalLine(juce::roundToInt(x),a.getCentreY()-amplitude,a.getCentreY()+amplitude);}text(g,(lane==0?"GUIDE  ":p.alignState.load()==3?"ALIGNED  ":"DOUBLE  ")+juce::String(seconds,1)+" S",{12,top+3,200,17},12,lane==0?cream:sage.brighter(.8f),juce::Justification::left);}g.setColour(cream.withAlpha(.18f));g.drawHorizontalLine(getHeight()/2,9,r.getWidth()-9);if(guide<=0&&dub<=0)text(g,"CAPTURE GUIDE + DOUBLE",r.reduced(20),12,cream.withAlpha(.5f));}
    void finish(){if(active.isNotEmpty()){p.apvts.getParameter(active)->endChangeGesture();active.clear();}}GillNextProcessor&p;juce::String active;std::array<float,128>input{},output{},gain{};
};

class CrownMeter final:public juce::Component {
public:explicit CrownMeter(GillNextProcessor&processor):p(processor){setInterceptsMouseClicks(false,false);}
    void paint(juce::Graphics&g)override{auto r=getLocalBounds().toFloat().reduced(2);g.setColour(dark);g.fillEllipse(r);g.setGradientFill(juce::ColourGradient(cream,r.getX(),r.getY(),juce::Colour(0xffc4b89d),r.getRight(),r.getBottom(),false));g.fillEllipse(r.reduced(6));text(g,"INPUT",{0,17,r.getWidth()+4,18},12);text(g,juce::String(levelDb(p.inputRms.load()),1),{0,40,r.getWidth()+4,28},23);text(g,"DBFS",{0,71,r.getWidth()+4,18},12);const auto c=r.getCentre();for(int i=0;i<16;++i){const float a=-2.1f+i*4.2f/15;const float radius=r.getWidth()*.44f;g.setColour(i<(levelDb(p.inputRms.load())+60)*16/60?sage:ink.withAlpha(.18f));g.fillEllipse(c.x+std::sin(a)*radius-2,c.y-std::cos(a)*radius-2,4,4);}}
private:GillNextProcessor&p;
};
}

struct GillNextEditor::Impl:private juce::Timer {
    GillNextEditor&owner;GillNextProcessor&p;SculptureLook look;juce::Image wood,logoMask;juce::Component canvas;juce::TooltipWindow tooltip;NextGraph graph;VoicePad pad;CrownMeter crown;
    std::vector<std::unique_ptr<Binding>> controls;std::vector<std::unique_ptr<ValueReadout>>values;
    struct Toggle{juce::String id;juce::TextButton button;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>attachment;};std::vector<std::unique_ptr<Toggle>>toggles;
    struct Choice{juce::String id;juce::ComboBox box;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>attachment;};std::vector<std::unique_ptr<Choice>>choices;
    juce::ComboBox preset;juce::TextButton previous{"<"},next{">"},learn{"LEARN"},apply{"APPLY"},revert{"REVERT"},reset{"RESET"},guide{"CAPTURE"},dub{"CAPTURE"},align{"ALIGN"},undo{"UNDO"},clear{"CLEAR"};std::array<juce::TextButton,3>listen;juce::Label status;
    int width=600,height=300;
    gill::QualitySelector quality{p.apvts, p};
    Impl(GillNextEditor&o,GillNextProcessor&v):owner(o),p(v),look(v),tooltip(&o,650),graph(v),pad(v),crown(v){
        owner.setLookAndFeel(&look);owner.setOpaque(true);owner.addAndMakeVisible(canvas);canvas.addAndMakeVisible(quality);canvas.setInterceptsMouseClicks(false,true);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);logoMask=extractLogo(wood);
        if(p.kind==NextKind::Clean){width=350;height=440;canvas.addAndMakeVisible(crown);}else if(p.kind==NextKind::Pocket){width=580;height=340;canvas.addAndMakeVisible(graph);}else if(p.kind==NextKind::Align){width=650;height=370;canvas.addAndMakeVisible(graph);}else if(p.kind==NextKind::Form){width=510;height=350;canvas.addAndMakeVisible(pad);}else if(p.kind==NextKind::Finish){width=760;height=460;canvas.addAndMakeVisible(graph);}else canvas.addAndMakeVisible(graph);
        for(const auto&d:p.definitions){const juce::String id(d.id);if(p.kind==NextKind::Clean&&id=="listen")continue;if(d.choices.empty()){
            const bool readout=(p.kind==NextKind::Ride&&(id=="target"||id=="up"||id=="down"))||(p.kind==NextKind::Pocket&&(id=="low"||id=="high"))||(p.kind==NextKind::Form&&(id=="pitch"||id=="formant"));if(readout){auto c=std::make_unique<ValueReadout>(p,d);canvas.addAndMakeVisible(*c);values.push_back(std::move(c));continue;}
            auto c=std::make_unique<Binding>(p,d);if(p.kind==NextKind::Clean||(p.kind==NextKind::Finish&&(id=="width"||id=="bassmono")))c->slider.setSliderStyle(juce::Slider::LinearHorizontal);canvas.addAndMakeVisible(c->label);canvas.addAndMakeVisible(c->slider);controls.push_back(std::move(c));
        }else if(d.choices.size()==2){auto c=std::make_unique<Toggle>();c->id=id;c->button.setComponentID(id);c->button.setName(d.name);c->button.setButtonText(d.name);c->button.setClickingTogglesState(true);c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,id,c->button);canvas.addAndMakeVisible(c->button);toggles.push_back(std::move(c));}
        else{auto c=std::make_unique<Choice>();c->id=id;c->box.setComponentID(id);c->box.setName(d.name);for(size_t i=0;i<d.choices.size();++i)c->box.addItem(d.choices[i],static_cast<int>(i+1));c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,id,c->box);canvas.addAndMakeVisible(c->box);choices.push_back(std::move(c));}}
        auto action=[this](juce::TextButton&b,const juce::String&id,auto fn,const juce::String&tip){b.setName(id);b.setComponentID(id);b.onClick=fn;b.setTooltip(tip);canvas.addAndMakeVisible(b);};
        if(p.kind==NextKind::Clean){for(int i=0;i<3;++i){listen[static_cast<size_t>(i)].setButtonText("S");action(listen[static_cast<size_t>(i)],"listen"+juce::String(i+1),[this,i]{p.setValue("listen",p.value("listen")==i+1?0.f:static_cast<float>(i+1));},"NUR DAS ENTFERNTE SIGNAL DIESES MODULS ANHOEREN. ERNEUT KLICKEN: NORMALER AUSGANG.");}}
        if(p.kind==NextKind::Align){action(guide,"captureGuide",[this]{p.capture(0);},"GUIDE-VOCAL ALS SIDECHAIN ROUTEN, PHRASE ABSPIELEN UND CAPTURE DRUECKEN. ERNEUT DRUECKEN BEENDET DIE AUFNAHME. MAXIMAL 20 SEKUNDEN.");action(dub,"captureDouble",[this]{p.capture(1);},"PLUGIN AUF DIE DOUBLE-SPUR LEGEN. DIESELBE PHRASE VOM GLEICHEN STARTPUNKT ABSPIELEN UND AUFNEHMEN. ERNEUT KLICKEN: STOP.");action(align,"align",[this]{p.alignTakes();},"DIE BEIDEN AUFNAHMEN ANALYSIEREN UND EINE ZEITLICH ANGEPASSTE DOUBLE-PHRASE VORBEREITEN.");action(undo,"undo",[this]{p.undoAlignment();},"ALIGNMENT ZURUECKNEHMEN; AUFNAHMEN BLEIBEN ERHALTEN.");action(clear,"clear",[this]{p.clearCaptures();},"BEIDE AUFNAHMEN UND ALIGNMENT ENTFERNEN.");}
        if(p.kind==NextKind::Form)action(reset,"resetForm",[this]{p.resetForm();},"PITCH UND FORMANT AUF NEUTRAL ZURUECKSETZEN.");
        if(p.kind==NextKind::Finish){action(learn,"learn",[this]{if(p.learnState==1||p.learnState==4)p.stopLearn();else p.startLearn();},"DEN FERTIGEN SONG ABSPIELEN. LEARN ANALYSIERT UND SCHLAEGT EINSTELLUNGEN VOR; APPLY UEBERNIMMT SIE.");action(apply,"apply",[this]{p.applyLearn();},"DIE ANGEZEIGTE LEARN-EMPFEHLUNG ANWENDEN.");action(revert,"revert",[this]{p.revertLearn();},"ZU DEN EINSTELLUNGEN VOR APPLY ZURUECKKEHREN.");action(reset,"resetMeters",[this]{p.resetMeters();},"LOUDNESS- UND TRUE-PEAK-MESSUNG NEU STARTEN.");}
        for(auto&t:toggles){if(t->id=="hold")t->button.setTooltip("HOLD FRIERT DIE AKTUELLE VERSTAERKUNG EIN. RIDE PASST DEN VOCALPEGEL LAUFEND AN.");if(t->id=="listen"&&p.kind==NextKind::Pocket)t->button.setTooltip("NUR DEN AUS DEM BEAT ENTFERNTEN ANTEIL HOEREN. EXTERNEN VOCAL-SIDECHAIN ZUFUEHREN.");if(t->id=="preview")t->button.setTooltip("DIE ALIGNTE AUFNAHME WIEDERGEBEN. BEI HOST-TRANSPORTZEIT BEGINNT SIE AN DER AUFGENOMMENEN DOUBLE-POSITION. DAS ORIGINAL-AUDIOFILE WIRD NICHT VERAENDERT.");if(t->id=="link")t->button.setTooltip("FORMANT FOLGT DEM PITCH-WERT. AUSSCHALTEN, UM BEIDE ACHSEN UNABHAENGIG ZU BEARBEITEN.");if(t->id=="transients")t->button.setTooltip("KONSONANTEN UND ANSCHLAEGE IM PITCH-/FORMANT-EFFEKT SCHONEN.");if(t->id=="match")t->button.setTooltip("AUSGANGSLAUTSTAERKE AN DEN EINGANG ANGLEICHEN, UM DEN KLANG FAIR ZU VERGLEICHEN. FUER DIE FINALE LAUTSTAERKE WIEDER AUSSCHALTEN.");}
        status.setName("STATUS");status.setColour(juce::Label::textColourId,ink);status.setFont(face(12,true));status.setJustificationType(juce::Justification::centred);canvas.addAndMakeVisible(status);
        preset.setName("PRESET");preset.setComponentID("preset");for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.onChange=[this]{if(preset.getSelectedId()>0)p.selectPreset(preset.getSelectedId()-1);};previous.setName("PREVIOUS PRESET");next.setName("NEXT PRESET");previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+p.getNumPrograms()-1)%p.getNumPrograms());};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%p.getNumPrograms());};canvas.addAndMakeVisible(preset);canvas.addAndMakeVisible(previous);canvas.addAndMakeVisible(next);refresh();startTimerHz(25);
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    Binding*control(const juce::String&id){for(auto&b:controls)if(b->slider.getComponentID()==id)return b.get();return nullptr;}
    void place(const juce::String&id,int x,int y,int w,int h,bool light=false){if(auto*b=control(id)){b->label.setBounds(x,y,w,18);b->label.setColour(juce::Label::textColourId,light?cream:ink);b->slider.setBounds(x,y+18,w,h-18);b->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,std::min(w,88),24);}}
    void button(const juce::String&id,int x,int y,int w,int h){for(auto&b:toggles)if(b->id==id)b->button.setBounds(x,y,w,h);}
    void field(const juce::String&id,int x,int y,int w,int h=24){for(auto&b:values)if(b->getComponentID()==id)b->setBounds(x,y,w,h);}
    void presetAt(int x,int y,int w){previous.setBounds(x,y,28,28);preset.setBounds(x+33,y,w-66,28);next.setBounds(x+w-28,y,28,28);}
    void resized(){const float scale=owner.getWidth()/static_cast<float>(width);canvas.setBounds(0,0,width,height);canvas.setTransform(juce::AffineTransform::scale(scale));
        if(p.kind==NextKind::Ride){quality.setBounds(382,18,110,26);graph.setBounds(118,60,367,138);button("bypass",503,17,82,28);button("hold",24,137,82,30);place("speed",496,105,86,125);field("target",123,219,98);field("down",251,219,98);field("up",378,219,98);presetAt(193,248,214);status.setBounds(18,181,90,40);}
        else if(p.kind==NextKind::Clean){quality.setBounds(222,18,110,26);crown.setBounds(124,55,102,102);button("bypass",263,86,66,28);const char*ids[]{"noise","plosives","breaths"};for(int i=0;i<3;++i){auto*b=control(ids[i]);const int x=42+i*9,y=161+i*61,w=208-i*17;if(b){b->label.setColour(juce::Label::textColourId,cream);b->label.setBounds(x,y,94,18);b->slider.setBounds(x,y+20,w,29);b->slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,57,26);}listen[static_cast<size_t>(i)].setBounds(x+w+5,y+20,38,29);}presetAt(85,351,180);status.setBounds(97,393,156,20);}
        else if(p.kind==NextKind::Pocket){quality.setBounds(342,20,110,26);graph.setBounds(89,64,403,143);button("bypass",468,20,85,27);place("speed",100,209,91,90);place("amount",235,206,106,93);place("maxcut",378,209,94,90);button("listen",499,149,55,27);field("low",104,178,89,23);field("high",388,178,89,23);presetAt(165,303,250);status.setBounds(20,99,68,63);}
        else if(p.kind==NextKind::Align){quality.setBounds(422,20,110,26);graph.setBounds(111,65,510,151);button("bypass",542,20,86,28);guide.setBounds(27,108,77,29);dub.setBounds(27,183,77,29);place("tightness",36,231,110,91);place("maxshift",163,231,112,91);align.setBounds(295,253,134,38);button("preview",445,245,86,29);undo.setBounds(445,285,86,29);clear.setBounds(535,283,74,29);status.setBounds(284,222,337,23);presetAt(189,332,282);}
        else if(p.kind==NextKind::Form){quality.setBounds(289,19,110,26);pad.setBounds(89,59,247,182);button("bypass",411,18,80,28);place("mix",367,135,110,105);button("link",377,247,91,27);button("transients",366,277,112,25);field("pitch",100,265,106);field("formant",221,265,106);reset.setBounds(90,297,87,27);presetAt(193,305,235);status.setBounds(20,184,66,49);}
        else{quality.setBounds(224,25,110,26);graph.setBounds(22,74,457,137);button("bypass",650,24,86,29);learn.setBounds(341,24,92,29);apply.setBounds(443,24,91,29);revert.setBounds(544,24,96,29);status.setBounds(22,214,457,22);reset.setBounds(660,212,71,24);button("toneon",22,247,216,27);button("compon",248,247,83,27);button("stereoon",342,247,99,27);button("clipon",451,247,83,27);button("limiteron",548,247,190,27);place("low",22,286,70,115);place("mid",95,286,70,115);place("high",168,286,70,115);place("comp",252,286,76,115);place("width",346,285,91,57);place("bassmono",346,346,91,57);place("clip",455,286,76,115);place("drive",551,286,79,115);place("ceiling",645,286,93,115);for(auto&c:choices)if(c->id=="style")c->box.setBounds(22,419,158,29);presetAt(211,419,332);button("match",555,419,116,29);button("mono",681,419,57,29);}
    }
    void refresh(){graph.tick();pad.repaint();crown.repaint();for(auto&v:values){v->refresh();if(p.kind==NextKind::Form&&v->getComponentID()=="formant")v->setEnabled(p.value("link")<.5f);}preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);preset.setText(p.getProgramName(p.getCurrentProgram())+(p.presetMatches()?"":" *"),juce::dontSendNotification);preset.setTooltip(p.getProgramName(p.getCurrentProgram()));
        if(p.kind==NextKind::Ride){for(auto&t:toggles)if(t->id=="hold")t->button.setButtonText(p.value("hold")>.5f?"HOLD":"RIDE");status.setText(p.voiceActivity.load()>.1f?"VOICE":"WAITING",juce::dontSendNotification);}
        else if(p.kind==NextKind::Clean){for(int i=0;i<3;++i)listen[static_cast<size_t>(i)].setToggleState(p.value("listen")==i+1,juce::dontSendNotification);status.setText(p.value("listen")>.5f?"LISTEN ACTIVE":"",juce::dontSendNotification);}
        else if(p.kind==NextKind::Pocket)status.setText(p.sidechainActive.load()?"VOCAL SC":"ROUTE\nVOCAL SC",juce::dontSendNotification);
        else if(p.kind==NextKind::Align){const int capture=p.captureState.load(),state=p.alignState.load();guide.setButtonText(capture==1?"STOP":"CAPTURE");dub.setButtonText(capture==2?"STOP":"CAPTURE");guide.setEnabled(capture!=2);dub.setEnabled(capture!=1);align.setEnabled(capture==0&&p.guideSeconds.load()>0&&p.doubleSeconds.load()>0&&state!=2);align.setButtonText(state==2?"ANALYSING":"ALIGN");undo.setEnabled(state==3);for(auto&t:toggles)if(t->id=="preview")t->button.setEnabled(state==3);status.setText(p.statusText(),juce::dontSendNotification);status.setTooltip(p.statusText());}
        else if(p.kind==NextKind::Form)status.setText(p.value("link")>.5f?"LINKED":"",juce::dontSendNotification);
        else{const int state=p.learnState.load();learn.setEnabled(true);learn.setButtonText(state==1?"FINISH":state==4?"CANCEL":"LEARN");apply.setEnabled(state==2);revert.setEnabled(state==3);if(state==1)status.setText("LISTENING "+juce::String(p.learnProgress.load()*300,1)+" S / 300 S",juce::dontSendNotification);else if(state==4)status.setText("ARMED / START THE SONG",juce::dontSendNotification);else if(state==5)status.setText("NOT ENOUGH AUDIO / PLAY & LEARN AGAIN",juce::dontSendNotification);else if(state==2)status.setText("READY  LOW "+juce::String(p.learnLow.load(),1)+"  MID "+juce::String(p.learnMid.load(),1)+"  HIGH "+juce::String(p.learnHigh.load(),1)+" DB",juce::dontSendNotification);else status.setText(p.statusText(),juce::dontSendNotification);status.setTooltip(p.statusText());}
        owner.repaint();}
    void timerCallback()override{refresh();}
    juce::Path silhouette()const {
        juce::Path body;
        body.addRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        return body;
    }
    bool hitTest(int x,int y)const{return owner.getLocalBounds().contains(x,y);}
    void logo(juce::Graphics&g,int x,int y,int w){if(logoMask.isValid()){g.setColour(cream.withAlpha(.55f));g.drawImage(logoMask,x,y+1,w,juce::roundToInt(w*.70f),0,0,174,122,true);g.setColour(juce::Colour(0xff372715));g.drawImage(logoMask,x,y,w,juce::roundToInt(w*.70f),0,0,174,122,true);}}
    void paint(juce::Graphics&g){g.fillAll(juce::Colour(0xff1d2423));g.addTransform(juce::AffineTransform::scale(owner.getWidth()/static_cast<float>(width)));const auto body=silhouette();{juce::Graphics::ScopedSaveState saved(g);g.reduceClipRegion(body);g.setColour(copper);g.fillRect(0,0,width,height);if(wood.isValid())g.drawImage(wood,0,0,width,height,950,285,170,805);g.setColour(p.kind==NextKind::Ride?juce::Colour(0xff4b2e22).withAlpha(.32f):p.kind==NextKind::Finish?juce::Colour(0xff3a2922).withAlpha(.18f):cream.withAlpha(p.kind==NextKind::Clean?.22f:.09f));g.fillRect(0,0,width,height);}g.setColour(juce::Colour(0xff67472d));g.strokePath(body,juce::PathStrokeType(3));g.setColour(cream.withAlpha(.65f));g.strokePath(body,juce::PathStrokeType(1));
        if(p.kind==NextKind::Ride){logo(g,20,17,40);text(g,"GILLRIDE",{76,16,174,29},20);text(g,"TARGET",{123,202,98,17},12);text(g,"DOWN",{251,202,98,17},12);text(g,"UP",{378,202,98,17},12);g.setColour(p.voiceActivity.load()>.1f?sage.brighter(.6f):ink.withAlpha(.3f));g.fillEllipse(60,173,8,8);}
        else if(p.kind==NextKind::Clean){logo(g,18,17,36);text(g,"GILLCLEAN",{63,17,149,26},16);juce::Path inset;inset.startNewSubPath(39,149);inset.cubicTo(97,135,115,157,175,157);inset.cubicTo(231,157,263,135,311,149);inset.cubicTo(330,160,316,281,287,341);inset.quadraticTo(175,363,62,341);inset.cubicTo(35,283,22,170,39,149);inset.closeSubPath();g.setColour(dark);g.fillPath(inset);for(int i=0;i<3;++i){const float amount=std::clamp(p.cleanReduction[static_cast<size_t>(i)].load()/24,0.f,1.f);const float x=151.f+i*5,y=166.f+i*61;for(int cell=0;cell<10;++cell){g.setColour(cell<amount*10?sage.brighter(.8f):cream.withAlpha(.16f));g.fillRoundedRectangle(x+cell*7,y,5,7,1);}}}
        else if(p.kind==NextKind::Pocket){logo(g,20,20,42);text(g,"GILLPOCKET",{76,18,240,29},21);text(g,"BEAT",{499,94,63,20},12);g.setColour(p.outputPeak.load()>.001f?sage.brighter(.6f):ink.withAlpha(.2f));g.fillEllipse(526,124,9,9);g.setColour(p.sidechainActive.load()?sage.brighter(.6f):juce::Colour(0xffa96e47));g.fillEllipse(52,87,9,9);}
        else if(p.kind==NextKind::Align){logo(g,20,20,44);text(g,"GILLALIGN",{78,18,240,30},22);text(g,"GUIDE SC",{23,79,83,20},12);text(g,"DOUBLE",{23,154,83,20},12);}
        else if(p.kind==NextKind::Form){logo(g,20,17,40);text(g,"GILLFORM",{75,16,186,29},20);text(g,"PITCH",{100,245,106,17},12);text(g,"FORMANT",{221,245,106,17},12);}
        else{logo(g,25,21,47);text(g,"GILLFINISH",{91,19,128,35},20);g.setColour(dark);g.fillRoundedRectangle(492,74,246,160,16);const auto lufs=[](float n){return n<=-90?juce::String("--"):juce::String(n,1);};text(g,"LUFS M",{502,82,104,17},12,cream.withAlpha(.7f));text(g,"LUFS I",{615,82,111,17},12,cream.withAlpha(.7f));text(g,lufs(p.momentaryLufs.load()),{502,101,104,30},25,cream);text(g,lufs(p.integratedLufs.load()),{615,101,111,30},25,cream);text(g,"TRUE PEAK",{502,141,108,17},12,cream.withAlpha(.7f));text(g,lufs(p.truePeakDb.load())+" DBTP",{502,159,108,24},16,cream);text(g,"LIMIT GR",{616,141,110,17},12,cream.withAlpha(.7f));text(g,juce::String(p.limiterReduction.load(),1)+" DB",{616,159,110,24},16,sage.brighter(.7f));text(g,"GLUE "+juce::String(p.compressorReduction.load(),1)+" DB",{504,206,145,21},12,cream.withAlpha(.8f));g.setColour(ink.withAlpha(.18f));for(int x:{242,337,445,540})g.drawVerticalLine(x,283,402);}
        if(!p.rateSupported.load())text(g,"UNSUPPORTED RATE / BYPASS",{width*.15f,height-18.f,width*.7f,16},12,juce::Colour(0xffa34435));
    }
};
GillNextEditor::GillNextEditor(GillNextProcessor&p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);setResizable(true,true);setResizeLimits(impl->width,impl->height,impl->width*2,impl->height*2);if(auto*c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(impl->width)/impl->height);setSize(impl->width,impl->height);}
GillNextEditor::~GillNextEditor()=default;
void GillNextEditor::paint(juce::Graphics&g){impl->paint(g);}void GillNextEditor::resized(){if(impl)impl->resized();}bool GillNextEditor::hitTest(int x,int y){return impl&&impl->hitTest(x,y);}



