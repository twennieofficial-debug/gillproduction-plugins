#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

namespace {const char*names[]{"SOFT ENTRANCE","HOOK LIFT","SHORT PICKUP","DARK SWELL","TREMOLO RISE","SLOW CINEMATIC"};}
GillRiseProcessor::GillRiseProcessor():AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),Thread("GILLRISE renderer"),apvts(*this,nullptr,"GILLRISE_STATE",layout()){}
GillRiseProcessor::~GillRiseProcessor(){signalThreadShouldExit();generation++;stopThread(-1);}
juce::AudioProcessorValueTreeState::ParameterLayout GillRiseProcessor::layout(){
 juce::AudioProcessorValueTreeState::ParameterLayout p;auto add=[&](const char*id,const char*label,float lo,float hi,float step,float v){p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},label,juce::NormalisableRange<float>(lo,hi,step),v));};
 add("length","LENGTH",.25f,8,.01f,2);add("decay","REVERB",.25f,8,.01f,2.5f);add("tone","TONE",800,18000,1,7500);add("level","LEVEL",-30,0,.1f,-6);add("rate","PULSE RATE",.5f,20,.1f,6);add("depth","PULSE DEPTH",0,100,.1f,70);add("start","SYLLABLE START",0,95,.1f,0);add("end","SYLLABLE END",5,100,.1f,100);
 p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"style",1},"STYLE",juce::StringArray{"NORMAL","TREMOLO"},0));p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));p.add(gill::qualityParameter(1));return p;
}
float GillRiseProcessor::value(const char*id)const{auto*p=apvts.getRawParameterValue(id);return p&&std::isfinite(p->load())?p->load():0;}
void GillRiseProcessor::setValue(const char*id,float v){if(!std::isfinite(v))return;if(auto*p=apvts.getParameter(id)){p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));p->endChangeGesture();}}
bool GillRiseProcessor::isBusesLayoutSupported(const BusesLayout&b)const{auto c=b.getMainInputChannelSet();return (c==juce::AudioChannelSet::mono()||c==juce::AudioChannelSet::stereo())&&c==b.getMainOutputChannelSet();}
void GillRiseProcessor::prepareToPlay(double rate,int){signalThreadShouldExit();generation++;stopThread(-1);supported=std::isfinite(rate)&&rate>=8000&&rate<=192000;sampleRateView=supported?rate:48000;capture.prepare(sampleRateView);if(captured())capture.stop();previewState=0;previewing=false;previewCommand=0;for(auto&v:preview)v.assign(std::size_t(std::ceil(sampleRateView*8)),0);setLatencySamples(0);startThread();}
void GillRiseProcessor::releaseResources(){previewCommand=2;}
void GillRiseProcessor::arm(){{std::lock_guard<std::mutex>l(resultMutex);source.reset();rendered.reset();pendingRestore.reset();renderFailed=false;generation++;capture.arm();}previewCommand=2;}
void GillRiseProcessor::finishCapture(){capture.stop();}
void GillRiseProcessor::audition(bool play){previewCommand=play?1:2;}
gill::rise::Settings GillRiseProcessor::settings()const{gill::rise::Settings s;s.length=value("length");s.decay=value("decay");s.tone=value("tone");s.level=value("level");s.rate=value("rate");s.depth=value("depth")*.01;s.start=value("start")*.01;s.end=value("end")*.01;s.tremolo=value("style")>.5f;return s;}
void GillRiseProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,false);}
void GillRiseProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,true);}
void GillRiseProcessor::process(juce::AudioBuffer<float>&b,bool hostBypass){
 juce::ScopedNoDenormals denormal;const int n=b.getNumSamples(),channels=std::min(2,b.getNumChannels());if(!supported||channels==0||n==0)return;
 const bool bypass=hostBypass||value("bypass")>.5f;bool hasTransport=false,playing=true,positionKnown=false;std::int64_t position=0;
 if(auto*h=getPlayHead())if(auto info=h->getPosition()){hasTransport=true;playing=info->getIsPlaying();if(auto samples=info->getTimeInSamples()){position=*samples;positionKnown=true;}}
 if(!isNonRealtime())capture.process(b.getReadPointer(0),channels>1?b.getReadPointer(1):nullptr,n,playing,hasTransport,position,positionKnown,!bypass);
 const int command=previewCommand.exchange(0);if(bypass||isNonRealtime()){int playingState=3;previewState.compare_exchange_strong(playingState,2);previewing=false;}
 if(command==2&&previewState.load()==3)previewStop=std::max(1,int(sampleRateView*.01));
 if(command==1&&!bypass&&!isNonRealtime()){int ready=2;if(previewState.compare_exchange_strong(ready,3)){previewPosition=0;previewStop=0;previewing=true;}}
 if(previewState.load(std::memory_order_acquire)==3){
   const int fade=std::max(1,int(sampleRateView*.01));
   for(int i=0;i<n;++i){if(previewPosition>=previewCount){previewState.store(2,std::memory_order_release);previewing=false;break;}float wet=std::min({1.f,float(previewPosition)/fade,float(previewCount-1-previewPosition)/fade});if(previewStop>0)wet*=float(previewStop)/fade;for(int c=0;c<channels;++c){float dry=b.getSample(c,i);b.setSample(c,i,dry+wet*(preview[c][previewPosition]-dry));}++previewPosition;if(previewStop>0&&--previewStop==0){previewState.store(2,std::memory_order_release);previewing=false;break;}}
 }
}
std::shared_ptr<const gill::rise::Render>GillRiseProcessor::result()const{std::lock_guard<std::mutex>l(resultMutex);return rendered;}
std::shared_ptr<const gill::rise::Capture>GillRiseProcessor::captured()const{std::lock_guard<std::mutex>l(resultMutex);return source;}
bool GillRiseProcessor::publishPreview(const gill::rise::Render&r){int wanted=previewState.load();if(wanted==3||wanted==1)return false;if(!previewState.compare_exchange_strong(wanted,1))return false;
 const double ratio=r.sampleRate/sampleRateView.load();previewCount=std::min(int(preview[0].size()),int(std::round(r.left.size()/ratio)));
 for(int i=0;i<previewCount;++i){double at=i*ratio;int first=std::min(int(at),int(r.left.size())-1),last=std::min(first+1,int(r.left.size())-1);float t=float(at-first);preview[0][i]=r.left[first]+t*(r.left[last]-r.left[first]);preview[1][i]=r.right[first]+t*(r.right[last]-r.right[first]);}
 previewState.store(2,std::memory_order_release);return true;
}
void GillRiseProcessor::run(){
 std::shared_ptr<const gill::rise::Capture>current=captured();gill::rise::Settings last;bool dirty=bool(current),previewPending=false;unsigned seenGeneration=generation.load();
 while(!threadShouldExit()){
   if(generation.load()!=seenGeneration){seenGeneration=generation.load();current=captured();dirty=bool(current);previewPending=false;}
   {std::lock_guard<std::mutex>l(resultMutex);if(pendingRestore){current=std::move(pendingRestore);source=current;dirty=true;}}
   const auto captureToken=generation.load();gill::rise::Capture next;if(capture.take(next)){auto taken=std::make_shared<gill::rise::Capture>(std::move(next));std::lock_guard<std::mutex>l(resultMutex);if(captureToken==generation.load()){current=taken;source=current;seenGeneration=captureToken;dirty=true;}}
   auto now=settings();if(current&&(!(now==last)||dirty)){
     const auto snapshot=now;const auto token=generation.load();rendering=true;renderFailed=false;auto made=std::make_shared<gill::rise::Render>();
     bool good=gill::rise::render(*current,snapshot,*made,[&]{return threadShouldExit()||generation.load()!=token||!(settings()==snapshot);});rendering=false;
     if(good){std::lock_guard<std::mutex>l(resultMutex);if(token==generation.load()){rendered=made;revision++;previewPending=true;last=snapshot;dirty=false;}}else if(generation.load()!=token){last=snapshot;dirty=false;}else if(settings()==snapshot){std::lock_guard<std::mutex>l(resultMutex);rendered.reset();renderFailed=true;last=snapshot;dirty=false;previewCommand=2;revision++;}
   }
   if(previewPending){auto r=result();if(r&&publishPreview(*r))previewPending=false;}
   wait(35);
 }
}
juce::String GillRiseProcessor::statusText()const{
 switch(capture.status()){case gill::rise::CaptureEngine::Armed:return "ARMED  /  PLAY THE FIRST SYLLABLE";case gill::rise::CaptureEngine::Recording:return "CAPTURING FIRST SYLLABLE";case gill::rise::CaptureEngine::Ready:case gill::rise::CaptureEngine::Reading:return "PREPARING SYLLABLE";default:break;}
 if(rendering)return "RENDERING REVERSE REVERB";if(renderFailed)return "ADJUST START / END OR RECAPTURE";if(result())return previewing?"AUDITION  /  ORIGINAL RETURNS AFTER PREVIEW":"READY  /  DRAG INTO YOUR PLAYLIST";return "ARM & PLAY  /  ORIGINAL AUDIO PASSES THROUGH";
}
juce::File GillRiseProcessor::exportWav(juce::String&error,const juce::File&folderOverride){
 auto r=result();if(!r){error="Capture a syllable first.";return {};}
 if(rendering.load()||!(r->settings==settings())){error="Rendering changed settings. Please try again when READY.";return {};}
 if(exported.existsAsFile()&&exportedRender.lock()==r)return exported;
 auto folder=folderOverride==juce::File{}?juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Jill Plugins/Renders/GILLRISE"):folderOverride;if(!folder.createDirectory()){error="The render folder could not be created.";return {};}
 auto file=folder.getChildFile("GILLRISE-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+"-"+juce::Uuid().toString().substring(0,8)+".wav");
 auto stream=file.createOutputStream();if(!stream){error="The WAV file could not be opened.";return {};}
 juce::StringPairArray metadata;auto start=r->endSample-std::int64_t(r->left.size());
 if(r->hostPositionKnown&&start>=0)metadata=juce::WavAudioFormat::createBWAVMetadata("GILLRISE reverse reverb; align its END to the selected syllable", "GILLPRODUCTION",juce::Uuid().toString(),juce::Time::getCurrentTime(),start,"A=PCM,F="+juce::String(int(r->sampleRate))+",W=24,M=stereo");
 juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatWriter>writer(wav.createWriterFor(stream.get(),r->sampleRate,2,24,metadata,0));if(!writer){error="The WAV writer could not be created.";return {};}stream.release();
 const float*channels[]{r->left.data(),r->right.data()};const bool ok=writer->writeFromFloatArrays(channels,2,int(r->left.size()));writer.reset();if(!ok||file.getSize()<44){error="The WAV could not be completely written.";return {};}
 exported=file;exportedRender=r;return file;
}
const juce::String GillRiseProcessor::getProgramName(int i){return names[std::clamp(i,0,5)];}
void GillRiseProcessor::setCurrentProgram(int i){i=std::clamp(i,0,5);setValue("length",i==1?3:i==2?.65f:i==3?2.5f:i==5?6:2);setValue("decay",i==1?4:i==2?1.1f:i==3?4.5f:i==5?7:2.5f);setValue("tone",i==1?12000:i==3?2200:i==5?5500:7500);setValue("level",i==1?-4:i==3?-8:-6);setValue("style",i==4?1:0);setValue("rate",i==4?8:6);setValue("depth",i==4?85:70);program=i;}
void GillRiseProcessor::getStateInformation(juce::MemoryBlock&out){
 auto tree=apvts.copyState();tree.setProperty("schema",1,nullptr);tree.setProperty("program",program.load(),nullptr);
 if(auto c=captured()){
   juce::MemoryOutputStream audio;audio.writeDouble(c->sampleRate);audio.writeInt(int(c->left.size()));audio.writeInt(c->defaultStart);audio.writeInt(c->defaultEnd);audio.writeInt64(c->onsetSample);audio.writeBool(c->hostPositionKnown);
   for(std::size_t i=0;i<c->left.size();++i){audio.writeFloat(c->left[i]);audio.writeFloat(c->right[i]);}
   tree.setProperty("capture",audio.getMemoryBlock().toBase64Encoding(),nullptr);
 }
 if(auto xml=tree.createXml())copyXmlToBinary(*xml,out);
}
void GillRiseProcessor::setStateInformation(const void*data,int size){
 if(!data||size<=0||size>8*1024*1024)return;auto xml=getXmlFromBinary(data,size);if(!xml||!xml->hasTagName("GILLRISE_STATE"))return;auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||int(tree.getProperty("schema",0))!=1)return;
 juce::StringArray seen;for(auto child:tree){if(!child.hasType("PARAM"))return;auto id=child["id"].toString();auto*p=apvts.getParameter(id);if(!p||seen.contains(id))return;seen.add(id);auto t=child["value"].toString();char*end=nullptr;double v=std::strtod(t.toRawUTF8(),&end);auto range=p->getNormalisableRange();if(t.isEmpty()||!end||*end||!std::isfinite(v)||v<range.start-.001||v>range.end+.001||(p->isDiscrete()&&v!=std::floor(v)))return;}
 if(seen.size()!=getParameters().size())return;
 std::shared_ptr<gill::rise::Capture>restored;const auto encoded=tree.getProperty("capture").toString();if(encoded.isNotEmpty()){
   juce::MemoryBlock bytes;if(!bytes.fromBase64Encoding(encoded)||bytes.getSize()<33||bytes.getSize()>4*1024*1024)return;juce::MemoryInputStream audio(bytes,false);restored=std::make_shared<gill::rise::Capture>();restored->sampleRate=audio.readDouble();int count=audio.readInt();restored->defaultStart=audio.readInt();restored->defaultEnd=audio.readInt();restored->onsetSample=audio.readInt64();restored->hostPositionKnown=audio.readBool();
   if(!std::isfinite(restored->sampleRate)||restored->sampleRate<8000||restored->sampleRate>192000||count<=0||count>int(restored->sampleRate*2.5)||audio.getNumBytesRemaining()!=std::int64_t(count)*8||restored->defaultStart<0||restored->defaultEnd<=restored->defaultStart||restored->defaultEnd>count)return;
   restored->left.resize(count);restored->right.resize(count);for(int i=0;i<count;++i){restored->left[i]=audio.readFloat();restored->right[i]=audio.readFloat();if(!std::isfinite(restored->left[i])||!std::isfinite(restored->right[i])||std::abs(restored->left[i])>16||std::abs(restored->right[i])>16)return;}
 }
 tree.removeProperty("capture",nullptr);apvts.replaceState(tree);for(auto child:tree)if(auto*p=apvts.getParameter(child["id"].toString()))p->setValueNotifyingHost(p->convertTo0to1(float(child["value"])));program=std::clamp(int(tree.getProperty("program",0)),0,5);
 previewCommand=2;capture.stop();{std::lock_guard<std::mutex>l(resultMutex);pendingRestore=restored;source=restored;rendered.reset();generation++;}revision++;
}
juce::AudioProcessorEditor*GillRiseProcessor::createEditor(){return new GillRiseEditor(*this);}
