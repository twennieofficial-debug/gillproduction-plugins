#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"
#include <cmath>
#include <limits>
namespace {
const juce::Colour ink(0xff102237),sage(0xff54b8d5),cream(0xffe2ecf6),dark(0xff091d31);
juce::Font font(float h,bool bold=false){return juce::Font(juce::FontOptions(gillInterfaceFontName(),std::max(12.f,h),bold?juce::Font::bold:juce::Font::plain));}
class GestureSlider final:public juce::Slider{
public:bool commandWheel=false;
    bool hitTest(int x,int y)override{if(!commandWheel)return Slider::hitTest(x,y);auto a=getLookAndFeel().getSliderLayout(*this).sliderBounds.toFloat().reduced(7);const auto radius=std::min(a.getWidth(),a.getHeight())*.5f;const float d=juce::Point<float>(static_cast<float>(x),static_cast<float>(y)).getDistanceFrom(a.getCentre());return d>=radius*.77f||y>a.getBottom();}
    bool keyPressed(const juce::KeyPress& k)override{if(k==juce::KeyPress::upKey||k==juce::KeyPress::downKey||k==juce::KeyPress::leftKey||k==juce::KeyPress::rightKey){juce::Slider::ScopedDragNotification g(*this);return Slider::keyPressed(k);}return Slider::keyPressed(k);}
};
class OakLook final : public gill::prism::Look {  };
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
public:explicit Display(GillVocalProcessor& v):p(v){pitch.fill(std::numeric_limits<float>::quiet_NaN());noteHistory.fill(std::numeric_limits<float>::quiet_NaN());targetHistory.fill(std::numeric_limits<float>::quiet_NaN());}
    void tick(){std::move(pre.begin()+1,pre.end(),pre.begin());std::move(post.begin()+1,post.end(),post.begin());pre.back()=p.inputPeak.load();post.back()=p.outputPeak.load();
        std::move(pitch.begin()+1,pitch.end(),pitch.begin());const float a=p.pitchHz.load(),b=p.targetHz.load();pitch.back()=a>0&&b>0&&p.pitchConfidence.load()>.5f?juce::jlimit(-100.f,100.f,1200*std::log2(a/b)):std::numeric_limits<float>::quiet_NaN();
        std::move(noteHistory.begin()+1,noteHistory.end(),noteHistory.begin());std::move(targetHistory.begin()+1,targetHistory.end(),targetHistory.begin());
        const bool voiced=std::isfinite(pitch.back());const float level=voiced?juce::jlimit(0.f,1.f,std::sqrt(std::max(0.f,p.inputPeak.load()))*1.4f*p.pitchConfidence.load()):0.f;energy+=(level-energy)*(level>energy?.38f:.12f);std::move(energyHistory.begin()+1,energyHistory.end(),energyHistory.begin());energyHistory.back()=level;if(voiced)++signalTicks;noteHistory.back()=voiced?69.f+12.f*std::log2(a/440.f):std::numeric_limits<float>::quiet_NaN();targetHistory.back()=voiced?69.f+12.f*std::log2(b/440.f):std::numeric_limits<float>::quiet_NaN();repaint();}
    void paint(juce::Graphics& g)override{
        if(p.kind==GillKind::Tune){paintWheel(g);return;}
        auto r=getLocalBounds().toFloat();g.setGradientFill(juce::ColourGradient(dark,0,0,juce::Colour(0xff071727),0,r.getHeight(),false));g.fillRoundedRectangle(r,12);g.setColour(cream.withAlpha(.22f));g.drawRoundedRectangle(r.reduced(1),11,1);
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
    float getActivity()const noexcept{return energy;}
private:
    void paintWheel(juce::Graphics& g){
const auto r=getLocalBounds().toFloat();gill::prism::glass(g,r,9);
        auto a=r.reduced(5).withTrimmedLeft(35).withTrimmedTop(10).withTrimmedBottom(20);
        float lowest=60,highest=72;bool any=false;for(float v:noteHistory)if(std::isfinite(v)){lowest=any?std::min(lowest,v):v;highest=any?std::max(highest,v):v;any=true;}
        const int low=juce::jlimit(24,95,int(std::floor(lowest))-2);const int high=juce::jlimit(low+12,108,int(std::ceil(highest))+2);const float count=float(high-low+1),row=a.getHeight()/count;
        const auto yFor=[&](float midi){return a.getBottom()-(midi-low+.5f)*row;};
        for(int midi=low;midi<=high;++midi){const int key=midi%12;const bool black=key==1||key==3||key==6||key==8||key==10;const float y=yFor(float(midi))-row*.5f;
            g.setColour((black?gill::prism::dark:gill::prism::navy).withAlpha(.55f));g.fillRect(a.getX(),y,a.getWidth(),row);
            auto piano=juce::Rectangle<float>(5,y,black?21.f:31.f,std::max(1.f,row-.7f));g.setGradientFill(juce::ColourGradient(black?gill::prism::navy:gill::prism::silver,piano.getX(),y,black?gill::prism::dark:gill::prism::white,piano.getRight(),y,false));g.fillRect(piano);
            if(key==0){g.setColour(gill::prism::white.withAlpha(.75f));g.setFont(font(10));g.drawText("C"+juce::String(midi/12-1),a.getX()+4,y,24,row,juce::Justification::centredLeft);}}
        for(int i=0;i<=12;++i){g.setColour(gill::prism::silver.withAlpha(i%3==0?.19f:.07f));g.drawVerticalLine(juce::roundToInt(a.getX()+i*a.getWidth()/12),a.getY(),a.getBottom());}
        const auto xFor=[&](size_t i){return a.getX()+float(i)*a.getWidth()*.90f/float(noteHistory.size()-1);};
        for(size_t i=0;i<targetHistory.size();){if(!std::isfinite(targetHistory[i])){++i;continue;}size_t end=i+1;while(end<targetHistory.size()&&std::isfinite(targetHistory[end])&&std::abs(targetHistory[end]-targetHistory[i])<.05f)++end;
            auto blob=juce::Rectangle<float>(xFor(i),yFor(targetHistory[i])-row*.46f,std::max(2.f,xFor(std::min(end,targetHistory.size()-1))-xFor(i)),std::max(5.f,row*.92f));
            float charge=0;for(size_t sample=i;sample<end;++sample)charge=std::max(charge,energyHistory[sample]);
            {juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(a.expanded(0,2).toNearestInt());gill::prism::noteGlass(g,blob,gill::prism::cyan,charge,end==targetHistory.size());}
            i=end;}
        juce::Path trace;bool pen=false;for(size_t i=0;i<noteHistory.size();++i){if(!std::isfinite(noteHistory[i])){pen=false;continue;}if(pen)trace.lineTo(xFor(i),yFor(noteHistory[i]));else{trace.startNewSubPath(xFor(i),yFor(noteHistory[i]));pen=true;}}
        {juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(a.toNearestInt());gill::prism::electricTrace(g,trace,gill::prism::pink,.30f+energy*.70f,1.25f);
            // Spark position follows real measured pitch. It never creates or shifts a note.
            if(energy>.015f&&std::isfinite(noteHistory.back())){const size_t index=noteHistory.size()-1-std::size_t(signalTicks%23);if(std::isfinite(noteHistory[index])){const juce::Point<float>pos{xFor(index),yFor(noteHistory[index])};g.setGradientFill(juce::ColourGradient(gill::prism::white.withAlpha(.60f*energy),pos.x,pos.y,juce::Colours::transparentWhite,pos.x+7,pos.y+7,true));g.fillEllipse(pos.x-7,pos.y-7,14,14);g.setColour(gill::prism::white);g.fillEllipse(pos.x-1,pos.y-1,2,2);}}
        }
        if(energy>.01f&&std::isfinite(noteHistory.back())){const float x=xFor(noteHistory.size()-1);juce::Path playhead;playhead.startNewSubPath(x,a.getY());playhead.lineTo(x,a.getBottom());gill::prism::electricTrace(g,playhead,gill::prism::cyan,energy,.65f);juce::Path cap;cap.addTriangle(x-4,a.getY()-1,x+4,a.getY()-1,x,a.getY()+6);g.setColour(gill::prism::white);g.fillPath(cap);}
        const bool live=p.value("gillQuality")<.5f;g.setFont(font(10));g.setColour(gill::prism::white.withAlpha(.70f));g.drawText(live?"LIVE / DRY MONITOR / 0 SAMPLES":"PRO / INPUT PITCH + CORRECTED TARGET",r.withY(r.getBottom()-18).withHeight(14).reduced(8,0),juce::Justification::centredLeft);
        if(!any){g.setColour(gill::prism::white.withAlpha(.75f));g.setFont(font(14));g.drawText(live?"SWITCH TO PRO FOR PITCH + FORMANT":"PLAY A MONOPHONIC VOCAL",a,juce::Justification::centred);}
        if(!p.rateSupported.load()){g.setColour(gill::prism::pink);g.drawText("RATE UNSUPPORTED / BYPASS",a,juce::Justification::centred);}
    }
    GillVocalProcessor& p;std::array<float,100> pre{},post{},pitch{};std::array<float,240> noteHistory{},targetHistory{},energyHistory{};float energy=0;unsigned signalTicks=0;
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
    gill::QualitySelector quality{p.apvts, p};
    Impl(GillVocalEditor& o,GillVocalProcessor& v):owner(o),p(v),display(v),tooltip(&o,600){
        owner.setLookAndFeel(&look);owner.addAndMakeVisible(quality);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);owner.addAndMakeVisible(display);
        auto add=[&](const char* id,const char* title,const char* suffix,bool vertical=false){auto b=std::make_unique<Binding>(p,id,title,suffix,vertical);owner.addAndMakeVisible(b->slider);owner.addAndMakeVisible(b->label);controls.push_back(std::move(b));};
        bypass.setName("BYPASS");bypass.setClickingTogglesState(true);owner.addAndMakeVisible(bypass);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);bypass.setTooltip("VERARBEITUNG UMGEHEN; GEMELDETE LATENZ BLEIBT GLEICH");
        if(p.kind==GillKind::Flow){baseWidth=340;baseHeight=480;add("amount","AMOUNT"," %",true);owner.addAndMakeVisible(learn);owner.addAndMakeVisible(autogain);owner.addAndMakeVisible(status);
            autogain.setClickingTogglesState(true);autoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"autogain",autogain);autogain.setTooltip("LAUTSTAERKE LANGSAM AUSGLEICHEN; MAXIMAL +9 DB");
            learn.setName("LEARN");learn.setTooltip("LEARN, SONG ABSPIELEN, DANN STOP. BIS 5 MINUTEN. ERNEUTER KLICK SCHLIESST DIE ANALYSE AB.");learn.onClick=[this]{const auto state=p.learnState.load();p.requestLearning(state!=1&&state!=4);};status.setJustificationType(juce::Justification::centred);status.setColour(juce::Label::textColourId,ink);status.setFont(font(12,true));
        }else if(p.kind==GillKind::Heat){baseWidth=580;baseHeight=380;add("low","LOW"," %",true);add("mid","MID"," %",true);add("high","HIGH"," %",true);add("mix","MIX"," %");add("output","OUTPUT"," DB");for(int i=0;i<3;++i){controls[i]->slider.textFromValueFunction=[](double v){return juce::String(v/24.*100.,1);};controls[i]->slider.valueFromTextFunction=[](const juce::String& t){return t.getDoubleValue()*.24;};controls[i]->slider.updateText();controls[i]->slider.setTooltip("SATURATION: STUFENLOS VON CLEAN BIS EXTREME. DIE FAERBUNG NIMMT UEBER DEN GANZEN REGELWEG ZU; ABHAENGIG VOM EINGANGSSIGNAL.");}}
        else{baseWidth=760;baseHeight=430;add("retune","RETUNE"," MS");add("humanize","HUMANIZE"," %");add("mix","MIX"," %");add("formant","FORMANT"," ST");
            controls[0]->slider.commandWheel=false;controls[0]->slider.setSliderStyle(juce::Slider::Rotary);controls[0]->slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);controls[0]->slider.setMouseCursor(juce::MouseCursor::PointingHandCursor);controls[0]->slider.setTooltip("RETUNE 0 BIS 200 MS: DEN REGLER IM KREIS ZIEHEN. KLEINER = SCHNELLERE KORREKTUR. PRO: PITCH + FORMANT. LIVE: CLEAN DRY MONITOR, 0 SAMPLES.");
            for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);preset.setName("PRESET");preset.onChange=[this]{if(preset.getSelectedId()>0)p.setCurrentProgram(preset.getSelectedId()-1);};owner.addAndMakeVisible(preset);
            for(int i:{1,2,3}){controls[i]->slider.setSliderStyle(juce::Slider::LinearHorizontal);controls[i]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,76,21);}
            controls[3]->slider.setTooltip("FORMANT: -12 TO +12 SEMITONES. PRO ONLY; LIVE MONITORS THE CLEAN ORIGINAL AT ZERO PLUG-IN DELAY.");
            auto* kp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("key"));auto* sp=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("scale"));key.addItemList(kp->choices,1);scale.addItemList(sp->choices,1);key.setName("KEY");scale.setName("SCALE");owner.addAndMakeVisible(key);owner.addAndMakeVisible(scale);keyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"key",key);scaleAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"scale",scale);
            key.setTooltip("GRUNDTON DES SONGS; BEI CHROMATIC WERDEN ALLE HALBTOENE VERWENDET");scale.setTooltip("PASSENDE SONG-SKALA WAEHLEN; EINZELNE VOCALSPUR VERWENDEN");
            keyLabel.setText("KEY",juce::dontSendNotification);scaleLabel.setText("SCALE",juce::dontSendNotification);for(auto* label:{&keyLabel,&scaleLabel}){label->setColour(juce::Label::textColourId,ink);label->setFont(font(12,true));owner.addAndMakeVisible(*label);}
        }
        if(p.kind!=GillKind::Tune){const char* flowNames[]{"NATURAL","FOCUS","CRUSH"};const char* heatNames[]{"WARM","TAPE","EDGE"};for(int i=0;i<3;++i){modes[i].setButtonText(p.kind==GillKind::Flow?flowNames[i]:heatNames[i]);modes[i].setName(modes[i].getButtonText());modes[i].onClick=[this,i]{p.setValue(p.kind==GillKind::Flow?"mode":"style",static_cast<float>(i));};owner.addAndMakeVisible(modes[i]);}}
        startTimerHz(25);timerCallback();
    }
    ~Impl(){stopTimer();owner.setLookAndFeel(nullptr);}
    void timerCallback()override{display.tick();if(p.kind!=GillKind::Tune){const int selected=juce::roundToInt(p.value(p.kind==GillKind::Flow?"mode":"style"));for(int i=0;i<3;++i)modes[i].setToggleState(i==selected,juce::dontSendNotification);}
        if(p.kind==GillKind::Flow){const int state=p.learnState.load();learn.setButtonText(state==1?"FINISH":state==4?"CANCEL":"LEARN");status.setText(state==4?"ARMED / PRESS PLAY":state==1?"LISTENING  "+juce::String(p.learnProgress.load()*300,0)+" S / STOP TO FINISH":state==2?"PROFILE READY":state==3?"MORE VOCAL NEEDED":"PLAY VOCAL + LEARN",juce::dontSendNotification);}
        else if(p.kind==GillKind::Tune){preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);preset.setText(p.getProgramName(p.getCurrentProgram())+(p.presetMatches()?"":" *"),juce::dontSendNotification);}owner.repaint();
    }
    void bounds(juce::Component& c,float x,float y,float w,float h){const float s=owner.getWidth()/static_cast<float>(baseWidth);c.setBounds(juce::Rectangle<float>(x*s,y*s,w*s,h*s).toNearestInt());}
    void control(int i,float x,float y,float w,float h){bounds(controls[i]->label,x,y,w,21);bounds(controls[i]->slider,x,y+24,w,h-24);}
    void resized(){bounds(quality,p.kind==GillKind::Flow?94.f:baseWidth-232.f,p.kind==GillKind::Flow?36.f:21.f,110,26);if(p.kind==GillKind::Flow){bounds(bypass,235,19,84,29);bounds(display,20,65,300,115);control(0,66,197,133,185);for(int i=0;i<3;++i)bounds(modes[i],20+i*101.f,382,97,29);bounds(learn,20,418,115,29);bounds(autogain,145,418,123,29);bounds(status,20,451,300,19);}
        else if(p.kind==GillKind::Heat){bounds(bypass,475,19,84,29);bounds(display,20,65,540,89);for(int i=0;i<3;++i)control(i,22+i*108.f,168,98,140);for(int i=0;i<3;++i)bounds(modes[i],20+i*108.f,335,102,29);control(3,356,197,88,114);control(4,466,197,91,114);}
        else{bounds(quality,534,19,110,26);bounds(bypass,653,18,86,28);bounds(preset,269,18,249,29);
            bounds(keyLabel,22,85,89,18);bounds(key,22,110,89,31);bounds(scaleLabel,22,167,89,18);bounds(scale,22,192,89,31);
            keyLabel.setColour(juce::Label::textColourId,gill::prism::white);scaleLabel.setColour(juce::Label::textColourId,gill::prism::white);
            bounds(display,126,63,613,207);control(0,122,279,162,141);control(1,301,320,132,79);control(3,452,320,132,79);control(2,603,320,132,79);
        }
    }
    void paint(juce::Graphics& g){const float scale=owner.getWidth()/static_cast<float>(baseWidth);g.addTransform(juce::AffineTransform::scale(scale));const float s=1.f;
        gill::prism::chassis(g,float(baseWidth),float(baseHeight),p.kind==GillKind::Tune?274.f:p.kind==GillKind::Flow?187.f:160.f,p.kind==GillKind::Flow?61.f:57.f,true,p.kind==GillKind::Tune?65.f:23.f,display.getActivity());
        if(p.kind==GillKind::Tune)gill::prism::tuneKeyPanel(g);
        gill::prism::title(g,p.getName(),{75,p.kind==GillKind::Flow?11.f:16.f,p.kind==GillKind::Flow?153.f:p.kind==GillKind::Tune?178.f:baseWidth-309.f,31},p.kind==GillKind::Flow?20.f:23.f);
        if(p.kind==GillKind::Tune){g.setColour(ink.withAlpha(.28f));for(float x:{288.f,442.f,593.f})g.drawLine(x,321,x,404,.7f);}
        if(p.kind==GillKind::Flow){g.setColour(ink);const float gr=juce::jlimit(0.f,30.f,p.reduction.load());g.setFont(font(12*s,true));g.drawText("GR",juce::Rectangle<float>(231*s,199*s,58*s,20*s),juce::Justification::centred,false);
            const auto bar=juce::Rectangle<float>(246*s,228*s,18*s,120*s);g.setColour(dark);g.fillRoundedRectangle(bar,4*s);for(int i=0;i<24;++i){g.setColour(i<gr/30*24?sage.brighter(.65f):sage.withAlpha(.2f));g.fillRect(250*s,(231+i*4.7f)*s,10*s,2.8f*s);}g.setFont(font(12*s));g.setColour(ink);for(int db:{0,15,30})g.drawText(juce::String(db),juce::Rectangle<float>(271*s,(222+db*3.8f)*s,30*s,18*s),juce::Justification::left,false);
            g.setFont(font(16*s,true));g.drawText("-"+juce::String(gr,1)+" DB",juce::Rectangle<float>(215*s,356*s,99*s,24*s),juce::Justification::centred,false);
        }else if(p.kind==GillKind::Heat){g.setFont(font(12*s,true));g.setColour(ink.withAlpha(.7f));const char* captions[]{"< 180 HZ","180 HZ - 3K","> 3 KHZ"};for(int i=0;i<3;++i)g.drawText(captions[i],juce::Rectangle<float>((22+i*108)*s,312*s,98*s,17*s),juce::Justification::centred,false);g.drawLine(342*s,175*s,342*s,361*s,s);}
    }

};
GillVocalEditor::GillVocalEditor(GillVocalProcessor& p):AudioProcessorEditor(&p){impl=std::make_unique<Impl>(*this,p);const int w=impl->baseWidth,h=impl->baseHeight;setResizable(true,true);setResizeLimits(w,h,w*2,h*2);if(auto* c=getConstrainer())c->setFixedAspectRatio(static_cast<double>(w)/h);setSize(w,h);}
GillVocalEditor::~GillVocalEditor()=default;
void GillVocalEditor::paint(juce::Graphics& g){impl->paint(g);}
void GillVocalEditor::resized(){if(impl)impl->resized();}

