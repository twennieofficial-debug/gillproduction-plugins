#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <fstream>
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
struct Head:juce::AudioPlayHead{double seconds=0,beat=0,bpm=120;bool playing=true,samplesOnly=false;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;if(samplesOnly)p.setTimeInSamples(static_cast<juce::int64>(std::llround(seconds*48000)));else p.setTimeInSeconds(seconds);p.setBpm(bpm);p.setIsPlaying(playing);return p;}};
float voice(double t){const int phrase=int(t);const double local=std::fmod(t,1.),end=.48+.04*(phrase%5),pitch=137+19*(phrase%7),level=.13+.02*(phrase%4);if(local<.08||local>end)return 0;const double fade=std::min(1.,std::min((local-.08)/.025,(end-local)/.035));return static_cast<float>(fade*(level*std::sin(2*gill::creative::pi*pitch*t)+.035*std::sin(4*gill::creative::pi*pitch*t)));}
int main(){
#if JUCE_MAC
gillInitialiseMacTestApplication();
#endif
juce::ScopedJuceInitialiser_GUI gui;int checks=0,failures=0;auto check=[&](bool pass,const char*name){++checks;if(!pass)++failures;std::printf("%s %s\n",pass?"PASS":"FAIL",name);};
const auto evidenceOverride=juce::SystemStats::getEnvironmentVariable("GILL_CREATIVE_AUDIO_ROOT",{});const auto folder=(evidenceOverride.isEmpty()?juce::File::getCurrentWorkingDirectory():juce::File(evidenceOverride)).getChildFile("full-song-evidence");folder.createDirectory();
#if JUCE_WINDOWS
_putenv_s("GILL_CREATIVE_AUDIO_ROOT",folder.getFullPathName().toRawUTF8());
#else
setenv("GILL_CREATIVE_AUDIO_ROOT",folder.getFullPathName().toRawUTF8(),1);
#endif
for(auto kind:{CreativeKind::Phrase,CreativeKind::Director,CreativeKind::Reply}){
auto p=std::make_unique<GillCreativeProcessor>(kind);printf("PRODUCT %s\n",p->getName().toRawUTF8());Head head;head.seconds=12;head.samplesOnly=kind!=CreativeKind::Phrase;head.playing=false;p->setPlayHead(&head);p->prepareToPlay(48000,1024);juce::AudioBuffer<float>audio(2,1024);juce::MidiBuffer midi;
p->startLearn();audio.clear();p->processBlock(audio,midi);check(p->engine.learningState()==5,"LEARN arms while host transport is stopped");head.playing=true;
// Accelerated playback, not an unbounded producer-speed stress: allow the
// background disk writer to run outside the audio callback between blocks.
for(int at=0;at<14402400;at+=1024){const int n=std::min(1024,14402400-at);audio.setSize(2,n,false,false,true);for(int i=0;i<n;++i){const float value=voice((at+i)/48000.);audio.setSample(0,i,value);audio.setSample(1,i,value);}head.seconds=12+at/48000.;head.bpm=at<48000*100?120:at<48000*200?173:89;p->processBlock(audio,midi);juce::Thread::sleep(1);}
const auto plan=p->plan();check(plan.valid()&&plan.count==300&&std::abs(plan.durationSeconds-300)<.001f,"complete 48 kHz five-minute pass retains all 300 phrase markers");check(plan.timeAnchor&&plan.originSeconds==12,"captured timeline is anchored from seconds or sample-only host start");check(plan.count>1&&std::abs(plan.markers[0].endSec-plan.markers[0].startSec-(plan.markers[4].endSec-plan.markers[4].startSec))>.08,"varied phrase lengths survive tempo changes without truncation");
for(int i=0;i<3000&&!p->canRender();++i)juce::Thread::sleep(5);if(!p->canRender())std::printf("ARCHIVE STATUS %d: %s\n",p->archive.status(),p->archive.error().toRawUTF8());check(p->canRender(),"background writer completes the whole-song source asset");juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>source(formats.createReaderFor(p->archive.sourceFile(plan.sourceRevision)));check(source&&source->lengthInSamples==14400000&&source->sampleRate==48000,"durable source contains every sample up to the 300-second boundary");
p->setValue("amount",100,false);p->setValue("length",.18f,false);p->setValue("mix",100,false);if(kind==CreativeKind::Phrase)p->setValue("delaySend",65,false);
check(p->renderEffects(true),"whole-song wet export starts");for(int i=0;i<12000&&p->renderer.busy();++i)juce::Thread::sleep(5);std::unique_ptr<juce::AudioFormatReader>render(formats.createReaderFor(p->renderer.file()));check(render&&render->lengthInSamples>=14400000,"whole-song export retains the five minute song and effect tail");
check(p->renderer.file().copyFileTo(folder.getChildFile(p->getName()+"-five-minute-FX.wav")),"named five-minute listening artifact retained");
if(render){juce::AudioBuffer<float>tail(2,48000*4);render->read(&tail,0,tail.getNumSamples(),298*48000,true,true);check(tail.getRMSLevel(0,0,tail.getNumSamples())>1.e-5f,"rendered effect remains audible at final phrases after 298 seconds");}
juce::MemoryBlock saved;p->getStateInformation(saved);check(saved.getSize()<200000,"five-minute project state remains compact and refers to durable audio");
auto restored=std::make_unique<GillCreativeProcessor>(kind);restored->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));Head next;restored->setPlayHead(&next);restored->prepareToPlay(48000,1024);audio.clear();restored->processBlock(audio,midi);check(restored->plan().count==300&&restored->canRender(),"reopened song restores every marker and its renderable source");
p->setPlayHead(nullptr);restored->setPlayHead(nullptr);}
std::ofstream report("creative-full-song-report.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"sample_rate\":48000,\"captured_frames\":14400000,\"duration_seconds\":300}";std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
