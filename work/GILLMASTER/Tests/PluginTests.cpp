#include "CurrentUiChecks.h"
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include "../../GILLCommon/MasterRecallChecks.h"

#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
#if JUCE_WINDOWS
 #include <windows.h>
#endif
void pumpNative(){
#if JUCE_WINDOWS
 const auto end=juce::Time::getMillisecondCounter()+80;
 do{MSG message;while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessage(&message);}juce::Thread::sleep(2);}while(juce::Time::getMillisecondCounter()<end);
#endif
}
static int checks=0,failures=0;
void check(bool good,const char* name){++checks;if(!good)++failures;std::printf("%s %s\n",good?"PASS":"FAIL",name);}
bool childBounds(juce::Component& root,juce::Component& component){
    bool good=true;
    for(auto* child:component.getChildren()){
        if(child->isVisible()){const auto area=root.getLocalArea(child,child->getLocalBounds());good=good&&root.getLocalBounds().contains(area);}
        good=childBounds(root,*child)&&good;
    }return good;
}

juce::TextButton* buttonNamed(juce::Component& c,const juce::String& name){for(auto* child:c.getChildren()){if(auto* b=dynamic_cast<juce::TextButton*>(child))if(b->getButtonText()==name)return b;if(auto* found=buttonNamed(*child,name))return found;}return nullptr;}
void exerciseControls(GillMasterProcessor&p,juce::Component&c,int&count){
    auto specs=p.specs;if(int(p.kind)<6){specs.push_back({"output","OUTPUT"});specs.push_back({"mix","MIX"});}if(p.kind==MasterKind::Ceiling)specs.push_back({"boost","BOOST"});
    for(auto* child:c.getChildren()){
        const auto label=child->getParentComponent()->getName();
        for(const auto& spec:specs)if(label==spec.label){
            const float before=p.value(spec.id);
            if(auto* slider=dynamic_cast<juce::Slider*>(child)){const auto lo=slider->getMinimum(),hi=slider->getMaximum();slider->setValue(before<(lo+hi)*.5?hi:lo,juce::sendNotificationSync);check(std::abs(p.value(spec.id)-before)>1e-5,"Visible dial writes attached parameter");++count;}
            else if(auto* combo=dynamic_cast<juce::ComboBox*>(child)){combo->setSelectedId(combo->getSelectedId()==1?2:1,juce::sendNotificationSync);check(std::abs(p.value(spec.id)-before)>.5,"Visible selector writes attached parameter");++count;}
            else if(auto* button=dynamic_cast<juce::TextButton*>(child)){button->setToggleState(!button->getToggleState(),juce::sendNotificationSync);check(std::abs(p.value(spec.id)-before)>.5,"Visible switch writes attached parameter");++count;}
        }
        exerciseControls(p,*child,count);
    }
}
void feed(GillMasterProcessor&p,int blocks){juce::AudioBuffer<float>b(2,127);juce::MidiBuffer m;for(int n=0;n<blocks;++n){for(int c=0;c<2;++c)for(int i=0;i<127;++i)b.setSample(c,i,.25f*std::sin(float((n*127+i)*.037+c*.3)));p.processBlock(b,m);}}

bool peersClear(juce::Component& c){for(int i=0;i<c.getNumChildComponents();++i){auto*a=c.getChildComponent(i);if(!a->isVisible())continue;if(a->getName().isNotEmpty()&&a->getBounds().isEmpty())return false;for(int j=i+1;j<c.getNumChildComponents();++j){auto*b=c.getChildComponent(j);if(b->isVisible()&&!a->getName().isEmpty()&&!b->getName().isEmpty()&&a->getBounds().intersects(b->getBounds()))return false;}if(!peersClear(*a))return false;}return true;}
int main(){
#if JUCE_MAC
    gillInitialiseMacTestApplication();
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    for(int kind=0;kind<8;++kind){
        GillMasterProcessor p(static_cast<MasterKind>(kind));
        p.setPlayConfigDetails(2,2,48000,127);p.prepareToPlay(48000,127);
        check(p.getName()==juce::String(masterInfo(static_cast<MasterKind>(kind)).name),"Correct product identity");
        check(p.getNumPrograms()==6,"Six usable factory starting points");
        std::vector<std::vector<float>> presetValues;
        for(int preset=0;preset<6;++preset){
            p.setCurrentProgram(preset);std::vector<float> values;
            for(auto* parameter:p.getParameters())values.push_back(parameter->getValue());
            check(std::find(presetValues.begin(),presetValues.end(),values)==presetValues.end(),"Each preset has a distinct processing arrangement");
            presetValues.push_back(std::move(values));
        }
        for(int preset=0;preset<6;++preset){p.setCurrentProgram(preset);check(p.getCurrentProgram()==preset,"Factory preset selection");juce::MemoryBlock saved;p.getStateInformation(saved);p.setCurrentProgram((preset+1)%6);p.setStateInformation(saved.getData(),int(saved.getSize()));check(p.getCurrentProgram()==preset,"Preset and settings survive state recall");}
        p.selectPreset(0,false);
        if(kind==0)masterRecallChecks(p,true,[](bool good,const char*message){check(good,message);});
        {
            juce::MemoryBlock original;p.getStateInformation(original);
            std::vector<float> values;for(auto* parameter:p.getParameters())values.push_back(parameter->getValue());
            for(auto* parameter:p.getParameters())parameter->setValueNotifyingHost(.39821f);
            p.setStateInformation(original.getData(),int(original.getSize()));
            for(int i=0;i<p.getParameters().size();++i)
                check(std::abs(p.getParameters()[i]->getValue()-values[static_cast<size_t>(i)])<1e-6f,"Recall restores host parameter after fractional automation, including switches");
        }
        for(double rate:{44100.,48000.,96000.,192000.})for(bool pro:{false,true}){
            p.setValue("gillQuality",pro?1.f:0.f,false);p.prepareToPlay(rate,127);
            juce::AudioBuffer<float> audio(2,127);juce::MidiBuffer midi;
            bool finite=true;
            for(int block=0;block<40;++block){for(int c=0;c<2;++c)for(int n=0;n<127;++n)audio.setSample(c,n,.15f*std::sin(float((block*127+n)*2*juce::MathConstants<double>::pi*173/rate)));p.processBlock(audio,midi);for(int c=0;c<2;++c)for(int n=0;n<127;++n)finite=finite&&std::isfinite(audio.getSample(c,n));}
            check(finite,"Real processor finite at all tested rates and both modes");
            check(p.getLatencySamples()==(pro?(kind==0?int(std::ceil(rate*.003))+32:kind==5?32:0):0),"Wrapper reports actual engine latency");
        }
        p.selectPreset(0,false);p.setValue("gillQuality",1,false);p.prepareToPlay(48000,127);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor()); gillCurrentUi::verify(editor.get(),p.getName(),[](bool ok,const char* why){check(ok,why);});
        const auto info=masterInfo(static_cast<MasterKind>(kind));const int width=info.width,height=info.height;
        check(editor->getWidth()==width&&editor->getHeight()==height,"Compact default logical size");
        check(childBounds(*editor,*editor),"Every visible control lies within plugin bounds");check(peersClear(*editor),"Named native controls have nonempty nonoverlapping bounds");
        for(const float scale:{1.25f,2.f}){editor->setSize(juce::roundToInt(width*scale),juce::roundToInt(height*scale));check(gillCurrentUi::visibleInside(*editor,*editor),"Resized master editor preserves reachable native controls without ancestor clipping");check(peersClear(*editor),"Resized master editor preserves nonoverlapping named controls");}editor->setSize(width,height);
        juce::MemoryBlock uiState;p.getStateInformation(uiState);int controlCount=0;exerciseControls(p,*editor,controlCount);
        check(controlCount==int(p.specs.size())+(kind<6?2:0)+(kind==0?1:0),"Every processing parameter has a tested native control");p.setStateInformation(uiState.getData(),int(uiState.getSize()));
        for(const auto name:{"LIVE","PRO","<",">"}){auto*b=buttonNamed(*editor,name);check(b&&bool(b->onClick),"Header actions wired");if(b)b->onClick();}
        p.selectPreset(0,false);p.setValue("gillQuality",1,false);feed(p,800);
        if(p.delta){auto*b=buttonNamed(*editor,"LEARN");check(b&&bool(b->onClick),"Delta learn button wired");GillMasterProcessor source(MasterKind::Delta);source.setPlayConfigDetails(2,2,48000,127);source.setValue("role",1,false);source.prepareToPlay(48000,127);if(b)b->onClick();juce::AudioBuffer<float> audio(2,127);juce::MidiBuffer midi;std::uint32_t seed=812;
            for(int round=0;round<1800;++round){for(int i=0;i<127;++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;const float v=float((double(seed)/4294967295.-.5)*.4);audio.setSample(0,i,v);audio.setSample(1,i,v);}source.processBlock(audio,midi);p.processBlock(audio,midi);if(round%10==0)juce::Thread::sleep(2);}check(p.delta->alignment().valid,"Delta native Learn aligns real source audio");source.releaseResources();}
        if(p.deliver){auto*b=buttonNamed(*editor,"ANALYZE");check(b&&bool(b->onClick),"Deliver analyze button wired");if(b)b->onClick();feed(p,1600);check(p.deliver->snapshot().duration>4&&p.deliver->snapshot().hasAudio,"Deliver native Analyze measures actual audio");b=buttonNamed(*editor,"STOP");if(b)b->onClick();feed(p,1);check(!p.deliver->snapshot().running,"Deliver native Stop stops capture");check(p.deliveryReport().contains("GILLDELIVER"),"Deliver report generated from capture");const auto reportFile=juce::File::getCurrentWorkingDirectory().getChildFile("GILLDELIVER-native-report.txt");check(reportFile.replaceWithText(p.deliveryReport()),"Deliver report writes successfully");}
        pumpNative();
        for(float scale:{1.f,1.25f,1.5f,2.f}){
            const auto screenshot=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);
            check(screenshot.isValid()&&screenshot.getWidth()==juce::roundToInt(width*scale)&&screenshot.getHeight()==juce::roundToInt(height*scale),"Native UI renders at supported pixel scales");
            const auto file=juce::File::getCurrentWorkingDirectory().getChildFile(p.getName()+"-UI-"+juce::String(screenshot.getWidth())+"x"+juce::String(screenshot.getHeight())+".png");auto stream=file.createOutputStream();
            check(stream!=nullptr,"UI image output writable");if(stream){stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;check(png.writeImageToStream(screenshot,*stream),"Native UI image saved");}
        }
        if(p.deliver){auto*b=buttonNamed(*editor,"RESET");if(b)b->onClick();feed(p,1);check(p.deliver->snapshot().duration==0,"Deliver native Reset clears capture");}
        editor.reset();p.releaseResources();
    }
    std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
