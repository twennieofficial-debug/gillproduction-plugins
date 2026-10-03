#include "NoteEngine.h"
#include "../ThirdParty/signalsmith-stretch/signalsmith-stretch.h"

namespace gill::note {
Engine::Engine():Thread("GILLNOTE offline editor"),captureRing(std::make_unique<Frame[]>(capacity)),previewRing(std::make_unique<Frame[]>(previewCapacity)){startThread();}
Engine::~Engine(){signalThreadShouldExit();stopThread(-1);}
juce::File Engine::cacheRoot(){auto overrideRoot=juce::SystemStats::getEnvironmentVariable("GILL_NOTE_AUDIO_ROOT",{});if(overrideRoot.isNotEmpty())return juce::File(overrideRoot);return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("GILLPRODUCTION/GILLNOTE/Transfer");}
bool Engine::validToken(const juce::String&s){if(s.length()!=32)return false;for(auto c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
void Engine::prepare(double fs,int channels){rateView=std::isfinite(fs)&&fs>=8000&&fs<=192000?fs:48000;channelsView=std::clamp(channels,1,2);stopAudition();if(state==Capturing)stopRequested=true;}
void Engine::setStatus(juce::String s){std::lock_guard<std::mutex>lock(mutex);message=std::move(s);}
juce::String Engine::status()const{std::lock_guard<std::mutex>lock(mutex);return message;}
Plan Engine::plan()const{std::lock_guard<std::mutex>lock(mutex);return visual;}
Engine::Saved Engine::saved()const{std::lock_guard<std::mutex>lock(mutex);return resetRequested?Saved{resetToken,resetStart,resetNotes}:Saved{token,startSeconds.load(),visual.notes};}
juce::File Engine::exportFile()const{std::lock_guard<std::mutex>lock(mutex);return !resetRequested&&renderedRevision==revision.load()?result:juce::File{};}
void Engine::arm(){std::lock_guard<std::mutex>lock(mutex);resetArm=true;resetToken.clear();resetFile=juce::File{};resetNotes.clear();resetStart=0;resetRequested=true;previewing=false;}
void Engine::stop(){stopRequested=true;stopAudition();}
void Engine::importFile(const juce::File&f){std::lock_guard<std::mutex>lock(mutex);resetArm=false;resetToken.clear();resetFile=f;resetNotes.clear();resetStart=0;resetRequested=true;previewing=false;}
void Engine::restore(const juce::String&t,double start,const std::vector<Note>&notes){if(!validToken(t))return;std::lock_guard<std::mutex>lock(mutex);resetArm=false;resetToken=t;resetFile=juce::File{};resetNotes=notes;resetStart=start;resetRequested=true;previewing=false;}
void Engine::clear(){std::lock_guard<std::mutex>lock(mutex);resetArm=false;resetToken.clear();resetFile=juce::File{};resetNotes.clear();resetStart=0;resetRequested=true;previewing=false;}
void Engine::history(){undoStack.push_back(visual.notes);if(undoStack.size()>60)undoStack.erase(undoStack.begin());redoStack.clear();}
void Engine::edit(Note n){if(!std::isfinite(n.target)||!std::isfinite(n.start)||!std::isfinite(n.end)||!std::isfinite(n.strength))return;std::lock_guard<std::mutex>lock(mutex);auto it=std::find_if(visual.notes.begin(),visual.notes.end(),[&](auto&v){return v.id==n.id;});if(it==visual.notes.end())return;
 auto index=std::size_t(it-visual.notes.begin());n.start=std::clamp(n.start,index?visual.notes[index-1].end:0.,it->end-.025);n.end=std::clamp(n.end,n.start+.025,index+1<visual.notes.size()?visual.notes[index+1].start:visual.duration);n.target=std::clamp(n.target,it->detected-24,it->detected+24);n.strength=std::clamp(n.strength,0.f,1.f);n.detected=it->detected;n.confidence=it->confidence;history();*it=n;++revision;previewing=false;message="Note geaendert. RENDER erstellt die aktuelle WAV.";}
void Engine::snapNotes(int key,int scale,bool all,int selected){std::lock_guard<std::mutex>lock(mutex);if(visual.notes.empty())return;history();for(auto&n:visual.notes)if(all||n.id==selected)n.target=snap(n.target,key,scale);++revision;previewing=false;message="Noten eingerastet. RENDER aktualisiert die WAV.";}
void Engine::undo(){std::lock_guard<std::mutex>lock(mutex);if(undoStack.empty())return;redoStack.push_back(visual.notes);visual.notes=std::move(undoStack.back());undoStack.pop_back();++revision;previewing=false;}
void Engine::redo(){std::lock_guard<std::mutex>lock(mutex);if(redoStack.empty())return;undoStack.push_back(visual.notes);visual.notes=std::move(redoStack.back());redoStack.pop_back();++revision;previewing=false;}
bool Engine::canUndo()const{std::lock_guard<std::mutex>lock(mutex);return !undoStack.empty();}bool Engine::canRedo()const{std::lock_guard<std::mutex>lock(mutex);return !redoStack.empty();}
void Engine::render(){renderRequested=true;previewing=false;}
void Engine::audition(double start){previewStart=std::max(0.,start);previewCommand=1;}
void Engine::stopAudition(){previewing=false;previewCommand=2;}
void Engine::process(juce::AudioBuffer<float>&b,bool playing,bool valid,std::int64_t position,bool live,bool bypass)noexcept{
 if(resetRequested)return;audioUsers.fetch_add(1,std::memory_order_acquire);struct Guard{std::atomic<unsigned>&v;~Guard(){v.fetch_sub(1,std::memory_order_release);}}guard{audioUsers};if(resetRequested)return;
 int s=state.load(std::memory_order_acquire);double fs=rateView.load();
 if(s==Armed&&playing&&valid&&!stopRequested){startSeconds=double(position)/fs;expectedPosition=position;state.store(Capturing,std::memory_order_release);s=Capturing;}
 if(s==Capturing){if(!playing||!valid||stopRequested||std::llabs(position-expectedPosition)>1||std::abs(fs-captureRate)>.1){state=Analysing;captureFinished.store(true,std::memory_order_release);}else{
  auto wr=captureWrite.load(std::memory_order_relaxed),rd=captureRead.load(std::memory_order_acquire),done=captureFrames.load();auto count=std::min<std::uint64_t>(b.getNumSamples(),std::uint64_t(captureRate*maxSeconds)-done);
  if(wr-rd+count>capacity){captureFailed=true;state=Analysing;captureFinished.store(true,std::memory_order_release);}else{for(std::uint64_t n=0;n<count;++n){float l=b.getSample(0,int(n)),r=b.getSample(std::min(1,b.getNumChannels()-1),int(n));captureRing[(wr+n)&(capacity-1)]={std::isfinite(l)?l:0,std::isfinite(r)?r:0};}captureWrite.store(wr+count,std::memory_order_release);captureFrames=done+count;capturedSeconds=double(done+count)/captureRate;expectedPosition=position+b.getNumSamples();if(done+count>=std::uint64_t(captureRate*maxSeconds)){state=Analysing;captureFinished.store(true,std::memory_order_release);}}
 }return;}
 // LIVE and host bypass always pass the original buffer without touching it.
 if(live||bypass){previewing=false;return;}if(!previewing)return;
 auto rd=previewRead.load(std::memory_order_relaxed),wr=previewWrite.load(std::memory_order_acquire);auto count=std::min<std::uint64_t>(b.getNumSamples(),wr-rd);
 if(count<std::uint64_t(b.getNumSamples())&&!previewEnd){previewing=false;return;}
 b.clear();for(std::uint64_t n=0;n<count;++n){auto f=previewRing[(rd+n)&(previewCapacity-1)];b.setSample(0,int(n),f.l);if(b.getNumChannels()>1)b.setSample(1,int(n),f.r);}previewRead.store(rd+count,std::memory_order_release);previewSeconds=previewStart.load()+double(rd+count)/fs;if(count<std::uint64_t(b.getNumSamples()))previewing=false;
}
void Engine::reset(){
 juce::File file;juce::String id;double start=0;std::vector<Note>edits;bool armNow=false,copy=false;
 {std::lock_guard<std::mutex>lock(mutex);if(audioUsers.load(std::memory_order_acquire))return;writer.reset();previewReader.reset();previewing=false;previewCommand=0;previewWrite=0;previewRead=0;previewEnd=false;
  source=juce::File{};result=juce::File{};visual={};undoStack.clear();redoStack.clear();id=resetToken;file=resetFile;start=resetStart;edits=resetNotes;armNow=resetArm;copy=file!=juce::File();token=id;startSeconds=start;
  captureWrite=0;captureRead=0;captureFrames=0;capturedSeconds=0;stopRequested=false;captureFinished=false;captureFailed=false;renderRequested=false;sourceFrames=0;++revision;state=Idle;resetRequested.store(false,std::memory_order_release);
 }
 if(armNow){captureRate=rateView;captureChannels=channelsView;id=juce::Uuid().toString().removeCharacters("-");auto dir=cacheRoot().getChildFile(id);if(!dir.createDirectory().wasOk()){state=Error;setStatus("Cacheordner nicht beschreibbar.");return;}file=dir.getChildFile("source.wav");auto stream=file.createOutputStream();juce::WavAudioFormat wav;if(stream)writer.reset(wav.createWriterFor(stream.release(),captureRate,unsigned(captureChannels),24,{},0));if(!writer){state=Error;return;}{std::lock_guard<std::mutex>lock(mutex);token=id;source=file;}state.store(Armed,std::memory_order_release);setStatus("BEREIT - Song abspielen, dann STOP. Bis 5:00.");return;}
 if(!copy&&id.isNotEmpty())file=cacheRoot().getChildFile(id).getChildFile("source.wav");if(file!=juce::File())analyseFile(file,copy,start,edits);else setStatus("LEARN oder WAV importieren. Maximal 5:00.");
}
void Engine::finishCapture(){writer.reset();captureFinished=false;stopRequested=false;if(captureFailed||captureFrames==0){state=Error;setStatus("Aufnahme unvollstaendig. Datentraeger pruefen und neu aufnehmen.");return;}juce::File file;{std::lock_guard<std::mutex>lock(mutex);file=source;}analyseFile(file,false,startSeconds,{});}
void Engine::analyseFile(const juce::File&file,bool copy,double start,const std::vector<Note>&edits){
 state=Analysing;setStatus("ANALYSE - Tonhoehen und einzelne Noten werden erkannt.");juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>reader(formats.createReaderFor(file));
 if(!reader||reader->sampleRate<8000||reader->sampleRate>192000||reader->numChannels<1||reader->numChannels>2||reader->lengthInSamples<1){state=Error;setStatus("Audio fehlt/ungueltig. Mono/Stereo WAV, 8-192 kHz importieren.");return;}
 sourceRate=reader->sampleRate;sourceChannels=int(reader->numChannels);sourceFrames=std::min(reader->lengthInSamples,juce::int64(sourceRate*maxSeconds));double duration=sourceFrames/sourceRate;juce::File owned=file;std::unique_ptr<juce::AudioFormatWriter>copyWriter;
 if(copy){auto id=juce::Uuid().toString().removeCharacters("-");auto dir=cacheRoot().getChildFile(id);if(!dir.createDirectory().wasOk()){state=Error;return;}owned=dir.getChildFile("source.wav");auto stream=owned.createOutputStream();juce::WavAudioFormat wav;if(stream)copyWriter.reset(wav.createWriterFor(stream.release(),sourceRate,unsigned(sourceChannels),24,{},0));if(!copyWriter){state=Error;return;}std::lock_guard<std::mutex>lock(mutex);token=id;startSeconds=start;}
 std::vector<float>mono;mono.reserve(std::size_t(std::ceil(duration*analysisRate)));double next=0,previous=0;std::array<double,4>low{};const double a=1-std::exp(-2*3.141592653589793*2500/sourceRate);bool good=true;
 for(juce::int64 offset=0;offset<sourceFrames&&!threadShouldExit()&&!resetRequested;offset+=4096){int count=int(std::min<juce::int64>(4096,sourceFrames-offset));scratch.clear();good=reader->read(&scratch,0,count,offset,true,sourceChannels==2)&&good;if(copyWriter)good=copyWriter->writeFromAudioSampleBuffer(scratch,0,count)&&good;
  for(int n=0;n<count;++n){double x=0;for(int c=0;c<sourceChannels;++c){float v=scratch.getSample(c,n);x+=std::isfinite(v)?v/sourceChannels:0;}for(auto&f:low){f+=a*(x-f);x=f;}double index=double(offset+n);while(next<=index){double fraction=std::clamp(next-index+1.,0.,1.);mono.push_back(float(previous+(x-previous)*fraction));next+=sourceRate/analysisRate;}previous=x;}}
 copyWriter.reset();if(resetRequested||threadShouldExit())return;if(!good){state=Error;setStatus("Audio konnte nicht vollstaendig gelesen werden.");return;}
 auto nextPlan=analyse(mono,duration,[&]{return threadShouldExit()||resetRequested.load();});if(resetRequested||threadShouldExit())return;nextPlan.start=start;
 for(auto&n:nextPlan.notes)for(auto&e:edits)if(e.id==n.id&&std::abs(e.detected-n.detected)<.1f&&e.start>=0&&e.end<=duration&&e.end>e.start&&std::abs(e.target-n.detected)<=24){n.target=e.target;n.strength=e.strength;n.start=e.start;n.end=e.end;break;}
 {std::lock_guard<std::mutex>lock(mutex);source=owned;visual=std::move(nextPlan);startSeconds=start;result=juce::File{};++revision;message=juce::String(visual.notes.size())+" Noten / "+juce::String(duration,1)+" s. Ziehen, dann RENDER.";}
 state=Ready;renderRequested=true;
}
void Engine::renderFile(){
 Plan p;juce::File input;std::uint64_t rev;{std::lock_guard<std::mutex>lock(mutex);if(source==juce::File()||visual.frames.empty())return;p=visual;input=source;rev=revision.load();}
 state=Rendering;setStatus("RENDER - Notenbearbeitung wird als WAV berechnet.");previewing=false;juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatReader>reader;if(auto stream=input.createInputStream())reader.reset(wav.createReaderFor(stream.release(),true));
 auto output=input.getSiblingFile("GILLNOTE-"+juce::Uuid().toString().removeCharacters("-")+".wav");auto stream=output.createOutputStream();juce::StringPairArray meta;meta.set(juce::WavAudioFormat::bwavDescription,"GILLNOTE edited vocal; source start "+juce::String(p.start,6)+" seconds");if(p.start>=0)meta.set(juce::WavAudioFormat::bwavTimeReference,juce::String(juce::int64(p.start*sourceRate)));
 std::unique_ptr<juce::AudioFormatWriter>out;if(stream)out.reset(wav.createWriterFor(stream.release(),sourceRate,unsigned(sourceChannels),24,meta,0));if(!reader||!out){state=Error;setStatus("Renderdatei nicht beschreibbar.");return;}
 bool good=true,cancel=false;const bool clean=neutral(p);signalsmith::stretch::SignalsmithStretch<float>stretch;int latency=0;if(!clean){stretch.presetDefault(sourceChannels,sourceRate);latency=stretch.inputLatency()+stretch.outputLatency();}
 constexpr int block=128;juce::AudioBuffer<float>in(sourceChannels,block),rendered(sourceChannels,block);std::array<const float*,2>inputs{};std::array<float*,2>outputs{};for(int c=0;c<sourceChannels;++c){inputs[c]=in.getReadPointer(c);outputs[c]=rendered.getWritePointer(c);}
 juce::int64 written=0;float smoothed=0;bool formants=preserveFormants.load();
 for(juce::int64 offset=0;offset<sourceFrames+latency&&!cancel;offset+=block){cancel=threadShouldExit()||resetRequested||revision.load()!=rev;if(cancel)break;in.clear();int take=int(std::min<juce::int64>(block,std::max<juce::int64>(0,sourceFrames-offset)));if(take)good=reader->read(&in,0,take,offset,true,sourceChannels==2)&&good;for(int c=0;c<sourceChannels;++c)for(int n=0;n<block;++n)if(!std::isfinite(in.getSample(c,n)))in.setSample(c,n,0);
  if(clean)rendered.makeCopyOf(in);else{double t=(offset-stretch.inputLatency()+block*.5)/sourceRate;float shift=shiftAt(p,t);smoothed+=(1.f-float(std::exp(-block/(sourceRate*.006))))*(shift-smoothed);stretch.setTransposeSemitones(smoothed);stretch.setFormantFactor(1.,formants);stretch.process(inputs.data(),block,outputs.data(),block);}
  int skip=int(std::clamp<juce::int64>(latency-offset,0,block));int count=int(std::min<juce::int64>(block-skip,sourceFrames-written));if(count>0){for(int c=0;c<sourceChannels;++c)for(int n=skip;n<skip+count;++n){float v=rendered.getSample(c,n);if(!std::isfinite(v)){good=false;v=0;}rendered.setSample(c,n,v);}good=out->writeFromAudioSampleBuffer(rendered,skip,count)&&good;written+=count;}}
 out.reset();if(cancel){output.deleteFile();if(!resetRequested)state=Ready;return;}if(!good||written!=sourceFrames){output.deleteFile();state=Error;setStatus("WAV konnte nicht vollstaendig gerendert werden.");return;}
 {std::lock_guard<std::mutex>lock(mutex);if(revision.load()==rev&&!resetRequested){result=output;renderedRevision=rev;completedRevision=rev;message="BEREIT - WAV ziehen. Originalposition: "+juce::String(p.start,2)+" s / "+juce::String(p.duration,1)+" s.";}}
 state=Ready;
}
void Engine::fillPreview(){
 int command=previewCommand.exchange(0);if(command){previewing=false;if(audioUsers.load(std::memory_order_acquire)){previewCommand=command;return;}previewReader.reset();previewRead=0;previewWrite=0;previewEnd=false;
  if(command==1){auto file=exportFile();juce::WavAudioFormat wav;if(auto stream=file.createInputStream())previewReader.reset(wav.createReaderFor(stream.release(),true));if(previewReader){previewOutputRate=rateView;previewCursor=std::min(previewStart.load()*previewReader->sampleRate,double(previewReader->lengthInSamples));previewSeconds=previewCursor/previewReader->sampleRate;previewScratch.setSize(2,int(std::ceil(4096*previewReader->sampleRate/previewOutputRate))+3);}}}
 if(!previewReader)return;if(std::abs(rateView.load()-previewOutputRate)>.1){previewReader.reset();previewing=false;return;}auto wr=previewWrite.load(std::memory_order_relaxed),rd=previewRead.load(std::memory_order_acquire);if(wr-rd+4096>previewCapacity)return;
 const double ratio=previewReader->sampleRate/previewOutputRate;auto start=juce::int64(std::floor(previewCursor));int needed=int(std::ceil(4096*ratio))+2;previewScratch.clear();if(!previewReader->read(&previewScratch,0,needed,start,true,previewReader->numChannels==2)){previewReader.reset();previewing=false;return;}int count=0;
 for(;count<4096&&previewCursor<previewReader->lengthInSamples;++count){double local=previewCursor-start;int i=int(local);float fraction=float(local-i);Frame f;auto sample=[&](int c){return previewScratch.getSample(c,i)*(1-fraction)+previewScratch.getSample(c,i+1)*fraction;};f.l=sample(0);f.r=sample(previewReader->numChannels>1?1:0);double seconds=previewCursor/previewReader->sampleRate;float fade=float(std::clamp(std::min((seconds-previewStart.load())/.005,(previewReader->lengthInSamples-previewCursor)/(previewReader->sampleRate*.005)),0.,1.));f.l*=fade;f.r*=fade;previewRing[(wr+count)&(previewCapacity-1)]=f;previewCursor+=ratio;}
 previewWrite.store(wr+count,std::memory_order_release);if(count<4096){previewEnd=true;previewReader.reset();}if(command==1)previewing=true;
}
void Engine::run(){while(!threadShouldExit()){
 if(resetRequested){if(audioUsers.load(std::memory_order_acquire)){wait(1);continue;}reset();if(resetRequested)continue;}
 if(writer){auto rd=captureRead.load(std::memory_order_relaxed),wr=captureWrite.load(std::memory_order_acquire);while(rd<wr){int count=int(std::min<std::uint64_t>(4096,wr-rd));for(int n=0;n<count;++n){auto f=captureRing[(rd+n)&(capacity-1)];scratch.setSample(0,n,f.l);scratch.setSample(1,n,f.r);}if(!writer->writeFromAudioSampleBuffer(scratch,0,count))captureFailed=true;rd+=count;captureRead.store(rd,std::memory_order_release);}if(captureFinished&&captureRead==captureWrite)finishCapture();else if(state==Armed&&stopRequested){writer.reset();state=Idle;setStatus("Keine Aufnahme. LEARN erneut starten.");}}
 if(renderRequested.exchange(false)&&!writer&&!resetRequested)renderFile();if(!writer&&!resetRequested)fillPreview();wait(3);
 }writer.reset();}
}
