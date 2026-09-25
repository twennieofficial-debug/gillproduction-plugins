#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
double read(const std::atomic<float>* value, double fallback) {
    const double x = value ? value->load(std::memory_order_relaxed) : fallback;
    return std::isfinite(x) ? x : fallback;
}
}
GillRestorationAudioProcessor::GillRestorationAudioProcessor(gillrestoration::Mode m)
    : AudioProcessor(BusesProperties().withInput("INPUT", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("OUTPUT", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, m == gillrestoration::Mode::Declick ? "GILLDECLICK_STATE" : "GILLDECRACKLE_STATE", createParameterLayout()),
      mode(m) {
    amountParam = apvts.getRawParameterValue("amount");
    bypassParam = apvts.getRawParameterValue("bypass");
}
const juce::String GillRestorationAudioProcessor::getName() const {
    return mode == gillrestoration::Mode::Declick ? "GILLDECLICK" : "GILLDECRACKLE";
}
juce::String GillRestorationAudioProcessor::getCaption() const {
    return mode == gillrestoration::Mode::Declick ? "KLICKS REDUZIEREN" : "KNISTERN REDUZIEREN";
}
juce::AudioProcessorValueTreeState::ParameterLayout GillRestorationAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"amount", 1}, "AMOUNT", juce::NormalisableRange<float>(0,100,0.1f),55));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1}, "BYPASS",false));
    result.add(gill::qualityParameter()); return result;
}
bool GillRestorationAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto out = layout.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == layout.getMainInputChannelSet();
}
void GillRestorationAudioProcessor::prepareToPlay(double fs, int) {
    if (!std::isfinite(fs) || fs < 8000 || fs > 192000) fs = 48000;
    engine.prepare(fs, mode);
    engine.setLiveMode(!qualityClient.isPro());qualityClient.requestLatencySamples(engine.getLatencySamples());
    engine.setAmount(juce::jlimit(0.0,1.0,read(amountParam,55)*0.01));
    bypassSmooth.reset(fs, 0.005);
    bypassSmooth.setCurrentAndTargetValue(read(bypassParam,0) > 0.5 ? 1.0 : 0.0);
    engine.setLiveMode(!qualityClient.isPro());setLatencySamples(engine.getLatencySamples());
    qualityTransition.prepare(fs,qualityClient.mode());
}
void GillRestorationAudioProcessor::releaseResources() { engine.reset(); }
template<class T> void GillRestorationAudioProcessor::process(juce::AudioBuffer<T>& buffer, bool hostBypassed) {const int blockQuality=qualityClient.mode();
    juce::ScopedNoDenormals noDenormals;
    const int samples = buffer.getNumSamples(), channels = std::min(2, buffer.getNumChannels());
    if (samples <= 0 || channels <= 0) return;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear(c,0,samples);
    engine.setLiveMode(blockQuality==0);qualityClient.requestLatencySamples(engine.getLatencySamples());
    engine.setAmount(juce::jlimit(0.0,1.0,read(amountParam,55)*0.01));
    bypassSmooth.setTargetValue(hostBypassed || read(bypassParam,0)>0.5 ? 1.0 : 0.0);
    constexpr int chunk = 128;
    std::array<std::array<T,chunk>,2> dry{};
    std::array<T*,2> dryPointers{dry[0].data(),dry[1].data()};
    for (int start=0; start<samples; start+=chunk) {
        const int count=std::min(chunk,samples-start);
        std::array<T*,2> data{};
        for (int c=0;c<channels;++c) data[static_cast<size_t>(c)]=buffer.getWritePointer(c,start);
        engine.process(data.data(),channels,count,dryPointers.data());
        for (int i=0;i<count;++i) {
            const double bypass=bypassSmooth.getNextValue();
            for(int c=0;c<channels;++c) {
                const double wet=static_cast<double>(data[static_cast<size_t>(c)][i]);
                const double original=static_cast<double>(dry[static_cast<size_t>(c)][static_cast<size_t>(i)]);
                const double result=bypass>=1.0?original:(bypass<=0.0?wet:wet+bypass*(original-wet));
                const T cast=static_cast<T>(result);
                data[static_cast<size_t>(c)][i]=std::isfinite(static_cast<double>(cast))?cast:T{};
            }
        }
    }
    qualityTransition.process(buffer.getArrayOfWritePointers(),channels,samples,blockQuality);
}
void GillRestorationAudioProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& m){m.clear();process(b,false);}
void GillRestorationAudioProcessor::processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer& m){m.clear();process(b,false);}
void GillRestorationAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& m){m.clear();process(b,true);}
void GillRestorationAudioProcessor::processBlockBypassed(juce::AudioBuffer<double>& b,juce::MidiBuffer& m){m.clear();process(b,true);}
juce::AudioProcessorParameter* GillRestorationAudioProcessor::getBypassParameter() const { return apvts.getParameter("bypass"); }
void GillRestorationAudioProcessor::getStateInformation(juce::MemoryBlock& out) {
    auto state=apvts.copyState();state.setProperty("version",1,nullptr);
    if(auto xml=state.createXml())copyXmlToBinary(*xml,out);
}
void GillRestorationAudioProcessor::setStateInformation(const void* data,int size) {
    if(!data || size<=0 || size>1024*1024)return;
    if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(apvts.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);if(!state.isValid())return;
        auto clean=apvts.copyState();
    if (!state.getChildWithProperty("id","gillQuality").isValid()) { auto oldQuality=clean.getChildWithProperty("id","gillQuality"); if(oldQuality.isValid()) oldQuality.setProperty("value",apvts.getParameter("gillQuality")->convertFrom0to1(apvts.getParameter("gillQuality")->getDefaultValue()),nullptr); }
bool changed=false;
        for(auto child:state) {
            auto* parameter=apvts.getParameter(child.getProperty("id").toString());if(!parameter)continue;
            if(!child.hasProperty("value"))continue;
            const double value=static_cast<double>(child.getProperty("value"));if(!std::isfinite(value))continue;
            auto target=clean.getChildWithProperty("id",child.getProperty("id"));
            if(target.isValid()) {
                target.setProperty("value",parameter->convertFrom0to1(juce::jlimit(0.f,1.f,parameter->convertTo0to1(static_cast<float>(value)))),nullptr);
                changed=true;
            }
        }
        if(changed)apvts.replaceState(clean);
    }
}
juce::AudioProcessorEditor* GillRestorationAudioProcessor::createEditor() { return new GillRestorationAudioProcessorEditor(*this); }
