#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include <cmath>
#include <limits>
namespace {
const juce::Colour ink(0xff293d31),sage(0xff65836b),cream(0xfff7f3e9),dark(0xff263e32);
juce::Font font(float h,bool bold=false){return juce::Font(juce::FontOptions(gillInterfaceFontName(),std::max(12.f,h),bold?juce::Font::bold:juce::Font::plain));}
class GestureSlider final:public juce::Slider{
public:bool commandWheel=false;
    bool hitTest(int x,int y)override{if(!commandWheel)return Slider::hitTest(x,y);auto a=getLookAndFeel().getSliderLayout(*this).sliderBounds.toFloat().reduced(7);const auto radius=std::min(a.getWidth(),a.getHeight())*.5f;const float d=juce::Point<float>(static_cast<float>(x),static_cast<float>(y)).getDistanceFrom(a.getCentre());return d>=radius*.77f||y>a.getBottom();}
    bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey||k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){juce::Slider::ScopedDragNotification g(*this);return Slider::keyPressed(k);}return Slider::keyPressed(k);}
};
class OakLook final:public juce::LookAndFeel_V4{
public:OakLook(){setColour(juce::Slider::textBoxTextColourId,ink);setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.72f));setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);setColour(juce::TextButton::buttonColourId,cream.withAlpha(.8f));setColour(juce::TextButton::buttonOnColourId,sage);setColour(juce::TextButton::textColourOffId,ink);setColour(juce::TextButton::textColourOnId,cream);setColour(juce::ComboBox::backgroundColourId,cream);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::outlineColourId,sage.withAlpha(.6f));setColour(juce::ComboBox::arrowColourId,ink);setColour(juce::PopupMenu::backgroundColourId,cream);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,sage);setColour(juce::TooltipWindow::backgroundColourId,cream);setColour(juce::TooltipWindow::textColourId,ink);}
    juce::Font getTextButtonFont(juce::TextButton&,int height)override{return font(std::clamp(height*.42f,12.f,14.f),true);}
    juce::Font getComboBoxFont(juce::ComboBox&)override{return font(13,true);}
    juce::Font getLabelFont(juce::Label& l)override{return l.getFont();}
    juce::Label* createSliderTextBox(juce::Slider& slider)override{auto* label=juce::LookAndFeel_V4::createSliderTextBox(slider);label->setFont(font(13,true));label->setColour(juce::Label::textColourId,ink);label->setColour(juce::Label::backgroundColourId,cream.withAlpha(.9f));label->setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);label->setColour(juce::TextEditor::textColourId,ink);label->setColour(juce::TextEditor::backgroundColourId,cream);return label;}
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);g.setColour(juce::Colours::black.withAlpha(.13f));g.fillRoundedRectangle(r.translated(0,2),6);
        if(b.getName()=="LEARN"){const auto grain=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);juce::Graphics::ScopedSaveState saved(g);juce::Path clip;clip.addRoundedRectangle(r,6);g.reduceClipRegion(clip);g.drawImage(grain,r.getX(),r.getY(),r.getWidth(),r.getHeight(),950,380,170,95);g.setColour(cream.withAlpha(over?.32f:.16f));g.fillRect(r);g.setColour(juce::Colour(0xff765934));g.drawRoundedRectangle(r.reduced(1),5,2);g.setColour(cream.withAlpha(.65f));g.drawRoundedRectangle(r.reduced(3),4,1);return;}
        g.setColour(b.getToggleState()?sage:cream.withAlpha(over?.98f:.88f));g.fillRoundedRectangle(r,6);g.setColour(down?ink:sage.withAlpha(.35f));g.drawRoundedRectangle(r,6,1);}
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,const juce::Slider::SliderStyle style,juce::Slider&)override{
        const bool vertical=style==juce::Slider::LinearVertical;const float cx=x+w*.5f,cy=y+h*.5f;
        auto track=vertical?juce::Rectangle<float>(cx-5,static_cast<float>(y),10,static_cast<float>(h)):juce::Rectangle<float>(static_cast<float>(x),cy-4,static_cast<float>(w),8);
        g.setColour(juce::Colour(0xff756956));g.fillRoundedRectangle(track.expanded(3),7);g.setColour(dark);g.fillRoundedRectangle(track,4);
        g.setColour(sage.brighter(.25f));if(vertical)g.fillRoundedRectangle(cx-2,pos,4,std::max(0.f,y+h-pos),2);else g.fillRoundedRectangle(static_cast<float>(x),cy-2,std::max(0.f,pos-x),4,2);
        auto thumb=vertical?juce::Rectangle<float>(cx-29,pos-14,58,28):juce::Rectangle<float>(pos-10,cy-18,20,36);
        g.setColour(juce::Colours::black.withAlpha(.23f));g.fillRoundedRectangle(thumb.translated(2,4),5);g.setGradientFill(juce::ColourGradient(juce::Colours::white,thumb.getX(),thumb.getY(),juce::Colour(0xffd1d0c5),thumb.getRight(),thumb.getBottom(),false));g.fillRoundedRectangle(thumb,4);g.setColour(cream);g.drawRoundedRectangle(thumb.reduced(.5f),4,1);g.setColour(sage);if(vertical)g.fillRect(thumb.reduced(9,13));else g.fillRect(thumb.reduced(9,8));
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float end,juce::Slider& slider)override{
        const auto r=juce::Rectangle<float>(static_cast<float>(x),static_cast<float>(y),static_cast<float>(w),static_cast<float>(h)).reduced(7);const float size=std::min(r.getWidth(),r.getHeight()),radius=size*.5f,cx=r.getCentreX(),cy=r.getCentreY();auto body=juce::Rectangle<float>(size-15,size-15).withCentre({cx,cy});
        if(auto* ring=dynamic_cast<GestureSlider*>(&slider);ring&&ring->commandWheel){
            const auto shell=juce::Rectangle<float>(size,size).withCentre({cx,cy});
            g.setColour(juce::Colours::black.withAlpha(.2f));g.fillEllipse(shell.translated(2,5));
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffffff8),cx-radius,cy-radius,juce::Colour(0xffd0cbbd),cx+radius,cy+radius,false));g.fillEllipse(shell);
            g.setColour(juce::Colour(0xff8a6d47));g.drawEllipse(shell,2);g.setColour(cream);g.drawEllipse(shell.reduced(3),2);
            const auto inner=shell.withSizeKeepingCentre(size*.77f,size*.77f);g.setColour(juce::Colour(0xff9c7951));g.fillEllipse(inner.expanded(3));g.setColour(cream);g.drawEllipse(inner.expanded(3),1);
            for(int i=0;i<=20;++i){const float angle=start+i*(end-start)/20.f;g.setColour(ink.withAlpha(i%5==0?.7f:.24f));g.drawLine(cx+std::sin(angle)*radius*.83f,cy-std::cos(angle)*radius*.83f,cx+std::sin(angle)*radius*.89f,cy-std::cos(angle)*radius*.89f,i%5==0?2.f:1.f);}
            for(int i=0;i<=4;++i){const float angle=start+i*(end-start)/4.f;g.setColour(ink);g.setFont(font(std::max(12.f,size*.033f),true));g.drawText(juce::String(i*50),juce::Rectangle<float>(size*.11f,size*.05f).withCentre({cx+std::sin(angle)*radius*.95f,cy-std::cos(angle)*radius*.95f}),juce::Justification::centred,false);}
            const float angle=start+value*(end-start);const auto handle=juce::Rectangle<float>(size*.043f,size*.043f).withCentre({cx+std::sin(angle)*radius*.865f,cy-std::cos(angle)*radius*.865f});g.setColour(juce::Colours::black.withAlpha(.16f));g.fillEllipse(handle.translated(1,2));g.setColour(sage);g.fillEllipse(handle);g.setColour(cream);g.drawEllipse(handle,1.4f);return;
        }
        juce::Path a,b;a.addCentredArc(cx,cy,radius,radius,0,start,end,true);b.addCentredArc(cx,cy,radius,radius,0,start,start+value*(end-start),true);g.setColour(ink.withAlpha(.2f));g.strokePath(a,juce::PathStrokeType(4));g.setColour(sage);g.strokePath(b,juce::PathStrokeType(5));g.setColour(juce::Colours::black.withAlpha(.2f));g.fillEllipse(body.translated(2,4));g.setGradientFill(juce::ColourGradient(juce::Colours::white,body.getX(),body.getY(),juce::Colour(0xffcac8bb),body.getRight(),body.getBottom(),false));g.fillEllipse(body);g.setColour(cream);g.drawEllipse(body.reduced(1),1.2f);
        const float angle=start+value*(end-start);g.setColour(sage);g.drawLine(cx+std::sin(angle)*radius*.36f,cy-std::cos(angle)*radius*.36f,cx+std::sin(angle)*radius*.64f,cy-std::cos(angle)*radius*.64f,4);
    }
};
struct Binding {
    GestureSlider slider;juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    Binding(GillVocalProcessor& p,const juce::String& id,const juce::String& caption,const juce::String& suffix,bool vertical=false){
        slider.setName(caption);slider.setSliderStyle(vertical?juce::Slider::LinearVertical:juce::Slider::RotaryHorizontalVerticalDrag);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,84,24);slider.setTextValueSuffix(suffix);auto* parameter=p.apvts.getParameter(id);slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));slider.setWantsKeyboardFocus(true);
        slider.setTooltip(caption+": ZIEHEN ODER ZAHL EINGEBEN | DOPPELKLICK: STANDARDWERT");attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,slider);
        slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,cream.withAlpha(.94f));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
        label.setText(caption,juce::dontSendNotification);label.setJustificationType(juce::Justification::centred);label.setColour(juce::Label::textColourId,ink);label.setFont(font(12,true));
    }
};
juce::String note(float hz){if(hz<1||!std::isfinite(hz))return "--";const int midi=juce::roundToInt(69+12*std::log2(hz/440.f));const char* n[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};return juce::String(n[(midi%12+12)%12])+juce::String(midi/12-1);}
class Display final:public juce::Component {
public:explicit Display(GillVocalProcessor& v):p(v){pitch.fill(std::numeric_limits<float>::quiet_NaN());}
    void tick(){std::move(pre.begin()+1,pre.end(),pre.begin());std::move(post.begin()+1,post.end(),post.begin());pre.back()=p.inputPeak.load();post.back()=p.outputPeak.load();
        std::move(pitch.begin()+1,pitch.end(),pitch.begin());const float a=p.pitchHz.load(),b=p.targetHz.load();pitch.back()=a>0&&b>0&&p.pitchConfidence.load()>.5f?juce::jlimit(-100.f,100.f,1200*std::log2(a/b)):std::numeric_limits<float>::quiet_NaN();repaint();}
    void paint(juce::Graphics& g)override{
        if(p.kind==GillKind::Tune){paintWheel(g);return;}
        auto r=getLocalBounds().toFloat();g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff192c23),0,r.getHeight(),false));g.fillRoundedRectangle(r,12);g.setColour(cream.withAlpha(.22f));g.drawRoundedRectangle(r.reduced(1),11,1);
        if(!p.rateSupported.load()){g.setColour(cream);g.setFont(font(16,true));g.drawText("SAMPLE RATE UNSUPPORTED / BYPASS",getLocalBounds(),juce::Justification::centred);return;}
        g.setColour(cream.withAlpha(.7f));g.setFont(font(12,true));g.drawText(p.kind==GillKind::Flow?"VOCAL LEVEL  /  PRE + POST":p.kind==GillKind::Heat?"INPUT / OUTPUT":"PITCH / TARGET",16,10,getWidth()-32,16,juce::Justification::left);
        if(p.kind==GillKind::Tune){const float input=p.pitchHz.load(),target=p.targetHz.load();const bool voiced=p.pitchConfidence.load()>.5f&&input>0&&target>0;g.setFont(font(42,true));g.setColour(cream);g.drawText(voiced?note(input):"--",20,33,170,60,juce::Justification::centred);g.setColour(sage.brighter(.6f));g.drawText(voiced?note(target):"--",getWidth()-190,33,170,60,juce::Justification::centred);
            g.setFont(font(22));g.drawText(">",getWidth()/2-15,43,30,40,juce::Justification::centred);g.setFont(font(11));g.setColour(cream.withAlpha(.7f));g.drawText(voiced?juce::String(input,1)+" HZ":"WAITING FOR VOICE",20,93,170,20,juce::Justification::centred);g.drawText(voiced?juce::String(target,1)+" HZ":"MONO VOCAL",getWidth()-190,93,170,20,juce::Justification::centred);
            const auto bar=juce::Rectangle<float>(30,r.getBottom()-28,r.getWidth()-60,5);g.setColour(cream.withAlpha(.18f));g.fillRoundedRectangle(bar,2);g.setColour(sage.brighter(.8f));const float cents=voiced?juce::jlimit(-100.f,100.f,1200*std::log2(target/input)):0;const float x=bar.getCentreX()+cents*.005f*bar.getWidth();g.fillEllipse(x-4,bar.getY()-2,9,9);g.setFont(font(9));g.setColour(cream.withAlpha(.65f));g.drawText("-100 CT",27,getHeight()-20,60,13,juce::Justification::left);g.drawText("0",getWidth()/2-15,getHeight()-20,30,13,juce::Justification::centred);g.drawText("+100 CT",getWidth()-88,getHeight()-20,60,13,juce::Justification::right);
        }else{auto area=r.reduced(16).withTrimmedTop(22).withTrimmedBottom(20);for(int db:{-12,-24,-48}){const float y=area.getBottom()-(db+60)/60.f*area.getHeight();g.setColour(cream.withAlpha(.08f));g.drawHorizontalLine(static_cast<int>(y),area.getX(),area.getRight());}
            auto line=[&](const auto& data,juce::Colour colour){juce::Path path;for(size_t i=0;i<data.size();++i){const float x=area.getX()+i*area.getWidth()/(data.size()-1);const float db=juce::Decibels::gainToDecibels(std::max(1e-6f,data[i]),-60.f);const float y=area.getBottom()-juce::jlimit(0.f,1.f,(db+60)/60)*area.getHeight();if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}g.setColour(colour);g.strokePath(path,juce::PathStrokeType(1.6f));};line(pre,cream.withAlpha(.4f));line(post,sage.brighter(.7f));
            g.setFont(font(12,true));g.setColour(cream.withAlpha(.75f));g.drawText("IN "+juce::String(juce::Decibels::gainToDecibels(p.inputPeak.load(),-90.f),1)+" DBFS",16,getHeight()-24,160,18,juce::Justification::left);g.drawText("OUT "+juce::String(juce::Decibels::gainToDecibels(p.outputPeak.load(),-90.f),1)+" DBFS",getWidth()-176,getHeight()-24,160,18,juce::Justification::right);
        }
    }
private:
    void paintWheel(juce::Graphics& g){
        const float w=static_cast<float>(getWidth()),h=static_cast<float>(getHeight()),s=w/330.f;
        juce::Path outline;outline.addEllipse(getLocalBounds().toFloat());g.reduceClipRegion(outline);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff335d50),w*.4f,0,juce::Colour(0xff10291f),w*.6f,h,false));g.fillAll();
        g.setColour(cream.withAlpha(.3f));g.drawEllipse(getLocalBounds().toFloat().reduced(1),2*s);
        const float input=p.pitchHz.load(),target=p.targetHz.load();const bool voiced=p.rateSupported.load()&&p.pitchConfidence.load()>.5f&&input>0&&target>0;
        auto text=[&](const juce::String& t,float y,float height,float fontSize,juce::Colour colour,bool bold=false){g.setColour(colour);g.setFont(font(std::max(12.f,fontSize*s),bold));g.drawText(t,juce::Rectangle<float>(0,y*s,w,height*s),juce::Justification::centred,false);};
        text("INPUT",23,18,11,cream.withAlpha(.7f),true);text(voiced?note(input):"--",40,43,38,cream,true);text(voiced?juce::String(input,1)+" HZ":"PLAY MONO VOCAL",86,18,11,cream.withAlpha(.8f));
        const auto area=juce::Rectangle<float>(26*s,125*s,w-52*s,62*s);g.setColour(cream.withAlpha(.13f));g.drawHorizontalLine(static_cast<int>(area.getCentreY()),area.getX(),area.getRight());
        juce::Path trace;bool pen=false;for(size_t i=0;i<pitch.size();++i){if(!std::isfinite(pitch[i])){pen=false;continue;}const float x=area.getX()+i*area.getWidth()/(pitch.size()-1),y=area.getCentreY()-pitch[i]*area.getHeight()/200;if(pen)trace.lineTo(x,y);else{trace.startNewSubPath(x,y);pen=true;}}
        g.setColour(juce::Colour(0xffa5e8cf));g.strokePath(trace,juce::PathStrokeType(2*s));
        const auto cents=voiced?1200*std::log2(input/target):0;
        text(voiced?(cents>=0?"+":"")+juce::String(juce::roundToInt(cents))+" CENTS":"-- CENTS",196,21,13,cream,true);
        text("TARGET",235,16,11,cream.withAlpha(.7f),true);text(voiced?note(target):"--",250,40,33,juce::Colour(0xffbcebd5),true);text(voiced?juce::String(target,1)+" HZ":"WAITING",290,17,10,cream.withAlpha(.8f));
        if(!p.rateSupported.load())text("RATE UNSUPPORTED",145,28,13,cream,true);
    }
    GillVocalProcessor& p;std::array<float,100> pre{},post{},pitch{};
};
}
struct GillVocalEditor::Impl:private juce::Timer {
    GillVocalEditor& owner;GillVocalProcessor& p;OakLook look;juce::Image wood;Display display;juce::TooltipWindow tooltip;
    std::vector<std::unique_ptr<Binding>> controls;std::array<juce::TextButton,3> modes;
    std::array<juce::TextButton,5> tunePresets;
    juce::TextButton learn{"LEARN"},autogain{"AUTO GAIN"},bypass{"BYPASS"};juce::ComboBox preset,key,scale;
    juce::Label status,keyLabel,scaleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment,bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment,scaleAttachment;
    int baseWidth=700,baseHeight=470;
    Impl(GillVocalEditor& o,GillVocalProcessor& v):owner(o),p(v),display(v),tooltip(&o,600){
        owner.setLookAndFeel(&look);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);owner.addAndMakeVisible(display);
        auto add=[&](const char* id,const char* title,const char* suffix,bool vertical=false){auto b=std::make_unique<Binding>(p,id,title,suffix,vertical);owner.addAndMakeVisible(b->slider);owner.addAndMakeVisible(b->label);controls.push_back(std::move(b));};
        bypass.setName("BYPASS");bypass.setClickingTogglesState(true);owner.addAndMakeVisible(bypass);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);bypass.setTooltip("VERARBEITUNG UMGEHEN; GEMELDETE LATENZ BLEIBT GLEICH");
        if(p.kind==GillKind::Flow){baseWidth=340;baseHeight=480;add("amount","AMOUNT"," %",true);owner.addAndMakeVisible(learn);owner.addAndMakeVisible(autogain);owner.addAndMakeVisible(status);
            autogain.setClickingTogglesState(true);autoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"autogain",autogain);autogain.setTooltip("LAUTSTAERKE LANGSAM AUSGLEICHEN; MAXIMAL +9 DB");
            learn.setName("LEARN");learn.setTooltip("VOCAL ABSPIELEN: 10 SEKUNDEN AKTIVE STIMME ANALYSIEREN. STILLE ZAEHLT NICHT.");learn.onClick=[this]{p.requestLearning(p.learnState.load()!=1);};status.setJustificationType(juce::Justification::centred);status.setColour(juce::Label::textColourId,ink);status.setFont(font(12,true));
        }else if(p.kind==GillKind::Heat){baseWidth=580;baseHeight=380;add("low","LOW"," %",true);add("mid","MID"," %",true);add("high","HIGH"," %",true);add("mix","MIX"," %");add("output","OUTPUT"," DB");for(int i=0;i<3;++i){controls[i]->slider.textFromValueFunction=[](double v){return juce::String(v/24.*100.,1);};controls[i]->slider.valueFromTextFunction=[](const juce::String& t){return t.getDoubleValue()*.24;};controls[i]->slider.updateText();controls[i]->slider.setTooltip("SATURATION: STUFENLOS VON CLEAN BIS EXTREME. DIE FAERBUNG NIMMT UEBER DEN GANZEN REGELWEG ZU; ABHAENGIG VOM EINGANGSSIGNAL.");}}
        else{baseWidth=600;baseHeight=560;add("retune","RETUNE"," MS");add("humanize","HUMANIZE"," %");add("mix","MIX"," %");
            controls[0]->slider.commandWheel=true;controls[0]->slider.setSliderStyle(juce::Slider::Rotary);controls[0]->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);controls[0]->slider.setMouseCursor(juce::MouseCursor::PointingHandCursor);controls[0]->slider.setTooltip("RETUNE 0 BIS 200 MS: DEN GRUENEN PUNKT AUF DEM RING IM KREIS ZIEHEN. KLEINER = SCHNELLERE KORREKTUR. ZAHL DIREKT EINGEBEN.");
            for(int i=0;i<5;++i){tunePresets[i].setButtonText(p.getProgramName(i));tunePresets[i].setName("PRESET "+p.getProgramName(i));tunePresets[i].onClick=[this,i]{p.setCurrentProgram(i);};owner.addAndMakeVisible(tunePresets[i]);tunePresets[i].setTooltip("KLANGPRESET LADEN; TONART UND SKALA BLEIBEN ERHALTEN");}
            auto* kp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("key"));auto* sp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("scale"));key.addItemList(kp->choices,1);scale.addItemList(sp->choices,1);key.setName("KEY");scale.setName("SCALE");owner.addAndMakeVisible(key);owner.addAndMakeVisible(scale);keyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"key",key);scaleAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"scale",scale);
            key.setTooltip("GRUNDTON DES SONGS; BEI CHROMATIC WERDEN ALLE HALBTOENE VERWENDET");scale.setTooltip("PASSENDE SONG-SKALA WAEHLEN; EINZELNE VOCALSPUR VERWENDEN");
            keyLabel.setText("KEY",juce::dontSendNotification);scaleLabel.setText("SCALE",juce::dontSendNotification);for(auto* label:{&keyLabel,&scaleLabel}){label->setColour(juce::Label::textColourId,ink);label->setFont(font(12,true));owner.addAndMakeVisible(*label);}
        }
        if(p.kind!=GillKind::Tune){const char* flowNames[]{"NATURAL","FOCUS","CRUSH"};const char* heatNames[]{"WARM","TAPE","EDGE"};for(int i=0;i<3;++i){modes[i].setButtonText(p.kind==GillKind::Flow?flowNames[i]:heatNames[i]);modes[i].setName(modes[i].getButtonText());modes[i].onClick=[this,i]{p.setValue(p.kind==GillKind::Flow?"mode":"style",static_cast<float>(i));};owner.addAndMakeVisible(modes[i]);}}
        startTimerHz(25);timerCallback();
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    void timerCallback()override{display.tick();if(p.kind!=GillKind::Tune){const int selected=juce::roundToInt(p.value(p.kind==GillKind::Flow?"mode":"style"));for(int i=0;i<3;++i)modes[i].setToggleState(i==selected,juce::dontSendNotification);}
        if(p.kind==GillKind::Flow){const int state=p.learnState.load();learn.setButtonText(state==1?"CANCEL":"LEARN");status.setText(state==1?"LISTENING  "+juce::String(p.learnProgress.load()*100,0)+" %":state==2?"PROFILE READY":state==3?"MORE VOCAL NEEDED":"PLAY VOCAL + LEARN",juce::dontSendNotification);}
        else if(p.kind==GillKind::Tune)for(int i=0;i<5;++i)tunePresets[i].setToggleState(p.presetMatches()&&p.getCurrentProgram()==i,juce::dontSendNotification);owner.repaint();
    }
    void bounds(juce::Component& c,float x,float y,float w,float h){const float s=owner.getWidth()/static_cast<float>(baseWidth);c.setBounds(juce::Rectangle<float>(x*s,y*s,w*s,h*s).toNearestInt());}
    void control(int i,float x,float y,float w,float h){bounds(controls[i]->label,x,y,w,21);bounds(controls[i]->slider,x,y+24,w,h-24);}
    void resized(){if(p.kind==GillKind::Flow){bounds(bypass,235,19,84,29);bounds(display,20,65,300,115);control(0,66,197,133,185);for(int i=0;i<3;++i)bounds(modes[i],20+i*101.f,391,97,29);bounds(learn,20,430,115,29);bounds(autogain,145,430,123,29);bounds(status,20,459,300,20);}
        else if(p.kind==GillKind::Heat){bounds(bypass,475,19,84,29);bounds(display,20,65,540,89);for(int i=0;i<3;++i)control(i,22+i*108.f,168,98,140);for(int i=0;i<3;++i)bounds(modes[i],20+i*108.f,335,102,29);control(3,356,197,88,114);control(4,466,197,91,114);}
        else{bounds(bypass,493,19,85,29);bounds(keyLabel,24,126,102,18);bounds(key,24,149,102,30);bounds(scaleLabel,24,288,102,18);bounds(scale,24,311,102,30);
            bounds(controls[0]->label,135,77,330,20);bounds(controls[0]->slider,135,98,330,365);
            const auto& ring=controls[0]->slider;const auto a=look.getSliderLayout(const_cast<GestureSlider&>(ring)).sliderBounds.toFloat().reduced(7);const auto diameter=std::min(a.getWidth(),a.getHeight())*.77f;display.setBounds(juce::Rectangle<float>(diameter,diameter).withCentre(a.getCentre()+ring.getPosition().toFloat()).toNearestInt());display.toFront(false);
            control(1,476,128,102,124);control(2,476,290,102,124);
            for(int i=0;i<5;++i)bounds(tunePresets[i],i<3?24+i*185.f:116+(i-3)*185.f,i<3?470:510,178,30);}
    }
    void paint(juce::Graphics& g){const float s=owner.getWidth()/static_cast<float>(baseWidth),w=static_cast<float>(owner.getWidth()),h=static_cast<float>(owner.getHeight());g.fillAll(juce::Colour(0xffc9ac85));
        if(wood.isValid()){g.drawImage(wood,0,0,static_cast<int>(w),static_cast<int>(h),950,285,170,805);g.setColour(cream.withAlpha(.16f));g.fillAll();g.setOpacity(1.f);g.drawImage(wood,juce::roundToInt(21*s),juce::roundToInt(15*s),juce::roundToInt(46*s),juce::roundToInt(33*s),224,146,174,122);}
        g.setColour(juce::Colour(0xff65543c).withAlpha(.52f));g.drawRoundedRectangle({3*s,3*s,w-6*s,h-6*s},12*s,4*s);g.setColour(cream.withAlpha(.5f));g.drawRoundedRectangle({7*s,7*s,w-14*s,h-14*s},9*s,s);
        g.setColour(ink.withAlpha(.4f));g.drawLine(80*s,17*s,80*s,49*s,s);g.setColour(ink);g.setFont(font(22*s));g.drawText(p.getName(),juce::Rectangle<float>(94*s,17*s,(baseWidth-208)*s,33*s),juce::Justification::centredLeft,false);
        if(p.kind==GillKind::Tune){auto plate=juce::Rectangle<float>(14*s,65*s,w-28*s,h-80*s);g.setColour(juce::Colour(0xff816747));g.fillRoundedRectangle(plate.expanded(2*s),20*s);g.setGradientFill(juce::ColourGradient(cream,0,70*s,juce::Colour(0xffe7e2d5),w,h,false));g.fillRoundedRectangle(plate,18*s);g.setColour(juce::Colours::white.withAlpha(.85f));g.drawRoundedRectangle(plate.reduced(2*s),17*s,s);
            for(int y:{119,281}){auto island=juce::Rectangle<float>(20*s,y*s,110*s,71*s);g.setColour(juce::Colour(0xff9c7e59));g.fillRoundedRectangle(island.expanded(s),9*s);juce::Graphics::ScopedSaveState save(g);juce::Path clip;clip.addRoundedRectangle(island,8*s);g.reduceClipRegion(clip);if(wood.isValid())g.drawImage(wood,island.getX(),island.getY(),island.getWidth(),island.getHeight(),950,285,170,240);g.setColour(cream.withAlpha(.15f));g.fillRect(island);}
            g.setColour(ink.withAlpha(.25f));g.drawLine(25*s,455*s,575*s,455*s,s);
        }
        if(p.kind==GillKind::Flow){const float gr=juce::jlimit(0.f,30.f,p.reduction.load());g.setFont(font(12*s,true));g.drawText("GR",juce::Rectangle<float>(231*s,199*s,58*s,20*s),juce::Justification::centred,false);
            const auto bar=juce::Rectangle<float>(246*s,228*s,18*s,120*s);g.setColour(dark);g.fillRoundedRectangle(bar,4*s);for(int i=0;i<24;++i){g.setColour(i<gr/30*24?sage.brighter(.65f):sage.withAlpha(.2f));g.fillRect(250*s,(231+i*4.7f)*s,10*s,2.8f*s);}g.setFont(font(12*s));g.setColour(ink);for(int db:{0,15,30})g.drawText(juce::String(db),juce::Rectangle<float>(271*s,(222+db*3.8f)*s,30*s,18*s),juce::Justification::left,false);
            g.setFont(font(16*s,true));g.drawText("-"+juce::String(gr,1)+" DB",juce::Rectangle<float>(215*s,356*s,99*s,24*s),juce::Justification::centred,false);
        }else if(p.kind==GillKind::Heat){g.setFont(font(12*s,true));g.setColour(ink.withAlpha(.7f));const char* captions[]{"< 180 HZ","180 HZ - 3K","> 3 KHZ"};for(int i=0;i<3;++i)g.drawText(captions[i],juce::Rectangle<float>((22+i*108)*s,312*s,98*s,17*s),juce::Justification::centred,false);g.drawLine(342*s,175*s,342*s,361*s,s);}
    }

};
GillVocalEditor::GillVocalEditor(GillVocalProcessor& p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);const int w=impl->baseWidth,h=impl->baseHeight;setResizable(true,true);setResizeLimits(w,h,w*2,h*2);if(auto* c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(w)/h);setSize(w,h);}
GillVocalEditor::~GillVocalEditor()=default;
void GillVocalEditor::paint(juce::Graphics& g){impl->paint(g);}
void GillVocalEditor::resized(){if(impl)impl->resized();}

