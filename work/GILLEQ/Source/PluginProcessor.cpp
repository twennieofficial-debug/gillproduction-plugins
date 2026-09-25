#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
constexpr std::array<float,8> centers { 40.f,100.f,250.f,630.f,1600.f,4000.f,10000.f,16000.f };
juce::String bandId(int i, const char* suffix) { return "band" + juce::String(i+1) + "_" + suffix; }
float amplitudeDb(double value) { return static_cast<float>(20.0 * std::log10(std::max(1.0e-5, value))); }
}

GilleqAudioProcessor::GilleqAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("INPUT", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("OUTPUT", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GILLEQ_STATE", createParameterLayout()) {
    for (int i=0;i<8;++i) {
        bandAtoms[static_cast<size_t>(i)] = {apvts.getRawParameterValue(bandId(i,"enabled")),apvts.getRawParameterValue(bandId(i,"type")),
            apvts.getRawParameterValue(bandId(i,"freq")),apvts.getRawParameterValue(bandId(i,"gain")),apvts.getRawParameterValue(bandId(i,"q")),
            apvts.getRawParameterValue(bandId(i,"channel")),apvts.getRawParameterValue(bandId(i,"slope")),
            apvts.getRawParameterValue(bandId(i,"dynamic")),apvts.getRawParameterValue(bandId(i,"threshold")),
            apvts.getRawParameterValue(bandId(i,"range")),apvts.getRawParameterValue(bandId(i,"attack")),apvts.getRawParameterValue(bandId(i,"release"))};
    }
    outputParam=apvts.getRawParameterValue("output"); bypassParam=apvts.getRawParameterValue("bypass"); deltaParam=apvts.getRawParameterValue("delta");
    alternateState=apvts.copyState();
}

juce::AudioProcessorValueTreeState::ParameterLayout GilleqAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    for(int i=0;i<8;++i) {
        const auto prefix = "BAND " + juce::String(i+1) + " ";
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{bandId(i,"enabled"),1},prefix+"ENABLED",true));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{bandId(i,"type"),1},prefix+"TYPE",juce::StringArray{"BELL","LOW SHELF","HIGH SHELF","LOW CUT","HIGH CUT","NOTCH"},0));
        juce::NormalisableRange<float> freq(20.f,20000.f,0.01f); freq.setSkewForCentre(632.455532f);
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"freq"),1},prefix+"FREQUENCY",freq,centers[static_cast<size_t>(i)]));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"gain"),1},prefix+"GAIN",juce::NormalisableRange<float>(-24.f,24.f,0.01f),0.f));
        juce::NormalisableRange<float> q(0.1f,18.f,0.0f); q.setSkewForCentre(1.0f);
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"q"),1},prefix+"Q",q,0.70710678f));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{bandId(i,"channel"),1},prefix+"CHANNEL",juce::StringArray{"STEREO","MID","SIDE","LEFT","RIGHT"},0));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{bandId(i,"slope"),1},prefix+"SLOPE",juce::StringArray{"12 DB/OCT","24 DB/OCT","48 DB/OCT"},0));
    }
    p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"OUTPUT",juce::NormalisableRange<float>(-24.f,24.f,0.01f),0.f));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"delta",1},"DELTA LISTEN",false));
    // Do not insert into the original layout: all 59 legacy IDs, indices,
    // ranges and version hints above remain unchanged for saved automation.
    for(int i=0;i<8;++i) {
        const auto prefix="BAND "+juce::String(i+1)+" ";
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{bandId(i,"dynamic"),2},prefix+"DYNAMIC",false));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"threshold"),2},prefix+"THRESHOLD",juce::NormalisableRange<float>(-80.f,0.f,0.1f),-24.f));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"range"),2},prefix+"DYNAMIC RANGE",juce::NormalisableRange<float>(-24.f,24.f,0.1f),-6.f));
        juce::NormalisableRange<float> attack(0.1f,200.f,0.1f); attack.setSkewForCentre(10.f);
        juce::NormalisableRange<float> release(10.f,2000.f,1.f); release.setSkewForCentre(150.f);
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"attack"),2},prefix+"ATTACK",attack,10.f));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bandId(i,"release"),2},prefix+"RELEASE",release,150.f));
    }
    p.add(gill::qualityParameter()); return p;
}

bool GilleqAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out=layouts.getMainOutputChannelSet();
    return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==layouts.getMainInputChannelSet();
}
void GilleqAudioProcessor::prepareToPlay(double sampleRate,int) {
    engine.prepare(sampleRate); engine.setBands(getBands());
    gainSmooth.reset(sampleRate,0.02); bypassSmooth.reset(sampleRate,0.02); deltaSmooth.reset(sampleRate,0.02);
    gainSmooth.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(static_cast<double>(outputParam->load())));
    bypassSmooth.setCurrentAndTargetValue(bypassParam->load()>0.5f?1.0:0.0);
    deltaSmooth.setCurrentAndTargetValue(deltaParam->load()>0.5f?1.0:0.0);
    analyzerIndex=0; spectrumReady.store(false); inputAccumulator.fill(0); outputAccumulator.fill(0);
    inputDb.store(-100); outputDb.store(-100); setLatencySamples(0);
    publishDynamics();
}
void GilleqAudioProcessor::releaseResources() { engine.reset(); inputDb.store(-100); outputDb.store(-100); publishDynamics(); }
gill::Bands GilleqAudioProcessor::getBands() const {
    gill::Bands bands;
    for(size_t i=0;i<bands.size();++i) {
        const auto& a=bandAtoms[i]; auto& b=bands[i];
        b.enabled=a.enabled->load(std::memory_order_relaxed)>0.5f;
        b.type=static_cast<int>(gill::finiteClamp(a.type->load(std::memory_order_relaxed),0,5,0)); b.channel=static_cast<int>(gill::finiteClamp(a.channel->load(std::memory_order_relaxed),0,4,0));
        b.slope=static_cast<int>(gill::finiteClamp(a.slope->load(std::memory_order_relaxed),0,2,0)); b.frequency=a.freq->load(std::memory_order_relaxed);
        b.gainDb=a.gain->load(std::memory_order_relaxed); b.q=a.q->load(std::memory_order_relaxed);
        b.dynamic=a.dynamic->load(std::memory_order_relaxed)>0.5f;
        b.thresholdDb=a.threshold->load(std::memory_order_relaxed); b.dynamicRangeDb=a.range->load(std::memory_order_relaxed);
        b.attackMs=a.attack->load(std::memory_order_relaxed); b.releaseMs=a.release->load(std::memory_order_relaxed);
    }
    return bands;
}
template<class T> void GilleqAudioProcessor::process(juce::AudioBuffer<T>& buffer,bool hostBypassed) {
    juce::ScopedNoDenormals noDenormals;
    const int n=buffer.getNumSamples(), channels=std::min(2,buffer.getNumChannels());
    if(channels<1 || n<1) return;
    for(int c=getTotalNumInputChannels();c<buffer.getNumChannels();++c) buffer.clear(c,0,n);
    engine.setBands(getBands());
    const double outputDbValue=gill::finiteClamp(outputParam->load(std::memory_order_relaxed),-24,24,0);
    gainSmooth.setTargetValue(std::pow(10.0,outputDbValue/20.0));
    bypassSmooth.setTargetValue((hostBypassed || bypassParam->load(std::memory_order_relaxed)>0.5f)?1.0:0.0);
    deltaSmooth.setTargetValue(deltaParam->load(std::memory_order_relaxed)>0.5f?1.0:0.0);
    double peakIn=0,peakOut=0;
    constexpr int chunkSize=128;
    std::array<std::array<T,chunkSize>,2> dry{};
    const bool analyze=analyzerEnabled.load(std::memory_order_relaxed);
    for(int offset=0;offset<n;offset+=chunkSize) {
        const int count=std::min(chunkSize,n-offset); std::array<T*,2> data{};
        for(int c=0;c<channels;++c) {
            data[static_cast<size_t>(c)]=buffer.getWritePointer(c,offset);
            for(int j=0;j<count;++j) {
                auto& input=data[static_cast<size_t>(c)][j];
                if(!std::isfinite(static_cast<double>(input))) input=T{};
                dry[static_cast<size_t>(c)][static_cast<size_t>(j)]=input;
                peakIn=std::max(peakIn,std::abs(static_cast<double>(input)));
            }
        }
        engine.process(data.data(),channels,count);
        for(int j=0;j<count;++j) {
            const double trim=gainSmooth.getNextValue(), bypass=bypassSmooth.getNextValue(),delta=deltaSmooth.getNextValue();
            for(int c=0;c<channels;++c) {
                const double original=dry[static_cast<size_t>(c)][static_cast<size_t>(j)];
                const double processed=static_cast<double>(data[static_cast<size_t>(c)][j])*trim-delta*original;
                double value=bypass>=1.0?original:(bypass<=0.0?processed:processed+bypass*(original-processed));
                if(!std::isfinite(value)) value=0;
                T castValue=static_cast<T>(value);
                if(!std::isfinite(static_cast<double>(castValue))) castValue=T{};
                data[static_cast<size_t>(c)][j]=castValue; peakOut=std::max(peakOut,std::abs(static_cast<double>(castValue)));
            }
            if(analyze) feedAnalyzer(static_cast<float>(dry[0][static_cast<size_t>(j)]),static_cast<float>(data[0][j]));
        }
    }
    const float decay=static_cast<float>(n/std::max(8000.0,getSampleRate())*30.0);
    inputDb.store(std::max(amplitudeDb(peakIn),inputDb.load()-decay),std::memory_order_relaxed);
    outputDb.store(std::max(amplitudeDb(peakOut),outputDb.load()-decay),std::memory_order_relaxed);
    publishDynamics();
}
void GilleqAudioProcessor::publishDynamics() {
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free,"Meter publication must not lock the audio thread");
    liveSequence.fetch_add(1,std::memory_order_acq_rel);
    const auto bands=engine.getCurrentBands();
    for(size_t i=0;i<bands.size();++i) {
        const auto& b=bands[i]; auto& live=liveBands[i];
        live.gain.store(static_cast<float>(b.gainDb),std::memory_order_relaxed);
        live.frequency.store(static_cast<float>(b.frequency),std::memory_order_relaxed);
        live.q.store(static_cast<float>(b.q),std::memory_order_relaxed);
        live.dynamicGain.store(static_cast<float>(engine.getDynamicGainDb(i)),std::memory_order_relaxed);
        live.detectorDb.store(static_cast<float>(engine.getDetectorDb(i)),std::memory_order_relaxed);
        live.type.store(b.type,std::memory_order_relaxed); live.channel.store(b.channel,std::memory_order_relaxed);
        live.active.store(gill::dynamicsActive(b),std::memory_order_relaxed);
    }
    liveSequence.fetch_add(1,std::memory_order_release);
}
float GilleqAudioProcessor::getBandDynamicGainDb(int index) const { return index>=0 && index<8?liveBands[static_cast<size_t>(index)].dynamicGain.load(std::memory_order_relaxed):0.f; }
float GilleqAudioProcessor::getBandDetectorDb(int index) const { return index>=0 && index<8?liveBands[static_cast<size_t>(index)].detectorDb.load(std::memory_order_relaxed):-160.f; }
void GilleqAudioProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,false);}
void GilleqAudioProcessor::processBlock(juce::AudioBuffer<double>&b,juce::MidiBuffer&m){m.clear();process(b,false);}
void GilleqAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GilleqAudioProcessor::processBlockBypassed(juce::AudioBuffer<double>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GilleqAudioProcessor::feedAnalyzer(float pre,float post) {
    inputAccumulator[static_cast<size_t>(analyzerIndex)]=pre; outputAccumulator[static_cast<size_t>(analyzerIndex)]=post;
    if(++analyzerIndex==2048) {
        analyzerIndex=0;
        if(!spectrumReady.load(std::memory_order_acquire)) {
            publishedPre=inputAccumulator; publishedPost=outputAccumulator; spectrumReady.store(true,std::memory_order_release);
        }
    }
}
bool GilleqAudioProcessor::readSpectrum(std::array<float,2048>&pre,std::array<float,2048>&post) {
    if(!spectrumReady.load(std::memory_order_acquire)) return false;
    pre=publishedPre; post=publishedPost; spectrumReady.store(false,std::memory_order_release); return true;
}
GilleqAudioProcessor::ResponseSnapshot GilleqAudioProcessor::getResponseSnapshot() const {
    ResponseSnapshot result; const auto parameters=getBands(); result.bands=parameters;
    result.sampleRate=getSampleRate()>0?getSampleRate():48000;
    result.bypassed=bypassParam->load()>0.5f; result.mono=getTotalNumInputChannels()==1;
    result.trim=std::pow(10.0,gill::finiteClamp(outputParam->load(),-24,24,0)/20.0);
    result.delta=deltaParam->load()>0.5f?1.0:0.0;
    // Every field is atomic. The bounded sequence check avoids a UI wait or
    // audio-thread lock and usually captures one coherent published block.
    for(int attempt=0;attempt<3;++attempt) {
        const auto before=liveSequence.load(std::memory_order_acquire);
        if(before&1u) continue;
        auto candidate=parameters;
        for(size_t i=0;i<candidate.size();++i) {
            const auto& live=liveBands[i]; auto& b=candidate[i];
            if(gill::dynamicsActive(b) && live.active.load(std::memory_order_relaxed)
               && b.type==live.type.load(std::memory_order_relaxed) && b.channel==live.channel.load(std::memory_order_relaxed)) {
                b.gainDb=live.gain.load(std::memory_order_relaxed); b.frequency=live.frequency.load(std::memory_order_relaxed); b.q=live.q.load(std::memory_order_relaxed);
            }
        }
        if(before==liveSequence.load(std::memory_order_acquire)) { result.bands=candidate; break; }
    }
    return result;
}
double GilleqAudioProcessor::getResponseDb(double hz,int mode) const { return getResponseDb(hz,mode,getResponseSnapshot()); }
double GilleqAudioProcessor::getResponseDb(double hz,int mode,const ResponseSnapshot& snapshot) const {
    if(snapshot.bypassed) return 0.0;
    const double fs=snapshot.sampleRate,trim=snapshot.trim,delta=snapshot.delta;
    const auto& bands=snapshot.bands;
    double magnitude;
    if(snapshot.mono) {
        std::complex<double> h{1,0};
        for(const auto& b:bands) if(b.channel!=gill::Side && b.channel!=gill::Right) h*=gill::coefficientResponse(b,fs,hz);
        magnitude=std::abs(trim*h-delta);
    } else {
        auto m=gill::responseMatrix(bands,fs,hz);
        m.ll=m.ll*trim-delta; m.rr=m.rr*trim-delta; m.lr*=trim; m.rl*=trim;
        if(mode==gill::Left) magnitude=std::sqrt(std::norm(m.ll)+std::norm(m.rl));
        else if(mode==gill::Right) magnitude=std::sqrt(std::norm(m.lr)+std::norm(m.rr));
        else {const double polarity=mode==gill::Side?-1.0:1.0; magnitude=std::sqrt((std::norm(m.ll+polarity*m.lr)+std::norm(m.rl+polarity*m.rr))*0.5);}
    }
    return 20.0*std::log10(std::max(1.e-12,magnitude));
}
double GilleqAudioProcessor::getTailLengthSeconds() const {
    const double fs=getSampleRate()>0?getSampleRate():48000;
    double tail=0;
    for(const auto& b:getBands()) {
        double longest=0;
        for(int endpoint=0;endpoint<(gill::dynamicsActive(b)?2:1);++endpoint) {
            auto p=b;if(endpoint==1)p.gainDb=std::clamp(p.gainDb+p.dynamicRangeDb,-24.0,24.0);
            const auto filter=gill::designFilter(p,fs);double bandTail=0;
            for(int i=0;i<filter.count;++i) {
                const auto& c=filter.sections[static_cast<size_t>(i)];
                const auto root=std::sqrt(std::complex<double>(c.a1*c.a1-4*c.a2,0));
                const double radius=std::max(std::abs((-c.a1+root)*0.5),std::abs((-c.a1-root)*0.5));
                if(radius>0 && radius<1)bandTail+=std::log(1.e-9)/std::log(radius)/fs;
            }
            longest=std::max(longest,bandTail);
        }
        tail+=longest;
    }
    return std::min(600.0,tail+0.04);
}
juce::AudioProcessorParameter* GilleqAudioProcessor::getBypassParameter() const {return apvts.getParameter("bypass");}
void GilleqAudioProcessor::getStateInformation(juce::MemoryBlock&out) {
    auto tree=apvts.copyState(); tree.setProperty("version",2,nullptr);
    if(auto xml=tree.createXml()) copyXmlToBinary(*xml,out);
}
void GilleqAudioProcessor::setStateInformation(const void*data,int size) {
    if(!data || size<=0 || size>1024*1024) return;
    if(auto xml=getXmlFromBinary(data,size)) {
        if(xml->hasTagName(apvts.state.getType())) {
            const auto state=juce::ValueTree::fromXml(*xml);
            if(!state.isValid()) return;
            auto clean=apvts.copyState();
    if (!state.getChildWithProperty("id","gillQuality").isValid()) { auto oldQuality=clean.getChildWithProperty("id","gillQuality"); if(oldQuality.isValid()) oldQuality.setProperty("value",apvts.getParameter("gillQuality")->convertFrom0to1(apvts.getParameter("gillQuality")->getDefaultValue()),nullptr); }

            const bool legacy=static_cast<int>(state.getProperty("version",1))<2;
            // Missing parameters always have their documented defaults.
            // In particular, a v1 state cannot inherit active dynamics from
            // whatever project happened to occupy this instance beforehand.
            for(auto child:clean) if(auto* p=apvts.getParameter(child.getProperty("id").toString()))
                child.setProperty("value",p->convertFrom0to1(p->getDefaultValue()),nullptr);
            for(auto child:state) {
                const auto id=child.getProperty("id").toString(); auto* p=apvts.getParameter(id);
                if(!p || !child.hasProperty("value")) continue;
                if(legacy && (id.endsWith("_dynamic") || id.endsWith("_threshold") || id.endsWith("_range") || id.endsWith("_attack") || id.endsWith("_release"))) continue;
                const double value=static_cast<double>(child.getProperty("value"));
                if(!std::isfinite(value)) continue;
                auto target=clean.getChildWithProperty("id",id);
                if(target.isValid()) {
                    // APVTS state values are already physical values. A second
                    // skewed normalise/denormalise round trip can move an
                    // untouched legacy Q by several float ULPs. Preserve valid
                    // values verbatim; APVTS performs its own normalisation.
                    const auto& range=p->getNormalisableRange();
                    target.setProperty("value",juce::jlimit(static_cast<double>(range.start),static_cast<double>(range.end),value),nullptr);
                }
            }
            clean.setProperty("version",2,nullptr); apvts.replaceState(clean);
        }
    }
}
void GilleqAudioProcessor::copyAtoB(){alternateState=apvts.copyState();}
void GilleqAudioProcessor::swapAB(){auto current=apvts.copyState();if(alternateState.isValid())apvts.replaceState(alternateState);alternateState=current;}
void GilleqAudioProcessor::resetAllBands() {
    for(auto* p:getParameters()) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
    }
}
juce::AudioProcessorEditor* GilleqAudioProcessor::createEditor(){return new GilleqAudioProcessorEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GilleqAudioProcessor();}
