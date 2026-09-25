#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
namespace {
double finite(const std::atomic<float>* p,double fallback){const auto v=p?static_cast<double>(p->load(std::memory_order_relaxed)):fallback;return std::isfinite(v)?v:fallback;}
}
GillDeEsserAudioProcessor::GillDeEsserAudioProcessor()
 :AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),
  apvts(*this,nullptr,"GILL_DE_ESSER_STATE",createParameterLayout()) {
    amount=apvts.getRawParameterValue("amount");frequency=apvts.getRawParameterValue("frequency");
    listen=apvts.getRawParameterValue("listen");bypass=apvts.getRawParameterValue("bypass");
}
juce::AudioProcessorValueTreeState::ParameterLayout GillDeEsserAudioProcessor::createParameterLayout(){
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"amount",1},"AMOUNT",juce::NormalisableRange<float>(0,100,.1f),55.f));
    auto f=juce::NormalisableRange<float>(2500,12000,1.f);f.setSkewForCentre(6500);
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"frequency",1},"FREQUENCY",f,6500.f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"listen",1},"LISTEN S",false));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));l.add(gill::qualityParameter()); return l;
}
bool GillDeEsserAudioProcessor::isBusesLayoutSupported(const BusesLayout& l)const{
    const auto out=l.getMainOutputChannelSet();return (out==juce::AudioChannelSet::mono()||out==juce::AudioChannelSet::stereo())&&out==l.getMainInputChannelSet();
}
void GillDeEsserAudioProcessor::prepareToPlay(double fs,int){
    if(!std::isfinite(fs)||fs<8000||fs>768000)fs=48000;
    uiSampleRate.store(fs,std::memory_order_relaxed);engine.prepare(fs);
    engine.setAmount(juce::jlimit(0.,1.,finite(amount,55)*.01));engine.setFrequency(finite(frequency,6500));
    bypassSmooth.reset(fs,.005);bypassSmooth.setCurrentAndTargetValue(finite(bypass,0)>.5?1.:0.);
    listenSmooth.reset(fs,.005);listenSmooth.setCurrentAndTargetValue(finite(listen,0)>.5?1.:0.);
    setLatencySamples(engine.getLatencySamples());tailSeconds.store(engine.getTailLengthSeconds(),std::memory_order_relaxed);
    reductionDb.store(0,std::memory_order_relaxed);sibilanceDb.store(-160,std::memory_order_relaxed);
    spectrumIndex=0;accumulatorPre.fill(0);accumulatorPost.fill(0);
    // The published mailbox stays owned by the UI until readSpectrum releases it.
}
void GillDeEsserAudioProcessor::releaseResources(){engine.reset();reductionDb.store(0,std::memory_order_relaxed);sibilanceDb.store(-160,std::memory_order_relaxed);}
template<class T>void GillDeEsserAudioProcessor::process(juce::AudioBuffer<T>& buffer,bool hostBypassed){
    juce::ScopedNoDenormals denormals;const int channels=std::min(2,buffer.getNumChannels()),samples=buffer.getNumSamples();
    if(channels<=0||samples<=0)return;
    for(int c=getTotalNumInputChannels();c<buffer.getNumChannels();++c)buffer.clear(c,0,samples);
    engine.setAmount(juce::jlimit(0.,1.,finite(amount,55)*.01));engine.setFrequency(finite(frequency,6500));
    bypassSmooth.setTargetValue(hostBypassed||finite(bypass,0)>.5?1.:0.);listenSmooth.setTargetValue(finite(listen,0)>.5?1.:0.);
    constexpr int chunk=128;std::array<std::array<T,chunk>,2> dry{},band{};
    std::array<T*,2> bandPointers{band[0].data(),band[1].data()};
    for(int start=0;start<samples;start+=chunk){const int n=std::min(chunk,samples-start);std::array<T*,2> ptr{};
        for(int c=0;c<channels;++c){ptr[c]=buffer.getWritePointer(c,start);for(int i=0;i<n;++i)dry[c][i]=std::isfinite(static_cast<double>(ptr[c][i]))?ptr[c][i]:T{};}
        engine.process(ptr.data(),channels,n,bandPointers.data());
        for(int i=0;i<n;++i){const auto b=bypassSmooth.getNextValue(),s=listenSmooth.getNextValue();
            for(int c=0;c<channels;++c){const double processed=ptr[c][i];const double audition=s>=1?static_cast<double>(band[c][i]):processed+s*(static_cast<double>(band[c][i])-processed);
                const double x=b>=1?static_cast<double>(dry[c][i]):(b<=0?audition:audition+b*(static_cast<double>(dry[c][i])-audition));
                const auto out=static_cast<T>(x);ptr[c][i]=std::isfinite(static_cast<double>(out))?out:T{};}
            feedAnalyzer(static_cast<float>(dry[0][i]),static_cast<float>(ptr[0][i]));
        }
    }
    reductionDb.store(static_cast<float>(engine.getReductionDb()),std::memory_order_relaxed);
    sibilanceDb.store(static_cast<float>(engine.getSibilanceDb()),std::memory_order_relaxed);
}
void GillDeEsserAudioProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,false);}
void GillDeEsserAudioProcessor::processBlock(juce::AudioBuffer<double>&b,juce::MidiBuffer&m){m.clear();process(b,false);}
void GillDeEsserAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GillDeEsserAudioProcessor::processBlockBypassed(juce::AudioBuffer<double>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GillDeEsserAudioProcessor::feedAnalyzer(float pre,float post){
    accumulatorPre[static_cast<size_t>(spectrumIndex)]=pre;accumulatorPost[static_cast<size_t>(spectrumIndex)]=post;
    if(++spectrumIndex==2048){spectrumIndex=0;if(!spectrumReady.load(std::memory_order_acquire)){publishedPre=accumulatorPre;publishedPost=accumulatorPost;spectrumReady.store(true,std::memory_order_release);}}
}
bool GillDeEsserAudioProcessor::readSpectrum(std::array<float,2048>&pre,std::array<float,2048>&post){
    if(!spectrumReady.load(std::memory_order_acquire))return false;pre=publishedPre;post=publishedPost;spectrumReady.store(false,std::memory_order_release);return true;
}
juce::AudioProcessorParameter* GillDeEsserAudioProcessor::getBypassParameter()const{return apvts.getParameter("bypass");}
void GillDeEsserAudioProcessor::getStateInformation(juce::MemoryBlock& out){auto s=apvts.copyState();s.setProperty("version",1,nullptr);if(auto x=s.createXml())copyXmlToBinary(*x,out);}
void GillDeEsserAudioProcessor::setStateInformation(const void* data,int size){
    if(!data||size<=0||size>1024*1024)return;
    if(auto x=getXmlFromBinary(data,size))if(x->hasTagName(apvts.state.getType())){auto state=juce::ValueTree::fromXml(*x);if(!state.isValid())return;
        auto clean=apvts.copyState();
    if (!state.getChildWithProperty("id","gillQuality").isValid()) { auto oldQuality=clean.getChildWithProperty("id","gillQuality"); if(oldQuality.isValid()) oldQuality.setProperty("value",apvts.getParameter("gillQuality")->convertFrom0to1(apvts.getParameter("gillQuality")->getDefaultValue()),nullptr); }
bool changed=false;
        for(auto child:state){auto* parameter=apvts.getParameter(child.getProperty("id").toString());if(!parameter||!child.hasProperty("value"))continue;
            const double v=static_cast<double>(child.getProperty("value"));if(!std::isfinite(v))continue;const auto& r=parameter->getNormalisableRange();
            auto target=clean.getChildWithProperty("id",child.getProperty("id"));if(target.isValid()){target.setProperty("value",juce::jlimit(static_cast<double>(r.start),static_cast<double>(r.end),v),nullptr);changed=true;}}
        if(changed)apvts.replaceState(clean);
    }
}
juce::AudioProcessorEditor* GillDeEsserAudioProcessor::createEditor(){return new GillDeEsserAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillDeEsserAudioProcessor();}
