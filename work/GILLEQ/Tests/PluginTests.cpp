#include "PluginProcessor.h"
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

namespace {
int checks=0,failures=0,automationCases=0,uiChanges=0;
void check(bool pass,const char* name) {
    ++checks;if(!pass){++failures;static juce::StringArray reported;
        if(!reported.contains(name)){reported.add(name);std::cerr<<"FAIL: "<<name<<"\n";}}
}
void set(GilleqAudioProcessor& p,const juce::String& id,float value) {
    auto* param=p.apvts.getParameter(id); check(param!=nullptr,"parameter exists");
    if(param)param->setValueNotifyingHost(param->convertTo0to1(value));
}
void renderPng(juce::AudioProcessorEditor& editor,int w,int h,const char* file) {
    editor.setSize(w,h); auto img=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
    juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile(file));
    if (out.openedOk()) { out.setPosition(0); out.truncate(); }
    juce::PNGImageFormat png; check(out.openedOk() && png.writeImageToStream(img,out),"editor snapshot");
}
void collect(juce::Component& c,std::vector<juce::Slider*>& sliders,std::vector<juce::ComboBox*>& combos,std::vector<juce::Button*>& buttons) {
    if(auto* s=dynamic_cast<juce::Slider*>(&c))sliders.push_back(s);
    if(auto* s=dynamic_cast<juce::ComboBox*>(&c))combos.push_back(s);
    if(auto* s=dynamic_cast<juce::Button*>(&c))buttons.push_back(s);
    for(int i=0;i<c.getNumChildComponents();++i)collect(*c.getChildComponent(i),sliders,combos,buttons);
}
uint64_t componentHash(juce::Component& c) {
    const auto img=c.createComponentSnapshot(c.getLocalBounds());uint64_t hash=1469598103934665603ull;
    for(int y=0;y<img.getHeight();++y)for(int x=0;x<img.getWidth();++x){hash^=img.getPixelAt(x,y).getARGB();hash*=1099511628211ull;}
    return hash;
}
juce::String sliderSuffix(const juce::String& name) {
    if(name=="FREQUENCY")return "freq";
    return name.toLowerCase();
}
struct CallbackTimer final:juce::Timer {
    std::function<void()> callback;
    void timerCallback() override {if(callback)callback();}
};
juce::Component* named(juce::Component& root,const juce::String& name) {
    if(root.getName()==name)return &root;
    for(auto* child:root.getChildren())if(auto* found=named(*child,name))return found;
    return nullptr;
}
struct GestureListener final:juce::AudioProcessorListener {
    int starts=0,ends=0,lastStart=-1,lastEnd=-1;
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {}
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*,int index) override {++starts;lastStart=index;}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*,int index) override {++ends;lastEnd=index;}
};
juce::MouseEvent mouseAt(juce::Component& target,juce::Component& ancestor,juce::Point<float> position,juce::Point<float> down,bool dragged=false) {
    const auto time=juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),target.getLocalPoint(&ancestor,position),juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&target,&target,time,target.getLocalPoint(&ancestor,down),time,1,dragged};
}
}
int main() {
    const auto start=std::chrono::steady_clock::now();
    juce::ScopedJuceInitialiser_GUI gui;
    GilleqAudioProcessor p;
    check(p.getParameters().size()==99,"59 existing and 40 appended dynamic parameters");
    int legacyIndex=0;
    for(int band=1;band<=8;++band)for(auto suffix:{"enabled","type","freq","gain","q","channel","slope"})
        check(p.getParameters()[legacyIndex++]==p.apvts.getParameter("band"+juce::String(band)+"_"+suffix),"legacy band automation index remains unchanged");
    for(auto id:{"output","bypass","delta"})check(p.getParameters()[legacyIndex++]==p.apvts.getParameter(id),"legacy global automation index remains unchanged");
    check(p.getName()=="GILLEQ","uppercase product name");
    check(p.supportsDoublePrecisionProcessing(),"64bit processing");
    p.setPlayConfigDetails(2,2,48000,256); p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> b(2,256); juce::MidiBuffer midi;
    b.clear(); b.setSample(0,0,0.25f); b.setSample(1,0,-0.2f); p.processBlock(b,midi);
    check(b.getSample(0,0)==0.25f && b.getSample(1,0)==-0.2f,"flat exact null");
    check(p.getLatencySamples()==0,"zero latency");
    check(std::abs(p.getResponseDb(1000))<1.e-10,"flat graph zero");
    set(p,"band4_freq",1000); set(p,"band4_gain",6); set(p,"band4_q",1.4f);
    check(std::abs(p.getResponseDb(1000)-6)<0.0001,"peak gain graph");
    p.copyAtoB(); set(p,"band4_gain",-9);p.swapAB();check(std::abs(p.apvts.getRawParameterValue("band4_gain")->load()-6)<0.001,"A/B restore");
    p.swapAB();check(std::abs(p.apvts.getRawParameterValue("band4_gain")->load()+9)<0.001,"A/B second direction");
    juce::MemoryBlock state;p.getStateInformation(state);std::vector<float> remembered;
    for(auto* par:p.getParameters())remembered.push_back(par->getValue());
    p.resetAllBands();p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    for(int i=0;i<p.getParameters().size();++i)check(std::abs(p.getParameters()[i]->getValue()-remembered[static_cast<size_t>(i)])<1.e-6,"state roundtrip");
    p.setStateInformation(nullptr,20);p.setStateInformation("junk",4);check(true,"malformed state handled");
    // Simulate a saved v0.1 project with only the original 59 parameters.
    auto legacy=p.apvts.copyState();legacy.setProperty("version",1,nullptr);
    for(int i=legacy.getNumChildren()-1;i>=0;--i) {
        const auto id=legacy.getChild(i).getProperty("id").toString();
        if(id.endsWith("_dynamic")||id.endsWith("_threshold")||id.endsWith("_range")||id.endsWith("_attack")||id.endsWith("_release"))legacy.removeChild(i,nullptr);
    }
    juce::MemoryBlock legacyState;if(auto xml=legacy.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,legacyState);
    for(int band=1;band<=8;++band){set(p,"band"+juce::String(band)+"_dynamic",1);set(p,"band"+juce::String(band)+"_range",18);}
    p.setStateInformation(legacyState.getData(),static_cast<int>(legacyState.getSize()));
    for(int band=1;band<=8;++band) {
        const auto prefix="band"+juce::String(band)+"_";
        check(p.apvts.getRawParameterValue(prefix+"dynamic")->load()==0,"old project loads with dynamics off");
        check(p.apvts.getRawParameterValue(prefix+"range")->load()==-6,"old project resets previously edited dynamics to defaults");
    }
    {
        // This exact physical value was captured from the released v0.1 VST3.
        // It differs from the Q constructor literal after JUCE's skew mapping.
        // Compare against v0.1's actual loading operation (direct replaceState),
        // not a tolerance that would hide another normalise/denormalise pass.
        GilleqAudioProcessor originalLoader,migrated;
        auto physicalLegacy=originalLoader.apvts.copyState();physicalLegacy.setProperty("version",1,nullptr);
        for(int i=physicalLegacy.getNumChildren()-1;i>=0;--i) {
            auto child=physicalLegacy.getChild(i);const auto id=child.getProperty("id").toString();
            if(id.endsWith("_dynamic")||id.endsWith("_threshold")||id.endsWith("_range")||id.endsWith("_attack")||id.endsWith("_release"))physicalLegacy.removeChild(i,nullptr);
            else if(id.endsWith("_q"))child.setProperty("value",0.7071068286895752,nullptr);
        }
        juce::MemoryBlock captured;if(auto xml=physicalLegacy.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,captured);
        originalLoader.apvts.replaceState(physicalLegacy.createCopy());
        migrated.setStateInformation(captured.getData(),static_cast<int>(captured.getSize()));
        for(int pass=0;pass<2;++pass) {
            for(int band=1;band<=8;++band) {
                const auto id="band"+juce::String(band)+"_q";
                const auto* expected=originalLoader.apvts.getParameter(id);const auto* actual=migrated.apvts.getParameter(id);
                check(originalLoader.apvts.getRawParameterValue(id)->load()==migrated.apvts.getRawParameterValue(id)->load(),"captured legacy default Q restores with zero float ULP drift");
                check(expected->getValue()==actual->getValue(),"captured legacy Q normalized value is exact");
                check(expected->getText(expected->getValue(),128)==actual->getText(actual->getValue(),128),"captured legacy Q host physical text is exact");
            }
            migrated.getStateInformation(captured);
            migrated.setStateInformation(captured.getData(),static_cast<int>(captured.getSize()));
        }
    }
    set(p,"band4_dynamic",1);set(p,"band4_threshold",-39.5f);set(p,"band4_range",8.4f);set(p,"band4_attack",2.3f);set(p,"band4_release",357);
    p.getStateInformation(state);p.resetAllBands();p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    check(p.apvts.getRawParameterValue("band4_dynamic")->load()==1,"new project preserves dynamic mode");
    check(std::abs(p.apvts.getRawParameterValue("band4_threshold")->load()+39.5f)<0.001,"new project preserves editable threshold");
    check(std::abs(p.apvts.getRawParameterValue("band4_range")->load()-8.4f)<0.001,"new project preserves positive range");
    check(std::abs(p.apvts.getRawParameterValue("band4_attack")->load()-2.3f)<0.001&&p.apvts.getRawParameterValue("band4_release")->load()==357,"new project preserves attack and release");
    p.resetAllBands();set(p,"delta",1);check(p.getResponseDb(1000)<=-200,"delta graph exact null");
    for(int block=0;block<20;++block){b.clear();b.setSample(0,0,0.1f);p.processBlock(b,midi);}
    check(b.getMagnitude(0,0,256)==0,"delta audio exact null after ramp");
    p.resetAllBands();set(p,"band1_gain",24);set(p,"output",24);set(p,"bypass",1);
    for(int block=0;block<20;++block){b.clear();b.setSample(0,0,0.12345f);p.processBlock(b,midi);}
    check(b.getSample(0,0)==0.12345f && b.getMagnitude(0,1,255)==0,"bypass exact dry");
    p.resetAllBands();
    std::mt19937 random(0x47494c4c);std::uniform_real_distribution<float> uniform(0,1);
    b.setSize(2,64);p.prepareToPlay(48000,64);
    for(int iteration=0;iteration<10000;++iteration) {
        for(auto* par:p.getParameters()) {
            float v=uniform(random);if(iteration%257==0)v=0;if(iteration%263==0)v=1;
            par->setValueNotifyingHost(v);
            check(std::isfinite(par->getValue()) && par->getValue()>=0 && par->getValue()<=1,"automation range");
        }
        for(int c=0;c<2;++c)for(int i=0;i<64;++i)b.setSample(c,i,(uniform(random)-0.5f)*0.05f);
        p.processBlock(b,midi);
        for(int c=0;c<2;++c)for(int i=0;i<64;++i)check(std::isfinite(b.getSample(c,i)),"automation finite audio");
        check(std::isfinite(p.getResponseDb(997,iteration%5)),"automation finite response");
        for(int band=0;band<8;++band) {
            check(std::isfinite(p.getBandDetectorDb(band)),"automation finite detector snapshot");
            check(std::isfinite(p.getBandDynamicGainDb(band))&&std::abs(p.getBandDynamicGainDb(band))<=24.001f,"automation bounded actual dynamic gain snapshot");
        }
        ++automationCases;
    }
    p.resetAllBands(); p.releaseResources();
    p.setPlayConfigDetails(1,1,44100,128);p.prepareToPlay(44100,128);
    set(p,"band4_channel",gill::Side);set(p,"band4_gain",12);set(p,"band4_freq",1000);
    check(std::abs(p.getResponseDb(1000))<1.e-8,"mono ignores side");
    set(p,"band4_channel",gill::Right);check(std::abs(p.getResponseDb(1000))<1.e-8,"mono ignores right");
    p.resetAllBands();p.releaseResources();p.setPlayConfigDetails(2,2,48000,128);p.prepareToPlay(48000,128);
    juce::AudioBuffer<double> bd(2,128);bd.clear();bd.setSample(0,0,0.123456789012345);p.processBlock(bd,midi);
    check(bd.getSample(0,0)==0.123456789012345,"double precision exact null");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    check(editor!=nullptr,"editor creation");
    if(editor) {
        // A host shows its editor. JUCE getComponentAt intentionally rejects
        // invisible roots, even when snapshot rendering still paints them.
        // This creates no desktop peer and opens no native window.
        editor->setVisible(true);
        std::vector<juce::Slider*> sliders;std::vector<juce::ComboBox*> combos;std::vector<juce::Button*> buttons;
        collect(*editor,sliders,combos,buttons);check(sliders.size()==8,"four EQ controls and four graph value fields");check(combos.size()==3,"three filter selectors");
        auto buttonNamed=[&](const juce::String& name)->juce::Button* {
            for(auto* button:buttons)if(button->getButtonText()==name)return button;
            check(false,"required UI button exists");return nullptr;
        };
        auto sliderNamed=[&](const juce::String& name)->juce::Slider* {
            for(auto* slider:sliders)if(slider->getName()==name)return slider;
            check(false,"required UI slider exists");return nullptr;
        };
        auto command=[&](juce::Button* button) {
            check(button && static_cast<bool>(button->onClick),"UI command callback connected");
            // Invoke the actual command callback synchronously: triggerClick posts
            // an OS message and would not be delivered in this bounded test.
            if(button && button->onClick){button->onClick();++uiChanges;}
        };
        auto toggle=[&](juce::Button* button,bool on) {
            check(button!=nullptr,"UI toggle exists");
            if(button){button->setToggleState(on,juce::sendNotificationSync);++uiChanges;}
        };
        auto raw=[&](const juce::String& id){return p.apvts.getRawParameterValue(id)->load();};
        auto* frequencyControl=sliderNamed("FREQUENCY");auto* gainControl=sliderNamed("GAIN");
        auto* qControl=sliderNamed("Q");auto* outputControl=sliderNamed("OUTPUT");
        auto* bandEnabled=buttonNamed("ON");auto* resetButton=buttonNamed("RESET");
        auto* copyButton=buttonNamed("A > B");auto* swapButton=buttonNamed("A / B");
        auto* deltaButton=buttonNamed("DELTA LISTEN");auto* bypassButton=buttonNamed("BYPASS");
        auto* analyzerButton=buttonNamed("ANALYZER");auto* freezeButton=buttonNamed("FREEZE");
        auto* dynamicButton=buttonNamed("STATIC");
        const std::array<juce::Slider*,4> dynamicControls { sliderNamed("THRESHOLD"),sliderNamed("RANGE"),sliderNamed("ATTACK"),sliderNamed("RELEASE") };
        auto* graph=named(*editor,"RESPONSE GRAPH");auto* rangeHandle=named(*editor,"RANGE HANDLE");
        auto* thresholdSurface=named(*editor,"THRESHOLD DETECTOR");auto* dynamicsMeter=named(*editor,"BAND DYNAMICS METER");
        check(graph&&rangeHandle&&thresholdSurface&&dynamicsMeter,"graph contains direct range threshold and band-card controls");
        for(auto* control:dynamicControls)check(control&&graph&&graph->isParentOf(control),"all dynamic values belong inside the frequency preview");
        for(auto* control:dynamicControls)check(control&&!control->isVisible(),"static band hides dynamic controls");
        check(dynamicButton&&dynamicButton->isEnabled(),"bell band supports dynamic mode");
        toggle(dynamicButton,true);check(raw("band4_dynamic")==1,"DYNAMIC switch updates selected band parameter");
        for(auto* control:dynamicControls)check(control&&control->isVisible(),"dynamic band reveals editable controls");
        std::array<juce::Button*,8> bandSelectors{};
        for(int i=0;i<8;++i)bandSelectors[static_cast<size_t>(i)]=buttonNamed(juce::String(i+1));

        // All eleven continuous/discrete editors must update their APVTS bindings,
        // not merely their own displayed value, through 10,000 changes each.
        for(auto* slider:sliders) {
            const auto name=slider->getName();
            const auto id=name=="OUTPUT"?juce::String("output"):juce::String("band4_")+sliderSuffix(name);
            check(p.apvts.getParameter(id)!=nullptr,"UI slider binding identified");
            for(int i=0;i<10000;++i){slider->setValue(slider->proportionOfLengthToValue((i%1001)/1000.0),juce::sendNotificationSync);check(std::isfinite(slider->getValue()),"UI slider finite");check(std::abs(raw(id)-slider->getValue())<(name=="FREQUENCY"?0.03:0.002),"UI slider updates bound parameter");++uiChanges;}
        }
        for(int i=0;i<10000;++i) {
            toggle(dynamicButton,(i%2)==0);
            check(raw("band4_dynamic")==((i%2)==0?1.0f:0.0f),"10000 mode switches bind selected dynamic parameter");
            for(auto* control:dynamicControls)check(control&&control->isVisible()==((i%2)==0),"mode switch controls visibility synchronously");
        }
        for(auto* combo:combos) {
            const auto name=combo->getName();
            const auto id=juce::String("band4_")+(name=="TYPE"?"type":name=="CHANNEL"?"channel":"slope");
            check(name=="TYPE"||name=="CHANNEL"||name=="SLOPE","UI selector binding identified");
            for(int i=0;i<10000;++i){combo->setSelectedItemIndex(i%combo->getNumItems(),juce::sendNotificationSync);check(combo->getSelectedId()>0,"UI selector valid");check(std::abs(raw(id)-combo->getSelectedItemIndex())<0.001,"UI selector updates bound parameter");++uiChanges;}
        }
        command(resetButton);
        for(int band=0;band<8;++band) {
            toggle(bandSelectors[static_cast<size_t>(band)],true);
            int activeSelectors=0;for(auto* selector:bandSelectors)if(selector && selector->getToggleState())++activeSelectors;
            check(activeSelectors==1,"band selectors stay mutually exclusive");
            const auto prefix="band"+juce::String(band+1)+"_";
            if(frequencyControl){frequencyControl->setValue(200+band*317,juce::sendNotificationSync);check(std::abs(raw(prefix+"freq")-(200+band*317))<0.03,"band selector rebinds frequency");}
            if(gainControl){gainControl->setValue(-7.5+band,juce::sendNotificationSync);check(std::abs(raw(prefix+"gain")-(-7.5+band))<0.002,"band selector rebinds gain");}
            if(qControl){qControl->setValue(0.5+band*0.25,juce::sendNotificationSync);check(std::abs(raw(prefix+"q")-(0.5+band*0.25))<0.002,"band selector rebinds Q");}
            toggle(dynamicButton,true);check(raw(prefix+"dynamic")==1,"band selector rebinds dynamic mode");
            const std::array<double,4> editValues { -40.0+band, -12.0+band, 3.0+band, 180.0+band*20 };
            for(size_t index=0;index<dynamicControls.size();++index)if(auto* control=dynamicControls[index]) {
                check(control->isVisible(),"selected dynamic band reveals its controls");
                const auto id=prefix+sliderSuffix(control->getName());
                const auto otherId="band"+juce::String((band+1)%8+1)+"_"+sliderSuffix(control->getName());
                const auto untouched=raw(otherId);
                control->setValue(editValues[index],juce::sendNotificationSync);
                check(std::abs(raw(id)-editValues[index])<0.002,"band selection rebinds each dynamics control");
                check(raw(otherId)==untouched,"selected dynamics edit does not change another band");
                set(p,id,static_cast<float>(editValues[index]+1));
                check(std::abs(control->getValue()-editValues[index]-1)<0.002,"host automation updates selected dynamics readout");
            }
            toggle(bandEnabled,false);check(raw(prefix+"enabled")==0,"band power disables selected band");
            toggle(bandEnabled,true);check(raw(prefix+"enabled")==1,"band power enables selected band");
            set(p,prefix+"freq",777+band);check(frequencyControl && std::abs(frequencyControl->getValue()-(777+band))<0.03,"parameter automation updates selected knob");
            for(auto* combo:combos){const auto suffix=combo->getName()=="TYPE"?"type":combo->getName()=="CHANNEL"?"channel":"slope";combo->setSelectedItemIndex(1,juce::sendNotificationSync);check(raw(prefix+suffix)==1,"band selector rebinds dropdown");}
            for(auto* combo:combos)if(combo->getName()=="TYPE") {
                for(int unsupported=3;unsupported<=5;++unsupported) {
                    combo->setSelectedItemIndex(unsupported,juce::sendNotificationSync);
                    check(dynamicButton&&!dynamicButton->isEnabled()&&dynamicButton->getButtonText()=="STATIC ONLY","cuts and notch clearly disable dynamic mode");
                    for(auto* control:dynamicControls)check(control&&!control->isVisible(),"unsupported filter hides nonfunctional dynamics controls");
                    check(raw(prefix+"dynamic")==1,"temporary filter change preserves stored dynamic preference");
                }
                combo->setSelectedItemIndex(0,juce::sendNotificationSync);
                check(dynamicButton&&dynamicButton->isEnabled(),"return to bell restores editable dynamic mode");
                for(auto* control:dynamicControls)check(control&&control->isVisible(),"return to supported type restores dynamic controls");
            }
        }
        command(resetButton);toggle(bandSelectors[3],true);toggle(dynamicButton,true);
        GestureListener gestures;p.addListener(&gestures);
        if(graph&&rangeHandle&&thresholdSurface&&dynamicsMeter) {
            for(const auto size:std::array<juce::Point<int>,3>{juce::Point<int>(920,660),{1120,760},{1600,1000}}) {
                editor->setSize(size.x,size.y);
                for(float hz:{20.f,1000.f,20000.f})for(float base:{-24.f,0.f,24.f})for(float range:{-24.f,0.f,24.f}) {
                    set(p,"band4_freq",hz);set(p,"band4_gain",base);set(p,"band4_range",range);
                    const auto handleCentre=editor->getLocalPoint(rangeHandle,rangeHandle->getLocalBounds().toFloat().getCentre());
                    const auto detectorCentre=editor->getLocalPoint(thresholdSurface,thresholdSurface->getLocalBounds().toFloat().getCentre());
                    check(editor->getComponentAt(handleCentre.roundToInt())==rangeHandle,"range endpoint has an actual hit target at all frequency gain and resize edges");
                    check(editor->getComponentAt(detectorCentre.roundToInt())==thresholdSurface,"threshold detector remains reachable independently of EQ nodes");
                    check(graph->getLocalBounds().contains(rangeHandle->getBounds()),"range endpoint stays inside graph including 20 Hz and 20 kHz");
                    check(graph->getLocalBounds().contains(dynamicsMeter->getBounds()),"band card stays inside graph at upper and lower gain limits");
                    check(!dynamicsMeter->getBounds().intersects(rangeHandle->getBounds()),"band card never covers the draggable range endpoint");
                }
            }
            editor->setSize(1120,760);set(p,"band4_freq",1000);set(p,"band4_gain",0);set(p,"band4_range",-6);
            auto down=editor->getLocalPoint(rangeHandle,rangeHandle->getLocalBounds().toFloat().getCentre());
            const int starts=gestures.starts,ends=gestures.ends;
            rangeHandle->mouseDown(mouseAt(*rangeHandle,*editor,down,down));
            rangeHandle->mouseDrag(mouseAt(*rangeHandle,*editor,down.translated(0,25),down,true));
            rangeHandle->mouseUp(mouseAt(*rangeHandle,*editor,down.translated(0,25),down,true));
            check(raw("band4_range")<-6&&raw("band4_gain")==0&&raw("band4_freq")==1000,"dragging graph range changes only the selected dynamic range");
            check(gestures.starts==starts+1&&gestures.ends==ends+1&&gestures.lastStart==p.apvts.getParameter("band4_range")->getParameterIndex()&&gestures.lastEnd==gestures.lastStart,"range drag sends one balanced gesture for the exact parameter");
            for(float dy:{-3000.f,3000.f}) {
                down=editor->getLocalPoint(rangeHandle,rangeHandle->getLocalBounds().toFloat().getCentre());
                rangeHandle->mouseDown(mouseAt(*rangeHandle,*editor,down,down));
                rangeHandle->mouseDrag(mouseAt(*rangeHandle,*editor,down.translated(0,dy),down,true));
                rangeHandle->mouseUp(mouseAt(*rangeHandle,*editor,down.translated(0,dy),down,true));
                check(raw("band4_range")== (dy<0?24.f:-24.f),"captured range drag clamps correctly outside the graph");
            }
            down=editor->getLocalPoint(thresholdSurface,thresholdSurface->getLocalBounds().toFloat().getCentre());
            thresholdSurface->mouseDown(mouseAt(*thresholdSurface,*editor,down,down));
            check(std::abs(raw("band4_threshold")+40)<.101,"threshold inset midpoint maps to -40 RMS dBFS, independently of the EQ gain axis");
            thresholdSurface->mouseDrag(mouseAt(*thresholdSurface,*editor,down.translated(-1000,0),down,true));
            check(raw("band4_threshold")==-80,"threshold drag clamps at -80 dBFS");
            thresholdSurface->mouseDrag(mouseAt(*thresholdSurface,*editor,down.translated(1000,0),down,true));
            thresholdSurface->mouseUp(mouseAt(*thresholdSurface,*editor,down.translated(1000,0),down,true));
            check(raw("band4_threshold")==0&&raw("band4_gain")==0,"threshold upper endpoint is 0 dBFS and leaves EQ gain unchanged");
            check(gestures.starts==gestures.ends,"all actual graph drags keep host gestures balanced");
            for(auto* direct:{rangeHandle,thresholdSurface}) {
                const char* id=direct==rangeHandle?"band4_range":"band4_threshold";set(p,id,direct==rangeHandle?-6.f:-24.f);
                const float before=raw(id);const int startsBefore=gestures.starts;
                check(direct->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"direct graph target accepts keyboard precision editing");
                check(std::abs(raw(id)-before-.1f)<.002,"direct graph arrow changes parameter by exactly 0.1 dB");
                check(gestures.starts==startsBefore+1&&gestures.starts==gestures.ends,"direct keyboard edit sends a balanced host gesture");
            }
            for(auto* field:dynamicControls)if(field) {
                const auto id=juce::String("band4_")+sliderSuffix(field->getName());
                const auto before=raw(id);const int startsBefore=gestures.starts;
                check(field->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"numeric graph field accepts keyboard editing");
                check(raw(id)>before&&gestures.starts==startsBefore+1&&gestures.starts==gestures.ends,"graph numeric arrow updates binding and sends balanced host gesture");
                juce::Label* label=nullptr;for(auto* child:field->getChildren())if(auto* found=dynamic_cast<juce::Label*>(child))label=found;
                check(label!=nullptr,"graph numeric field has direct editable text fallback");
                const bool threshold=field->getName()=="THRESHOLD",range=field->getName()=="RANGE";
                const double target=threshold?-37.5:range?-8.5:field->getName()=="ATTACK"?6.5:225.0;
                if(label)label->setText(juce::String(target,1).replaceCharacter('.',',')+(threshold?" DBFS":range?" DB":" MS"),juce::sendNotificationSync);
                check(std::abs(raw(id)-target)<.002,"precise graph numeric text accepts decimal comma and units");
            }
            // Selecting another band during a direct drag must close the old
            // gesture before the shared controls are rebound.
            down=editor->getLocalPoint(thresholdSurface,thresholdSurface->getLocalBounds().toFloat().getCentre());
            thresholdSurface->mouseDown(mouseAt(*thresholdSurface,*editor,down,down));
            const auto thresholdBeforeStatic=raw("band4_threshold");toggle(dynamicButton,false);
            thresholdSurface->mouseDrag(mouseAt(*thresholdSurface,*editor,down.translated(500,0),down,true));
            thresholdSurface->mouseUp(mouseAt(*thresholdSurface,*editor,down.translated(500,0),down,true));
            check(raw("band4_threshold")==thresholdBeforeStatic&&gestures.starts==gestures.ends,"switching to static closes a captured threshold gesture without an extra edit");
            toggle(dynamicButton,true);
            down=editor->getLocalPoint(rangeHandle,rangeHandle->getLocalBounds().toFloat().getCentre());
            rangeHandle->mouseDown(mouseAt(*rangeHandle,*editor,down,down));
            const auto oldRange=raw("band4_range");toggle(bandSelectors[2],true);toggle(dynamicButton,true);
            rangeHandle->mouseDrag(mouseAt(*rangeHandle,*editor,down.translated(0,20),down,true));
            rangeHandle->mouseUp(mouseAt(*rangeHandle,*editor,down.translated(0,20),down,true));
            check(raw("band4_range")==oldRange&&raw("band3_range")==-6,"band change cancels captured drag without writing into the new or old band");
            check(gestures.starts==gestures.ends,"rebinding closes the old direct gesture");
            const auto thirdBefore=raw("band3_range");rangeHandle->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
            check(std::abs(raw("band3_range")-thirdBefore-.1f)<.002&&raw("band4_range")==oldRange,"rebound direct graph keyboard target edits only the newly selected band");
        }
        p.removeListener(&gestures);
        command(resetButton);
        for(auto* parameter:p.getParameters())check(std::abs(parameter->getValue()-parameter->getDefaultValue())<1.e-6,"RESET button restores every parameter");
        toggle(bandSelectors[3],true);
        if(gainControl)gainControl->setValue(6,juce::sendNotificationSync);
        toggle(dynamicButton,true);set(p,"band4_threshold",-33);set(p,"band4_range",-9);
        command(copyButton);if(gainControl)gainControl->setValue(-4,juce::sendNotificationSync);
        set(p,"band4_threshold",-51);set(p,"band4_range",5);toggle(dynamicButton,false);
        command(swapButton);check(std::abs(raw("band4_gain")-6)<0.002,"A/B button restores copied settings");
        check(gainControl && std::abs(gainControl->getValue()-6)<0.002,"A/B also refreshes bound controls");
        check(raw("band4_dynamic")==1&&raw("band4_threshold")==-33&&raw("band4_range")==-9,"A/B restores dynamic mode threshold and range");
        for(auto* control:dynamicControls)check(control&&control->isVisible(),"A/B updates dynamic panel visibility");
        command(swapButton);check(std::abs(raw("band4_gain")+4)<0.002,"A/B button returns edited settings");
        check(raw("band4_dynamic")==0&&raw("band4_threshold")==-51&&raw("band4_range")==5,"A/B returns edited dynamic settings");
        command(resetButton);
        toggle(deltaButton,true);check(raw("delta")==1 && p.getResponseDb(1000)<-200,"DELTA button enables difference null");
        toggle(bypassButton,true);check(raw("bypass")==1 && std::abs(p.getResponseDb(1000))<1.e-10,"BYPASS button bypasses delta");
        toggle(bypassButton,false);toggle(deltaButton,false);
        check(raw("bypass")==0 && raw("delta")==0,"DELTA and BYPASS buttons return to normal");
        if(outputControl){outputControl->setValue(-3.5,juce::sendNotificationSync);check(std::abs(raw("output")+3.5)<0.002,"OUTPUT control sets trim");}
        command(resetButton);
        std::array<float,2048> analyzerPre{},analyzerPost{};p.readSpectrum(analyzerPre,analyzerPost);
        toggle(analyzerButton,false);
        for(int block=0;block<32;++block){bd.clear();p.processBlock(bd,midi);}
        check(!p.readSpectrum(analyzerPre,analyzerPost),"ANALYZER button stops capture");
        toggle(analyzerButton,true);
        for(int block=0;block<32;++block){bd.clear();p.processBlock(bd,midi);}
        check(p.readSpectrum(analyzerPre,analyzerPost),"ANALYZER button restarts capture");
        check(graph!=nullptr,"response graph exists");
        if(graph) {
            auto snapshotHash=[&](){auto img=graph->createComponentSnapshot(graph->getLocalBounds());uint64_t hash=1469598103934665603ull;for(int y=0;y<img.getHeight();++y)for(int x=0;x<img.getWidth();++x){hash^=img.getPixelAt(x,y).getARGB();hash*=1099511628211ull;}return hash;};
            const auto liveHash=snapshotHash();toggle(freezeButton,true);
            check(snapshotHash()!=liveHash,"FREEZE callback changes graph display state");
            toggle(freezeButton,false);check(snapshotHash()==liveHash,"FREEZE callback restores live graph display");
        }
        command(resetButton);
        renderPng(*editor,1120,760,"GILLEQ-UI-STATIC.png");
        toggle(bandSelectors[3],true);toggle(dynamicButton,true);
        set(p,"band4_freq",1000);set(p,"band4_gain",3);set(p,"band4_q",1.4f);
        set(p,"band4_threshold",-36);set(p,"band4_range",-9);set(p,"band4_attack",5);set(p,"band4_release",120);
        p.prepareToPlay(48000,128);
        check(dynamicsMeter&&dynamicsMeter->isVisible(),"dynamic band has a visible actual-data meter");
        const auto silentGraphHash=graph?componentHash(*graph):0;
        const auto silentMeterHash=dynamicsMeter?componentHash(*dynamicsMeter):0;
        // Dispatch actual JUCE timers, while feeding deterministic audio. This
        // tests live FFT/meter repainting rather than drawing synthetic curves.
        auto* manager=juce::MessageManager::getInstance();
        std::atomic<bool> dispatched{false},timedOut{false};
        int timerSteps=0;int64_t sample=0;
        // Simulate automation delivered by a real non-message host thread.
        std::thread automationThread([&] {
            const std::array<std::pair<const char*,float>,6> values {{{"freq",1500},{"threshold",-35.5f},{"range",-8.5f},{"attack",7.5f},{"release",160},{"dynamic",0}}};
            for(const auto& entry:values)if(auto* parameter=p.apvts.getParameter(juce::String("band4_")+entry.first))parameter->setValueNotifyingHost(parameter->convertTo0to1(entry.second));
        });
        automationThread.join();
        // Timer messages may already be queued before these asynchronous
        // attachments, especially while parallel release builds load the host.
        // A FIFO message barrier makes the assertion order deterministic. The
        // existing five-second watchdog still fails a stalled message loop.
        bool automationBarrier=false;
        juce::MessageManager::callAsync([&] { automationBarrier=true; });
        CallbackTimer sourceTimer;
        sourceTimer.callback=[&] {
            if(!automationBarrier)return;
            if(timerSteps==0) {
                const std::array<double,4> expected {-35.5,-8.5,7.5,160};
                for(size_t i=0;i<dynamicControls.size();++i)check(dynamicControls[i]&&std::abs(dynamicControls[i]->getValue()-expected[i])<.002,"non-message host automation refreshes each visible graph value through the message loop");
                check(frequencyControl&&std::abs(frequencyControl->getValue()-1500)<.03,"asynchronous host frequency automation refreshes the selected graph position");
                check(dynamicButton&&!dynamicButton->getToggleState()&&rangeHandle&&!rangeHandle->isVisible(),"asynchronous host mode automation hides direct dynamic controls");
                for(auto* field:dynamicControls)check(field&&!field->isVisible(),"asynchronous static mode removes numeric dynamic controls from the graph");
                set(p,"band4_dynamic",1);
                if(rangeHandle) {
                    const auto centre=editor->getLocalPoint(rangeHandle,rangeHandle->getLocalBounds().toFloat().getCentre());
                    check(editor->getComponentAt(centre.roundToInt())==rangeHandle,"range hit target follows asynchronous host automation");
                }
                set(p,"band4_freq",1000);set(p,"band4_threshold",-36);set(p,"band4_range",-9);set(p,"band4_attack",5);set(p,"band4_release",120);
            }
            for(int block=0;block<64;++block) {
                for(int i=0;i<128;++i,++sample) {
                    const double t=static_cast<double>(sample)/48000;
                    const auto wave=0.18*std::sin(juce::MathConstants<double>::twoPi*1000*t)+0.025*std::sin(juce::MathConstants<double>::twoPi*237*t)+0.018*std::sin(juce::MathConstants<double>::twoPi*3877*t);
                    bd.setSample(0,i,wave);bd.setSample(1,i,wave*0.83);
                }
                p.processBlock(bd,midi);
            }
            if(++timerSteps>=8){sourceTimer.stopTimer();manager->stopDispatchLoop();}
        };
        std::thread watchdog([&] {
            for(int i=0;i<100;++i){if(dispatched.load())return;std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            timedOut.store(true);manager->stopDispatchLoop();
        });
        sourceTimer.startTimer(75);manager->runDispatchLoop();sourceTimer.stopTimer();dispatched.store(true);watchdog.join();
        check(automationBarrier,"queued asynchronous automation was dispatched before UI assertions");
        check(!timedOut.load()&&timerSteps==8,"bounded real message loop dispatched live editor timers");
        check(p.getBandDetectorDb(3)>-36&&p.getBandDetectorDb(3)<0,"selected band detector reacts to actual audio");
        check(p.getBandDynamicGainDb(3)<-2&&p.getBandDynamicGainDb(3)>=-9.001,"selected dynamic gain meter reports actual processing");
        check(graph&&componentHash(*graph)!=silentGraphHash,"live response and spectrum graph changes with audio");
        check(dynamicsMeter&&componentHash(*dynamicsMeter)!=silentMeterHash,"actual detector and gain meter display changes with audio");
        check(std::abs(p.getResponseDb(1000)-(3.0+p.getBandDynamicGainDb(3)))<0.01,"live centre response matches actual static plus dynamic gain");
        const auto captured=p.getResponseSnapshot();const auto capturedCentre=p.getResponseDb(1000,gill::Stereo,captured);
        set(p,"band4_gain",11);
        check(std::abs(p.getResponseDb(1000,gill::Stereo,captured)-capturedCentre)<1.e-12,"response snapshot keeps an entire paint frame stable after parameter edits");
        set(p,"band4_gain",3);
        for(const auto size:std::array<juce::Point<int>,3>{juce::Point<int>(760,500),{860,580},{1720,1160}}) {
            editor->setSize(size.x,size.y);
            for(auto* control:dynamicControls)check(control&&control->isVisible()&&graph&&graph->getLocalBounds().contains(graph->getLocalArea(control,control->getLocalBounds()))&&control->getWidth()>=70&&control->getHeight()>=23,"compact numeric values remain legible inside resized graph");
            check(dynamicsMeter&&graph&&graph->getLocalBounds().contains(dynamicsMeter->getBounds()),"dynamic band card stays inside resized graph");
            check(graph&&graph->getHeight()>=203,"compact minimum size retains a usable frequency preview");
            const auto filename="GILLEQ-UI-"+juce::String(size.x)+".png";
            renderPng(*editor,size.x,size.y,filename.toRawUTF8());
        }
        const auto bandFourMeterHash=dynamicsMeter?componentHash(*dynamicsMeter):0;
        toggle(bandSelectors[2],true);toggle(dynamicButton,true);
        check(dynamicsMeter&&componentHash(*dynamicsMeter)!=bandFourMeterHash,"band selection rebinds actual dynamics meter values");
        check(p.getBandDynamicGainDb(2)==0,"unprocessed band does not inherit another band dynamic gain");
        toggle(bandSelectors[3],true);
        check(dynamicButton&&dynamicButton->getToggleState()&&dynamicsMeter&&componentHash(*dynamicsMeter)==bandFourMeterHash,"returning to selected band restores its meter display");
        editor.reset();
    }
    p.releaseResources();
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::ofstream out("plugin-integration-report.json");
    out<<"{\n  \"passed\": "<<(failures==0?"true":"false")<<",\n  \"checks\": "<<checks<<",\n  \"failures\": "<<failures<<",\n  \"automated_configurations\": "<<automationCases<<",\n  \"ui_control_changes\": "<<uiChanges<<",\n  \"dynamic_control_edits\": 40000,\n  \"dynamic_mode_switches\": 10000,\n  \"elapsed_seconds\": "<<seconds<<",\n  \"scope\": \"Parameter robustness, legacy state compatibility, actual UI bindings, live audio-driven dynamic response and metering; not a universal audio-quality guarantee\"\n}\n";
    std::cout<<"Integration checks: "<<checks<<", failures: "<<failures<<", UI changes: "<<uiChanges<<", seconds: "<<seconds<<"\n";
    return failures?1:0;
}
