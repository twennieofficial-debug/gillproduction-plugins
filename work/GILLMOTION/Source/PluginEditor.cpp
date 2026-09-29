#include "PluginEditor.h"
#include "../../GILLCommon/MaterialUi.h"

namespace {
const juce::Colour cream(0xffefe7d7),gold(0xffb9955d),ink(0xff171817);
void labelText(juce::Graphics&g,juce::String text,juce::Rectangle<int>bounds,float size=11,juce::Colour colour=cream,int align=juce::Justification::centred){g.setFont(juce::FontOptions(size));g.setColour(colour);g.drawFittedText(text,bounds,align,1);}
struct MotionLook:juce::LookAndFeel_V4 {
 MotionLook(){setColour(juce::Slider::textBoxTextColourId,cream);setColour(juce::Slider::textBoxBackgroundColourId,ink);setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff665442));setColour(juce::ComboBox::backgroundColourId,ink);setColour(juce::ComboBox::textColourId,cream);setColour(juce::ComboBox::outlineColourId,gold);setColour(juce::PopupMenu::backgroundColourId,ink);setColour(juce::PopupMenu::textColourId,cream);setColour(juce::PopupMenu::highlightedBackgroundColourId,juce::Colour(0xff67513b));}
 void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float value,float start,float end,juce::Slider&)override{
   float radius=std::min(w,h)*.38f,cx=x+w*.5f,cy=y+h*.5f;auto r=juce::Rectangle<float>(cx-radius,cy-radius,2*radius,2*radius);
   for(int n=0;n<27;++n){float a=start+(end-start)*n/26;auto p=juce::Point<float>(cx,cy).getPointOnCircumference(radius+6,a),q=juce::Point<float>(cx,cy).getPointOnCircumference(radius+9,a);g.setColour(n<=value*26?gold:juce::Colour(0xff4c4c43));g.drawLine({p,q},n%4==0?1.7f:.7f);}
   gill::material::disc(g,r,juce::Colour(0xfff1e8d8));
   float a=start+(end-start)*value;auto p=juce::Point<float>(cx,cy).getPointOnCircumference(radius*.36f,a),q=juce::Point<float>(cx,cy).getPointOnCircumference(radius*.7f,a);g.setColour(juce::Colours::white.withAlpha(.75f));g.drawLine({p.translated(1,1),q.translated(1,1)},3.5f);g.setColour(juce::Colour(0xff3a3328));g.drawLine({p,q},2.6f);
 }
 void drawButtonBackground(juce::Graphics&g,juce::Button&b,const juce::Colour&,bool over,bool down)override{auto r=b.getLocalBounds().toFloat().reduced(1);bool bright=b.getToggleState()||b.getName()=="PRIMARY";auto a=bright?juce::Colour(0xffefddbf):juce::Colour(0xff45433c),z=bright?juce::Colour(0xffa17c49):juce::Colour(0xff1a1b18);if(down){a=a.darker(.3f);z=z.darker(.3f);}else if(over)a=a.brighter(.1f);g.setColour(juce::Colours::black.withAlpha(.7f));g.fillRoundedRectangle(r.translated(0,2),4);g.setGradientFill(juce::ColourGradient(a,r.getX(),r.getY(),z,r.getX(),r.getBottom(),false));g.fillRoundedRectangle(r,4);g.setColour(bright?cream:gold.withAlpha(.5f));g.drawRoundedRectangle(r.reduced(.5f),4,.8f);g.setColour(juce::Colours::black.withAlpha(.5f));g.drawRoundedRectangle(r.reduced(2),3,.8f);}
 void drawButtonText(juce::Graphics&g,juce::TextButton&b,bool,bool)override{labelText(g,b.getButtonText(),b.getLocalBounds().reduced(4),b.getWidth()<70?10:11,(b.getToggleState()||b.getName()=="PRIMARY")?ink:cream.withAlpha(b.isEnabled()?1.f:.4f));}
 void drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle,juce::Slider&)override{const float cy=y+h*.5f;g.setColour(juce::Colour(0xff090a08));g.fillRoundedRectangle(float(x),cy-3,float(w),6,3);g.setColour(gold.withAlpha(.65f));g.fillRoundedRectangle(float(x),cy-1,std::max(0.f,pos-x),2,1);gill::material::disc(g,{pos-5,cy-7,10,14});}
};
struct Dial:juce::Component {juce::String title;juce::Slider slider;juce::AudioProcessorValueTreeState::SliderAttachment attachment;
 Dial(GillMotionProcessor&p,const char*id,const char*name,const char*suffix):title(name),attachment(p.apvts,id,slider){slider.setSliderStyle(juce::Slider::Rotary);slider.setRotaryParameters(juce::MathConstants<float>::pi*1.2f,juce::MathConstants<float>::pi*2.8f,true);slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,20);slider.setTextValueSuffix(suffix);slider.setDoubleClickReturnValue(true,p.apvts.getParameter(id)->convertFrom0to1(p.apvts.getParameter(id)->getDefaultValue()));slider.setName(name);addAndMakeVisible(slider);}
 void resized()override{slider.setBounds(getLocalBounds().withTrimmedTop(18));}void paint(juce::Graphics&g)override{labelText(g,title,getLocalBounds().withHeight(17),10);}
};
struct Drag:juce::TextButton {std::function<void()>drag;bool fired=false;Drag():juce::TextButton("DRAG WAV"){}void mouseDown(const juce::MouseEvent&e)override{fired=false;juce::TextButton::mouseDown(e);}void mouseDrag(const juce::MouseEvent&e)override{if(!fired&&e.getDistanceFromDragStart()>5){fired=true;if(drag)drag();}}};
}

struct GillMotionEditor::Impl:juce::Component,private juce::Timer {
 GillMotionProcessor&p;MotionLook look;juce::TextButton live{"LIVE"},pro{"PRO"},bypass{"BYPASS"},arm{"ARM / RECAPTURE"},finish{"FINISH"},play{"AUDITION"},save{"SAVE WAV"},direct{"DIRECT OFF"};Drag drag;
 juce::ComboBox preset,style,grid;std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>styleAttach,gridAttach;
 juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttach;std::vector<std::unique_ptr<Dial>>dials;
 juce::Slider sourceIn,sourceOut,bpm;juce::AudioProcessorValueTreeState::SliderAttachment inAttach,outAttach,bpmAttach;
 juce::TooltipWindow tips{this,600};juce::String message;int width=640,height=470;std::shared_ptr<const gill::motion::Render>wave;
 std::vector<float>peaks;std::unique_ptr<juce::FileChooser>chooser;
 Impl(GillMotionProcessor&proc):p(proc),bypassAttach(p.apvts,"bypass",bypass),inAttach(p.apvts,"start",sourceIn),outAttach(p.apvts,"end",sourceOut),bpmAttach(p.apvts,"bpm",bpm){
  const int widths[]{520,620,600,680,620,700,730};width=widths[int(p.kind)];height=p.kind==MotionKind::Crowd?520:p.kind==MotionKind::Stutter?480:450;setLookAndFeel(&look);
  for(auto*b:{&live,&pro,&bypass,&arm,&finish,&play,&save,&direct})addAndMakeVisible(*b);addAndMakeVisible(drag);addAndMakeVisible(preset);addAndMakeVisible(style);
  for(int i=0;i<6;++i)preset.addItem(p.getProgramName(i),i+1);preset.onChange=[this]{p.setCurrentProgram(preset.getSelectedId()-1);};
  auto*styles=dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter("style"));style.addItemList(styles->choices,1);styleAttach=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"style",style);
  grid.addItemList({"1/16 T","1/16","1/8 T","1/8","1/8 D","1/4","1/2"},1);gridAttach=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"rhythm",grid);addAndMakeVisible(grid);grid.setVisible(p.kind==MotionKind::Trail||p.kind==MotionKind::Stutter);
  live.onClick=[this]{p.setValue("gillQuality",0);};pro.onClick=[this]{p.setValue("gillQuality",1);};bypass.setClickingTogglesState(true);arm.setName("PRIMARY");arm.onClick=[this]{message.clear();p.arm();};finish.onClick=[this]{p.finishCapture();};play.onClick=[this]{p.audition(!p.previewing);};save.onClick=[this]{saveFile(false);};drag.drag=[this]{saveFile(true);};direct.onClick=[this]{p.setValue("dry",p.value("dry")>.01f?0:100);};
  auto add=[&](const char*id,const char*title,const char*suffix){auto d=std::make_unique<Dial>(p,id,title,suffix);d->slider.setComponentID(id);addAndMakeVisible(*d);dials.push_back(std::move(d));};
  switch(p.kind){
   case MotionKind::Brake:add("beats","LENGTH"," BEATS");add("amount","CURVE"," %");add("tone","TONE"," %");add("level","LEVEL"," dB");break;
   case MotionKind::Wire:add("amount","DRIVE"," %");add("tone","TONE"," %");add("mix","EFFECT"," %");add("dry","DIRECT"," %");add("level","LEVEL"," dB");break;
   case MotionKind::Ghost:add("amount","BREATH"," %");add("tone","AIR"," %");add("width","WIDTH"," %");add("mix","GHOST"," %");add("dry","DIRECT"," %");add("level","LEVEL"," dB");break;
   case MotionKind::Trail:case MotionKind::Metal:add("amount",p.kind==MotionKind::Trail?"PITCH MOTION":"DEPTH"," %");add("time",p.kind==MotionKind::Trail?"FEEDBACK":"CARRIER"," %");add("tone","TONE"," %");add("width","WIDTH"," %");add("mix","EFFECT"," %");add("dry","DIRECT"," %");add("level","LEVEL"," dB");break;
   case MotionKind::Stutter:add("beats","LENGTH"," BEATS");add("syllables","SYLLABLES","");add("amount","RISE"," %");add("width","PAN"," %");add("tone","TONE"," %");add("level","LEVEL"," dB");break;
   case MotionKind::Crowd:add("voices","PEOPLE","");add("amount","HUMAN"," %");add("time","TIMING"," %");add("tone","TONE"," %");add("width","WIDTH"," %");add("mix","CROWD"," %");add("dry","DIRECT"," %");add("level","LEVEL"," dB");break;
  }
  for(auto*s:{&sourceIn,&sourceOut,&bpm}){s->setSliderStyle(juce::Slider::LinearHorizontal);s->setTextBoxStyle(juce::Slider::TextBoxRight,false,56,20);addAndMakeVisible(*s);}sourceIn.setTextValueSuffix(" %");sourceOut.setTextValueSuffix(" %");bpm.setTextValueSuffix(" BPM");bpm.setTextBoxStyle(juce::Slider::TextBoxRight,false,84,20);bpm.setName("FALLBACK BPM");
  arm.setTooltip("Capture the next vocal onset. STUTTER uses 1-3 detected syllables; other products keep the phrase, up to 12 seconds. FINISH or host STOP keeps the take. Adjust IN/OUT if the detector chooses the wrong region.");
  drag.setTooltip("Drag the rendered stereo WAV into the Playlist. STUTTER: align the end to the vocal entrance. Other effects: align the start. Rendered files stay in Documents / Jill Plugins.");
  live.setTooltip("The original signal has no added audio-buffer latency. Captured effects are rendered offline. Effect delays and timing spread are part of the sound.");pro.setTooltip("Higher-detail interpolation where applicable. Offline CROWD uses the full pitch/formant renderer in both modes.");direct.setTooltip("One-click switch for the original center voice. Also available as the DIRECT level.");
  setSize(width,height);timerCallback();startTimerHz(20);
 }
 ~Impl()override{stopTimer();setLookAndFeel(nullptr);}
 void saveFile(bool dragging){juce::String error;auto file=p.exportWav(error);if(!file.existsAsFile()){message=error;return;}message="SAVED: "+file.getFileName();if(dragging)juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,this);}
 void timerCallback()override{live.setToggleState(p.value("gillQuality")<.5f,juce::dontSendNotification);pro.setToggleState(p.value("gillQuality")>.5f,juce::dontSendNotification);direct.setToggleState(p.value("dry")<.01f,juce::dontSendNotification);direct.setButtonText(p.value("dry")<.01f?"DIRECT OFF":"DIRECT ON");direct.setVisible(p.kind!=MotionKind::Stutter&&p.kind!=MotionKind::Brake);preset.setSelectedId(p.presetMatches()?p.getCurrentProgram()+1:0,juce::dontSendNotification);preset.setTextWhenNothingSelected("CUSTOM");bpm.setEnabled(!p.hostTempo);
  auto next=p.result();bool ready=bool(next)&&!p.rendering;drag.setEnabled(ready);save.setEnabled(ready);play.setEnabled(ready);play.setButtonText(p.previewing?"STOP PREVIEW":"AUDITION");finish.setEnabled(p.capture.status()==gill::motion::Capture::Recording);if(next!=wave){wave=next;peaks.assign(width-64,0);if(wave)for(size_t x=0;x<peaks.size();++x){const size_t first=x*wave->left.size()/peaks.size(),last=(x+1)*wave->left.size()/peaks.size();for(size_t i=first;i<last;++i)peaks[x]=std::max({peaks[x],std::abs(wave->left[i]),std::abs(wave->right[i])});}}repaint();}
 void resized()override{live.setBounds(width-185,19,43,24);pro.setBounds(width-137,19,43,24);bypass.setBounds(width-88,19,65,24);arm.setBounds(22,65,148,29);finish.setBounds(177,65,65,29);style.setBounds(width-249,65,128,29);grid.setBounds(width-115,65,93,29);if(!grid.isVisible())style.setBounds(width-166,65,144,29);
  const int y=height-201;sourceIn.setBounds(53,y,(width-130)/2,22);sourceOut.setBounds(width/2+39,y,(width-130)/2,22);const int column=(width-36)/int(dials.size());for(size_t i=0;i<dials.size();++i)dials[i]->setBounds(18+int(i)*column,y+32,column,109);
  play.setBounds(22,height-53,105,27);save.setBounds(134,height-53,86,27);drag.setBounds(227,height-53,100,27);preset.setBounds(337,height-53,width-359,27);direct.setBounds(width-121,y-32,99,24);bpm.setBounds(23,y-32,170,24);
 }
 void paint(juce::Graphics&g)override{
  const juce::Colour woods[]{juce::Colour(0xff765137),juce::Colour(0xff59362e),juce::Colour(0xff79644b),juce::Colour(0xff4e3426),juce::Colour(0xff49403a),juce::Colour(0xff865b36),juce::Colour(0xff654b34)};
  g.setGradientFill(juce::ColourGradient(woods[int(p.kind)].brighter(.2f),0,0,woods[int(p.kind)].darker(.6f),float(width),float(height),false));g.fillAll();for(int y=0;y<height;y+=3){juce::Path grain;grain.startNewSubPath(0,float(y));for(int x=0;x<=width;x+=10)grain.lineTo(float(x),float(y+2.3*std::sin(x*.021+y*.015)));g.setColour(juce::Colours::black.withAlpha(.1f));g.strokePath(grain,juce::PathStrokeType(.7f));}
  auto body=getLocalBounds().toFloat().reduced(10);g.setColour(juce::Colours::black.withAlpha(.6f));g.fillRoundedRectangle(body.translated(0,3),6);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff393b38),0,12,juce::Colour(0xff101210),0,float(height),false));g.fillRoundedRectangle(body,6);g.setColour(gold.withAlpha(.6f));g.drawRoundedRectangle(body,6,1);
  juce::Path gp;gp.startNewSubPath(28,5);gp.lineTo(15,5);gp.cubicTo(-3,5,-3,30,15,30);gp.lineTo(29,30);gp.lineTo(29,19);gp.lineTo(17,19);gp.startNewSubPath(24,39);gp.lineTo(24,13);gp.lineTo(40,13);gp.cubicTo(55,13,55,31,40,31);gp.lineTo(24,31);gp.applyTransform(juce::AffineTransform::scale(.72f,.72f).translated(26,18));g.setColour(juce::Colours::black);g.strokePath(gp,juce::PathStrokeType(4));g.setColour(gold);g.strokePath(gp,juce::PathStrokeType(2.3f));labelText(g,p.getName(),{72,17,width-276,29},21,cream,juce::Justification::centredLeft);labelText(g,"GILLPRODUCTION",{74,43,240,12},8,gold,juce::Justification::centredLeft);
  auto display=juce::Rectangle<float>(22,105,float(width-44),float(height-344));g.setColour(juce::Colour(0xff080b0a));g.fillRoundedRectangle(display,5);g.setColour(gold.withAlpha(.45f));g.drawRoundedRectangle(display,5,.7f);
  if(wave){const float centre=display.getCentreY(),amplitude=display.getHeight()*.30f;for(int x=0;x<int(peaks.size());++x){const float value=peaks[x]/std::max(.001f,wave->peak)*amplitude;g.setColour(gold.withAlpha(.85f));g.drawVerticalLine(32+x,centre-value,centre+value);}labelText(g,juce::String(wave->left.size()/wave->rate,2)+" s",{32,111,80,17},10,gold,juce::Justification::centredLeft);}
  else if(p.kind==MotionKind::Stutter){for(int i=0;i<16;++i){float x=display.getX()+12+i*(display.getWidth()-24)/16,h=10+i*3;g.setColour(gold.withAlpha(.15f+i*.04f));g.fillRoundedRectangle(x,display.getBottom()-22-h,(display.getWidth()-48)/16,h,2);}}
  else if(p.kind==MotionKind::Crowd){const int people=int(p.value("voices"));for(int i=0;i<people;++i){float x=display.getX()+20+(display.getWidth()-40)*(i+.5f)/people,y=display.getCentreY()+float(std::sin(i*2.1)*15);g.setColour((i%3==0?cream:i%3==1?gold:juce::Colour(0xffa4b5ac)).withAlpha(.8f));g.fillEllipse(x-4,y-12,8,8);g.fillRoundedRectangle(x-6,y-2,12,18,3);}}
  else{labelText(g,gill::motion::designer(p.kind)?"CAPTURE YOUR VOCAL TO SHAPE THE EFFECT":"LIVE VOCAL EFFECT / ARM FOR WAV EXPORT",display.toNearestInt(),11,gold);}
  labelText(g,p.statusText(),{30,int(display.getBottom())-21,width-60,16},9,cream);const int y=height-201;labelText(g,"IN",{21,y,29,22},9);labelText(g,"OUT",{width/2+1,y,35,22},9);
  labelText(g,p.hostTempo?juce::String(p.tempo.load(),1)+" BPM / HOST":"MANUAL TEMPO",{205,y-30,width-340,20},9,gold);
  labelText(g,message.isNotEmpty()?message:p.kind==MotionKind::Stutter?"ALIGN WAV END TO VOCAL START":"ALIGN WAV START TO CAPTURED VOCAL",{22,height-23,width-44,15},8,gold);
 }
};
GillMotionEditor::GillMotionEditor(GillMotionProcessor&p):AudioProcessorEditor(p),ui(std::make_unique<Impl>(p)){addAndMakeVisible(*ui);setResizable(true,true);getConstrainer()->setFixedAspectRatio(double(ui->width)/ui->height);setResizeLimits(ui->width,ui->height,ui->width*2,ui->height*2);setSize(ui->width,ui->height);}
GillMotionEditor::~GillMotionEditor()=default;
void GillMotionEditor::resized(){ui->setTransform(juce::AffineTransform::scale(float(getWidth())/ui->width,float(getHeight())/ui->height));ui->setTopLeftPosition(0,0);}
