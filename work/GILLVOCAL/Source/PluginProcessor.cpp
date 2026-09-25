#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>
namespace {
juce::Identifier stateId(GillKind k) { return k==GillKind::Flow?"GILLFLOW_STATE":k==GillKind::Heat?"GILLHEAT_STATE":"GILLTUNE_STATE"; }
constexpr float presetRetune[]{100,40,15,5,0};
constexpr float presetHuman[]{80,45,20,5,0};
bool number(const juce::var& v,double& result) {
    auto s=v.toString().trim(); if(s.isEmpty())return false;
    const char* start=s.toRawUTF8(); char* end=nullptr; result=std::strtod(start,&end);
    return end!=start && *end=='\0' && std::isfinite(result);
}
}
GillVocalProcessor::GillVocalProcessor(GillKind k, bool liveTune)
:AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),kind(k),isLiveTune(k==GillKind::Tune&&liveTune),apvts(*this,nullptr,stateId(k),layout(k,liveTune?0:1)) {
    tune.setQualityMode(isLiveTune?1:0);
    const char* flowIds[]{"amount","mode","autogain","bypass"};
    const char* heatIds[]{"low","mid","high","style","mix","output","bypass"};
    const char* tuneIds[]{"key","scale","retune","humanize","mix","bypass"};
    const char** ids=k==GillKind::Flow?flowIds:k==GillKind::Heat?heatIds:tuneIds;
    const int count=k==GillKind::Flow?4:k==GillKind::Heat?7:6;
    for(int i=0;i<count;++i)parameterValues[static_cast<size_t>(i)]=apvts.getRawParameterValue(ids[i]);
}
juce::AudioProcessorValueTreeState::ParameterLayout GillVocalProcessor::layout(GillKind k,int qualityDefault) {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto f=[&](const char* id,const char* name,float lo,float hi,float step,float def){l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,step),def));};
    auto choice=[&](const char* id,const char* name,juce::StringArray items,int def){l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,1},name,items,def));};
    if(k==GillKind::Flow){f("amount","AMOUNT",0,100,.1f,60);choice("mode","MODE",{"NATURAL","FOCUS","CRUSH"},0);l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"autogain",1},"AUTO GAIN",true));}
    else if(k==GillKind::Heat){f("low","LOW DRIVE",0,24,.1f,4);f("mid","MID DRIVE",0,24,.1f,6);f("high","HIGH DRIVE",0,24,.1f,3);choice("style","STYLE",{"WARM","TAPE","EDGE"},0);f("mix","MIX",0,100,.1f,100);f("output","OUTPUT",-24,12,.1f,0);}
    else {choice("key","KEY",{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},0);choice("scale","SCALE",{"CHROMATIC","MAJOR","MINOR"},0);f("retune","RETUNE",0,200,.1f,100);f("humanize","HUMANIZE",0,100,.1f,80);f("mix","MIX",0,100,.1f,100);}
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));l.add(gill::qualityParameter(qualityDefault)); return l;
}
const juce::String GillVocalProcessor::getName()const { return kind==GillKind::Flow?"GILLFLOW":kind==GillKind::Heat?"GILLHEAT":isLiveTune?"GILLTUNE LIVE":"GILLTUNE"; }
bool GillVocalProcessor::isBusesLayoutSupported(const BusesLayout& l)const {auto o=l.getMainOutputChannelSet();return (o==juce::AudioChannelSet::mono()||o==juce::AudioChannelSet::stereo())&&o==l.getMainInputChannelSet();}
float GillVocalProcessor::value(const juce::String& id)const {auto* p=apvts.getRawParameterValue(id);const auto v=p?p->load(std::memory_order_relaxed):0.f;return std::isfinite(v)?v:0.f;}
void GillVocalProcessor::setValue(const juce::String& id,float v,bool gesture){if(auto* p=apvts.getParameter(id)){if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}}
float GillVocalProcessor::parameter(size_t i)const noexcept {const auto* p=parameterValues[i];const float v=p?p->load(std::memory_order_relaxed):0.f;return std::isfinite(v)?v:0.f;}
void GillVocalProcessor::updateParameters(){
    if(kind==GillKind::Flow)flow.setParameters(parameter(0),juce::roundToInt(parameter(1)),parameter(2)>.5f);
    else if(kind==GillKind::Heat)heat.setParameters(parameter(0),parameter(1),parameter(2),juce::roundToInt(parameter(3)),parameter(4),parameter(5));
    else tune.setParameters(juce::roundToInt(parameter(0)),juce::roundToInt(parameter(1)),parameter(2),parameter(3),parameter(4));
}
void GillVocalProcessor::prepareToPlay(double fs,int block){
    const bool supported=std::isfinite(fs)&&fs>=8000&&fs<=384000;rateSupported.store(supported);
    const double actualFs=std::isfinite(fs)&&fs>0?fs:48000.;if(!supported)fs=48000;
    uiRate.store(actualFs);const int channels=juce::jlimit(1,2,getTotalNumOutputChannels());
    heat.setLiveMode(false); tune.setLiveMode(false);
    updateParameters();
    if(kind==GillKind::Flow){flow.prepare(fs,std::max(1,block),channels);exchangeProfile();latency=flow.latencySamples();}
    else if(kind==GillKind::Heat){heat.prepare(fs,std::max(1,block),channels);latency=heat.latencySamples();}
    else {tune.prepare(fs,std::max(1,block),channels);latency=tune.latencySamples();}
    updateParameters();
    for(auto& r:dryRing)r.assign(static_cast<size_t>((kind==GillKind::Tune?tune.maximumLatencySamples():latency)+1),0.f);dryPosition=0;
    heat.setLiveMode(!qualityClient.isPro());tune.setLiveMode(!qualityClient.isPro());
    latency=kind==GillKind::Heat?heat.latencySamples():kind==GillKind::Tune?tune.latencySamples():0;
    bypassFade.reset(actualFs,.005);bypassFade.setCurrentAndTargetValue(!supported||value("bypass")>.5f?1.f:0.f);
    setLatencySamples(latency);tail.store(static_cast<double>(latency)/actualFs+.25);
qualityTransition.prepare(actualFs,qualityClient.mode());    inputPeak=0;outputPeak=0;reduction=0;pitchHz=0;targetHz=0;pitchConfidence=0;
}
void GillVocalProcessor::releaseResources(){flow.reset();heat.reset();tune.reset();inputPeak=0;outputPeak=0;reduction=0;learnCommand=0;}
void GillVocalProcessor::exchangeProfile(){
    const juce::SpinLock::ScopedTryLockType lock(profileLock);if(!lock.isLocked())return;
    if(profilePending){flow.setLearnedProfile(profile);profilePending=false;}
    profile=flow.learnedProfile();
}
gill::LearnProfile GillVocalProcessor::savedProfile()const {const juce::SpinLock::ScopedLockType lock(profileLock);return profile;}
void GillVocalProcessor::process(juce::AudioBuffer<float>& buffer,bool hostBypass){const int blockQuality=qualityClient.mode();
    juce::ScopedNoDenormals denormals;const int channels=std::min(2,buffer.getNumChannels()),count=buffer.getNumSamples();
    if(channels<=0||count<=0)return;
    for(int c=getTotalNumInputChannels();c<buffer.getNumChannels();++c)buffer.clear(c,0,count);
    heat.setLiveMode(blockQuality==0);tune.setLiveMode(blockQuality==0);
    latency=kind==GillKind::Heat?heat.latencySamples():kind==GillKind::Tune?tune.latencySamples():0;
    qualityClient.requestLatencySamples(latency);tail.store(double(latency)/uiRate.load()+.25);
    updateParameters();
    if(kind==GillKind::Flow){exchangeProfile();const int command=learnCommand.exchange(0);if(command==1)flow.startLearning();else if(command==2)flow.cancelLearning();}
    const auto bypassIndex=kind==GillKind::Flow?3u:kind==GillKind::Heat?6u:5u;
    bypassFade.setTargetValue(hostBypass||!rateSupported.load()||parameter(bypassIndex)>.5f?1.f:0.f);
    constexpr int chunk=128;std::array<std::array<float,chunk>,2> dry{};
    float peakIn=0,peakOut=0;
    for(int start=0;start<count;start+=chunk){const int n=std::min(chunk,count-start);std::array<float*,2> ptr{};
        for(int c=0;c<channels;++c)ptr[c]=buffer.getWritePointer(c,start);
        for(int i=0;i<n;++i){
            for(int c=0;c<channels;++c){const float raw=ptr[c][i];const float x=std::isfinite(raw)?juce::jlimit(-100.f,100.f,raw):0.f;ptr[c][i]=x;peakIn=std::max(peakIn,std::abs(x));
                if(dryRing[c].empty())dry[c][i]=x;
                else {dryRing[c][dryPosition]=x;dry[c][i]=dryRing[c][(dryPosition+dryRing[c].size()-static_cast<size_t>(latency))%dryRing[c].size()];}}
            if(!dryRing[0].empty())dryPosition=(dryPosition+1)%dryRing[0].size();
        }
        if(kind==GillKind::Flow)flow.process(ptr.data(),channels,n);
        else if(kind==GillKind::Heat)heat.process(ptr.data(),channels,n);
        else tune.process(ptr.data(),channels,n);
        for(int i=0;i<n;++i){const auto bypass=bypassFade.getNextValue();for(int c=0;c<channels;++c){const auto wet=std::isfinite(ptr[c][i])?ptr[c][i]:0.f;ptr[c][i]=bypass>=1?dry[c][i]:wet+bypass*(dry[c][i]-wet);peakOut=std::max(peakOut,std::abs(ptr[c][i]));}}
    }

    qualityTransition.process(buffer.getArrayOfWritePointers(),channels,count,blockQuality,kind!=GillKind::Flow);
    const float decay=static_cast<float>(std::exp(-count/(uiRate.load()*.12)));inputPeak.store(std::max(peakIn,inputPeak.load()*decay));outputPeak.store(std::max(peakOut,outputPeak.load()*decay));
    if(kind==GillKind::Flow){exchangeProfile();reduction=flow.gainReductionDb();learnProgress=flow.learningProgress();learnState=flow.learningState();}
    else if(kind==GillKind::Tune){pitchHz=tune.detectedHz();targetHz=tune.targetHz();pitchConfidence=tune.confidence();}
}
void GillVocalProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& m){m.clear();process(b,false);}
void GillVocalProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& m){m.clear();process(b,true);}
const juce::String GillVocalProcessor::getProgramName(int i){if(kind!=GillKind::Tune)return "DEFAULT";const char* names[]{"NATURAL","POP","RAP","TRAP","ROBOT"};return names[juce::jlimit(0,4,i)];}
void GillVocalProcessor::setCurrentProgram(int i){if(kind!=GillKind::Tune)return;i=juce::jlimit(0,4,i);currentProgram.store(i);setValue("retune",presetRetune[i]);setValue("humanize",presetHuman[i]);setValue("mix",100);}
bool GillVocalProcessor::presetMatches()const{const auto i=juce::jlimit(0,4,currentProgram.load());return kind==GillKind::Tune&&std::abs(value("retune")-presetRetune[i])<.01f&&std::abs(value("humanize")-presetHuman[i])<.01f&&std::abs(value("mix")-100)<.01f;}
void GillVocalProcessor::getStateInformation(juce::MemoryBlock& out){auto s=apvts.copyState();s.setProperty("version",1,nullptr);s.setProperty("program",currentProgram.load(),nullptr);
    if(kind==GillKind::Flow){const auto p=savedProfile();s.setProperty("learnValid",p.valid,nullptr);s.setProperty("learnRms",p.rmsDb,nullptr);s.setProperty("learnPeak",p.peakDb,nullptr);s.setProperty("learnCrest",p.crestDb,nullptr);s.setProperty("learnThreshold",p.thresholdDb,nullptr);
        s.setProperty("learnVersion",static_cast<int>(p.version),nullptr);s.setProperty("learnRange",p.dynamicRangeDb,nullptr);s.setProperty("learnMotion",p.motionDb,nullptr);s.setProperty("learnAttack",p.attackMs,nullptr);s.setProperty("learnRelease",p.releaseMs,nullptr);}
    if(auto xml=s.createXml())copyXmlToBinary(*xml,out);
}
void GillVocalProcessor::setStateInformation(const void* data,int bytes){
    if(!data||bytes<=0||bytes>1024*1024)return;
    if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(apvts.state.getType())){auto incoming=juce::ValueTree::fromXml(*xml);auto clean=apvts.copyState();
    if (!incoming.getChildWithProperty("id","gillQuality").isValid()) { auto oldQuality=clean.getChildWithProperty("id","gillQuality"); if(oldQuality.isValid()) oldQuality.setProperty("value",apvts.getParameter("gillQuality")->convertFrom0to1(apvts.getParameter("gillQuality")->getDefaultValue()),nullptr); }
bool changed=false;
        for(auto child:incoming){auto id=child.getProperty("id").toString();auto* p=apvts.getParameter(id);double v=0;if(!p||!child.hasProperty("value")||!number(child.getProperty("value"),v))continue;
            auto target=clean.getChildWithProperty("id",id);if(target.isValid()){const auto& r=p->getNormalisableRange();target.setProperty("value",r.snapToLegalValue(static_cast<float>(juce::jlimit(static_cast<double>(r.start),static_cast<double>(r.end),v))),nullptr);changed=true;}}
        if(changed)apvts.replaceState(clean);
        double v=0;if(incoming.hasProperty("program")&&number(incoming.getProperty("program"),v))currentProgram.store(static_cast<int>(juce::jlimit(0.,4.,v)));
        if(kind==GillKind::Flow&&incoming.hasProperty("learnValid")){gill::LearnProfile p;bool good=true;p.valid=static_cast<bool>(incoming.getProperty("learnValid"));
            auto read=[&](const char* id,float& field){double x=0;if(!number(incoming.getProperty(id),x))good=false;else field=static_cast<float>(x);};
            read("learnRms",p.rmsDb);read("learnPeak",p.peakDb);read("learnCrest",p.crestDb);read("learnThreshold",p.thresholdDb);
            if(incoming.hasProperty("learnVersion")){double version=0;if(!number(incoming.getProperty("learnVersion"),version)||(version!=1&&version!=2))good=false;else p.version=static_cast<std::uint32_t>(version);}
            if(p.version==2){read("learnRange",p.dynamicRangeDb);read("learnMotion",p.motionDb);read("learnAttack",p.attackMs);read("learnRelease",p.releaseMs);}
            if(good){gill::FlowDSP validator;if(validator.setLearnedProfile(p)){const juce::SpinLock::ScopedLockType lock(profileLock);profile=validator.learnedProfile();profilePending=true;}}
        }
    }
}
juce::AudioProcessorEditor* GillVocalProcessor::createEditor(){return new GillVocalEditor(*this);}
