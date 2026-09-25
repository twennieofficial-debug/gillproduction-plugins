#include "../Source/PluginProcessor.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <set>
#include <vector>

namespace {
thread_local bool watchAudio=false;
std::atomic<size_t>audioAllocations{0};
int checks=0,failures=0,audioCalls=0;
std::set<juce::String>ownedFiles;
constexpr double pi=3.14159265358979323846;
void check(bool good,const char*description,double value=0){++checks;if(!good)++failures;std::printf("%s %s %.9g\n",good?"PASS":"FAIL",description,value);std::fflush(stdout);}
struct Head final:juce::AudioPlayHead {
    juce::int64 samples=0;bool playing=true;
    juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setTimeInSamples(samples);p.setIsPlaying(playing);return p;}
};
bool setup(GillNextProcessor&p,Head&head,double fs,bool side=true){auto layout=p.getBusesLayout();layout.inputBuses.set(0,juce::AudioChannelSet::stereo());layout.outputBuses.set(0,juce::AudioChannelSet::stereo());layout.inputBuses.set(1,side?juce::AudioChannelSet::stereo():juce::AudioChannelSet::disabled());const bool ok=p.setBusesLayout(layout);p.setPlayHead(&head);p.setNonRealtime(false);p.prepareToPlay(fs,128);return ok;}
void process(GillNextProcessor&p,juce::AudioBuffer<float>&b){juce::MidiBuffer midi;watchAudio=true;p.processBlock(b,midi);watchAudio=false;++audioCalls;}
void constant(juce::AudioBuffer<float>&b,float left=.31f,float right=-.19f){b.clear();for(int n=0;n<b.getNumSamples();++n){b.setSample(0,n,left);b.setSample(1,n,right);}}
float takeSample(int index,double fs,bool dub,int channel){const double t=index/fs;double envelope=.045;for(double onset:{.04,.30,.56,.86}){const double time=t-onset-(dub?.04:0);if(time>=0&&time<.20)envelope+=(1-std::exp(-time/.004))*std::exp(-time/.06);}
    const double hz=dub?163:150;const double wave=std::sin(2*pi*hz*t)+.35*std::sin(4*pi*hz*t);const float result=float(.012+.19*envelope*wave);return channel?-.5f*result:result;}
void captureTake(GillNextProcessor&p,Head&head,int lane,double fs,double anchor,int frames){juce::AudioBuffer<float>b(4,128);p.capture(lane);for(int at=0;at<frames;at+=128){b.setSize(4,std::min(128,frames-at),false,false,true);head.samples=juce::int64(std::llround(anchor*fs))+at;head.playing=true;for(int n=0;n<b.getNumSamples();++n)for(int c=0;c<2;++c){b.setSample(c,n,lane==1?takeSample(at+n,fs,true,c):.91f);b.setSample(c+2,n,lane==0?takeSample(at+n,fs,false,c):.73f);}process(p,b);}
    p.capture(lane);b.setSize(4,128,false,false,true);b.clear();head.samples=juce::int64(std::llround(anchor*fs))+frames;process(p,b);}
bool waitState(GillNextProcessor&p,int desired,int milliseconds=15000){for(int elapsed=0;elapsed<milliseconds;elapsed+=10){if(p.alignState.load()==desired)return true;if(desired==3&&p.alignState.load()==4)return false;juce::Thread::sleep(10);}return false;}
juce::ValueTree stateTree(const juce::MemoryBlock&state){if(auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize())))return juce::ValueTree::fromXml(*xml);return {};}
juce::MemoryBlock saveState(GillNextProcessor&p,bool own=false){juce::MemoryBlock state;p.getStateInformation(state);if(own){const auto path=stateTree(state).getProperty("alignedAudio").toString();if(path.isNotEmpty())ownedFiles.insert(path);}return state;}
struct Wave {juce::AudioBuffer<float>audio;double fs=0;bool ok=false;};
Wave readWave(const juce::String&path){Wave w;juce::AudioFormatManager manager;manager.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>reader(manager.createReaderFor(juce::File(path)));if(!reader)return w;w.fs=reader->sampleRate;w.audio.setSize(int(reader->numChannels),int(reader->lengthInSamples));w.ok=reader->read(&w.audio,0,w.audio.getNumSamples(),0,true,true);return w;}
void warmPreview(GillNextProcessor&p,Head&head){juce::AudioBuffer<float>b(4,128);head.playing=true;for(int k=0;k<12;++k){head.samples=k*128;constant(b);process(p,b);}}
double fullPreviewError(GillNextProcessor&p,Head&head,const Wave&w,double rate,double anchor,int sourceStart){juce::AudioBuffer<float>b(4,256);constant(b);const int factor=int(std::lround(rate/w.fs));head.samples=juce::int64(std::llround(anchor*rate))+sourceStart*factor;head.playing=true;process(p,b);double error=0;for(int n=0;n<b.getNumSamples();n+=factor)for(int c=0;c<2;++c){const float expected=w.audio.getSample(std::min(c,w.audio.getNumChannels()-1),sourceStart+n/factor);error=std::max(error,std::abs(double(b.getSample(c,n)-expected)));}return error;}
void testTakeEdges(GillNextProcessor&p,Head&head,const Wave&w,double anchor){constexpr int block=128;const int length=w.audio.getNumSamples();const auto hostAnchor=juce::int64(std::llround(anchor*w.fs));const int fade=int(w.fs*.005);double e=0;juce::AudioBuffer<float>b(4,block);
    for(int start:{-64,length-64}){constant(b);head.samples=hostAnchor+start;head.playing=true;process(p,b);for(int n=0;n<block;++n)for(int c=0;c<2;++c){const int index=start+n;const float original=c?-.19f:.31f;double expected=original;if(index>=0&&index<length){const double weight=std::clamp(double(std::min(index,length-1-index))/fade,0.,1.);expected+=weight*(w.audio.getSample(c,index)-original);}e=std::max(e,std::abs(b.getSample(c,n)-expected));}}
    check(e<1e-7,"Preview uses aligned 5ms take-edge fades and original outside take",e);
}
void testStop(GillNextProcessor&p,Head&head,double anchor,double rate){juce::AudioBuffer<float>b(4,128);head.samples=juce::int64(std::llround(anchor*rate))+10000;head.playing=false;double e=0;for(int k=0;k<24;++k){constant(b);process(p,b);if(k>8)for(int n=0;n<128;++n){e=std::max(e,std::abs(double(b.getSample(0,n)-.31f)));e=std::max(e,std::abs(double(b.getSample(1,n)+.19f)));}}check(e==0,"Stopped transport does not repeat a frozen preview snippet after fade-out",e);}
}
void*operator new(size_t n){if(watchAudio)++audioAllocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](size_t n){return::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}

int main(){juce::ScopedJuceInitialiser_GUI gui;constexpr double rate=48000,anchor=8.125;constexpr int frames=57600;juce::MemoryBlock alignedState,undoState;Wave wave;
    {
        auto p=std::make_unique<GillNextProcessor>(NextKind::Align);Head head;check(setup(*p,head,rate),"ALIGN native main+external-guide layout prepares");p->setValue("tightness",100,false);p->setValue("maxshift",120,false);
        captureTake(*p,head,0,rate,anchor,frames);captureTake(*p,head,1,rate,anchor,frames);
        check(p->captureState==0&&std::abs(p->guideSeconds-1.2f)<1e-6&&std::abs(p->doubleSeconds-1.2f)<1e-6,"Real GUIDE sidechain and DOUBLE main captures stop at exact frame counts");
        const auto guideWave=p->waveform(0);const double guidePeak=*std::max_element(guideWave.begin(),guideWave.end());check(guidePeak>.03&&guidePeak<.4,"Guide capture used external sidechain rather than loud main input",guidePeak);
        p->alignTakes();juce::AudioBuffer<float>b(4,128);b.clear();process(*p,b);const bool ready=waitState(*p,3);check(ready,"Real offline wrapper analysis completes",p->alignConfidence);if(!ready)std::printf("STATUS %s\n",p->statusText().toRawUTF8());
        if(ready){p->setValue("preview",1,false);alignedState=saveState(*p,true);const auto tree=stateTree(alignedState);const auto path=tree.getProperty("alignedAudio").toString();wave=readWave(path);check(wave.ok&&wave.fs==rate&&wave.audio.getNumSamples()==frames&&wave.audio.getNumChannels()==2,"Aligned output is really persisted as readable stereo float WAV",wave.audio.getNumSamples());check(std::abs(double(tree.getProperty("anchorSeconds"))-anchor)<1e-12&&bool(tree.getProperty("hostAnchor")),"State stores the DOUBLE capture anchor in seconds",double(tree.getProperty("anchorSeconds")));check(alignedState.getSize()<20000,"Saved state contains file reference rather than large audio blob",alignedState.getSize());
            if(wave.ok){warmPreview(*p,head);check(fullPreviewError(*p,head,wave,rate,anchor,5000)<1e-7,"Preview reads the persisted aligned samples on their original timeline");testTakeEdges(*p,head,wave,anchor);testStop(*p,head,anchor,rate);}
            p->undoAlignment();const bool undone=waitState(*p,1);juce::Thread::sleep(20);undoState=saveState(*p);const auto undoTree=stateTree(undoState);check(undone&&undoTree.getProperty("alignedAudio").toString().isEmpty(),"Undo clears the persisted aligned-audio reference");check(float(undoTree.getChildWithProperty("id","preview").getProperty("value"))==0,"Undo state reloads with original playback selected");
            head.playing=true;head.samples=juce::int64(anchor*rate)+10000;constant(b);process(*p,b);double e=0;for(int n=0;n<128;++n)e=std::max(e,std::abs(double(b.getSample(0,n)-.31f)));check(e==0,"Undo actually restores live original audio",e);
            // Undo deliberately leaves a redo result in this same instance.
            // Loading an original-only state must clear that previous result,
            // not let another Undo resurrect audio from the former state.
            p->setStateInformation(undoState.getData(),int(undoState.getSize()));
            check(p->alignState==0&&p->value("preview")==0,"Same-instance original-only state recall clears READY and Preview");
            p->undoAlignment();const bool emptyUndo=waitState(*p,1);juce::Thread::sleep(20);p->setValue("preview",1,false);warmPreview(*p,head);
            head.samples=juce::int64(anchor*rate)+10000;constant(b);process(*p,b);e=0;for(int n=0;n<128;++n){e=std::max(e,std::abs(double(b.getSample(0,n)-.31f)));e=std::max(e,std::abs(double(b.getSample(1,n)+.19f)));}
            const auto repeatedTree=stateTree(saveState(*p,true));check(emptyUndo&&p->alignState!=3&&repeatedTree.getProperty("alignedAudio").toString().isEmpty()&&e==0,"Undo after same-instance original state cannot resurrect previous aligned audio",e);
        }
        p->setPlayHead(nullptr);
    }
    if(alignedState.getSize()>0&&wave.ok){
        auto restored=std::make_unique<GillNextProcessor>(NextKind::Align);Head head;restored->setStateInformation(alignedState.getData(),int(alignedState.getSize()));check(setup(*restored,head,96000),"Fresh 96k instance accepts state before prepare");const bool loaded=waitState(*restored,3);check(loaded&&std::abs(restored->doubleSeconds-1.2f)<1e-5,"Persisted WAV is loaded and resampled at the prepared 96k rate",restored->doubleSeconds);if(loaded){check(restored->value("preview")>.5f,"Saved preview selection is restored");warmPreview(*restored,head);check(fullPreviewError(*restored,head,wave,96000,anchor,5000)<2e-6,"96k preview uses seconds anchor and independently verified WAV samples");testStop(*restored,head,anchor,96000);}restored->setPlayHead(nullptr);
    }
    if(undoState.getSize()>0){auto restored=std::make_unique<GillNextProcessor>(NextKind::Align);Head head;restored->setStateInformation(undoState.getData(),int(undoState.getSize()));setup(*restored,head,96000);juce::Thread::sleep(150);head.samples=juce::int64(anchor*96000)+10000;juce::AudioBuffer<float>b(4,128);double e=0;for(int k=0;k<8;++k){constant(b);process(*restored,b);for(int n=0;n<128;++n)e=std::max(e,std::abs(double(b.getSample(0,n)-.31f)));}check(restored->value("preview")==0&&restored->alignState!=3&&e==0,"Fresh instance reloaded after Undo remains original with no ghost result",e);restored->setPlayHead(nullptr);}
    check(audioAllocations.load()==0,"Capture and active Preview audio callbacks allocate no C++ heap memory",double(audioAllocations.load()));
    // Only UUID files whose exact paths were returned by this test's own save
    // are removed. Never enumerate or delete other user/session take files.
    const auto directory=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Jill Plugins/Align Takes");for(const auto&path:ownedFiles){const juce::File file(path);if(file.getParentDirectory()==directory&&file.getFileName().startsWith("GILLALIGN-"))file.deleteFile();}
    std::printf("RESULT %d checks %d failures audioCalls%d audioAllocations%zu\n",checks,failures,audioCalls,audioAllocations.load());std::ofstream report("align-wrapper-report.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"audio_calls\":"<<audioCalls<<",\"audio_allocations\":"<<audioAllocations.load()<<",\"passed\":"<<(failures?"false":"true")<<"}";return failures?1:0;
}
