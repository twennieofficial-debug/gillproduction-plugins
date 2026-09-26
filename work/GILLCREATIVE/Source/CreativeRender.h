#pragma once
#include "CaptureArchive.h"

class CreativeRender final : private juce::Thread {
public:
    CreativeRender():Thread("GILL effect render"){}
    ~CreativeRender()override{signalThreadShouldExit();stopThread(-1);}
    bool start(const juce::File& source,const juce::String& product,gill::creative::Kind kind,const gill::creative::Plan& plan,gill::creative::Controls controls,bool effectsOnly){
        if(isThreadRunning()||!source.existsAsFile()||!gill::creative::Engine::validPlan(plan))return false;
        task={source,product,kind,plan,controls,effectsOnly};progress=0;status=1;{std::lock_guard<std::mutex>lock(resultMutex);result={};failure.clear();}startThread();return true;
    }
    bool busy()const noexcept{return status==1||isThreadRunning();}
    juce::File file()const {std::lock_guard<std::mutex>lock(resultMutex);return result;}
    juce::String error()const {std::lock_guard<std::mutex>lock(resultMutex);return failure;}
    std::atomic<float>progress{0};std::atomic<int>status{0};
private:
    struct Task{juce::File source;juce::String product;gill::creative::Kind kind=gill::creative::Kind::Phrase;gill::creative::Plan plan;gill::creative::Controls controls;bool effectsOnly=true;}task;
    void fail(const juce::String& message){std::lock_guard<std::mutex>lock(resultMutex);failure=message;status=3;}
    void run()override {
        try{render();}catch(const std::exception&){fail("RENDER FAILED - MEMORY OR FILE ERROR");}
    }
    void render(){
        juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>source(formats.createReaderFor(task.source));
        if(!source||source->sampleRate<8000||source->sampleRate>384000||source->lengthInSamples/source->sampleRate>300.05){fail("CAPTURE FILE INVALID");return;}
        auto plan=task.plan;const double rate=source->sampleRate;gill::creative::Engine offline(task.kind);offline.prepare(rate);
        if(task.kind==gill::creative::Kind::Reply){plan.capture=offline.importSource(source->lengthInSamples,rate,[&](float*l,float*r,std::int64_t start,int n){float*channels[]{l,r};return source->read(channels,2,start,n);});if(plan.capture<0){fail("CAPTURE IMPORT FAILED");return;}plan.captureEpoch=offline.capture(plan.capture)->epoch.load();}
        offline.postPlan(plan,true);auto controls=task.controls;controls.pro=true;controls.bypass=false;if(task.effectsOnly){controls.dry=0;controls.effectsOnly=true;}
        const auto folder=CreativeCaptureArchive::folder(task.product).getChildFile("Exports");if(folder.createDirectory().failed()){fail("EXPORT FOLDER NOT WRITABLE");return;}
        const auto id=juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+"-"+juce::Uuid().toString().substring(0,8);
        const auto temporary=folder.getChildFile(".render-"+id+".wav");const auto finalFile=folder.getChildFile(task.product+(task.effectsOnly?"-FX-":"-MIX-")+id+".wav");
        juce::WavAudioFormat wav;auto stream=temporary.createOutputStream();std::unique_ptr<juce::AudioFormatWriter>writer(stream?wav.createWriterFor(stream.release(),rate,2,32,{},0):nullptr);
        if(!writer){fail("EXPORT FILE CANNOT BE CREATED");return;}
        const std::int64_t total=source->lengthInSamples+static_cast<std::int64_t>(rate*std::max(12.f,controls.length*2+2));juce::AudioBuffer<float>buffer(2,1024);float peak=0;
        for(std::int64_t offset=0;offset<total;offset+=1024){if(threadShouldExit()){writer.reset();temporary.deleteFile();status=0;return;}const int n=static_cast<int>(std::min<std::int64_t>(1024,total-offset));buffer.clear();
            const int available=static_cast<int>(std::clamp<std::int64_t>(source->lengthInSamples-offset,0,n));if(available>0&&!source->read(&buffer,0,available,offset,true,true)){writer.reset();fail("CAPTURE READ FAILED");return;}
            gill::creative::Transport transport;transport.hasPPQ=true;transport.ppq=plan.originPPQ+offset/rate*plan.bpm/60.;transport.bpm=plan.bpm;transport.playing=true;transport.hasSeconds=plan.timeAnchor;transport.seconds=plan.originSeconds+offset/rate;
            offline.process(buffer.getWritePointer(0),buffer.getWritePointer(1),nullptr,nullptr,n,controls,transport);
            for(int c=0;c<2;++c)for(int i=0;i<n;++i){const float fade=std::min(1.f,static_cast<float>((total-offset-i)/(rate*.02)));buffer.setSample(c,i,buffer.getSample(c,i)*fade);peak=std::max(peak,std::abs(buffer.getSample(c,i)));}
            if(!writer->writeFromAudioSampleBuffer(buffer,0,n)){writer.reset();fail("EXPORT DISK FULL / WRITE FAILED");return;}progress=static_cast<float>(offset/static_cast<double>(total)*.8);
        }
        const bool flushed=writer->flush();writer.reset();if(!flushed){fail("EXPORT FLUSH FAILED");return;}
        // Attenuate only when needed. Quiet throws keep the chosen mix level.
        const float gain=peak>.98f?.98f/peak:1.f;std::unique_ptr<juce::AudioFormatReader>rendered(formats.createReaderFor(temporary));stream=finalFile.createOutputStream();writer.reset(stream?wav.createWriterFor(stream.release(),rate,2,32,{},0):nullptr);
        if(!rendered||!writer){fail("FINAL EXPORT CANNOT BE CREATED");return;}
        for(std::int64_t offset=0;offset<total;offset+=1024){if(threadShouldExit()){writer.reset();finalFile.deleteFile();temporary.deleteFile();status=0;return;}const int n=static_cast<int>(std::min<std::int64_t>(1024,total-offset));if(!rendered->read(&buffer,0,n,offset,true,true)){fail("RENDER READ FAILED");return;}buffer.applyGain(0,n,gain);if(!writer->writeFromAudioSampleBuffer(buffer,0,n)){fail("FINAL EXPORT WRITE FAILED");return;}progress=.8f+static_cast<float>(offset/static_cast<double>(total)*.2);}
        const bool finalFlushed=writer->flush();writer.reset();rendered.reset();temporary.deleteFile();if(!finalFlushed){fail("FINAL EXPORT FLUSH FAILED");return;}
        const auto note="GILLPRODUCTION / "+task.product+"\nCaptured source: "+task.source.getFullPathName()+"\nPlace clip at the capture start: "+juce::String(plan.originSeconds,3)+" seconds / "+juce::String(plan.originPPQ,3)+" PPQ beats.\nCaptured duration: "+juce::String(plan.durationSeconds,3)+" seconds.\nPRO render, 32-bit float stereo WAV, effect tail included.\nExport gain: "+juce::String(juce::Decibels::gainToDecibels(gain),2)+" dB (attenuation only if needed).\n";
        finalFile.withFileExtension(".txt").replaceWithText(note);{std::lock_guard<std::mutex>lock(resultMutex);result=finalFile;failure.clear();}progress=1;status=2;
    }
    mutable std::mutex resultMutex;juce::File result;juce::String failure;
};
