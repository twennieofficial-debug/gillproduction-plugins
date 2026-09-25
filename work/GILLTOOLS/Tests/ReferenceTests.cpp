#include "../Source/ReferenceEngine.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <thread>

namespace {
thread_local bool audioScope=false;
std::atomic<int> audioAllocations{0};
int checks=0, failures=0;
void check(bool result,const char* name){++checks;if(!result){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
using Engine=gill::tools::ReferenceEngine;
constexpr double pi=3.14159265358979323846;
void process(Engine& e,juce::AudioBuffer<float>& b,const Engine::Transport& t,const Engine::Parameters& p){audioScope=true;e.process(b,t,p);audioScope=false;}
float tone(std::int64_t i,double rate,float amplitude){return amplitude*static_cast<float>(std::sin(2*pi*997*i/rate));}
void makeFile(const juce::File& file,double rate,float amplitude,int format=0,double frequency=997){
    juce::WavAudioFormat wav;juce::AiffAudioFormat aiff;juce::FlacAudioFormat flac;
    juce::AudioFormat* f=format==0?static_cast<juce::AudioFormat*>(&wav):format==1?static_cast<juce::AudioFormat*>(&aiff):static_cast<juce::AudioFormat*>(&flac);
    auto stream=file.createOutputStream();check(stream&&stream->openedOk(),"fixture output stream");if(!stream)return;
    auto writer=std::unique_ptr<juce::AudioFormatWriter>(f->createWriterFor(stream.get(),rate,2,24,{},0));
    check(writer!=nullptr,"fixture format writer");if(!writer)return;stream.release();
    juce::AudioBuffer<float> b(2,static_cast<int>(rate*4.5));
    for(int i=0;i<b.getNumSamples();++i){const auto value=amplitude*static_cast<float>(std::sin(2*pi*frequency*i/rate));b.setSample(0,i,value);b.setSample(1,i,value);}
    check(writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()),"fixture write");
}
bool ready(Engine& e){for(int i=0;i<500;++i){if(e.snapshot().loaded){juce::Thread::sleep(150);return true;}juce::Thread::sleep(10);}return false;}
double renderRms(Engine& engine,double rate,Engine::Parameters p,std::int64_t start,int samples,float input,bool paced=false){
    juce::AudioBuffer<float> b(2,256);double energy=0;int measured=0;bool finite=true;
    for(int offset=0;offset<samples;offset+=256){const int n=std::min(256,samples-offset);b.setSize(2,n,false,false,true);
        for(int i=0;i<n;++i){const float v=tone(start+offset+i,rate,input);b.setSample(0,i,v);b.setSample(1,i,v);}
        Engine::Transport t{true,true,false,false,start+offset};process(engine,b,t,p);
        for(int i=0;i<n;++i){finite=finite&&std::isfinite(b.getSample(0,i))&&std::isfinite(b.getSample(1,i));if(offset+i>samples/2){energy+=b.getSample(0,i)*b.getSample(0,i);++measured;}}
        if(paced&&offset%4096==0)juce::Thread::sleep(12);
    }
    check(finite,"finite rendered audio");return measured?std::sqrt(energy/measured):0;
}
}
void* operator new(std::size_t size){if(audioScope)++audioAllocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

int main(){
    std::cout<<std::unitbuf;juce::ScopedJuceInitialiser_GUI init;
    const auto fixtures=juce::File::getCurrentWorkingDirectory().getChildFile("reference-fixtures");fixtures.createDirectory();
    const auto wav=fixtures.getChildFile("own-reference.wav"),aiff=fixtures.getChildFile("own-reference.aiff"),flac=fixtures.getChildFile("own-reference.flac");
    makeFile(wav,48000,.25f);makeFile(aiff,44100,.25f,1);makeFile(flac,96000,.25f,2);
    {
        Engine e;Engine::Parameters p;p.match=false;
        for(double rate:{8000.,44100.,48000.,96000.,192000.})for(int channels:{1,2}){
            e.prepare(rate,2048);check(Engine::latencySamples==0,"zero added latency");
            for(int block:{1,16,64,127,256,512,1024,2048}){juce::AudioBuffer<float> b(channels,block),copy(channels,block);
                for(int c=0;c<channels;++c)for(int i=0;i<block;++i)b.setSample(c,i,std::sin(i*.17f+c)*.7f);copy.makeCopyOf(b);
                process(e,b,{true,true,false,false,0},p);bool exact=true;for(int c=0;c<channels;++c)exact=exact&&std::memcmp(b.getReadPointer(c),copy.getReadPointer(c),static_cast<std::size_t>(block)*sizeof(float))==0;
                check(exact,"neutral MIX bit-exact at every rate/block/channel");}
        }
    }
    for(const auto& file:{wav,aiff,flac}){
        Engine e;e.prepare(48000,256);e.requestLoad(0,file);check(ready(e),"WAV AIFF FLAC async load");
        Engine::Parameters p;p.match=false;
        const auto own=renderRms(e,48000,p,0,24000,.1f);check(std::abs(own-.1/std::sqrt(2.0))<.0002,"LOAD remains on MIX");
        e.setReferenceEnabled(false);e.setReferenceEnabled(true);
        const auto ref=renderRms(e,48000,p,0,24000,0);check(std::abs(ref-.25/std::sqrt(2.0))<.003,"resampled reference audible at expected level");
        juce::AudioBuffer<float> b(2,512);for(int i=0;i<512;++i){b.setSample(0,i,.123f);b.setSample(1,i,-.234f);}
        process(e,b,{true,true,false,true,24000},p);check(b.getSample(0,0)==.123f&&b.getSample(1,0)==-.234f,"offline reference disabled from first sample");
        auto state=e.getState();e.setState(state);e.setReferenceEnabled(true);const auto recalled=renderRms(e,48000,p,0,24000,.1f);
        check(std::abs(recalled-.1/std::sqrt(2.0))<.0002,"state recall forces MIX even with stale REF request");
        check(state.getNumChildren()==3&&state.getChild(0).getProperty("fingerprint").toString().isNotEmpty(),"three path slots and fingerprint persisted");
    }
    {
        Engine e;e.prepare(48000,256);e.requestLoad(0,wav);check(ready(e),"match reference ready");
        Engine::Parameters p;p.match=true;renderRms(e,48000,p,0,static_cast<int>(48000*3.6),.5f);
        const auto s=e.snapshot();check(s.loudnessValid,"three active seconds produce frozen weighted RMS match");
        check(std::abs(s.mixGainDb+6.0206)<.1&&std::abs(s.matchGainDb)<.001,"louder MIX attenuated by six dB, reference not boosted");
        const float frozen=s.rmsMix;renderRms(e,48000,p,0,12000,.05f);check(std::abs(e.snapshot().rmsMix-frozen)<1e-7,"match profile freezes instead of following source level");
        e.setLoop(.5,1.5);check(ready(e),"loop reload remains bounded");check(std::abs(e.snapshot().loopStartSeconds-.5)<1e-9,"loop start retained");
        auto state=e.getState();auto malformed=state.createCopy();malformed.getChild(0).setProperty("start","not-a-number",nullptr);e.setState(malformed);
        check(std::abs(static_cast<double>(e.getState().getChild(0).getProperty("start"))-.5)<1e-9,"malformed loop rejected atomically");
        malformed=state.createCopy();malformed.getChild(1).setProperty("index",0,nullptr);e.setState(malformed);check(e.getState().getChild(1).getProperty("index")==juce::var(1),"duplicate slot state rejected");
        for(const auto field:{"schema","slot"}){malformed=state.createCopy();malformed.setProperty(field,"1junk",nullptr);malformed.getChild(0).setProperty("start",.25,nullptr);e.setState(malformed);
            check(static_cast<double>(e.getState().getChild(0).getProperty("start"))==.5,"malformed integer state rejected without partial changes");}
        malformed=state.createCopy();malformed.getChild(0).setProperty("index",.5,nullptr);malformed.getChild(0).setProperty("start",.25,nullptr);e.setState(malformed);
        check(static_cast<double>(e.getState().getChild(0).getProperty("start"))==.5,"fractional slot index rejected");
        e.requestLoad(1,fixtures.getChildFile("missing.wav"));juce::Thread::sleep(100);check(e.snapshot().status=="FILE MISSING","missing reference has explicit status");
        e.setReferenceEnabled(false);e.setReferenceEnabled(true);check(renderRms(e,48000,p,0,12000,.1f)>0.06,"missing reference safely plays MIX");
    }
    {
        Engine e;e.prepare(48000,256);e.requestLoad(0,wav);check(ready(e),"transport fixture ready");e.setReferenceEnabled(false);e.setReferenceEnabled(true);
        Engine::Parameters p;p.match=false;renderRms(e,48000,p,10000000,1000,.1f);check(e.snapshot().underruns>0,"uncached seek is explicit underflow");
        juce::Thread::sleep(250);const auto settled=renderRms(e,48000,p,10001000,24000,0);check(settled>.1,"worker recovers at requested transport position");
        e.clearSlot(0);const auto dry=renderRms(e,48000,p,0,24000,.1f);check(std::abs(dry-.1/std::sqrt(2.0))<.0003,"cleared slot fades back to own MIX");
        juce::AudioBuffer<float> b(2,256);b.clear();b.setSample(0,0,std::numeric_limits<float>::quiet_NaN());b.setSample(1,1,std::numeric_limits<float>::infinity());process(e,b,{true,true,false,false,0},p);
        check(std::isfinite(b.getSample(0,0))&&std::isfinite(b.getSample(1,1)),"non-finite input contained");
    }
    for(double rate:{44100.,48000.,96000.,192000.}){
        Engine e;e.prepare(rate,256);e.requestLoad(0,wav);check(ready(e),"cross-rate reference ready");
        Engine::Parameters p;p.match=false;e.setReferenceEnabled(false);e.setReferenceEnabled(true);
        const auto measured=renderRms(e,rate,p,0,static_cast<int>(rate*.25),0);
        check(std::abs(measured-.25/std::sqrt(2.0))<.003,"44.1/48/96/192k windowed-sinc playback amplitude");
    }
    for(float difference:{-12.f,-6.f,-1.f,1.f,6.f,12.f}){
        Engine e;e.prepare(48000,256);e.requestLoad(0,wav);check(ready(e),"level-match fixture ready");
        const float level=.25f*std::pow(10.0f,difference/20);Engine::Parameters p;
        renderRms(e,48000,p,0,172800,level);const auto snap=e.snapshot();
        check(snap.loudnessValid,"level match has sufficient active evidence");
        check(std::abs((snap.mixGainDb-snap.matchGainDb)+difference)<.1,"plus/minus1/6/12dB match within0.1dB");
        check(snap.mixGainDb<=.0001f&&snap.matchGainDb<=.0001f,"automatic matching never boosts either source");
    }
    {
        const auto silent=fixtures.getChildFile("silence.wav"),ultrasonic=fixtures.getChildFile("ultrasonic.wav");
        makeFile(silent,48000,0);makeFile(ultrasonic,96000,.25f,0,30000);
        Engine e;e.prepare(48000,256);e.requestLoad(0,silent);check(ready(e),"silent reference loads without inventing loudness");
        Engine::Parameters p;renderRms(e,48000,p,0,172800,.25f);
        check(!e.snapshot().loudnessValid&&std::abs(e.snapshot().mixGainDb)<.001,"silent REF never produces gain correction");
        e.requestLoad(0,wav);check(ready(e),"silence gate active MIX fixture ready");renderRms(e,48000,p,0,172800,0);
        check(!e.snapshot().loudnessValid&&std::abs(e.snapshot().matchGainDb)<.001,"silent MIX never produces fabricated match");
        renderRms(e,48000,p,0,172800,.5f);check(e.snapshot().loudnessValid,"MATCH accepts actual later active evidence");
        p.match=false;renderRms(e,48000,p,0,256,.5f);p.match=true;renderRms(e,48000,p,0,12000,.25f);
        check(!e.snapshot().loudnessValid,"MATCH toggle explicitly resets frozen MIX measurement");
        e.requestLoad(0,ultrasonic);check(ready(e),"ultrasonic resampling fixture ready");p.match=false;e.setReferenceEnabled(false);e.setReferenceEnabled(true);
        check(renderRms(e,48000,p,0,24000,0)<.00025,"downsampling rejects out-of-band30k tone instead of aliasing it");
    }
    {
        Engine e;e.prepare(48000,256);Engine::Parameters p;p.match=false;juce::AudioBuffer<float> b(2,256);
        for(int view:{0,1,2}){p.channelView=view;for(int i=0;i<256;++i){b.setSample(0,i,.2f);b.setSample(1,i,-.2f);}process(e,b,{true,true,false,false,0},p);
            check(view==1?b.getSample(0,100)==0:b.getSample(0,100)==.2f,"stereo mid side monitoring");}
        p.channelView=0;p.mono=true;for(int i=0;i<256;++i){b.setSample(0,i,.2f);b.setSample(1,i,-.2f);}process(e,b,{true,true,false,false,0},p);
        check(b.getSample(0,100)==0&&b.getSample(1,100)==0,"mono cancels antiphase without boosting");
        p.mono=false;
        for(int band:{1,2,3}){p.listenBand=band;const auto rms=renderRms(e,48000,p,0,24000,.2f);check(rms>=0&&rms<.2,"all listening filters stable and bounded");}
    }
    {
        Engine e;e.prepare(48000,256);e.requestLoad(0,wav);check(ready(e),"content identity ready");auto state=e.getState();
        makeFile(wav,48000,.2f);e.setState(state);for(int i=0;i<300&&e.snapshot().status!="FILE CHANGED - RELOAD";++i)juce::Thread::sleep(10);
        check(e.snapshot().status=="FILE CHANGED - RELOAD","changed file content requires explicit reload");
        e.requestLoad(0,wav);check(ready(e),"explicit reload accepts changed content");
        std::atomic<bool> finished{false};std::thread control([&]{for(int i=0;i<20;++i){e.requestLoad(i%3,wav);e.setLoop(.1,.8);auto saved=e.getState();e.setState(saved);juce::Thread::sleep(4);}finished.store(true);});
        juce::AudioBuffer<float> b(2,127);Engine::Parameters p;p.match=false;std::int64_t position=0;bool valid=true;int blocks=0;
        while(!finished.load()||blocks<1000){b.clear();e.setReferenceEnabled(false);e.setReferenceEnabled(true);process(e,b,{true,true,blocks%31==0,false,position},p);
            for(int c=0;c<2;++c)for(int i=0;i<127;++i)valid=valid&&std::isfinite(b.getSample(c,i));position+=127;++blocks;if(blocks>100000)break;}
        control.join();check(valid,"concurrent load/recall/loop and real-time playback remain finite");
    }
    check(audioAllocations.load()==0,"zero allocations inside process");
    auto* json=new juce::DynamicObject;json->setProperty("passed",failures==0);json->setProperty("checks",checks);json->setProperty("failures",failures);json->setProperty("audio_allocations",audioAllocations.load());
    juce::File::getCurrentWorkingDirectory().getChildFile("reference-dsp-report.json").replaceWithText(juce::JSON::toString(juce::var(json),true));
    std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures; audio allocations "<<audioAllocations.load()<<'\n';return failures?1:0;
}
