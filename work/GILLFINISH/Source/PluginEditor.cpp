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
    Binding(GillFinishProcessor& p,const juce::String& id,const juce::String& caption,const juce::String& suffix,bool vertical=false){
        slider.setName(caption);slider.setSliderStyle(vertical?juce::Slider::LinearVertical:juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,24);slider.setTextValueSuffix(suffix);auto* parameter=p.apvts.getParameter(id);slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));slider.setWantsKeyboardFocus(true);
        slider.setTooltip(caption+": ZIEHEN ODER ZAHL EINGEBEN | DOPPELKLICK: STANDARDWERT");attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,slider);
        slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.94f));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        label.setText(caption,juce::dontSendNotification);label.setJustificationType(juce::Justification::centred);label.setColour(juce::Label::textColourId,ink);label.setFont(font(12,true));
    }
};

class Display final:public juce::Component {
public:
    explicit Display(GillFinishProcessor&v):p(v){}
    void mouseDown(const juce::MouseEvent&e)override{if(p.kind!=FinishKind::Silk&&p.kind!=FinishKind::Spark)return;const auto hz=hzAt(e.position.x);dragId=std::abs(std::log(hz/std::max(20.f,p.value("low"))))<std::abs(std::log(hz/std::max(20.f,p.value("high"))))?"low":"high";if(auto*param=p.apvts.getParameter(dragId))param->beginChangeGesture();mouseDrag(e);}
    void mouseDrag(const juce::MouseEvent&e)override{if(dragId.isEmpty())return;float hz=hzAt(e.position.x);if(dragId=="low")hz=std::min(hz,p.value("high"));else hz=std::max(hz,p.value("low"));p.setValue(dragId,hz,false);repaint();}
    void mouseUp(const juce::MouseEvent&)override{if(auto*param=p.apvts.getParameter(dragId))param->endChangeGesture();dragId.clear();}
    void paint(juce::Graphics&g)override{
        auto r=getLocalBounds().toFloat();g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff14271f),r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,14);g.setColour(cream.withAlpha(.3f));g.drawRoundedRectangle(r.reduced(1),13,1);
        g.setFont(font(12,true));g.setColour(cream.withAlpha(.8f));g.drawText(p.kind==FinishKind::Silk?"DYNAMIC RESONANCE CONTROL":p.kind==FinishKind::Spark?"TRANSIENT CONTROL":p.kind==FinishKind::Gold?"TONE CURVE":p.kind==FinishKind::Dive?"FILTER / MOTION":"VOCAL CHANNEL",16,10,getWidth()-32,20,juce::Justification::left);
        if(!p.rateSupported.load()){g.setFont(font(18,true));g.drawText("UNSUPPORTED RATE / BYPASS",getLocalBounds(),juce::Justification::centred);return;}
        const float level=juce::Decibels::gainToDecibels(p.outputPeak.load(),-90.f);
        if(p.kind==FinishKind::Strip){g.setFont(font(20,true));const int third=(getWidth()-32)/3;g.drawText("IN "+juce::String(juce::Decibels::gainToDecibels(p.inputPeak.load(),-90.f),1)+" DBFS",16,37,third,30,juce::Justification::left);g.drawText("GR "+juce::String(p.reduction.load(),1)+" DB",16+third,37,third,30,juce::Justification::centred);g.drawText("OUT "+juce::String(level,1)+" DBFS",16+2*third,37,third,30,juce::Justification::right);return;}
        const auto a=r.reduced(37,38).withTrimmedBottom(9);const auto mx=std::min(20000.,p.uiRate.load()*.48);auto xFor=[&](double hz){return a.getX()+static_cast<float>(std::log(hz/40.)/std::log(mx/40.))*a.getWidth();};
        const bool spectral=p.kind==FinishKind::Silk||p.kind==FinishKind::Spark;auto yFor=[&](float d){return a.getY()+a.getHeight()*(spectral?(1.f/3.f):.5f)-d*a.getHeight()/(spectral?36:30);};
        for(int hz:{100,500,1000,5000,10000})if(hz<mx){const auto x=xFor(hz);g.setColour(cream.withAlpha(.07f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());g.setColour(cream.withAlpha(.6f));g.setFont(font(10));g.drawText(hz>=1000?juce::String(hz/1000)+"K":juce::String(hz),juce::roundToInt(x)-20,getHeight()-22,40,14,juce::Justification::centred);}
        for(int d:{-12,0,12}){g.setColour(cream.withAlpha(d==0?.25f:.08f));g.drawHorizontalLine(juce::roundToInt(yFor(static_cast<float>(d))),a.getX(),a.getRight());g.setColour(cream.withAlpha(.6f));g.drawText(juce::String(d),2,juce::roundToInt(yFor(static_cast<float>(d)))-7,30,15,juce::Justification::right);}
        juce::Path path;
        if(spectral){auto view=p.graph();for(size_t i=0;i<view.size();++i){const double hz=40*std::pow(500.,i/127.);if(hz>mx)break;const float x=xFor(hz),y=std::clamp(yFor(-view[i]),a.getY(),a.getBottom());if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}for(const char*id:{"low","high"}){const float x=std::clamp(xFor(p.value(id)),a.getX(),a.getRight());g.setColour(cream.withAlpha(.4f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());g.setColour(cream);g.fillEllipse(x-5,a.getY()-4,10,10);}g.setFont(font(13,true));g.setColour(cream);const float gr=p.reduction.load();g.drawText((gr<0?"BOOST ":"REDUCTION ")+juce::String(std::abs(gr),1)+" DB",getWidth()-211,29,196,20,juce::Justification::right);}
        else {gillfinish::GoldParameters gp;gillfinish::Biquad d1,d2;const float lowHz[]{20,30,60,100},highHz[]{3000,4000,5000,8000,10000,12000,16000},cutHz[]{5000,10000,20000};gillfinish::GoldDSP preview;
            if(p.kind==FinishKind::Gold){gp.lowBoost=p.value("lowboost");gp.lowCut=p.value("lowcut");gp.lowHz=lowHz[juce::jlimit(0,3,juce::roundToInt(p.value("lowfreq")))];gp.highBoost=p.value("highboost");gp.highCut=p.value("highcut");gp.highHz=highHz[juce::jlimit(0,6,juce::roundToInt(p.value("highfreq")))];gp.cutHz=cutHz[juce::jlimit(0,2,juce::roundToInt(p.value("cutfreq")))];gp.bandwidth=p.value("bandwidth");preview.setParameters(gp);preview.prepare(p.uiRate.load(),1,1);}
            else{d1.set(0,p.uiRate.load(),p.cutoff.load(),.5411961);d2.set(0,p.uiRate.load(),p.cutoff.load(),1.306563+p.value("resonance")*.014);g.setFont(font(22,true));g.setColour(cream);g.drawText(juce::String(juce::roundToInt(p.cutoff.load()))+" HZ",getWidth()-180,8,160,26,juce::Justification::right);}
            for(int i=0;i<=260;++i){const double hz=40*std::pow(mx/40.,i/260.);const auto mag=p.kind==FinishKind::Gold?preview.magnitude(hz):d1.magnitude(p.uiRate.load(),hz)*d2.magnitude(p.uiRate.load(),hz);const float y=std::clamp(yFor(static_cast<float>(gillfinish::db(mag))),a.getY(),a.getBottom());if(i==0)path.startNewSubPath(a.getX(),y);else path.lineTo(a.getX()+i*a.getWidth()/260,y);}
        }
        g.setColour(p.kind==FinishKind::Gold?juce::Colour(0xffe4be71):p.kind==FinishKind::Dive?juce::Colour(0xff91d8d2):sage.brighter(.8f));g.strokePath(path,juce::PathStrokeType(2.3f));
    }
private:
    float hzAt(float x)const{const auto maximum=std::min(20000.,p.uiRate.load()*.48);return static_cast<float>(40*std::pow(maximum/40.,juce::jlimit(0.f,1.f,(x-37)/(getWidth()-74))));}
    GillFinishProcessor&p;juce::String dragId;
};
}

struct GillFinishEditor::Impl:private juce::Timer{
    GillFinishEditor&owner;GillFinishProcessor&p;OakLook look;juce::Image wood;juce::Component canvas;Display display;juce::TooltipWindow tooltip;
    std::vector<std::unique_ptr<Binding>>controls;
    struct Choice{juce::ComboBox box;juce::Label label;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>attachment;juce::String id;};
    std::vector<std::unique_ptr<Choice>>choices;
    juce::TextButton bypass{"BYPASS"},delta{"DELTA"},sync{"SYNC"},previous{"<"},next{">"};juce::ComboBox preset;juce::Label info;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>bypassA,deltaA,syncA;
    int width=680,height=450;
    Impl(GillFinishEditor&o,GillFinishProcessor&v):owner(o),p(v),display(v),tooltip(&o,650){owner.setLookAndFeel(&look);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);owner.addAndMakeVisible(canvas);canvas.addAndMakeVisible(display);
        if(p.kind==FinishKind::Strip){width=820;height=420;}else if(p.kind==FinishKind::Gold){width=760;height=440;}else if(p.kind==FinishKind::Dive){width=600;height=460;}
        auto button=[&](juce::TextButton&b,const char*id,std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&a){b.setName(id);b.setClickingTogglesState(true);canvas.addAndMakeVisible(b);a=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,id,b);};button(bypass,"bypass",bypassA);
        for(const auto&d:p.definitions){const juce::String id(d.id);if(id=="bypass")continue;if(id=="delta"){button(delta,"delta",deltaA);delta.setTooltip("NUR DEN UNTERSCHIED ZUM ZEITLICH AUSGERICHTETEN ORIGINAL HOEREN");continue;}if(id=="sync"){button(sync,"sync",syncA);continue;}
            if(d.choices.empty()){auto b=std::make_unique<Binding>(p,id,d.name,d.unit,p.kind==FinishKind::Strip&&id!="bpm");if(id=="bpm")b->slider.setSliderStyle(juce::Slider::LinearHorizontal);b->slider.setComponentID(id);canvas.addAndMakeVisible(b->slider);canvas.addAndMakeVisible(b->label);controls.push_back(std::move(b));}
            else{auto c=std::make_unique<Choice>();c->id=id;c->box.setName(d.name);c->box.setComponentID(id);for(size_t i=0;i<d.choices.size();++i)c->box.addItem(d.choices[i],static_cast<int>(i+1));c->attachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,id,c->box);c->label.setText(d.name,juce::dontSendNotification);c->label.setFont(font(12,true));c->label.setColour(juce::Label::textColourId,ink);canvas.addAndMakeVisible(c->box);canvas.addAndMakeVisible(c->label);choices.push_back(std::move(c));}
        }
        for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.setName("PRESET");preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);preset.onChange=[this]{p.selectPreset(preset.getSelectedId()-1);};previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+p.getNumPrograms()-1)%p.getNumPrograms());};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%p.getNumPrograms());};canvas.addAndMakeVisible(preset);canvas.addAndMakeVisible(previous);canvas.addAndMakeVisible(next);canvas.addAndMakeVisible(info);info.setFont(font(12,true));info.setColour(juce::Label::textColourId,ink);startTimerHz(24);
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    Binding*control(const char*id){for(auto&c:controls)if(c->slider.getComponentID()==id)return c.get();return nullptr;}
    void place(const char*id,int x,int y,int w,int h){if(auto*c=control(id)){c->label.setBounds(x,y,w,20);c->slider.setBounds(x,y+21,w,h-21);}}
    void choice(const char*id,int x,int y,int w){for(auto&c:choices)if(c->id==id){c->label.setBounds(x,y,w,17);c->box.setBounds(x,y+18,w,30);}}
    void resized(){const float scale=owner.getWidth()/static_cast<float>(width);canvas.setBounds(0,0,width,height);canvas.setTransform(juce::AffineTransform::scale(scale));bypass.setBounds(width-106,18,84,29);
        if(p.kind==FinishKind::Silk||p.kind==FinishKind::Spark){display.setBounds(174,67,width-196,180);place("depth",29,101,126,134);place("sensitivity",31,236,122,73);delta.setBounds(36,70,110,28);choice("mode",184,254,132);const char*ids[]{"low","high","attack","release","mix","output"};for(int i=0;i<6;++i)place(ids[i],22+i*107,310,101,92);info.setBounds(327,269,333,24);}
        else if(p.kind==FinishKind::Strip){display.setBounds(22,65,width-44,84);const char*ids[]{"input","bass","treble","compress","deess","space","echo","width","output"};for(int i=0;i<9;++i)place(ids[i],22+i*87,210,80,157);choice("style",25,153,135);place("bpm",176,151,172,52);info.setBounds(366,171,426,26);}
        else if(p.kind==FinishKind::Gold){display.setBounds(22,65,width-44,132);const char*ids[]{"lowboost","lowcut","highboost","highcut","bandwidth","mix","output"};for(int i=0;i<7;++i)place(ids[i],21+i*103,209,99,114);choice("lowfreq",29,326,154);choice("highfreq",226,326,154);choice("cutfreq",420,326,154);info.setBounds(586,338,153,36);}
        else{display.setBounds(174,65,width-196,179);place("depth",24,85,137,154);const char*ids[]{"resonance","motion","rate","envelope","mix","output"};for(int i=0;i<6;++i)place(ids[i],20+i*94,304,89,108);sync.setBounds(24,261,84,29);choice("division",117,243,143);place("bpm",270,243,157,52);info.setBounds(436,258,143,36);}
        previous.setBounds(22,height-39,30,28);preset.setBounds(61,height-39,width-122,28);next.setBounds(width-52,height-39,30,28);
    }
    void timerCallback()override{preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);const auto label=p.getProgramName(p.getCurrentProgram())+(p.presetMatches()?"":" *");if(preset.getText()!=label)preset.setText(label,juce::dontSendNotification);info.setText((p.hostTempo.load()?"HOST ":"TEMPO ")+juce::String(p.tempo.load(),1)+" BPM   |   "+juce::String(1000.*p.getLatencySamples()/p.uiRate.load(),1)+" MS",juce::dontSendNotification);if(p.kind==FinishKind::Silk||p.kind==FinishKind::Spark||p.kind==FinishKind::Gold)info.setText("IN "+juce::String(juce::Decibels::gainToDecibels(p.inputPeak.load(),-90.f),1)+"  /  OUT "+juce::String(juce::Decibels::gainToDecibels(p.outputPeak.load(),-90.f),1)+" DBFS",juce::dontSendNotification);if(auto*c=control("bpm"))c->slider.setEnabled(!p.hostTempo.load());if(auto*c=control("rate"))c->slider.setEnabled(p.value("sync")<.5f);display.repaint();}
    void paint(juce::Graphics&g){const float s=owner.getWidth()/static_cast<float>(width);g.addTransform(juce::AffineTransform::scale(s));g.fillAll(juce::Colour(0xffc9ac85));if(wood.isValid()){g.drawImage(wood,0,0,width,height,950,285,170,805);g.setColour(cream.withAlpha(.12f));g.fillRect(0,0,width,height);g.setOpacity(1);g.drawImage(wood,21,15,46,33,224,146,174,122);}g.setColour(juce::Colour(0xff65543c).withAlpha(.6f));g.drawRoundedRectangle(3,3,width-6.f,height-6.f,16,5);g.setColour(cream.withAlpha(.7f));g.drawRoundedRectangle(8,8,width-16.f,height-16.f,13,1);g.setColour(cream.withAlpha(.93f));if(p.kind==FinishKind::Dive)g.fillRoundedRectangle(16,59,width-32.f,height-111.f,24);else g.fillRoundedRectangle(16,59,width-32.f,height-111.f,13);g.setColour(ink);g.setFont(font(22));g.drawText(p.getName(),94,17,width-218,33,juce::Justification::centredLeft);g.setColour(ink.withAlpha(.4f));g.drawLine(80,17,80,49,1);g.drawLine(32,height-48.f,width-32.f,height-48.f,1);}
};
GillFinishEditor::GillFinishEditor(GillFinishProcessor&p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);const int w=impl->width,h=impl->height;setResizable(true,true);setResizeLimits(w,h,w*2,h*2);if(auto*c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(w)/h);setSize(w,h);}
GillFinishEditor::~GillFinishEditor()=default;void GillFinishEditor::paint(juce::Graphics&g){impl->paint(g);}void GillFinishEditor::resized(){if(impl)impl->resized();}
