#include "PluginEditor.h"
#include "../../GILLCommon/PrismUi.h"
#include "../../GILLCommon/MaterialUi.h"

namespace {
const juce::Colour cream(0xffe2ecf6),gold(0xff72cde9),ink(0xff102237);
void drawRiseLabel(juce::Graphics&g,juce::String text,juce::Rectangle<int>bounds,float size=11,juce::Colour colour=ink,int align=juce::Justification::centred){g.setFont(juce::FontOptions(size));g.setColour(colour);g.drawFittedText(text,bounds,align,1);}
class RiseLook final : public gill::prism::Look {  };
struct Dial:juce::Component {juce::String title;juce::Slider slider;juce::AudioProcessorValueTreeState::SliderAttachment attachment;
 Dial(GillRiseProcessor&p,const char*id,const char*name,const char*suffix):title(name),attachment(p.apvts,id,slider){slider.setSliderStyle(juce::Slider::Rotary);slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,20);slider.setTextValueSuffix(suffix);slider.setDoubleClickReturnValue(true,p.apvts.getParameter(id)->convertFrom0to1(p.apvts.getParameter(id)->getDefaultValue()));slider.setName(name);addAndMakeVisible(slider);}
 void resized()override{slider.setBounds(getLocalBounds().withTrimmedTop(18));}void paint(juce::Graphics&g)override{drawRiseLabel(g,title,getLocalBounds().withHeight(17),10);}
};
struct Drag:juce::TextButton {std::function<void()>drag;bool fired=false;Drag():juce::TextButton("DRAG WAV"){}void mouseDown(const juce::MouseEvent&e)override{fired=false;juce::TextButton::mouseDown(e);}void mouseDrag(const juce::MouseEvent&e)override{if(!fired&&e.getDistanceFromDragStart()>5){fired=true;if(drag)drag();}}};
}
struct GillRiseEditor::Impl:juce::Component,private juce::Timer {
 GillRiseProcessor&p;RiseLook look;juce::TextButton live{"LIVE"},pro{"PRO"},bypass{"BYPASS"},arm{"ARM / RECAPTURE"},finish{"FINISH"},play{"AUDITION"},save{"SAVE WAV"},normal{"NORMAL"},tremolo{"TREMOLO"};Drag drag;
 juce::ComboBox preset;std::vector<std::unique_ptr<Dial>>dials;juce::Slider trimStart,trimEnd;juce::AudioProcessorValueTreeState::SliderAttachment startAttach,endAttach;juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttach;juce::TooltipWindow tips{this,600};juce::String message;std::array<float,590>wavePeaks{};std::shared_ptr<const gill::rise::Render>waveSource;
 Impl(GillRiseProcessor&processor):p(processor),startAttach(p.apvts,"start",trimStart),endAttach(p.apvts,"end",trimEnd),bypassAttach(p.apvts,"bypass",bypass){setLookAndFeel(&look);
   for(auto*b:{&live,&pro,&bypass,&arm,&finish,&play,&save,&normal,&tremolo})addAndMakeVisible(*b);addAndMakeVisible(drag);addAndMakeVisible(preset);
   for(int i=0;i<6;++i)preset.addItem(p.getProgramName(i),i+1);preset.onChange=[this]{p.setCurrentProgram(preset.getSelectedId()-1);};
   live.onClick=[this]{p.setValue("gillQuality",0);};pro.onClick=[this]{p.setValue("gillQuality",1);};bypass.setClickingTogglesState(true);arm.setName("PRIMARY");arm.onClick=[this]{message.clear();p.arm();};finish.onClick=[this]{p.finishCapture();};play.onClick=[this]{p.audition(!p.previewing);};normal.onClick=[this]{p.setValue("style",0);};tremolo.onClick=[this]{p.setValue("style",1);};save.onClick=[this]{saveFile(false);};drag.drag=[this]{saveFile(true);};
   const char*ids[]{"length","decay","tone","level","rate","depth"};const char*titles[]{"LENGTH","REVERB","TONE","LEVEL","PULSE RATE","PULSE DEPTH"};const char*suffix[]{" s"," s"," Hz"," dB"," Hz"," %"};for(int i=0;i<6;++i){auto d=std::make_unique<Dial>(p,ids[i],titles[i],suffix[i]);addAndMakeVisible(*d);dials.push_back(std::move(d));}
   for(auto*s:{&trimStart,&trimEnd}){s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::TextBoxRight,false,57,20);s->setTextValueSuffix(" %");addAndMakeVisible(*s);}
   trimStart.setTooltip("Trim inside the captured first syllable. The file ends at the selected start, so earlier or later starts change the placement hint.");trimEnd.setTooltip("Set which part of the captured syllable feeds the reverse reverb. Recapture if the wrong syllable was detected.");
   arm.setTooltip("Play from just before the desired syllable. Captures one voice-like onset above the noise floor, with 100 ms preroll. Automatic detection is adjustable, not speech recognition.");finish.setTooltip("Finish capture manually if the syllable has no clear gap.");drag.setTooltip("Drag the actual 24-bit WAV to FL Studio. Align the WAV's END to the original selected syllable. Your host decides placement; negative start positions require room before the song.");play.setTooltip("Preview through the plugin output. Original audio is untouched while capturing. Offline export never includes preview.");live.setTooltip("0 samples added latency. The audio passes through; effect generation is offline.");pro.setTooltip("0 samples added latency. Full-quality offline generation is identical in both modes.");
   setSize(660,465);timerCallback();startTimerHz(20);
 }
 ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
 void saveFile(bool dragging){juce::String error;auto file=p.exportWav(error);if(!file.existsAsFile()){message=error;return;}message="SAVED: "+file.getFileName();if(dragging)juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,this);}
 void timerCallback()override{bool proMode=p.value("gillQuality")>.5f;live.setToggleState(!proMode,juce::dontSendNotification);pro.setToggleState(proMode,juce::dontSendNotification);normal.setToggleState(p.value("style")<.5f,juce::dontSendNotification);tremolo.setToggleState(p.value("style")>.5f,juce::dontSendNotification);auto r=p.result();drag.setEnabled(bool(r)&&!p.rendering);save.setEnabled(bool(r)&&!p.rendering);play.setEnabled(bool(r)&&!p.rendering);play.setButtonText(p.previewing?"STOP PREVIEW":"AUDITION");finish.setEnabled(p.capture.status()==gill::rise::CaptureEngine::Recording);preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);dials[4]->setEnabled(p.value("style")>.5f);dials[5]->setEnabled(p.value("style")>.5f);repaint();}
 void resized()override{live.setBounds(470,18,48,25);pro.setBounds(521,18,45,25);bypass.setBounds(575,18,64,25);arm.setBounds(23,68,155,31);finish.setBounds(185,68,72,31);normal.setBounds(383,69,114,29);tremolo.setBounds(503,69,135,29);trimStart.setBounds(99,226,218,24);trimEnd.setBounds(410,226,226,24);for(int i=0;i<6;++i)dials[i]->setBounds(19+104*i,263,100,123);play.setBounds(23,397,134,31);save.setBounds(165,397,99,31);drag.setBounds(272,397,140,31);preset.setBounds(426,397,211,31);}
 void paint(juce::Graphics&g)override{
    gill::prism::chassis(g,660,465,254);gill::prism::title(g,"GILLRISE",{76,16,290,29},23);
   auto wave=juce::Rectangle<float>(23,112,614,105);g.setColour(juce::Colour(0xff080b09));g.fillRoundedRectangle(wave,6);g.setColour(gold.withAlpha(.45f));g.drawRoundedRectangle(wave,6,.7f);g.setColour(cream.withAlpha(.04f));g.fillRoundedRectangle(wave.reduced(2).withHeight(27),4);
   auto r=p.result();if(r&&!r->left.empty()){
     if(waveSource!=r){for(int x=0;x<590;++x){std::size_t a=std::size_t(x)*r->left.size()/590,b=std::size_t(x+1)*r->left.size()/590;float peak=0;for(std::size_t i=a;i<b;++i)peak=std::max({peak,std::abs(r->left[i]),std::abs(r->right[i])});wavePeaks[x]=std::min(35.f,peak/std::max(.0001f,r->peak)*35);}waveSource=r;}
     const float mid=165;for(int x=0;x<590;++x){g.setColour(gold.withAlpha(.85f));g.drawVerticalLine(35+x,mid-wavePeaks[x],mid+wavePeaks[x]);}
     drawRiseLabel(g,juce::String(r->lengthSeconds,2)+" s",{34,117,150,17},10,cream,juce::Justification::centredLeft);drawRiseLabel(g,"END AT SYLLABLE",{455,194,167,17},9,gold,juce::Justification::centredRight);
   }else drawRiseLabel(g,"PLAY THE FIRST SYLLABLE TO CREATE YOUR RISE",wave.toNearestInt(),12,gold);
   drawRiseLabel(g,"START",{24,226,65,24},10,cream);drawRiseLabel(g,"END",{345,226,58,24},10,cream);drawRiseLabel(g,p.statusText(),{270,102,362,11},8,gold,juce::Justification::centredRight);
   juce::String hint="ALIGN WAV END TO THE SELECTED SYLLABLE  /  0 SAMPLES LATENCY";if(r&&r->hostPositionKnown){const double end=double(r->endSample)/r->sampleRate,start=end-r->lengthSeconds;hint="PLACE "+juce::String(start,3)+" s  ->  "+juce::String(end,3)+" s";if(start<0)hint+="  /  MOVE SONG RIGHT TO MAKE ROOM";}
   drawRiseLabel(g,message.isNotEmpty()?message:hint,{23,434,614,17},9,ink);
 }
};
GillRiseEditor::GillRiseEditor(GillRiseProcessor&p):AudioProcessorEditor(p),ui(std::make_unique<Impl>(p)){addAndMakeVisible(*ui);setResizable(true,true);getConstrainer()->setFixedAspectRatio(660./465.);setResizeLimits(594,419,990,698);setSize(660,465);}
GillRiseEditor::~GillRiseEditor()=default;
void GillRiseEditor::resized(){ui->setTransform(juce::AffineTransform::scale(float(getWidth())/660,float(getHeight())/465));ui->setTopLeftPosition(0,0);}

