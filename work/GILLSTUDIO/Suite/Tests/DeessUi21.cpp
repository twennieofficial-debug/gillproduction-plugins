#include "Processor.h"
#include "MacTestSupport.h"
#include <iostream>
#include <set>
#include <stdexcept>
using studio::Processor;
void require(bool result,const char* message){if(!result)throw std::runtime_error(message);}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto*c:root.getChildren())if(auto*r=find(*c,id))return r;return nullptr;}
float x(float hz){return 59+510*std::log(hz/20.f)/std::log(1000.f);}
juce::MouseEvent event(juce::Component& c,float px,float py,bool alt=false){return{juce::Desktop::getInstance().getMainMouseSource(),{px,py},juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|(alt?juce::ModifierKeys::altModifier:0)),1,0,0,0,0,&c,&c,juce::Time::getCurrentTime(),{px,py},juce::Time::getCurrentTime(),1,true};}
struct Listener:juce::AudioProcessorParameter::Listener{
 int starts=0,ends=0,changes=0,active=0;
 void parameterValueChanged(int,float)override{++changes;}
 void parameterGestureChanged(int,bool start)override{if(start){++starts;++active;}else{++ends;--active;}require(active>=0&&active<=1,"unbalanced host gesture");}
};
void snapshot(juce::AudioProcessorEditor& e,const juce::String& name){juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile(name+".png"));out.setPosition(0);out.truncate();require(juce::PNGImageFormat().writeImageToStream(e.createComponentSnapshot(e.getLocalBounds()),out),"snapshot failed");}
double render(const juce::MemoryBlock& state){Processor p(studio::Kind::Deess);p.setStateInformation(state.getData(),int(state.getSize()));p.prepareToPlay(48000,256);juce::MidiBuffer midi;double sum=0;for(int block=0;block<150;++block){juce::AudioBuffer<float>b(2,256);for(int i=0;i<256;++i){float sample=.45f*std::sin(2*juce::MathConstants<double>::pi*4500*(block*256+i)/48000.);for(int c=0;c<2;++c)b.setSample(c,i,sample);}p.processBlock(b,midi);if(block>80)for(int i=0;i<256;++i){require(std::isfinite(b.getSample(0,i)),"nonfinite DSP");sum+=b.getSample(0,i)*b.getSample(0,i);}}return sum;}
int main(){gillInitialiseTestApplication();juce::ScopedJuceInitialiser_GUI gui;try{
 Processor p(studio::Kind::Deess);p.prepareToPlay(48000,256);
 std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
 require(e->getWidth()==620&&e->getHeight()==660,"editor geometry changed");
 snapshot(*e,"deess-default");
 Listener low,high,threshold;auto*lp=p.state.getParameter("b1_low");auto*hp=p.state.getParameter("b1_high");auto*tp=p.state.getParameter("b1_threshold");lp->addListener(&low);hp->addListener(&high);tp->addListener(&threshold);
 e->mouseDown(event(*e,x(p.value("b1_low")),232));e->mouseDrag(event(*e,x(3200),232));e->mouseUp(event(*e,x(3200),232));require(std::abs(p.value("b1_low")-3200)<2,"low edge not mapped");require(low.starts==1&&low.ends==1&&low.changes>0,"low host gesture missing");
 e->mouseDown(event(*e,x(p.value("b1_high")),232));e->mouseDrag(event(*e,x(11000),232));e->mouseUp(event(*e,x(11000),232));require(std::abs(p.value("b1_high")-11000)<2,"high edge not mapped");
 e->mouseDown(event(*e,x(p.value("b1_low")),232));e->mouseDrag(event(*e,900,232));e->mouseUp(event(*e,900,232));require(p.value("b1_low")<=p.value("b1_high"),"crossed low edge");
 e->mouseDown(event(*e,x(p.value("b1_high")),256));e->mouseDrag(event(*e,x(14000),256));e->mouseUp(event(*e,x(14000),256));require(p.value("b1_high")>13990,"coincident upper edge cannot be separated");
 e->mouseDown(event(*e,x(p.value("b1_high")),232));e->mouseDrag(event(*e,-100,232));e->mouseUp(event(*e,-100,232));require(p.value("b1_high")>=p.value("b1_low"),"crossed high edge");
 p.set("b1_low",2500);p.set("b1_high",6500);
 e->mouseDown(event(*e,x(4500),232,true));e->mouseDrag(event(*e,x(4500)+30,232,true));e->mouseUp(event(*e,x(4500)+30,232,true));require(p.value("b1_low")>2500&&std::abs(p.value("b1_high")/p.value("b1_low")-2.6f)<.005f,"range move failed");
 e->mouseDown(event(*e,x(4500),232,true));e->mouseDrag(event(*e,-900,232,true));e->mouseUp(event(*e,-900,232,true));require(std::abs(p.value("b1_low")-20)<.01&&p.value("b1_high")<=20000,"range bounds failed");
 p.set("b1_low",2500);p.set("b1_high",6500);p.set("b1_ratio",12);p.set("b1_threshold",12);p.set("b2_enable",0);
 juce::MemoryBlock before,after;p.getStateInformation(before);
 e->mouseDown(event(*e,340,240));e->mouseDrag(event(*e,340,280));e->mouseUp(event(*e,340,280));require(p.value("b1_threshold")<-45,"threshold gesture failed");p.getStateInformation(after);
 const double loud=render(before),quiet=render(after);require(quiet<loud*.85,"UI threshold has no material DSP effect");std::cout<<"DSP energy before="<<loud<<" after="<<quiet<<"\n";
 {juce::MidiBuffer midi;for(int block=0;block<100;++block){juce::AudioBuffer<float>b(2,256);for(int i=0;i<256;++i)for(int c=0;c<2;++c)b.setSample(c,i,.45f*std::sin(2*juce::MathConstants<double>::pi*4500*(block*256+i)/48000.));p.processBlock(b,midi);}}
 require(p.reduction[0].load()>1,"measured reduction is absent");snapshot(*e,"deess-active-reduction");
 Processor recalled(studio::Kind::Deess);recalled.setStateInformation(after.getData(),int(after.getSize()));require(std::abs(recalled.value("b1_threshold")-p.value("b1_threshold"))<.001f,"state recall failed");
 auto*pages=dynamic_cast<juce::ComboBox*>(find(*e,"pages"));require(pages,"pages absent");
 e->mouseDown(event(*e,340,240));require(threshold.active==1,"gesture not open");pages->setSelectedId(1,juce::sendNotificationSync);require(threshold.active==0,"page change does not close gesture");
 e->mouseDown(event(*e,340,240));e->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));require(threshold.active==0,"cancel leaves host gesture open");
 auto*band2=dynamic_cast<juce::TextButton*>(find(*e,"deess_band_2"));require(band2,"band selector absent");band2->onClick();require(band2->getToggleState(),"band 2 not selected");
 const float old=p.value("b2_low");e->mouseDown(event(*e,x(old),308));e->mouseDrag(event(*e,x(8000),308));e->mouseUp(event(*e,x(8000),308));require(std::abs(p.value("b2_low")-8000)<2,"band 2 gesture failed");
 snapshot(*e,"deess-band-two");
 std::set<std::string> reachable;
 for(int page=1;page<=pages->getNumItems();++page){pages->setSelectedId(page,juce::sendNotificationSync);for(auto*c:e->getChildren())if(c->isVisible()&&(dynamic_cast<juce::Slider*>(c)||dynamic_cast<juce::ComboBox*>(c))){reachable.insert(c->getComponentID().toStdString());require(e->getLocalBounds().contains(c->getBounds()),"control outside editor");}snapshot(*e,"deess-page-"+juce::String(page));}
 for(const auto&spec:p.definition.params)require(reachable.count(spec.id)>0,"parameter inaccessible");
 std::cout<<"reachable parameters="<<reachable.size()<<" pages="<<pages->getNumItems()<<"\n";
 p.set("b1_low",12000);p.set("b1_high",1000);const auto savedLow=p.value("b1_low"),savedHigh=p.value("b1_high");e.reset();e.reset(p.createEditor());require(p.value("b1_low")==savedLow&&p.value("b1_high")==savedHigh,"opening UI rewrites old automation");snapshot(*e,"deess-reversed");
 e->mouseDown(event(*e,x(12000),232));e->mouseDrag(event(*e,x(500),232));e->mouseUp(event(*e,x(500),232));require(p.value("b1_low")<=p.value("b1_high"),"explicit inverted-edge repair failed");
 e->mouseDown(event(*e,340,240));e->mouseDrag(event(*e,340,248));e.reset();require(threshold.active==0&&low.active==0&&high.active==0,"destructor leaves gesture open");
 lp->removeListener(&low);hp->removeListener(&high);tp->removeListener(&threshold);
 require(low.starts==low.ends&&high.starts==high.ends&&threshold.starts==threshold.ends,"unbalanced gestures");
 std::cout<<"UI21_DEESS_NATIVE_PASS\n";return 0;
 }catch(const std::exception&ex){std::cerr<<ex.what()<<"\n";return 1;}}
