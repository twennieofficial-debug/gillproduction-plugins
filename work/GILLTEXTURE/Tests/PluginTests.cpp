#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
namespace {
int checks=0,failures=0;
std::atomic<int> allocations{0};
thread_local bool watch=false;
void check(bool ok,const juce::String& name,double value=0){++checks;if(!ok)++failures;std::printf("%s %s %.9g\n",ok?"PASS":"FAIL",name.toRawUTF8(),value);}
float signal(double t){return float(.15*std::sin(t*193*6.283185307179586)+.06*std::sin(t*579*6.283185307179586)+.03*std::sin(t*1737*6.283185307179586));}
struct Head final : juce::AudioPlayHead {
    double rate=48000,bpm=123;std::int64_t position=0;bool playing=true,known=true;
    juce::Optional<PositionInfo> getPosition()const override{if(!known)return{};PositionInfo p;p.setTimeInSamples(position);p.setPpqPosition(position/rate*bpm/60);p.setBpm(bpm);p.setIsPlaying(playing);return p;}
};
void process(GillTextureProcessor& p,Head& head,juce::AudioBuffer<float>& b,bool bypass=false){juce::MidiBuffer midi;watch=true;if(bypass)p.processBlockBypassed(b,midi);else p.processBlock(b,midi);watch=false;if(head.playing)head.position+=b.getNumSamples();}
void fill(juce::AudioBuffer<float>& b,Head& head){for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)b.setSample(c,i,signal((head.position+i)/head.rate)*(c?-.7f:1.f));}
bool finite(const juce::AudioBuffer<float>& b){for(int c=0;c<std::min(2,b.getNumChannels());++c)for(int i=0;i<b.getNumSamples();++i)if(!std::isfinite(b.getSample(c,i))||std::abs(b.getSample(c,i))>16)return false;return true;}
void gather(juce::Component& c,std::vector<juce::Component*>& out){for(auto* child:c.getChildren()){out.push_back(child);gather(*child,out);}}

void stateTests(GillTextureProcessor& p){
    const auto name=p.getName();std::set<juce::String> names;std::vector<std::vector<float>> programs;
    for(int i=0;i<6;++i){p.setCurrentProgram(i);names.insert(p.getProgramName(i));check(p.presetMatches(),name+" selected preset matches parameters");std::vector<float> values;for(auto* param:p.getParameters())values.push_back(param->getValue());check(std::find(programs.begin(),programs.end(),values)==programs.end(),name+" distinct preset values "+juce::String(i));programs.push_back(values);
        juce::MemoryBlock saved;p.getStateInformation(saved);p.setCurrentProgram((i+1)%6);p.setStateInformation(saved.getData(),int(saved.getSize()));bool same=p.getCurrentProgram()==i;for(std::size_t j=0;j<values.size();++j)same=same&&std::abs(p.getParameters()[int(j)]->getValue()-values[j])<1.e-6f;check(same&&p.presetMatches(),name+" all parameters and selected preset roundtrip");
    }
    check(names.size()==6,name+" six uniquely named presets");p.setCurrentProgram(0);p.setValue("output",-3.7f);check(!p.presetMatches(),name+" editing clears selected preset");juce::MemoryBlock saved;p.getStateInformation(saved);p.setCurrentProgram(1);p.setStateInformation(saved.getData(),int(saved.getSize()));check(!p.presetMatches()&&std::abs(p.value("output")+3.7f)<1.e-4f,name+" custom state survives recall");
    const auto before=p.value("output");const char invalid[]{1,2,3,4,5};p.setStateInformation(invalid,5);check(p.value("output")==before,name+" malformed state leaves settings unchanged");
    GillTextureProcessor other(p.kind==TextureKind::Grain?TextureKind::Pulse:TextureKind::Grain);juce::MemoryBlock foreign;other.getStateInformation(foreign);p.setStateInformation(foreign.getData(),int(foreign.getSize()));check(p.value("output")==before,name+" another product state rejected");
    p.setCurrentProgram(0);
}
void audioTests(GillTextureProcessor& p){
    const auto name=p.getName();Head head;p.setPlayHead(&head);
    for(double rate:{44100.,48000.,96000.,192000.})for(int channels:{1,2}){
        head.rate=rate;head.position=0;p.setPlayConfigDetails(channels,channels,rate,127);p.setCurrentProgram(0);p.prepareToPlay(rate,127);
        for(int mode:{0,1}){
            p.setValue("gillQuality",float(mode));bool good=true;double outputEnergy=0;
            for(int n:{1,17,127,512,2048}){juce::AudioBuffer<float> b(channels,n);for(int loop=0;loop<10;++loop){fill(b,head);process(p,head,b);good=good&&finite(b);for(int i=0;i<n;++i)outputEnergy+=b.getSample(0,i)*b.getSample(0,i);}}
            check(good&&outputEnergy>1.e-5,name+" finite audible variable blocks "+juce::String(int(rate))+" Hz "+juce::String(channels)+" ch mode "+juce::String(mode),outputEnergy);
            check(p.getLatencySamples()==0,name+" LIVE and PRO report zero added buffer");
            juce::AudioBuffer<float> b(channels,127);p.setValue("mix",0);p.setValue("output",0);p.prepareToPlay(rate,127);fill(b,head);std::vector<float> original(b.getReadPointer(0),b.getReadPointer(0)+127);process(p,head,b);check(std::equal(original.begin(),original.end(),b.getReadPointer(0)),name+" dry mix exact same-sample identity");
            p.setCurrentProgram(0);fill(b,head);original.assign(b.getReadPointer(0),b.getReadPointer(0)+127);process(p,head,b,true);check(std::equal(original.begin(),original.end(),b.getReadPointer(0)),name+" host bypass immediate exact identity");
            p.setValue("bypass",1);for(int n=0;n<int(rate*.4);n+=127){b.clear();process(p,head,b);}fill(b,head);original.assign(b.getReadPointer(0),b.getReadPointer(0)+127);process(p,head,b);check(std::equal(original.begin(),original.end(),b.getReadPointer(0)),name+" native bypass reaches exact identity");
            head.position-=1234;b.clear();b.setSample(0,13,.77f);process(p,head,b);check(b.getSample(0,13)==.77f,name+" bypass remains exact through transport seek");
            head.playing=false;b.clear();b.setSample(0,0,.43f);process(p,head,b);check(b.getSample(0,0)==.43f,name+" bypass remains exact on stop");head.playing=true;p.setCurrentProgram(0);
        }
        p.releaseResources();
    }
    p.setPlayConfigDetails(2,2,48000,127);head.rate=48000;head.position=0;p.setCurrentProgram(0);p.prepareToPlay(48000,127);juce::AudioBuffer<float> b(2,127);bool extremaGood=true;
    for(bool high:{false,true}){for(auto* param:p.getParameters()){auto* id=dynamic_cast<juce::AudioProcessorParameterWithID*>(param);if(id&&id->paramID!="bypass"&&id->paramID!="gillQuality")param->setValueNotifyingHost(high?1.f:0.f);}for(int i=0;i<300;++i){fill(b,head);process(p,head,b);extremaGood=extremaGood&&finite(b);}}
    check(extremaGood,name+" all controls at extrema remain finite");p.setCurrentProgram(0);head.known=false;for(int i=0;i<40;++i){fill(b,head);process(p,head,b);}check(!p.hostTempo&&finite(b),name+" missing transport uses valid fallback");head.known=true;
    b.clear();b.setSample(0,0,std::numeric_limits<float>::quiet_NaN());b.setSample(1,1,std::numeric_limits<float>::infinity());process(p,head,b);check(finite(b),name+" invalid input cannot poison output");
    p.setNonRealtime(true);fill(b,head);process(p,head,b);check(finite(b),name+" offline rendering uses same effect");p.setNonRealtime(false);p.setPlayHead(nullptr);
}
void shortHostBypassTest(){
    GillTextureProcessor p(TextureKind::Pulse);for(int i=0;i<16;++i)p.setValue("step"+juce::String(i+1),0);
    p.prepareToPlay(48000,17);Head head;juce::AudioBuffer<float> b(2,17);
    auto constant=[&]{for(int c=0;c<2;++c)for(int i=0;i<17;++i)b.setSample(c,i,.5f);};
    for(int i=0;i<100;++i){constant();process(p,head,b);}
    check(std::abs(b.getSample(0,16))<1.e-6f,"PULSE zero-step reference is silent before bypass test");
    constant();process(p,head,b,true);check(b.getSample(0,16)==.5f,"Single short host-bypass block is exactly dry");
    constant();process(p,head,b);check(std::abs(b.getSample(0,0)-.5f)<.002f,"Leaving short host bypass ramps from actual dry endpoint",b.getSample(0,0));
}
void carrierTests(){
    GillTextureProcessor p(TextureKind::Vocode);p.setCurrentProgram(5);p.setValue("unvoiced",100);p.setValue("mix",100);p.prepareToPlay(48000,127);Head head;juce::AudioBuffer<float> b(2,127);p.setPlayHead(&head);double energy=0;for(int i=0;i<250;++i){fill(b,head);process(p,head,b);for(int j=0;j<127;++j)energy+=b.getSample(0,j)*b.getSample(0,j);}check(energy==0&&!p.carrierConnected,"VOCODE absent external carrier gives exactly silent wet including consonants",energy);
    auto layout=p.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());check(p.setBusesLayout(layout),"VOCODE accepts stereo external carrier sidechain");p.prepareToPlay(48000,127);b.setSize(4,127);energy=0;bool preserved=true;for(int loop=0;loop<250;++loop){fill(b,head);std::array<float,127> carrier;for(int i=0;i<127;++i){carrier[i]=float(.2*std::sin((head.position+i)/48000.*6.283185307179586*330));b.setSample(2,i,carrier[i]);b.setSample(3,i,carrier[i]);}process(p,head,b);for(int i=0;i<127;++i){energy+=b.getSample(0,i)*b.getSample(0,i);preserved=preserved&&b.getSample(2,i)==carrier[i]&&b.getSample(3,i)==carrier[i];}}
    check(energy>1.e-4&&p.carrierConnected,"VOCODE connected external carrier creates audible wet",energy);check(preserved,"VOCODE never overwrites carrier bus");p.setPlayHead(nullptr);
}
void guiTests(GillTextureProcessor& p){
    p.setCurrentProgram(0);p.prepareToPlay(48000,127);juce::AudioBuffer<float> b(2,127);Head head;for(int i=0;i<200;++i){fill(b,head);process(p,head,b);}std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());const auto info=textureInfo(p.kind);check(editor->getWidth()==info.width&&editor->getHeight()==info.height,p.getName()+" exact catalog dimensions");
    std::vector<juce::Component*> components;gather(*editor,components);bool focus=!editor->getWantsKeyboardFocus(),fits=true;int knobs=0;juce::ComboBox* presets=nullptr;juce::ComboBox* patterns=nullptr;
    for(auto* c:components){if(dynamic_cast<juce::TextEditor*>(c)==nullptr)focus=focus&&!c->getWantsKeyboardFocus();if(c->isVisible())fits=fits&&editor->getLocalBounds().contains(editor->getLocalArea(c,c->getLocalBounds()));if(auto* slider=dynamic_cast<juce::Slider*>(c))if(slider->getComponentID().isNotEmpty()){const auto id=slider->getComponentID();auto* param=p.apvts.getParameter(id);check(param!=nullptr&&slider->getWidth()>20&&slider->getHeight()>20,p.getName()+" visible control has usable bounds and parameter "+id);if(param){slider->setValue(param->convertFrom0to1(.37f),juce::sendNotificationSync);check(std::abs(p.value(id)-float(slider->getValue()))<.011f,p.getName()+" visible control actually edits "+id);++knobs;}}if(auto* combo=dynamic_cast<juce::ComboBox*>(c)){if(combo->getComponentID()=="preset")presets=combo;if(combo->getComponentID()=="pattern")patterns=combo;}}
    check(focus,p.getName()+" normal controls preserve DAW keyboard");check(fits,p.getName()+" every visible control fits editor");check(knobs>=7,p.getName()+" meaningful sound controls wired");
    juce::Timer::callPendingTimersSynchronously();
    if(presets)for(int i=0;i<6;++i){presets->setSelectedId(0,juce::dontSendNotification);presets->setSelectedId(i+1,juce::sendNotificationSync);check(p.getCurrentProgram()==i&&p.presetMatches(),p.getName()+" UI preset selects real settings "+juce::String(i));}
    check(presets!=nullptr,p.getName()+" visible preset selector");
    if(p.kind==TextureKind::Pulse){check(patterns!=nullptr,"PULSE exposes four editable patterns");if(patterns){patterns->setSelectedId(2,juce::sendNotificationSync);check(p.value("step1")==0&&p.value("step3")==1,"PULSE pattern button changes actual step parameters");}}
    juce::TextEditor entry;editor->addAndMakeVisible(entry);entry.setWantsKeyboardFocus(true);check(entry.getWantsKeyboardFocus(),p.getName()+" text entry retains keyboard focus");editor->removeChildComponent(&entry);
    p.setCurrentProgram(0);juce::Thread::sleep(60);juce::Timer::callPendingTimersSynchronously();
    for(float scale:{1.f,1.5f}){auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);auto file=juce::File::getCurrentWorkingDirectory().getChildFile(p.getName()+"-UI-"+juce::String(image.getWidth())+"x"+juce::String(image.getHeight())+".png");auto stream=file.createOutputStream();if(stream){stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;check(png.writeImageToStream(image,*stream),p.getName()+" actual native editor screenshot");}else check(false,p.getName()+" editor screenshot file");}
}
}
void* operator new(std::size_t n){if(watch)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int main(){
#if JUCE_MAC
    gillInitialiseMacTestApplication();
#endif
    std::setvbuf(stdout,nullptr,_IONBF,0);juce::ScopedJuceInitialiser_GUI init;
    for(auto kind:{TextureKind::Vocode,TextureKind::Grain,TextureKind::Pulse}){GillTextureProcessor p(kind);stateTests(p);audioTests(p);guiTests(p);}carrierTests();shortHostBypassTest();
    check(allocations==0,"No heap allocations in actual audio callbacks",allocations);std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
