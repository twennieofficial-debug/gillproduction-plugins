#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
namespace {
const char* names[]{"GILLVOX","GILLOPTA","GILLBUSS","GILLQUAD","GILLSTAGE"};
struct Group {std::atomic<float>drive{0},trim{0},bypass{0},noise{0};};
std::array<Group,8> groups;
std::mutex groupLifetimeMutex;int groupInstances=0;
void clearGroups(){for(auto&g:groups){g.drive=0;g.trim=0;g.bypass=0;g.noise=0;}}
thread_local bool mirroringGroups=false;
bool groupId(const juce::String&id,int&group,int&field){const int length=id.length();if(length<4||id[0]!='g'||id[1]<'1'||id[1]>'8')return false;group=id[1]-'1';const char* suffix[]{"drive","trim","bypass","noise"};for(int f=0;f<4;++f){const int n=static_cast<int>(std::strlen(suffix[f]));if(length!=n+2)continue;bool same=true;for(int j=0;j<n;++j)same=same&&id[j+2]==suffix[f][j];if(same){field=f;return true;}}return false;}
std::atomic<float>& groupField(int i,int field){return field==0?groups[i].drive:field==1?groups[i].trim:field==2?groups[i].bypass:groups[i].noise;}
bool numeric(const juce::var&v,double&d){const auto s=v.toString().trim();if(s.isEmpty())return false;auto*start=s.toRawUTF8();char*end=nullptr;d=std::strtod(start,&end);return end!=start&&*end=='\0'&&std::isfinite(d);}
float safe(float v,float fallback=0){return std::isfinite(v)?v:fallback;}
const char* bandIds[4][7]={{"b1threshold","b1range","b1gain","b1attack","b1release","b1solo","b1bypass"},{"b2threshold","b2range","b2gain","b2attack","b2release","b2solo","b2bypass"},{"b3threshold","b3range","b3gain","b3attack","b3release","b3solo","b3bypass"},{"b4threshold","b4range","b4gain","b4attack","b4release","b4solo","b4bypass"}};
}
GillDynamicsProcessor::GillDynamicsProcessor(DynKind k):AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),kind(k),definitions(gilldyn::specs(k)),programs(gilldyn::presets(k)),apvts(*this,nullptr,juce::String(names[static_cast<int>(k)])+"_STATE",layout(k)){
 if(kind==DynKind::Buss){std::lock_guard<std::mutex>lock(groupLifetimeMutex);if(groupInstances++==0)clearGroups();}
 for(size_t i=0;i<definitions.size();++i)values[i]=apvts.getRawParameterValue(definitions[i].id);selectPreset(0,false);
 if(kind==DynKind::Buss){syncGroups();for(const auto&d:definitions)if(d.id.size()>2&&d.id[0]=='g'&&d.id[1]>='1'&&d.id[1]<='8')apvts.addParameterListener(d.id,this);}
}
GillDynamicsProcessor::~GillDynamicsProcessor(){if(kind==DynKind::Buss){for(const auto&d:definitions)apvts.removeParameterListener(d.id,this);std::lock_guard<std::mutex>lock(groupLifetimeMutex);if(--groupInstances==0)clearGroups();}}
juce::AudioProcessorValueTreeState::ParameterLayout GillDynamicsProcessor::layout(DynKind k){juce::AudioProcessorValueTreeState::ParameterLayout l;for(const auto&d:gilldyn::specs(k)){if(d.choices.empty())l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{d.id,1},d.name,juce::NormalisableRange<float>(d.lo,d.hi,d.step,d.skew),d.def));else if(d.choices.size()==2&&juce::String(d.choices[0])=="OFF")l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{d.id,1},d.name,d.def>.5f));else{juce::StringArray a;for(auto c:d.choices)a.add(c);l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{d.id,1},d.name,a,juce::roundToInt(d.def)));}}return l;}
const juce::String GillDynamicsProcessor::getName()const{return names[static_cast<int>(kind)];}
float GillDynamicsProcessor::value(const juce::String&id)const{auto*p=apvts.getRawParameterValue(id);return p?safe(p->load()):0;}
float GillDynamicsProcessor::at(const char*id)const{for(size_t i=0;i<definitions.size();++i)if(definitions[i].id==id)return safe(values[i]->load(std::memory_order_relaxed),definitions[i].def);return 0;}
void GillDynamicsProcessor::setValue(const juce::String&id,float v,bool gesture){if(auto*p=apvts.getParameter(id)){if(!std::isfinite(v))return;if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}}
void GillDynamicsProcessor::parameterChanged(const juce::String&id,float value){if(mirroringGroups)return;int group=0,field=0;if(groupId(id,group,field))groupField(group,field).store(safe(value),std::memory_order_relaxed);}
void GillDynamicsProcessor::syncGroups(){if(kind!=DynKind::Buss)return;const bool old=mirroringGroups;mirroringGroups=true;for(const auto&d:definitions){int group=0,field=0;if(groupId(d.id,group,field)){const auto v=groupField(group,field).load(std::memory_order_relaxed);if(std::abs(value(d.id)-v)>.001f)setValue(d.id,v,false);}}mirroringGroups=old;}
bool GillDynamicsProcessor::isBusesLayoutSupported(const BusesLayout&l)const{auto o=l.getMainOutputChannelSet();return(o==juce::AudioChannelSet::mono()||o==juce::AudioChannelSet::stereo())&&o==l.getMainInputChannelSet();}
void GillDynamicsProcessor::update(){
 if(kind==DynKind::Vox){gilldyn::VoxParameters p;p.gateDb=at("gate");p.comp=at("comp");p.outputDb=at("output");vox.setParameters(p);}
 else if(kind==DynKind::Opta){gilldyn::OptaParameters p;p.gainDb=at("gain");p.peakReduction=at("reduction");p.limit=at("limit")>.5f;p.hf=at("hf");p.noise=at("noise")>.5f;opta.setParameters(p);}
 else if(kind==DynKind::Buss){gilldyn::BussParameters p;p.driveDb=at("drive");p.trimDb=at("trim");p.style=juce::roundToInt(at("style"));p.noise=at("noise")>.5f;const int group=juce::roundToInt(at("group"))-1;if(group>=0&&group<8){p.driveDb=std::clamp(p.driveDb+groups[group].drive.load(),0.f,24.f);p.trimDb+=groups[group].trim.load();p.noise=p.noise||groups[group].noise.load()>.5f;p.bypass=groups[group].bypass.load()>.5f;}buss.setParameters(p);}
 else if(kind==DynKind::Quad){gilldyn::QuadParameters p;p.crossoversHz={at("cross1"),at("cross2"),at("cross3")};p.outputDb=at("output");for(int i=0;i<4;++i){auto&b=p.bands[i];b.thresholdDb=at(bandIds[i][0]);b.rangeDb=at(bandIds[i][1]);b.gainDb=at(bandIds[i][2]);b.attackMs=at(bandIds[i][3]);b.releaseMs=at(bandIds[i][4]);b.solo=at(bandIds[i][5])>.5f;b.bypass=at(bandIds[i][6])>.5f;}quad.setParameters(p);}
 else stage.setParameters(at("x"),at("distance"),at("spread"),at("doubler"),at("mix"),at("output"),at("mono")>.5f,at("direct")>.5f);
}
void GillDynamicsProcessor::updateGraph(){if(kind!=DynKind::Quad)return;auto cross=quad.crossoverHz();auto gr=quad.bandReductionDb();for(int i=0;i<3;++i)crossoversDisplay[i]=cross[i];for(int i=0;i<4;++i)bandReduction[i]=gr[i];const juce::SpinLock::ScopedTryLockType lock(graphLock);if(lock.isLocked())for(size_t i=0;i<display.size();++i)display[i]=quad.responseDb(20*std::pow(std::min(20000.,uiRate.load()*.45)/20.,i/127.));}
std::array<float,128> GillDynamicsProcessor::graph()const{const juce::SpinLock::ScopedLockType lock(graphLock);return display;}
void GillDynamicsProcessor::prepareToPlay(double rate,int block){const bool good=std::isfinite(rate)&&rate>=8000&&rate<=192000;rateSupported=good;const auto fs=good?rate:48000.;uiRate=fs;const int ch=juce::jlimit(1,2,getTotalNumOutputChannels());update();int latency=0;
 if(kind==DynKind::Vox){vox.prepare(fs,block,ch);latency=vox.latencySamples();}else if(kind==DynKind::Opta){opta.prepare(fs,block,ch);latency=opta.latencySamples();}else if(kind==DynKind::Buss){buss.prepare(fs,block,ch);latency=buss.latencySamples();}else if(kind==DynKind::Quad){quad.prepare(fs,block,ch);latency=quad.latencySamples();}else{stage.prepare(fs,block,ch);latency=stage.latencySamples();}
 update();for(auto&r:dry)r.assign(static_cast<size_t>(latency+1),0);dryPosition=0;setLatencySamples(latency);bypassFade.reset(fs,.005);bypassFade.setCurrentAndTargetValue(!good||at("bypass")>.5f?1.f:0.f);inputPeak=outputPeak=inputRms=outputRms=reduction=0;inPower=outPower=0;channelPower.fill(0);for(auto&r:channelOutputRms)r=0;for(auto&r:channelOutputPeak)r=0;meterAlpha=1-std::exp(-1/(fs*.3));graphCounter=0;updateGraph();}
void GillDynamicsProcessor::releaseResources(){vox.reset();opta.reset();buss.reset();quad.reset();stage.reset();inputPeak=outputPeak=inputRms=outputRms=reduction=0;}
void GillDynamicsProcessor::process(juce::AudioBuffer<float>&b,bool bypass){juce::ScopedNoDenormals nd;const int ch=std::min(2,b.getNumChannels()),n=b.getNumSamples();if(ch==0||n==0)return;for(int c=getTotalNumInputChannels();c<b.getNumChannels();++c)b.clear(c,0,n);if(dry[0].empty())return;update();bypassFade.setTargetValue(bypass||!rateSupported.load()||at("bypass")>.5f?1.f:0.f);float inPeak=0,outPeak=0;std::array<float,2> outPeaks{};
 for(int start=0;start<n;start+=128){const int count=std::min(128,n-start);std::array<float*,2>ptr{};std::array<std::array<float,128>,2>original{};std::array<double,128>inputPowers{};for(int c=0;c<ch;++c)ptr[c]=b.getWritePointer(c,start);
  for(int i=0;i<count;++i){for(int c=0;c<ch;++c){const float x=std::clamp(safe(ptr[c][i]),-32.f,32.f);ptr[c][i]=x;inputPowers[i]+=double(x)*x/ch;inPeak=std::max(inPeak,std::abs(x));dry[c][dryPosition]=x;original[c][i]=dry[c][(dryPosition+1)%dry[c].size()];}dryPosition=(dryPosition+1)%dry[0].size();}
  if(kind==DynKind::Vox)vox.process(ptr.data(),count,ch);else if(kind==DynKind::Opta)opta.process(ptr.data(),count,ch);else if(kind==DynKind::Buss)buss.process(ptr.data(),count,ch);else if(kind==DynKind::Quad)quad.process(ptr.data(),count,ch);else stage.process(ptr.data(),ch,count);
  for(int i=0;i<count;++i){const float bp=bypassFade.getNextValue();double power=0;for(int c=0;c<ch;++c){const float wet=safe(ptr[c][i]);ptr[c][i]=bp>=1?original[c][i]:wet+bp*(original[c][i]-wet);outPeak=std::max(outPeak,std::abs(ptr[c][i]));outPeaks[c]=std::max(outPeaks[c],std::abs(ptr[c][i]));const double v=double(ptr[c][i])*ptr[c][i];power+=v/ch;channelPower[c]+=meterAlpha*(v-channelPower[c]);}inPower+=meterAlpha*(inputPowers[i]-inPower);outPower+=meterAlpha*(power-outPower);}
 }
 const float decay=static_cast<float>(std::exp(-n/(uiRate.load()*.12)));inputPeak=std::max(inPeak,inputPeak.load()*decay);outputPeak=std::max(outPeak,outputPeak.load()*decay);inputRms=static_cast<float>(std::sqrt(std::max(0.,inPower)));outputRms=static_cast<float>(std::sqrt(std::max(0.,outPower)));for(int c=0;c<2;++c)channelOutputRms[c]=static_cast<float>(std::sqrt(std::max(0.,channelPower[std::min(c,ch-1)])));
 for(int c=0;c<2;++c)channelOutputPeak[c]=std::max(outPeaks[std::min(c,ch-1)],channelOutputPeak[c].load()*decay);reduction=kind==DynKind::Vox?vox.reductionDb():kind==DynKind::Opta?opta.reductionDb():0;graphCounter+=n;if(graphCounter>=uiRate.load()*.025){updateGraph();graphCounter=0;}
}
void GillDynamicsProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,false);}void GillDynamicsProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
void GillDynamicsProcessor::selectPreset(int i,bool gesture){i=juce::jlimit(0,getNumPrograms()-1,i);currentProgram=i;for(const auto&d:definitions){const juce::String id(d.id);if(id=="bypass"||id=="meter"||id=="group"||(kind==DynKind::Buss&&id.length()>2&&id[0]=='g'&&id[1]>='1'&&id[1]<='8'))continue;setValue(id,d.def,gesture);}for(const auto&v:programs[static_cast<size_t>(i)].values)setValue(v.first,v.second,gesture);}
bool GillDynamicsProcessor::presetMatches()const{const auto&p=programs[static_cast<size_t>(juce::jlimit(0,static_cast<int>(programs.size())-1,currentProgram.load()))];for(const auto&d:definitions){const juce::String id(d.id);if(id=="bypass"||id=="meter"||id=="group"||(kind==DynKind::Buss&&id.length()>2&&id[0]=='g'&&id[1]>='1'&&id[1]<='8'))continue;float v=d.def;for(const auto&x:p.values)if(d.id==x.first)v=x.second;if(std::abs(value(id)-v)>std::max(.0001f,d.step*.51f))return false;}return true;}
void GillDynamicsProcessor::getStateInformation(juce::MemoryBlock&out){auto s=apvts.copyState();if(kind==DynKind::Buss)for(auto child:s){int group=0,field=0;if(groupId(child.getProperty("id").toString(),group,field))child.setProperty("value",groupField(group,field).load(std::memory_order_relaxed),nullptr);}s.setProperty("version",2,nullptr);s.setProperty("program",currentProgram.load(),nullptr);if(auto x=s.createXml())copyXmlToBinary(*x,out);}
void GillDynamicsProcessor::setStateInformation(const void*data,int bytes){
 if(!data||bytes<=0||bytes>1024*1024)return;
 if(auto x=getXmlFromBinary(data,bytes))if(x->hasTagName(apvts.state.getType())){
  auto incoming=juce::ValueTree::fromXml(*x),clean=apvts.copyState();bool changed=false;std::array<std::array<float,4>,8>restored{};std::array<std::array<bool,4>,8>restoreGroup{};
  std::vector<std::pair<juce::RangedAudioParameter*,float>> acceptedParameters;
  acceptedParameters.reserve(definitions.size());
  for(auto child:incoming){const auto id=child.getProperty("id").toString();auto*p=apvts.getParameter(id);double v=0;if(!p||!numeric(child.getProperty("value"),v))continue;auto target=clean.getChildWithProperty("id",id);if(target.isValid()){const auto&r=p->getNormalisableRange();const float accepted=r.snapToLegalValue(static_cast<float>(std::clamp(v,static_cast<double>(r.start),static_cast<double>(r.end))));target.setProperty("value",accepted,nullptr);changed=true;const float normalised=p->convertTo0to1(accepted);const auto existing=std::find_if(acceptedParameters.begin(),acceptedParameters.end(),[p](const auto&item){return item.first==p;});if(existing==acceptedParameters.end())acceptedParameters.emplace_back(p,normalised);else existing->second=normalised;int group=0,field=0;if(kind==DynKind::Buss&&groupId(id,group,field)){restored[group][field]=accepted;restoreGroup[group][field]=true;}}}
  // Version-1 STAGE sessions predate DIRECT and must retain their original
  // sound even when recalled into an instance whose direct path is muted.
  if(kind==DynKind::Stage && static_cast<int>(incoming.getProperty("version",1))<2 &&
     !incoming.getChildWithProperty("id","direct").isValid() && changed){
   auto*p=apvts.getParameter("direct");auto target=clean.getChildWithProperty("id","direct");
   if(p&&target.isValid()){target.setProperty("value",1.f,nullptr);acceptedParameters.emplace_back(p,1.f);}
  }
  if(changed){
   apvts.replaceState(clean);
   // JUCE's boolean parameter can retain an off-grid host value (e.g. 0.39),
   // while APVTS already holds its snapped value (0). replaceState then skips
   // assigning that apparently unchanged value. Reassert each valid saved
   // parameter and notify the host so its VST3 controller cache is restored too.
   // Missing/invalid fields remain untouched; duplicate IDs use the last valid value.
   for(const auto&item:acceptedParameters)item.first->setValueNotifyingHost(item.second);
  }
  // Reassert saved global groups even when this instance's local APVTS value
  // is unchanged and JUCE therefore intentionally emits no parameter listener.
  for(int group=0;group<8;++group)for(int field=0;field<4;++field)if(restoreGroup[group][field])groupField(group,field).store(restored[group][field],std::memory_order_relaxed);
  double v=0;if(numeric(incoming.getProperty("program"),v))currentProgram=juce::jlimit(0,getNumPrograms()-1,static_cast<int>(std::clamp(v,0.,static_cast<double>(getNumPrograms()-1))));
 }
}
juce::AudioProcessorEditor*GillDynamicsProcessor::createEditor(){return new GillDynamicsEditor(*this);}

