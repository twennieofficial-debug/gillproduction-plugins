#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <type_traits>
#include <vector>

namespace {
int checks=0,failures=0,uiEdits=0,audioCases=0;
void check(bool ok,const char* description){++checks;if(!ok){++failures;if(failures<=40)std::cerr<<"FAIL: "<<description<<'\n';}}
void set(GillDeEsserAudioProcessor& p,const char* id,float value){auto* parameter=p.apvts.getParameter(id);check(parameter!=nullptr,"parameter exists");if(parameter)parameter->setValueNotifyingHost(parameter->convertTo0to1(value));}
float raw(GillDeEsserAudioProcessor& p,const char* id){return p.apvts.getRawParameterValue(id)->load();}
constexpr std::array<const char*,4> ids{"amount","frequency","listen","bypass"};
std::array<float,4> values(GillDeEsserAudioProcessor& p){return {raw(p,"amount"),raw(p,"frequency"),raw(p,"listen"),raw(p,"bypass")};}
void stateFromTree(GillDeEsserAudioProcessor& p,const juce::ValueTree& tree){juce::MemoryBlock data;if(auto xml=tree.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,data);p.setStateInformation(data.getData(),static_cast<int>(data.getSize()));}
struct HostListener final:juce::AudioProcessorListener{
    int begins=0,ends=0,changes=0;
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float)override{++changes;}
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails&)override{}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*,int)override{++begins;}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*,int)override{++ends;}
};
void collect(juce::Component& component,std::vector<juce::Slider*>& sliders,std::vector<juce::Button*>& buttons,std::vector<juce::Component*>& all){
    all.push_back(&component);if(auto* slider=dynamic_cast<juce::Slider*>(&component))sliders.push_back(slider);if(auto* button=dynamic_cast<juce::Button*>(&component))buttons.push_back(button);
    for(auto* child:component.getChildren())collect(*child,sliders,buttons,all);
}
uint64_t imageHash(juce::Component& component){auto img=component.createComponentSnapshot(component.getLocalBounds());uint64_t result=1469598103934665603ull;for(int y=0;y<img.getHeight();++y)for(int x=0;x<img.getWidth();++x){result^=img.getPixelAt(x,y).getARGB();result*=1099511628211ull;}return result;}
void screenshot(juce::AudioProcessorEditor& editor,int width,int height){editor.setSize(width,height);auto img=editor.createComponentSnapshot(editor.getLocalBounds());juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile("GILL-DE-ESSER-UI-"+juce::String(width)+".png"));check(out.openedOk(),"snapshot file opens");if(!out.openedOk())return;out.setPosition(0);out.truncate();juce::PNGImageFormat png;check(png.writeImageToStream(img,out),"snapshot writes valid PNG");}
juce::MouseEvent mouse(juce::Component& target,juce::Point<float> point,int clicks=1,bool dragged=false,juce::ModifierKeys mods=juce::ModifierKeys::leftButtonModifier){const auto now=juce::Time::getCurrentTime();return {juce::Desktop::getInstance().getMainMouseSource(),point,mods,1,0,0,0,0,&target,&target,now,point,now,clicks,dragged};}

template<class T>void dryRoutes(double fs,int channels){
    for(int route=0;route<3;++route){GillDeEsserAudioProcessor processor;set(processor,"amount",route==0?0:100);set(processor,"frequency",9300);if(route==1)set(processor,"bypass",1);
        processor.setPlayConfigDetails(channels,channels,fs,512);processor.prepareToPlay(fs,512);check(processor.getLatencySamples()==0,"zero processing latency at every supported sample rate");
        check(processor.getTailLengthSeconds()>=0&&processor.getTailLengthSeconds()<=.25,"finite short filter tail is reported");juce::AudioBuffer<T> buffer(channels,128);juce::MidiBuffer midi;
        // Let the host-bypass crossfade finish on silence before testing exact dry samples.
        for(int i=0;i<32;++i){buffer.clear();if(route==2)processor.processBlockBypassed(buffer,midi);else processor.processBlock(buffer,midi);}
        const int sizes[]{1,17,64,127,257,511};int at=0;
        for(int block=0;block<18;++block){const int n=sizes[block%6];buffer.setSize(channels,n,false,false,true);std::vector<std::vector<T>> input(channels,std::vector<T>(n));
            for(int c=0;c<channels;++c)for(int i=0;i<n;++i)buffer.setSample(c,i,input[c][i]=static_cast<T>(.18*std::sin((.111+c*.071)*(at+i))+((at+i)%509==11?.67:0.0)));
            if(route==2)processor.processBlockBypassed(buffer,midi);else processor.processBlock(buffer,midi);
            for(int c=0;c<channels;++c)for(int i=0;i<n;++i)check(buffer.getSample(c,i)==input[c][i],"amount zero and both bypass routes are bit-exact undelayed dry");at+=n;
        }processor.releaseResources();
    }
}

template<class T>void listenRoute(double fs,int channels){
    GillDeEsserAudioProcessor processor;set(processor,"amount",100);set(processor,"frequency",6500);set(processor,"listen",1);processor.setPlayConfigDetails(channels,channels,fs,128);processor.prepareToPlay(fs,128);
    // Independently evaluate the public RBJ constant-peak bandpass recurrence.
    const double w=2*3.14159265358979323846*6500/fs,alpha=std::sin(w)/(2*.8),norm=1+alpha,b0=alpha/norm,b2=-alpha/norm,a1=-2*std::cos(w)/norm,a2=(1-alpha)/norm;
    std::array<std::array<double,4>,2> state{};juce::AudioBuffer<T> buffer(channels,127);juce::MidiBuffer midi;std::mt19937 random(901);std::uniform_real_distribution<double> noise(-.3,.3);
    for(int block=0;block<32;++block){std::array<std::array<T,127>,2> expected{};
        for(int i=0;i<127;++i)for(int c=0;c<channels;++c){const auto input=static_cast<T>(noise(random));buffer.setSample(c,i,input);auto& s=state[c];const auto y=b0*static_cast<double>(input)+b2*s[1]-a1*s[2]-a2*s[3];s[1]=s[0];s[0]=input;s[3]=s[2];s[2]=y;expected[c][i]=static_cast<T>(y);}
        processor.processBlock(buffer,midi);for(int c=0;c<channels;++c)for(int i=0;i<127;++i)check(std::abs(static_cast<double>(buffer.getSample(c,i))-expected[c][i])<(std::is_same_v<T,float>?2e-7:2e-12),"LISTEN S equals independently computed selected band");
    }
}

template<class T>void routeTransitions(bool useHostBypass,double fs){
    GillDeEsserAudioProcessor processor;set(processor,"amount",87);set(processor,"frequency",7800);processor.setPlayConfigDetails(2,2,fs,64);processor.prepareToPlay(fs,64);
    gilldeesser::DeEsserEngine reference;reference.prepare(fs);reference.setAmount(.87);reference.setFrequency(7800);
    juce::AudioBuffer<T> actual(2,64),wet(2,64),band(2,64);juce::MidiBuffer midi;std::mt19937 random(927);std::uniform_real_distribution<double> noise(-.45,.45);
    const int ramp=static_cast<int>(std::floor(.005*fs));
    auto phase=[ramp](int sample,int on,int off){if(sample<on)return 0.0;if(sample<on+ramp)return (sample-on+1)/static_cast<double>(ramp);if(sample<off)return 1.0;if(sample<off+ramp)return 1.0-(sample-off+1)/static_cast<double>(ramp);return 0.0;};
    for(int block=0;block<80;++block){const bool listening=block>=12&&block<30,bypassed=block>=22&&block<42;set(processor,"listen",listening?1:0);if(!useHostBypass)set(processor,"bypass",bypassed?1:0);
        std::array<std::array<T,64>,2> original{};for(int c=0;c<2;++c)for(int i=0;i<64;++i){original[c][i]=static_cast<T>(noise(random));actual.setSample(c,i,original[c][i]);wet.setSample(c,i,original[c][i]);}
        std::array<T*,2> wetPointers{wet.getWritePointer(0),wet.getWritePointer(1)},bandPointers{band.getWritePointer(0),band.getWritePointer(1)};
        reference.process(wetPointers.data(),2,64,bandPointers.data());if(useHostBypass&&bypassed)processor.processBlockBypassed(actual,midi);else processor.processBlock(actual,midi);
        for(int i=0;i<64;++i){const int sample=block*64+i;const double listenMix=phase(sample,12*64,30*64),bypassMix=phase(sample,22*64,42*64);
            for(int c=0;c<2;++c){const double processed=wet.getSample(c,i),selected=band.getSample(c,i),dry=original[c][i];const double audition=listenMix>=1?selected:processed+listenMix*(selected-processed);const auto expected=static_cast<T>(bypassMix>=1?dry:(bypassMix<=0?audition:audition+bypassMix*(dry-audition)));
                check(std::abs(static_cast<double>(actual.getSample(c,i))-expected)<(std::is_same_v<T,float>?2e-6:2e-12),"5 ms listen and bypass fades follow independent sample-index ramps and remain time aligned");}
        }check(processor.getLatencySamples()==0,"latency remains zero across listen and bypass transitions");
    }
}

void stateTests(){GillDeEsserAudioProcessor processor;set(processor,"amount",73.4f);set(processor,"frequency",8391);set(processor,"listen",1);set(processor,"bypass",1);const auto saved=values(processor);juce::MemoryBlock state;processor.getStateInformation(state);
    set(processor,"amount",2);set(processor,"frequency",3000);set(processor,"listen",0);set(processor,"bypass",0);processor.setStateInformation(state.getData(),static_cast<int>(state.getSize()));check(values(processor)==saved,"all four parameters survive exact state roundtrip");
    processor.setStateInformation(nullptr,8);processor.setStateInformation("junk",4);processor.setStateInformation(state.getData(),1024*1024+1);check(values(processor)==saved,"null malformed and oversized state is ignored");
    auto wrong=juce::ValueTree("OTHER_PLUGIN_STATE");auto wrongChild=juce::ValueTree("PARAM");wrongChild.setProperty("id","amount",nullptr);wrongChild.setProperty("value",3,nullptr);wrong.addChild(wrongChild,-1,nullptr);stateFromTree(processor,wrong);check(values(processor)==saved,"wrong product state root cannot change parameters");
    for(int direction:{-1,1}){auto tree=processor.apvts.copyState();for(const auto* id:ids)tree.getChildWithProperty("id",id).setProperty("value",direction*999999,nullptr);stateFromTree(processor,tree);check(raw(processor,"amount")==float(direction<0?0:100)&&raw(processor,"frequency")==float(direction<0?2500:12000)&&raw(processor,"listen")==float(direction<0?0:1)&&raw(processor,"bypass")==float(direction<0?0:1),"every saved parameter clamps to its documented range");}
    const auto before=values(processor);auto missing=processor.apvts.copyState();for(auto child:missing)child.removeProperty("value",nullptr);stateFromTree(processor,missing);check(values(processor)==before,"known parameters without values leave current settings intact");
    for(const char* poison:{"NaN","inf","-inf"}){auto tree=processor.apvts.copyState();for(auto child:tree)child.setProperty("value",poison,nullptr);stateFromTree(processor,tree);check(values(processor)==before,"non-finite saved parameter values are ignored");}
    auto partial=juce::ValueTree("GILL_DE_ESSER_STATE");auto child=juce::ValueTree("PARAM");child.setProperty("id","amount",nullptr);child.setProperty("value",42.6,nullptr);partial.addChild(child,-1,nullptr);stateFromTree(processor,partial);check(std::abs(raw(processor,"amount")-42.6)<.002&&raw(processor,"frequency")==before[1]&&raw(processor,"listen")==before[2]&&raw(processor,"bypass")==before[3],"partial state updates only supplied parameters");
}

void automationTests(){GillDeEsserAudioProcessor processor;processor.setPlayConfigDetails(2,2,48000,64);processor.prepareToPlay(48000,64);std::mt19937 random(0x57128);std::uniform_real_distribution<float> uniform(0,1);juce::AudioBuffer<float> audio(2,64);juce::MidiBuffer midi;
    for(int iteration=0;iteration<10000;++iteration){set(processor,"amount",100*uniform(random));set(processor,"frequency",2500+9500*uniform(random));set(processor,"listen",iteration%7==0?1:0);set(processor,"bypass",iteration%17==0?1:0);
        for(int c=0;c<2;++c)for(int i=0;i<64;++i)audio.setSample(c,i,(uniform(random)-.5f)*.8f);
        if(iteration%97==0)audio.setSample(0,31,std::numeric_limits<float>::quiet_NaN());if(iteration%101==0)audio.setSample(1,19,std::numeric_limits<float>::infinity());
        processor.processBlock(audio,midi);for(int c=0;c<2;++c)for(int i=0;i<64;++i)check(std::isfinite(audio.getSample(c,i)),"10000 real processor configurations remain finite, including malformed samples");
        check(processor.getLatencySamples()==0,"automation has zero fixed latency");check(std::isfinite(processor.getReductionDb())&&processor.getReductionDb()>=0&&processor.getReductionDb()<=12.001f,"reduction meter stays within the actual processing limit");check(std::isfinite(processor.getSibilanceDb()),"sibilance meter remains finite");++audioCases;
    }
    std::array<float,2048> pre{},post{};check(processor.readSpectrum(pre,post),"audio publishes a spectrum frame");for(size_t i=0;i<pre.size();++i)check(std::isfinite(pre[i])&&std::isfinite(post[i]),"published spectrum samples are finite");check(!processor.readSpectrum(pre,post),"spectrum mailbox is consumed exactly once");processor.releaseResources();
}

void uiTests(){
    GillDeEsserAudioProcessor processor;processor.setPlayConfigDetails(2,2,48000,128);processor.prepareToPlay(48000,128);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());check(editor!=nullptr,"editor exists");if(!editor)return;
    std::vector<juce::Slider*> sliders;std::vector<juce::Button*> buttons;std::vector<juce::Component*> components;collect(*editor,sliders,buttons,components);
    check(sliders.size()==2&&buttons.size()==1,"exactly two knobs and one listen button");check(editor->getWidth()==480&&editor->getHeight()==330,"compact 480 by 330 default size");
    const auto* limits=editor->getConstrainer();check(limits&&limits->getMinimumWidth()==480&&limits->getMinimumHeight()==330&&limits->getMaximumWidth()==960&&limits->getMaximumHeight()==660&&std::abs(limits->getFixedAspectRatio()-480./330.)<1e-12,"documented resize limits and fixed aspect ratio");
    HostListener listener;processor.addListener(&listener);
    for(auto* slider:sliders){const bool isAmount=slider->getName()=="AMOUNT";const char* id=isAmount?"amount":"frequency";
        check(isAmount||slider->getName()=="FREQUENCY","every rotary control has its documented accessible name");const double minimum=isAmount?0:2500,maximum=isAmount?100:12000,interval=isAmount?.1:1,initial=isAmount?55:6500;
        check(slider->getMinimum()==minimum&&slider->getMaximum()==maximum&&std::abs(slider->getInterval()-interval)<1e-7,"UI parameter range matches processor range");check(slider->getDoubleClickReturnValue()==initial,"knob has the documented default reset");
        const int initialBegins=listener.begins,initialEnds=listener.ends;
        for(int i=0;i<10000;++i){juce::Slider::ScopedDragNotification gesture(*slider);slider->setValue(isAmount?(i%1001)*.1:2500+(i%9501),juce::sendNotificationSync);check(std::abs(raw(processor,id)-slider->getValue())<(isAmount?.002:1.01),"10000 real edits of each knob update the DSP parameter");++uiEdits;}
        check(listener.begins==initialBegins+10000&&listener.ends==initialEnds+10000,"10000 edits of each rotary control emit balanced host gestures");
        const float hostValue=isAmount?47.3f:7251.f;const int starts=listener.begins,changes=listener.changes;set(processor,id,hostValue);
        check(std::abs(slider->getValue()-hostValue)<.01,"host automation synchronizes the real knob");check(listener.begins==starts&&listener.changes==changes+1,"host synchronization does not echo an edit or invent a gesture");
        const int arrowStarts=listener.begins,arrowEnds=listener.ends;check(slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"right arrow edits a focused knob");check(raw(processor,id)>hostValue,"right arrow increases the actual parameter");check(listener.begins==arrowStarts+1&&listener.ends==arrowEnds+1,"keyboard knob edit emits one balanced gesture");
        const auto beforeModified=raw(processor,id);const int modifiedStarts=listener.begins;slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey,juce::ModifierKeys::ctrlModifier,0));check(raw(processor,id)==beforeModified&&listener.begins==modifiedStarts,"modified arrow does not make an unintended knob edit");
        set(processor,id,static_cast<float>(maximum));const int noopBegins=listener.begins,noopEnds=listener.ends,noopChanges=listener.changes;slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));check(raw(processor,id)==maximum&&listener.begins==noopBegins&&listener.ends==noopEnds&&listener.changes==noopChanges,"arrow at upper limit is a true no-op with no host gesture");
        juce::Label* label=nullptr;for(auto* child:slider->getChildren())if(auto* text=dynamic_cast<juce::Label*>(child))label=text;
        check(label!=nullptr,"each knob has an editable numeric readout");if(label){
            label->setText(isAmount?"62,5 %":"6,75 KHZ",juce::sendNotificationSync);check(std::abs(raw(processor,id)-(isAmount?62.5:6750))<(isAmount?.002:1.01),"decimal comma numeric input respects percent or kHz units");
            label->setText(isAmount?"999 %":"999 KHZ",juce::sendNotificationSync);check(raw(processor,id)==maximum,"numeric input clamps above maximum");
            label->setText(isAmount?"-9 %":"1 KHZ",juce::sendNotificationSync);check(raw(processor,id)==minimum,"numeric input clamps below minimum");
        }
        set(processor,id,isAmount?79.f:9251.f);slider->mouseDoubleClick(mouse(*slider,{30,30},2));check(raw(processor,id)==initial,"real double-click handler restores the documented knob default");
        {juce::Slider::ScopedDragNotification gesture(*slider);const auto editsBefore=listener.changes;set(processor,id,isAmount?39.2f:8181.f);check(listener.changes==editsBefore+1&&std::abs(slider->getValue()-(isAmount?39.2:8181))<.01,"host update during a drag does not echo another parameter change");}
        set(processor,id,static_cast<float>(minimum));const auto empty=imageHash(*slider);set(processor,id,static_cast<float>(maximum));check(imageHash(*slider)!=empty,"real knob arc and value visibly react to the parameter");set(processor,id,static_cast<float>(minimum));check(imageHash(*slider)==empty,"knob rendering is deterministic at the same setting");set(processor,id,static_cast<float>(initial));
    }
    if(buttons.size()==1){auto* button=buttons[0];check(button->getName()=="LISTEN S","audition button has the documented accessible name");const int starts=listener.begins,ends=listener.ends;
        for(int i=0;i<10000;++i){button->setToggleState(!button->getToggleState(),juce::sendNotificationSync);check(raw(processor,"listen")==float(button->getToggleState()?1:0),"10000 real listen-button edits update the processor");++uiEdits;}
        check(listener.begins==starts+10000&&listener.ends==ends+10000,"listen-button edits emit one balanced host gesture each");const auto beforeHost=listener.begins,beforeChanges=listener.changes;set(processor,"listen",1);check(button->getToggleState()&&listener.begins==beforeHost&&listener.changes==beforeChanges+1,"host automation updates LISTEN S without an echo");set(processor,"listen",0);
    }
    juce::Component* graph=nullptr;for(auto* component:components)if(component->getName()=="S-BAND DISPLAY")graph=component;check(graph!=nullptr,"real interactive S-band display exists");
    if(graph){const auto xFor=[graph](double hz){return 37.f+static_cast<float>(std::log(hz/20.)/std::log(1000.))*(graph->getWidth()-49.f);};const int starts=listener.begins,ends=listener.ends;
        for(int i=0;i<10000;++i){const double first=2500+(i%9501),last=2500+((i*7)%9501);graph->mouseDown(mouse(*graph,{xFor(first),80}));graph->mouseDrag(mouse(*graph,{xFor(last),80},1,true));graph->mouseUp(mouse(*graph,{xFor(last),80},1,true,juce::ModifierKeys()));check(std::abs(raw(processor,"frequency")-last)<1.01,"10000 actual graph drag handlers set the selected S-band frequency");++uiEdits;}
        check(listener.begins==starts+10000&&listener.ends==ends+10000,"graph drags have balanced host automation gestures");
        set(processor,"frequency",7300);const auto keyStarts=listener.begins,keyEnds=listener.ends;check(graph->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"frequency graph accepts right arrow");check(std::abs(raw(processor,"frequency")-7400)<1.01&&listener.begins==keyStarts+1&&listener.ends==keyEnds+1,"graph keyboard movement advances by 100 Hz in a balanced gesture");
        set(processor,"frequency",12000);const auto noopBegins=listener.begins,noopEnds=listener.ends,noopChanges=listener.changes;graph->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));check(raw(processor,"frequency")==12000&&listener.begins==noopBegins&&listener.ends==noopEnds&&listener.changes==noopChanges,"graph arrow at upper limit is a true no-op");
        graph->mouseDoubleClick(mouse(*graph,{xFor(8100),80},2));check(raw(processor,"frequency")==6500,"graph double-click restores 6500 Hz");
        graph->mouseDown(mouse(*graph,{xFor(7600),80}));const int hostChanges=listener.changes;set(processor,"frequency",8150);check(listener.changes==hostChanges+1,"host update during graph dragging does not echo another edit");graph->mouseUp(mouse(*graph,{xFor(8150),80},1,true,juce::ModifierKeys()));
        const auto beforeRight=values(processor);const int beforeRightBegins=listener.begins;graph->mouseDown(mouse(*graph,{xFor(4500),80},1,false,juce::ModifierKeys::rightButtonModifier));graph->mouseDrag(mouse(*graph,{xFor(6000),80},1,true,juce::ModifierKeys::rightButtonModifier));graph->mouseUp(mouse(*graph,{xFor(6000),80},1,true,juce::ModifierKeys()));check(values(processor)==beforeRight&&listener.begins==beforeRightBegins,"right mouse button cannot accidentally move the S-band");
    }
    check(listener.begins==listener.ends,"all mouse keyboard text button and host-sync paths finish their automation gestures");processor.removeListener(&listener);
    set(processor,"amount",55);set(processor,"frequency",6500);set(processor,"listen",0);set(processor,"bypass",0);
    // Feed the real analyzer and meter before capturing the actual native UI.
    juce::AudioBuffer<float> audio(2,128);juce::MidiBuffer midi;std::mt19937 random(7123);std::uniform_real_distribution<float> noise(-.23f,.23f);
    for(int block=0;block<160;++block){for(int i=0;i<128;++i){const auto t=(block*128+i)/48000.;const float sample=static_cast<float>(.07*std::sin(2*3.14159265358979323846*220*t))+noise(random);audio.setSample(0,i,sample);audio.setSample(1,i,sample*.87f);}processor.processBlock(audio,midi);}
    juce::Thread::sleep(50);juce::Timer::callPendingTimersSynchronously();
    for(const auto size:std::array<std::pair<int,int>,3>{{{480,330},{720,495},{960,660}}}){screenshot(*editor,size.first,size.second);
        for(auto* component:components)if(component->getParentComponent()==editor.get())check(editor->getLocalBounds().contains(component->getBounds()),"every top-level control fits inside each supported editor size");
        for(auto* slider:sliders)for(auto* child:slider->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))check(slider->getLocalBounds().contains(label->getBounds())&&label->getWidth()>=110,"numeric readout stays inside the knob and legible at minimum size");
    }
    editor.reset();processor.releaseResources();
}
}
int main(){const auto start=std::chrono::steady_clock::now();juce::ScopedJuceInitialiser_GUI gui;
    GillDeEsserAudioProcessor metadata;check(metadata.getName()=="GILL-DE-ESSER","exact uppercase product name");check(metadata.getParameters().size()==4,"amount frequency listen and bypass are the four parameters");check(metadata.supportsDoublePrecisionProcessing(),"double precision is supported");check(metadata.getBypassParameter()==metadata.apvts.getParameter("bypass"),"host bypass is registered");check(values(metadata)==std::array<float,4>{55,6500,0,0},"documented default parameter values");
    for(double fs:{44100.,48000.,88200.,96000.,192000.})for(int channels:{1,2}){dryRoutes<float>(fs,channels);dryRoutes<double>(fs,channels);listenRoute<float>(fs,channels);listenRoute<double>(fs,channels);}
    for(bool host:{false,true})for(double fs:{44100.,48000.,192000.}){routeTransitions<float>(host,fs);routeTransitions<double>(host,fs);}
    stateTests();automationTests();uiTests();
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::ofstream report("plugin-integration-report.json");report<<"{\"passed\":"<<(failures==0?"true":"false")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_configurations\":"<<audioCases<<",\"ui_edits\":"<<uiEdits<<",\"elapsed_seconds\":"<<elapsed<<"}\n";
    std::cout<<checks<<" integration checks, "<<failures<<" failures, "<<audioCases<<" audio configurations, "<<uiEdits<<" UI edits, "<<elapsed<<" seconds\n";return failures?1:0;
}
