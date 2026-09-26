#include "PluginEditor.h"
#include <map>

namespace {
const juce::Colour gold(0xffddba82),cream(0xfff0dfbf),ink(0xff121211),edge(0xff735434);
void text(juce::Graphics&g,const juce::String&s,juce::Rectangle<float>r,float size,juce::Colour colour=cream,int alignment=juce::Justification::centred){g.setColour(colour);g.setFont(juce::FontOptions(size));g.drawFittedText(s,r.toNearestInt(),alignment,1);}
void panel(juce::Graphics&g,juce::Rectangle<float>r,float radius=7){g.setColour(juce::Colours::black.withAlpha(.6f));g.fillRoundedRectangle(r.translated(0,2),radius);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff333330),r.getX(),r.getY(),ink,r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,radius);g.setColour(juce::Colour(0xffa18c70).withAlpha(.6f));g.drawRoundedRectangle(r.reduced(.5f),radius,1);g.setColour(juce::Colours::black);g.drawRoundedRectangle(r.reduced(2),radius-1,1);}
void glass(juce::Graphics&g,juce::Rectangle<float>r){panel(g,r,8);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff20211e),r.getX(),r.getY(),juce::Colour(0xff080908),r.getX(),r.getBottom(),false));g.fillRoundedRectangle(r.reduced(5),4);g.setColour(juce::Colours::white.withAlpha(.035f));g.fillRoundedRectangle(r.reduced(6).withHeight(r.getHeight()*.4f),3);}
juce::Image metalFace(){
    juce::Image face(juce::Image::ARGB,256,256,true);juce::Image::BitmapData pixels(face,juce::Image::BitmapData::writeOnly);
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){const double dx=(x-127.5)/127.,dy=(y-127.5)/127.,r=std::hypot(dx,dy);if(r>1)continue;const double a=std::atan2(dy,dx);const double v=std::clamp(.55+.22*std::cos(a+.7)+.18*std::cos(2*a-1.1)+.035*std::sin(r*740)-.10*r,0.,1.);pixels.setPixelColour(x,y,juce::Colour::fromFloatRGBA(float(.43+.56*v),float(.31+.60*v),float(.18+.63*v),float(std::clamp((1-r)*128,0.,1.))));}return face;
}
struct MasterLook:juce::LookAndFeel_V4 {
    juce::Image metal=metalFace();
    MasterLook(){setColour(juce::Slider::textBoxTextColourId,cream);setColour(juce::Slider::textBoxBackgroundColourId,ink);setColour(juce::Slider::textBoxOutlineColourId,edge);setColour(juce::ComboBox::textColourId,cream);setColour(juce::ComboBox::backgroundColourId,ink);setColour(juce::ComboBox::outlineColourId,edge);setColour(juce::PopupMenu::backgroundColourId,ink);setColour(juce::PopupMenu::textColourId,cream);setColour(juce::PopupMenu::highlightedBackgroundColourId,edge);setColour(juce::TextEditor::textColourId,cream);setColour(juce::TextEditor::backgroundColourId,ink);}
    void drawButtonBackground(juce::Graphics&g,juce::Button&b,const juce::Colour&,bool over,bool down)override{
        auto r=b.getLocalBounds().toFloat().reduced(1);const bool on=b.getToggleState();g.setColour(juce::Colours::black.withAlpha(.7f));g.fillRoundedRectangle(r.translated(0,1),4);
        auto top=on?juce::Colour(0xfff4d9ac):juce::Colour(0xff37352f),bottom=on?juce::Colour(0xffb58750):juce::Colour(0xff111210);if(over){top=top.brighter(.1f);bottom=bottom.brighter(.1f);}if(down){top=top.darker(.15f);bottom=bottom.darker(.15f);}
        g.setGradientFill(juce::ColourGradient(top,r.getX(),r.getY(),bottom,r.getX(),r.getBottom(),false));g.fillRoundedRectangle(r,4);g.setColour(on?cream:juce::Colour(0xff888076));g.drawRoundedRectangle(r.reduced(.5f),4,1);g.setColour(juce::Colours::black.withAlpha(.7f));g.drawRoundedRectangle(r.reduced(2),3,1);
    }
    void drawButtonText(juce::Graphics&g,juce::TextButton&b,bool,bool)override{text(g,b.getButtonText(),b.getLocalBounds().toFloat().reduced(5),b.getWidth()<70?10.5f:11.5f,b.getToggleState()?ink:cream);}
    void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&)override{
        const float radius=std::min(float(w),float(h))*.39f,cx=x+w*.5f,cy=y+h*.5f,angle=start+value*(end-start);auto ring=juce::Rectangle<float>(cx-radius,cy-radius,2*radius,2*radius);
        for(int i=0;i<=28;++i){const float a=start+(end-start)*i/28.f;const auto p=juce::Point<float>(cx,cy).getPointOnCircumference(radius+6,a);const auto q=juce::Point<float>(cx,cy).getPointOnCircumference(radius+9,a);g.setColour(i<=int(value*28)?gold:juce::Colour(0xff71634f));g.drawLine({p,q},i%4==0?1.3f:.6f);}
        g.setColour(juce::Colours::black.withAlpha(.75f));g.fillEllipse(ring.expanded(3).translated(1,3));g.setGradientFill(juce::ColourGradient(cream,cx-radius,cy-radius,juce::Colour(0xff49301d),cx+radius,cy+radius,false));g.fillEllipse(ring.expanded(1));g.setColour(ink);g.fillEllipse(ring.reduced(2));
        auto face=ring.reduced(5);g.drawImage(metal,face);g.setColour(cream.withAlpha(.6f));g.drawEllipse(face,.6f);
        const auto p=juce::Point<float>(cx,cy).getPointOnCircumference(radius*.40f,angle),q=juce::Point<float>(cx,cy).getPointOnCircumference(radius*.78f,angle);g.setColour(juce::Colours::black.withAlpha(.7f));g.drawLine({p.translated(1,1),q.translated(1,1)},4);g.setColour(cream);g.drawLine({p,q},2.7f);
    }
    void drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float sliderPos,float,float,juce::Slider::SliderStyle,juce::Slider&)override{
        const float cy=y+h*.5f;g.setColour(juce::Colours::black);g.fillRoundedRectangle(float(x),cy-3,float(w),6,3);g.setColour(edge);g.drawRoundedRectangle(float(x),cy-3,float(w),6,3,.6f);g.setColour(gold.withAlpha(.6f));g.fillRoundedRectangle(float(x),cy-1,std::max(0.f,sliderPos-x),2,1);auto handle=juce::Rectangle<float>(sliderPos-4,cy-8,8,16);g.setGradientFill(juce::ColourGradient(cream,handle.getX(),cy-8,edge,handle.getRight(),cy+8,false));g.fillRoundedRectangle(handle,2);
    }
};
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
    void resized()override{auto r=getLocalBounds();if(spec.flag)button.setBounds(r);else{if(compact&&getHeight()<35)r.removeFromLeft(54);else r.removeFromTop(17);if(!spec.choices.isEmpty())combo.setBounds(r.withHeight(std::min(25,r.getHeight())));else slider.setBounds(r);}}
    void paint(juce::Graphics&g)override{if(!spec.flag){if(compact&&getHeight()<35)text(g,spec.label,{0,0,50,float(getHeight())},9);else text(g,spec.label,{0,0,float(getWidth()),16},10);}}
};
juce::Image woodTexture(int w,int h){
    juce::Image image(juce::Image::RGB,w,h,false);juce::Image::BitmapData data(image,juce::Image::BitmapData::writeOnly);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){std::uint32_t hash=std::uint32_t(x*374761393u+y*668265263u);hash=(hash^(hash>>13))*1274126177u;const double noise=(double(hash&255)/255-.5);const double wave=std::sin(y*.39+5*std::sin(x*.008)+1.2*std::sin(y*.019+x*.002));const double fine=std::sin(y*2.1+3*std::sin(x*.032));const double v=wave*.035+fine*.045+noise*.13+.05*std::sin(x*.007+y*.012);data.setPixelColour(x,y,juce::Colour::fromFloatRGBA(float(.26+v*.33),float(.15+v*.22),float(.085+v*.13),1));}
    return image;
}
void logo(juce::Graphics&g,juce::Rectangle<float>r){
    juce::Path p;p.startNewSubPath(28,5);p.lineTo(15,5);p.cubicTo(-3,5,-3,30,15,30);p.lineTo(29,30);p.lineTo(29,19);p.lineTo(17,19);p.startNewSubPath(24,39);p.lineTo(24,13);p.lineTo(40,13);p.cubicTo(55,13,55,31,40,31);p.lineTo(24,31);
    p.applyTransform(juce::AffineTransform::scale(r.getWidth()/54,r.getHeight()/43).translated(r.getX(),r.getY()));g.setColour(juce::Colours::black);g.strokePath(p,juce::PathStrokeType(5.5f));g.setColour(gold);g.strokePath(p,juce::PathStrokeType(3.2f));g.setColour(cream.withAlpha(.5f));g.strokePath(p,juce::PathStrokeType(.6f));
}
juce::String dbLabel(float linear){return linear>1e-7f?juce::String(20*std::log10(linear),1):juce::String("--");}
}

struct GillMasterEditor::Impl:juce::Component,private juce::Timer {
    GillMasterEditor&owner;GillMasterProcessor&p;MasterLook look;juce::Image wood;
    std::map<juce::String,std::unique_ptr<Control>>controls;
    juce::TextButton live{"LIVE"},pro{"PRO"},bypass{"BYPASS"},previous{"<"},next{">"},learn{"LEARN"},analyze{"ANALYZE"},stop{"STOP"},reset{"RESET"},report{"REPORT"};
    juce::ComboBox presets;std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>bypassAttachment;
    std::unique_ptr<juce::FileChooser>chooser;juce::TooltipWindow tips{&owner,600};
    Impl(GillMasterEditor&o,GillMasterProcessor&processor):owner(o),p(processor){
        const auto info=masterInfo(p.kind);setSize(info.width,info.height);wood=woodTexture(info.width,info.height);setLookAndFeel(&look);
        for(const auto&s:p.specs)addControl(s);
        if(int(p.kind)<6){addControl({"output","OUTPUT",-18,p.kind==MasterKind::Ceiling?0.f:12.f,.01f,0," dB",{},false});addControl({"mix","MIX",0,100,.1f,100," %",{},false});}
        for(auto*b:{&live,&pro,&bypass,&previous,&next})addAndMakeVisible(*b);
        live.onClick=[this]{p.setValue("gillQuality",0);};pro.onClick=[this]{p.setValue("gillQuality",1);};bypass.setClickingTogglesState(true);bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass);
        for(int i=0;i<6;++i)presets.addItem(p.getProgramName(i),i+1);presets.onChange=[this]{p.selectPreset(presets.getSelectedId()-1);};addAndMakeVisible(presets);
        previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+5)%6);};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%6);};
        if(p.delta){addAndMakeVisible(learn);learn.onClick=[this]{p.delta->learn();};learn.setTooltip("Put SOURCE before the chain and RETURN after it, select the same PAIR, then play audio and LEARN. Up to 500 ms delay. Links stay inside one host process. Offline export always uses AFTER.");}
        if(p.deliver){for(auto*b:{&analyze,&stop,&reset,&report})addAndMakeVisible(*b);analyze.onClick=[this]{p.deliver->start();};stop.onClick=[this]{p.deliver->stop();};reset.onClick=[this]{p.deliver->clear();};report.onClick=[this]{saveReport();};}
        if(p.kind==MasterKind::Ceiling){addAndMakeVisible(reset);reset.onClick=[this]{p.resetMeterRequested=true;};}
        refresh();startTimerHz(25);resized();
    }
    ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
    void addControl(const MasterParam&s){auto c=std::make_unique<Control>(p,s);addAndMakeVisible(*c);controls.emplace(s.id,std::move(c));}
    void place(const char*id,int x,int y,int w,int h,bool small=false){auto it=controls.find(id);if(it==controls.end())return;it->second->small(small);it->second->setBounds(x,y,w,h);}
    void resized()override{
        const int w=getWidth(),h=getHeight();const int headerButton=w<500?43:51;
        live.setBounds(w-2*headerButton-78,17,headerButton,27);pro.setBounds(w-headerButton-78,17,headerButton,27);bypass.setBounds(w-73,17,59,27);
        previous.setBounds(17,h-43,30,27);next.setBounds(w-47,h-43,30,27);presets.setBounds(54,h-43,w-108,27);
        switch(p.kind){
        case MasterKind::Ceiling:place("drive",286,278,142,143);place("ceiling",438,303,110,118);place("release",159,303,110,118);place("character",266,231,228,44);place("match",30,340,118,27);place("output",565,305,161,49,true);place("mix",565,369,161,49,true);reset.setBounds(655,239,71,23);break;
        case MasterKind::Low:place("amount",28,154,100,100);place("frequency",141,154,100,100);place("threshold",254,154,100,100);place("protect",367,154,100,100);place("width",480,154,100,100);place("listen",28,268,105,24);place("output",177,255,180,39,true);place("mix",401,255,180,39,true);break;
        case MasterKind::Glue:place("amount",139,223,143,158);place("attack",24,259,109,122);place("release",287,259,109,122);place("detector",24,397,119,50,true);place("character",156,397,237,45);place("match",248,453,145,26);place("output",24,451,105,44,true);place("mix",136,451,105,44,true);break;
        case MasterKind::Width:place("low",26,304,195,53,true);place("mid",242,304,195,53,true);place("high",458,304,195,53,true);place("lowHz",29,133,142,50,true);place("highHz",29,199,142,50,true);place("guard",491,139,162,28);place("mono",491,179,162,28);place("output",491,221,162,48,true);place("mix",29,259,142,42,true);break;
        case MasterKind::Punch:place("low",25,178,104,123);place("mid",146,178,104,123);place("high",267,178,104,123);place("sustain",389,178,104,123);place("lowHz",511,170,143,38,true);place("highHz",511,209,143,38,true);place("output",511,251,143,25,true);place("mix",511,280,143,25,true);break;
        case MasterKind::Weight:place("amount",130,115,140,108);place("frequency",18,128,105,95);place("colour",277,140,105,43);place("speaker",277,197,105,26);place("output",25,229,164,39,true);place("mix",210,229,165,39,true);break;
        case MasterKind::Delta:place("role",25,209,115,45);place("pair",151,209,63,45);place("audition",329,209,167,45);place("match",511,226,160,27);learn.setBounds(227,226,89,27);break;
        case MasterKind::Deliver:place("target",28,345,121,94);place("peakTarget",159,345,121,94);place("silence",290,345,121,94);analyze.setBounds(25,454,100,31);stop.setBounds(134,454,78,31);reset.setBounds(221,454,78,31);report.setBounds(308,454,107,31);break;
        }
    }
    void refresh(){const bool isPro=p.value("gillQuality")>.5f;live.setToggleState(!isPro,juce::dontSendNotification);pro.setToggleState(isPro,juce::dontSendNotification);const auto latency=juce::String(p.getLatencySamples())+" samples / "+juce::String(1000.*p.getLatencySamples()/p.rateView.load(),2)+" ms";live.setTooltip("LIVE: zero added algorithmic latency. "+latency);pro.setTooltip("PRO: full processing quality. "+latency);presets.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);if(p.delta){learn.setButtonText(p.delta->learning()?"LEARNING":"LEARN");learn.setEnabled(p.value("role")<.5f);}if(p.deliver){const auto s=p.deliver->snapshot();analyze.setToggleState(s.running,juce::dontSendNotification);report.setEnabled(s.duration>0);}repaint();}
    void timerCallback()override{refresh();}
    void saveReport(){const auto content=p.deliveryReport();chooser=std::make_unique<juce::FileChooser>("SAVE GILLDELIVER REPORT",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GILLDELIVER-REPORT.txt"),"*.txt");const juce::Component::SafePointer<Impl>safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,content](const juce::FileChooser&f){if(safe){const auto file=f.getResult();if(file!=juce::File{}&&!file.replaceWithText(content))juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"REPORT","The report could not be saved to this location.");}});}
    void history(juce::Graphics&g,juce::Rectangle<float>r,bool before=true,bool after=true){
        glass(g,r);auto graph=r.reduced(15,19);g.setColour(cream.withAlpha(.12f));for(int i=0;i<5;++i){const float y=graph.getY()+i*graph.getHeight()/4;g.drawHorizontalLine(int(y),graph.getX(),graph.getRight());}for(int i=0;i<9;++i){float x=graph.getX()+i*graph.getWidth()/8;g.drawVerticalLine(int(x),graph.getY(),graph.getBottom());}
        auto plot=[&](const std::array<std::atomic<float>,256>&a,juce::Colour color){juce::Path path;const unsigned end=p.historyPosition.load();for(unsigned i=0;i<256;++i){const float v=a[(end+i)%256].load();const float db=v>1e-8f?20*std::log10(v):-60;const float x=graph.getX()+graph.getWidth()*i/255.f,y=graph.getBottom()-juce::jlimit(0.f,1.f,(db+60)/60)*graph.getHeight();if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}g.setColour(color);g.strokePath(path,juce::PathStrokeType(1.25f));};
        if(before)plot(p.historyIn,cream.withAlpha(.48f));if(after)plot(p.historyOut,gold);text(g,"PEAK HISTORY  /  6.4 S",r.withY(r.getBottom()-16).withHeight(13),8,cream.withAlpha(.5f));
    }
    void vu(juce::Graphics&g,juce::Rectangle<float>r){glass(g,r);auto face=r.reduced(9);g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe6c695),face.getCentreX(),face.getY(),juce::Colour(0xfffff0c9),face.getCentreX(),face.getBottom(),false));g.fillRoundedRectangle(face,4);g.saveState();g.reduceClipRegion(face.toNearestInt());const auto centre=juce::Point<float>(r.getCentreX(),r.getBottom()+48);const float radius=r.getWidth()*.44f;
        for(int i=0;i<=10;++i){const float angle=-.95f+1.9f*i/10;auto a=centre.getPointOnCircumference(radius,angle),b=centre.getPointOnCircumference(radius+7,angle);g.setColour(ink);g.drawLine({a,b},1.2f);if(i%2==0)text(g,juce::String(20-i*2),{b.x-12,b.y-16,24,14},10,ink);}
        const float angle=.95f-1.9f*juce::jlimit(0.f,20.f,p.reduction.load())/20;auto tip=centre.getPointOnCircumference(radius,angle);g.setColour(ink);g.drawLine({centre,tip},2);g.restoreState();text(g,"GAIN REDUCTION",{r.getX(),r.getBottom()-36,r.getWidth(),18},10,ink);text(g,juce::String(p.reduction.load(),1)+" DB",{r.getX(),r.getBottom()-21,r.getWidth(),16},11,ink);
    }
    void scope(juce::Graphics&g){auto r=juce::Rectangle<float>(205,69,270,230);auto circle=r.withSizeKeepingCentre(222,222);g.setColour(gold);g.fillEllipse(circle.expanded(2));g.setColour(ink);g.fillEllipse(circle);g.setColour(cream.withAlpha(.13f));for(float q:{.33f,.66f,1.f})g.drawEllipse(circle.withSizeKeepingCentre(222*q,222*q),1);const auto c=circle.getCentre();g.drawLine(c.x,circle.getY(),c.x,circle.getBottom());g.drawLine(circle.getX(),c.y,circle.getRight(),c.y);
        g.saveState();juce::Path clip;clip.addEllipse(circle);g.reduceClipRegion(clip);const auto end=p.scopePosition.load();float maximum=.1f;for(unsigned i=0;i<256;++i)maximum=std::max(maximum,std::max(std::abs(p.scopeL[i].load()),std::abs(p.scopeR[i].load())));for(unsigned i=0;i<256;++i){const auto at=(end+i)%256;const float l=p.scopeL[at].load(),rr=p.scopeR[at].load();g.setColour(gold.withAlpha(.1f+.6f*i/256));g.fillEllipse(c.x+(l-rr)*75/maximum-1,c.y-(l+rr)*75/maximum-1,2,2);}g.restoreState();text(g,"STEREO FIELD",circle.withY(circle.getY()+8).withHeight(15),9);text(g,"CORRELATION  "+juce::String(p.correlation.load(),2),{491,99,162,25},11,p.correlation.load()<0?juce::Colour(0xffe79a71):gold);
    }
    void paint(juce::Graphics&g)override{
        const float w=float(getWidth()),h=float(getHeight());g.drawImageAt(wood,0,0);g.setColour(cream.withAlpha(.45f));g.drawRect(.5f,.5f,w-1,h-1,1.f);g.setColour(juce::Colours::black.withAlpha(.6f));g.drawRect(3.f,3.f,w-6,h-6,2.f);logo(g,{15,13,40,33});
        const float titleRight=live.getX()-7;const float titleSize=w<500?15.f:22.f;text(g,p.getName(),{62,11,titleRight-62,25},titleSize,gold);text(g,"MASTERING SERIES",{62,35,titleRight-62,12},w<500?7.5f:9.f,cream.withAlpha(.65f));panel(g,{13,57,w-26,h-109});
        switch(p.kind){
        case MasterKind::Ceiling:history(g,{25,69,478,151});glass(g,{513,69,221,151});text(g,"INTEGRATED",{523,82,201,20},11);text(g,p.integrated.load()>-99?juce::String(p.integrated.load(),1)+" LUFS":"-- LUFS",{523,103,201,34},24,gold);text(g,"GAIN REDUCTION",{523,144,201,20},11);text(g,juce::String(p.reduction.load(),1)+" DB",{523,165,201,31},23,gold);text(g,"INPUT / OUTPUT HISTORY",{30,74,240,15},9);break;
        case MasterKind::Low:history(g,{25,67,570,82});text(g,"LOW BAND CONTROL",{32,72,205,16},9);text(g,"REDUCTION  "+juce::String(p.reduction.load(),1)+" DB",{350,72,233,16},10,gold,juce::Justification::right);break;
        case MasterKind::Glue:vu(g,{24,69,372,144});break;
        case MasterKind::Width:scope(g);break;
        case MasterKind::Punch:history(g,{25,67,630,100});text(g,"ORIGINAL / PROCESSED",{34,72,210,16},9);for(int i=0;i<3;++i)text(g,juce::String(p.bandMeter[i].load(),1)+" DB",{337.f+i*100.f,72,93,16},10,gold);break;
        case MasterKind::Weight:history(g,{24,65,352,48},false,true);text(g,"HARMONIC WEIGHT  /  OUTPUT",{31,69,245,15},8);break;
        case MasterKind::Delta:{history(g,{25,67,319,115},true,false);history(g,{355,67,320,115},false,true);text(g,"RETURN INPUT",{30,72,300,15},9);text(g,"AUDITION OUTPUT",{360,72,300,15},9);const auto a=p.delta->alignment();juce::String status=p.delta->conflict()?"PAIR ALREADY HAS A SOURCE":p.value("role")>.5f?"SOURCE - PLACE BEFORE THE CHAIN":!p.delta->linked()?"WAITING FOR SOURCE":p.delta->learning()?"LEARNING DELAY AND LEVEL":a.valid?"LINKED  /  "+juce::String(a.delay)+" SAMPLES  /  "+juce::String(20*std::log10(a.gain),2)+" DB":"PLAY AUDIO AND PRESS LEARN";text(g,status,{25,188,650,19},11,gold);text(g,"SOURCE BEFORE  /  RETURN AFTER  /  SAME PAIR",{25,261,650,17},9,cream.withAlpha(.6f));break;}
        case MasterKind::Deliver:{const auto s=p.deliver->snapshot();glass(g,{25,68,390,152});text(g,"INTEGRATED",{33,81,165,19},10);text(g,s.integrated> -99?juce::String(s.integrated,1):"--",{33,102,165,49},38,gold);text(g,"LUFS",{33,153,165,18},12);text(g,s.truePeak?"TRUE PEAK":"SAMPLE PEAK",{210,81,194,19},10);text(g,s.hasAudio?juce::String(s.peakDb,1):"--",{210,105,194,40},31,gold);text(g,s.truePeak?"DBTP":"DBFS",{210,152,194,18},12);text(g,"LRA  "+(s.duration>=3?juce::String(s.range,1):juce::String("--"))+" LU     /     "+juce::String(s.duration,1)+" S",{33,184,370,23},13);glass(g,{25,230,390,101});text(g,"PEAK TARGET",{37,241,194,19},11,cream,juce::Justification::left);text(g,s.hasAudio&&s.peakDb<=p.value("peakTarget")?"OK":"CHECK",{254,241,140,19},11,gold);text(g,"CLIPPED FRAMES",{37,268,194,19},11,cream,juce::Justification::left);text(g,juce::String(juce::int64(s.clipped)),{254,268,140,19},11,gold);text(g,"START / END",{37,296,150,19},11,cream,juce::Justification::left);text(g,juce::String(s.leading,2)+" / "+juce::String(s.trailing,2)+" S",{202,296,192,19},11,gold);break;}
        }
        text(g,p.quality.isPro()?"PRO":"LIVE",{17,h-13,40,10},7,gold);text(g,juce::String(p.getLatencySamples())+" SAMPLES",{w-120,h-13,103,10},7,cream.withAlpha(.6f),juce::Justification::right);
    }
};
GillMasterEditor::GillMasterEditor(GillMasterProcessor&p):AudioProcessorEditor(&p),impl(std::make_unique<Impl>(*this,p)){
    addAndMakeVisible(*impl);const auto info=masterInfo(p.kind);setResizable(true,false);setResizeLimits(int(info.width*.85),int(info.height*.85),info.width*2,info.height*2);getConstrainer()->setFixedAspectRatio(double(info.width)/info.height);setSize(info.width,info.height);
}
GillMasterEditor::~GillMasterEditor()=default;
void GillMasterEditor::paint(juce::Graphics&g){g.fillAll(ink);}
void GillMasterEditor::resized(){if(impl){const auto info=masterInfo(impl->p.kind);impl->setBounds(0,0,info.width,info.height);impl->setTransform(juce::AffineTransform::scale(float(getWidth())/info.width,float(getHeight())/info.height));}}
