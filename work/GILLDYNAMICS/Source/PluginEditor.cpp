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
    explicit SculptureLook(GillDynamicsProcessor& processor):p(processor){
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
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle style,juce::Slider&)override{const bool vertical=style==juce::Slider::LinearVertical;const float cx=x+w*.5f,cy=y+h*.5f;const auto track=vertical?juce::Rectangle<float>(cx-3.f,static_cast<float>(y),6.f,static_cast<float>(h)):juce::Rectangle<float>(static_cast<float>(x),cy-3,static_cast<float>(w),6.f);g.setColour(juce::Colours::black.withAlpha(.6f));g.fillRoundedRectangle(track.expanded(2),5);g.setColour(sage);g.fillRoundedRectangle(track,3);auto thumb=vertical?juce::Rectangle<float>(cx-14,pos-11,28,22):juce::Rectangle<float>(pos-9,cy-13,18,26);g.setColour(juce::Colours::black.withAlpha(.3f));g.fillRoundedRectangle(thumb.translated(1,3),3);g.setGradientFill(juce::ColourGradient(cream.brighter(.1f),0,thumb.getY(),cream.darker(.2f),0,thumb.getBottom(),false));g.fillRoundedRectangle(thumb,3);g.setColour(ink.withAlpha(.6f));if(vertical)g.drawHorizontalLine(juce::roundToInt(pos),thumb.getX()+3,thumb.getRight()-3);else g.drawVerticalLine(juce::roundToInt(pos),thumb.getY()+3,thumb.getBottom()-3);}
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float end,juce::Slider& s)override{
        const float size=std::min(static_cast<float>(w),static_cast<float>(h))-12.f,radius=size*.5f,cx=x+w*.5f,cy=y+h*.5f;
        gill::material::rotary(g, {static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)}, value, start, end, sage);
        if(static_cast<bool>(s.getProperties()["ringMeter"])){
            const float inner=radius*.56f;const auto m=juce::Rectangle<float>(inner*2,inner*2).withCentre({cx,cy+7});g.setGradientFill(juce::ColourGradient(dark.brighter(.10f),m.getX(),m.getY(),juce::Colour(0xff142923),m.getRight(),m.getBottom(),false));g.fillEllipse(m);g.setColour(copper.withAlpha(.8f));g.drawEllipse(m,1.5f);
            const float gr=std::max(0.f,p.reduction.load()),fraction=std::clamp(gr/24.f,0.f,1.f);for(int i=0;i<32;++i){const float a=-2.2f+4.4f*i/31.f;g.setColour(i<juce::roundToInt(fraction*32)?juce::Colour(0xffb7e0bb):sage.withAlpha(.25f));g.drawLine(cx+std::sin(a)*inner*.78f,cy+7-std::cos(a)*inner*.78f,cx+std::sin(a)*inner*.90f,cy+7-std::cos(a)*inner*.90f,4.f);}
            text(g,"GR",{cx-inner,cy-inner*.47f,inner*2,18},12,cream);text(g,juce::String(gr,1),{cx-inner,cy-6,inner*2,30},std::clamp(inner*.54f,20.f,31.f),cream);text(g,"DB",{cx-inner,cy+inner*.53f,inner*2,16},12,cream.withAlpha(.8f));
        }
    }
private:GillDynamicsProcessor& p;
};

struct Binding {
    GestureSlider slider;juce::Label label;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    Binding(GillDynamicsProcessor& p,const gilldyn::ParamSpec& d){const juce::String id(d.id);slider.setName(d.name);slider.setComponentID(id);slider.setSliderStyle(juce::Slider::Rotary);slider.setRotaryParameters(pi*1.25f,pi*2.75f,true);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,84,22);slider.setTextValueSuffix(d.unit);slider.setWantsKeyboardFocus(true);slider.setTooltip(juce::String(d.name)+": IM KREIS ZIEHEN, MAUSRAD ODER ZAHL EINGEBEN. DOPPELKLICK: STANDARD.");auto* param=p.apvts.getParameter(id);slider.setDoubleClickReturnValue(true,param->convertFrom0to1(param->getDefaultValue()));
        
        attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,slider);if(id=="gate"){slider.setTextValueSuffix("");slider.textFromValueFunction=[](double v){return v<=-89.95?juce::String("OFF"):juce::String(v,1)+" DB";};slider.valueFromTextFunction=[](const juce::String& v){return v.trim().equalsIgnoreCase("OFF")?-90.:v.getDoubleValue();};slider.updateText();}slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.96f));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);label.setText(d.name,juce::dontSendNotification);label.setColour(juce::Label::textColourId,ink);label.setFont(face(12,true));label.setJustificationType(juce::Justification::centred);label.setInterceptsMouseClicks(false,false);
    }
};

class AnalogMeter final:public juce::Component {
public:enum Type {Optical,Input,Output};AnalogMeter(GillDynamicsProcessor& processor,Type type):p(processor),kind(type){setInterceptsMouseClicks(false,false);}
    void paint(juce::Graphics& g)override{const auto r=getLocalBounds().toFloat().reduced(2);const bool round=kind==Optical;g.setColour(juce::Colours::black.withAlpha(.3f));if(round)g.fillEllipse(r.translated(0,4));else g.fillRoundedRectangle(r.translated(0,4),13);g.setColour(dark);if(round)g.fillEllipse(r);else g.fillRoundedRectangle(r,12);const auto faceRect=r.reduced(round?18.f:9.f);g.setGradientFill(juce::ColourGradient(juce::Colour(0xffc4b490),0,faceRect.getY(),juce::Colour(0xfff7ebc9),0,faceRect.getBottom(),false));if(round)g.fillEllipse(faceRect);else g.fillRoundedRectangle(faceRect,8);
        const bool gr=round&&juce::roundToInt(p.value("meter"))==1;const bool input=kind==Input||(round&&p.value("meter")<.5f);const float reading=gr?std::max(0.f,p.reduction.load()):levelDb(round?(input?p.inputRms.load():p.outputRms.load()):p.channelOutputRms[kind==Input?0:1].load())+18.f;const float fraction=gr?1.f-std::clamp(reading/24.f,0.f,1.f):std::clamp((reading+24.f)/30.f,0.f,1.f);const float cx=r.getCentreX(),py=round?r.getHeight()*.80f:r.getHeight()*1.12f,rad=round?r.getWidth()*.55f:r.getHeight()*.80f,span=round?.65f:.75f;juce::Path scale;scale.addCentredArc(cx,py,rad,rad,0,-span,span,true);g.setColour(ink);g.strokePath(scale,juce::PathStrokeType(1.2f));
        const int ticks=gr?8:10;for(int i=0;i<=ticks;++i){const float a=-span+2*span*i/static_cast<float>(ticks);g.setColour(i>8&&!gr?juce::Colour(0xffa84332):ink);g.drawLine(cx+std::sin(a)*(rad-5),py-std::cos(a)*(rad-5),cx+std::sin(a)*(rad+6),py-std::cos(a)*(rad+6),1.2f);if(i%2==0){const juce::String t(gr?juce::String(24-i*3):juce::String(-24+i*3));text(g,t,{cx+std::sin(a)*(rad+14)-18,py-std::cos(a)*(rad+14)-8,36,16},round?12.f:11.f);}}
        const float a=-span+fraction*2*span;g.setColour(ink.withAlpha(.2f));g.drawLine(cx+2,py,cx+std::sin(a)*(rad+1)+2,py-std::cos(a)*(rad+1),4);g.setColour(dark);g.drawLine(cx,py,cx+std::sin(a)*(rad+1),py-std::cos(a)*(rad+1),2.2f);
        if(round)text(g,gr?"GAIN REDUCTION":"VU",{20,r.getHeight()*.51f,r.getWidth()-40,26},gr?15.f:20.f);else{g.setColour(dark);g.fillRoundedRectangle(faceRect.getX(),faceRect.getBottom()-21,faceRect.getWidth(),21,4);text(g,kind==Input?"L / VU":"R / VU",{faceRect.getX(),faceRect.getBottom()-21,faceRect.getWidth(),20},12,cream);}
        if(round){text(g,gr?juce::String(reading,1)+" DB":"0 VU = -18 DBFS",{25,r.getHeight()*.63f,r.getWidth()-50,23},11,ink.withAlpha(.8f));disc(g,juce::Rectangle<float>(30,30).withCentre({cx,py}));}
    }
private:GillDynamicsProcessor& p;Type kind;
};

class QuadGraph final:public juce::Component,public juce::SettableTooltipClient {
public:explicit QuadGraph(GillDynamicsProcessor& processor):p(processor){setName("FREQUENCY RESPONSE / CROSSOVERS");setComponentID("quadGraph");setWantsKeyboardFocus(true);setTooltip("DREI TRENNFREQUENZEN AN DEN PUNKTEN ZIEHEN. DIE LINIE ZEIGT DEN TATSAECHLICHEN GESAMTFREQUENZGANG.");}
    ~QuadGraph()override{finish();}
    void mouseDown(const juce::MouseEvent& e)override{finish();grabKeyboardFocus();float best=1.e9f;for(int i=0;i<3;++i){const float distance=std::abs(xFor(crossover(i))-e.position.x);if(distance<best){best=distance;active=i;}}dragging=true;if(auto* a=p.apvts.getParameter(id()))a->beginChangeGesture();mouseDrag(e);}
    void mouseDrag(const juce::MouseEvent& e)override{if(dragging)change(hzAt(e.position.x));}
    void mouseUp(const juce::MouseEvent&)override{finish();}
    bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){if(auto* a=p.apvts.getParameter(id()))a->beginChangeGesture();change(crossover(active)*(k==juce::KeyPress::leftKey?.98f:1.02f));if(auto* a=p.apvts.getParameter(id()))a->endChangeGesture();return true;}return false;}
    void paint(juce::Graphics& g)override{auto r=getLocalBounds().toFloat();g.setGradientFill(juce::ColourGradient(dark.brighter(.08f),0,0,juce::Colour(0xff192a25),0,r.getBottom(),false));g.fillRoundedRectangle(r,35);g.setColour(copper.withAlpha(.6f));g.drawRoundedRectangle(r.reduced(1),34,1);const auto a=plot();auto y=[&](float gain){return a.getCentreY()-gain*a.getHeight()/36.f;};for(int d:{-12,0,12}){g.setColour(cream.withAlpha(d==0?.3f:.1f));g.drawHorizontalLine(juce::roundToInt(y(static_cast<float>(d))),a.getX(),a.getRight());text(g,juce::String(d),{3,y(static_cast<float>(d))-8,36,16},12,cream.withAlpha(.75f));}for(int hz:{20,100,1000,10000,20000})if(hz<=maximum()){const float x=xFor(static_cast<float>(hz));g.setColour(cream.withAlpha(.09f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());text(g,hz>=1000?juce::String(hz/1000)+"K":juce::String(hz),{x-23,r.getHeight()-23,46,16},12,cream.withAlpha(.75f));}
        const auto values=p.graph();juce::Path line;for(size_t i=0;i<values.size();++i){const float x=a.getX()+a.getWidth()*static_cast<float>(i)/127.f,yy=std::clamp(y(values[i]),a.getY(),a.getBottom());if(i==0)line.startNewSubPath(x,yy);else line.lineTo(x,yy);}auto fill=line;fill.lineTo(a.getRight(),a.getCentreY());fill.lineTo(a.getX(),a.getCentreY());fill.closeSubPath();g.setColour(sage.withAlpha(.16f));g.fillPath(fill);g.setColour(juce::Colour(0xffc1e4d0));g.strokePath(line,juce::PathStrokeType(2.3f));for(int i=0;i<3;++i){const float x=xFor(crossover(i));g.setColour(bandColours[static_cast<size_t>(i)].withAlpha(.5f));const float dashes[]{4,4};g.drawDashedLine({x,a.getY(),x,a.getBottom()},dashes,2,1);disc(g,{x-6,a.getY()-7,12,12});}text(g,"DYNAMIC RESPONSE",{getWidth()-184.f,5,166,17},12,cream.withAlpha(.65f));}
private:
    juce::Rectangle<float> plot()const{return getLocalBounds().toFloat().reduced(44,23).withTrimmedBottom(4);}
    double maximum()const{return std::max(40.,std::min(20000.,p.uiRate.load()*.45));}
    float crossover(int i)const{
        // Handle positions reflect the current control targets immediately,
        // including while transport is stopped. The plotted response above
        // remains the processor's actual cached audio-engine response.
        const float top=static_cast<float>(.45*std::clamp(p.uiRate.load(),8000.,192000.));
        std::array<float,3> c{{p.value("cross1"),p.value("cross2"),p.value("cross3")}};
        for(auto& v:c)v=std::clamp(v,20.f,top);
        std::sort(c.begin(),c.end());
        c[0]=std::min(c[0],top/(1.35f*1.35f));
        c[1]=std::clamp(c[1],c[0]*1.35f,top/1.35f);
        c[2]=std::clamp(c[2],c[1]*1.35f,top);
        return c[static_cast<size_t>(i)];
    }
    float xFor(float hz)const{const auto a=plot();return a.getX()+static_cast<float>(std::log(std::clamp(static_cast<double>(hz),20.,maximum())/20.)/std::log(maximum()/20.))*a.getWidth();}
    float hzAt(float x)const{const auto a=plot();return static_cast<float>(20.*std::pow(maximum()/20.,std::clamp((x-a.getX())/a.getWidth(),0.f,1.f)));}
    juce::String id()const{return "cross"+juce::String(active+1);}
    void change(float hz){if(active>0)hz=std::max(hz,crossover(active-1)*1.35f);if(active<2)hz=std::min(hz,crossover(active+1)/1.35f);p.setValue(id(),hz,false);repaint();}
    void finish(){if(dragging){if(auto* a=p.apvts.getParameter(id()))a->endChangeGesture();dragging=false;}}
    GillDynamicsProcessor& p;int active=0;bool dragging=false;
};

class SpatialMap final:public juce::Component,public juce::SettableTooltipClient {
public:explicit SpatialMap(GillDynamicsProcessor& processor):p(processor){setName("POSITION / DISTANCE");setComponentID("stageMap");setWantsKeyboardFocus(true);setTooltip("QUELLE ZIEHEN: LINKS/RECHTS UND NAEHE. PFEILTASTEN: FEINBEWEGUNG. DOPPELKLICK: MITTE/NAH.");for(auto* l:{&positionValue,&distanceValue}){addAndMakeVisible(*l);l->setEditable(true,true);l->setJustificationType(juce::Justification::centred);l->setColour(juce::Label::textColourId,cream);l->setColour(juce::TextEditor::textColourId,ink);l->setColour(juce::TextEditor::backgroundColourId,cream);l->setFont(face(11,true));}positionValue.setName("POSITION VALUE");positionValue.setComponentID("x");distanceValue.setName("DISTANCE VALUE");distanceValue.setComponentID("distance");positionValue.onTextChange=[this]{p.setValue("x",positionValue.getText().getFloatValue()*.01f);};distanceValue.onTextChange=[this]{p.setValue("distance",distanceValue.getText().getFloatValue()*.01f);};refresh();}
    ~SpatialMap()override{finish();}
    bool hitTest(int x,int y)override{
        const auto r=getLocalBounds().toFloat().reduced(3);
        const auto offset=juce::Point<float>(static_cast<float>(x),static_cast<float>(y))-r.getCentre();
        const float radius=std::min(r.getWidth(),r.getHeight())*.5f;
        // Both editable value labels lie inside this circle and keep their
        // normal child-component hit testing and text-entry behaviour.
        return offset.x*offset.x+offset.y*offset.y<=radius*radius;
    }
    void resized()override{positionValue.setBounds(getWidth()/2-116,getHeight()/2+45,110,23);distanceValue.setBounds(getWidth()/2+6,getHeight()/2+45,110,23);}
    void refresh(){if(!positionValue.isBeingEdited())positionValue.setText(juce::String(p.value("x")*100,1)+" %",juce::dontSendNotification);if(!distanceValue.isBeingEdited())distanceValue.setText(juce::String(p.value("distance")*100,1)+" %",juce::dontSendNotification);repaint();}
    void mouseDown(const juce::MouseEvent& e)override{finish();grabKeyboardFocus();dragging=true;for(const char* id:{"x","distance"})if(auto* a=p.apvts.getParameter(id))a->beginChangeGesture();mouseDrag(e);}
    void mouseDrag(const juce::MouseEvent& e)override{if(!dragging)return;const auto c=getLocalBounds().toFloat().getCentre();const float radius=getWidth()*.45f,qy=std::clamp((e.position.y-c.y)/radius,-.75f,.75f),x=(e.position.x-c.x)/(radius*.84f*std::sqrt(1-qy*qy));p.setValue("x",std::clamp(x,-1.f,1.f),false);p.setValue("distance",.5f-qy/1.5f,false);refresh();}
    void mouseUp(const juce::MouseEvent&)override{finish();}
    void mouseDoubleClick(const juce::MouseEvent&)override{finish();p.setValue("x",0);p.setValue("distance",0);refresh();}
    bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){p.setValue("x",p.value("x")+(k==juce::KeyPress::leftKey?-.02f:.02f));refresh();return true;}if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey){p.setValue("distance",p.value("distance")+(k==juce::KeyPress::upKey?.02f:-.02f));refresh();return true;}return false;}
    void paint(juce::Graphics& g)override{const auto r=getLocalBounds().toFloat().reduced(3);const auto c=r.getCentre();const float radius=getWidth()*.45f;g.setGradientFill(juce::ColourGradient(dark.brighter(.08f),r.getX(),r.getY(),juce::Colour(0xff152f29),r.getRight(),r.getBottom(),false));g.fillEllipse(r);g.setColour(copper);g.drawEllipse(r,3);g.setColour(cream.withAlpha(.55f));g.drawEllipse(r.reduced(4),.7f);
        g.setColour(cream.withAlpha(.14f));g.drawLine(c.x-radius*.76f,c.y,c.x+radius*.76f,c.y,1);g.drawLine(c.x,c.y-radius*.78f,c.x,c.y+radius*.78f,1);for(float factor:{.30f,.56f,.80f}){juce::Path circle;circle.addEllipse(juce::Rectangle<float>(radius*2*factor,radius*2*factor).withCentre(c));const float dashes[]{4,5};juce::Path dashed;juce::PathStrokeType(1).createDashedStroke(dashed,circle,dashes,2);g.fillPath(dashed);}
        text(g,"FAR",{c.x-30,17,60,20},12,cream);text(g,"NEAR",{c.x-35,getHeight()-30.f,70,20},12,cream);text(g,"LEFT",{14,c.y-10,58,20},12,cream);text(g,"RIGHT",{getWidth()-76.f,c.y-10,64,20},12,cream);
        const float qy=(.5f-p.value("distance"))*1.5f;const auto source=juce::Point<float>(c.x+p.value("x")*radius*.84f*std::sqrt(1-qy*qy),c.y+qy*radius);const float doubler=std::clamp(p.value("doubler")*.01f,0.f,1.f),spread=std::clamp(p.value("spread")*.01f,0.f,1.f);if(doubler>.001f){const float offset=(12+spread*45)*std::sqrt(doubler);for(float direction:{-1.f,1.f}){auto point=source+juce::Point<float>(direction*offset,18+10*doubler);const auto delta=point-c;if(delta.getDistanceFromOrigin()>radius*.84f)point=c+delta*(radius*.84f/delta.getDistanceFromOrigin());g.setColour(cream.withAlpha(.35f));g.drawLine({source,point},1);disc(g,juce::Rectangle<float>(13+6*doubler,13+6*doubler).withCentre(point));}}
        if(p.apvts.getParameter("direct")==nullptr||p.value("direct")>.5f){disc(g,juce::Rectangle<float>(34,34).withCentre(source));g.setColour(sage);g.fillEllipse(juce::Rectangle<float>(5,5).withCentre(source));}else{g.setColour(cream.withAlpha(.75f));g.drawEllipse(juce::Rectangle<float>(30,30).withCentre(source),2);g.drawLine(source.x-8,source.y+8,source.x+8,source.y-8,2);}text(g,"POSITION",{c.x-116,c.y+29,110,16},12,cream.withAlpha(.6f));text(g,"DISTANCE",{c.x+6,c.y+29,110,16},12,cream.withAlpha(.6f));
    }
private:void finish(){if(dragging){for(const char* id:{"x","distance"})if(auto* a=p.apvts.getParameter(id))a->endChangeGesture();dragging=false;}}GillDynamicsProcessor& p;juce::Label positionValue,distanceValue;bool dragging=false;
};
}

struct GillDynamicsEditor::Impl:private juce::Timer {
    GillDynamicsEditor& owner;GillDynamicsProcessor& p;SculptureLook look;juce::Image wood,logoMask;juce::Component canvas;juce::TooltipWindow tooltip;
    std::vector<std::unique_ptr<Binding>> controls;
    struct Switch {juce::TextButton button;juce::String id;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>attachment;};
    struct Choice {juce::String id;juce::Label label;juce::ComboBox box;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>attachment;std::vector<std::unique_ptr<juce::TextButton>>segments;};
    std::vector<std::unique_ptr<Switch>> switches;std::vector<std::unique_ptr<Choice>> choices;
    juce::ComboBox preset;juce::TextButton previous{"<"},next{">"},groupsToggle{"GROUPS +"};QuadGraph graph;SpatialMap map;AnalogMeter optical,inMeter,outMeter;
    int width=480,height=380;bool groupsOpen=false;
    gill::QualitySelector quality{p.apvts, p};
    Impl(GillDynamicsEditor& editor,GillDynamicsProcessor& processor):owner(editor),p(processor),look(processor),tooltip(&editor,650),graph(processor),map(processor),optical(processor,AnalogMeter::Optical),inMeter(processor,AnalogMeter::Input),outMeter(processor,AnalogMeter::Output){
        owner.setLookAndFeel(&look);owner.setOpaque(true);owner.addAndMakeVisible(canvas);canvas.addAndMakeVisible(quality);canvas.setInterceptsMouseClicks(false,true);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);logoMask=extractLogo(wood);
        if(p.kind==DynKind::Opta){width=390;height=570;canvas.addAndMakeVisible(optical);}else if(p.kind==DynKind::Buss){width=800;height=330;canvas.addAndMakeVisible(inMeter);canvas.addAndMakeVisible(outMeter);canvas.addAndMakeVisible(groupsToggle);groupsToggle.setName("SHOW GROUPS");groupsToggle.setComponentID("groupDrawer");groupsToggle.setTooltip("ACHT VCA-GRUPPEN EIN- ODER AUSKLAPPEN; DER KLANG BLEIBT UNVERAENDERT.");groupsToggle.onClick=[this]{const float scale=owner.getWidth()/static_cast<float>(width);groupsOpen=!groupsOpen;height=groupsOpen?570:330;groupsToggle.setButtonText(groupsOpen?"GROUPS -":"GROUPS +");owner.setResizeLimits(width,height,width*2,height*2);if(auto*c=owner.getConstrainer())c->setFixedAspectRatio(static_cast<double>(width)/height);owner.setSize(juce::roundToInt(width*scale),juce::roundToInt(height*scale));resized();owner.repaint();};}else if(p.kind==DynKind::Quad){width=900;height=550;canvas.addAndMakeVisible(graph);}else if(p.kind==DynKind::Stage){width=500;height=540;canvas.addAndMakeVisible(map);}
        for(const auto& d:p.definitions){const juce::String id(d.id);if(p.kind==DynKind::Stage&&(id=="x"||id=="distance"||id=="direct"))continue;
            if(d.choices.empty()){auto b=std::make_unique<Binding>(p,d);if(p.kind==DynKind::Vox&&id=="comp"){b->slider.getProperties().set("ringMeter",true);b->label.setFont(face(14,true));}if(p.kind==DynKind::Buss&&id=="drive")b->slider.setTooltip("DRIVE: KALIBRIERTE SAETTIGUNGSSTAERKE. TRIM REGELT DIE LAUTSTAERKE.");if(p.kind==DynKind::Buss&&id.startsWith("g")&&id.endsWith("trim"))b->slider.setSliderStyle(juce::Slider::LinearVertical);if(id.startsWith("cross")){b->slider.setSliderStyle(juce::Slider::IncDecButtons);b->slider.setTextBoxStyle(juce::Slider::TextBoxLeft,false,88,25);}if(p.kind==DynKind::Quad&&(id.endsWith("attack")||id.endsWith("release")))b->slider.setSliderStyle(juce::Slider::LinearHorizontal);canvas.addAndMakeVisible(b->slider);if(id.startsWith("cross"))b->slider.sendLookAndFeelChange();canvas.addAndMakeVisible(b->label);controls.push_back(std::move(b));
            }else if(id=="bypass"||id=="noise"||id=="mono"||id.endsWith("solo")||id.endsWith("bypass")||id.endsWith("noise")){addSwitch(id,d.name);}
            else{auto c=std::make_unique<Choice>();c->id=id;c->label.setText(d.name,juce::dontSendNotification);c->label.setJustificationType(juce::Justification::centred);c->label.setFont(face(12,true));c->label.setColour(juce::Label::textColourId,ink);canvas.addAndMakeVisible(c->label);if(id=="style"||id=="limit"||id=="meter"){for(size_t i=0;i<d.choices.size();++i){auto b=std::make_unique<juce::TextButton>(d.choices[i]);b->setName(id);b->setComponentID(id);b->getProperties().set("choiceIndex",static_cast<int>(i));b->onClick=[this,id,i]{p.setValue(id,static_cast<float>(i));};canvas.addAndMakeVisible(*b);c->segments.push_back(std::move(b));}}else{c->box.setName(d.name);c->box.setComponentID(id);for(size_t i=0;i<d.choices.size();++i)c->box.addItem(d.choices[i],static_cast<int>(i+1));c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,id,c->box);canvas.addAndMakeVisible(c->box);}choices.push_back(std::move(c));}
        }
        if(p.kind==DynKind::Stage&&p.apvts.getParameter("direct")!=nullptr){addSwitch("direct","DIRECT ON");switches.back()->button.setTooltip("DIRECT OFF: NUR DOUBLER UND RAUM. MIX BLENDET DIE DIREKTE STIMME DANN NICHT WIEDER EIN. BYPASS GIBT DAS ORIGINAL WIEDER.");}
        preset.setName("PRESET");preset.setComponentID("preset");for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.onChange=[this]{if(preset.getSelectedId()>0)p.selectPreset(preset.getSelectedId()-1);};previous.setName("PREVIOUS PRESET");next.setName("NEXT PRESET");previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+p.getNumPrograms()-1)%p.getNumPrograms());};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%p.getNumPrograms());};canvas.addAndMakeVisible(preset);canvas.addAndMakeVisible(previous);canvas.addAndMakeVisible(next);refresh();startTimerHz(30);
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    void addSwitch(const juce::String& id,const juce::String& name){if(p.apvts.getParameter(id)==nullptr)return;auto b=std::make_unique<Switch>();b->id=id;b->button.setName(id);b->button.setComponentID(id);b->button.setButtonText(name);b->button.setClickingTogglesState(true);b->button.setTooltip(name+": EIN / AUS");b->attachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,id,b->button);canvas.addAndMakeVisible(b->button);switches.push_back(std::move(b));}
    Binding* control(const juce::String& id){for(auto& b:controls)if(b->slider.getComponentID()==id)return b.get();return nullptr;}
    void place(const juce::String& id,int x,int y,int w,int h,bool light=false){if(auto*b=control(id)){b->label.setBounds(x,y,w,18);b->label.setColour(juce::Label::textColourId,light?cream:ink);b->slider.setBounds(x,y+18,w,h-18);if(!id.startsWith("cross"))b->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,std::min(w,88),24);}}
    void button(const juce::String& id,int x,int y,int w,int h){for(auto&b:switches)if(b->id==id)b->button.setBounds(x,y,w,h);}
    void choice(const juce::String& id,int x,int y,int w){for(auto&c:choices)if(c->id==id){c->label.setBounds(x,y,w,17);if(c->segments.empty())c->box.setBounds(x,y+18,w,29);else{const int n=static_cast<int>(c->segments.size()),each=w/n;for(int i=0;i<n;++i)c->segments[static_cast<size_t>(i)]->setBounds(x+i*each,y+18,each-2,29);}}}
    void presetAt(int x,int y,int w){previous.setBounds(x,y,28,30);preset.setBounds(x+33,y,w-66,30);next.setBounds(x+w-28,y,28,30);}
    static bool groupControl(const juce::String&id){return id.length()>2&&id[0]=='g'&&id[1]>='1'&&id[1]<='8';}
    void resized(){const float scale=owner.getWidth()/static_cast<float>(width);canvas.setBounds(0,0,width,height);canvas.setTransform(juce::AffineTransform::scale(scale));
        if(p.kind==DynKind::Vox){quality.setBounds(250,20,110,26);button("bypass",378,20,82,28);place("comp",146,91,188,195);place("gate",39,230,106,108);place("output",335,239,106,108);presetAt(128,339,224);}
        else if(p.kind==DynKind::Opta){quality.setBounds(260,19,110,26);optical.setBounds(110,83,170,170);button("bypass",73,260,97,28);button("noise",220,260,99,28);place("gain",78,299,109,126);place("hf",233,297,83,80);place("reduction",216,378,126,130);choice("limit",62,428,126);choice("meter",61,480,148);presetAt(95,530,200);}
        else if(p.kind==DynKind::Buss){quality.setBounds(532,24,110,26);button("bypass",654,23,94,29);place("drive",42,78,129,146);place("trim",629,78,129,146);inMeter.setBounds(212,77,176,100);outMeter.setBounds(401,77,176,100);choice("style",215,185,276);choice("group",520,185,105);button("noise",645,239,104,29);groupsToggle.setBounds(48,239,123,29);
            for(auto&b:controls)if(groupControl(b->slider.getComponentID())){b->slider.setVisible(groupsOpen);b->label.setVisible(groupsOpen);}for(auto&b:switches)if(groupControl(b->id))b->button.setVisible(groupsOpen);
            for(int i=1;i<=8;++i){const int x=39+(i-1)*93;const auto prefix="g"+juce::String(i);place(prefix+"drive",x,301,76,77,true);place(prefix+"trim",x+2,379,72,85,true);button(prefix+"bypass",x,468,76,24);button(prefix+"noise",x,496,76,24);}presetAt(223,height-42,354);}
        else if(p.kind==DynKind::Quad){quality.setBounds(655,16,110,26);button("bypass",781,23,96,29);graph.setBounds(23,94,854,114);place("cross1",281,45,152,46);place("cross2",443,45,152,46);place("cross3",605,45,152,46);
            for(int i=1;i<=4;++i){const int x=25+(i-1)*194;const auto prefix="b"+juce::String(i);place(prefix+"threshold",x+44,237,98,86);place(prefix+"range",x+5,328,82,78);place(prefix+"gain",x+96,328,82,78);place(prefix+"attack",x+5,414,82,56);place(prefix+"release",x+96,414,82,56);button(prefix+"solo",x+7,475,78,27);button(prefix+"bypass",x+97,475,78,27);}place("output",808,250,70,113);presetAt(286,511,330);}
        else{quality.setBounds(284,18,110,26);map.setBounds(99,47,302,302);button("bypass",404,18,80,28);place("doubler",88,355,77,95);place("spread",170,355,77,95);place("mix",252,355,77,95);place("output",334,355,77,95);button("direct",108,455,137,28);button("mono",255,455,137,28);presetAt(132,489,236);}
    }
    void refresh(){if(p.kind==DynKind::Buss)p.syncGroups();const int current=p.getCurrentProgram();preset.setSelectedId(current+1,juce::dontSendNotification);const auto name=p.getProgramName(current)+(p.presetMatches()?"":" *");if(preset.getText()!=name)preset.setText(name,juce::dontSendNotification);for(auto&c:choices)for(size_t i=0;i<c->segments.size();++i)c->segments[i]->setToggleState(juce::roundToInt(p.value(c->id))==static_cast<int>(i),juce::dontSendNotification);for(auto&b:switches){if(b->id=="noise")b->button.setButtonText(p.value(b->id)>.5f?"NOISE ON":"NOISE OFF");if(b->id=="direct")b->button.setButtonText(p.value(b->id)>.5f?"DIRECT ON":"DIRECT OFF");}if(p.kind==DynKind::Stage)map.refresh();graph.repaint();optical.repaint();inMeter.repaint();outMeter.repaint();owner.repaint();}
    void timerCallback()override{refresh();}
    juce::Path silhouette()const {
        juce::Path body;
        body.addRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        return body;
    }
    bool hitTest(int x,int y)const{return owner.getLocalBounds().contains(x,y);}
    void woodBody(juce::Graphics&g,const juce::Path&q){g.setColour(juce::Colour(0xffb9895d));g.fillPath(q);{juce::Graphics::ScopedSaveState save(g);g.reduceClipRegion(q);if(wood.isValid())g.drawImage(wood,0,0,width,height,950,285,170,805);g.setColour(p.kind==DynKind::Stage?juce::Colour(0xff6c3f23).withAlpha(.34f):p.kind==DynKind::Buss?juce::Colour(0xff784a2e).withAlpha(.26f):p.kind==DynKind::Vox?juce::Colour(0xffc27a43).withAlpha(.10f):cream.withAlpha(.13f));g.fillRect(0,0,width,height);}g.setColour(juce::Colour(0xff715135).withAlpha(.9f));g.strokePath(q,juce::PathStrokeType(3));g.setColour(cream.withAlpha(.65f));g.strokePath(q,juce::PathStrokeType(1));}
    void logo(juce::Graphics&g,int x,int y,int w=46){const int h=juce::roundToInt(w*.70f);if(logoMask.isValid()){g.setColour(cream.withAlpha(.6f));g.drawImage(logoMask,x,y+1,w,h,0,0,174,122,true);g.setColour(juce::Colour(0xff352819).withAlpha(.9f));g.drawImage(logoMask,x,y,w,h,0,0,174,122,true);}else if(wood.isValid())g.drawImage(wood,x,y,w,h,224,146,174,122);}
    void bar(juce::Graphics&g,float x,float y,float w,float h,float value,const juce::String&caption){const int cells=12;const float amount=std::clamp((levelDb(value)+48.f)/54.f,0.f,1.f);for(int i=0;i<cells;++i){g.setColour(i<amount*cells?(i>10?juce::Colour(0xffb96d53):sage):ink.withAlpha(.16f));g.fillRoundedRectangle(x,y+h-(i+1)*h/cells,w,h/cells-2,2);}text(g,caption,{x-10,y+h+3,w+20,17},12);}
    void paint(juce::Graphics&g){
        // Fill the host's whole rectangular content area with the wood faceplate.
        g.fillAll(juce::Colour(0xff1d2423));g.addTransform(juce::AffineTransform::scale(owner.getWidth()/static_cast<float>(width)));woodBody(g,silhouette());
        if(p.kind==DynKind::Vox){logo(g,20,20,40);text(g,"GILLVOX",{76,18,156,32},22);text(g,"OUT "+juce::String(levelDb(p.outputPeak.load()),1)+" DBFS",{158,302,166,22},12,ink.withAlpha(.8f));}
        else if(p.kind==DynKind::Opta){logo(g,20,19,36);text(g,"GILLOPTA",{72,18,160,28},20);juce::Path inset;inset.startNewSubPath(199,289);inset.cubicTo(258,311,215,345,192,381);inset.cubicTo(169,419,190,465,198,488);g.setColour(dark);g.strokePath(inset,juce::PathStrokeType(10,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));}
        else if(p.kind==DynKind::Buss){gill::material::panel(g,{37,18,726,253},cream,19);logo(g,58,22,44);text(g,"GILLBUSS",{121,20,211,35},22);if(groupsOpen){g.setGradientFill(juce::ColourGradient(dark.brighter(.1f),25,280,dark.darker(.22f),770,525,false));g.fillRoundedRectangle(25,280,750,244,18);g.setColour(copper);g.drawRoundedRectangle(25,280,750,244,18,1);for(int i=1;i<=8;++i){const float x=39.f+(i-1)*93;text(g,juce::String(i),{x,282,76,17},12,cream);if(i>1){g.setColour(cream.withAlpha(.1f));g.drawVerticalLine(juce::roundToInt(x-9),304,516);}}}}
        else if(p.kind==DynKind::Quad){logo(g,31,22,42);text(g,"GILLQUAD",{86,18,174,29},22);text(g,"DYNAMIC EQ",{88,46,151,17},12);const char*names[]{"LOW","LOW MID","HIGH MID","HIGH"};for(int i=0;i<4;++i){const float x=25.f+i*194;gill::material::panel(g,{x-5,215,188,291},cream,29);text(g,names[i],{x+6,217,168,18},13);const float gr=p.bandReduction[static_cast<size_t>(i)].load();text(g,juce::String(gr,1)+" DB",{x+123,252,55,18},12,ink.withAlpha(.8f));}g.setColour(ink.withAlpha(.25f));g.drawLine(803,232,803,505,1);bar(g,825,391,10,82,p.channelOutputPeak[0].load(),"L");bar(g,857,391,10,82,p.channelOutputPeak[1].load(),"R");}
        else{logo(g,20,17,40);text(g,"GILLSTAGE",{74,16,186,29},20);juce::Graphics::ScopedSaveState saved(g);g.reduceClipRegion(silhouette());juce::Path pod;pod.startNewSubPath(55,349);pod.cubicTo(81,324,104,331,126,348);pod.cubicTo(188,380,312,380,374,348);pod.cubicTo(396,331,419,324,445,349);pod.cubicTo(469,392,440,456,412,481);pod.cubicTo(348,503,153,503,88,481);pod.cubicTo(60,456,31,392,55,349);pod.closeSubPath();g.setColour(juce::Colours::black.withAlpha(.2f));g.fillPath(pod,juce::AffineTransform::translation(0,2));g.setGradientFill(juce::ColourGradient(cream,70,351,cream.darker(.11f),430,488,false));g.fillPath(pod);g.setColour(cream.brighter(.2f));g.strokePath(pod,juce::PathStrokeType(1.3f));}
        if(!p.rateSupported.load())text(g,"UNSUPPORTED RATE / BYPASS",{width*.2f,height-19.f,width*.6f,16},12,juce::Colour(0xff924635));
    }
};

GillDynamicsEditor::GillDynamicsEditor(GillDynamicsProcessor&p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);setResizable(true,true);setResizeLimits(impl->width,impl->height,impl->width*2,impl->height*2);if(auto*c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(impl->width)/impl->height);setSize(impl->width,impl->height);}
GillDynamicsEditor::~GillDynamicsEditor()=default;
void GillDynamicsEditor::paint(juce::Graphics&g){impl->paint(g);}
void GillDynamicsEditor::resized(){if(impl)impl->resized();}
bool GillDynamicsEditor::hitTest(int x,int y){return impl&&impl->hitTest(x,y);}
