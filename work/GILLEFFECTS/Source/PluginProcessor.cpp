#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <cmath>
#include <cstdlib>
namespace {
const char* names[]{"GILLAIR","GILLSPACE","GILLECHO","GILLBALANCE"};
juce::Identifier stateId(GillKind k){return juce::String(names[static_cast<int>(k)])+"_STATE";}
bool number(const juce::var& v,double& result){auto s=v.toString().trim();if(s.isEmpty())return false;const char* start=s.toRawUTF8();char* end=nullptr;result=std::strtod(start,&end);return end!=start&&*end=='\0'&&std::isfinite(result);}
}
GillEffectProcessor::GillEffectProcessor(GillKind k)
:AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),kind(k),apvts(*this,nullptr,stateId(k),layout(k)){
    const char* a[]{"midair","highair","mix","output","bypass"};
    const char* s[]{"mix","decay","predelay","tone","size","width","style","mixlock","bypass"};
    const char* e[]{"time","feedback","mix","color","width","style","sync","division","bpm","mixlock","bypass"};
    const char* b[]{"amount","target","bypass"};
    const char** ids=k==GillKind::Air?a:k==GillKind::Space?s:k==GillKind::Echo?e:b;
    bypassIndex=k==GillKind::Air?4:k==GillKind::Space?8:k==GillKind::Echo?10:2;
    for(int i=0;i<=bypassIndex;++i)parameterValues[static_cast<size_t>(i)]=apvts.getRawParameterValue(ids[i]);
    if(k==GillKind::Space||k==GillKind::Echo)parameterValues[static_cast<size_t>(bypassIndex+1)]=apvts.getRawParameterValue("dry");
}
juce::AudioProcessorValueTreeState::ParameterLayout GillEffectProcessor::layout(GillKind k){
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto f=[&](const char* id,const char* name,float lo,float hi,float step,float def,float skew=1){l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,step,skew),def));};
    auto c=[&](const char* id,const char* name,juce::StringArray items,int def){l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id,1},name,items,def));};
    auto b=[&](const char* id,const char* name,bool def){l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,1},name,def));};
    if(k==GillKind::Air){f("midair","MID AIR",0,100,.1f,15);f("highair","HIGH AIR",0,100,.1f,15);f("mix","MIX",0,100,.1f,100);f("output","OUTPUT",-18,6,.1f,0);}
    else if(k==GillKind::Space){f("mix","MIX",0,100,.1f,18);f("decay","DECAY",.2f,15,.01f,.75f,.45f);f("predelay","PREDELAY",0,200,.1f,18);f("tone","TONE",0,100,.1f,58);f("size","SIZE",0,100,.1f,35);f("width","WIDTH",0,100,.1f,72);c("style","STYLE",{"ROOM","HALL","PLATE"},0);b("mixlock","MIX LOCK",false);}
    else if(k==GillKind::Echo){f("time","TIME",1,8000,.1f,500,.4f);f("feedback","FEEDBACK",0,90,.1f,28);f("mix","MIX",0,100,.1f,18);f("color","COLOR",0,100,.1f,38);f("width","WIDTH",0,100,.1f,80);c("style","STYLE",{"CLEAN","TAPE","PINGPONG"},0);b("sync","SYNC",true);c("division","DIVISION",{"1/16","1/8 T","1/8","1/8 D","1/4","1/4 D","1/2","1/1"},4);f("bpm","TEMPO",20,300,.1f,120);b("mixlock","MIX LOCK",false);}
    else{f("amount","AMOUNT",0,100,.1f,60);c("target","TARGET",{"NATURAL","POP","RAP LEAD","TRAP AIR","DARK RAP"},2);}
    b("bypass","BYPASS",false);
    // Append new parameters: keep every pre-existing host parameter index.
    if(k==GillKind::Space||k==GillKind::Echo)f("dry","DRY",0,100,.1f,100);
    l.add(gill::qualityParameter()); return l;
}
const juce::String GillEffectProcessor::getName()const{return names[static_cast<int>(kind)];}
bool GillEffectProcessor::isBusesLayoutSupported(const BusesLayout& l)const{auto o=l.getMainOutputChannelSet();return(o==juce::AudioChannelSet::mono()||o==juce::AudioChannelSet::stereo())&&o==l.getMainInputChannelSet();}
float GillEffectProcessor::value(const juce::String& id)const{auto* p=apvts.getRawParameterValue(id);const float v=p?p->load(std::memory_order_relaxed):0;return std::isfinite(v)?v:0;}
void GillEffectProcessor::setValue(const juce::String& id,float v,bool gesture){if(auto* p=apvts.getParameter(id)){if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}}
float GillEffectProcessor::parameter(size_t i)const noexcept{auto* p=parameterValues[i];const float v=p?p->load(std::memory_order_relaxed):0;return std::isfinite(v)?v:0;}
void GillEffectProcessor::updateParameters(bool queryHost){
    if(kind==GillKind::Air)air.setParameters(parameter(0),parameter(1),parameter(2),parameter(3));
    else if(kind==GillKind::Space)space.setParameters(parameter(0),parameter(1),parameter(2),parameter(3),parameter(4),parameter(5),juce::roundToInt(parameter(6)),parameter(9));
    else if(kind==GillKind::Echo){
        double bpm=parameter(8);bool validHost=false;
        if(queryHost)if(auto* playHead=getPlayHead())if(auto pos=playHead->getPosition())if(auto host=pos->getBpm())if(std::isfinite(*host)&&*host>0&&*host<=1000){bpm=*host;validHost=true;}
        if(!std::isfinite(bpm)||bpm<=0)bpm=120;
        const auto division=static_cast<size_t>(juce::jlimit(0,7,juce::roundToInt(parameter(7))));
        const double time=parameter(6)>.5f?60000./bpm*divisionBeats[division]:parameter(0);
        const float actual=static_cast<float>(juce::jlimit(1.,8000.,time));
        hostTempoAvailable=validHost;tempoBpm=static_cast<float>(bpm);delayMs=actual;delayLimited=time>8000||time<1;
        echo.setParameters(actual,parameter(1),parameter(2),parameter(3),parameter(4),juce::roundToInt(parameter(5)),parameter(11));
    }else balance.setParameters(parameter(0),juce::roundToInt(parameter(1)));
}
void GillEffectProcessor::prepareToPlay(double fs,int block){
    const bool supported=std::isfinite(fs)&&fs>=8000&&fs<=384000;rateSupported=supported;
    const double actualFs=std::isfinite(fs)&&fs>0?fs:48000.;if(!supported)fs=48000;
    air.setLiveMode(false);
    uiRate=actualFs;const int channels=juce::jlimit(1,2,getTotalNumOutputChannels());updateParameters();
    if(kind==GillKind::Air){air.prepare(fs,std::max(1,block),channels);latency=air.latencySamples();}
    else if(kind==GillKind::Space){space.prepare(fs,std::max(1,block),channels);latency=space.latencySamples();}
    else if(kind==GillKind::Echo){echo.prepare(fs,std::max(1,block),channels);latency=echo.latencySamples();}
    else{balance.prepare(fs,std::max(1,block),channels);exchangeProfile();latency=balance.latencySamples();}
    updateParameters();for(auto& r:dryRing)r.assign(static_cast<size_t>(latency+1),0.f);dryPosition=0;air.setLiveMode(!qualityClient.isPro());latency=kind==GillKind::Air?air.latencySamples():0;
    bypassFade.reset(actualFs,.005);bypassFade.setCurrentAndTargetValue(!supported||parameter(bypassIndex)>.5f?1.f:0.f);
    setLatencySamples(latency);tail=kind==GillKind::Space?space.tailSeconds():kind==GillKind::Echo?echo.tailSeconds():latency/actualFs+.15;
qualityTransition.prepare(actualFs,qualityClient.mode());    inputPeak=0;outputPeak=0;
    if(kind==GillKind::Balance)learnCommand=0;
}
void GillEffectProcessor::releaseResources(){air.reset();space.reset();echo.reset();balance.reset();inputPeak=0;outputPeak=0;learnCommand=0;if(kind==GillKind::Balance)exchangeProfile();}
void GillEffectProcessor::exchangeProfile(){const juce::SpinLock::ScopedTryLockType lock(profileLock);if(!lock.isLocked())return;
    if(profilePending){balance.setLearnedProfile(profile);profilePending=false;}profile=balance.learnedProfile();
    view={balance.currentGainsDb(),balance.preBandLevelsDb(),balance.postBandLevelsDb(),balance.activeBands(),balance.fineView()};
    learnState=balance.learningState();learnProgress=balance.learningProgress();
}
gill::LearnBalanceProfile GillEffectProcessor::savedProfile()const{const juce::SpinLock::ScopedLockType lock(profileLock);return profile;}
BalanceView GillEffectProcessor::balanceView()const{const juce::SpinLock::ScopedLockType lock(profileLock);return view;}
void GillEffectProcessor::process(juce::AudioBuffer<float>& buffer,bool hostBypass){const int blockQuality=qualityClient.mode();
    juce::ScopedNoDenormals denormals;const int channels=std::min(2,buffer.getNumChannels()),count=buffer.getNumSamples();if(channels<=0||count<=0)return;
    for(int c=getTotalNumInputChannels();c<buffer.getNumChannels();++c)buffer.clear(c,0,count);
    air.setLiveMode(blockQuality==0);latency=kind==GillKind::Air?air.latencySamples():0;qualityClient.requestLatencySamples(latency);
    updateParameters(true);if(kind==GillKind::Balance){exchangeProfile();const int command=learnCommand.exchange(0);if(command==1)balance.startLearning();else if(command==2)balance.cancelLearning();}
    bypassFade.setTargetValue(hostBypass||!rateSupported.load()||parameter(bypassIndex)>.5f?1.f:0.f);
    constexpr int chunk=128;std::array<std::array<float,chunk>,2> dry{};float peakIn=0,peakOut=0;
    for(int start=0;start<count;start+=chunk){const int n=std::min(chunk,count-start);std::array<float*,2> ptr{};for(int c=0;c<channels;++c)ptr[c]=buffer.getWritePointer(c,start);
        for(int i=0;i<n;++i){for(int c=0;c<channels;++c){const float raw=ptr[c][i],x=std::isfinite(raw)?juce::jlimit(-100.f,100.f,raw):0.f;ptr[c][i]=x;peakIn=std::max(peakIn,std::abs(x));if(dryRing[c].empty())dry[c][i]=x;else{dryRing[c][dryPosition]=x;dry[c][i]=dryRing[c][(dryPosition+dryRing[c].size()-static_cast<size_t>(latency))%dryRing[c].size()];}}if(!dryRing[0].empty())dryPosition=(dryPosition+1)%dryRing[0].size();}
        if(kind==GillKind::Air)air.process(ptr.data(),channels,n);else if(kind==GillKind::Space)space.process(ptr.data(),channels,n);else if(kind==GillKind::Echo)echo.process(ptr.data(),channels,n);else balance.process(ptr.data(),channels,n);
        for(int i=0;i<n;++i){const auto bypass=bypassFade.getNextValue();for(int c=0;c<channels;++c){const float wet=std::isfinite(ptr[c][i])?ptr[c][i]:0;ptr[c][i]=bypass>=1?dry[c][i]:wet+bypass*(dry[c][i]-wet);peakOut=std::max(peakOut,std::abs(ptr[c][i]));}}
    }

    qualityTransition.process(buffer.getArrayOfWritePointers(),channels,count,blockQuality,kind==GillKind::Air);
    const float decay=static_cast<float>(std::exp(-count/(uiRate.load()*.12)));inputPeak=std::max(peakIn,inputPeak.load()*decay);outputPeak=std::max(peakOut,outputPeak.load()*decay);
    if(kind==GillKind::Balance)exchangeProfile();
    if(kind==GillKind::Space)tail=space.tailSeconds();else if(kind==GillKind::Echo)tail=echo.tailSeconds();
}
void GillEffectProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& m){m.clear();process(b,false);}
void GillEffectProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& m){m.clear();process(b,true);}
const juce::String GillEffectProcessor::getProgramName(int i){i=juce::jlimit(0,11,i);return kind==GillKind::Space?gill::spacePresets[i].name:kind==GillKind::Echo?gill::echoPresets[i].name:"DEFAULT";}
void GillEffectProcessor::selectPreset(int i,bool gesture){if(kind!=GillKind::Space&&kind!=GillKind::Echo)return;i=juce::jlimit(0,11,i);currentProgram=i;
    auto set=[&](const char* id,float v){setValue(id,v,gesture);};const bool locked=value("mixlock")>.5f;
    if(kind==GillKind::Space){const auto& p=gill::spacePresets[i];if(!locked)set("mix",p.mix);set("decay",p.decay);set("predelay",p.pre);set("tone",p.tone);set("size",p.size);set("width",p.width);set("style",static_cast<float>(p.style));}
    else{const auto& p=gill::echoPresets[i];if(!locked)set("mix",p.mix);set("time",p.time);set("feedback",p.feedback);set("color",p.color);set("width",p.width);set("style",static_cast<float>(p.style));set("sync",p.sync?1.f:0.f);set("division",static_cast<float>(p.division));}
}
bool GillEffectProcessor::presetMatches()const{const int i=juce::jlimit(0,11,currentProgram.load());auto eq=[&](const char* id,float v){return std::abs(value(id)-v)<.011f;};const bool locked=value("mixlock")>.5f;
    if(kind==GillKind::Space){const auto& p=gill::spacePresets[i];return(locked||eq("mix",p.mix))&&eq("decay",p.decay)&&eq("predelay",p.pre)&&eq("tone",p.tone)&&eq("size",p.size)&&eq("width",p.width)&&eq("style",static_cast<float>(p.style));}
    if(kind==GillKind::Echo){const auto& p=gill::echoPresets[i];return(locked||eq("mix",p.mix))&&eq("time",p.time)&&eq("feedback",p.feedback)&&eq("color",p.color)&&eq("width",p.width)&&eq("style",static_cast<float>(p.style))&&eq("sync",p.sync?1.f:0.f)&&eq("division",static_cast<float>(p.division));}return false;
}
void GillEffectProcessor::getStateInformation(juce::MemoryBlock& out){auto s=apvts.copyState();s.setProperty("version",2,nullptr);s.setProperty("program",currentProgram.load(),nullptr);
    if(kind==GillKind::Balance){const auto p=savedProfile();s.setProperty("learnVersion",static_cast<int>(p.version),nullptr);s.setProperty("learnValid",p.valid,nullptr);s.setProperty("learnRate",p.sampleRate,nullptr);s.setProperty("learnRms",p.activeRmsDb,nullptr);for(size_t i=0;i<8;++i){s.setProperty("learnBand"+juce::String(i),p.bandDb[i],nullptr);s.setProperty("learnUse"+juce::String(i),p.validBands[i],nullptr);}
        if(p.version==2){s.setProperty("fineRate",p.fine.analysisRate,nullptr);s.setProperty("fineFrames",static_cast<int>(p.fine.frames),nullptr);for(int i=0;i<gill::FineBalanceProfile::points;++i)s.setProperty("fineBin"+juce::String(i),p.fine.db[i],nullptr);}}
    if(auto xml=s.createXml())copyXmlToBinary(*xml,out);
}
void GillEffectProcessor::setStateInformation(const void* data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;
    if(auto xml=getXmlFromBinary(data,bytes))if(xml->hasTagName(apvts.state.getType())){auto incoming=juce::ValueTree::fromXml(*xml);auto clean=apvts.copyState();
    if (!incoming.getChildWithProperty("id","gillQuality").isValid()) { auto oldQuality=clean.getChildWithProperty("id","gillQuality"); if(oldQuality.isValid()) oldQuality.setProperty("value",apvts.getParameter("gillQuality")->convertFrom0to1(apvts.getParameter("gillQuality")->getDefaultValue()),nullptr); }
bool changed=false;
        if((kind==GillKind::Space||kind==GillKind::Echo)&&!incoming.getChildWithProperty("id","dry").isValid()){clean.getChildWithProperty("id","dry").setProperty("value",100.f,nullptr);changed=true;}
        for(auto child:incoming){auto id=child.getProperty("id").toString();auto* p=apvts.getParameter(id);double v=0;if(!p||!child.hasProperty("value")||!number(child.getProperty("value"),v))continue;auto target=clean.getChildWithProperty("id",id);if(target.isValid()){const auto& range=p->getNormalisableRange();target.setProperty("value",range.snapToLegalValue(static_cast<float>(juce::jlimit(static_cast<double>(range.start),static_cast<double>(range.end),v))),nullptr);changed=true;}}
        if(changed)apvts.replaceState(clean);double v=0;if(incoming.hasProperty("program")&&number(incoming.getProperty("program"),v))currentProgram=static_cast<int>(juce::jlimit(0.,static_cast<double>(getNumPrograms()-1),v));
        if(kind==GillKind::Balance&&incoming.hasProperty("learnValid")){gill::LearnBalanceProfile p;bool good=true;double x=0;
            auto read=[&](const juce::Identifier& id,double& field){if(!incoming.hasProperty(id)||!number(incoming.getProperty(id),field))good=false;};
            read("learnVersion",x);if(x!=1&&x!=2)good=false;p.version=x==2?2:1;
            read("learnValid",x);if(x!=0&&x!=1)good=false;p.valid=x==1;
            read("learnRate",p.sampleRate);read("learnRms",x);p.activeRmsDb=static_cast<float>(x);
            for(size_t i=0;i<8;++i){read("learnBand"+juce::String(i),x);p.bandDb[i]=static_cast<float>(x);read("learnUse"+juce::String(i),x);if(x!=0&&x!=1)good=false;p.validBands[i]=x==1;}
            if(p.version==2){read("fineRate",p.fine.analysisRate);read("fineFrames",x);if(x<10||x>10000||std::floor(x)!=x)good=false;p.fine.frames=static_cast<uint32_t>(juce::jlimit(0.,10000.,x));for(int i=0;i<gill::FineBalanceProfile::points;++i){read("fineBin"+juce::String(i),x);p.fine.db[i]=static_cast<float>(x);}}
            if(good){gill::BalanceDSP validator;if(validator.setLearnedProfile(p)){const juce::SpinLock::ScopedLockType lock(profileLock);profile=validator.learnedProfile();profilePending=true;learnState=profile.valid?2:0;learnProgress=profile.valid?1.f:0.f;learnCommand=0;}}
        }
    }
}
juce::AudioProcessorEditor* GillEffectProcessor::createEditor(){return new GillEffectEditor(*this);}
