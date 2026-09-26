#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "CreativeDSP.h"
#include <mutex>
#include <map>

// One audio producer, one disk writer. The audio callback only copies to a
// preallocated SPSC ring. File creation, WAV headers and flushes live here.
class CreativeCaptureArchive final : public gill::creative::Engine::CaptureSink, private juce::Thread {
    static constexpr unsigned blockSize=256, slots=4096;
    struct Packet { int action=0,frames=0;unsigned revision=0;double rate=48000;std::array<float,blockSize*2> samples{}; };
public:
    explicit CreativeCaptureArchive(juce::String product):Thread("GILL capture writer"),name(std::move(product)),ring(std::make_unique<Packet[]>(slots)){startThread();}
    ~CreativeCaptureArchive() override {signalThreadShouldExit();stopThread(-1);}
    unsigned begin(double rate) noexcept override {
        const auto revision=serial.fetch_add(1)+1;failed=false;pending=Packet{};pending.rate=rate;pending.revision=revision;state=1;
        enqueue(1);pending.action=0;return revision;
    }
    void sample(float l,float r) noexcept override {
        if(failed.load(std::memory_order_relaxed))return;
        pending.samples[static_cast<size_t>(pending.frames)*2]=l;pending.samples[static_cast<size_t>(pending.frames)*2+1]=r;
        if(++pending.frames==static_cast<int>(blockSize)){enqueue(2);pending.frames=0;}
    }
    void finish(bool valid) noexcept override {
        if(pending.frames){enqueue(2);pending.frames=0;}
        enqueue(valid&&!failed.load()?3:4);if(failed)state=4;
    }
    int status()const noexcept{return state.load();}
    bool ready(unsigned revision)const {return sourceFile(revision).existsAsFile();}
    juce::File sourceFile(unsigned revision)const {std::lock_guard<std::mutex> lock(fileMutex);const auto it=files.find(revision);return it==files.end()?juce::File{}:it->second;}
    // State serialization is a non-audio operation. A completed DSP plan may
    // precede the worker's final WAV header by a few milliseconds; don't save a
    // map without its source merely because the user saved immediately at STOP.
    juce::File sourceFileForState(unsigned revision){
        if(revision==0)return {};
        notify();const auto deadline=juce::Time::getMillisecondCounterHiRes()+5000;
        for(;;){auto file=sourceFile(revision);if(file!=juce::File{}||failedRevision.load()==revision||!isThreadRunning())return file;
            if(juce::Time::getMillisecondCounterHiRes()>=deadline)return {};
            juce::Thread::sleep(1);
        }
    }
    juce::String error()const {std::lock_guard<std::mutex> lock(fileMutex);return failure;}
    unsigned restore(const juce::File& file){const auto revision=serial.fetch_add(1)+1;std::lock_guard<std::mutex>lock(fileMutex);files[revision]=file;failure.clear();state=file.existsAsFile()?2:4;if(state==4)failure="CAPTURE FILE MISSING - LEARN AGAIN";return revision;}
    static juce::File folder(const juce::String& product){const auto root=juce::SystemStats::getEnvironmentVariable("GILL_CREATIVE_AUDIO_ROOT",{});return (root.isNotEmpty()?juce::File(root):juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Jill Plugins").getChildFile("Audio")).getChildFile(product);}
private:
    void enqueue(int action) noexcept {
        const auto w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
        if(w-r>=slots){failed=true;failedRevision=pending.revision;state=4;return;}
        pending.action=action;ring[w%slots]=pending;write.store(w+1,std::memory_order_release);
    }
    void fail(const juce::String& why,unsigned revision){failedRevision=revision;std::lock_guard<std::mutex>lock(fileMutex);if(serial.load()==revision){failure=why;state=4;}}
    void run() override {
        std::unique_ptr<juce::AudioFormatWriter>writer;juce::File file;unsigned active=0;bool good=false;juce::AudioBuffer<float>buffer(2,blockSize);
        while(!threadShouldExit()||read.load()!=write.load()){
            const auto r=read.load(std::memory_order_relaxed);if(r==write.load(std::memory_order_acquire)){wait(4);continue;}
            const auto& packet=ring[r%slots];
            if(packet.action==1){writer.reset();active=packet.revision;good=false;auto dir=folder(name).getChildFile("Sources");
                if(dir.createDirectory().wasOk()) { file=dir.getChildFile("Capture-"+juce::Uuid().toString()+".wav");auto stream=file.createOutputStream();juce::WavAudioFormat wav;
                    if(stream)writer.reset(wav.createWriterFor(stream.release(),packet.rate,2,32,{},0));good=writer!=nullptr; }
                if(!good)fail("CAPTURE WRITE FAILED - CHECK FREE SPACE",active);
            }else if(packet.revision==active&&packet.action==2&&good&&writer){
                for(int n=0;n<packet.frames;++n){buffer.setSample(0,n,packet.samples[static_cast<size_t>(n)*2]);buffer.setSample(1,n,packet.samples[static_cast<size_t>(n)*2+1]);}
                if(!writer->writeFromAudioSampleBuffer(buffer,0,packet.frames)){good=false;fail("CAPTURE DISK FULL / WRITE FAILED",active);}
            }else if(packet.revision==active&&(packet.action==3||packet.action==4)){
                if(writer&&!writer->flush())good=false;writer.reset();
                if(packet.action==3&&good&&failedRevision.load()!=active){std::lock_guard<std::mutex>lock(fileMutex);files[active]=file;failure.clear();if(serial.load()==active)state=2;}
                else fail("CAPTURE INCOMPLETE - LEARN AGAIN",active);
            }
            read.store(r+1,std::memory_order_release);
        }
        writer.reset();
    }
    juce::String name;std::unique_ptr<Packet[]>ring;Packet pending;std::atomic<unsigned>write{0},read{0},serial{0},failedRevision{0};std::atomic<int>state{0};std::atomic<bool>failed{false};
    mutable std::mutex fileMutex;std::map<unsigned,juce::File> files;juce::String failure;
};
