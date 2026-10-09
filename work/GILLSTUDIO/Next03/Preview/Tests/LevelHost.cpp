#include "MacTestSupport.h"
#include "Processor.h"
#include <iostream>
#include <stdexcept>
thread_local bool counting=false;thread_local size_t allocations=0;
void*operator new(size_t n){if(counting)++allocations;if(auto*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
void*operator new[](size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}
using preview::Processor;namespace sp=preview::sp;
void require(bool ok,const char*m){if(!ok)throw std::runtime_error(m);}
void buses(Processor&p,int main,int key){auto l=p.getBusesLayout();l.inputBuses.set(0,main==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());l.outputBuses.set(0,l.inputBuses[0]);l.inputBuses.set(1,key==0?juce::AudioChannelSet::disabled():key==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());require(p.setBusesLayout(l),"supported main/key layout rejected");}
void processing(){
 for(int mode=0;mode<2;++mode)for(int main:{1,2})for(int key:{0,1,2})for(int block:{1,127,513}){
  Processor p;buses(p,main,key);p.set("mode",float(mode));p.set("strength",.8f);p.set("levelthreshold",-35);p.set("levelslope",2);p.set("externalkey",1);p.set("keygain",-3);p.set("d0_on",1);p.set("d0_freq",3000);p.set("d0_gain",8);p.set("d0_q",.7f);p.prepareToPlay(48000,block);
  sp::Processor core;core.prepare(48000,size_t(block),size_t(main),2048);sp::Parameters q;q.mode=mode?sp::Mode::Live:sp::Mode::Pro;q.levelStrength=.8;q.levelThresholdDb=-35;q.levelSlopeDbPerOct=2;q.externalKey=true;q.keyGainDb=-3;q.detectorNodes[0].enabled=true;q.detectorNodes[0].frequency=3000;q.detectorNodes[0].gainDb=8;q.detectorNodes[0].q=.7;core.setParameters(q);
  double action=0;std::vector<float>history;juce::MidiBuffer midi;
  for(int start=0;start<18000;start+=block){const int count=std::min(block,18000-start);juce::AudioBuffer<float>b(main+key,count),reference(main,count);std::array<std::vector<float>,2>copy;std::array<const float*,2>kp{};for(int c=0;c<key;++c){copy[size_t(c)].resize(size_t(count));kp[size_t(c)]=copy[size_t(c)].data();}
   for(int i=0;i<count;++i){const double t=(start+i)/48000.;const float x=float(.1*std::sin(2*sp::pi*3000*t)+.03*std::sin(2*sp::pi*1000*t));history.push_back(x);for(int c=0;c<main;++c)b.setSample(c,i,x);for(int c=0;c<key;++c){const float v=float(.2*std::sin(2*sp::pi*(c?1100:3000)*t));b.setSample(main+c,i,v);copy[size_t(c)][size_t(i)]=v;}}
   for(int c=0;c<main;++c)reference.copyFrom(c,0,b,c,0,count);core.process(reference.getArrayOfWritePointers(),size_t(count),key?kp.data():nullptr,size_t(key));
   allocations=0;counting=true;p.processBlock(b,midi);counting=false;require(allocations==0,"key bus callback allocation");
   for(int i=0;i<count;++i){for(int c=0;c<main;++c)require(std::abs(b.getSample(c,i)-reference.getSample(c,i))<3e-6,"host controls/key differ from independently tested core");for(int c=0;c<key;++c)require(b.getSample(main+c,i)==copy[size_t(c)][size_t(i)],"sidechain input overwritten");if(start+i>6000){const int index=start+i-(mode?0:2048);action+=std::abs(b.getSample(0,i)-history[size_t(index)]);}}
  }require(action>1,"active level path ineffective");
 }
 std::cout<<"main mono/stereo, key disabled/mono/stereo, variable blocks and both modes passed\n";
}
juce::MemoryBlock encode(const juce::ValueTree&t){juce::MemoryBlock b;juce::MemoryOutputStream s(b,false);t.writeToStream(s);return b;}
void states(){Processor p;p.set("strength",.8f);p.set("levelthreshold",-49);p.set("d4_on",1);p.set("d4_gain",14);p.set("externalkey",1);juce::MemoryBlock state;p.getStateInformation(state);Processor fresh;fresh.setStateInformation(state.getData(),int(state.getSize()));require(fresh.value("strength")==p.value("strength")&&fresh.value("d4_gain")==p.value("d4_gain"),"new controls recall");auto legacy=juce::ValueTree::readFromData(state.getData(),state.getSize());legacy.removeProperty("stateSchema",nullptr);for(int i=legacy.getNumChildren()-1;i>=0;--i){const auto id=legacy.getChild(i).getProperty("id").toString();if(id=="strength"||id=="levelthreshold"||id=="levelslope"||id=="externalkey"||id=="keygain"||(id.startsWith("d")&&id.contains("_")))legacy.removeChild(i,nullptr);}auto bytes=encode(legacy);p.setStateInformation(bytes.getData(),int(bytes.getSize()));require(p.value("strength")==0&&p.value("levelthreshold")==-30&&p.value("externalkey")==0&&p.value("d4_on")==0&&p.value("d4_gain")==0,"old state retains previously active new controls");std::cout<<"new recall and legacy-to-used-instance default migration passed\n";}
void ui(){
 Processor p;p.prepareToPlay(48000,127);std::unique_ptr<juce::AudioProcessorEditor>editor(p.createEditor());
 auto component=[&](const char* id){return editor->findChildWithID(id);};
 auto*bank=dynamic_cast<juce::ComboBox*>(component("bank"));auto*page=dynamic_cast<juce::ComboBox*>(component("pages"));auto*freq=dynamic_cast<juce::Slider*>(component("n0_freq"));
 require(bank&&page&&freq,"graph/page selectors missing");const float original=p.value("n0_freq");bank->setSelectedId(2,juce::sendNotificationSync);freq->setValue(2345,juce::sendNotificationSync);
 require(std::abs(p.value("d0_freq")-2345)<.1f&&p.value("n0_freq")==original,"detector graph edits legacy weight bank");
 page->setSelectedId(3,juce::sendNotificationSync);
 for(const char* id:{"strength","levelthreshold","levelslope","keygain"}){auto*s=dynamic_cast<juce::Slider*>(component(id));require(s&&s->isVisible(),"level page control missing");require(editor->getLocalBounds().contains(s->getBounds()),"level controls clipped");const auto changed=s->getMinimum()+.6*(s->getMaximum()-s->getMinimum());s->setValue(changed,juce::sendNotificationSync);require(std::abs(p.value(id)-s->getValue())<.01,"level parameter binding failed");}
 juce::Button*external=nullptr;for(auto*c:editor->getChildren())if(auto*b=dynamic_cast<juce::Button*>(c))if(b->getButtonText()=="EXTERNAL KEY")external=b;
 require(external&&external->isVisible()&&editor->getLocalBounds().contains(external->getBounds()),"external key control missing or clipped");external->setToggleState(true,juce::sendNotificationSync);require(p.value("externalkey")==1,"external key parameter binding failed");
 auto image=editor->createComponentSnapshot(editor->getLocalBounds());juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile("level-editor.png"));juce::PNGImageFormat().writeImageToStream(image,out);
 page->setSelectedId(1,juce::sendNotificationSync);require(!component("strength")->isVisible()&&!external->isVisible(),"level controls remain on noise page");
 std::cout<<"independent graph binding and compact level page passed\n";
}
int main(){gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;try{processing();states();ui();std::cout<<"SPECTRA_LEVEL_HOST_PASS\n";}catch(const std::exception&e){counting=false;std::cerr<<e.what()<<"\n";return 1;}}
