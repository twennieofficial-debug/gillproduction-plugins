#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr const char* profileIds[]{"frequency","q","threshold","maxcut","amount","profile","learnActive","learnVoice","learnSibilant"};
constexpr const char* names[]{"NATURAL RAP","SOFT S","BRIGHT VOCAL","DARK VOCAL","STRONG S"};
constexpr float presets[5][5]{{6500,.8f,-48,12,55},{7000,.9f,-43,9,45},{8500,1.2f,-48,12,60},{4500,.85f,-48,12,55},{6500,.7f,-55,16,75}};
}
juce::AudioProcessorValueTreeState::ParameterLayout GillSmartDeEsserProcessor::layout(){
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto number=[&](const char* id,const char* name,float lo,float hi,float step,float initial){p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(lo,hi,step),initial));};
    number("amount","AMOUNT",0,100,.1f,55);number("frequency","LEARNED CENTRE",2500,12000,1,6500);
    number("q","LEARNED Q",.6f,2.5f,.001f,.8f);number("threshold","LEARNED THRESHOLD",-72,-6,.1f,-48);
    number("maxcut","LEARNED MAXIMUM",3,18,.1f,12);
    number("learnActive","ANALYSED ACTIVE SECONDS",0,30,.001f,0);number("learnVoice","ANALYSED VOICE SECONDS",0,30,.001f,0);number("learnSibilant","ANALYSED SIBILANCE SECONDS",0,30,.001f,0);
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"profile",1},"LEARNED PROFILE",false));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"listen",1},"LISTEN REMOVED",false));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));
    p.add(gill::qualityParameter());return p;
}
GillSmartDeEsserProcessor::GillSmartDeEsserProcessor():AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),apvts(*this,nullptr,"GILLSMARTDEESSER_STATE",layout()){
    const char* ids[]{"amount","frequency","q","threshold","maxcut","bypass","listen"};
    for(size_t i=0;i<realtimeParameters.size();++i)realtimeParameters[i]=apvts.getRawParameterValue(ids[i]);
}
bool GillSmartDeEsserProcessor::isBusesLayoutSupported(const BusesLayout& b)const{return b.getMainInputChannelSet()==b.getMainOutputChannelSet()&&(b.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());}
float GillSmartDeEsserProcessor::value(const char* id)const noexcept{if(auto* p=apvts.getRawParameterValue(id))return p->load(std::memory_order_relaxed);return 0;}
void GillSmartDeEsserProcessor::setValue(const char* id,float v,bool gesture){if(auto* p=apvts.getParameter(id)){if(!std::isfinite(v))return;if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}}
void GillSmartDeEsserProcessor::prepareToPlay(double rate,int){
    supported.store(std::isfinite(rate)&&rate>=16000&&rate<=384000);const double fs=gillsmart::validSampleRate(rate);uiRate.store(fs);
    engine.setAmount(value("amount")*.01);engine.setProfile(value("frequency"),value("q"),value("threshold"),value("maxcut"));engine.prepare(fs);learner.prepare(fs);
    wetRamp.reset(fs,.008);wetRamp.setCurrentAndTargetValue(value("bypass")>.5?0:1);listenRamp.reset(fs,.008);listenRamp.setCurrentAndTargetValue(value("listen")>.5?1:0);
    collecting=false;request.store(0);activeSeconds.store(0);learnState.store(value("profile")>.5?applied:idle);setLatencySamples(0);
}
void GillSmartDeEsserProcessor::startLearning(){learnState.store(learning,std::memory_order_release);activeSeconds.store(0);request.store(1,std::memory_order_release);}
void GillSmartDeEsserProcessor::finishLearning(){if(learnState.load()==learning)request.store(2,std::memory_order_release);}
void GillSmartDeEsserProcessor::publish(const gillsmart::Profile& p)noexcept{
    candidateVersion.fetch_add(1,std::memory_order_acq_rel);
    const double values[]{p.frequency,p.q,p.threshold,p.maximum,p.amount,p.activeSeconds,p.voiceSeconds,p.sibilantSeconds};
    for(size_t i=0;i<candidate.size();++i)candidate[i].store(static_cast<float>(values[i]),std::memory_order_relaxed);
    candidateVersion.fetch_add(1,std::memory_order_release);learnState.store(p.valid?ready:insufficient,std::memory_order_release);
}
bool GillSmartDeEsserProcessor::learnedCandidate(gillsmart::Profile& p)const noexcept{
    for(int attempt=0;attempt<3;++attempt){const auto v=candidateVersion.load(std::memory_order_acquire);if(v&1)continue;
        std::array<float,8> a{};for(size_t i=0;i<a.size();++i)a[i]=candidate[i].load(std::memory_order_relaxed);
        if(v!=candidateVersion.load(std::memory_order_acquire))continue;
        p={a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],learnState.load()==ready};return gillsmart::validProfile(p);
    }return false;
}
bool GillSmartDeEsserProcessor::applyLearned(){
    gillsmart::Profile p;if(!learnedCandidate(p))return false;
    for(size_t i=0;i<undoValues.size();++i)undoValues[i]=value(profileIds[i]);
    const double values[]{p.frequency,p.q,p.threshold,p.maximum,p.amount,1,p.activeSeconds,p.voiceSeconds,p.sibilantSeconds};
    for(size_t i=0;i<undoValues.size();++i)setValue(profileIds[i],static_cast<float>(values[i]));
    undoAvailable.store(true);learnState.store(applied);return true;
}
bool GillSmartDeEsserProcessor::undoLearned(){if(!undoAvailable.exchange(false))return false;for(size_t i=0;i<undoValues.size();++i)setValue(profileIds[i],undoValues[i]);learnState.store(value("profile")>.5?applied:idle);return true;}
template<class T> void GillSmartDeEsserProcessor::process(juce::AudioBuffer<T>& buffer,bool hostBypass){
    juce::ScopedNoDenormals denormals;const int channels=std::min(2,buffer.getNumChannels()),samples=buffer.getNumSamples();if(channels<1)return;
    for(int c=channels;c<buffer.getNumChannels();++c)buffer.clear(c,0,samples);
    const int command=request.exchange(0,std::memory_order_acq_rel);
    if(command==1){learner.reset();collecting=true;}else if(command==2){publish(collecting?learner.result():gillsmart::Profile{});collecting=false;}else if(command==3){collecting=false;}
    if(!supported.load()){for(int c=0;c<channels;++c)for(int n=0;n<samples;++n)if(!std::isfinite(buffer.getSample(c,n)))buffer.setSample(c,n,T{});if(collecting){collecting=false;learnState.store(insufficient);}return;}
    if(collecting){learner.process(buffer.getArrayOfReadPointers(),channels,samples);activeSeconds.store(static_cast<float>(learner.activeSeconds()));if(learner.shouldFinish()){publish(learner.result());collecting=false;}}
    const auto parameter=[this](size_t i){return realtimeParameters[i]->load(std::memory_order_relaxed);};
    engine.setAmount(parameter(0)*.01);engine.setProfile(parameter(1),parameter(2),parameter(3),parameter(4));
    wetRamp.setTargetValue(hostBypass||parameter(5)>.5?0:1);listenRamp.setTargetValue(parameter(6)>.5?1:0);
    // Both modes use the same causal, double-precision filter. There is no
    // look-ahead, resampler or audio-thread quality allocation to hide.
    (void)qualityClient.mode();
    for(int offset=0;offset<samples;offset+=256){const int count=std::min(256,samples-offset);std::array<T*,2> ptr{};
        for(int c=0;c<channels;++c){ptr[c]=buffer.getWritePointer(c,offset);for(int i=0;i<count;++i){const double raw=static_cast<double>(ptr[c][i]);original[c][i]=std::isfinite(raw)&&std::abs(raw)<=1e100?raw:0;}}
        engine.process(ptr.data(),channels,count);
        for(int i=0;i<count;++i){const double wet=wetRamp.getNextValue(),listen=listenRamp.getNextValue();for(int c=0;c<channels;++c){const double dry=original[c][i],processed=static_cast<double>(ptr[c][i]),removed=dry-processed;
            const double result=dry+wet*((processed+(removed-processed)*listen)-dry);const T output=static_cast<T>(result);ptr[c][i]=std::isfinite(output)?output:T{};}}
    }
    reduction.store(static_cast<float>(engine.getReductionDb()));sibilance.store(static_cast<float>(engine.getSibilanceDb()));
}
void GillSmartDeEsserProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,false);}void GillSmartDeEsserProcessor::processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer&){process(b,false);}
void GillSmartDeEsserProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,true);}void GillSmartDeEsserProcessor::processBlockBypassed(juce::AudioBuffer<double>& b,juce::MidiBuffer&){process(b,true);}
const juce::String GillSmartDeEsserProcessor::getProgramName(int i){return names[juce::jlimit(0,4,i)];}
void GillSmartDeEsserProcessor::setCurrentProgram(int i){i=juce::jlimit(0,4,i);for(int k=0;k<5;++k)setValue(profileIds[k],presets[i][k],false);setValue("profile",0,false);for(int k=6;k<9;++k)setValue(profileIds[k],0,false);currentProgram.store(i);undoAvailable.store(false);learnState.store(idle);request.store(3);}
void GillSmartDeEsserProcessor::getStateInformation(juce::MemoryBlock& block){auto state=apvts.copyState();state.setProperty("schema",1,nullptr);state.setProperty("program",currentProgram.load(),nullptr);if(auto xml=state.createXml())copyXmlToBinary(*xml,block);}
void GillSmartDeEsserProcessor::setStateInformation(const void* data,int bytes){
    if(!data||bytes<=0||bytes>131072)return;const auto xml=getXmlFromBinary(data,bytes);if(!xml||!xml->hasTagName(apvts.state.getType()))return;
    const auto state=juce::ValueTree::fromXml(*xml);if(static_cast<int>(state.getProperty("schema",0))!=1)return;
    // Validate every parameter and reject malformed/non-finite/out-of-range data
    // before touching live state. Duplicate and unknown IDs are rejected too.
    juce::StringArray seen;
    for(auto child:state){const auto id=child.getProperty("id").toString();auto* parameter=apvts.getParameter(id);if(!parameter||seen.contains(id))return;seen.add(id);
        const auto string=child.getProperty("value").toString();char* end=nullptr;const double number=std::strtod(string.toRawUTF8(),&end);if(string.isEmpty()||!end||*end||!std::isfinite(number))return;
        const auto& range=parameter->getNormalisableRange();if(number<range.start-1e-5||number>range.end+1e-5)return;
        if((id=="profile"||id=="listen"||id=="bypass"||id==gill::qualityParameterId)&&number!=0&&number!=1)return;
    }
    if(seen.size()!=getParameters().size())return;
    const auto v=[&](const char* id){return static_cast<double>(state.getChildWithProperty("id",id).getProperty("value"));};
    if(v("profile")>.5&&(v("learnActive")<3.999||v("learnVoice")<2.499||v("learnSibilant")<.179||v("learnVoice")>v("learnActive")+.011||v("learnSibilant")>v("learnActive")+.011))return;
    apvts.replaceState(state);currentProgram.store(juce::jlimit(0,4,static_cast<int>(state.getProperty("program",0))));undoAvailable.store(false);learnState.store(value("profile")>.5?applied:idle);request.store(3);
}
juce::AudioProcessorEditor* GillSmartDeEsserProcessor::createEditor(){return new GillSmartDeEsserEditor(*this);}
