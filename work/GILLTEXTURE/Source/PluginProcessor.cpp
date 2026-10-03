#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
constexpr const char* presets[3][6]{
    {"CLASSIC ROBOT", "DEEP TALK", "BRIGHT CIRCUIT", "CHORD HOOK", "SOFT SYNTH", "EXTERNAL CARRIER"},
    {"SOFT HALO", "OCTAVE SHIMMER", "LOW CLOUD", "SCATTERED WORDS", "DENSE TEXTURE", "WIDE SPARKLES"},
    {"STRAIGHT SIXTEENTHS", "OFFBEAT HOOK", "SYNCOPATED RAP", "SWUNG GATE", "GENTLE PUMP", "FAST FLICKER"}};
constexpr float patterns[4][16]{
    {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1},
    {1,0,.4f,1,0,1,0,.5f,1,0,1,0,.3f,0,1,.7f},
    {.1f,.3f,.65f,1,.1f,.3f,.65f,1,.1f,.3f,.65f,1,.1f,.3f,.65f,1}};
float clean(float x) { return std::isfinite(x) ? std::clamp(x,-16.f,16.f) : 0.f; }
juce::String stateId(TextureKind kind) { return juce::String(textureInfo(kind).name)+"_STATE"; }
}

juce::AudioProcessor::BusesProperties GillTextureProcessor::buses(TextureKind kind) {
    auto b=BusesProperties().withInput("VOCAL", juce::AudioChannelSet::stereo(), true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true);
    if(kind==TextureKind::Vocode) b=b.withInput("CARRIER",juce::AudioChannelSet::stereo(),false);
    return b;
}
GillTextureProcessor::GillTextureProcessor(TextureKind k)
    : AudioProcessor(buses(k)), kind(k), specs(textureParams(k)), apvts(*this,nullptr,stateId(k),layout(k)), engine(k) {
    for(std::size_t i=0;i<specs.size();++i) raw[i]=apvts.getRawParameterValue(specs[i].id);
    raw[specs.size()]=apvts.getRawParameterValue("bypass");
    raw[specs.size()+1]=apvts.getRawParameterValue("gillQuality");
    selectPreset(0,false);
}
juce::AudioProcessorValueTreeState::ParameterLayout GillTextureProcessor::layout(TextureKind kind) {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    for(const auto& p:textureParams(kind)) {
        if(!p.choices.isEmpty()) result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{p.id,1},p.label,p.choices,int(p.initial)));
        else result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{p.id,1},p.label,juce::NormalisableRange<float>(p.lo,p.hi,p.step),p.initial));
    }
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));
    result.add(gill::qualityParameter(1));
    return result;
}
bool GillTextureProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto main=layout.getMainInputChannelSet();
    if((main!=juce::AudioChannelSet::mono()&&main!=juce::AudioChannelSet::stereo())||main!=layout.getMainOutputChannelSet())return false;
    if(kind==TextureKind::Vocode) {
        if(layout.inputBuses.size()!=2)return false;
        const auto side=layout.inputBuses[1];
        return side.isDisabled()||side==juce::AudioChannelSet::mono()||side==juce::AudioChannelSet::stereo();
    }
    return layout.inputBuses.size()==1;
}
float GillTextureProcessor::param(std::size_t i) const {
    if(i>=raw.size()||!raw[i])return 0;
    const float v=raw[i]->load(std::memory_order_relaxed);
    return std::isfinite(v)?v:0;
}
float GillTextureProcessor::value(const juce::String& id) const {
    if(const auto* p=apvts.getRawParameterValue(id)){const auto v=p->load();return std::isfinite(v)?v:0;}return 0;
}
void GillTextureProcessor::setValue(const juce::String& id,float v,bool gesture) {
    if(!std::isfinite(v))return;
    if(auto* p=apvts.getParameter(id)) {if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}
}
gill::texture::Settings GillTextureProcessor::settings() const {
    gill::texture::Settings s;s.pro=quality.isPro();
    if(kind==TextureKind::Vocode){s.carrier=int(param(0));s.note=param(1);s.formant=param(2);s.response=param(3);s.brightness=param(4);s.unvoiced=param(5);}
    if(kind==TextureKind::Grain){s.size=param(0);s.density=param(1);s.pitch=param(2);s.scatter=param(3);s.feedback=param(4);s.width=param(5);}
    if(kind==TextureKind::Pulse){s.division=int(param(0));s.depth=param(1);s.smooth=param(2);s.swing=param(3);s.phase=param(4);for(int i=0;i<16;++i)s.steps[i]=param(std::size_t(6+i));}
    return s;
}
void GillTextureProcessor::prepareToPlay(double sampleRate,int block) {
    supported=std::isfinite(sampleRate)&&sampleRate>=8000&&sampleRate<=192000;rate=supported?sampleRate:48000;rateView=rate;
    engine.prepare(rate,std::max(1,block));setLatencySamples(0);
    const auto n=specs.size();mixSmooth=param(n-2)*.01;gainSmooth=std::pow(10.,param(n-1)/20.);bypassSmooth=param(n)>.5f?1:0;
    smoothing=1-std::exp(-1/(rate*.015));decay=std::exp(-1/(rate*.35));inputHold=outputHold=0;
    hadPosition=wasPlaying=false;scopeWrite=scopeClock=0;scopePosition=0;for(auto& x:scope)x=0;
}
void GillTextureProcessor::releaseResources(){engine.reset();hadPosition=false;}
void GillTextureProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){process(buffer,false);}
void GillTextureProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&){process(buffer,true);}
void GillTextureProcessor::process(juce::AudioBuffer<float>& buffer,bool hostBypass) {
    juce::ScopedNoDenormals noDenormals;
    const int frames=buffer.getNumSamples(),channels=std::min(2,getMainBusNumOutputChannels());
    if(frames<=0||channels<=0)return;
    if(!supported){for(int c=0;c<channels;++c)for(int i=0;i<frames;++i)buffer.setSample(c,i,clean(buffer.getSample(c,i)));return;}
    quality.requestLatencySamples(0);
    const auto s=settings();const auto n=specs.size();
    const double targetMix=param(n-2)*.01,targetGain=std::pow(10.,param(n-1)/20.),targetBypass=hostBypass||param(n)>.5f?1.:0.;
    // Host bypass is immediately dry. Re-enter processing from that exact
    // audible endpoint even when the host bypassed only a single tiny block.
    if(hostBypass)bypassSmooth=1;
    double bpm=kind==TextureKind::Pulse?param(5):120,ppq=0;bool ppqValid=false,tempoKnown=false,playing=true,known=false;std::int64_t position=0;
    if(auto* head=getPlayHead())if(auto t=head->getPosition()){
        playing=t->getIsPlaying();if(auto p=t->getTimeInSamples()){position=*p;known=true;}
        if(auto b=t->getBpm())if(std::isfinite(*b)&&*b>=20&&*b<=400){bpm=*b;tempoKnown=true;}
        if(auto p=t->getPpqPosition())if(std::isfinite(*p)){ppq=*p;ppqValid=true;}
    }
    bpm=std::clamp(bpm,20.,400.);tempo=bpm;hostTempo=tempoKnown;
    if(kind!=TextureKind::Pulse&&((wasPlaying&&!playing)||(known&&hadPosition&&playing&&position!=nextPosition)))engine.reset();
    wasPlaying=playing;hadPosition=known;nextPosition=position+(playing?frames:0);
    int sideChannels=0,sideOffset=0;
    if(kind==TextureKind::Vocode&&getBusCount(true)>1)if(auto* bus=getBus(true,1))if(bus->isEnabled()){
        sideChannels=std::min(2,bus->getNumberOfChannels());sideOffset=getChannelIndexInProcessBlockBuffer(true,1,0);
        if(sideOffset<0||sideOffset+sideChannels>buffer.getNumChannels())sideChannels=0;
    }
    carrierConnected=sideChannels>0;
    for(int offset=0;offset<frames;offset+=256){
        const int count=std::min(256,frames-offset);
        std::array<std::array<float,256>,2> dry{},wet{},carrier{};
        for(int c=0;c<channels;++c)for(int i=0;i<count;++i)dry[c][i]=wet[c][i]=clean(buffer.getSample(c,offset+i));
        for(int c=0;c<sideChannels;++c)for(int i=0;i<count;++i)carrier[c][i]=clean(buffer.getSample(sideOffset+c,offset+i));
        const double blockPpq=ppq+(playing?offset*bpm/(rate*60):0);
        engine.process(wet[0].data(),channels>1?wet[1].data():nullptr,sideChannels?carrier[0].data():nullptr,sideChannels>1?carrier[1].data():nullptr,count,s,bpm,blockPpq,ppqValid,playing);
        for(int i=0;i<count;++i){
            auto follow=[&](double& v,double target){v+=smoothing*(target-v);if(std::abs(v-target)<1.e-7)v=target;};
            follow(mixSmooth,targetMix);follow(gainSmooth,targetGain);follow(bypassSmooth,targetBypass);
            for(int c=0;c<channels;++c){
                const float original=dry[c][i];
                const float processed=clean(float((original+mixSmooth*(clean(wet[c][i])-original))*gainSmooth));
                const float out=(hostBypass||bypassSmooth==1)?original:clean(float(processed+(original-processed)*bypassSmooth));
                buffer.setSample(c,offset+i,out);inputHold=std::max(double(std::abs(original)),inputHold*decay);outputHold=std::max(double(std::abs(out)),outputHold*decay);
            }
            if(++scopeClock>=128){scope[scopeWrite%scope.size()]=buffer.getSample(0,offset+i);scopePosition=++scopeWrite;scopeClock=0;}
        }
    }
    if(kind==TextureKind::Pulse)currentStep=engine.currentStep();
    inputPeak=float(inputHold);outputPeak=float(outputHold);
}
const juce::String GillTextureProcessor::getProgramName(int i){return presets[int(kind)][std::clamp(i,0,5)];}
void GillTextureProcessor::applyPattern(int pattern){if(kind!=TextureKind::Pulse)return;for(int i=0;i<16;++i)setValue("step"+juce::String(i+1),patterns[std::clamp(pattern,0,3)][i]);}
void GillTextureProcessor::selectPreset(int i,bool gesture){
    i=std::clamp(i,0,5);auto set=[&](const char* id,float v){setValue(id,v,gesture);};
    for(const auto& p:specs)setValue(p.id,p.initial,gesture);set("bypass",0);
    if(kind==TextureKind::Vocode){set("carrier",i==3?2:i==2?1:i==5?3:0);set("note",i==1?36:i==2?60:i==3?48:i==4?55:48);set("formant",i==1?-4:i==2?4:0);set("response",i==4?80:i==2?20:50);set("brightness",i==1?30:i==2?90:i==4?45:65);set("unvoiced",i==2?45:i==4?15:25);set("mix",i==4?65:100);}
    if(kind==TextureKind::Grain){set("size",i==1?120:i==2?210:i==3?35:i==4?170:i==5?25:80);set("density",i==3?4:i==4?26:i==5?22:9);set("pitch",i==1?12:i==2?-12:i==5?7:0);set("scatter",i==3?90:i==4?65:i==5?80:40);set("feedback",i==1?30:i==2?35:i==3?10:i==4?50:15);set("width",i==2?40:i==5?100:75);set("mix",i==3?65:i==4?80:i==2?50:40);set("output",i==4?-4:0);}
    if(kind==TextureKind::Pulse){const int pattern=i==1?1:i==2?2:i==4?3:0;for(int j=0;j<16;++j)setValue("step"+juce::String(j+1),patterns[pattern][j],gesture);set("division",i==4?1:i==5?3:2);set("depth",i==4?60:100);set("smooth",i==4?35:i==5?3:8);set("swing",i==3?35:0);}
    for(std::size_t j=0;j<specs.size()+2;++j)presetValues[j]=param(j);program=i;
}
bool GillTextureProcessor::presetMatches()const {for(std::size_t i=0;i<specs.size()+1;++i)if(std::abs(param(i)-presetValues[i].load())>1.e-5f)return false;return true;}
void GillTextureProcessor::getStateInformation(juce::MemoryBlock& bytes){
    auto tree=apvts.copyState();tree.setProperty("schema",1,nullptr);tree.setProperty("program",program.load(),nullptr);
    for(std::size_t i=0;i<specs.size()+1;++i)tree.setProperty("preset_"+juce::String(int(i)),presetValues[i].load(),nullptr);
    if(auto xml=tree.createXml())copyXmlToBinary(*xml,bytes);
}
void GillTextureProcessor::setStateInformation(const void* data,int bytes){
    if(!data||bytes<=0||bytes>1024*1024)return;auto xml=getXmlFromBinary(data,bytes);if(!xml||!xml->hasTagName(stateId(kind)))return;
    auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||int(tree.getProperty("schema",0))!=1)return;
    for(std::size_t i=0;i<specs.size()+2;++i){const juce::String id=i<specs.size()?specs[i].id:i==specs.size()?"bypass":"gillQuality";auto child=tree.getChildWithProperty("id",id);if(!child.isValid())return;const float v=float(child.getProperty("value"));if(!std::isfinite(v))return;const float lo=i<specs.size()?specs[i].lo:0,hi=i<specs.size()?specs[i].hi:1;child.setProperty("value",std::clamp(v,lo,hi),nullptr);}
    apvts.replaceState(tree);program=std::clamp(int(tree.getProperty("program",0)),0,5);
    for(std::size_t i=0;i<specs.size()+1;++i){const float v=float(tree.getProperty("preset_"+juce::String(int(i)),param(i)));presetValues[i]=std::isfinite(v)?v:param(i);}
}
juce::AudioProcessorEditor* GillTextureProcessor::createEditor(){return new GillTextureEditor(*this);}
