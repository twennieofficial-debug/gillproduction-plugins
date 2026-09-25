#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
static int checks=0,failures=0;
void check(bool good,const char* name){++checks;if(!good)++failures;std::printf("%s %s\n",good?"PASS":"FAIL",name);}
bool childBounds(juce::Component& root,juce::Component& component){
    bool good=true;
    for(auto* child:component.getChildren()){
        if(child->isVisible()){const auto area=root.getLocalArea(child,child->getLocalBounds());good=good&&root.getLocalBounds().contains(area);}
        good=childBounds(root,*child)&&good;
    }return good;
}
int main(){
#if JUCE_MAC
    gillInitialiseMacTestApplication();
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    for(int kind=0;kind<3;++kind){
        GillToolsProcessor p(static_cast<ToolsKind>(kind));
        p.setPlayConfigDetails(2,2,48000,127);p.prepareToPlay(48000,127);
        check(p.getName()==juce::String(kind==0?"GILLHARMONY":kind==1?"GILLREFERENCE":"GILLRESCUE"),"Correct product identity");
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
            check(p.getLatencySamples()==(p.reference?0:p.harmony?p.harmony->latencySamples():p.rescue->latencySamples()),"Wrapper reports actual engine latency");
        }
        p.selectPreset(0,false);p.setValue("gillQuality",1,false);p.prepareToPlay(48000,127);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        const int width=kind==1?660:540,height=kind==0?440:kind==1?410:390;
        check(editor->getWidth()==width&&editor->getHeight()==height,"Compact default logical size");
        check(childBounds(*editor,*editor),"Every visible control lies within plugin bounds");
        for(float scale:{1.f,1.25f,1.5f,2.f}){
            const auto screenshot=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);
            check(screenshot.isValid()&&screenshot.getWidth()==juce::roundToInt(width*scale)&&screenshot.getHeight()==juce::roundToInt(height*scale),"Native UI renders at supported pixel scales");
            const auto file=juce::File::getCurrentWorkingDirectory().getChildFile(p.getName()+"-UI-"+juce::String(screenshot.getWidth())+"x"+juce::String(screenshot.getHeight())+".png");auto stream=file.createOutputStream();
            check(stream!=nullptr,"UI image output writable");if(stream){stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;check(png.writeImageToStream(screenshot,*stream),"Native UI image saved");}
        }
        editor.reset();p.releaseResources();
    }
    std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
