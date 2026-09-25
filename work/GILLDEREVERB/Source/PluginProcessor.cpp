#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
float amplitudeDb(double v) { return static_cast<float>(20.0 * std::log10(std::max(1.e-5, v))); }
double read(std::atomic<float>* p, double lo, double hi, double fallback) {
    return gilldereverb::finiteClamp(p->load(std::memory_order_relaxed),lo,hi,fallback);
}
}
GillDereverbAudioProcessor::GillDereverbAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true)
                                     .withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),
      apvts(*this,nullptr,"GILLDEREVERB_STATE",createParameterLayout()) {
    amountParam=apvts.getRawParameterValue("amount"); roomParam=apvts.getRawParameterValue("room");
    preserveParam=apvts.getRawParameterValue("preserve"); lowParam=apvts.getRawParameterValue("low");
    highParam=apvts.getRawParameterValue("high"); mixParam=apvts.getRawParameterValue("mix");
    outputParam=apvts.getRawParameterValue("output"); removedParam=apvts.getRawParameterValue("removed");
    bypassParam=apvts.getRawParameterValue("bypass"); alternateState=apvts.copyState();
    setLatencySamples(gilldereverb::fftSize);
}
juce::AudioProcessorValueTreeState::ParameterLayout GillDereverbAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto add=[&](const char* id,const char* name,float low,float high,float step,float value,float centre=0.f) {
        juce::NormalisableRange<float> range(low,high,step);
        if(centre>low && centre<high) range.setSkewForCentre(centre);
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,range,value));
    };
    add("amount","AMOUNT",0,100,0.1f,55);
    add("room","ROOM",80,1500,1,450,450);
    add("preserve","VOICE PROTECT",0,100,0.1f,75);
    add("low","LOW",20,1000,0.1f,80,140);
    add("high","HIGH",1000,20000,1,16000,6000);
    add("mix","MIX",0,100,0.1f,100);
    add("output","OUTPUT",-12,12,0.01f,0);
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"removed",1},"REMOVED",false));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));
    return p;
}
bool GillDereverbAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto out=l.getMainOutputChannelSet();
    return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==l.getMainInputChannelSet();
}
void GillDereverbAudioProcessor::prepareToPlay(double fs,int) {
    fs=gilldereverb::validSampleRate(fs); engine.prepare(fs);
    for(auto* s:{&gainSmooth,&bypassSmooth,&removedSmooth,&mixSmooth}) s->reset(fs,0.02);
    const bool automatic=automaticMode.load(std::memory_order_relaxed);
    gainSmooth.setCurrentAndTargetValue(automatic?1.0:std::pow(10.0,read(outputParam,-12,12,0)/20.0));
    bypassSmooth.setCurrentAndTargetValue(read(bypassParam,0,1,0)>0.5?1.0:0.0);
    removedSmooth.setCurrentAndTargetValue(!automatic && read(removedParam,0,1,0)>0.5?1.0:0.0);
    mixSmooth.setCurrentAndTargetValue(automatic?1.0:read(mixParam,0,100,100)*0.01);
    analyzerIndex=0; spectrumReady.store(false); inputAccumulator.fill(0); outputAccumulator.fill(0);
    inputDb.store(-100); outputDb.store(-100); reductionDb.store(0); setLatencySamples(engine.getLatencySamples());
}
void GillDereverbAudioProcessor::releaseResources() { engine.reset(); inputDb.store(-100); outputDb.store(-100); reductionDb.store(0); }
template<class T> void GillDereverbAudioProcessor::process(juce::AudioBuffer<T>& b,bool hostBypassed) {
    juce::ScopedNoDenormals noDenormals;
    const int n=b.getNumSamples(),nc=std::min(2,b.getNumChannels());
    if(n<1 || nc<1) return;
    for(int c=getTotalNumInputChannels();c<b.getNumChannels();++c)b.clear(c,0,n);
    const bool automatic=automaticMode.load(std::memory_order_relaxed);
    const double amount=read(amountParam,0,100,55)*0.01;
    auto p=gilldereverb::autoParameters(amount);
    if(!automatic) {
        p=gilldereverb::Params{};
        p.amount=amount; p.roomMs=read(roomParam,80,1500,450);
        p.preserve=read(preserveParam,0,100,75)*0.01; p.lowHz=read(lowParam,20,1000,80); p.highHz=read(highParam,1000,20000,16000);
    }
    engine.setParameters(p);
    gainSmooth.setTargetValue(automatic?1.0:std::pow(10.0,read(outputParam,-12,12,0)/20.0));
    bypassSmooth.setTargetValue(hostBypassed || read(bypassParam,0,1,0)>0.5 ? 1.0:0.0);
    removedSmooth.setTargetValue(!automatic && read(removedParam,0,1,0)>0.5 ? 1.0:0.0);
    mixSmooth.setTargetValue(automatic?1.0:read(mixParam,0,100,100)*0.01);
    constexpr int chunkSize=128;
    std::array<std::array<T,chunkSize>,2> dry{};
    std::array<T*,2> dryPtrs{dry[0].data(),dry[1].data()};
    const bool analyze=analyzerEnabled.load(std::memory_order_relaxed);
    double peakIn=0,peakOut=0;
    for(int offset=0;offset<n;offset+=chunkSize) {
        const int count=std::min(chunkSize,n-offset); std::array<T*,2> data{};
        for(int c=0;c<nc;++c) data[static_cast<size_t>(c)]=b.getWritePointer(c,offset);
        engine.process(data.data(),nc,count,dryPtrs.data());
        for(int j=0;j<count;++j) {
            const double trim=gainSmooth.getNextValue(),by=bypassSmooth.getNextValue(),removed=removedSmooth.getNextValue(),mix=mixSmooth.getNextValue();
            for(int c=0;c<nc;++c) {
                const double original=static_cast<double>(dry[static_cast<size_t>(c)][static_cast<size_t>(j)]);
                const double wet=static_cast<double>(data[static_cast<size_t>(c)][j]);
                // REMOVED auditions exactly dry-minus-processed before MIX and OUTPUT.
                // Every branch uses the same delay, including host bypass.
                const double normal=original+mix*(wet-original), difference=original-wet;
                const double processed=normal*trim+removed*(difference-normal*trim);
                double value=by>=1.0?original:(by<=0.0?processed:processed+by*(original-processed));
                if(!std::isfinite(value))value=0;
                auto cast=static_cast<T>(value); if(!std::isfinite(static_cast<double>(cast)))cast=T{};
                data[static_cast<size_t>(c)][j]=cast;
                peakIn=std::max(peakIn,std::abs(original));peakOut=std::max(peakOut,std::abs(static_cast<double>(cast)));
            }
            if(analyze)feedAnalyzer(static_cast<float>(dry[0][static_cast<size_t>(j)]),static_cast<float>(data[0][j]));
        }
    }
    const float decay=static_cast<float>(n/std::max(8000.0,getSampleRate())*30.0);
    inputDb.store(std::max(amplitudeDb(peakIn),inputDb.load()-decay),std::memory_order_relaxed);
    outputDb.store(std::max(amplitudeDb(peakOut),outputDb.load()-decay),std::memory_order_relaxed);
    reductionDb.store(static_cast<float>((1.0-bypassSmooth.getCurrentValue())*engine.getReductionDb()),std::memory_order_relaxed);
}
void GillDereverbAudioProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,false);}
void GillDereverbAudioProcessor::processBlock(juce::AudioBuffer<double>&b,juce::MidiBuffer&m){m.clear();process(b,false);}
void GillDereverbAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GillDereverbAudioProcessor::processBlockBypassed(juce::AudioBuffer<double>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GillDereverbAudioProcessor::feedAnalyzer(float pre,float post) {
    inputAccumulator[static_cast<size_t>(analyzerIndex)]=pre;outputAccumulator[static_cast<size_t>(analyzerIndex)]=post;
    if(++analyzerIndex==2048) {analyzerIndex=0;if(!spectrumReady.load(std::memory_order_acquire)) {
        publishedPre=inputAccumulator;publishedPost=outputAccumulator;spectrumReady.store(true,std::memory_order_release);
    }}
}
bool GillDereverbAudioProcessor::readSpectrum(std::array<float,2048>&pre,std::array<float,2048>&post) {
    if(!spectrumReady.load(std::memory_order_acquire))return false;
    pre=publishedPre;post=publishedPost;spectrumReady.store(false,std::memory_order_release);return true;
}
juce::AudioProcessorParameter* GillDereverbAudioProcessor::getBypassParameter() const { return apvts.getParameter("bypass"); }
void GillDereverbAudioProcessor::getStateInformation(juce::MemoryBlock&out) {
    auto tree=apvts.copyState();tree.setProperty("version",2,nullptr);
    tree.setProperty("automatic",automaticMode.load(std::memory_order_relaxed),nullptr);
    if(auto xml=tree.createXml())copyXmlToBinary(*xml,out);
}
void GillDereverbAudioProcessor::setStateInformation(const void* data,int size) {
    if(!data || size<=0 || size>1024*1024)return;
    if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(apvts.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);if(!state.isValid())return;
        // Only accept known parameters with finite, clamped values.
        auto clean=apvts.copyState();bool restoredParameter=false;
        for(auto child:state) {
            auto* param=apvts.getParameter(child.getProperty("id").toString());if(!param)continue;
            const double value=static_cast<double>(child.getProperty("value"));if(!std::isfinite(value))continue;
            auto target=clean.getChildWithProperty("id",child.getProperty("id"));
            if(target.isValid()) {target.setProperty("value",param->convertFrom0to1(juce::jlimit(0.f,1.f,param->convertTo0to1(static_cast<float>(value)))),nullptr);restoredParameter=true;}
        }
        if(restoredParameter) {
            // Version 1 has no automatic flag. A version 2 re-save of such an
            // instance explicitly retains false until a user edits Amount.
            const bool automatic=state.hasProperty("automatic")?static_cast<bool>(state.getProperty("automatic")):(static_cast<int>(state.getProperty("version",1))>=2);
            automaticMode.store(automatic,std::memory_order_relaxed);
            apvts.replaceState(clean);
        }
    }
}
void GillDereverbAudioProcessor::copyAtoB(){alternateState=apvts.copyState();}
void GillDereverbAudioProcessor::swapAB(){auto current=apvts.copyState();if(alternateState.isValid())apvts.replaceState(alternateState);alternateState=current;}
void GillDereverbAudioProcessor::resetAllParameters(){automaticMode.store(true,std::memory_order_relaxed);for(auto* p:getParameters()){p->beginChangeGesture();p->setValueNotifyingHost(p->getDefaultValue());p->endChangeGesture();}}
void GillDereverbAudioProcessor::beginUserAmountGesture(){
    if(!automaticMode.exchange(true,std::memory_order_relaxed))
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}
juce::AudioProcessorEditor* GillDereverbAudioProcessor::createEditor(){return new GillDereverbAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillDereverbAudioProcessor();}
