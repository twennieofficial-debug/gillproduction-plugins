#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <fstream>
#include <new>
#include <cstdlib>
#include <set>
#include <random>
#if JUCE_WINDOWS
#include <windows.h>
#endif
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
extern "C" void gillPumpMacTestEvents();
#endif
static int checks=0,failures=0;static std::atomic<int>allocations{0};static thread_local bool watch=false;
void*operator new(size_t n){if(watch)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](size_t n){return::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void check(bool good,const juce::String&name,double value=0){++checks;if(!good)++failures;std::printf("%s %s %.9g\n",good?"PASS":"FAIL",name.toRawUTF8(),value);}
float vocal(double t){const double local=std::fmod(t,1.);if(local<.08||local>.60)return 0;const double fade=std::min(1.,std::min((local-.08)/.025,(.60-local)/.035));return static_cast<float>(fade*(.2*std::sin(2*juce::MathConstants<double>::pi*171*t)+.055*std::sin(2*juce::MathConstants<double>::pi*342*t)));}
struct Head:juce::AudioPlayHead{double beat=0,bpm=120;bool playing=true;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setPpqPosition(beat);p.setBpm(bpm);p.setIsPlaying(playing);return p;}};
bool setup(GillCreativeProcessor&p,double rate,int block,int channels=2,bool side=false){auto layout=p.getBusesLayout();layout.inputBuses.set(0,channels==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());layout.outputBuses.set(0,layout.inputBuses[0]);if(layout.inputBuses.size()>1)layout.inputBuses.set(1,side?juce::AudioChannelSet::stereo():juce::AudioChannelSet::disabled());const bool valid=p.setBusesLayout(layout);p.prepareToPlay(rate,block);return valid;}
double process(GillCreativeProcessor&p,Head&head,double seconds,double rate=48000,int block=127,bool source=true){juce::AudioBuffer<float>b(p.getTotalNumInputChannels(),block);juce::MidiBuffer midi;double energy=0;const int total=static_cast<int>(seconds*rate);for(int at=0;at<total;at+=block){const int n=std::min(block,total-at);b.setSize(p.getTotalNumInputChannels(),n,false,false,true);for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<n;++i)b.setSample(c,i,c>=p.getTotalNumOutputChannels()?.2f:(source?vocal((at+i)/rate):0));watch=true;p.processBlock(b,midi);watch=false;for(int c=0;c<p.getTotalNumOutputChannels();++c)for(int i=0;i<n;++i){const float v=b.getSample(c,i);if(!std::isfinite(v))return std::numeric_limits<double>::quiet_NaN();energy+=v*v;}head.beat+=n/rate*head.bpm/60;}return energy;}
void basic(CreativeKind kind){auto p=std::make_unique<GillCreativeProcessor>(kind);Head head;p->setPlayHead(&head);const auto name=p->getName();check(p->getNumPrograms()==6,name+" has six usable presets");
 for(double rate:{8000.,44100.,48000.,96000.,192000.})for(int channels:{1,2}){check(setup(*p,rate,127,channels),name+" mono/stereo layout accepted");head.beat=0;check(std::isfinite(process(*p,head,.035,rate)),name+" finite sample-rate processing");check(p->getLatencySamples()==0,name+" has zero reported extra latency");}
 setup(*p,48000,127);head.beat=0;for(int i=0;i<6;++i){p->selectPreset(i,false);check(p->presetMatches(),name+" preset recalls every parameter "+juce::String(i));check(std::isfinite(process(*p,head,.08)),name+" every preset processes audio");}
 p->selectPreset(0,false);for(auto*parameter:p->getParameters())if(auto*ranged=dynamic_cast<juce::RangedAudioParameter*>(parameter))p->setValue(ranged->paramID,ranged->convertFrom0to1(.37f),false);
 juce::MemoryBlock state;p->getStateInformation(state);std::vector<float>values;for(auto*parameter:p->getParameters())values.push_back(parameter->getValue());p->selectPreset(4,false);p->setStateInformation(state.getData(),static_cast<int>(state.getSize()));bool equal=true;for(size_t i=0;i<values.size();++i)equal&=std::abs(values[i]-p->getParameters()[static_cast<int>(i)]->getValue())<1.e-6f;check(equal,name+" APVTS state roundtrip");const float before=p->value("amount");const char bad[]{'a','b','c'};p->setStateInformation(bad,3);check(p->value("amount")==before,name+" malformed state rejected");
 auto originalXml=juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize()));
 for(const auto text:{"nonsense","nan","200"}){auto invalid=juce::ValueTree::fromXml(*originalXml);invalid.getChildWithProperty("id","amount").setProperty("value",text,nullptr);juce::MemoryBlock poisoned;juce::AudioProcessor::copyXmlToBinary(*invalid.createXml(),poisoned);p->setStateInformation(poisoned.getData(),static_cast<int>(poisoned.getSize()));check(p->value("amount")==before,name+" nonnumeric/nonfinite/out-of-range state rejected atomically");}
 {auto invalid=juce::ValueTree::fromXml(*originalXml);invalid.addChild(invalid.getChildWithProperty("id","amount").createCopy(),-1,nullptr);juce::MemoryBlock poisoned;juce::AudioProcessor::copyXmlToBinary(*invalid.createXml(),poisoned);p->setStateInformation(poisoned.getData(),static_cast<int>(poisoned.getSize()));check(p->value("amount")==before,name+" duplicate state parameters rejected atomically");}
 p->selectPreset(0,false);p->setValue("gillQuality",0,false);head.beat=0;check(std::isfinite(process(*p,head,.1)),name+" LIVE actual audio");p->setValue("gillQuality",1,false);check(std::isfinite(process(*p,head,.1)),name+" PRO actual audio");
 p->setValue("bypass",1,false);process(*p,head,.4);juce::AudioBuffer<float>b(2,127);juce::MidiBuffer midi;for(int i=0;i<127;++i)b.setSample(0,i,vocal(i/48000.+.1)),b.setSample(1,i,vocal(i/48000.+.1));p->processBlock(b,midi);bool exact=true;for(int i=0;i<127;++i)exact&=std::abs(b.getSample(0,i)-vocal(i/48000.+.1))<1.e-6;check(exact,name+" bypass reaches dry unity without latency");p->setPlayHead(nullptr);
}
void learning(CreativeKind kind){auto p=std::make_unique<GillCreativeProcessor>(kind);Head head;p->setPlayHead(&head);setup(*p,48000,127,2,kind==CreativeKind::Director);p->setValue("amount",100,false);p->setValue("mix",100,false);p->setValue("dry",0,false);const auto name=p->getName();
 p->startLearn();process(*p,head,3);p->stopLearn();process(*p,head,.01,48000,127,false);check(p->engine.learningState()==2&&p->plan().count==3,name+" learns real phrase boundaries");p->applyLearn();head.beat=0;const double output=process(*p,head,4,48000,127,kind!=CreativeKind::Reply);check(output>1.e-5&&std::isfinite(output),name+" applied learned processing audible",output);
 auto model=p->plan();model.markers[0].strength=.22f;p->editPlan(model);process(*p,head,.01);check(std::abs(p->plan().markers[0].strength-.22f)<1.e-6,name+" timeline edits reach DSP");
 juce::MemoryBlock state;p->getStateInformation(state);auto restored=std::make_unique<GillCreativeProcessor>(kind);restored->setStateInformation(state.getData(),static_cast<int>(state.getSize()));Head second;restored->setPlayHead(&second);setup(*restored,48000,127);process(*restored,second,.01,48000,127,false);check(restored->plan().count==3&&restored->engine.isApplied(),name+" learned model recalls before prepareToPlay");second.beat=0;double replay=process(*restored,second,4,48000,127,kind!=CreativeKind::Reply);check(replay>1.e-5&&std::isfinite(replay),name+" recalled model and captured audio audibly work",replay);
 for(int i=0;i<5;++i){restored->setStateInformation(state.getData(),static_cast<int>(state.getSize()));process(*restored,second,.01,48000,127,false);}check(restored->engine.isApplied()&&restored->plan().count==3,name+" repeated state recall never exhausts capture slots");
 setup(*restored,96000,127);second.beat=0;replay=process(*restored,second,4,96000,127,kind!=CreativeKind::Reply);check(replay>1.e-5&&restored->plan().count==3,name+" sample-rate restart preserves learned plan and capture",replay);
 p->undoLearn();process(*p,head,.01);check(std::abs(p->plan().markers[0].strength-.22f)>.01,name+" undo restores previous marker settings");p->setPlayHead(nullptr);restored->setPlayHead(nullptr);
}
void tick(){juce::Thread::sleep(60);
#if JUCE_WINDOWS
 MSG message;for(int i=0;i<4096&&PeekMessage(&message,nullptr,0,0,PM_REMOVE);++i){TranslateMessage(&message);DispatchMessage(&message);}
#elif JUCE_MAC
 gillPumpMacTestEvents();
#endif
 juce::Timer::callPendingTimersSynchronously();}
juce::MouseEvent mouse(juce::Component& c,juce::Point<float> pt,juce::ModifierKeys mods){auto now=juce::Time::getCurrentTime();return{juce::Desktop::getInstance().getMainMouseSource(),pt,mods,1,0,0,0,0,&c,&c,now,pt,now,1,false};}
void click(juce::Button& b){juce::Component& c=b;const auto pt=b.getLocalBounds().getCentre().toFloat();c.mouseDown(mouse(c,pt,juce::ModifierKeys::leftButtonModifier));c.mouseUp(mouse(c,pt,juce::ModifierKeys()));tick();}
void gather(juce::Component&c,std::vector<juce::Component*>&out){out.push_back(&c);for(auto*child:c.getChildren())gather(*child,out);}
void ui(CreativeKind kind){auto p=std::make_unique<GillCreativeProcessor>(kind);Head head;p->setPlayHead(&head);setup(*p,48000,127);p->startLearn();process(*p,head,3);p->stopLearn();process(*p,head,.01);auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());editor->setVisible(true);tick();const auto name=p->getName();check(editor->getWidth()<=760&&editor->getHeight()<=480,name+" compact default editor");std::vector<juce::Component*>components;gather(*editor,components);std::set<juce::String>covered;
 for(auto*c:components)if(auto*s=dynamic_cast<juce::Slider*>(c)){const auto id=c->getComponentID();if(auto*parameter=p->apvts.getParameter(id)){const float wanted=parameter->convertFrom0to1(.64f);s->setValue(wanted,juce::sendNotificationSync);check(std::abs(p->value(id)-static_cast<float>(s->getValue()))<.02,name+" slider connected "+id);covered.insert(id);}else if(id.startsWith("marker")){s->setValue(s->getValue()*.94,juce::sendNotificationSync);process(*p,head,.01);check(gill::creative::Engine::validPlan(p->plan()),name+" editable source trim/strength "+id);}}
 check(covered.size()==7,name+" all seven sound controls wired");
 for(auto*c:components)if(auto*b=dynamic_cast<juce::TextButton*>(c)){if(b->getComponentID().startsWith("variant")){click(*b);process(*p,head,.01);check(p->value("variant")==b->getComponentID().getTrailingIntValue(),name+" variant button previews actual variant");}if(b->getButtonText()=="LIVE"||b->getButtonText()=="PRO"){click(*b);check(p->value("gillQuality")== (b->getButtonText()=="PRO"?1.f:0.f),name+" quality UI controls real APVTS");}if(b->getComponentID()=="apply"){click(*b);process(*p,head,.01);check(p->engine.isApplied(),name+" APPLY button commits model");}}

 for(auto*c:components)if(auto*b=dynamic_cast<juce::TextButton*>(c)){
  if(b->getComponentID()=="bypass"){const float old=p->value("bypass");click(*b);check(p->value("bypass")!=old,name+" actual bypass click controls audio parameter");click(*b);}
  if(b->getComponentID()==">"){const int old=p->getCurrentProgram();click(*b);check(p->getCurrentProgram()==(old+1)%6,name+" next preset button");}
  if(b->getComponentID()=="<"){const int old=p->getCurrentProgram();click(*b);check(p->getCurrentProgram()==(old+5)%6,name+" previous preset button");}
 }
 for(auto*c:components)if(auto*combo=dynamic_cast<juce::ComboBox*>(c))if(combo->getComponentID()=="preset"){combo->setSelectedId(6,juce::sendNotificationSync);check(p->getCurrentProgram()==5&&p->presetMatches(),name+" preset selector recalls its real sound");}
 for(auto*c:components)if(c->getComponentID()=="creativeTimeline"){
  const auto before=p->plan();const float x=15+before.markers[0].endBeat/before.durationBeats*(c->getWidth()-30);const juce::Point<float>point(x,c->getHeight()*.5f);c->mouseDown(mouse(*c,point,juce::ModifierKeys::leftButtonModifier));c->mouseDrag(mouse(*c,point+juce::Point<float>(7,-8),juce::ModifierKeys::leftButtonModifier));c->mouseUp(mouse(*c,point+juce::Point<float>(7,-8),juce::ModifierKeys()));process(*p,head,.01);check(p->plan().markers[0].endBeat!=before.markers[0].endBeat,name+" actual timeline drag edits learned timing");
 }
 for(auto*c:components)if(auto*b=dynamic_cast<juce::TextButton*>(c))if(b->getComponentID()=="learn"){
  p->setValue("sensitivity",-40,false);click(*b);process(*p,head,3);check(p->engine.learningState()==1,name+" LEARN click starts signal capture");tick();click(*b);process(*p,head,.01);check(p->engine.learningState()==2&&p->plan().valid(),name+" STOP click produces reviewable learned plan");
 }
 p->selectPreset(0,false);p->audition(false);process(*p,head,.01);tick();const int w=editor->getWidth(),h=editor->getHeight();for(int scale:{1,2}){editor->setSize(w*scale,h*scale);tick();auto image=editor->createComponentSnapshot(editor->getLocalBounds());juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile(name+"-UI-"+juce::String(w*scale)+"x"+juce::String(h*scale)+".png"));output.setPosition(0);output.truncate();juce::PNGImageFormat png;check(output.openedOk()&&png.writeImageToStream(image,output),name+" native UI screenshot");}p->setPlayHead(nullptr);
}
int main(){
#if JUCE_MAC
 gillInitialiseMacTestApplication();
#endif
 juce::ScopedJuceInitialiser_GUI gui;for(auto kind:{CreativeKind::Phrase,CreativeKind::Director,CreativeKind::Reply}){basic(kind);learning(kind);tick();ui(kind);}check(allocations.load()==0,"wrapper audio blocks and learning transitions allocate no heap",allocations.load());std::printf("RESULT %d checks %d failures\n",checks,failures);std::ofstream report("creative-integration-report.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_allocations\":"<<allocations.load()<<"}";return failures?1:0;}
