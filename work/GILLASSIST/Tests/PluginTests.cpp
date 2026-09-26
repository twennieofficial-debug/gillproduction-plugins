#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <random>
#include <cstdlib>
#include <new>
static thread_local bool trackAudioAllocations=false;
static thread_local std::size_t audioAllocations=0;
void*operator new(std::size_t size){if(trackAudioAllocations)++audioAllocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
namespace{
int checks=0,failures=0;
void check(bool good,const char*name){++checks;if(!good)++failures;std::printf("%s %s\n",good?"PASS":"FAIL",name);std::fflush(stdout);}
template<class Predicate>bool waitFor(Predicate ready,int timeout=20000){const auto limit=juce::Time::getMillisecondCounter()+juce::uint32(timeout);while(juce::Time::getMillisecondCounter()<limit){if(ready())return true;juce::Thread::sleep(10);}return ready();}
juce::File fixture(const juce::File&dir,const juce::String&name,double seconds,double rate=8000){
    auto path=dir.getChildFile(name);auto stream=path.createOutputStream();stream->setPosition(0);stream->truncate();juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatWriter>writer(wav.createWriterFor(stream.release(),rate,2,24,{},0));juce::AudioBuffer<float>b(2,1024);int frames=int(std::llround(seconds*rate));
    for(int offset=0;offset<frames;offset+=1024){int n=std::min(1024,frames-offset);for(int i=0;i<n;++i){const double t=(offset+i)/rate;float x=float((std::fmod(t,4.)<2?.1:.025)*std::sin(t*2*juce::MathConstants<double>::pi*173));b.setSample(0,i,x);b.setSample(1,i,x);}writer->writeFromAudioSampleBuffer(b,0,n);}writer.reset();return path;
}
bool bounds(juce::Component&root,juce::Component&c){for(auto*child:c.getChildren())if(child->isVisible()&&(!root.getLocalBounds().contains(root.getLocalArea(child,child->getLocalBounds()))||!bounds(root,*child)))return false;return true;}
struct PlayHead:juce::AudioPlayHead{bool playing=false;juce::int64 samples=0;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setIsPlaying(playing);p.setTimeInSamples(samples);return p;}};
}
int main(){
#if JUCE_MAC
    gillInitialiseMacTestApplication();
#endif
    juce::ScopedJuceInitialiser_GUI gui;using namespace gill::assist;
    auto testdir=juce::File::getCurrentWorkingDirectory().getChildFile("test-audio");testdir.createDirectory();
    {
        std::vector<Feature>f(30000);for(int i=0;i<30000;++i){float a=i%400<200?.1f:.025f;f[std::size_t(i)]={a*.7071f,a,.08f,.04f};}
        Settings neutral;neutral.rideOn=neutral.gateOn=neutral.breathOn=neutral.sibilanceOn=false;auto p=analyse(f,neutral,{},300,8);check(p.gainDb.size()==30000&&p.duration==300,"Full 300-second analysis with bounded 30,000-point timeline");bool unity=true;for(auto v:p.gainDb)unity=unity&&v==0;check(unity,"Disabled sections and zero output are exactly neutral");
        auto normal=analyse(f,Settings{}, {},300);check(normal.at(1)<normal.at(3),"Ride reduces loud phrase versus quiet phrase");check(normal.at(-1)==1&&normal.at(300)==1,"Outside transferred time range is dry");
        auto edit=analyse(f,neutral,{{298,299,-6,0}},300);check(std::abs(db(edit.at(298.5))+6)<1.e-5&&edit.at(150)==1,"Manual gain edits reach end of five-minute song without changing earlier audio");
        Settings hard;hard.gate=45;std::vector<Feature>gate(1000);for(int i=200;i<400;++i)gate[std::size_t(i)]={.07f,.1f,.08f,.04f};auto gp=analyse(gate,hard,{},10);check(db(gp.at(.5))<-40,"Gate attenuates verified low-level pause");check(gp.gate[199]>-1&&gp.gate[400]>-1,"Gate opens ahead of phrase and preserves trailing syllable");
        std::vector<Feature>breaths(600,{.07f,.1f,.06f,.04f});for(int i=250;i<270;++i)breaths[std::size_t(i)]={.01f,.03f,.4f,.15f};for(int i=350;i<375;++i)breaths[std::size_t(i)]={.055f,.12f,.8f,.25f};auto bp=analyse(breaths,Settings{}, {},6);check(bp.breath[260]<-1&&bp.sibilance[360]<-1,"Acoustic breath and sibilance fixtures produce distinct reductions");auto protectedPlan=analyse(breaths,Settings{},{{2.5,2.7,0,1}},6);check(std::abs(protectedPlan.at(2.6)-1)<1.e-6,"PROTECT region bypasses all automatic reduction locally");
        for(double rate:{8000.,22050.,44100.,48000.,96000.,192000.}){FeatureAccumulator a(rate);for(int i=0;i<int(rate*.051);++i)a.push(float(.1*std::sin(i*.13)),float(.1*std::sin(i*.13)));a.finish();check(a.features.size()==6,"Native-rate feature extraction preserves partial last frame");}
    }
    {
        GillAssistProcessor p;p.setPlayConfigDetails(2,2,48000,127);p.prepareToPlay(48000,127);check(p.getName()=="GILLASSIST"&&p.getNumPrograms()==6,"Product identity and six presets");
        std::vector<std::vector<float>>presets;for(int i=0;i<6;++i){p.setCurrentProgram(i);std::vector<float>values;for(auto*parameter:p.getParameters())values.push_back(parameter->getValue());check(std::find(presets.begin(),presets.end(),values)==presets.end(),"Distinct factory preset");presets.push_back(values);juce::MemoryBlock state;p.getStateInformation(state);p.setCurrentProgram((i+1)%6);p.setStateInformation(state.getData(),int(state.getSize()));check(p.getCurrentProgram()==i,"Factory preset survives state recall");}
        for(double rate:{8000.,44100.,48000.,96000.,192000.})for(bool pro:{false,true}){p.setValue("gillQuality",pro?1:0);p.prepareToPlay(rate,127);juce::AudioBuffer<float>b(2,127);juce::MidiBuffer midi;for(int c=0;c<2;++c)for(int i=0;i<127;++i)b.setSample(c,i,float(std::sin(i*.1)*.1));auto before=b;p.processBlock(b,midi);bool same=true;for(int c=0;c<2;++c)for(int i=0;i<127;++i)same=same&&before.getSample(c,i)==b.getSample(c,i);check(same&&p.getLatencySamples()==0,"No learned curve: exact dry with zero LIVE/PRO PDC");}
        p.prepareToPlay(8000,127);p.setCurrentProgram(5);auto audio=fixture(testdir,"full300.wav",300);p.engine.importFile(audio,12.5);check(waitFor([&]{return p.engine.state==Engine::Ready;}),"Full five-minute stereo file analysed and rendered");auto plan=p.engine.plan();check(plan.duration==300&&plan.startSeconds==12.5&&plan.waveform.size()==30000,"Full duration and original timeline position retained");auto output=p.engine.claimExportFile();juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatReader>reader(wav.createReaderFor(output.createInputStream().release(),true));check(reader&&reader->lengthInSamples==2400000&&reader->numChannels==2&&reader->sampleRate==8000,"WAV export includes full five minutes at source rate and channel count");check(reader&&reader->metadataValues.getValue(juce::WavAudioFormat::bwavTimeReference,"").getLargeIntValue()==100000,"BWF time reference preserves exact 12.5-second DAW offset");
        auto rev=p.engine.completedRevision.load();p.engine.edit({298,299,-6,0});check(waitFor([&]{return p.engine.completedRevision>rev&&p.engine.state==Engine::Ready;}),"Late manual edit automatically rebuilds curve and export");check(std::abs(db(p.engine.plan().at(298.5))+6)<.01,"Manual late gain correction verified");
        PlayHead ph;ph.playing=true;ph.samples=juce::int64((12.5+298.5)*8000);p.setPlayHead(&ph);juce::AudioBuffer<float>b(2,127);juce::MidiBuffer midi;for(int c=0;c<2;++c)for(int i=0;i<127;++i)b.setSample(c,i,.1f);p.processBlock(b,midi);check(std::abs(b.getSample(0,50)-.1f*gain(-6))<1.e-6&&p.engine.timelineMatched,"Learned curve processes exact host position near end of song");
        audioAllocations=0;trackAudioAllocations=true;for(int block=0;block<100;++block)p.processBlock(b,midi);trackAudioAllocations=false;check(audioAllocations==0,"Learned processing performs no C++ heap allocation on audio thread");
        for(int c=0;c<2;++c)for(int i=0;i<127;++i)b.setSample(c,i,.1f);ph.samples=0;p.processBlock(b,midi);check(b.getSample(0,50)==.1f&&!p.engine.timelineMatched,"Host seek outside source returns unchanged signal");
        rev=p.engine.completedRevision;p.engine.undo();check(waitFor([&]{return p.engine.completedRevision>rev;}),"Undo creates rebuilt curve");check(p.engine.plan().at(298.5)==1,"Undo restores original neutral gain");rev=p.engine.completedRevision;p.engine.redo();check(waitFor([&]{return p.engine.completedRevision>rev;}),"Redo creates rebuilt curve");check(db(p.engine.plan().at(298.5))<-5.99,"Redo restores edited gain");
        juce::MemoryBlock saved;p.getStateInformation(saved);GillAssistProcessor restored;restored.prepareToPlay(8000,127);restored.setStateInformation(saved.getData(),int(saved.getSize()));check(waitFor([&]{return restored.engine.state==Engine::Ready;}),"Owned durable cache and edits survive project recall");check(restored.engine.plan().startSeconds==12.5&&db(restored.engine.plan().at(298.5))<-5.99,"Restored transfer is aligned and retains manual edits");check(restored.engine.exportFile()!=output&&output.existsAsFile(),"Recalled instance never overwrites a WAV already dragged to a project");
        check(!Engine::validToken("../../elsewhere")&&!Engine::validToken("C:/file.wav"),"State cache token rejects arbitrary paths");
        GillAssistProcessor blank;juce::MemoryBlock emptyState;blank.getStateInformation(emptyState);auto retained=restored.engine.claimExportFile();restored.setStateInformation(emptyState.getData(),int(emptyState.getSize()));
        check(restored.engine.transferState().token.isEmpty()&&restored.engine.exportFile()==juce::File(),"Empty project state immediately detaches prior cache and export");
        restored.setPlayHead(&ph);ph.samples=juce::int64((12.5+298.5)*8000);for(int c=0;c<2;++c)for(int i=0;i<127;++i)b.setSample(c,i,.1f);restored.processBlock(b,midi);
        check(b.getSample(0,50)==.1f&&!restored.engine.timelineMatched,"Loading empty state cannot apply an old learned gain curve");
        check(waitFor([&]{return restored.engine.state==Engine::Idle&&restored.engine.plan().gainDb.empty();})&&restored.engine.edits().empty()&&retained.existsAsFile(),"Empty-state worker reset clears edits but preserves previously claimed WAV");restored.setPlayHead(nullptr);
        p.setPlayHead(nullptr);p.setCurrentProgram(0);std::unique_ptr<juce::AudioProcessorEditor>editor(p.createEditor());check(editor->getWidth()==820&&editor->getHeight()==520&&bounds(*editor,*editor),"Compact rectangular UI keeps all controls in bounds");
        for(float scale:{1.f,1.25f,1.5f,2.f}){auto img=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);auto file=juce::File::getCurrentWorkingDirectory().getChildFile("GILLASSIST-UI-"+juce::String(img.getWidth())+"x"+juce::String(img.getHeight())+".png");auto stream=file.createOutputStream();if(stream){stream->setPosition(0);stream->truncate();}juce::PNGImageFormat png;check(stream&&png.writeImageToStream(img,*stream),"Native UI screenshot at supported pixel scale");}
    }
    {
        Settings neutral;neutral.rideOn=neutral.gateOn=neutral.breathOn=neutral.sibilanceOn=false;Engine e([&]{return neutral;});e.prepare(8000,2);e.arm();check(waitFor([&]{return e.state==Engine::Armed;}),"Transfer arms before host playback");juce::AudioBuffer<float>b(2,128);b.clear();e.process(b,false,true,0,false);check(e.state==Engine::Armed&&e.capturedSeconds==0,"Stopped host does not record idle samples");
        std::int64_t position=16000;for(int block=0;block<188;++block){for(int i=0;i<128;++i)for(int c=0;c<2;++c)b.setSample(c,i,float(.1*std::sin((position+i)*.1)));e.process(b,true,true,position,false);position+=128;}e.process(b,false,true,position,false);check(waitFor([&]{return e.state==Engine::Ready;}),"Transport STOP finalizes capture, analysis and export");check(std::abs(e.plan().duration-3.008)<.001&&e.plan().startSeconds==2,"Transfer duration and DAW offset accurate to sample");
        e.arm();check(waitFor([&]{return e.state==Engine::Armed;}),"Second transfer re-arms safely");e.process(b,true,true,0,false);e.process(b,true,true,20000,false);check(waitFor([&]{return e.state==Engine::Ready;}),"Timeline seek finalizes contiguous capture instead of recording wrong alignment");check(e.plan().duration==.016,"Seek excludes discontinuous block");
        e.arm();check(waitFor([&]{return e.state==Engine::Armed;}),"Five-minute recording arms");b.clear();position=0;for(int block=0;block<18750;++block){for(int i=0;i<128;++i){float sample=float(.05*std::sin((position+i)*.137));b.setSample(0,i,sample);b.setSample(1,i,sample);}e.process(b,true,true,position,false);position+=128;if(block%100==0)juce::Thread::sleep(2);}check(waitFor([&]{return e.state==Engine::Ready;}),"Actual streamed recording automatically stops at full 300-second limit");check(e.plan().duration==300&&e.capturedSeconds==300,"Five-minute recording retains last sample without truncation");
    }
    {
        GillAssistProcessor p;p.prepareToPlay(8000,128);p.setCurrentProgram(5);PlayHead ph;ph.playing=true;ph.samples=16000;p.setPlayHead(&ph);p.engine.arm();check(waitFor([&]{return p.engine.state==Engine::Armed;}),"Capture-state regression arms normally");
        juce::AudioBuffer<float>b(2,128);juce::MidiBuffer midi;for(int c=0;c<2;++c)for(int i=0;i<128;++i)b.setSample(c,i,.05f);p.processBlock(b,midi);ph.samples+=128;
        juce::MemoryBlock midCapture;p.getStateInformation(midCapture);auto xml=juce::AudioProcessor::getXmlFromBinary(midCapture.getData(),int(midCapture.getSize()));
        check(xml&&xml->getDoubleAttribute("start")==2&&Engine::validToken(xml->getStringAttribute("cache")),"Mid-capture autosave keeps actual host start and owned cache token");
        for(int block=0;block<187;++block){p.processBlock(b,midi);ph.samples+=128;}ph.playing=false;p.processBlock(b,midi);check(waitFor([&]{return p.engine.state==Engine::Ready;}),"Autosaved transfer can finish normally after the snapshot");
        GillAssistProcessor restored;restored.prepareToPlay(8000,128);restored.setStateInformation(midCapture.getData(),int(midCapture.getSize()));check(waitFor([&]{return restored.engine.state==Engine::Ready;})&&restored.engine.plan().startSeconds==2&&std::abs(restored.engine.plan().duration-3.008)<.001,"Earlier capture snapshot restores completed cache at correct nonzero song position");
        p.engine.arm();check(waitFor([&]{return p.engine.state==Engine::Armed;}),"Capture can re-arm before state cancellation");ph.playing=true;p.processBlock(b,midi);GillAssistProcessor blank;juce::MemoryBlock empty;blank.getStateInformation(empty);p.setStateInformation(empty.getData(),int(empty.getSize()));p.processBlock(b,midi);
        check(waitFor([&]{return p.engine.state==Engine::Idle&&p.engine.plan().gainDb.empty();})&&p.engine.transferState().token.isEmpty()&&p.engine.exportFile()==juce::File(),"Empty-state recall safely cancels active capture and detaches its output");p.setPlayHead(nullptr);
    }
    std::printf("RESULT %d checks, %d failures\n",checks,failures);return failures?1:0;
}

