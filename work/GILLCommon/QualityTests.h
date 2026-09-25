#pragma once
#include "QualityUi.h"
#include <iostream>
#include <memory>
namespace gill::testing {
// Runs against the real processor in each group's native integration executable.
// Tests both runtime directions, persistent quality, correct bypass PDC and GUI.
template<class Factory,class Check>void qualityRoutes(Factory factory,Check check){
 auto p=factory();auto*q=p->apvts.getParameter("gillQuality");
 const auto name=p->getName().toStdString();auto expect=[&](bool ok,const std::string&s){check(ok,name+" quality: "+s);};
 expect(q&&q->getNumSteps()==2,"two discrete host-automatable modes");if(!q)return;
 auto set=[&](int mode){q->setValueNotifyingHost(float(mode));};
 {
  auto legacy=p->apvts.copyState();legacy.removeChild(legacy.getChildWithProperty("id","gillQuality"),nullptr);
  juce::MemoryBlock bytes;if(auto xml=legacy.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,bytes);
  set(q->getDefaultValue()<.5f?1:0);p->setStateInformation(bytes.getData(),int(bytes.getSize()));
  expect(p->qualityClient.mode()==int(q->getDefaultValue()),"pre-update state preserves original product quality");
 }
 for(double fs:{44100.,96000.}){
  set(1);p->setPlayConfigDetails(2,2,fs,127);p->prepareToPlay(fs,127);
  const int proLatency=p->getLatencySamples();
  for(int mode:{1,0,1}){
   set(mode);juce::AudioBuffer<float>b(2,127);juce::MidiBuffer midi;
   for(int j=0;j<4;++j){b.clear();p->processBlockBypassed(b,midi);}
   // Apply the same message-thread PDC queue as the timer, deterministically.
   // Timer scheduling itself is tested in the separate controller/host suite.
   p->qualityClient.pollOnMessageThread();
   const int latency=p->getLatencySamples();
   expect(latency>=0&&latency<=proLatency,"runtime latency is bounded by PRO");
   const bool pitch=name=="GILLTUNE"||name=="GILLTUNE LIVE"||name=="GILLFORM";
   if(mode==0)expect(pitch?latency>0&&latency<fs*.025:latency==0,"LIVE delay is truthful and minimized");
   // Flush the largest previous history, then measure one asymmetric impulse.
   for(int j=0;j<(proLatency+1024)/127+2;++j){b.clear();p->processBlockBypassed(b,midi);}
   bool exact=true;int at=0;const int total=latency+512;
   while(at<total){const int n=std::min(127,total-at);b.setSize(2,n,false,false,true);b.clear();if(at==0){b.setSample(0,0,.25f);b.setSample(1,0,-.125f);}p->processBlockBypassed(b,midi);
    for(int i=0;i<n;++i){exact&=std::abs(b.getSample(0,i)-(at+i==latency?.25f:0.f))<1e-6;exact&=std::abs(b.getSample(1,i)-(at+i==latency?-.125f:0.f))<1e-6;}at+=n;}
   expect(exact,"measured stereo bypass impulse equals reported delay after switch");
   juce::MemoryBlock saved;p->getStateInformation(saved);set(1-mode);p->setStateInformation(saved.getData(),int(saved.getSize()));expect(p->qualityClient.mode()==mode,"saved session restores quality");
  }
  p->releaseResources();
 }
 set(1);p->setPlayConfigDetails(2,2,48000,64);p->prepareToPlay(48000,64);
 auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());int selectors=0;
 const auto visit=[&](auto&&self,juce::Component&c)->void{if(auto*s=dynamic_cast<gill::QualitySelector*>(&c)){++selectors;expect(s->isVisible()&&s->getParentComponent()->getLocalBounds().contains(s->getBounds()),"selector fits editor");for(auto*b:s->getChildren())if(auto*button=dynamic_cast<juce::Button*>(b)){if(button->getButtonText()=="LIVE"){button->onClick();expect(p->qualityClient.mode()==0,"LIVE button updates actual parameter");}if(button->getButtonText()=="PRO"){button->onClick();expect(p->qualityClient.mode()==1,"PRO button updates actual parameter");}}}for(auto*child:c.getChildren())self(self,*child);};
 if(editor)visit(visit,*editor);expect(selectors==1,"exactly one quality selector");
}
}
