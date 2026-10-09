#include "PluginEditor.h"
#include "../../GILLCommon/TwinLensUi.h"
#include "../../GILLCommon/PrismUi.h"
#include "../../GILLCommon/MaterialUi.h"
#include "../../GILLCommon/QualityUi.h"

namespace{
const juce::Colour ivory(0xffe5ded3),ink(0xff39363d),green(0xff9d81d2),amber(0xffb49af9);
void drawAssistText(juce::Graphics&g,const juce::String&t,juce::Rectangle<int>r,float size,juce::Colour c=ink,int alignment=juce::Justification::centredLeft){g.setColour(c);g.setFont(juce::FontOptions(size));g.drawFittedText(t,r,alignment,1);}
class Look final : public gill::twin::Look {  };
struct Control:juce::Component{
    juce::String name;juce::Slider slider;std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>attachment;
    Control(GillAssistProcessor&p,const char*id,const char*text,const char*suffix):name(text){slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xffc1d0df));slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff8daac2));slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,57,18);slider.setTextValueSuffix(suffix);slider.setName(text);slider.onDragStart=[&p]{p.undoManager.beginNewTransaction("Control");};addAndMakeVisible(slider);attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,id,slider);}
    void resized()override{slider.setBounds(getLocalBounds().withTrimmedLeft(58));}
    void paint(juce::Graphics&g)override{drawAssistText(g,name,{0,0,56,getHeight()},10);}
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
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2d2a35),0,0,juce::Colour(0xff101116),0,float(getHeight()),false));g.fillRoundedRectangle(getLocalBounds().toFloat(),7);
        if(snapshot.waveform.empty()){drawAssistText(g,"LEARN -> SONG ABSPIELEN -> STOP",getLocalBounds().reduced(20),17,ivory,juce::Justification::centred);return;}
        const double step=span()>60?15:span()>20?5:span()>5?1:.25;
        for(double time=std::floor(left()/step)*step;time<left()+span();time+=step){float xx=x(time);g.setColour(ivory.withAlpha(.09f));g.drawVerticalLine(int(xx),19.f,float(getHeight()));drawAssistText(g,juce::String(time,step<1?2:0)+" s",{int(xx)+4,2,60,15},9,ivory.withAlpha(.75f));}
        for(const auto&r:snapshot.regions){if(r.end<left()||r.begin>left()+span())continue;auto colour=r.type==4?juce::Colour(0xff99a9dc):r.type==3?amber:juce::Colour(0xff859084);g.setColour(colour.withAlpha(.08f+.14f*r.confidence));g.fillRect(juce::Rectangle<float>(x(r.begin),20,std::max(1.f,x(r.end)-x(r.begin)),float(getHeight()-20)));}
        g.setColour(ivory.withAlpha(.35f));for(int pixel=0;pixel<getWidth();++pixel){int a=std::clamp(int(seconds(float(pixel))/gill::assist::hopSeconds),0,int(snapshot.waveform.size())-1),b=std::clamp(int(seconds(float(pixel+1))/gill::assist::hopSeconds)+1,a+1,int(snapshot.waveform.size()));float peak=0;for(int j=a;j<b;++j)peak=std::max(peak,snapshot.waveform[std::size_t(j)]);float h=std::min(.95f,peak*1.8f)*(getHeight()-38)*.5f;g.drawVerticalLine(pixel,getHeight()*.55f-h,getHeight()*.55f+h);}
        for(int d:{-24,-12,0,12}){g.setColour(ivory.withAlpha(d==0?.2f:.07f));g.drawHorizontalLine(int(y(float(d))),0,float(getWidth()));drawAssistText(g,juce::String(d)+" dB",{getWidth()-46,int(y(float(d)))-14,43,13},9,ivory.withAlpha(.7f));}
        juce::Path curve;for(int px=0;px<getWidth();++px){int i=std::clamp(int(seconds(float(px))/gill::assist::hopSeconds),0,int(snapshot.gainDb.size())-1);float yy=y(snapshot.gainDb[std::size_t(i)]);if(px==0)curve.startNewSubPath(0,yy);else curve.lineTo(float(px),yy);}g.setColour(gill::twin::purple);g.strokePath(curve,juce::PathStrokeType(1.7f));
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
    GillAssistEditor&owner;GillAssistProcessor&p;Look look;gill::QualitySelector quality;Timeline timeline;gill::twin::History before,after;
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
        setSize(780,540);startTimerHz(15);refresh();resized();
    }
    ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
    void resized()override{
        quality.setBounds(544,21,110,27);bypass.setBounds(670,21,87,27);learn.setBounds(343,68,94,35);stop.setBounds(285,110,94,26);import.setBounds(400,110,94,26);
        for(int i=0;i<4;++i)controls[size_t(i)]->setBounds(273,143+i*24,234,23);
        undo.setBounds(273,244,62,25);redo.setBounds(340,244,62,25);ab.setBounds(407,244,49,25);clear.setBounds(462,244,96,25);
        timeline.setBounds(22,282,736,91);scroll.setBounds(22,373,736,12);editMode.setBounds(22,389,187,25);zoom.setBounds(670,389,88,25);
        for(int i=0;i<4;++i)sections[size_t(i)].setBounds(22+i*185,420,175,25);
        for(int i=4;i<7;++i)controls[size_t(i)]->setBounds(22+(i-3)*185,449,175,25);
        controls[7]->setBounds(22,449,175,25);preset.setBounds(22,484,244,27);save.setBounds(281,484,86,27);drag.setBounds(382,484,376,27);
    }
    void paint(juce::Graphics&g)override{
        gill::twin::chassis(g,780,540,"GILLASSIST");gill::twin::levelLens(g,{30,65,210,210},"BEFORE",before,"PEAK DBFS / 6.7 S");gill::twin::levelLens(g,{540,65,210,210},"AFTER",after,"PEAK DBFS / 6.7 S");drawAssistText(g,"EDIT THE CAPTURED SONG",{225,389,270,24},10);drawAssistText(g,"ZOOM",{623,389,45,24},10);drawAssistText(g,message,{22,516,736,17},10,p.engine.state==gill::assist::Engine::Error?juce::Colours::darkred:gill::twin::ink);
    }
    void refresh(){before.gain(p.inputPeak.load());after.gain(p.outputPeak.load());
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
GillAssistEditor::GillAssistEditor(GillAssistProcessor&p):AudioProcessorEditor(p),impl(std::make_unique<Impl>(*this,p)){addAndMakeVisible(*impl);setResizable(true,true);getConstrainer()->setFixedAspectRatio(780./540.);setResizeLimits(780,540,1170,810);setSize(780,540);}
GillAssistEditor::~GillAssistEditor()=default;
void GillAssistEditor::resized(){if(impl){impl->setTransform(juce::AffineTransform::scale(getWidth()/780.f));impl->setBounds(0,0,780,540);}}
