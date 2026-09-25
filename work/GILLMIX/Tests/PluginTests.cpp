#include "PluginProcessor.h"
#include <iostream>
#include <memory>
#include <new>
#include <cstdlib>
#include <thread>
#if defined(__APPLE__)
extern "C" void gillInitialiseMacTestApplication();
#endif
namespace gillMixAllocationAudit{thread_local bool enabled=false;thread_local unsigned allocations=0;}
void*operator new(size_t n){if(gillMixAllocationAudit::enabled)++gillMixAllocationAudit::allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](size_t n){return::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
int checks=0,failures=0;void check(bool b,const char*m){++checks;if(!b){++failures;std::cerr<<"FAIL "<<m<<'\n';}}
struct PlayHead:juce::AudioPlayHead{int64_t at=0;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setTimeInSamples(at);p.setTimeInSeconds(at/48000.);p.setIsPlaying(true);return p;}};
int main(){
#if defined(__APPLE__)
 gillInitialiseMacTestApplication();
#endif
 juce::ScopedJuceInitialiser_GUI gui;
 using namespace gill::mix07;
 auto master=std::make_unique<GillMixProcessor>(GillMixKind::master);
 std::array<std::unique_ptr<GillMixProcessor>,3>links;
 for(auto&p:links)p=std::make_unique<GillMixProcessor>(GillMixKind::link);
 links[0]->setLocalName("MAIN");links[0]->setLocalRole(Role::main);links[1]->setLocalName("BEAT");links[1]->setLocalRole(Role::beat);links[2]->setLocalName("DOUBLE");links[2]->setLocalRole(Role::doubleVoice);
 auto service=[&]{for(auto&p:links)p->service();master->service();};service();check(master->tracks.size()==3,"three real processor instances discovered");
 links[0]->setLocalName("BEAT MISLEADING NAME");service();bool manualWins=false;for(const auto&t:master->tracks)if(t.row.address.runtime==links[0]->bus.runtimeId())manualWins=t.role==Role::main&&t.confirmed;check(manualWins,"explicit MAIN role overrides a misleading BEAT name");links[0]->setLocalName("MAIN");service();
 for(const auto&t:master->tracks)master->chooseTrack(t.row.local.persistent,true);master->service();master->connectSelected();service();
 bool connected=true;for(const auto&t:master->tracks)connected&=t.row.owner==master->bus.runtimeId();check(connected,"explicit selected links connected without gain change");
 master->setCurrentProgram(1);PlayHead play;for(auto&p:links){p->setPlayHead(&play);p->prepareToPlay(48000,256);}master->setPlayHead(&play);master->prepareToPlay(48000,256);
 juce::MidiBuffer midi;juce::AudioBuffer<float>buffer(2,256);
 // More history than the shared telemetry ring; LEARN must start at its current cursor.
 for(int block=0;block<500;++block){play.at=int64_t(block)*256;buffer.clear();for(auto&p:links)p->processBlock(buffer,midi);if(block%4==0)service();}
 master->startLearn();service();
 unsigned allocations=0;
 for(int block=0;block<1900;++block){play.at=int64_t(block)*256;for(int voice=0;voice<3;++voice){for(int c=0;c<2;++c)for(int n=0;n<256;++n)buffer.setSample(c,n,(voice==1?.1f:.05f)*static_cast<float>(std::sin((play.at+n)*2*3.141592653589793*(voice==1?110:220)/48000)));gillMixAllocationAudit::allocations=0;gillMixAllocationAudit::enabled=true;links[static_cast<size_t>(voice)]->processBlock(buffer,midi);gillMixAllocationAudit::enabled=false;allocations+=gillMixAllocationAudit::allocations;}if(block%4==0)service();}
 service();master->stopLearn();check(allocations==0,"gain plus learning callbacks have no C++ allocation");check(master->canApply(),"aligned active ten-second section produces bounded proposal");
 master->apply();for(int i=0;i<12;++i)service();check(master->canUndo(),"successful committed set creates undo");
 check(std::abs(links[0]->gainParameter->value.db())<.001,"main anchor preserved");check(std::abs(links[1]->gainParameter->value.db()+3)<.001,"beat correction respects default three-dB limit");check(std::abs(links[2]->gainParameter->value.db()+3)<.001,"double correction respects default three-dB limit");
 for(auto&p:links){const auto raw=p->apvts.getRawParameterValue("trackLevel")->load();check(std::abs(raw-p->gainParameter->value.db())<.001,"APVTS raw cache agrees with canonical gain after remote notification");check(p->getLatencySamples()==0,"LINK reports zero additional latency");}
 master->undo();for(int i=0;i<12;++i)service();for(auto&p:links)check(std::abs(p->gainParameter->value.db())<.001,"undo restores previous local gain");
 // Selection controls writes even for tracks that remain connected as analysis references.
 master->chooseTrack(links[2]->localSnapshot().persistent,false);master->service();master->startLearn();service();
 for(int block=0;block<1900;++block){play.at=int64_t(block)*256;for(int voice=0;voice<3;++voice){for(int c=0;c<2;++c)for(int n=0;n<256;++n)buffer.setSample(c,n,(voice==1?.1f:.05f)*static_cast<float>(std::sin((play.at+n)*2*3.141592653589793*220/48000)));links[static_cast<size_t>(voice)]->processBlock(buffer,midi);}if(block%4==0)service();}
 service();master->stopLearn();check(master->canApply(),"selection-aware proposal remains available");master->apply();for(int i=0;i<12;++i)service();check(links[2]->gainParameter->value.db()==0,"connected but deselected DOUBLE is not changed by APPLY");check(links[1]->gainParameter->value.db()==-3,"selected connected BEAT still receives bounded proposal");master->undo();for(int i=0;i<12;++i)service();
 // Recall must invalidate an issued COMMIT even without a later master timer tick.
 master->manualGain(links[1]->bus.runtimeId(),-2);for(auto&p:links)p->service();master->service();
 juce::MemoryBlock masterState;master->getStateInformation(masterState);master->setStateInformation(masterState.getData(),static_cast<int>(masterState.getSize()));links[1]->service();check(links[1]->gainParameter->value.db()==0,"master recall revokes a pending COMMIT before next master tick");service();master->connectSelected();service();
 const auto target=links[1]->bus.runtimeId();master->manualGain(target,-2);for(int i=0;i<12;++i)service();links[1]->gainParameter->setValueNotifyingHost(links[1]->gainParameter->convertTo0to1(1));service();master->undo();for(int i=0;i<12;++i)service();check(links[1]->gainParameter->value.db()==1,"undo never overwrites intervening host edit");
 juce::MemoryBlock state;links[1]->getStateInformation(state);links[1]->gainParameter->setValueNotifyingHost(0);links[1]->setStateInformation(state.getData(),static_cast<int>(state.getSize()));service();check(links[1]->gainParameter->value.db()==1,"state restores canonical local gain");check(links[1]->status=="NOT CONNECTED","state recall disarms remote binding");
 links[1]->prepareToPlay(48000,256);buffer.clear();buffer.setSample(0,0,.5f);links[1]->processBlock(buffer,midi);check(std::abs(buffer.getSample(0,0)-.5f*std::pow(10.f,.05f))<1e-6,"restored gain works without any GUI pump from sample zero");
 auto bad=state;static_cast<char*>(bad.getData())[0]='X';links[1]->setStateInformation(bad.getData(),static_cast<int>(bad.getSize()));check(links[1]->gainParameter->value.db()==1,"invalid state is rejected without gain mutation");
 // Actual receiver lock is authoritative even for direct manual remote edits.
 master->service();master->connectSelected();service();links[0]->setLocalLock(true);service();const auto lockedGain=links[0]->gainParameter->value.snapshot();master->manualGain(links[0]->bus.runtimeId(),-4);for(int i=0;i<6;++i)service();check(links[0]->gainParameter->value.snapshot()==lockedGain,"LINK lock blocks remote manual gain without changing revision");links[0]->setLocalLock(false);service();
 // Persisted duplicate controllers cannot silently acquire the existing session.
 {auto duplicate=std::make_unique<GillMixProcessor>(GillMixKind::master);juce::MemoryBlock saved;master->getStateInformation(saved);duplicate->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));duplicate->service();master->service();check(master->status.contains("DUPLICATE SESSION")&&duplicate->status.contains("DUPLICATE SESSION"),"duplicate controller identity is explicitly visible and disarmed");duplicate->newIdentity();duplicate->service();master->service();check(!master->status.contains("DUPLICATE SESSION"),"NEW SESSION resolves controller identity ambiguity without adopting links");}
 for(auto*p:{master.get(),links[0].get()}){juce::MemoryBlock saved;p->getStateInformation(saved);for(const char*id:{"bypass","gillQuality"})p->apvts.getParameter(id)->setValueNotifyingHost(.39821f);p->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));check(p->apvts.getParameter("bypass")->getValue()==0&&p->apvts.getParameter("gillQuality")->getValue()==1,"valid recall restores actual fractional Boolean/choice host values, not only snapped APVTS cache");}
 for(auto*p:{master.get(),links[0].get()}){
  std::unique_ptr<juce::AudioProcessorEditor>editor(p->createEditor());const bool isMaster=p->kind==GillMixKind::master;check(editor->getWidth()==(isMaster?760:360)&&editor->getHeight()==(isMaster?480:210),"compact editor has intended logical dimensions");
  auto image=editor->createComponentSnapshot(editor->getLocalBounds());const auto file=juce::File::getCurrentWorkingDirectory().getChildFile(p->getName()+"-UI-"+juce::String(editor->getWidth())+"x"+juce::String(editor->getHeight())+".png");file.deleteFile();if(auto out=file.createOutputStream()){juce::PNGImageFormat png;check(png.writeImageToStream(image,*out),"real editor screenshot written");}else check(false,"screenshot output opened");
 }
 std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
