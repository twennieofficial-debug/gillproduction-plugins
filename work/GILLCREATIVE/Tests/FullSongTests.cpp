#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <fstream>
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
struct Head:juce::AudioPlayHead{double seconds=0;bool playing=true;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setTimeInSeconds(seconds);p.setPpqPosition(seconds*2);p.setBpm(120);p.setIsPlaying(playing);return p;}};
float voice(double t){const double local=std::fmod(t,1.);if(local<.08||local>.60)return 0;const double fade=std::min(1.,std::min((local-.08)/.025,(.60-local)/.035));return static_cast<float>(fade*(.2*std::sin(2*gill::creative::pi*171*t)+.055*std::sin(2*gill::creative::pi*342*t)));}
int main(){
#if JUCE_MAC
gillInitialiseMacTestApplication();
#endif
juce::ScopedJuceInitialiser_GUI gui;int checks=0,failures=0;auto check=[&](bool pass,const char*name){++checks;if(!pass)++failures;std::printf("%s %s\n",pass?"PASS":"FAIL",name);};
const auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("full-song-evidence");folder.createDirectory();
#if JUCE_WINDOWS
_putenv_s("GILL_CREATIVE_AUDIO_ROOT",folder.getFullPathName().toRawUTF8());
#else
setenv("GILL_CREATIVE_AUDIO_ROOT",folder.getFullPathName().toRawUTF8(),1);
#endif
auto p=std::make_unique<GillCreativeProcessor>(CreativeKind::Phrase);Head head;head.seconds=12;head.playing=false;p->setPlayHead(&head);p->prepareToPlay(48000,1024);juce::AudioBuffer<float>audio(2,1024);juce::MidiBuffer midi;
p->startLearn();audio.clear();p->processBlock(audio,midi);check(p->engine.learningState()==5,"LEARN arms while host transport is stopped");head.playing=true;
for(int at=0;at<14402400;at+=1024){const int n=std::min(1024,14402400-at);audio.setSize(2,n,false,false,true);for(int i=0;i<n;++i){const float value=voice((at+i)/48000.);audio.setSample(0,i,value);audio.setSample(1,i,value);}head.seconds=12+at/48000.;p->processBlock(audio,midi);}
const auto plan=p->plan();check(plan.valid()&&plan.count==300&&std::abs(plan.durationSeconds-300)<.001f,"complete 48 kHz five-minute pass retains all 300 phrase markers");check(plan.timeAnchor&&plan.originSeconds==12,"captured timeline is anchored to its actual host start");
for(int i=0;i<3000&&!p->canRender();++i)juce::Thread::sleep(5);check(p->canRender(),"background writer completes the whole-song source asset");juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader>source(formats.createReaderFor(p->archive.sourceFile(plan.sourceRevision)));check(source&&source->lengthInSamples==14400000&&source->sampleRate==48000,"durable source contains every sample up to the 300-second boundary");
check(p->renderEffects(true),"whole-song wet export starts");for(int i=0;i<12000&&p->renderer.busy();++i)juce::Thread::sleep(5);std::unique_ptr<juce::AudioFormatReader>render(formats.createReaderFor(p->renderer.file()));check(render&&render->lengthInSamples==14976000,"whole-song export includes five minutes plus twelve seconds of tail");
if(render){juce::AudioBuffer<float>tail(2,48000*4);render->read(&tail,0,tail.getNumSamples(),298*48000,true,true);check(tail.getRMSLevel(0,0,tail.getNumSamples())>1.e-5f,"rendered reverb remains audible at the final phrase after 298 seconds");}
juce::MemoryBlock saved;p->getStateInformation(saved);check(saved.getSize()<200000,"five-minute project state remains compact and refers to durable audio");
auto restored=std::make_unique<GillCreativeProcessor>(CreativeKind::Phrase);restored->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));Head next;restored->setPlayHead(&next);restored->prepareToPlay(48000,1024);audio.clear();restored->processBlock(audio,midi);check(restored->plan().count==300&&restored->canRender(),"reopened song restores every marker and its renderable source");
std::ofstream report("creative-full-song-report.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"sample_rate\":48000,\"captured_frames\":14400000,\"duration_seconds\":300}";p->setPlayHead(nullptr);restored->setPlayHead(nullptr);std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
