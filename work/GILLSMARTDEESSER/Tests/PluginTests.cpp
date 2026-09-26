#include "PluginProcessor.h"
#include "Signals.h"
#include <thread>
#include <cstdlib>
#include <new>
namespace allocationAudit { thread_local bool enabled=false; thread_local size_t count=0; }
void* operator new(std::size_t size){if(allocationAudit::enabled)++allocationAudit::count;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#if defined(__APPLE__)
extern "C" void gillInitialiseMacTestApplication();
#endif
namespace {
void feed(GillSmartDeEsserProcessor& p,const std::vector<float>& input,int block=257){juce::MidiBuffer midi;for(size_t start=0;start<input.size();start+=block){const int count=static_cast<int>(std::min<size_t>(block,input.size()-start));juce::AudioBuffer<float> b(2,count);for(int c=0;c<2;++c)std::copy_n(input.data()+start,count,b.getWritePointer(c));p.processBlock(b,midi);}}
void pump(){auto* manager=juce::MessageManager::getInstance();juce::Timer::callAfterDelay(25,[manager]{manager->stopDispatchLoop();});manager->runDispatchLoop();}
}
int main(int argc,char** argv){std::cout<<std::unitbuf;
#if defined(__APPLE__)
 gillInitialiseMacTestApplication();
#endif
 juce::ScopedJuceInitialiser_GUI gui;using test::check;
 {
    GillSmartDeEsserProcessor p;p.prepareToPlay(48000,512);check(p.getLatencySamples()==0,"initial host latency is zero");
    p.startLearning();feed(p,std::vector<float>(48000*5,0));p.finishLearning();feed(p,std::vector<float>(1,0));check(p.learnState.load()==GillSmartDeEsserProcessor::insufficient&&!p.applyLearned(),"silent capture cannot be applied");
    auto vocal=test::vocal(48000);p.startLearning();feed(p,vocal);check(p.learnState.load()==GillSmartDeEsserProcessor::learning,"full-song learner remains active beyond old eight-second limit");p.finishLearning();feed(p,std::vector<float>(1,0));gillsmart::Profile learned;check(p.learnedCandidate(learned),"real active capture produces a candidate");check(p.value("profile")==0&&p.value("frequency")==6500,"learn waits for APPLY before altering sound");
    const float initialAmount=p.value("amount");check(p.applyLearned()&&p.value("profile")==1,"APPLY writes learned profile");const auto firstHz=p.value("frequency");check(std::abs(firstHz-learned.frequency)<1,"frequency is actual analyzed candidate");check(p.canUndo()&&p.undoLearned()&&p.value("profile")==0&&p.value("amount")==initialAmount,"UNDO restores earlier manual profile");check(!p.undoLearned(),"undo is single coherent transaction");
    p.startLearning();feed(p,vocal);p.finishLearning();feed(p,std::vector<float>(1,0));check(p.applyLearned(),"second measured profile can be applied");
    juce::MemoryBlock state;p.getStateInformation(state);GillSmartDeEsserProcessor restored;restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));restored.prepareToPlay(48000,512);check(restored.value("profile")==1&&restored.value("frequency")==p.value("frequency")&&restored.value("threshold")==p.value("threshold"),"valid learned profile survives state recall and prepare");
    auto invalid=juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize()));auto tree=juce::ValueTree::fromXml(*invalid);tree.getChildWithProperty("id","threshold").setProperty("value","nan",nullptr);juce::MemoryBlock poisoned;juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),poisoned);const float before=restored.value("threshold");restored.setStateInformation(poisoned.getData(),static_cast<int>(poisoned.getSize()));check(restored.value("threshold")==before&&restored.value("profile")==1,"NaN state is rejected atomically");
    tree=juce::ValueTree::fromXml(*invalid);tree.getChildWithProperty("id","learnVoice").setProperty("value",0,nullptr);juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),poisoned);restored.setStateInformation(poisoned.getData(),static_cast<int>(poisoned.getSize()));check(restored.value("learnVoice")>2.5,"invalid learning evidence cannot restore as learned");
    for(int i=0;i<p.getNumPrograms();++i){p.setCurrentProgram(i);check(p.getProgramName(i).isNotEmpty()&&p.value("profile")==0,"factory starting point is explicitly unlearned");}
    for(double rate:{16000.,44100.,48000.,96000.,192000.})for(int channels:{1,2})for(int block:{1,17,128,2048}){
        GillSmartDeEsserProcessor processor;auto layout=processor.getBusesLayout();layout.inputBuses.set(0,channels==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());layout.outputBuses.set(0,layout.inputBuses[0]);check(processor.setBusesLayout(layout),"mono/stereo layout accepted");processor.prepareToPlay(rate,64);processor.setValue("amount",100,false);
        juce::AudioBuffer<float> b(channels,block);juce::MidiBuffer midi;for(int n=0;n<block;++n)for(int c=0;c<channels;++c)b.setSample(c,n,static_cast<float>(.2*std::sin(n*.3+c)));b.setSample(0,0,std::numeric_limits<float>::quiet_NaN());processor.processBlock(b,midi);bool finite=true;for(int c=0;c<channels;++c)for(int n=0;n<block;++n)finite&=std::isfinite(b.getSample(c,n));check(finite,"callback produces finite audio across layouts/rates/blocks");
        processor.setValue(gill::qualityParameterId,0,false);processor.qualityClient.pollOnMessageThread();processor.processBlock(b,midi);check(processor.getLatencySamples()==0,"LIVE latency remains exactly zero");processor.setValue(gill::qualityParameterId,1,false);processor.qualityClient.pollOnMessageThread();processor.processBlock(b,midi);check(processor.getLatencySamples()==0,"PRO latency remains exactly zero");
        juce::AudioBuffer<double> d(channels,block);d.clear();d.setSample(0,0,std::numeric_limits<double>::infinity());processor.processBlock(d,midi);check(std::isfinite(d.getSample(0,0)),"double precision callback sanitizes nonfinite input");
    }
    // LISTEN REMOVED is the exact residual after ramps settle; bypass settles
    // to dry independently of the residual audition state.
    GillSmartDeEsserProcessor normal,delta;normal.setValue("amount",100,false);delta.setValue("amount",100,false);delta.setValue("listen",1,false);normal.prepareToPlay(48000,512);delta.prepareToPlay(48000,512);
    auto noise=test::noise(48000,48000);juce::MidiBuffer midi;double reconstruction=0;
    for(size_t at=0;at<noise.size();at+=256){const int count=static_cast<int>(std::min<size_t>(256,noise.size()-at));juce::AudioBuffer<float>a(2,count),b(2,count);for(int c=0;c<2;++c){std::copy_n(noise.data()+at,count,a.getWritePointer(c));std::copy_n(noise.data()+at,count,b.getWritePointer(c));}normal.processBlock(a,midi);delta.processBlock(b,midi);for(int i=0;i<count;++i)reconstruction=std::max(reconstruction,std::abs(static_cast<double>(a.getSample(0,i)+b.getSample(0,i)-noise[at+i])));}
    check(reconstruction<1e-6,"normal output plus LISTEN REMOVED reconstructs original");
    delta.setValue("bypass",1,false);juce::AudioBuffer<float>bypass(2,2048);for(int c=0;c<2;++c)for(int i=0;i<2048;++i)bypass.setSample(c,i,.125f);delta.processBlock(bypass,midi);check(bypass.getSample(0,2047)==.125f,"bypass returns dry even during removed audition");
    GillSmartDeEsserProcessor realtime;realtime.prepareToPlay(48000,128);juce::AudioBuffer<float> scratch(2,128);scratch.clear();
    for(int mode:{0,1})for(bool capture:{false,true}){realtime.setValue(gill::qualityParameterId,static_cast<float>(mode),false);if(capture)realtime.startLearning();allocationAudit::count=0;allocationAudit::enabled=true;for(int i=0;i<100;++i)realtime.processBlock(scratch,midi);allocationAudit::enabled=false;check(allocationAudit::count==0,"LIVE/PRO audio callback including learning performs no C++ heap allocation");}
    p.setCurrentProgram(0);std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());check(editor&&editor->getWidth()==480&&editor->getHeight()==360,"compact editor dimensions");pump();
    auto image=editor->createComponentSnapshot(editor->getLocalBounds());auto directory=juce::File::getCurrentWorkingDirectory();if(argc==3&&juce::String(argv[1])=="--screenshots")directory=juce::File(argv[2]);directory.createDirectory();auto file=directory.getChildFile("GILLSMARTDEESSER-UI-480x360.png");file.deleteFile();if(auto stream=file.createOutputStream()){juce::PNGImageFormat png;check(png.writeImageToStream(image,*stream),"actual editor screenshot written");}else check(false,"screenshot path writable");
 }
 return test::finish();
}
