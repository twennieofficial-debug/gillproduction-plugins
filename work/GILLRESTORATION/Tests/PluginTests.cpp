#include "../../GILLCommon/QualityTests.h"
#include "PluginProcessor.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <type_traits>
#include <vector>

namespace {
int checks=0,failures=0,edits=0,audioCases=0;
void check(bool result,const char* description){++checks;if(!result){++failures;if(failures<30)std::cerr<<"FAIL: "<<description<<'\n';}}
void set(GillRestorationAudioProcessor& p,const char* id,float value){auto* param=p.apvts.getParameter(id);check(param!=nullptr,"parameter exists");if(param)param->setValueNotifyingHost(param->convertTo0to1(value));}
float raw(GillRestorationAudioProcessor& p,const char* id){return p.apvts.getRawParameterValue(id)->load();}
struct HostListener final:juce::AudioProcessorListener {
    int begins=0,ends=0,values=0;
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {++values;}
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*,int) override {++begins;}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*,int) override {++ends;}
};
void collect(juce::Component& c,std::vector<juce::Slider*>& sliders,std::vector<juce::Button*>& buttons){
    // LIVE/PRO has a separate full interaction test in QualityTests.h.
    if(dynamic_cast<gill::QualitySelector*>(&c))return;

    if(auto* s=dynamic_cast<juce::Slider*>(&c))sliders.push_back(s);
    if(auto* b=dynamic_cast<juce::Button*>(&c))buttons.push_back(b);
    for(auto* child:c.getChildren())collect(*child,sliders,buttons);
}
uint64_t hash(juce::Component& c){auto img=c.createComponentSnapshot(c.getLocalBounds());uint64_t result=1469598103934665603ull;for(int y=0;y<img.getHeight();++y)for(int x=0;x<img.getWidth();++x){result^=img.getPixelAt(x,y).getARGB();result*=1099511628211ull;}return result;}
void png(juce::AudioProcessorEditor& editor,int size,const juce::String& name){editor.setSize(size,size*300/320);auto img=editor.createComponentSnapshot(editor.getLocalBounds());juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile(name+"-UI-"+juce::String(size)+".png"));check(out.openedOk(),"snapshot file opens");if(!out.openedOk())return;out.setPosition(0);out.truncate();juce::PNGImageFormat format;check(format.writeImageToStream(img,out),"snapshot writes");}
template<class T>void dryRoutes(gillrestoration::Mode mode,double fs,int channels){
    const int previous=static_cast<int>(std::ceil(fs*(mode==gillrestoration::Mode::Declick?.004:.008)));
    const int gap=std::max(2,static_cast<int>(std::ceil(fs*(mode==gillrestoration::Mode::Declick?.00065:.00035))));
    const int delay=std::min(previous,gap+41*std::max(1,static_cast<int>(std::lround(fs/48000.0)))+2);
    const int total=delay+4096;
    for(int route=0;route<3;++route){
        GillRestorationAudioProcessor p(mode);set(p,"amount",route==0?0.f:100.f);if(route==1)set(p,"bypass",1);
        p.setPlayConfigDetails(channels,channels,fs,512);p.prepareToPlay(fs,512);
        check(p.getLatencySamples()==delay,"reported sample-rate-scaled latency");
        check(p.getTailLengthSeconds()>=static_cast<double>(delay)/fs,"reported tail includes all delayed samples");
        juce::AudioBuffer<T> b(channels,128);juce::MidiBuffer midi;
        for(int warm=0;warm<64;++warm){b.clear();if(route==2)p.processBlockBypassed(b,midi);else p.processBlock(b,midi);}
        std::vector<std::vector<T>> input(channels,std::vector<T>(total));
        for(int c=0;c<channels;++c)for(int i=0;i<total;++i)input[c][i]=static_cast<T>(.18*std::sin((.11+c*.07)*i)+(i==513?.65:0.0));
        const int blocks[]{1,17,64,127,257,511};int bi=0;
        for(int at=0;at<total;){const int n=std::min(blocks[(bi++)%6],total-at);b.setSize(channels,n,false,false,true);
            for(int c=0;c<channels;++c)for(int j=0;j<n;++j)b.setSample(c,j,input[c][at+j]);
            if(route==2)p.processBlockBypassed(b,midi);else p.processBlock(b,midi);
            for(int c=0;c<channels;++c)for(int j=0;j<n;++j)check(b.getSample(c,j)==(at+j<delay?T{}:input[c][at+j-delay]),"zero amount and both bypass routes are bit-exact delayed dry");at+=n;}
        p.releaseResources();
    }
}
template<class T>void bypassTransitions(gillrestoration::Mode mode,bool hostRoute){
    // A second, always-active instance provides the changing wet output; the
    // expected dry stream is computed independently from the reported delay.
    GillRestorationAudioProcessor active(mode), switched(mode);
    for(auto* p:{&active,&switched}){set(*p,"amount",100);p->setPlayConfigDetails(2,2,48000,64);p->prepareToPlay(48000,64);}
    const int delay=switched.getLatencySamples(),total=96*64;
    std::array<std::vector<T>,2> source{std::vector<T>(total),std::vector<T>(total)};
    for(int c=0;c<2;++c)for(int i=0;i<total;++i)source[c][i]=static_cast<T>(.13*std::sin((.071+c*.019)*i)+((i%509)==17?.72:0.0));
    juce::AudioBuffer<T> wet(2,64),actual(2,64);juce::MidiBuffer midi;
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> blend;blend.reset(48000,.005);blend.setCurrentAndTargetValue(0);
    bool previous=false;
    for(int block=0;block<96;++block){
        const bool bypass=block>=20&&block<40;
        if(!hostRoute&&bypass!=previous)set(switched,"bypass",bypass?1.f:0.f);
        previous=bypass;blend.setTargetValue(bypass?1.0:0.0);
        for(int c=0;c<2;++c)for(int n=0;n<64;++n){wet.setSample(c,n,source[c][block*64+n]);actual.setSample(c,n,source[c][block*64+n]);}
        active.processBlock(wet,midi);
        if(hostRoute&&bypass)switched.processBlockBypassed(actual,midi);else switched.processBlock(actual,midi);
        for(int n=0;n<64;++n){const double mix=blend.getNextValue();for(int c=0;c<2;++c){
            const int sourceIndex=block*64+n-delay;const double dry=sourceIndex<0?0.0:static_cast<double>(source[c][sourceIndex]);
            const double processed=static_cast<double>(wet.getSample(c,n));
            const auto expected=static_cast<T>(mix>=1?dry:(mix<=0?processed:processed+mix*(dry-processed)));
            check(std::abs(static_cast<double>(actual.getSample(c,n))-expected)<(std::is_same_v<T,float>?1.e-6:1.e-12),"bypass transition blends correctly aligned dry and active wet without a timing jump");
        }}
        check(switched.getLatencySamples()==delay,"bypass transition keeps reported latency constant");
    }
}
void run(gillrestoration::Mode mode){
    GillRestorationAudioProcessor p(mode);check(p.getParameters().size()==3,"amount, host bypass and appended LIVE/PRO");
    check(p.supportsDoublePrecisionProcessing(),"double precision supported");check(p.getBypassParameter()==p.apvts.getParameter("bypass"),"host bypass exposed");
    check(p.getName()==(mode==gillrestoration::Mode::Declick?"GILLDECLICK":"GILLDECRACKLE"),"mode has the correct uppercase product name");
    for(double fs:{44100.,48000.,96000.,192000.})for(int channels:{1,2}){dryRoutes<float>(mode,fs,channels);dryRoutes<double>(mode,fs,channels);}
    for(bool hostRoute:{false,true}){bypassTransitions<float>(mode,hostRoute);bypassTransitions<double>(mode,hostRoute);}
    set(p,"amount",73.4f);set(p,"bypass",1);juce::MemoryBlock state;p.getStateInformation(state);set(p,"amount",5);set(p,"bypass",0);
    p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));check(std::abs(raw(p,"amount")-73.4)<.002&&raw(p,"bypass")==1,"state roundtrip");
    p.setStateInformation(nullptr,8);p.setStateInformation("junk",4);p.setStateInformation(state.getData(),1024*1024+1);
    check(std::abs(raw(p,"amount")-73.4)<.002&&raw(p,"bypass")==1,"malformed state rejected");
    GillRestorationAudioProcessor other(mode==gillrestoration::Mode::Declick?gillrestoration::Mode::Decrackle:gillrestoration::Mode::Declick);
    other.setStateInformation(state.getData(),static_cast<int>(state.getSize()));check(raw(other,"amount")==55&&raw(other,"bypass")==0,"other product state cannot overwrite this product");
    auto tree=p.apvts.copyState();tree.getChildWithProperty("id","amount").setProperty("value",99999,nullptr);juce::MemoryBlock poison;if(auto xml=tree.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,poison);
    p.setStateInformation(poison.getData(),static_cast<int>(poison.getSize()));check(raw(p,"amount")==100,"oversized saved amount clamps");
    tree=p.apvts.copyState();tree.getChildWithProperty("id","amount").removeProperty("value",nullptr);poison.reset();
    if(auto xml=tree.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,poison);
    p.setStateInformation(poison.getData(),static_cast<int>(poison.getSize()));check(raw(p,"amount")==100,"known parameter without a saved value is ignored");
    tree=p.apvts.copyState();tree.getChildWithProperty("id","amount").setProperty("value",-99999,nullptr);poison.reset();
    if(auto xml=tree.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,poison);
    p.setStateInformation(poison.getData(),static_cast<int>(poison.getSize()));check(raw(p,"amount")==0,"negative saved amount clamps");
    p.setPlayConfigDetails(2,2,48000,64);p.prepareToPlay(48000,64);const int latency=p.getLatencySamples();
    std::mt19937 random(0x8172);std::uniform_real_distribution<float> uniform(0,1);juce::AudioBuffer<float> audio(2,64);juce::MidiBuffer midi;
    for(int i=0;i<10000;++i){set(p,"amount",uniform(random)*100);set(p,"bypass",i%17==0?1.f:0.f);
        for(int c=0;c<2;++c)for(int n=0;n<64;++n)audio.setSample(c,n,(uniform(random)-.5f)*.4f);
        p.processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=0;n<64;++n)check(std::isfinite(audio.getSample(c,n)),"automated actual processor remains finite");
        check(p.getLatencySamples()==latency,"automation latency constant");++audioCases;}
    set(p,"amount",55);set(p,"bypass",0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());check(editor!=nullptr,"editor exists");
    if(editor){std::vector<juce::Slider*> sliders;std::vector<juce::Button*> buttons;collect(*editor,sliders,buttons);
        check(sliders.size()==1&&buttons.empty(),"one effect control plus separately tested LIVE/PRO");
        check(editor->getWidth()==320&&editor->getHeight()==300,"compact 320 by 300 default");auto* limit=editor->getConstrainer();
        check(limit&&limit->getMinimumWidth()==320&&limit->getMinimumHeight()==300&&limit->getMaximumWidth()==640&&limit->getMaximumHeight()==600&&std::abs(limit->getFixedAspectRatio()-320./300.)<1e-12,"native to doubled compact aspect resizing");
        if(sliders.size()==1){auto* slider=sliders[0];check(slider->getName()=="AMOUNT","single named Amount knob");
            check(slider->getMinimum()==0&&slider->getMaximum()==100&&std::abs(slider->getInterval()-.1)<1.e-7,"knob exposes full amount range at tenth-percent resolution");
            check(slider->getDoubleClickReturnValue()==55,"double click resets to the documented amount");
            HostListener listener;p.addListener(&listener);
            for(int i=0;i<10000;++i){juce::Slider::ScopedDragNotification gesture(*slider);slider->setValue((i%1001)*.1,juce::sendNotificationSync);check(std::abs(raw(p,"amount")-slider->getValue())<.002,"real knob updates DSP parameter");++edits;}
            check(listener.begins==10000&&listener.ends==10000,"10000 knob edits emit balanced host automation gestures");
            const int startsBeforeHost=listener.begins,valuesBeforeHost=listener.values;
            set(p,"amount",47.3f);check(std::abs(slider->getValue()-47.3)<.002,"host automation updates knob");
            check(listener.begins==startsBeforeHost&&listener.values==valuesBeforeHost+1,"host synchronization does not echo a parameter edit or start a user gesture");
            const int startsBeforeArrow=listener.begins,endsBeforeArrow=listener.ends;
            check(slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"arrow key accepted");check(raw(p,"amount")>47.3f,"arrow edits amount");
            check(listener.begins==startsBeforeArrow+1&&listener.ends==endsBeforeArrow+1,"actual arrow edit emits a balanced host gesture");
            const auto beforeModified=raw(p,"amount");
            slider->keyPressed(juce::KeyPress(juce::KeyPress::rightKey,juce::ModifierKeys::ctrlModifier,0));
            check(raw(p,"amount")==beforeModified&&listener.begins==startsBeforeArrow+1,"modified unhandled arrow does not edit the parameter");
            juce::Label* label=nullptr;for(auto* child:slider->getChildren())if(auto* value=dynamic_cast<juce::Label*>(child))label=value;
            check(label!=nullptr,"editable numeric value");if(label){
                label->setText("62,5 %",juce::sendNotificationSync);check(std::abs(raw(p,"amount")-62.5)<.002,"numeric comma input");
                label->setText("999 %",juce::sendNotificationSync);check(raw(p,"amount")==100,"numeric input clamps above maximum");
                label->setText("-9 %",juce::sendNotificationSync);check(raw(p,"amount")==0,"numeric input clamps below minimum");
            }
            set(p,"amount",79);const auto time=juce::Time::getCurrentTime();const juce::Point<float> point{186,129};
            juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(),1,0,0,0,0,slider,slider,time,point,time,2,false);
            slider->mouseDoubleClick(doubleClick);check(raw(p,"amount")==55,"actual double click restores the default amount");
            {juce::Slider::ScopedDragNotification gesture(*slider);const auto valuesBeforeSync=listener.values;set(p,"amount",39.2f);check(listener.values==valuesBeforeSync+1&&std::abs(slider->getValue()-39.2)<.002,"host synchronization during a drag does not echo another change");}
            check(listener.begins==listener.ends,"text reset and host-during-drag paths keep gestures balanced");
            set(p,"amount",0);auto empty=hash(*slider);set(p,"amount",100);check(hash(*slider)!=empty,"live arc and readout change");set(p,"amount",0);check(hash(*slider)==empty,"deterministic live rendering");
            p.removeListener(&listener);
            set(p,"amount",55);for(int size:{320,480,640}){
                png(*editor,size,p.getName());
                check(editor->getLocalBounds().contains(slider->getBounds()),"knob fits the editor at every supported size");
                check(label&&slider->getLocalBounds().contains(label->getBounds())&&label->getWidth()>=130,"editable percentage remains legible inside the compact knob component");
            }
        }
        editor.reset();}
    p.releaseResources();
}
}
int main(){auto start=std::chrono::steady_clock::now();juce::ScopedJuceInitialiser_GUI gui;
    // Update06: exercise real LIVE/PRO host state, audio timing and UI.
    gill::testing::qualityRoutes([]{return std::make_unique<GillRestorationAudioProcessor>(gillrestoration::Mode::Declick);},[](bool ok,const std::string& why){check(ok,why.c_str());});
    gill::testing::qualityRoutes([]{return std::make_unique<GillRestorationAudioProcessor>(gillrestoration::Mode::Decrackle);},[](bool ok,const std::string& why){check(ok,why.c_str());});
run(gillrestoration::Mode::Declick);run(gillrestoration::Mode::Decrackle);
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream report("plugin-integration-report.json");report<<"{\"passed\":"<<(failures==0?"true":"false")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_configurations\":"<<audioCases<<",\"ui_edits\":"<<edits<<",\"elapsed_seconds\":"<<elapsed<<"}\n";
    std::cout<<checks<<" integration checks, "<<failures<<" failures, "<<edits<<" UI edits, "<<elapsed<<" seconds\n";return failures?1:0;}
