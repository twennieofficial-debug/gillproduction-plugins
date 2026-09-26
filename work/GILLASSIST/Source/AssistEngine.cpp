#include "AssistEngine.h"

namespace gill::assist {
Engine::Engine(std::function<Settings()>fn):Thread("GILLASSIST analysis"),ring(std::make_unique<Frame[]>(capacity)),readSettings(std::move(fn)){startThread();}
Engine::~Engine(){signalThreadShouldExit();stopThread(-1);}
juce::File Engine::cacheRoot(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("GILLPRODUCTION/GILLASSIST/Transfer");}
bool Engine::validToken(const juce::String&s){if(s.length()!=32)return false;for(auto c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
void Engine::prepare(double rate,int channels){rateView=std::isfinite(rate)&&rate>=8000&&rate<=192000?rate:48000;channelView=std::clamp(channels,1,2);if(state==Capturing)stopRequested=true;currentGain=0;}
void Engine::setStatus(juce::String s){std::lock_guard<std::mutex>lock(mutex);message=std::move(s);}
juce::String Engine::status()const{std::lock_guard<std::mutex>lock(mutex);return message;}
juce::String Engine::token()const{std::lock_guard<std::mutex>lock(mutex);return cacheToken;}
juce::File Engine::exportFile()const{std::lock_guard<std::mutex>lock(mutex);return resetRequested?juce::File():rendered;}
juce::File Engine::claimExportFile(){std::lock_guard<std::mutex>lock(mutex);if(resetRequested)return {};if(rendered!=juce::File())claimedRenders.insert(rendered.getFullPathName());return rendered;}
Engine::TransferState Engine::transferState()const{std::lock_guard<std::mutex>lock(mutex);if(resetRequested)return {resetToken,resetStart,resetEdits};return {cacheToken,startSeconds.load(),manual};}
Plan Engine::plan()const{std::lock_guard<std::mutex>lock(mutex);return visual;}
std::vector<Edit>Engine::edits()const{std::lock_guard<std::mutex>lock(mutex);return manual;}
void Engine::arm(){const int current=state.load();if(current==Capturing||current==Armed){stop();return;}startRequested=true;}
void Engine::stop(){stopRequested=true;}
void Engine::importFile(const juce::File&file,double start){if((state==Capturing||state==Armed)&&!resetRequested)return;std::lock_guard<std::mutex>lock(mutex);resetToken={};resetImport=file;resetStart=std::isfinite(start)?start:0;resetEdits.clear();resetRequested=true;published=-1;}
void Engine::restore(const juce::String&t,double start,const std::vector<Edit>&e){
    if(!validToken(t)){setStatus("Kein gespeicherter Transfer. LEARN starten.");return;}
    std::lock_guard<std::mutex>lock(mutex);resetToken=t;resetImport={};resetStart=std::isfinite(start)?start:0;resetEdits=e;resetRequested=true;published=-1;
}
void Engine::clearTransfer(){std::lock_guard<std::mutex>lock(mutex);resetToken={};resetImport={};resetStart=0;resetEdits.clear();resetRequested=true;published=-1;}
void Engine::history(){undoStack.push_back(manual);if(undoStack.size()>100)undoStack.erase(undoStack.begin());redoStack.clear();}
void Engine::edit(Edit e){if(!std::isfinite(e.begin)||!std::isfinite(e.end)||!std::isfinite(e.db))return;e.begin=std::clamp(e.begin,0.,maxSeconds);e.end=std::clamp(e.end,e.begin,maxSeconds);e.db=std::clamp(e.db,-24.f,24.f);e.type=std::clamp(e.type,0,4);if(e.end<=e.begin)return;std::lock_guard<std::mutex>lock(mutex);if(manual.size()>=2000)return;history();manual.push_back(e);++editRevision;}
void Engine::clearEdits(){std::lock_guard<std::mutex>lock(mutex);history();manual.clear();++editRevision;}
void Engine::undo(){std::lock_guard<std::mutex>lock(mutex);if(undoStack.empty())return;redoStack.push_back(manual);manual=std::move(undoStack.back());undoStack.pop_back();++editRevision;}
void Engine::redo(){std::lock_guard<std::mutex>lock(mutex);if(redoStack.empty())return;undoStack.push_back(manual);manual=std::move(redoStack.back());redoStack.pop_back();++editRevision;}
void Engine::swapAB(){std::lock_guard<std::mutex>lock(mutex);history();manual.swap(abManual);++editRevision;}
bool Engine::canUndo()const{std::lock_guard<std::mutex>lock(mutex);return !undoStack.empty();}
bool Engine::canRedo()const{std::lock_guard<std::mutex>lock(mutex);return !redoStack.empty();}
void Engine::process(juce::AudioBuffer<float>&b,bool playing,bool hasPosition,std::int64_t position,bool bypass)noexcept{
    // Reset never races a callback that still writes the capture FIFO. New
    // callbacks remain dry while the worker closes/detaches the old transfer.
    if(resetRequested){timelineMatched=false;currentGain=0;return;}
    audioUsers.fetch_add(1,std::memory_order_acquire);
    struct AudioUse{std::atomic<unsigned>&users;~AudioUse(){users.fetch_sub(1,std::memory_order_release);}}use{audioUsers};
    if(resetRequested){timelineMatched=false;currentGain=0;return;}
    const double rate=rateView.load();if(hasPosition)positionView=double(position)/rate;
    int s=state.load(std::memory_order_acquire);
    if(s==Armed&&playing&&hasPosition&&!stopRequested){startSeconds.store(double(position)/rate);expectedPosition=position;state.store(Capturing,std::memory_order_release);s=Capturing;}
    if(s==Capturing){
        if(!playing||!hasPosition||stopRequested||std::llabs(position-expectedPosition)>1||std::abs(rate-captureRate)>.1){state.store(Analysing,std::memory_order_release);captureFinished.store(true,std::memory_order_release);}
        else{
            const auto wr=writeIndex.load(std::memory_order_relaxed),rd=readIndex.load(std::memory_order_acquire);const auto done=captureFrames.load();const auto maximum=std::uint64_t(std::floor(captureRate*maxSeconds));
            const auto count=std::uint64_t(std::min<int>(b.getNumSamples(),int(std::min<std::uint64_t>(maximum-done,INT_MAX))));
            if(wr-rd+count>capacity){captureFailed=true;state=Analysing;captureFinished.store(true,std::memory_order_release);}
            else{for(std::uint64_t n=0;n<count;++n){float l=b.getSample(0,int(n)),r=b.getSample(std::min(1,b.getNumChannels()-1),int(n));ring[(wr+n)&mask]={std::isfinite(l)?l:0,std::isfinite(r)?r:0};}writeIndex.store(wr+count,std::memory_order_release);captureFrames=done+count;capturedSeconds=double(done+count)/captureRate;expectedPosition=position+b.getNumSamples();if(done+count>=maximum){state=Analysing;captureFinished.store(true,std::memory_order_release);}}
        }
        timelineMatched=false;currentGain=0;return;
    }
    timelineMatched=false;if(bypass||!playing||!hasPosition||s==Armed){currentGain=0;return;}
    int index=published.load(std::memory_order_acquire);if(index<0){currentGain=0;return;}
    auto&slot=slots[std::size_t(index)];slot.users.fetch_add(1,std::memory_order_acquire);
    if(index!=published.load(std::memory_order_acquire)){slot.users.fetch_sub(1,std::memory_order_release);return;}
    float last=0;bool matched=false;
    for(int n=0;n<b.getNumSamples();++n){const double seconds=(position+n)/rate-slot.start;float value=0;if(seconds>=0&&seconds<slot.duration&&slot.count){double x=seconds/hopSeconds;int i=std::min(int(x),slot.count-1),j=std::min(i+1,slot.count-1);value=slot.values[std::size_t(i)]+float(x-i)*(slot.values[std::size_t(j)]-slot.values[std::size_t(i)]);matched=true;}if(value!=0){const float factor=gain(value);for(int c=0;c<b.getNumChannels();++c){const float sample=b.getSample(c,n);b.setSample(c,n,std::isfinite(sample)?sample*factor:0);}}last=value;}
    slot.users.fetch_sub(1,std::memory_order_release);timelineMatched=matched;currentGain=last;
}
void Engine::publish(const Plan&p){
    if(resetRequested)return;
    for(auto&slot:slots){const int i=int(&slot-slots.data());if(i==published.load()||slot.users.load(std::memory_order_acquire))continue;slot.count=int(std::min<std::size_t>(p.gainDb.size(),maxPoints));std::copy_n(p.gainDb.begin(),slot.count,slot.values.begin());slot.duration=p.duration;slot.start=p.startSeconds;published.store(i,std::memory_order_release);return;}
}
void Engine::run(){
    while(!threadShouldExit()){
        if(resetRequested){
            if(audioUsers.load(std::memory_order_acquire)){wait(1);continue;}
            std::lock_guard<std::mutex>lock(mutex);
            writer.reset();features.clear();published=-1;source={};rendered={};visual={};
            cacheToken=resetToken;startSeconds=resetStart;manual=resetEdits;abManual.clear();undoStack.clear();redoStack.clear();
            // Detach results, never delete a WAV that may already be in a DAW.
            unclaimedRenders.clear();claimedRenders.clear();
            pendingCopy=resetImport!=juce::File();pendingFile=pendingCopy?resetImport:(cacheToken.isEmpty()?juce::File():cacheRoot().getChildFile(cacheToken).getChildFile("source.wav"));pendingRestore=!pendingCopy&&cacheToken.isNotEmpty();pendingStart=resetStart;
            writeIndex=0;readIndex=0;captureFrames=0;capturedSeconds=0;startRequested=false;stopRequested=false;captureFinished=false;captureFailed=false;
            sourceDuration=0;currentGain=0;timelineMatched=false;state=Idle;++editRevision;
            message=cacheToken.isEmpty()?"Kein Transfer. LEARN, dann den Song abspielen.":"Gespeicherter Transfer wird geladen.";
            resetRequested.store(false,std::memory_order_release);
        }
        if(startRequested.exchange(false)&&state!=Capturing){
            writer.reset();published=-1;features.clear();capturedSeconds=0;captureFrames=0;writeIndex=0;readIndex=0;stopRequested=false;captureFinished=false;captureFailed=false;
            captureRate=rateView;captureChannels=channelView;const auto id=juce::Uuid().toString().removeCharacters("-");auto dir=cacheRoot().getChildFile(id);
            if(!dir.createDirectory().wasOk()){state=Error;setStatus("Cacheordner kann nicht angelegt werden.");continue;}
            auto file=dir.getChildFile("source.wav");auto stream=file.createOutputStream();juce::WavAudioFormat wav;if(stream)writer.reset(wav.createWriterFor(stream.release(),captureRate,unsigned(captureChannels),24,{},0));
            if(!writer){state=Error;setStatus("Transferdatei kann nicht geschrieben werden.");continue;}
            {std::lock_guard<std::mutex>lock(mutex);cacheToken=id;startSeconds=0;source=file;rendered={};visual={};manual.clear();undoStack.clear();redoStack.clear();}
            state.store(Armed,std::memory_order_release);setStatus("BEREIT - Song abspielen. Stop beendet LEARN.");
        }
        if(writer){
            auto rd=readIndex.load(std::memory_order_relaxed),wr=writeIndex.load(std::memory_order_acquire);
            while(rd<wr){const int count=int(std::min<std::uint64_t>(4096,wr-rd));for(int n=0;n<count;++n){const auto frame=ring[(rd+std::uint64_t(n))&mask];scratch.setSample(0,n,frame.l);scratch.setSample(1,n,frame.r);}if(!writer->writeFromAudioSampleBuffer(scratch,0,count))captureFailed=true;rd+=std::uint64_t(count);readIndex.store(rd,std::memory_order_release);}
            if(captureFinished.load(std::memory_order_acquire)&&readIndex.load()==writeIndex.load())finishCapture();
            else if(state==Armed&&stopRequested){writer.reset();state=Idle;setStatus("LEARN abgebrochen - kein Audiomaterial.");}
        }
        juce::File file;bool copy=false,restoreOnly=false;double start=0;
        {std::lock_guard<std::mutex>lock(mutex);if(pendingFile!=juce::File()&&!writer){file=pendingFile;copy=pendingCopy;restoreOnly=pendingRestore;start=pendingStart;pendingFile={};}}
        if(file!=juce::File()){state=Analysing;published=-1;loadSource(file,copy,start);if(state!=Error){build();state=Ready;}else if(restoreOnly)setStatus("Transferdatei fehlt oder ist unlesbar. Erneut LEARN starten.");}
        if(!features.empty()&&!writer&&state!=Analysing){const auto settings=readSettings();if(settings!=lastSettings||editRevision.load()!=builtEditRevision){state=Analysing;build();if(state!=Error)state=Ready;}}
        wait(5);
    }
    writer.reset();
}
void Engine::finishCapture(){
    writer.reset();captureFinished=false;stopRequested=false;
    if(captureFailed||captureFrames==0){state=Error;setStatus(captureFailed?"Transfer unterbrochen: Datentraeger zu langsam/voll. Neu lernen.":"Kein Audiomaterial. Song abspielen und LEARN wiederholen.");return;}
    juce::File file;{std::lock_guard<std::mutex>lock(mutex);file=source;}
    loadSource(file,false,startSeconds);if(state!=Error){build();if(state!=Error)state=Ready;}
}
void Engine::loadSource(const juce::File&file,bool copy,double start){
    setStatus("ANALYSE - vollstaendiger Transfer wird ausgewertet.");juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>reader(formats.createReaderFor(file));
    if(!reader||reader->sampleRate<8000||reader->sampleRate>192000||reader->numChannels<1||reader->numChannels>2){state=Error;features.clear();setStatus("Audio nicht lesbar: Mono/Stereo, 8-192 kHz verwenden.");return;}
    const auto frames=std::min(reader->lengthInSamples,juce::int64(reader->sampleRate*maxSeconds));sourceRate=reader->sampleRate;sourceChannels=int(reader->numChannels);sourceDuration=frames/sourceRate;sourceStart=start;features.clear();
    if(frames==0){state=Error;setStatus("Audiodatei ist leer.");return;}
    std::unique_ptr<juce::AudioFormatWriter>copyWriter;juce::File owned=file;
    if(copy){const auto id=juce::Uuid().toString().removeCharacters("-");auto directory=cacheRoot().getChildFile(id);if(!directory.createDirectory().wasOk()){state=Error;return;}owned=directory.getChildFile("source.wav");auto stream=owned.createOutputStream();juce::WavAudioFormat wav;if(stream)copyWriter.reset(wav.createWriterFor(stream.release(),sourceRate,unsigned(sourceChannels),24,{},0));if(!copyWriter){state=Error;setStatus("Import-Cache nicht beschreibbar.");return;}std::lock_guard<std::mutex>lock(mutex);cacheToken=id;startSeconds=start;manual.clear();undoStack.clear();redoStack.clear();}
    FeatureAccumulator analyser(sourceRate);bool good=true;
    for(juce::int64 offset=0;offset<frames&&!threadShouldExit();offset+=4096){int count=int(std::min<juce::int64>(4096,frames-offset));scratch.clear();good=reader->read(&scratch,0,count,offset,true,sourceChannels==2)&&good;for(int n=0;n<count;++n)analyser.push(scratch.getSample(0,n),scratch.getSample(sourceChannels==2?1:0,n));if(copyWriter)good=copyWriter->writeFromAudioSampleBuffer(scratch,0,count)&&good;}
    copyWriter.reset();analyser.finish();if(!good||threadShouldExit()){state=Error;setStatus("Audio konnte nicht vollstaendig gelesen werden.");return;}
    features=std::move(analyser.features);{std::lock_guard<std::mutex>lock(mutex);source=owned;startSeconds=start;rendered={};}
    // Owned metadata makes the timeline anchor durable alongside the raw transfer.
    juce::DynamicObject::Ptr metadata=new juce::DynamicObject;metadata->setProperty("startSeconds",sourceStart);metadata->setProperty("duration",sourceDuration);metadata->setProperty("sampleRate",sourceRate);owned.getSiblingFile("transfer.json").replaceWithText(juce::JSON::toString(juce::var(metadata.get())));
}
void Engine::build(){
    if(features.empty())return;lastSettings=readSettings();std::vector<Edit>editsCopy;juce::File input;
    {std::lock_guard<std::mutex>lock(mutex);editsCopy=manual;input=source;builtEditRevision=editRevision.load();}
    auto next=analyse(features,lastSettings,editsCopy,sourceDuration,sourceStart);next.revision=++renderRevision;
    // Publish curves before export. No full-song audio is retained in RAM.
    publish(next);{std::lock_guard<std::mutex>lock(mutex);visual=next;}
    juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatReader>reader;
    if(auto inputStream=input.createInputStream())reader.reset(wav.createReaderFor(inputStream.release(),true));
    // A recalled or duplicated plugin may share the source cache with a prior
    // instance. Unique result names never overwrite audio already used by a DAW.
    const auto output=input.getSiblingFile("GILLASSIST-"+juce::Uuid().toString().removeCharacters("-")+".wav");auto stream=output.createOutputStream();
    juce::StringPairArray metadata;metadata.set(juce::WavAudioFormat::bwavDescription,"GILLASSIST processed vocal; original timeline start "+juce::String(sourceStart,6)+" seconds");metadata.set(juce::WavAudioFormat::bwavTimeReference,juce::String(juce::int64(std::max(0.,sourceStart)*sourceRate)));
    std::unique_ptr<juce::AudioFormatWriter>out;if(stream)out.reset(wav.createWriterFor(stream.release(),sourceRate,unsigned(sourceChannels),24,metadata,0));
    if(!reader||!out){setStatus("Kurven bereit; Exportdatei konnte nicht erstellt werden.");completedRevision=renderRevision;return;}
    bool good=true;const auto frames=juce::int64(std::llround(sourceDuration*sourceRate));
    for(juce::int64 offset=0;offset<frames&&!threadShouldExit();offset+=4096){int count=int(std::min<juce::int64>(4096,frames-offset));scratch.clear();good=reader->read(&scratch,0,count,offset,true,sourceChannels==2)&&good;for(int n=0;n<count;++n){const float factor=next.at((offset+n)/sourceRate);for(int c=0;c<sourceChannels;++c)scratch.setSample(c,n,std::isfinite(scratch.getSample(c,n))?scratch.getSample(c,n)*factor:0);}good=out->writeFromAudioSampleBuffer(scratch,0,count)&&good;}
    out.reset();if(good&&!threadShouldExit()){
        std::lock_guard<std::mutex>lock(mutex);rendered=output;unclaimedRenders.push_back(output);
        while(unclaimedRenders.size()>3){auto old=unclaimedRenders.front();unclaimedRenders.erase(unclaimedRenders.begin());if(!claimedRenders.count(old.getFullPathName())&&old!=rendered&&old.getParentDirectory()==input.getParentDirectory())old.deleteFile();}
        message="BEREIT - "+juce::String(sourceDuration,1)+" s / Position "+juce::String(sourceStart,2)+" s / WAV ins Projekt ziehen.";
    }else setStatus("Kurven bereit; WAV-Export fehlgeschlagen (Datentraeger pruefen).");completedRevision=renderRevision;
}
}
