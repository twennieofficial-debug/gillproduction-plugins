#include "PluginEditor.h"
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
        auto r=getLocalBounds().toFloat();g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff142e47),r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,14);g.setColour(cream.withAlpha(.3f));g.drawRoundedRectangle(r.reduced(1),13,1);
        g.setFont(font(12,true));g.setColour(cream.withAlpha(.8f));g.drawText(p.kind==FinishKind::Silk?"DYNAMIC RESONANCE CONTROL":p.kind==FinishKind::Spark?"TRANSIENT CONTROL":p.kind==FinishKind::Gold?"TONE CURVE":p.kind==FinishKind::Dive?"FILTER / MOTION":"VOCAL CHANNEL",16,10,getWidth()-32,20,juce::Justification::left);
        if(!p.rateSupported.load()){g.setFont(font(18,true));g.drawText("UNSUPPORTED RATE / BYPASS",getLocalBounds(),juce::Justification::centred);return;}
        const float level=juce::Decibels::gainToDecibels(p.outputPeak.load(),-90.f);
        if(p.kind==FinishKind::Strip){g.setFont(font(20,true));const int third=(getWidth()-32)/3;g.drawText("IN "+juce::String(juce::Decibels::gainToDecibels(p.inputPeak.load(),-90.f),1)+" DBFS",16,37,third,30,juce::Justification::left);g.drawText("GR "+juce::String(p.reduction.load(),1)+" DB",16+third,37,third,30,juce::Justification::centred);g.drawText("OUT "+juce::String(level,1)+" DBFS",16+2*third,37,third,30,juce::Justification::right);return;}
        const auto a=r.reduced(37,38).withTrimmedBottom(9);const auto mx=std::min(20000.,p.uiRate.load()*.48);auto xFor=[&](double hz){return a.getX()+static_cast<float>(std::log(hz/40.)/std::log(mx/40.))*a.getWidth();};
        const bool spectral=p.kind==FinishKind::Silk||p.kind==FinishKind::Spark;auto yFor=[&](float d){return a.getY()+a.getHeight()*(spectral?(1.f/3.f):.5f)-d*a.getHeight()/(spectral?36:30);};
        for(int hz:{100,500,1000,5000,10000})if(hz<mx){const auto x=xFor(hz);g.setColour(cream.withAlpha(.07f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());g.setColour(cream.withAlpha(.6f));g.setFont(font(10));g.drawText(hz>=1000?juce::String(hz/1000)+"K":juce::String(hz),juce::roundToInt(x)-20,getHeight()-22,40,14,juce::Justification::centred);}
        for(int d:{-12,0,12}){g.setColour(cream.withAlpha(d==0?.25f:.08f));g.drawHorizontalLine(juce::roundToInt(yFor(static_cast<float>(d))),a.getX(),a.getRight());g.setColour(cream.withAlpha(.6f));g.drawText(juce::String(d),2,juce::roundToInt(yFor(static_cast<float>(d)))-7,30,15,juce::Justification::right);}
        juce::Path path;
        if(spectral){auto view=p.graph();for(size_t i=0;i<view.size();++i){const double hz=40*std::pow(500.,i/127.);if(hz>mx)break;const float x=xFor(hz),y=std::clamp(yFor(-view[i]),a.getY(),a.getBottom());if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}for(const char*id:{"low","high"}){const float x=std::clamp(xFor(p.value(id)),a.getX(),a.getRight());g.setColour(cream.withAlpha(.4f));g.drawVerticalLine(juce::roundToInt(x),a.getY(),a.getBottom());g.setColour(cream);g.fillEllipse(x-5,a.getY()-4,10,10);}g.setFont(font(13,true));g.setColour(cream);const float gr=p.reduction.load();g.drawText((gr<0?"BOOST ":"REDUCTION ")+juce::String(std::abs(gr),1)+" DB",getWidth()-211,8,196,20,juce::Justification::right);}
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
    gill::QualitySelector quality{p.apvts, p};
    Impl(GillFinishEditor&o,GillFinishProcessor&v):owner(o),p(v),display(v),tooltip(&o,650){owner.setLookAndFeel(&look);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);owner.addAndMakeVisible(canvas);canvas.addAndMakeVisible(quality);canvas.addAndMakeVisible(display);
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
    void resized(){const float scale=owner.getWidth()/static_cast<float>(width);canvas.setBounds(0,0,width,height);canvas.setTransform(juce::AffineTransform::scale(scale));bypass.setBounds(width-106,18,84,29);quality.setBounds(width-230,20,110,26);
        if(p.kind==FinishKind::Silk||p.kind==FinishKind::Spark){display.setBounds(174,67,width-196,180);place("depth",29,101,126,134);place("sensitivity",31,236,122,73);delta.setBounds(36,70,110,28);choice("mode",184,254,132);const char*ids[]{"low","high","attack","release","mix","output"};for(int i=0;i<6;++i)place(ids[i],22+i*107,310,101,92);info.setBounds(327,269,333,24);}
        else if(p.kind==FinishKind::Strip){display.setBounds(22,65,width-44,84);const char*ids[]{"input","bass","treble","compress","deess","space","echo","width","output"};for(int i=0;i<9;++i)place(ids[i],22+i*87,210,80,157);choice("style",25,153,135);for(auto& c:choices)if(c->id=="style")c->label.setBounds(54,153,106,17);place("bpm",176,151,172,52);info.setBounds(366,171,426,26);}
        else if(p.kind==FinishKind::Gold){display.setBounds(22,65,width-44,132);const char*ids[]{"lowboost","lowcut","highboost","highcut","bandwidth","mix","output"};for(int i=0;i<7;++i)place(ids[i],21+i*103,209,99,114);choice("lowfreq",29,326,154);choice("highfreq",226,326,154);choice("cutfreq",420,326,154);info.setBounds(586,338,153,36);}
        else{display.setBounds(174,65,width-196,179);place("depth",24,85,137,154);const char*ids[]{"resonance","motion","rate","envelope","mix","output"};for(int i=0;i<6;++i)place(ids[i],20+i*94,304,89,108);sync.setBounds(24,261,84,29);choice("division",117,243,143);place("bpm",270,243,157,52);info.setBounds(436,258,143,36);}
        previous.setBounds(22,height-39,30,28);preset.setBounds(61,height-39,width-122,28);next.setBounds(width-52,height-39,30,28);
    }
    void timerCallback()override{preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);const auto label=p.getProgramName(p.getCurrentProgram())+(p.presetMatches()?"":" *");if(preset.getText()!=label)preset.setText(label,juce::dontSendNotification);info.setText((p.hostTempo.load()?"HOST ":"TEMPO ")+juce::String(p.tempo.load(),1)+" BPM   |   "+juce::String(1000.*p.getLatencySamples()/p.uiRate.load(),1)+" MS",juce::dontSendNotification);if(p.kind==FinishKind::Silk||p.kind==FinishKind::Spark||p.kind==FinishKind::Gold)info.setText("IN "+juce::String(juce::Decibels::gainToDecibels(p.inputPeak.load(),-90.f),1)+"  /  OUT "+juce::String(juce::Decibels::gainToDecibels(p.outputPeak.load(),-90.f),1)+" DBFS",juce::dontSendNotification);if(auto*c=control("bpm"))c->slider.setEnabled(!p.hostTempo.load());if(auto*c=control("rate"))c->slider.setEnabled(p.value("sync")<.5f);display.repaint();}
    void paint(juce::Graphics&g){
g.addTransform(juce::AffineTransform::scale(owner.getWidth()/static_cast<float>(width)));
 const float top=p.kind==FinishKind::Strip?145.f:p.kind==FinishKind::Gold?197.f:251.f;
 gill::prism::chassis(g,float(width),float(height),top);gill::prism::title(g,p.getName(),{75,16,width-310.f,32},22);
 if(p.kind==FinishKind::Silk||p.kind==FinishKind::Spark)gill::material::panel(g,{20,68,145,241},cream,13);
 else if(p.kind==FinishKind::Dive){gill::material::panel(g,{20,68,145,176},cream,13);gill::material::panel(g,{110,242,321,56},cream,8);}
    }
};
GillFinishEditor::GillFinishEditor(GillFinishProcessor&p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);const int w=impl->width,h=impl->height;setResizable(true,true);setResizeLimits(w,h,w*2,h*2);if(auto*c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(w)/h);setSize(w,h);}
GillFinishEditor::~GillFinishEditor()=default;void GillFinishEditor::paint(juce::Graphics&g){impl->paint(g);}void GillFinishEditor::resized(){if(impl)impl->resized();}
