#include "PluginProcessor.h"
#include "PluginEditor.h"
namespace{
constexpr const char*ids[]{"target","range","speed","amount","gate","breath","sibilance","output","rideOn","gateOn","breathOn","sibilanceOn","bypass"};
constexpr float presets[6][8]={{-20,10,400,80,18,4,4,0},{-18,12,220,95,24,5,6,0},{-22,7,700,60,10,2,3,0},{-20,8,550,75,30,8,5,0},{-22,10,350,85,24,5,5,0},{-20,0,400,0,0,0,0,0}};
}
juce::AudioProcessorValueTreeState::ParameterLayout GillAssistProcessor::layout(){
    juce::AudioProcessorValueTreeState::ParameterLayout p;auto f=[&](const char*id,const char*name,float low,float high,float step,float initial){p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,juce::NormalisableRange<float>{low,high,step},initial));};
    f("target","TARGET RMS",-36,-10,.1f,-20);f("range","RIDE RANGE",0,18,.1f,10);f("speed","RIDE SPEED",80,2000,1,400);f("amount","RIDE AMOUNT",0,100,.1f,80);f("gate","GATE FLOOR",0,60,.1f,18);f("breath","BREATH REDUCTION",0,18,.1f,4);f("sibilance","SIBILANCE REDUCTION",0,18,.1f,4);f("output","OUTPUT",-18,6,.1f,0);
    for(auto id:{"rideOn","gateOn","breathOn","sibilanceOn"})p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,1},id,true));p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));p.add(gill::qualityParameter(1));return p;
}
GillAssistProcessor::GillAssistProcessor():AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),state(*this,&undoManager,"GILLASSIST",layout()),engine([this]{return settings();}){
    for(std::size_t i=0;i<raw.size();++i)raw[i]=state.getRawParameterValue(ids[i]);setLatencySamples(0);
}
float GillAssistProcessor::value(const char*id)const{const auto*p=state.getRawParameterValue(id);return p?p->load():0;}
void GillAssistProcessor::setValue(const char*id,float v){if(auto*p=state.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}}
gill::assist::Settings GillAssistProcessor::settings()const{
    // The worker can start while this constructor fills raw[]. APVTS exists
    // before Engine and is the stable atomic source during its entire lifetime.
    gill::assist::Settings s;s.target=value("target");s.range=value("range");s.speed=value("speed");s.amount=value("amount");s.gate=value("gate");s.breath=value("breath");s.sibilance=value("sibilance");s.output=value("output");s.rideOn=value("rideOn")>.5f;s.gateOn=value("gateOn")>.5f;s.breathOn=value("breathOn")>.5f;s.sibilanceOn=value("sibilanceOn")>.5f;return s;
}
void GillAssistProcessor::prepareToPlay(double fs,int){engine.prepare(fs,getTotalNumOutputChannels());setLatencySamples(0);}
bool GillAssistProcessor::isBusesLayoutSupported(const BusesLayout&b)const{return b.getMainInputChannelSet()==b.getMainOutputChannelSet()&&(b.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());}
void GillAssistProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,raw[12]->load()>.5f);}
void GillAssistProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,true);}
void GillAssistProcessor::process(juce::AudioBuffer<float>&b,bool bypass){
    juce::ScopedNoDenormals guard;for(int c=getTotalNumInputChannels();c<b.getNumChannels();++c)b.clear(c,0,b.getNumSamples());if(b.getNumChannels()==0)return;
    bool playing=false,valid=false;std::int64_t position=0;if(auto*playhead=getPlayHead()){if(auto info=playhead->getPosition()){playing=info->getIsPlaying();if(auto samples=info->getTimeInSamples()){position=*samples;valid=true;}else if(auto seconds=info->getTimeInSeconds()){valid=std::isfinite(*seconds)&&std::abs(*seconds)<1.e9;if(valid)position=std::int64_t(std::llround(*seconds*engine.rateView.load()));}}}
    playingView=playing;float peak=0;for(int c=0;c<b.getNumChannels();++c)peak=std::max(peak,b.getMagnitude(c,0,b.getNumSamples()));inputPeak=peak;
    engine.process(b,playing,valid,position,bypass);peak=0;for(int c=0;c<b.getNumChannels();++c)peak=std::max(peak,b.getMagnitude(c,0,b.getNumSamples()));outputPeak=peak;
}
const juce::String GillAssistProcessor::getProgramName(int i){static const char*names[]{"BALANCED VOCAL","RAP / TRAP","NATURAL SINGING","CLEAN DIALOGUE","BACKING VOCALS","MANUAL / NEUTRAL"};return names[juce::jlimit(0,5,i)];}
void GillAssistProcessor::setCurrentProgram(int i){i=juce::jlimit(0,5,i);preset=i;undoManager.beginNewTransaction("Preset");for(int j=0;j<8;++j)setValue(ids[j],presets[i][j]);for(int j=8;j<12;++j)setValue(ids[j],1);}
void GillAssistProcessor::swapAB(){auto current=state.copyState();if(!abSet){ab=current.createCopy();abSet=true;}else{state.replaceState(ab);ab=current;}engine.swapAB();}
void GillAssistProcessor::getStateInformation(juce::MemoryBlock&block){
    auto tree=state.copyState();auto transfer=engine.transferState();tree.setProperty("schema",1,nullptr);tree.setProperty("preset",preset.load(),nullptr);tree.setProperty("cache",transfer.token,nullptr);tree.setProperty("start",transfer.start,nullptr);
    auto old=tree.getChildWithName("EDITS");if(old.isValid())tree.removeChild(old,nullptr);juce::ValueTree edits("EDITS");for(const auto&e:transfer.edits){juce::ValueTree item("EDIT");item.setProperty("begin",e.begin,nullptr);item.setProperty("end",e.end,nullptr);item.setProperty("db",e.db,nullptr);item.setProperty("type",e.type,nullptr);edits.appendChild(item,nullptr);}tree.appendChild(edits,nullptr);if(auto xml=tree.createXml())copyXmlToBinary(*xml,block);
}
void GillAssistProcessor::setStateInformation(const void*data,int size){
    if(!data||size<1||size>2*1024*1024)return;auto xml=getXmlFromBinary(data,size);if(!xml||!xml->hasTagName("GILLASSIST"))return;auto tree=juce::ValueTree::fromXml(*xml);if(int(tree.getProperty("schema",1))!=1)return;
    for(auto child:tree){if(child.hasType("PARAM")){const auto id=child.getProperty("id").toString();auto*p=state.getParameter(id);double v=double(child.getProperty("value"));if(!p||!std::isfinite(v))return;const auto range=p->getNormalisableRange();if(v<range.start-1.e-5||v>range.end+1.e-5)return;}}
    std::vector<gill::assist::Edit>edits;auto list=tree.getChildWithName("EDITS");if(list.getNumChildren()>2000)return;for(auto e:list){double a=e.getProperty("begin"),b=e.getProperty("end"),d=e.getProperty("db");int type=e.getProperty("type");if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(d)||a<0||b<a||b>300||std::abs(d)>24||type<0||type>4)return;edits.push_back({a,b,float(d),type});}
    double start=double(tree.getProperty("start",0));if(!std::isfinite(start)||std::abs(start)>1.e9)return;const auto cache=tree.getProperty("cache").toString();if(cache.isNotEmpty()&&!gill::assist::Engine::validToken(cache))return;
    state.replaceState(tree);preset=juce::jlimit(0,5,int(tree.getProperty("preset",0)));if(cache.isNotEmpty())engine.restore(cache,start,edits);else engine.clearTransfer();
}
juce::AudioProcessorEditor*GillAssistProcessor::createEditor(){return new GillAssistEditor(*this);}
