#include "PluginEditor.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"

namespace{
const juce::Colour ivory(0xffeee7d7),ink(0xff222925),green(0xff397368),amber(0xffd6b47d);
void label(juce::Graphics&g,const juce::String&t,juce::Rectangle<int>r,float size,juce::Colour c=ink,int alignment=juce::Justification::centredLeft){g.setColour(c);g.setFont(juce::FontOptions(size));g.drawFittedText(t,r,alignment,1);}
struct Look:juce::LookAndFeel_V4{
    Look(){setColour(juce::Slider::textBoxTextColourId,ink);setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xffe3ddcd));setColour(juce::ComboBox::backgroundColourId,ivory);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::outlineColourId,juce::Colour(0xffbfb29b));setColour(juce::PopupMenu::backgroundColourId,ivory);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,green);setColour(juce::PopupMenu::highlightedTextColourId,ivory);}
    void drawButtonBackground(juce::Graphics&g,juce::Button&b,const juce::Colour&,bool over,bool down)override{auto c=b.getToggleState()?green:ivory;if(over)c=c.brighter(.06f);gill::material::panel(g,b.getLocalBounds().toFloat().reduced(1),c,5,down);}
    void drawButtonText(juce::Graphics&g,juce::TextButton&b,bool,bool)override{label(g,b.getButtonText(),b.getLocalBounds().reduced(4),11,b.getToggleState()?ivory:ink,juce::Justification::centred);}
    juce::Label*createSliderTextBox(juce::Slider&s)override{auto*l=juce::LookAndFeel_V4::createSliderTextBox(s);l->setColour(juce::Label::textColourId,ink);l->setColour(juce::Label::backgroundColourId,juce::Colour(0xffe3ddcd));l->setColour(juce::Label::outlineColourId,juce::Colour(0xffbfb5a2));l->setColour(juce::TextEditor::textColourId,ink);l->setColour(juce::TextEditor::backgroundColourId,ivory);return l;}
    void drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float position,float,float,juce::Slider::SliderStyle,juce::Slider&)override{float cy=y+h*.5f;g.setColour(juce::Colour(0xffb4aa94));g.fillRoundedRectangle(float(x),cy-2,float(w),4,2);g.setColour(green);g.fillRoundedRectangle(float(x),cy-1,std::max(0.f,position-x),2,1);gill::material::disc(g,{position-7,cy-7,14,14});}
};
struct Control:juce::Component{
    juce::String name;juce::Slider slider;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>attachment;
    Control(GillAssistProcessor&p,const char*id,const char*text,const char*suffix):name(text){slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xffe3ddcd));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xffbfb5a2));slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,57,18);slider.setTextValueSuffix(suffix);slider.setName(text);slider.onDragStart=[&p]{p.undoManager.beginNewTransaction("Control");};addAndMakeVisible(slider);attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,id,slider);}
    void resized()override{slider.setBounds(getLocalBounds().withTrimmedLeft(58));}
    void paint(juce::Graphics&g)override{label(g,name,{0,0,56,getHeight()},10);}
};
struct DragButton:juce::TextButton{
    std::function<void()>drag;bool started=false;DragButton():TextButton("WAV INS PROJEKT ZIEHEN"){}
    void mouseDown(const juce::MouseEvent&e)override{started=false;TextButton::mouseDown(e);}
    void mouseDrag(const juce::MouseEvent&e)override{if(!started&&e.getDistanceFromDragStart()>5){started=true;if(drag)drag();}TextButton::mouseDrag(e);}
};
struct Timeline:juce::Component,juce::SettableTooltipClient{
    GillAssistProcessor&p;gill::assist::Plan snapshot;double zoom=0,scroll=0;int editType=0;juce::Point<float>down;bool dragging=false;
    explicit Timeline(GillAssistProcessor&processor):p(processor){setTooltipText();}
    void setTooltipText(){setName("Waveform and gain curve. Drag to change local gain; choose a region type to mark a range. Shift-wheel zooms.");}
    double span()const{return std::max(.1,snapshot.duration/std::pow(2.,zoom));}double left()const{return std::max(0.,snapshot.duration-span())*scroll;}
    double seconds(float x)const{return juce::jlimit(0.,snapshot.duration,left()+juce::jlimit(0.,1.,double(x)/std::max(1,getWidth()))*span());}
    float x(double t)const{return float((t-left())/span()*getWidth());}
    float y(float db)const{return getHeight()*.58f-db*getHeight()/72.f;}
    void paint(juce::Graphics&g)override{
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff25372f),0,0,juce::Colour(0xff101c16),0,float(getHeight()),false));g.fillRoundedRectangle(getLocalBounds().toFloat(),7);
        if(snapshot.waveform.empty()){label(g,"LEARN -> SONG ABSPIELEN -> STOP",getLocalBounds().reduced(20),17,ivory,juce::Justification::centred);return;}
        const double step=span()>60?15:span()>20?5:span()>5?1:.25;
        for(double time=std::floor(left()/step)*step;time<left()+span();time+=step){float xx=x(time);g.setColour(ivory.withAlpha(.09f));g.drawVerticalLine(int(xx),19.f,float(getHeight()));label(g,juce::String(time,step<1?2:0)+" s",{int(xx)+4,2,60,15},9,ivory.withAlpha(.75f));}
        for(const auto&r:snapshot.regions){if(r.end<left()||r.begin>left()+span())continue;auto colour=r.type==4?juce::Colour(0xff99a9dc):r.type==3?amber:juce::Colour(0xff859084);g.setColour(colour.withAlpha(.08f+.14f*r.confidence));g.fillRect(juce::Rectangle<float>(x(r.begin),20,std::max(1.f,x(r.end)-x(r.begin)),float(getHeight()-20)));}
        g.setColour(ivory.withAlpha(.35f));for(int pixel=0;pixel<getWidth();++pixel){int a=std::clamp(int(seconds(float(pixel))/gill::assist::hopSeconds),0,int(snapshot.waveform.size())-1),b=std::clamp(int(seconds(float(pixel+1))/gill::assist::hopSeconds)+1,a+1,int(snapshot.waveform.size()));float peak=0;for(int j=a;j<b;++j)peak=std::max(peak,snapshot.waveform[std::size_t(j)]);float h=std::min(.95f,peak*1.8f)*(getHeight()-38)*.5f;g.drawVerticalLine(pixel,getHeight()*.55f-h,getHeight()*.55f+h);}
        for(int d:{-24,-12,0,12}){g.setColour(ivory.withAlpha(d==0?.2f:.07f));g.drawHorizontalLine(int(y(float(d))),0,float(getWidth()));label(g,juce::String(d)+" dB",{getWidth()-46,int(y(float(d)))-14,43,13},9,ivory.withAlpha(.7f));}
        juce::Path curve;for(int px=0;px<getWidth();++px){int i=std::clamp(int(seconds(float(px))/gill::assist::hopSeconds),0,int(snapshot.gainDb.size())-1);float yy=y(snapshot.gainDb[std::size_t(i)]);if(px==0)curve.startNewSubPath(0,yy);else curve.lineTo(float(px),yy);}g.setColour(juce::Colour(0xffa3dfbf));g.strokePath(curve,juce::PathStrokeType(1.7f));
        const double play=p.engine.positionView.load()-snapshot.startSeconds;if(play>=left()&&play<=left()+span()){g.setColour(amber);g.drawVerticalLine(int(x(play)),18,float(getHeight()));}
        if(dragging){g.setColour(amber.withAlpha(.16f));float end=float(getMouseXYRelative().x);g.fillRect(juce::Rectangle<float>(std::min(down.x,end),20,std::max(1.f,std::abs(end-down.x)),float(getHeight()-20)));}
    }
    void mouseDown(const juce::MouseEvent&e)override{if(snapshot.gainDb.empty())return;down=e.position;dragging=true;}
    void mouseMove(const juce::MouseEvent&e)override{const auto time=seconds(e.position.x);for(const auto&r:snapshot.regions)if(time>=r.begin&&time<=r.end){const char*name=r.type==4?"SIBILANCE":r.type==3?"BREATH":"GATE";setTooltip(juce::String(name)+" candidate / confidence "+juce::String(int(r.confidence*100))+"%. Use PROTECT to preserve this region.");return;}setTooltip("Drag a time interval vertically to adjust its gain. Select PROTECT to keep the original signal locally.");}
    void mouseDrag(const juce::MouseEvent&)override{repaint();}
    void mouseUp(const juce::MouseEvent&e)override{
        if(!dragging)return;dragging=false;double a=seconds(down.x),b=seconds(e.position.x);if(a>b)std::swap(a,b);if(b-a<.06){a=std::max(0.,a-.1);b=std::min(snapshot.duration,a+.2);}
        const float db=juce::jlimit(-24.f,24.f,(down.y-e.position.y)*72.f/getHeight());p.engine.edit({a,b,db,editType});repaint();
    }
    void mouseWheelMove(const juce::MouseEvent&e,const juce::MouseWheelDetails&w)override{if(e.mods.isShiftDown())zoom=juce::jlimit(0.,6.,zoom+w.deltaY*3);else scroll=juce::jlimit(0.,1.,scroll-w.deltaY*.2);repaint();}
};
}
struct GillAssistEditor::Impl:juce::Component,private juce::Timer{
    GillAssistEditor&owner;GillAssistProcessor&p;Look look;gill::QualitySelector quality;Timeline timeline;
    juce::TextButton learn{"LEARN"},stop{"STOP"},import{"IMPORT"},undo{"UNDO"},redo{"REDO"},ab{"A / B"},clear{"RESET EDITS"},bypass{"BYPASS"},save{"EXPORT"};DragButton drag;
    std::array<juce::TextButton,4>sections;std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,4>toggles;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>bypassAttach;
    std::vector<std::unique_ptr<Control>>controls;juce::ComboBox preset,editMode;juce::Slider zoom,scroll;
    std::unique_ptr<juce::FileChooser>chooser;juce::TooltipWindow tips{&owner,600};std::uint64_t revision=~std::uint64_t(0);juce::String message;bool sideB=false;
    Impl(GillAssistEditor&o,GillAssistProcessor&processor):owner(o),p(processor),quality(p.state,p),timeline(p){
        setLookAndFeel(&look);addAndMakeVisible(quality);addAndMakeVisible(timeline);
        for(auto*b:{&learn,&stop,&import,&undo,&redo,&ab,&clear,&bypass,&save})addAndMakeVisible(*b);addAndMakeVisible(drag);
        learn.onClick=[this]{p.engine.arm();};stop.onClick=[this]{p.engine.stop();};import.onClick=[this]{chooseImport();};undo.onClick=[this]{if(p.engine.canUndo())p.engine.undo();else p.undoManager.undo();};redo.onClick=[this]{if(p.engine.canRedo())p.engine.redo();else p.undoManager.redo();};ab.onClick=[this]{p.swapAB();sideB=!sideB;ab.setButtonText(sideB?"B / A":"A / B");};clear.onClick=[this]{p.engine.clearEdits();};
        bypass.setClickingTogglesState(true);bypassAttach=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,"bypass",bypass);
        drag.drag=[this]{auto file=p.engine.claimExportFile();if(file.existsAsFile())juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,&drag);};save.onClick=[this]{chooseExport();};
        const char*sectionNames[]{"RIDE","GATE","BREATH","SIBILANCE"},*ids[]{"rideOn","gateOn","breathOn","sibilanceOn"};for(int i=0;i<4;++i){sections[std::size_t(i)].setButtonText(sectionNames[i]);sections[std::size_t(i)].setClickingTogglesState(true);addAndMakeVisible(sections[std::size_t(i)]);toggles[std::size_t(i)]=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,ids[i],sections[std::size_t(i)]);}
        auto control=[&](const char*id,const char*name,const char*unit){auto c=std::make_unique<Control>(p,id,name,unit);addAndMakeVisible(*c);controls.push_back(std::move(c));};control("target","RMS"," dB");control("range","RANGE"," dB");control("speed","SPEED"," ms");control("amount","AMOUNT"," %");control("gate","FLOOR"," dB");control("breath","REDUCE"," dB");control("sibilance","REDUCE"," dB");control("output","OUTPUT"," dB");
        for(int i=0;i<6;++i)preset.addItem(p.getProgramName(i),i+1);preset.onChange=[this]{p.setCurrentProgram(preset.getSelectedId()-1);};addAndMakeVisible(preset);
        editMode.addItemList({"GAIN EDIT","PROTECT","GATE REGION","BREATH REGION","SIBILANCE REGION"},1);editMode.setSelectedId(1);editMode.onChange=[this]{timeline.editType=editMode.getSelectedId()-1;};addAndMakeVisible(editMode);
        zoom.setRange(0,6,.01);zoom.setSliderStyle(juce::Slider::LinearHorizontal);zoom.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);zoom.onValueChange=[this]{timeline.zoom=zoom.getValue();timeline.repaint();};zoom.setTooltip("Zoom into the transferred song (up to 64x)");addAndMakeVisible(zoom);
        scroll.setRange(0,1,.001);scroll.setSliderStyle(juce::Slider::LinearHorizontal);scroll.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);scroll.onValueChange=[this]{timeline.scroll=scroll.getValue();timeline.repaint();};scroll.setTooltip("Timeline position");addAndMakeVisible(scroll);
        learn.setTooltip("Arm transfer, then play your entire vocal track. Stop playback or press STOP to analyse. Maximum 5 minutes. Insert first on the vocal track.");
        drag.setTooltip("Drag the rendered 24-bit WAV into the Playlist. Place it at the displayed transfer start. Bypass GILLASSIST on the rendered audio to avoid double processing.");
        sections[2].setTooltip("Conservative acoustic breath candidates, not AI classification. Inspect highlighted regions and use PROTECT for mistakes.");sections[3].setTooltip("Broadband clip gain on acoustic sibilance candidates. Inspect regions and use PROTECT for false detections.");
        setSize(820,520);startTimerHz(15);refresh();resized();
    }
    ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
    void resized()override{
        quality.setBounds(558,21,110,27);bypass.setBounds(685,21,110,27);
        learn.setBounds(24,65,82,29);stop.setBounds(111,65,55,29);import.setBounds(171,65,65,29);undo.setBounds(246,65,55,29);redo.setBounds(305,65,55,29);ab.setBounds(366,65,57,29);clear.setBounds(429,65,96,29);editMode.setBounds(536,65,158,29);zoom.setBounds(728,65,66,29);
        timeline.setBounds(24,106,771,190);scroll.setBounds(24,296,771,14);
        for(int i=0;i<4;++i)sections[std::size_t(i)].setBounds(31+i*194,321,175,25);
        for(int i=0;i<4;++i)controls[std::size_t(i)]->setBounds(32,350+i*22,172,22);
        for(int i=4;i<7;++i)controls[std::size_t(i)]->setBounds(32+(i-3)*194,355,172,26);
        preset.setBounds(24,457,222,28);controls[7]->setBounds(268,457,204,28);save.setBounds(491,457,74,28);drag.setBounds(580,457,215,28);
    }
    void paint(juce::Graphics&g)override{
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff7f5733),0,0,juce::Colour(0xff312416),820,520,false));g.fillAll(juce::Colour(0xff4d3520));
        for(int y=0;y<520;y+=3){g.setColour(juce::Colour(0xffbc9866).withAlpha(.09f+float(std::sin(y*.79))*.035f));g.drawLine(0,float(y),820,float(y+2),.7f);}
        gill::material::panel(g,{12,12,796,496},ivory,9);gill::material::panel(g,{17,58,786,251},juce::Colour(0xffc7c0b1),7,true);
        juce::Path monogram;monogram.startNewSubPath(28,5);monogram.lineTo(15,5);monogram.cubicTo(-3,5,-3,30,15,30);monogram.lineTo(29,30);monogram.lineTo(29,19);monogram.lineTo(17,19);monogram.startNewSubPath(24,39);monogram.lineTo(24,13);monogram.lineTo(40,13);monogram.cubicTo(55,13,55,31,40,31);monogram.lineTo(24,31);monogram.applyTransform(juce::AffineTransform::scale(.7f).translated(27,19));g.setColour(juce::Colour(0xff493322));g.strokePath(monogram,juce::PathStrokeType(3.7f));g.setColour(juce::Colour(0xffa3865e));g.strokePath(monogram,juce::PathStrokeType(1.7f));label(g,"GILLASSIST",{87,18,240,31},24);label(g,"TRANSFER VOCAL EDITOR",{89,45,242,12},9,juce::Colour(0xff6c746e));label(g,"ZOOM",{698,69,36,20},8);
        for(int i=0;i<4;++i)gill::material::panel(g,{float(24+i*194),315,189,128},juce::Colour(0xffe8e0d0),6);
        label(g,"Pause cleanup",{230,392,157,15},11);label(g,"30 ms pre-roll / soft tails",{230,412,160,15},9,juce::Colour(0xff69736a));
        label(g,"Breath candidates",{425,392,160,15},11);label(g,"Inspect / protect regions",{425,412,160,15},9,juce::Colour(0xff69736a));
        label(g,"S / SH candidates",{619,392,159,15},11);label(g,"Confidence-based reduction",{619,412,164,15},9,juce::Colour(0xff69736a));
        label(g,message,{24,491,772,17},10,p.engine.state==gill::assist::Engine::Error?juce::Colours::darkred:juce::Colour(0xff58675c));
        label(g,"0 SAMPLES",{432,23,104,24},10,green,juce::Justification::centredRight);
    }
    void refresh(){
        const auto current=p.engine.completedRevision.load();if(current!=revision){revision=current;timeline.snapshot=p.engine.plan();}
        const int state=p.engine.state.load();if(state==gill::assist::Engine::Capturing)message="LEARN / "+juce::String(p.engine.capturedSeconds.load(),1)+" / 300 s / Beim Songende STOP druecken.";else if(state==gill::assist::Engine::Analysing)message="ANALYSE / RENDER - vollstaendiger Transfer wird verarbeitet.";else message=p.engine.status();
        if(state==gill::assist::Engine::Ready&&p.playingView&&!p.engine.timelineMatched)message="Ausserhalb des gelernten Songbereichs - unveraendertes Originalsignal.";
        learn.setToggleState(state==gill::assist::Engine::Armed||state==gill::assist::Engine::Capturing,juce::dontSendNotification);learn.setEnabled(state!=gill::assist::Engine::Analysing);import.setEnabled(state!=gill::assist::Engine::Capturing&&state!=gill::assist::Engine::Armed&&state!=gill::assist::Engine::Analysing);
        const bool ready=state==gill::assist::Engine::Ready&&p.engine.exportFile().existsAsFile();drag.setEnabled(ready);save.setEnabled(ready);undo.setEnabled(p.engine.canUndo()||p.undoManager.canUndo());redo.setEnabled(p.engine.canRedo()||p.undoManager.canRedo());preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);timeline.repaint();repaint();
    }
    void timerCallback()override{refresh();}
    void chooseImport(){chooser=std::make_unique<juce::FileChooser>("Vocal importieren",juce::File(),"*.wav;*.aif;*.aiff;*.flac");auto safe=juce::Component::SafePointer<Impl>(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&dialog){if(safe){auto file=dialog.getResult();if(file.existsAsFile())safe->p.engine.importFile(file,safe->p.engine.positionView.load());}});}
    void chooseExport(){auto source=p.engine.claimExportFile();if(!source.existsAsFile())return;chooser=std::make_unique<juce::FileChooser>("Bearbeitete Vocal exportieren",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GILLASSIST-VOCAL.wav"),"*.wav");auto safe=juce::Component::SafePointer<Impl>(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,source](const juce::FileChooser&dialog){if(safe){auto destination=dialog.getResult();if(destination!=juce::File()&&!source.copyFileTo(destination.withFileExtension("wav")))juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Export fehlgeschlagen","Zielordner oder freien Speicher pruefen.");}});}
};
GillAssistEditor::GillAssistEditor(GillAssistProcessor&p):AudioProcessorEditor(p),impl(std::make_unique<Impl>(*this,p)){addAndMakeVisible(*impl);setResizable(true,true);getConstrainer()->setFixedAspectRatio(820./520.);setResizeLimits(820,520,1230,780);setSize(820,520);}
GillAssistEditor::~GillAssistEditor()=default;
void GillAssistEditor::resized(){if(impl){impl->setTransform(juce::AffineTransform::scale(getWidth()/820.f));impl->setBounds(0,0,820,520);}}
