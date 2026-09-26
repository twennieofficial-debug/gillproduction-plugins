#include "../Source/PluginProcessor.h"
#include <cstdio>
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
int checks=0,failures=0;void check(bool b,const char*t){++checks;if(!b)++failures;std::printf("%s %s\n",b?"PASS":"FAIL",t);}
bool childrenInside(juce::Component&root,juce::Component&c){bool good=true;for(auto*child:c.getChildren()){if(child->isVisible())good=good&&root.getLocalBounds().contains(root.getLocalArea(child,child->getLocalBounds()));good=childrenInside(root,*child)&&good;}return good;}
struct Head:juce::AudioPlayHead{std::int64_t sample=0;bool playing=true;juce::Optional<PositionInfo>getPosition()const override{PositionInfo info;info.setTimeInSamples(sample);info.setTimeInSeconds(double(sample)/48000.);info.setIsPlaying(playing);return info;}};
int main(){
 std::setvbuf(stdout,nullptr,_IONBF,0);
#if JUCE_MAC
 gillInitialiseMacTestApplication();
#endif
 juce::ScopedJuceInitialiser_GUI gui;GillRiseProcessor p;check(p.getName()=="GILLRISE","Product identity");check(p.getNumPrograms()==6,"Six factory presets");
 std::vector<std::vector<float>>presets;
 for(int i=0;i<6;++i){p.setCurrentProgram(i);std::vector<float>v;for(auto*q:p.getParameters())v.push_back(q->getValue());check(std::find(presets.begin(),presets.end(),v)==presets.end(),"Distinct factory preset");presets.push_back(v);juce::MemoryBlock state;p.getStateInformation(state);p.setCurrentProgram((i+1)%6);p.setStateInformation(state.getData(),int(state.getSize()));check(p.getCurrentProgram()==i,"Preset state recall");}
 for(double fs:{44100.,48000.,96000.,192000.})for(int mode:{0,1}){p.setValue("gillQuality",float(mode));p.setPlayConfigDetails(2,2,fs,127);p.prepareToPlay(fs,127);juce::AudioBuffer<float>b(2,127),original(2,127);juce::MidiBuffer midi;bool identical=true;for(int block=0;block<100;++block){for(int c=0;c<2;++c)for(int i=0;i<127;++i)b.setSample(c,i,float(.08*std::sin((block*127+i)*.03+c)));original.makeCopyOf(b);p.processBlock(b,midi);for(int c=0;c<2;++c)for(int i=0;i<127;++i)identical=identical&&b.getSample(c,i)==original.getSample(c,i);}check(identical,"Capture leaves original audio bit exact in LIVE and PRO");check(p.getLatencySamples()==0,"Both modes report actual zero algorithmic latency");}
 p.setCurrentProgram(0);p.prepareToPlay(48000,127);p.arm();juce::MidiBuffer midi;juce::AudioBuffer<float>b(2,127);Head head;p.setPlayHead(&head);
 for(int block=0;block<400;++block){head.sample=480000+block*127;for(int i=0;i<127;++i){double t=(block*127+i)/48000.;float a=t>.1&&t<.45?float(.2*std::sin(2*gill::rise::pi*200*t)):0;b.setSample(0,i,a);b.setSample(1,i,a);}p.processBlock(b,midi);}
 for(int n=0;n<300&&!p.result();++n)juce::Thread::sleep(10);check(bool(p.result()),"Real processor automatically renders captured syllable on worker");
 if(auto r=p.result()){check(r->hostPositionKnown&&std::abs(r->endSample-484800)<480,"Processor retains actual host position at first syllable");juce::String error;auto file=p.exportWav(error,juce::File::getCurrentWorkingDirectory().getChildFile("Exports"));juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader>reader(format.createReaderFor(file.createInputStream().release(),true));check(reader&&reader->numChannels==2&&reader->bitsPerSample==24&&reader->lengthInSamples==96000,"Export creates a real stereo 24-bit WAV with exact duration");check(reader&&reader->metadataValues.getValue(juce::WavAudioFormat::bwavTimeReference,"").getLargeIntValue()==r->endSample-96000,"BWF metadata records original host placement, never an invented drop position");check(p.exportWav(error)==file,"Repeated drag reuses unchanged durable WAV");}
 juce::MemoryBlock saved;p.getStateInformation(saved);p.setValue("length",4);p.setStateInformation(saved.getData(),int(saved.getSize()));for(int n=0;n<300&&!p.result();++n)juce::Thread::sleep(10);check(bool(p.captured())&&bool(p.result()),"Captured syllable and settings survive project recall");
 p.setNonRealtime(true);p.audition(true);b.clear();b.setSample(0,0,.25f);p.processBlock(b,midi);check(b.getSample(0,0)==.25f&&!p.previewing,"Offline host render excludes preview");p.setNonRealtime(false);
 auto stateBefore=p.value("length");const char broken[]="not state";p.setStateInformation(broken,sizeof(broken));check(p.value("length")==stateBefore,"Invalid state cannot corrupt controls");
 std::unique_ptr<juce::AudioProcessorEditor>editor(p.createEditor());check(editor->getWidth()==660&&editor->getHeight()==465,"Compact default rectangular editor");check(childrenInside(*editor,*editor),"All visible controls stay within native editor");
 for(float scale:{1.f,1.25f,1.5f,2.f}){auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);auto file=juce::File::getCurrentWorkingDirectory().getChildFile("GILLRISE-UI-"+juce::String(image.getWidth())+"x"+juce::String(image.getHeight())+".png");auto stream=file.createOutputStream();check(image.isValid()&&stream!=nullptr,"Native editor capture at target pixel scale");if(stream){stream->setPosition(0);stream->truncate();juce::PNGImageFormat png;check(png.writeImageToStream(image,*stream),"Native screenshot encoded");}}
 editor.reset();p.setPlayHead(nullptr);std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;
}
