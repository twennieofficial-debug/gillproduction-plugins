#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HarmonyPresets.h"
#include <cstdlib>

namespace {
const char* names[] {"GILLHARMONY","GILLREFERENCE","GILLRESCUE"};
const char* presets[3][6] {
    {"THIRD ABOVE","THIRD BELOW","OCTAVE SHADOW","WIDE HOOK","DARK STACK","THREE VOICE CHOIR"},
    {"FULL MIX","VOCAL PRESENCE","LOW-END CHECK","AIR CHECK","MONO CHECK","SIDE CHECK"},
    {"MILD ADC CLIP","HOT RAP TAKE","SHORT PEAKS","ASYMMETRIC CLIP","CAREFUL REPAIR","LISTEN REPAIRS"}
};
juce::String stateId(ToolsKind kind) { return juce::String(names[static_cast<int>(kind)])+"_STATE"; }
}

GillToolsProcessor::GillToolsProcessor(ToolsKind selected)
    : AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true)
                                      .withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),
      kind(selected), apvts(*this,nullptr,stateId(kind),layout(kind)) {
    for (auto* parameter : getParameters()) {
        auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        raw.push_back(identified ? apvts.getRawParameterValue(identified->paramID) : nullptr);
    }
    if (kind==ToolsKind::Harmony) harmony=std::make_unique<gill::tools::HarmonyDSP>();
    if (kind==ToolsKind::Reference) reference=std::make_unique<gill::tools::ReferenceEngine>();
    if (kind==ToolsKind::Rescue) rescue=std::make_unique<gill::tools::RescueDSP>();
    selectPreset(0,false);
    startTimerHz(20);
}
GillToolsProcessor::~GillToolsProcessor() { stopTimer(); }

juce::AudioProcessorValueTreeState::ParameterLayout GillToolsProcessor::layout(ToolsKind kind) {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    auto number=[&](const juce::String& id,const juce::String& name,float lo,float hi,float step,float value) {
        result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,step),value));
    };
    auto flag=[&](const juce::String& id,const juce::String& name,bool value) {
        result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,1},name,value));
    };
    auto choice=[&](const juce::String& id,const juce::String& name,juce::StringArray items,int value) {
        result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,1},name,items,value));
    };
    if (kind==ToolsKind::Harmony) {
        choice("key","KEY",{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},0);
        choice("scale","SCALE",{"MAJOR","MINOR","HARMONIC MINOR","PENTATONIC"},1);
        number("natural","NATURAL",0,100,.1f,75);flag("direct","DIRECT",true);number("output","OUTPUT",-18,0,.01f,-6);
        for (int i=0;i<3;++i) {
            const juce::String prefix="voice"+juce::String(i),label="VOICE "+juce::String(i+1)+" ";
            flag(prefix+"on",label+"ON",i==0);
            choice(prefix+"mode",label+"MODE",{"SCALE","SEMITONES"},0);
            number(prefix+"interval",label+"INTERVAL",-12,12,1,i==0?2.f:i==1?4.f:-7.f);
            number(prefix+"level",label+"LEVEL",-60,0,.1f,-9);number(prefix+"pan",label+"PAN",-100,100,.1f,i==0?-35.f:i==1?35.f:0.f);
        }
    } else if (kind==ToolsKind::Reference) {
        flag("reference","REFERENCE",false);flag("match","MATCH",true);number("matchTrim","MATCH TRIM",-6,6,.1f,0);
        flag("mono","MONO",false);choice("channelView","CHANNEL",{"STEREO","MID","SIDE"},0);
        choice("listenBand","LISTEN",{"FULL","VOICE","LOW","AIR"},0);flag("follow","FOLLOW",true);
        choice("fileSlot","FILE SLOT",{"1","2","3"},0);
    } else {
        number("repair","REPAIR",0,100,.1f,100);number("clipDb","CLIP +",-24,0,.01f,0);
        number("negativeClipDb","CLIP -",-24,0,.01f,0);number("maxRepair","MAX REPAIR",0,12,.1f,6);
        number("output","OUTPUT",-18,0,.01f,-6);flag("delta","LISTEN REPAIRS",false);
    }
    flag("bypass","BYPASS",false);result.add(gill::qualityParameter(1));return result;
}
const juce::String GillToolsProcessor::getName() const { return names[static_cast<int>(kind)]; }
bool GillToolsProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto channels=layout.getMainOutputChannelSet();
    return (channels==juce::AudioChannelSet::mono()||channels==juce::AudioChannelSet::stereo())&&layout.getMainInputChannelSet()==channels;
}
float GillToolsProcessor::parameter(size_t index) const noexcept {
    if(index>=raw.size()||!raw[index])return 0;
    const float value=raw[index]->load(std::memory_order_relaxed);return std::isfinite(value)?value:0;
}
float GillToolsProcessor::value(const juce::String& id) const {
    if(auto* p=apvts.getRawParameterValue(id)){const float value=p->load();return std::isfinite(value)?value:0;}return 0;
}
void GillToolsProcessor::setValue(const juce::String& id,float value,bool gesture) {
    if(!std::isfinite(value))return;
    if(auto* p=apvts.getParameter(id)) {
        if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(value));if(gesture)p->endChangeGesture();
    }
}
void GillToolsProcessor::prepareToPlay(double rate,int block) {
    supported=std::isfinite(rate)&&rate>=8000&&rate<=192000;rateView=supported?rate:48000;
    const bool live=!quality.isPro();const int channels=getTotalNumOutputChannels();
    if(harmony){harmony->setParameters(harmonyParameters(false));harmony->setLiveMode(live);harmony->prepare(rateView.load(),block,channels);setLatencySamples(harmony->latencySamples());}
    if(rescue){rescue->setParameters(rescueParameters(false));rescue->setLiveMode(live);rescue->prepare(rateView.load(),block,channels);setLatencySamples(rescue->latencySamples());}
    if(reference){reference->prepare(rateView.load(),block);setLatencySamples(0);}
    transition.prepare(rateView.load(),live?0:1);hadPosition=false;nextPosition=0;
}
void GillToolsProcessor::releaseResources() {
    if(harmony)harmony->reset();if(rescue)rescue->reset();if(reference)reference->reset();
}
double GillToolsProcessor::getTailLengthSeconds() const {
    if(harmony)return double(harmony->maximumLatencySamples())/std::max(8000.,rateView.load());
    if(rescue)return double(rescue->maximumLatencySamples())/std::max(8000.,rateView.load());
    return 0;
}
void GillToolsProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { process(buffer,false); }
void GillToolsProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { process(buffer,true); }
gill::tools::HarmonyParameters GillToolsProcessor::harmonyParameters(bool hostBypass) const noexcept {
    gill::tools::HarmonyParameters p;p.key=int(parameter(0));p.scale=int(parameter(1));p.natural=parameter(2);p.direct=parameter(3)>.5f;p.outputDb=parameter(4);
    for(int i=0;i<3;++i){const size_t at=5+static_cast<size_t>(i)*5;auto& voice=p.voices[i];voice.enabled=parameter(at)>.5f;voice.intervalMode=int(parameter(at+1));voice.interval=int(parameter(at+2));voice.levelDb=parameter(at+3);voice.pan=parameter(at+4);}
    p.bypass=hostBypass||parameter(20)>.5f;return p;
}
gill::tools::RescueParameters GillToolsProcessor::rescueParameters(bool hostBypass) const noexcept {
    gill::tools::RescueParameters p;p.repair=parameter(0);p.clipDb=parameter(1);p.negativeClipDb=parameter(2);p.maxRepairDb=parameter(3);p.outputDb=parameter(4);p.listenRepairs=parameter(5)>.5f;p.bypass=hostBypass||parameter(6)>.5f;return p;
}
void GillToolsProcessor::process(juce::AudioBuffer<float>& buffer,bool hostBypass) {
    juce::ScopedNoDenormals guard;
    const int frames=buffer.getNumSamples(),channels=buffer.getNumChannels();if(frames<=0||channels<=0)return;
    if(!supported){for(int c=0;c<channels;++c)for(int n=0;n<frames;++n)if(!std::isfinite(buffer.getSample(c,n)))buffer.setSample(c,n,0);return;}
    const bool live=!quality.isPro();int latency=0;
    if(harmony) {
        harmony->setLiveMode(live);harmony->setParameters(harmonyParameters(hostBypass));harmony->process(buffer.getArrayOfWritePointers(),channels,frames);
        latency=harmony->latencySamples();inputPeak=harmony->inputPeak();outputPeak=harmony->outputPeak();
    } else if(rescue) {
        rescue->setLiveMode(live);rescue->setParameters(rescueParameters(hostBypass));rescue->process(buffer.getArrayOfWritePointers(),channels,frames);
        latency=rescue->latencySamples();inputPeak=rescue->inputPeak();outputPeak=rescue->outputPeak();
    } else if(reference) {
        gill::tools::ReferenceEngine::Parameters p;p.match=parameter(1)>.5f;p.trimDb=parameter(2);p.mono=parameter(3)>.5f;p.channelView=int(parameter(4));p.listenBand=int(parameter(5));p.follow=parameter(6)>.5f;p.bypass=hostBypass||parameter(8)>.5f;
        gill::tools::ReferenceEngine::Transport transport;transport.offline=isNonRealtime();
        if(auto* playHead=getPlayHead())if(auto position=playHead->getPosition()) {
            transport.playing=position->getIsPlaying();if(auto samples=position->getTimeInSamples()){transport.hasPosition=true;transport.positionSamples=*samples;}
        }
        transport.discontinuity=transport.hasPosition&&hadPosition&&transport.positionSamples!=nextPosition;
        nextPosition=transport.positionSamples+(transport.playing?frames:0);hadPosition=transport.hasPosition;
        reference->setReferenceEnabled(parameter(0)>.5f);reference->process(buffer,transport,p);
    }
    if(latency!=getLatencySamples())setLatencySamples(latency);
    if(!reference)transition.process(buffer.getArrayOfWritePointers(),channels,frames,live?0:1);
}
void GillToolsProcessor::timerCallback() {
    if(reference){const int slot=std::clamp(int(value("fileSlot")),0,2);if(slot!=lastSlot){lastSlot=slot;setValue("reference",0,false);reference->selectSlot(slot);}}
    if(rescue){const auto revision=rescue->learnRevision();if(revision!=learnSeen){learnSeen=revision;
        const float positive=rescue->learnedPositiveDb(),negative=rescue->learnedNegativeDb();
        if(positive>=-24&&positive<=.01f)setValue("clipDb",std::min(0.f,positive));
        if(negative>=-24&&negative<=.01f)setValue("negativeClipDb",std::min(0.f,negative));
    }}
}
void GillToolsProcessor::loadReference(int slot,const juce::File& file) {
    if(!reference)return;setValue("reference",0);setValue("fileSlot",float(std::clamp(slot,0,2)));
    lastSlot=std::clamp(slot,0,2);reference->selectSlot(lastSlot);reference->requestLoad(lastSlot,file);
}
const juce::String GillToolsProcessor::getProgramName(int index) { return presets[static_cast<int>(kind)][std::clamp(index,0,5)]; }
void GillToolsProcessor::selectPreset(int index,bool gesture) {
    index=std::clamp(index,0,5);
    auto set=[&](const juce::String& id,float value){setValue(id,value,gesture);};
    set("bypass",0);
    if(kind==ToolsKind::Harmony){
        const auto settings=gill::tools::harmonyPresets()[static_cast<size_t>(index)].parameters;
        // Preserve the song's selected key/scale while changing the arrangement.
        set("natural",settings.natural);set("direct",settings.direct?1.f:0.f);set("output",settings.outputDb);
        for(int i=0;i<3;++i){const auto& voice=settings.voices[static_cast<size_t>(i)];const juce::String prefix="voice"+juce::String(i);set(prefix+"on",voice.enabled?1.f:0.f);set(prefix+"mode",float(voice.intervalMode));set(prefix+"interval",float(voice.interval));set(prefix+"level",voice.levelDb);set(prefix+"pan",voice.pan);}
    }else if(kind==ToolsKind::Reference){set("reference",0);set("match",1);set("matchTrim",0);set("mono",index==4?1.f:0.f);set("channelView",index==5?2.f:0.f);set("listenBand",index>=1&&index<=3?float(index):0.f);set("follow",1);}
    else {set("repair",index==4?45.f:index==0?75.f:100.f);set("clipDb",0);set("negativeClipDb",index==3?-1.f:0.f);set("maxRepair",index==1?9.f:index==0||index==4?3.f:6.f);set("output",index==1?-9.f:-6.f);set("delta",index==5?1.f:0.f);}
    program=index;
}
void GillToolsProcessor::getStateInformation(juce::MemoryBlock& bytes) {
    auto tree=apvts.copyState();tree.setProperty("schema",1,nullptr);tree.setProperty("program",program.load(),nullptr);
    if(reference){for(auto child:tree)if(child["id"].toString()=="reference")child.setProperty("value",0,nullptr);tree.addChild(reference->getState(),-1,nullptr);}
    if(auto xml=tree.createXml())copyXmlToBinary(*xml,bytes);
}
void GillToolsProcessor::setStateInformation(const void* data,int bytes) {
    if(!data||bytes<=0||bytes>2*1024*1024)return;
    auto xml=getXmlFromBinary(data,bytes);if(!xml||!xml->hasTagName(stateId(kind)))return;
    auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||int(tree.getProperty("schema",0))!=1)return;
    juce::StringArray seen;juce::ValueTree referenceState;
    for(auto child:tree){
        if(!child.hasType("PARAM")){if(reference&&!referenceState.isValid()){referenceState=child;continue;}return;}
        const auto id=child["id"].toString();auto* parameter=apvts.getParameter(id);if(!parameter||seen.contains(id))return;seen.add(id);
        const auto text=child["value"].toString();char* end=nullptr;const double value=std::strtod(text.toRawUTF8(),&end);const auto range=parameter->getNormalisableRange();
        if(text.isEmpty()||!end||*end||!std::isfinite(value)||value<range.start-.001||value>range.end+.001)return;
        if(parameter->isDiscrete()&&value!=std::floor(value))return;
        if(id=="reference")child.setProperty("value",0,nullptr);
    }
    if(seen.size()!=int(raw.size()))return;
    if(referenceState.isValid()){reference->setState(referenceState);tree.removeChild(referenceState,nullptr);}
    apvts.replaceState(tree);
    // Hosts can send fractional normalized values even for switches. APVTS
    // caches the snapped value and can skip restoring an equal snapped value
    // while the underlying host parameter still holds the fractional input.
    // Resynchronize the actual parameter after the entire state is validated.
    for(auto child:tree)if(child.hasType("PARAM")){
        if(auto* parameter=apvts.getParameter(child["id"].toString()))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(child["value"])));
    }
    program=std::clamp(int(tree.getProperty("program",0)),0,5);
    if(reference)reference->setReferenceEnabled(false);
}
juce::AudioProcessorEditor* GillToolsProcessor::createEditor() { return new GillToolsEditor(*this); }
