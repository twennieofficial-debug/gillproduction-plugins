#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RideDSP.h"
#include "PocketDSP.h"
#include "FinishDSP.h"
#include "CleanDSP.h"
#include "FormDSP.h"
#include "AlignDSP.h"
#include "Loudness.h"
#include "OutputPeakMeter.h"
#include <cstdlib>
#include <mutex>
namespace {
const char* names[]{"GILLRIDE","GILLCLEAN","GILLPOCKET","GILLALIGN","GILLFORM","GILLFINISH"};
bool numeric(const juce::var&v,double&x){auto s=v.toString().trim();char*end=nullptr;const auto*start=s.toRawUTF8();x=std::strtod(start,&end);return end!=start&&*end=='\0'&&std::isfinite(x);}
}
GillNextProcessor::BusesProperties GillNextProcessor::buses(NextKind k){auto b=juce::AudioProcessor::BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true);if(k==NextKind::Pocket||k==NextKind::Align)b=b.withInput("VOCAL GUIDE",juce::AudioChannelSet::stereo(),false);return b;}
struct GillNextProcessor::Impl:juce::Thread {
 GillNextProcessor&p;gillnext::RideDSP ride;gillnext::PocketDSP pocket;gillnext::FinishDSP finish;gillnext::CleanDSP clean;gillnext::FormDSP form;
 std::array<std::atomic<float>*,64>values{};std::array<juce::RangedAudioParameter*,64>parameters{};std::array<std::atomic<float>,128>display{};std::array<std::array<std::atomic<float>,256>,2>waves{};std::array<std::array<std::atomic<float>,8192>,2>capturePeaks{};std::array<std::atomic<bool>,2>waveIsResult{};
 std::array<std::vector<float>,2>dry;size_t dryPos=0;juce::SmoothedValue<float>bypass,match,mono,previewFade;gillnext::detail::Meter meter;gillnext::Loudness loudness,learnLoudness;gillnext::OutputPeakMeter outputTruePeak;
 std::atomic<int>captureCommand{0};std::atomic<std::uint64_t>workerCommand{0};std::atomic<unsigned>generation{0};std::atomic<bool>captureBuffersRead{false};std::array<gillnext::AlignAudio,2>takes;std::array<std::atomic<int>,2>takeSize{};int capacity=0,captureLane=-1;std::atomic<double>dubAnchorSeconds{0};std::atomic<bool>dubHasHost{false};bool wasPreview=false;std::int64_t previewClock=0;
 std::vector<std::unique_ptr<gillnext::AlignResult>>results;std::atomic<const gillnext::AlignResult*>result{nullptr},reading{nullptr};std::atomic<const gillnext::AlignResult*>previous{nullptr};
 mutable std::mutex statusMutex;juce::String status,audioPath;std::atomic<bool>learnRequest{false},meterReset{false};double learnFrames=0;std::array<double,3>learnPower{};std::array<double,2>learnLowState{},learnHighState{};std::vector<std::pair<juce::String,float>>beforeLearn;
 int displayClock=0;double fs=48000;int channelCount=2;bool prepared=false;
 explicit Impl(GillNextProcessor&v):Thread("GILLALIGN ANALYSIS"),p(v){for(size_t i=0;i<p.definitions.size();++i){values[i]=p.apvts.getRawParameterValue(p.definitions[i].id);parameters[i]=p.apvts.getParameter(p.definitions[i].id);}if(p.kind==NextKind::Align)startThread();}
 ~Impl()override{signalThreadShouldExit();notify();stopThread(-1);}
 float at(const char*id)const noexcept{for(size_t i=0;i<p.definitions.size();++i)if(p.definitions[i].id==id)return float(gillnext::detail::finite(values[i]->load(std::memory_order_relaxed),p.definitions[i].def));return 0;}
 void queue(int command,bool advance=false){std::lock_guard<std::mutex>l(statusMutex);if(advance)++generation;workerCommand=(std::uint64_t(generation.load())<<8)|command;notify();}
 void invalidate(){std::lock_guard<std::mutex>l(statusMutex);++generation;workerCommand=0;result=nullptr;previous=nullptr;audioPath.clear();p.alignState=0;}
 void message(const juce::String&s){std::lock_guard<std::mutex>l(statusMutex);status=s;}
 void jobMessage(unsigned epoch,int state,const juce::String&text){std::lock_guard<std::mutex>l(statusMutex);if(epoch==generation.load()){p.alignState=state;status=text;}}
 bool saveResult(const gillnext::AlignAudio&a,unsigned epoch){
  const auto dir=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Jill Plugins/Align Takes");
  if(!dir.createDirectory())return false;const auto file=dir.getChildFile("GILLALIGN-"+juce::Uuid().toString()+".wav");
  auto stream=file.createOutputStream();if(!stream)return false;
  const int channels=int(a.size()),frames=int(a[0].size()),bytes=frames*channels*4;
  stream->write("RIFF",4);stream->writeInt(36+bytes);stream->write("WAVEfmt ",8);stream->writeInt(16);stream->writeShort(3);stream->writeShort(short(channels));stream->writeInt(int(fs));stream->writeInt(int(fs)*channels*4);stream->writeShort(short(channels*4));stream->writeShort(32);stream->write("data",4);stream->writeInt(bytes);
  for(int i=0;i<frames;++i)for(int c=0;c<channels;++c)stream->writeFloat(a[c][i]);stream->flush();const bool ok=stream->getStatus().wasOk();stream.reset();
  if(ok){std::lock_guard<std::mutex>l(statusMutex);if(epoch==generation.load())audioPath=file.getFullPathName();}return ok;
 }
 void loadResult(unsigned epoch){
  juce::String path;{std::lock_guard<std::mutex>l(statusMutex);if(epoch!=generation.load())return;path=audioPath;}if(path.isEmpty())return;
  const juce::File file(path);juce::AudioFormatManager fm;fm.registerBasicFormats();
  if(!file.existsAsFile()||file.getSize()>64*1024*1024){jobMessage(epoch,4,"SAVED ALIGN AUDIO MISSING");return;}
  std::unique_ptr<juce::AudioFormatReader>reader(fm.createReaderFor(file));
  if(!reader||reader->numChannels<1||reader->numChannels>2||reader->sampleRate<8000||reader->sampleRate>192000||reader->lengthInSamples>reader->sampleRate*20.1){jobMessage(epoch,4,"SAVED ALIGN AUDIO INVALID");return;}
  juce::AudioBuffer<float>audio(int(reader->numChannels),int(reader->lengthInSamples));if(!reader->read(&audio,0,audio.getNumSamples(),0,true,true)){jobMessage(epoch,4,"SAVED ALIGN AUDIO COULD NOT BE READ");return;}
  auto r=std::make_unique<gillnext::AlignResult>();const int frames=int(std::round(audio.getNumSamples()*fs/reader->sampleRate));r->audio.resize(channelCount);const double ratio=reader->sampleRate/fs;
  for(int c=0;c<channelCount;++c){auto&out=r->audio[c];out.resize(frames);const auto*src=audio.getReadPointer(std::min(c,audio.getNumChannels()-1));for(int i=0;i<frames;++i){const double pos=i*ratio;const int center=int(pos);double sum=0,weight=0;const double cutoff=std::min(1.,1/ratio);for(int k=-16;k<=16;++k){const int j=center+k;if(j<0||j>=audio.getNumSamples())continue;const double x=pos-j,phase=gillnext::detail::pi*x*cutoff;const double w=(std::abs(phase)<1e-12?1:std::sin(phase)/phase)*(.5+.5*std::cos(gillnext::detail::pi*x/17));sum+=src[j]*w;weight+=w;}out[i]=float(weight!=0?sum/weight:0);}}
  if(threadShouldExit())return;r->success=true;const auto*next=r.get();{std::lock_guard<std::mutex>l(statusMutex);if(epoch!=generation.load())return;previous=result.load();results.push_back(std::move(r));result=next;p.alignState=3;p.doubleSeconds=float(frames/fs);status="SAVED ALIGNMENT READY";}publishWave(1,next->audio,frames);
 }
 float snap(const char*id,float v)const noexcept{for(size_t i=0;i<p.definitions.size();++i)if(p.definitions[i].id==id)return parameters[i]->getNormalisableRange().snapToLegalValue(v);return v;}
 void update()noexcept{
  using namespace gillnext;
  if(p.kind==NextKind::Ride)ride.setParameters({at("target"),at("down"),at("up"),at("speed"),at("hold")>.5f});
  else if(p.kind==NextKind::Clean)clean.setParameters({at("noise"),at("plosives"),at("breaths"),juce::roundToInt(at("listen"))});
  else if(p.kind==NextKind::Pocket)pocket.setParameters({at("amount"),at("maxcut"),at("low"),at("high"),at("speed")});
  else if(p.kind==NextKind::Form)form.setParameters({at("pitch"),at("formant"),at("mix"),at("link")>.5f,at("transients")>.5f});
  else if(p.kind==NextKind::Finish){FinishParameters v;v.ceilingDb=at("ceiling");v.driveDb=at("drive");v.comp=at("comp");v.clip=at("clip");v.width=at("width");v.bassMonoHz=at("bassmono");v.lowDb=at("low");v.midDb=at("mid");v.highDb=at("high");v.toneEnabled=at("toneon")>.5f;v.compEnabled=at("compon")>.5f;v.clipEnabled=at("clipon")>.5f;v.stereoEnabled=at("stereoon")>.5f;v.limiterEnabled=at("limiteron")>.5f;finish.setParameters(v);}
 }
 void run()override{
  while(!threadShouldExit()){
   const auto job=workerCommand.exchange(0);const int command=int(job&255);const unsigned epoch=unsigned(job>>8);if(command&&epoch!=generation.load())continue;
   if(command==1){
    for(int tries=0;tries<200&&p.captureState.load()!=0&&!threadShouldExit();++tries)wait(10);
    if(p.captureState!=0){jobMessage(epoch,4,"STOP CAPTURE / PLAY BRIEFLY TO FINISH");continue;}
    const int ng=takeSize[0].load(),nd=takeSize[1].load();
    if(ng<fs*.2||nd<fs*.2){jobMessage(epoch,4,"CAPTURE GUIDE + DOUBLE FIRST");continue;}
    gillnext::AlignAudio guide(channelCount),dub(channelCount);
    {std::lock_guard<std::mutex>l(statusMutex);if(epoch!=generation.load())continue;captureBuffersRead=true;}
    for(int c=0;c<channelCount;++c){guide[c].assign(takes[0][c].begin(),takes[0][c].begin()+ng);dub[c].assign(takes[1][c].begin(),takes[1][c].begin()+nd);}
    captureBuffersRead=false;
    auto r=std::make_unique<gillnext::AlignResult>(gillnext::AlignDSP::align(guide,dub,fs,{at("tightness"),at("maxshift")}));
    if(threadShouldExit())return;if(epoch!=generation.load())continue;
    if(r->success){const bool saved=saveResult(r->audio,epoch);const auto*next=r.get();{std::lock_guard<std::mutex>l(statusMutex);if(epoch!=generation.load())continue;previous=result.load();results.push_back(std::move(r));result.store(next);p.alignConfidence=next->confidence;p.alignState=3;status=saved?"READY / PLAY FROM DOUBLE TAKE START":"READY / AUDIO COULD NOT BE SAVED";}publishWave(1,next->audio,nd);}else{jobMessage(epoch,4,juce::String(r->message));}
   }else if(command==2){const gillnext::AlignResult*r=nullptr;{std::lock_guard<std::mutex>l(statusMutex);if(epoch!=generation.load())continue;auto*current=result.exchange(previous.load());previous=current;r=result.load();p.alignState=r?3:1;if(!r)audioPath.clear();}if(r){saveResult(r->audio,epoch);if(epoch!=generation.load())continue;publishWave(1,r->audio,int(r->audio[0].size()));}else{const int n=takeSize[1].load();waveIsResult[1]=false;p.doubleSeconds=float(n/fs);}jobMessage(epoch,result.load()?3:1,"UNDO / REDO");}
   else if(command==3)loadResult(epoch);
   // The audio thread protects its currently read immutable result. Old results
   // are reclaimed here, never on the real-time thread.
   const auto*current=result.load();const auto*protectedResult=reading.load();
   results.erase(std::remove_if(results.begin(),results.end(),[&](const auto&r){return r.get()!=current&&r.get()!=previous.load()&&r.get()!=protectedResult;}),results.end());
   wait(100);
  }
 }
 void publishWave(int lane,const gillnext::AlignAudio&a,int n){waveIsResult[lane]=true;for(int b=0;b<256;++b){float peak=0;for(int i=b*n/256;i<(b+1)*n/256;++i)for(const auto&c:a)if(i<int(c.size()))peak=std::max(peak,std::abs(c[i]));waves[lane][b]=peak;}}
 void learnSample(const std::array<float,2>&x,int ch)noexcept{
  if(p.learnState!=1)return;learnLoudness.sample(x,ch);double e=0;for(int c=0;c<ch;++c)e+=double(x[c])*x[c];if(e<1e-8)return;
  const double al=1-std::exp(-2*gillnext::detail::pi*180/fs),ah=1-std::exp(-2*gillnext::detail::pi*4000/fs);
  for(int c=0;c<ch;++c){learnLowState[c]+=al*(x[c]-learnLowState[c]);learnHighState[c]+=ah*(x[c]-learnHighState[c]);const double lo=learnLowState[c],mid=learnHighState[c]-lo,hi=x[c]-learnHighState[c];learnPower[0]+=lo*lo;learnPower[1]+=mid*mid;learnPower[2]+=hi*hi;}
  learnFrames++;p.learnProgress=float(std::min(1.,learnFrames/(12*fs)));
  if(learnFrames>=12*fs){const int style=juce::roundToInt(at("style"));const double target=style==0?-14:style==1?-11:-9;const double measured=learnLoudness.integrated;
   p.learnDrive=snap("drive",float(std::clamp(target-measured,0.,9.)));const double ratioLow=10*std::log10((learnPower[0]+1e-12)/(learnPower[1]+1e-12)),ratioHigh=10*std::log10((learnPower[2]+1e-12)/(learnPower[1]+1e-12));
   p.learnLow=snap("low",float(std::clamp((-3-ratioLow)*.18,-1.5,1.5)));p.learnMid=0;p.learnHigh=snap("high",float(std::clamp((-9-ratioHigh)*.15,-1.5,1.5)));p.learnComp=style==0?12.f:style==1?25.f:38.f;p.learnState=2;
  }
 }
};
GillNextProcessor::GillNextProcessor(NextKind k):AudioProcessor(buses(k)),kind(k),definitions(gillnext::specs(k)),programs(gillnext::presets(k)),apvts(*this,nullptr,juce::String(names[int(k)])+"_STATE",layout(k)){impl=std::make_unique<Impl>(*this);selectPreset(0,false);}
GillNextProcessor::~GillNextProcessor()=default;
juce::AudioProcessorValueTreeState::ParameterLayout GillNextProcessor::layout(NextKind k){juce::AudioProcessorValueTreeState::ParameterLayout l;for(const auto&d:gillnext::specs(k)){if(d.choices.empty())l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{d.id,1},d.name,juce::NormalisableRange<float>(d.lo,d.hi,d.step,d.skew),d.def));else if(d.choices.size()==2&&juce::String(d.choices[0])=="OFF")l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{d.id,1},d.name,d.def>.5f));else{juce::StringArray a;for(auto*s:d.choices)a.add(s);l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{d.id,1},d.name,a,juce::roundToInt(d.def)));}}return l;}
const juce::String GillNextProcessor::getName()const{return names[int(kind)];}
float GillNextProcessor::value(const juce::String&id)const{auto*v=apvts.getRawParameterValue(id);return v?float(gillnext::detail::finite(v->load())):0;}
void GillNextProcessor::setValue(const juce::String&id,float v,bool gesture){if(auto*param=apvts.getParameter(id)){if(!std::isfinite(v))return;if(gesture)param->beginChangeGesture();param->setValueNotifyingHost(param->convertTo0to1(v));if(gesture)param->endChangeGesture();}}
bool GillNextProcessor::isBusesLayoutSupported(const BusesLayout&l)const{const auto main=l.getMainOutputChannelSet();if((main!=juce::AudioChannelSet::mono()&&main!=juce::AudioChannelSet::stereo())||main!=l.getMainInputChannelSet())return false;if(l.inputBuses.size()>1){const auto sc=l.getChannelSet(true,1);if(!sc.isDisabled()&&sc!=juce::AudioChannelSet::mono()&&sc!=juce::AudioChannelSet::stereo())return false;}return true;}
void GillNextProcessor::prepareToPlay(double rate,int block){auto&e=*impl;if(kind==NextKind::Align){e.signalThreadShouldExit();e.notify();e.stopThread(-1);e.workerCommand=0;}rateSupported=std::isfinite(rate)&&rate>=8000&&rate<=192000;e.fs=rateSupported?rate:48000;e.channelCount=juce::jlimit(1,2,getMainBusNumOutputChannels());uiRate=e.fs;e.update();int latency=0;
 if(kind==NextKind::Ride)e.ride.prepare(e.fs,block,e.channelCount);if(kind==NextKind::Clean){e.clean.prepare(e.fs,block,e.channelCount);latency=e.clean.latencySamples();}if(kind==NextKind::Pocket)e.pocket.prepare(e.fs,block,e.channelCount);if(kind==NextKind::Form){e.form.prepare(e.fs,block,e.channelCount);latency=e.form.latencySamples();}if(kind==NextKind::Finish){e.finish.prepare(e.fs,block,e.channelCount);latency=e.finish.latencySamples();e.outputTruePeak.prepare(e.fs,e.channelCount);e.loudness.prepare(e.fs,e.channelCount);e.learnLoudness.prepare(e.fs,e.channelCount);}
 if(kind==NextKind::Align){e.capacity=int(e.fs*20);for(auto&t:e.takes){t.resize(e.channelCount);for(auto&c:t)c.assign(e.capacity,0);}for(auto&n:e.takeSize)n=0;e.captureLane=-1;e.captureCommand=0;captureState=0;guideSeconds=doubleSeconds=0;alignState=0;e.result=nullptr;e.reading=nullptr;e.previous=nullptr;e.results.clear();{std::lock_guard<std::mutex>l(e.statusMutex);if(e.audioPath.isNotEmpty())e.workerCommand=(std::uint64_t(e.generation.load())<<8)|3;}e.startThread();}
 e.update();e.dryPos=0;for(auto&d:e.dry)d.assign(latency+1,0);setLatencySamples(latency);e.bypass.reset(e.fs,.005);e.bypass.setCurrentAndTargetValue(!rateSupported||e.at("bypass")>.5f?1:0);e.match.reset(e.fs,.15);e.match.setCurrentAndTargetValue(1);e.mono.reset(e.fs,.02);e.mono.setCurrentAndTargetValue(e.at("mono"));e.previewFade.reset(e.fs,.005);e.previewFade.setCurrentAndTargetValue(0);e.meter.prepare(e.fs);e.prepared=true;
}
void GillNextProcessor::releaseResources(){impl->prepared=false;impl->ride.reset();impl->clean.reset();impl->pocket.reset();impl->form.reset();impl->finish.reset();}
double GillNextProcessor::getTailLengthSeconds()const{return kind==NextKind::Form?impl->form.tailSeconds():kind==NextKind::Finish?impl->finish.tailSeconds():kind==NextKind::Pocket?impl->pocket.tailSeconds():getLatencySamples()/uiRate.load();}
void GillNextProcessor::process(juce::AudioBuffer<float>&buffer,bool hostBypass){juce::ScopedNoDenormals nd;auto&e=*impl;if(!e.prepared)return;auto main=getBusBuffer(buffer,false,0);const int channels=std::min(2,main.getNumChannels()),frames=main.getNumSamples();if(channels<1||frames<=0)return;e.update();
 juce::AudioBuffer<float>sc;if(getBusCount(true)>1&&getBus(true,1)->isEnabled())sc=getBusBuffer(buffer,true,1);const int scChannels=std::min(2,sc.getNumChannels());
 std::int64_t hostSample=0;bool hasHost=false,hasTransport=false,playing=false;if(auto*ph=getPlayHead())if(auto position=ph->getPosition()){hasTransport=true;playing=position->getIsPlaying();if(auto t=position->getTimeInSamples()){hasHost=true;hostSample=*t;}}
 if(e.meterReset.exchange(false)){e.loudness.reset();e.outputTruePeak.reset();e.finish.resetPeakStatistics();momentaryLufs=integratedLufs=-100;}
 if(e.learnRequest.exchange(false)){e.learnLoudness.reset();e.learnFrames=0;e.learnPower={};e.learnLowState={};e.learnHighState={};learnState=1;learnProgress=0;}
 const int capture=e.captureCommand.exchange(0);if(capture){if(capture<0)e.captureLane=-1;else if(alignState!=2){e.captureLane=capture-1;e.takeSize[e.captureLane]=0;e.waveIsResult[e.captureLane]=false;for(auto&w:e.capturePeaks[e.captureLane])w=0;if(e.captureLane==1){e.dubAnchorSeconds=hostSample/e.fs;e.dubHasHost=hasHost;}}captureState=e.captureLane+1;}
 const bool preview=kind==NextKind::Align&&e.at("preview")>.5f&&(!hasTransport||playing||isNonRealtime());if(preview&&!e.wasPreview)e.previewClock=0;e.wasPreview=preview;
 const gillnext::AlignResult* aligned=nullptr;if(kind==NextKind::Align){do{aligned=e.result.load();e.reading.store(aligned);}while(aligned!=e.result.load());e.previewFade.setTargetValue(preview&&aligned&&aligned->success?1:0);}
 e.bypass.setTargetValue(hostBypass||!rateSupported||e.at("bypass")>.5f?1:0);
 if(kind==NextKind::Finish){const float in=e.finish.inputRms(),out=e.finish.outputRms();e.match.setTargetValue(e.at("match")>.5f&&in>1e-5f&&out>1e-5f?std::min(1.f,in/out):1.f);e.mono.setTargetValue(e.at("mono"));}
 for(int at=0;at<frames;at+=128){const int n=std::min(128,frames-at);std::array<float*,2>audio{};std::array<const float*,2>side{};std::array<std::array<float,128>,2>original{},input{};
  for(int c=0;c<channels;++c)audio[c]=main.getWritePointer(c,at);for(int c=0;c<scChannels;++c)side[c]=sc.getReadPointer(c,at);
  for(int i=0;i<n;++i){std::array<float,2>x{};for(int c=0;c<channels;++c){x[c]=input[c][i]=float(gillnext::detail::input(audio[c][i]));audio[c][i]=x[c];e.dry[c][e.dryPos]=x[c];original[c][i]=e.dry[c][(e.dryPos+1)%e.dry[c].size()];}e.dryPos=(e.dryPos+1)%e.dry[0].size();
   if(kind==NextKind::Finish)e.learnSample(x,channels);
   if(kind==NextKind::Align&&e.captureLane>=0){const int lane=e.captureLane,index=e.takeSize[lane].load(std::memory_order_relaxed);if(index<e.capacity){float peak=0;for(int c=0;c<channels;++c){const float sample=lane==1?x[c]:(scChannels?float(gillnext::detail::input(side[std::min(c,scChannels-1)][i])):0);e.takes[lane][c][index]=sample;peak=std::max(peak,std::abs(sample));}const int bin=std::min(8191,int(std::int64_t(index)*8192/e.capacity));e.capturePeaks[lane][bin]=std::max(e.capturePeaks[lane][bin].load(),peak);e.takeSize[lane]=index+1;if(lane==0)guideSeconds=float((index+1)/e.fs);else doubleSeconds=float((index+1)/e.fs);}else{e.captureLane=-1;captureState=0;}}
  }
  if(kind==NextKind::Ride)e.ride.process(audio.data(),channels,n);else if(kind==NextKind::Clean)e.clean.process(audio.data(),channels,n);else if(kind==NextKind::Pocket){e.pocket.process(audio.data(),channels,n,side.data(),scChannels);if(e.at("listen")>.5f)for(int c=0;c<channels;++c)for(int i=0;i<n;++i)audio[c][i]=input[c][i]-audio[c][i];}
  else if(kind==NextKind::Form)e.form.process(audio.data(),channels,n);else if(kind==NextKind::Finish)e.finish.process(audio.data(),channels,n);
  else if(kind==NextKind::Align){for(int i=0;i<n;++i){const float fade=e.previewFade.getNextValue();const auto index=hasHost&&e.dubHasHost.load()?hostSample+at+i-std::int64_t(std::llround(e.dubAnchorSeconds.load()*e.fs)):e.previewClock++;if(aligned&&index>=0&&index<std::int64_t(aligned->audio[0].size()))for(int c=0;c<channels;++c){const double edge=std::clamp(std::min(double(index),double(aligned->audio[0].size()-1-index))/(e.fs*.005),0.,1.);audio[c][i]+=float(fade*edge)*(aligned->audio[std::min(c,int(aligned->audio.size())-1)][size_t(index)]-audio[c][i]);}}}
  for(int i=0;i<n;++i){const float bp=e.bypass.getNextValue(),matchGain=kind==NextKind::Finish?e.match.getNextValue():1,monoAmount=kind==NextKind::Finish?e.mono.getNextValue():0;const float mid=(audio[0][i]+audio[channels-1][i])*.5f;std::array<float,2>out{};double ip=0,op=0,pkIn=0,pkOut=0;
   for(int c=0;c<channels;++c){const float wet=float(gillnext::detail::input(audio[c][i]+monoAmount*(mid-audio[c][i])))*matchGain;out[c]=audio[c][i]=bp>=1?original[c][i]:wet+bp*(original[c][i]-wet);ip+=double(input[c][i])*input[c][i]/channels;op+=double(out[c])*out[c]/channels;pkIn=std::max(pkIn,std::abs(double(input[c][i])));pkOut=std::max(pkOut,std::abs(double(out[c])));}e.meter.sample(ip,op,pkIn,pkOut);if(kind==NextKind::Finish){e.loudness.sample(out,channels);e.outputTruePeak.process(out[0],out[channels-1],channels);}
  }
 }
 e.reading=nullptr;e.meter.publish();inputPeak=e.meter.inputPeak.load();outputPeak=e.meter.outputPeak.load();inputRms=e.meter.in.load();outputRms=e.meter.out.load();
 if(kind==NextKind::Ride){appliedGain=e.ride.gainDb();voiceActivity=e.ride.activity();reduction=e.ride.gainReductionDb();}else if(kind==NextKind::Pocket){sidechainActive=e.pocket.sidechainActive();reduction=e.pocket.gainReductionDb();}else if(kind==NextKind::Clean){auto r=e.clean.reductionsDb();for(int j=0;j<3;++j)cleanReduction[j]=r[j];reduction=e.clean.gainReductionDb();}else if(kind==NextKind::Finish){reduction=e.finish.gainReductionDb();compressorReduction=e.finish.compressorReductionDb();limiterReduction=e.finish.limiterReductionDb();truePeakDb=float(gillnext::detail::db(e.outputTruePeak.peak()));momentaryLufs=float(e.loudness.momentary);integratedLufs=float(e.loudness.integrated);}
 if((e.displayClock+=frames)>=e.fs*.05){e.displayClock=0;if(kind==NextKind::Ride){auto h=e.ride.history();for(int j=0;j<128;++j)e.display[j]=h.gainDb[j];}else for(int j=0;j<128;++j){const float hz=float(20*std::pow(std::min(20000.,e.fs*.45)/20.,j/127.));e.display[j]=kind==NextKind::Pocket?e.pocket.responseDb(hz):kind==NextKind::Finish?e.finish.toneResponseDb(hz):0;}}
}
void GillNextProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,false);}void GillNextProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&m){m.clear();process(b,true);}
std::array<float,128>GillNextProcessor::graph()const{std::array<float,128>r{};for(int i=0;i<128;++i)r[i]=impl->display[i].load();return r;}
std::array<float,256>GillNextProcessor::waveform(int lane)const{std::array<float,256>r{};if(lane<0||lane>1)return r;if(impl->waveIsResult[lane]){for(int i=0;i<256;++i)r[i]=impl->waves[lane][i].load();}else{const double used=impl->capacity>0?8192.*impl->takeSize[lane].load()/impl->capacity:0;for(int i=0;i<256;++i){const int begin=std::clamp(int(i*used/256),0,8191),end=std::clamp(int(std::ceil((i+1)*used/256)),0,8192);for(int j=begin;j<end;++j)r[i]=std::max(r[i],impl->capturePeaks[lane][j].load());}}return r;}

void GillNextProcessor::capture(int lane){if(kind!=NextKind::Align||lane<0||lane>1||alignState==2||impl->captureBuffersRead)return;setValue("preview",0);if(lane==0&&(!getBus(true,1)||!getBus(true,1)->isEnabled())){impl->message("CONNECT GUIDE TO EXTERNAL SIDECHAIN");return;}if(lane==1&&captureState.load()!=2)impl->invalidate();impl->captureCommand=captureState.load()==lane+1?-1:lane+1;impl->message("CAPTURE UP TO 20 S / CLICK AGAIN TO STOP");}
void GillNextProcessor::alignTakes(){if(kind!=NextKind::Align||alignState==2)return;setValue("preview",0);alignState=2;impl->captureCommand=-1;impl->queue(1,true);impl->message("ALIGNING TAKES");impl->notify();}
void GillNextProcessor::undoAlignment(){if(kind!=NextKind::Align||alignState==2)return;impl->queue(2);}
void GillNextProcessor::clearCaptures(){if(kind!=NextKind::Align||alignState==2||captureState!=0||impl->captureBuffersRead)return;setValue("preview",0);for(auto&n:impl->takeSize)n=0;for(auto&a:impl->waves)for(auto&v:a)v=0;for(auto&a:impl->capturePeaks)for(auto&v:a)v=0;for(auto&v:impl->waveIsResult)v=false;guideSeconds=doubleSeconds=0;alignState=0;impl->invalidate();impl->message("CAPTURE GUIDE + DOUBLE / UP TO 20 SECONDS");}
void GillNextProcessor::startLearn(){if(kind==NextKind::Finish){impl->learnRequest=true;impl->message("PLAY 12 S OF REPRESENTATIVE MUSIC");}}
void GillNextProcessor::applyLearn(){if(kind!=NextKind::Finish||learnState!=2)return;impl->beforeLearn.clear();for(const char*id:{"drive","low","mid","high","comp"})impl->beforeLearn.emplace_back(id,value(id));setValue("drive",learnDrive);setValue("low",learnLow);setValue("mid",learnMid);setValue("high",learnHigh);setValue("comp",learnComp);learnState=3;impl->message("SUGGESTION APPLIED / COMPARE AT MATCHED LEVEL");}
void GillNextProcessor::revertLearn(){for(const auto&v:impl->beforeLearn)setValue(v.first,v.second);if(!impl->beforeLearn.empty()){learnState=2;impl->message("PREVIOUS SETTINGS RESTORED");}}
void GillNextProcessor::resetMeters(){impl->meterReset=true;}
void GillNextProcessor::resetForm(){setValue("pitch",0);setValue("formant",0);}
juce::String GillNextProcessor::statusText()const{std::lock_guard<std::mutex>l(impl->statusMutex);return impl->status;}
void GillNextProcessor::selectPreset(int index,bool gesture){index=juce::jlimit(0,getNumPrograms()-1,index);currentProgram=index;for(const auto&d:definitions)if(d.id!="bypass"&&d.id!="preview")setValue(d.id,d.def,gesture);for(const auto&v:programs[size_t(index)].values)setValue(v.first,v.second,gesture);}
bool GillNextProcessor::presetMatches()const{const auto&preset=programs[size_t(currentProgram.load())];for(const auto&d:definitions){if(d.id=="bypass"||d.id=="preview")continue;float expected=d.def;for(const auto&v:preset.values)if(v.first==d.id)expected=v.second;if(std::abs(value(d.id)-expected)>std::max(.0001f,d.step*.51f))return false;}return true;}
void GillNextProcessor::getStateInformation(juce::MemoryBlock&out){auto s=apvts.copyState();s.setProperty("version",1,nullptr);s.setProperty("program",currentProgram.load(),nullptr);if(kind==NextKind::Align){std::lock_guard<std::mutex>l(impl->statusMutex);s.setProperty("alignedAudio",impl->audioPath,nullptr);s.setProperty("anchorSeconds",impl->dubAnchorSeconds.load(),nullptr);s.setProperty("hostAnchor",impl->dubHasHost.load(),nullptr);if(impl->audioPath.isEmpty()){auto c=s.getChildWithProperty("id","preview");if(c.isValid())c.setProperty("value",0,nullptr);}}if(auto x=s.createXml())copyXmlToBinary(*x,out);}
void GillNextProcessor::setStateInformation(const void*data,int bytes){if(!data||bytes<=0||bytes>1024*1024)return;if(auto x=getXmlFromBinary(data,bytes))if(x->hasTagName(apvts.state.getType())){auto incoming=juce::ValueTree::fromXml(*x),clean=apvts.copyState();std::vector<std::pair<juce::RangedAudioParameter*,float>>accepted;
 for(auto c:incoming){auto id=c.getProperty("id").toString();auto*param=apvts.getParameter(id);double v=0;if(!param||!numeric(c.getProperty("value"),v))continue;const auto&r=param->getNormalisableRange();const float legal=r.snapToLegalValue(float(std::clamp(v,double(r.start),double(r.end))));auto target=clean.getChildWithProperty("id",id);if(target.isValid()){target.setProperty("value",legal,nullptr);auto found=std::find_if(accepted.begin(),accepted.end(),[&](const auto&a){return a.first==param;});if(found!=accepted.end())found->second=param->convertTo0to1(legal);else accepted.emplace_back(param,param->convertTo0to1(legal));}}
 if(kind==NextKind::Align){impl->captureCommand=-1;auto path=incoming.getProperty("alignedAudio").toString();{std::lock_guard<std::mutex>l(impl->statusMutex);++impl->generation;impl->result=nullptr;impl->previous=nullptr;impl->audioPath=path;alignState=path.isEmpty()?0:2;impl->workerCommand=path.isEmpty()?0:((std::uint64_t(impl->generation.load())<<8)|3);}double seconds=0;if(numeric(incoming.getProperty("anchorSeconds"),seconds))impl->dubAnchorSeconds=std::clamp(seconds,-86400.,86400.);impl->dubHasHost=bool(incoming.getProperty("hostAnchor",false));if(path.isNotEmpty())impl->notify();else{auto target=clean.getChildWithProperty("id","preview");if(target.isValid())target.setProperty("value",0.f,nullptr);for(auto&item:accepted)if(item.first==apvts.getParameter("preview"))item.second=0;}}
 if(!accepted.empty()){apvts.replaceState(clean);for(const auto&a:accepted)a.first->setValueNotifyingHost(a.second);}double program=0;if(numeric(incoming.getProperty("program"),program))currentProgram=juce::jlimit(0,getNumPrograms()-1,int(std::clamp(program,0.,double(getNumPrograms()-1))));}}
juce::AudioProcessorEditor*GillNextProcessor::createEditor(){return new GillNextEditor(*this);}
