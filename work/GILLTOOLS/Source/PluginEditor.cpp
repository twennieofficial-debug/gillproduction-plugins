#include "PluginEditor.h"
#include "GillPlatform.h"
#include "BinaryData.h"
#include "../../GILLCommon/QualityUi.h"
#include "../../GILLCommon/MaterialUi.h"

namespace {
const juce::Colour ink(0xff263a30),cream(0xfff2ecdd),sage(0xff74947e),amber(0xffc99549),dark(0xff142b23);
juce::Font font(float size,bool bold=false){return juce::Font(juce::FontOptions(gillInterfaceFontName(),size,bold?juce::Font::bold:juce::Font::plain));}
class Look final:public juce::LookAndFeel_V4 {
public:
    Look(){setColour(juce::Slider::textBoxTextColourId,ink);setColour(juce::Slider::textBoxBackgroundColourId,cream);setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);setColour(juce::ComboBox::backgroundColourId,cream);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::outlineColourId,sage.darker());setColour(juce::ComboBox::arrowColourId,ink);setColour(juce::PopupMenu::backgroundColourId,cream);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,sage);}
    juce::Font getTextButtonFont(juce::TextButton&,int)override{return font(11,true);}
    juce::Font getComboBoxFont(juce::ComboBox&)override{return font(11,true);}
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool over,bool down)override{
        const auto face=b.getToggleState()?sage:b.getName()=="LEARN LEVEL"?juce::Colour(0xffc6a377):cream;
        gill::material::panel(g,b.getLocalBounds().toFloat().reduced(1),face,7,down);
        if(over){g.setColour(juce::Colours::white.withAlpha(.1f));g.fillRoundedRectangle(b.getLocalBounds().toFloat().reduced(1),7);}
        b.setColour(juce::TextButton::textColourOffId,ink);b.setColour(juce::TextButton::textColourOnId,ink);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&)override{
        gill::material::rotary(g,{float(x+3),float(y+3),float(w-6),float(h-6)},value,start,end,accent);
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float position,float,float,juce::Slider::SliderStyle style,juce::Slider&)override{
        const bool vertical=style==juce::Slider::LinearVertical;
        if(vertical){const float cx=x+w*.5f;gill::material::panel(g,{cx-5,float(y),10,float(h)},dark,5,true);g.setColour(accent);g.fillRoundedRectangle(cx-2,position,4,std::max(0.f,float(y+h)-position),2);gill::material::panel(g,{cx-18,position-10,36,20},cream,4);g.setColour(ink);g.drawHorizontalLine(int(position),cx-12,cx+12);}
        else{const float cy=y+h*.5f;gill::material::panel(g,{float(x),cy-4,float(w),8},dark,4,true);g.setColour(accent);g.fillRoundedRectangle(float(x),cy-2,std::max(0.f,position-x),4,2);gill::material::disc(g,juce::Rectangle<float>(15,15).withCentre({position,cy}));}
    }
    juce::Colour accent=sage;
};
struct Dial {
    juce::Slider slider;juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    Dial(GillToolsProcessor& p,const juce::String& id,const juce::String& title,const juce::String& suffix,juce::Slider::SliderStyle style=juce::Slider::Rotary){
        slider.setName(title);slider.setComponentID(id);slider.setSliderStyle(style);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,78,20);slider.setTextValueSuffix(suffix);slider.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);
        slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,cream);slider.setColour(juce::Slider::textBoxOutlineColourId,sage.withAlpha(.3f));
        auto* parameter=p.apvts.getParameter(id);slider.setDoubleClickReturnValue(true,parameter->convertFrom0to1(parameter->getDefaultValue()));
        attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,id,slider);
        label.setText(title,juce::dontSendNotification);label.setFont(font(10.5f,true));label.setColour(juce::Label::textColourId,ink);label.setJustificationType(juce::Justification::centred);
    }
};
}

struct GillToolsEditor::Impl:private juce::Timer {
    GillToolsEditor& owner;GillToolsProcessor& p;Look look;juce::Image wood;gill::QualitySelector quality;
    int width=540,height=440;bool syncing=false;
    juce::ComboBox preset;juce::TextButton previous{"<"},next{">"},bypass{"BYPASS"};
    juce::Label status;
    std::vector<std::unique_ptr<Dial>> dials;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::TextButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::array<juce::ComboBox,3> intervals;
    juce::TextButton learn{"LEARN LEVEL"},load{"LOAD"},clear{"CLEAR"};
    juce::Slider loopStart,loopEnd;
    std::unique_ptr<juce::FileChooser> chooser;
    gill::tools::ReferenceEngine::Snapshot referenceView;
    gill::tools::RescueDSP::Scope rescueView;
    Impl(GillToolsEditor& o,GillToolsProcessor& processor):owner(o),p(processor),quality(p.apvts,p){
        if(p.kind==ToolsKind::Reference){width=660;height=410;}
        if(p.kind==ToolsKind::Rescue){width=540;height=390;look.accent=amber;}
        owner.setLookAndFeel(&look);wood=juce::ImageCache::getFromMemory(BinaryData::core_reference_png,BinaryData::core_reference_pngSize);
        for(auto* component:std::initializer_list<juce::Component*>{&quality,&preset,&previous,&next,&bypass,&status})owner.addAndMakeVisible(component);
        bypass.setName("BYPASS");bypass.setComponentID("bypass");bypass.setClickingTogglesState(true);buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypass));
        preset.setName("PRESET");preset.setComponentID("preset");for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);
        preset.onChange=[this]{if(!syncing&&preset.getSelectedId()>0)p.selectPreset(preset.getSelectedId()-1);};
        previous.onClick=[this]{p.selectPreset((p.getCurrentProgram()+5)%6);};next.onClick=[this]{p.selectPreset((p.getCurrentProgram()+1)%6);};
        status.setFont(font(10.5f,true));status.setColour(juce::Label::textColourId,cream);
        if(p.harmony){
            addCombo("key","KEY",{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"});
            addCombo("scale","SCALE",{"MAJOR","MINOR","HARM. MINOR","PENTATONIC"});
            addToggle("direct","DIRECT");addDial("natural","NATURAL"," %");addDial("output","OUTPUT"," DB");
            for(int i=0;i<3;++i){const juce::String prefix="voice"+juce::String(i);
                addToggle(prefix+"on","VOICE "+juce::String(i+1));addCombo(prefix+"mode","MODE "+juce::String(i+1),{"SCALE","SEMITONES"});
                intervals[i].setName("INTERVAL "+juce::String(i+1));intervals[i].setComponentID(prefix+"interval");owner.addAndMakeVisible(intervals[i]);
                intervals[i].onChange=[this,i,prefix]{if(!syncing&&intervals[i].getSelectedId()>0)p.setValue(prefix+"interval",float(intervals[i].getSelectedId()-20));};
                addDial(prefix+"level","LEVEL"," DB",juce::Slider::LinearVertical);addDial(prefix+"pan","PAN","",juce::Slider::Rotary);
            }
        }else if(p.reference){
            addToggle("reference","MIX / REF");addToggle("match","MATCH");addToggle("mono","MONO");addToggle("follow","FOLLOW");
            addCombo("fileSlot","FILE",{"SLOT 1","SLOT 2","SLOT 3"});addCombo("channelView","CHANNEL",{"STEREO","MID","SIDE"});addCombo("listenBand","LISTEN",{"FULL","VOICE","LOW","AIR"});addDial("matchTrim","MATCH TRIM"," DB",juce::Slider::LinearHorizontal);
            load.setName("LOAD");clear.setName("CLEAR");owner.addAndMakeVisible(load);owner.addAndMakeVisible(clear);
            load.onClick=[this]{chooser=std::make_unique<juce::FileChooser>("REFERENZDATEI",juce::File(),"*.wav;*.aif;*.aiff;*.flac");const juce::Component::SafePointer<GillToolsEditor> safe(&owner);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& selected){if(!safe)return;const auto file=selected.getResult();if(file.existsAsFile())safe->impl->p.loadReference(int(safe->impl->p.value("fileSlot")),file);});};
            clear.onClick=[this]{p.setValue("reference",0);p.reference->clearSlot(int(p.value("fileSlot")));};
            for(auto* slider:{&loopStart,&loopEnd}){slider->setSliderStyle(juce::Slider::LinearHorizontal);slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,66,20);slider->setRange(0,600,.01);slider->setTextValueSuffix(" S");owner.addAndMakeVisible(slider);}
            loopStart.setName("LOOP START");loopEnd.setName("LOOP END");loopStart.onValueChange=loopEnd.onValueChange=[this]{if(!syncing){p.setValue("reference",0,false);p.reference->setLoop(loopStart.getValue(),loopEnd.getValue());}};
            toggles[0]->setTooltip("REF IST EIN ABHOERSIGNAL. VOR EINEM ECHTZEIT-EXPORT AUF MIX ZURUECKSCHALTEN.");
            toggles[1]->setTooltip("FRIERT DREI AKTIVE SEKUNDEN GEWICHTETEN RMS EIN. DIE LAUTERE SEITE WIRD ABGESENKT. ZUM NEULERNEN MATCH AUS- UND EINSCHALTEN.");
        }else{
            addDial("repair","REPAIR"," %");addDial("output","OUTPUT"," DB");addDial("clipDb","CLIP +"," DB");addDial("negativeClipDb","CLIP -"," DB");addDial("maxRepair","MAX REPAIR"," DB");addToggle("delta","LISTEN REPAIRS");
            learn.setName("LEARN LEVEL");learn.setComponentID("learn");owner.addAndMakeVisible(learn);learn.onClick=[this]{p.rescue->requestLearn();};
            learn.setTooltip("DREI SEKUNDEN ABSPIELEN. NUR WIEDERHOLTE HARTE CLIP-PLATEAUS ERGEBEN EINE NEUE POSITIVE/NEGATIVE CLIPGRENZE.");
            dials[0]->slider.setTooltip("VORSICHTIGE SCHAETZUNG KURZER ABGESCHNITTENER SPITZEN. VERLORENE ORIGINALDETAILS SIND NICHT SICHER REKONSTRUIERBAR.");
        }
        timerCallback();startTimerHz(24);
    }
    ~Impl()override{stopTimer();chooser.reset();owner.setLookAndFeel(nullptr);}
    void addDial(const juce::String& id,const juce::String& label,const juce::String& suffix,juce::Slider::SliderStyle style=juce::Slider::Rotary){auto dial=std::make_unique<Dial>(p,id,label,suffix,style);owner.addAndMakeVisible(dial->label);owner.addAndMakeVisible(dial->slider);dials.push_back(std::move(dial));}
    void addCombo(const juce::String& id,const juce::String& name,juce::StringArray labels){auto combo=std::make_unique<juce::ComboBox>();combo->setName(name);combo->setComponentID(id);combo->addItemList(labels,1);owner.addAndMakeVisible(*combo);comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,id,*combo));combos.push_back(std::move(combo));}
    void addToggle(const juce::String& id,const juce::String& title){auto button=std::make_unique<juce::TextButton>(title);button->setName(title);button->setComponentID(id);button->setClickingTogglesState(true);owner.addAndMakeVisible(*button);buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,id,*button));toggles.push_back(std::move(button));}
    void dialBounds(int index,int x,int y,int w,int h){dials[index]->label.setBounds(x,y,w,18);dials[index]->slider.setBounds(x,y+18,w,h-18);}
    void resized(){
        quality.setBounds(width-199,17,116,30);bypass.setBounds(width-77,17,62,30);
        previous.setBounds(18,height-39,29,25);preset.setBounds(51,height-39,width-102,25);next.setBounds(width-47,height-39,29,25);status.setBounds(24,height-64,width-48,20);
        if(p.harmony){
            combos[0]->setBounds(419,111,99,27);combos[1]->setBounds(419,143,99,28);toggles[0]->setBounds(419,187,99,30);
            dialBounds(0,419,224,99,96);dialBounds(1,419,323,99,55);
            for(int i=0;i<3;++i){const int x=22+i*130;toggles[i+1]->setBounds(x+8,97,102,26);combos[i+2]->setBounds(x+8,128,102,25);intervals[i].setBounds(x+8,158,102,26);dialBounds(2+i*2,x+8,190,102,113);dialBounds(3+i*2,x+24,306,70,65);}
        }else if(p.reference){
            toggles[0]->setBounds(267,105,126,55);toggles[1]->setBounds(278,172,104,31);toggles[2]->setBounds(278,210,104,31);toggles[3]->setBounds(278,248,104,29);
            combos[0]->setBounds(24,297,91,27);load.setBounds(121,297,65,27);clear.setBounds(192,297,65,27);combos[1]->setBounds(404,297,101,27);combos[2]->setBounds(511,297,123,27);
            dialBounds(0,267,278,126,49);loopStart.setBounds(25,330,239,22);loopEnd.setBounds(396,330,239,22);
        }else{
            dialBounds(0,18,204,147,121);dialBounds(1,172,214,77,109);dialBounds(2,258,214,78,109);dialBounds(3,340,214,78,109);dialBounds(4,423,214,97,109);
            learn.setBounds(353,84,160,30);toggles[0]->setBounds(353,121,160,30);
        }
    }
    void text(juce::Graphics& g,const juce::String& value,juce::Rectangle<float> box,float size=11,bool bold=false,juce::Colour colour=cream){g.setColour(colour);g.setFont(font(size,bold));g.drawText(value,box,juce::Justification::centred);}
    void meter(juce::Graphics& g,juce::Rectangle<float> box,float value,const juce::String& title){
        gill::material::panel(g,box,dark,17,true);text(g,title,box.withHeight(29),15,true);const double db=20*std::log10(std::max(1e-7f,value));
        const auto rail=box.reduced(23).withTrimmedTop(19).withTrimmedBottom(24);const float amount=float(std::clamp((db+60)/60,0.,1.));
        for(int i=0;i<20;++i){const float y=rail.getBottom()-(i+1)*rail.getHeight()/20;g.setColour(i/20.f<amount?sage.brighter(.25f):sage.withAlpha(.13f));g.fillRoundedRectangle(rail.getX(),y,rail.getWidth(),rail.getHeight()/20-2,2);}
        text(g,value>1e-6f?juce::String(db,1)+" DBFS":"-INF",box.withY(box.getBottom()-28).withHeight(23),13,true);
    }
    void paint(juce::Graphics& g){
        const auto bounds=owner.getLocalBounds().toFloat();const juce::Colour tint=p.harmony?juce::Colour(0xffd2b27c):p.reference?juce::Colour(0xff3f2c20):juce::Colour(0xff685645);
        g.fillAll(tint);if(wood.isValid()){g.drawImage(wood,0,0,width,height,950,285,170,805);g.setColour(tint.withAlpha(p.harmony?.35f:.68f));g.fillAll();g.setOpacity(1);g.drawImage(wood,20,18,40,28,224,146,174,122);}
        g.setColour(cream.withAlpha(.6f));g.drawRoundedRectangle(bounds.reduced(3),p.rescue?26.f:16.f,2);g.setColour(juce::Colours::black.withAlpha(.4f));g.drawRoundedRectangle(bounds.reduced(7),p.rescue?23.f:12.f,1);
        text(g,p.getName(),{74,17,float(width-282),30},21,false,p.harmony?ink:cream);
        if(p.harmony){
            for(int i=0;i<3;++i)gill::material::panel(g,{float(22+i*130),88,118,287},cream,14);
            gill::material::panel(g,{412,88,112,287},cream,14);text(g,"KEY / SCALE",{420,88,97,20},10,true,ink);
            const float hz=p.harmony->detectedHz();text(g,hz>0?juce::String(hz,1)+" HZ  /  "+juce::String(p.harmony->confidence()*100,0)+" %":"PLAY ONE VOICE",{24,61,370,20},11,true,ink);
        }else if(p.reference){
            meter(g,{22,87,231,198},referenceView.rmsMix*std::pow(10.f,referenceView.mixGainDb/20.f),"MIX / LEARNED LEVEL");meter(g,{407,87,231,198},referenceView.rmsRef*std::pow(10.f,referenceView.matchGainDb/20.f),"REF / LEARNED LEVEL");
            text(g,referenceView.fileName.isEmpty()?"LOAD YOUR REFERENCE":referenceView.fileName,{24,61,612,21},11,true);
            const bool mixReduced=referenceView.mixGainDb<-.01f;
            text(g,referenceView.loudnessValid?juce::String(mixReduced?"MIX ":"REF ")+juce::String(mixReduced?referenceView.mixGainDb:referenceView.matchGainDb,1)+" DB":"MEASURING",{264,72,132,23},10,true);
        }else{
            gill::material::panel(g,{20,81,321,113},dark,20,true);text(g,"REPAIR SHAPE / BEFORE OUTPUT TRIM",{28,85,304,21},10,true);
            const juce::Rectangle<float> area(29,114,301,69);g.setColour(cream.withAlpha(.2f));g.drawHorizontalLine(int(area.getCentreY()),area.getX(),area.getRight());
            for(int pass=0;pass<2;++pass){juce::Path line;const auto& values=pass==0?rescueView.before:rescueView.after;for(size_t i=0;i<values.size();++i){const float x=area.getX()+float(i)*area.getWidth()/float(values.size()-1),y=area.getCentreY()-std::clamp(values[i],-1.5f,1.5f)*area.getHeight()*.3f;if(i==0)line.startNewSubPath(x,y);else line.lineTo(x,y);}g.setColour(pass==0?cream.withAlpha(.55f):amber);g.strokePath(line,juce::PathStrokeType(pass==0?1.f:1.3f));}
            text(g,juce::String(p.rescue->repairs())+" REPAIRS",{350,161,165,25},12,true);
            gill::material::panel(g,{17,205,506,121},cream,15);
        }
    }
    void timerCallback()override{
        syncing=true;preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);
        if(p.harmony){const int scale=int(p.value("scale"));
            for(int i=0;i<3;++i){const juce::String prefix="voice"+juce::String(i);const int mode=int(p.value(prefix+"mode")),limit=mode?12:scale==3?5:7;
                const int wanted=std::clamp(int(p.value(prefix+"interval")),-limit,limit),tag=mode*100+scale;
                if(int(intervals[i].getProperties().getWithDefault("itemsTag",-1))!=tag){intervals[i].clear(juce::dontSendNotification);for(int value=-limit;value<=limit;++value){juce::String label;if(mode)label=juce::String(value)+" SEMITONES";else if(value==0)label="UNISON";else if(std::abs(value)==limit)label=value>0?"OCTAVE UP":"OCTAVE DOWN";else if(scale==3)label=juce::String(value)+" SCALE NOTES";else{const char* ordinal[]{"UNISON","SECOND","THIRD","FOURTH","FIFTH","SIXTH","SEVENTH","OCTAVE"};label=juce::String(ordinal[std::abs(value)])+(value>0?" UP":" DOWN");}intervals[i].addItem(label,value+20);}intervals[i].getProperties().set("itemsTag",tag);}
                intervals[i].setSelectedId(wanted+20,juce::dontSendNotification);
            }
            status.setText("ONE VOICE  /  "+juce::String(1000.*p.getLatencySamples()/p.rateView.load(),1)+" MS",juce::dontSendNotification);
        }else if(p.reference){referenceView=p.reference->snapshot();
            toggles[0]->setButtonText(referenceView.referenceActive?"REF PLAYING":p.value("reference")>.5f?"REF ARMED":"MIX / REF");
            status.setText(referenceView.status+"  /  "+juce::String(referenceView.positionSeconds,1)+" S  /  "+juce::String(referenceView.durationSeconds,1)+" S",juce::dontSendNotification);
            const double maximum=std::max(.01,referenceView.durationSeconds);loopStart.setRange(0,maximum,.01);loopEnd.setRange(0,maximum,.01);
            if(!loopStart.isMouseButtonDown())loopStart.setValue(referenceView.loopStartSeconds,juce::dontSendNotification);if(!loopEnd.isMouseButtonDown())loopEnd.setValue(referenceView.loopEndSeconds,juce::dontSendNotification);
            loopStart.setEnabled(referenceView.loaded);loopEnd.setEnabled(referenceView.loaded);
        }else{rescueView=p.rescue->scope();juce::String label=p.rescue->isLearning()?"LEARNING / PLAY YOUR TAKE":p.rescue->learnRevision()>0&&p.rescue->learnedPositiveDb()<-24&&p.rescue->learnedNegativeDb()<-24?"NO CLIP FOUND":"READY";
            status.setText(label+"  /  "+juce::String(p.rescue->rejected())+" SKIPPED  /  "+juce::String(1000.*p.getLatencySamples()/p.rateView.load(),1)+" MS",juce::dontSendNotification);learn.setEnabled(!p.rescue->isLearning());
        }
        syncing=false;owner.repaint();
    }
};
GillToolsEditor::GillToolsEditor(GillToolsProcessor& processor):AudioProcessorEditor(processor),impl(std::make_unique<Impl>(*this,processor)){setResizable(false,false);setSize(impl->width,impl->height);}
GillToolsEditor::~GillToolsEditor()=default;
void GillToolsEditor::paint(juce::Graphics& graphics){impl->paint(graphics);}
void GillToolsEditor::resized(){if(impl)impl->resized();}
