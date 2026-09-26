#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <cstdlib>
namespace{
const char*names[]{"GILLPHRASE","GILLDIRECTOR","GILLREPLY"};
const char*ids[]{"amount","length","tone","width","mix","dry","sensitivity","variant","bypass","gillQuality"};
const char*presetNames[3][6]={{"LAST WORD HALO","SHORT VERSE SPACE","DARK TAIL","TRAP CLOUD","BRIGHT ADLIB","ONLY THROWS"},{"RAP IN FRONT","OPEN HOOK","DARK VERSE","MOVING ADLIB","CLEAN INTIMATE","WIDE CHORUS"},{"SINGLE ANSWER","TRAP TRIPLETS","WIDE DOUBLES","LOW DENSITY","SHORT SYLLABLE","ONLY REPLIES"}};
// amount, time, tone, width, wet, dry, detector threshold, variant.
const float presets[3][6][8]={
 {{65,2.6f,55,105,38,100,-40,0},{45,.7f,60,75,26,100,-38,0},{75,3.8f,25,95,45,100,-42,1},{85,5.8f,48,140,42,100,-44,2},{65,1.8f,80,125,38,100,-40,2},{80,3.2f,60,120,80,0,-40,0}},
 {{55,1.1f,58,95,65,100,-40,0},{65,2.3f,64,125,62,100,-40,1},{65,1.8f,32,90,58,100,-42,0},{78,3.2f,65,140,64,100,-38,2},{35,.6f,50,90,55,100,-40,0},{65,2.8f,58,135,75,100,-42,1}},
 {{70,.3f,58,100,65,100,-40,0},{85,.18f,66,130,72,100,-42,2},{80,.28f,54,140,68,100,-40,1},{35,.25f,50,90,65,100,-38,0},{90,.12f,72,110,78,100,-42,2},{95,.35f,60,120,90,0,-40,1}}};
juce::Identifier stateId(CreativeKind k){return juce::String(names[static_cast<int>(k)])+"_STATE";}
}
GillCreativeProcessor::GillCreativeProcessor(CreativeKind k):AudioProcessor([k]{auto buses=BusesProperties().withInput("VOCAL",juce::AudioChannelSet::stereo(),true);if(k==CreativeKind::Director)buses=buses.withInput("BEAT GUIDE",juce::AudioChannelSet::stereo(),false);return buses.withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true);}()),kind(k),apvts(*this,nullptr,stateId(k),layout(k)),engine(k),archive(names[static_cast<int>(k)]){engine.setCaptureSink(&archive);for(size_t i=0;i<raw.size();++i)raw[i]=apvts.getRawParameterValue(ids[i]);selectPreset(0,false);}
juce::AudioProcessorValueTreeState::ParameterLayout GillCreativeProcessor::layout(CreativeKind k){
    juce::AudioProcessorValueTreeState::ParameterLayout result;auto f=[&](const char*id,const char*name,float low,float high,float step,float def){result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>(low,high,step),def));};
    f("amount",k==CreativeKind::Reply?"DENSITY":k==CreativeKind::Director?"INTENSITY":"THROW",0,100,.1f,65);
    f("length",k==CreativeKind::Reply?"CHOP LENGTH":"TAIL",k==CreativeKind::Reply?.05f:.15f,k==CreativeKind::Reply?1.5f:8.f,.01f,k==CreativeKind::Reply?.3f:2.6f);
    f("tone","TONE",0,100,.1f,55);f("width","WIDTH",0,150,.1f,100);f("mix","WET",0,100,.1f,k==CreativeKind::Director?65.f:38.f);f("dry","DRY",0,100,.1f,100);f("sensitivity","DETECT",-60,-18,.1f,-40);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"variant",1},"VARIANT",juce::StringArray{"I","II","III"},0));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));result.add(gill::qualityParameter(1));return result;
}
const juce::String GillCreativeProcessor::getName()const{return names[static_cast<int>(kind)];}
bool GillCreativeProcessor::isBusesLayoutSupported(const BusesLayout&l)const{const auto main=l.getMainOutputChannelSet();if((main!=juce::AudioChannelSet::mono()&&main!=juce::AudioChannelSet::stereo())||l.getMainInputChannelSet()!=main)return false;if(l.inputBuses.size()>1){const auto side=l.inputBuses[1];if(!side.isDisabled()&&side!=juce::AudioChannelSet::mono()&&side!=juce::AudioChannelSet::stereo())return false;}return true;}
float GillCreativeProcessor::value(const juce::String&id)const{auto*p=apvts.getRawParameterValue(id);const float v=p?p->load():0;return std::isfinite(v)?v:0;}
void GillCreativeProcessor::setValue(const juce::String&id,float v,bool gesture){if(!std::isfinite(v))return;if(auto*p=apvts.getParameter(id)){if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}}
void GillCreativeProcessor::prepareToPlay(double rate,int){rateSupported=std::isfinite(rate)&&rate>=8000&&rate<=384000;sampleRateView=rate;engine.prepare(rateSupported?rate:48000);setLatencySamples(0);prepared=true;if(pendingState.getSize()){auto state=std::move(pendingState);setStateInformation(state.getData(),static_cast<int>(state.getSize()));}}
void GillCreativeProcessor::releaseResources(){engine.finishCaptureOutsideProcess();}
void GillCreativeProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,false);}
void GillCreativeProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,true);}
void GillCreativeProcessor::process(juce::AudioBuffer<float>&b,bool hostBypass){
    juce::ScopedNoDenormals guard;const int count=b.getNumSamples(),channels=getTotalNumOutputChannels();if(count<=0||channels<=0||b.getNumChannels()<channels)return;
    if(!rateSupported){for(int c=0;c<channels;++c)for(int i=0;i<count;++i)b.setSample(c,i,gill::creative::finite(b.getSample(c,i)));return;}
    std::array<float,10>v{};for(size_t i=0;i<v.size();++i){const float x=raw[i]?raw[i]->load(std::memory_order_relaxed):0;v[i]=std::isfinite(x)?x:0;}
    gill::creative::Controls controls;controls.amount=std::clamp(v[0]/100,0.f,1.f);controls.length=std::clamp(v[1],.05f,8.f);controls.tone=std::clamp(v[2]/100,0.f,1.f);controls.width=std::clamp(v[3]/100,0.f,1.5f);controls.mix=std::clamp(v[4]/100,0.f,1.f);controls.dry=std::clamp(v[5]/100,0.f,1.f);controls.sensitivity=std::clamp(v[6],-60.f,-18.f);controls.variant=std::clamp(static_cast<int>(v[7]+.5f),0,2);controls.bypass=hostBypass||v[8]>.5f;controls.pro=quality.isPro();
    gill::creative::Transport transport;if(auto*head=getPlayHead())if(auto position=head->getPosition()){transport.playing=position->getIsPlaying();if(auto bpm=position->getBpm())if(std::isfinite(*bpm)&&*bpm>0)transport.bpm=*bpm;if(auto ppq=position->getPpqPosition())if(std::isfinite(*ppq)){transport.ppq=*ppq;transport.hasPPQ=true;}if(auto seconds=position->getTimeInSeconds())if(std::isfinite(*seconds)){transport.seconds=*seconds;transport.hasSeconds=true;}}
    tempo=static_cast<float>(transport.bpm);const float*sideL=nullptr,*sideR=nullptr;if(kind==CreativeKind::Director&&getBusCount(true)>1&&getBus(true,1)->isEnabled()){auto side=getBusBuffer(b,true,1);if(side.getNumChannels()>0){sideL=side.getReadPointer(0);sideR=side.getReadPointer(std::min(1,side.getNumChannels()-1));}}
    engine.process(b.getWritePointer(0),channels>1?b.getWritePointer(1):nullptr,sideL,sideR,count,controls,transport);
}
void GillCreativeProcessor::selectPreset(int index,bool gesture){index=std::clamp(index,0,5);const auto&v=presets[static_cast<int>(kind)][index];for(int i=0;i<8;++i)setValue(ids[i],v[i],gesture);setValue("bypass",0,gesture);currentProgram=index;}
const juce::String GillCreativeProcessor::getProgramName(int i){return presetNames[static_cast<int>(kind)][std::clamp(i,0,5)];}
bool GillCreativeProcessor::presetMatches()const{const auto&v=presets[static_cast<int>(kind)][currentProgram.load()];for(int i=0;i<8;++i)if(std::abs(value(ids[i])-v[i])>.011f)return false;return true;}
void GillCreativeProcessor::editPlan(const gill::creative::Plan&p,bool commit){if(!gill::creative::Engine::validPlan(p))return;const std::lock_guard<std::mutex>lock(modelProducer);engine.postPlan(p,commit);}
void GillCreativeProcessor::getStateInformation(juce::MemoryBlock&out){
    auto state=apvts.copyState();state.setProperty("schema",2,nullptr);state.setProperty("program",currentProgram.load(),nullptr);auto plan=engine.savedPlan();
    if(gill::creative::Engine::validPlan(plan)){
        juce::ValueTree model("MODEL");model.setProperty("origin",plan.originPPQ,nullptr);model.setProperty("originSeconds",plan.originSeconds,nullptr);model.setProperty("timeAnchor",plan.timeAnchor,nullptr);const auto source=archive.sourceFileForState(plan.sourceRevision);if(source.existsAsFile())model.setProperty("sourceFile",source.getFullPathName(),nullptr);model.setProperty("beats",plan.durationBeats,nullptr);model.setProperty("seconds",plan.durationSeconds,nullptr);model.setProperty("bpm",plan.bpm,nullptr);model.setProperty("applied",engine.isApplied(),nullptr);
        for(int i=0;i<plan.count;++i){const auto&m=plan.markers[i];juce::ValueTree event("MARKER");event.setProperty("start",m.startBeat,nullptr);event.setProperty("end",m.endBeat,nullptr);event.setProperty("in",m.startSec,nullptr);event.setProperty("out",m.endSec,nullptr);event.setProperty("strength",m.strength,nullptr);model.addChild(event,-1,nullptr);}
        for(int i=0;i<gill::creative::wavePoints;++i)model.setProperty("w"+juce::String(i),plan.waveform[i],nullptr);
        bool captureGood=kind!=CreativeKind::Reply||source.existsAsFile();
        if(kind==CreativeKind::Reply&&!captureGood)if(const auto*bank=engine.capture(plan.capture)){const int frames=bank->used.load();const int role=bank->role.load();const unsigned epoch=bank->epoch.load();if(frames>1&&frames<=bank->capacity&&(role==2||role==3)&&epoch==plan.captureEpoch){
            juce::MemoryOutputStream stream;stream.writeInt(frames);stream.writeDouble(bank->rate.load());for(int n=0;n<frames*2;++n)stream.writeFloat(bank->data[n].load(std::memory_order_relaxed));if(epoch==bank->epoch.load()&&bank->role.load()>=2){model.setProperty("audio",stream.getMemoryBlock().toBase64Encoding(),nullptr);captureGood=true;}}}
        if(captureGood)state.addChild(model,-1,nullptr);
    }
    if(auto xml=state.createXml())copyXmlToBinary(*xml,out);
}
void GillCreativeProcessor::setStateInformation(const void*data,int bytes){
    if(!data||bytes<=0||bytes>192*1024*1024)return;auto xml=getXmlFromBinary(data,bytes);if(!xml||!xml->hasTagName(stateId(kind).toString()))return;auto state=juce::ValueTree::fromXml(*xml);if(!state.isValid())return;
    const int schema=static_cast<int>(state.getProperty("schema",0));if(schema!=1&&schema!=2)return;
    juce::StringArray seen;int models=0;
    for(const auto&child:state){
        if(child.hasType("MODEL")){if(++models>1)return;continue;}
        if(!child.hasType("PARAM"))return;
        const auto id=child["id"].toString();auto*p=apvts.getParameter(id);if(!p||seen.contains(id))return;seen.add(id);
        const auto text=child["value"].toString();char*end=nullptr;const double value=std::strtod(text.toRawUTF8(),&end);
        if(text.isEmpty()||!end||*end||!std::isfinite(value))return;
        const auto range=p->getNormalisableRange();if(value<range.start-.001||value>range.end+.001)return;
        if((id=="variant"||id=="bypass"||id=="gillQuality")&&value!=std::floor(value))return;
    }
    if(seen.size()!=static_cast<int>(raw.size()))return;
    const auto model=state.getChildWithName("MODEL");gill::creative::Plan plan;bool commit=false;std::vector<float>audio;int frames=0;double rate=48000;juce::File sourceFile;bool missingSource=false;
    if(model.isValid()){
        plan.originPPQ=static_cast<double>(model["origin"]);plan.originSeconds=static_cast<double>(model["originSeconds"]);plan.timeAnchor=static_cast<bool>(model["timeAnchor"]);if(schema>=2){const auto sourcePath=model["sourceFile"].toString();if(sourcePath.isNotEmpty()){sourceFile=juce::File(sourcePath);missingSource=!sourceFile.existsAsFile();}}plan.durationBeats=static_cast<float>(model["beats"]);plan.durationSeconds=static_cast<float>(model["seconds"]);plan.bpm=static_cast<float>(model["bpm"]);commit=static_cast<bool>(model["applied"]);
        for(const auto&event:model)if(event.hasType("MARKER")){if(plan.count>=gill::creative::maxMarkers)return;plan.markers[plan.count++]={static_cast<float>(event["start"]),static_cast<float>(event["end"]),static_cast<float>(event["in"]),static_cast<float>(event["out"]),static_cast<float>(event["strength"])};}
        for(int i=0;i<gill::creative::wavePoints;++i){const float value=static_cast<float>(model["w"+juce::String(schema==1?i*128/gill::creative::wavePoints:i)]);if(!std::isfinite(value))return;plan.waveform[i]=std::clamp(value,0.f,16.f);}if(!gill::creative::Engine::validPlan(plan))return;
        if(kind==CreativeKind::Reply&&!sourceFile.existsAsFile()&&!missingSource){juce::MemoryBlock capture;if(!capture.fromBase64Encoding(model["audio"].toString())||capture.getSize()<12)return;juce::MemoryInputStream stream(capture,false);frames=stream.readInt();rate=stream.readDouble();if(frames<2||frames>48000*300||!std::isfinite(rate)||rate<8000||rate>48000||capture.getSize()!=12+static_cast<size_t>(frames)*8)return;audio.resize(static_cast<size_t>(frames)*2);for(auto&sample:audio){sample=stream.readFloat();if(!std::isfinite(sample)||std::abs(sample)>16)return;}}
    }
    if(!prepared){pendingState.replaceWith(data,static_cast<size_t>(bytes));}
    else if(model.isValid()&&!missingSource){
        const std::lock_guard<std::mutex>lock(modelProducer);
        if(kind==CreativeKind::Reply){
            if(sourceFile.existsAsFile()){juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>reader(formats.createReaderFor(sourceFile));if(!reader)return;plan.capture=engine.importSource(reader->lengthInSamples,reader->sampleRate,[&](float*l,float*r,std::int64_t offset,int n){float*channels[]{l,r};return reader->read(channels,2,offset,n);});}
            else plan.capture=engine.importCapture(audio.data(),frames,rate);
            if(plan.capture<0)return;plan.captureEpoch=engine.capture(plan.capture)->epoch.load();}
        if(sourceFile.existsAsFile())plan.sourceRevision=archive.restore(sourceFile);engine.postPlan(plan,commit);
    }
    else{engine.request(7);if(missingSource)archive.restore(sourceFile);}
    state.removeChild(model,nullptr);apvts.replaceState(state);currentProgram=std::clamp(static_cast<int>(state["program"]),0,5);
}
juce::AudioProcessorEditor*GillCreativeProcessor::createEditor(){return new GillCreativeEditor(*this);}

bool GillCreativeProcessor::renderEffects(bool effectsOnly){
    const auto model=plan();if(!canRender())return false;
    gill::creative::Controls c;c.amount=value("amount")/100;c.length=value("length");c.tone=value("tone")/100;c.width=value("width")/100;c.mix=value("mix")/100;c.dry=value("dry")/100;c.sensitivity=value("sensitivity");c.variant=static_cast<int>(value("variant"));
    return renderer.start(archive.sourceFile(model.sourceRevision),getName(),kind,model,c,effectsOnly);
}
