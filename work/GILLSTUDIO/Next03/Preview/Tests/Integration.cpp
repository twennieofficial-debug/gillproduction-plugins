#include "MacTestSupport.h"
#include "Processor.h"
#include <iostream>
#include <thread>
#include <random>
#include <stdexcept>
thread_local bool counting=false;thread_local size_t allocations=0;
void* operator new(size_t n){if(counting)++allocations;if(auto*p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}
using preview::Processor;
void require(bool pass,const char*message){if(!pass)throw std::runtime_error(message);}
void process(Processor&p,juce::AudioBuffer<float>&b){juce::MidiBuffer midi;allocations=0;counting=true;p.processBlock(b,midi);counting=false;require(allocations==0,"audio callback allocation");for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)require(std::isfinite(b.getSample(c,i)),"nonfinite audio");}
void presets(){
 for(int mode=0;mode<2;++mode)for(int program=0;program<9;++program){Processor p;p.set("mode",float(mode));p.prepareToPlay(48000,512);p.set("learn",1);
  std::mt19937 rng(145);std::normal_distribution<float> noise(0,.02f);juce::AudioBuffer<float>b(2,512);
  for(int n=0;n<120;++n){for(int i=0;i<512;++i)for(int c=0;c<2;++c)b.setSample(c,i,noise(rng));process(p,b);}
  require(p.profilePresent.load(),"factory fixture learns noise");p.setCurrentProgram(program);require(p.getCurrentProgram()==program,"factory program selection");require(p.value("mode")==float(mode),"factory changes LIVE/PRO");require(p.value("learn")==0,"factory leaves learn on");
  double inputPower=0,outputPower=0;for(int block=0;block<100;++block){for(int i=0;i<512;++i){const double time=(block*512+i)/48000.;float v=program>=4?float(.12*std::sin(2*juce::MathConstants<double>::pi*3800*time)+.06*std::sin(2*juce::MathConstants<double>::pi*1000*time)):noise(rng);for(int c=0;c<2;++c)b.setSample(c,i,v);if(block>=10)inputPower+=v*v;}
   process(p,b);if(block>=10)for(int i=0;i<512;++i)outputPower+=std::pow(b.getSample(0,i),2);}
  const double ratio=std::sqrt(outputPower/inputPower);require(p.profilePresent.load(),"factory erased learned profile");require(ratio<1.02&&ratio>.001,"factory unbounded audio");if(program>0)require(ratio<.997,"factory processing has no meaningful effect");else require(std::abs(ratio-1)<.015,"neutral preset attenuates");
  juce::MemoryBlock state;p.getStateInformation(state);Processor recalled;recalled.setStateInformation(state.getData(),int(state.getSize()));require(recalled.getCurrentProgram()==program,"factory name recall");
  std::cout<<"Preset "<<mode<<" "<<program<<" "<<p.getProgramName(program)<<" RMS ratio "<<ratio<<"\n";
 }
}
int main(int argc,char**argv){gillInitialiseTestApplication();
  juce::ScopedJuceInitialiser_GUI gui;std::cout.setf(std::ios::unitbuf);std::cout<<"GUI initialized\n";try{presets();
 for(int mode=0;mode<2;++mode){Processor p;std::cout<<"Processor constructed\n";p.set("mode",float(mode));p.prepareToPlay(48000,512);std::cout<<"Prepared\n";juce::AudioBuffer<float>b(2,5000);b.clear();b.setSample(0,0,1);b.setSample(1,0,1);process(p,b);int peak=0;for(int i=1;i<5000;++i)if(std::abs(b.getSample(0,i))>std::abs(b.getSample(0,peak)))peak=i;require(peak==(mode?0:2048),"measured latency mismatch");require(p.getLatencySamples()==peak,"reported latency mismatch");require(std::abs(b.getSample(0,peak)-1)<1e-5,"neutral peak");std::cout<<"Mode "<<mode<<" measured/reported latency "<<peak<<"\n";
 p.prepareToPlay(48000,512);b.clear();b.setSample(0,0,1);juce::MidiBuffer bypassMidi;p.processBlockBypassed(b,bypassMidi);require(std::abs(b.getSample(0,mode?0:2048)-1)<1e-6,"bypass delay");require(p.getTailLengthSeconds()>=(mode?0.:2048./48000.),"buffered tail");
 p.set("mode",float(1-mode));juce::Thread::sleep(80);juce::Timer::callPendingTimersSynchronously();b.clear();process(p,b);require(p.getLatencySamples()==(mode?0:2048)&&p.modePending(),"unknown-transport mode request must remain pending");p.prepareToPlay(48000,512);require(p.getLatencySamples()==(mode?2048:0)&&!p.modePending(),"reprepared mode transition PDC");b.clear();b.setSample(0,0,1);process(p,b);int transitionPeak=0;for(int i=1;i<5000;++i)if(std::abs(b.getSample(0,i))>std::abs(b.getSample(0,transitionPeak)))transitionPeak=i;require(transitionPeak==(mode?2048:0),"mode transition actual audio delay");}
 std::cout<<"Learning start\n";Processor p;p.set("learn",1);p.prepareToPlay(48000,333);std::mt19937 random(42);std::uniform_real_distribution<float>d(-.03f,.03f);for(int n=0;n<100;++n){juce::AudioBuffer<float>b(2,333);for(int c=0;c<2;++c)for(int i=0;i<333;++i)b.setSample(c,i,d(random));process(p,b);}std::cout<<"Learning done\n";require(p.profilePresent.load(),"learning");p.set("learn",0);p.set("denoise",.8f);p.set("n3_on",1);p.set("n3_freq",2700);p.set("n3_gain",12);
 std::cout<<"Save start\n";juce::MemoryBlock saved;p.getStateInformation(saved);std::cout<<"Save done\n";Processor recalled;recalled.setStateInformation(saved.getData(),int(saved.getSize()));std::cout<<"Restore done\n";recalled.prepareToPlay(48000,512);juce::AudioBuffer<float>b(2,512);b.clear();process(recalled,b);require(recalled.profilePresent.load(),"profile recall");require(std::abs(recalled.value("n3_freq")-2700)<.1f,"node recall");
 std::cout<<"Recall validated\n";for(int size:{1,7,63,127,512,1023,2049}){b.setSize(2,size);for(int n=0;n<10;++n){for(int c=0;c<2;++c)for(int i=0;i<size;++i)b.setSample(c,i,d(random));p.set("output",float(n-5));p.set("mix",n*.1f);process(p,b);}}
 std::atomic<bool> running{true};std::thread reader([&]{for(int i=0;i<120;++i){juce::MemoryBlock state;p.getStateInformation(state);require(state.getSize()>100,"concurrent state");}running=false;});while(running){b.clear();process(p,b);}reader.join();
 recalled.clearRequested.store(true);b.clear();process(recalled,b);require(!recalled.profilePresent.load(),"clear profile");
 std::cout<<"Editor start\n";std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());require(!editor->getWantsKeyboardFocus(),"editor swallows host keys");juce::ComboBox*nodeSelector=nullptr;juce::Slider*nodeFrequency=nullptr;for(auto*child:editor->getChildren()){if(auto*c=dynamic_cast<juce::ComboBox*>(child))if(c->getComponentID()=="node")nodeSelector=c;if(auto*slider=dynamic_cast<juce::Slider*>(child))if(slider->getComponentID()=="n0_freq")nodeFrequency=slider;require(!child->getWantsKeyboardFocus(),"child swallows host keys");}require(nodeSelector&&nodeFrequency,"node controls missing");nodeSelector->setSelectedId(4,juce::sendNotificationSync);nodeFrequency->setValue(3333,juce::sendNotificationSync);require(std::abs(p.value("n3_freq")-3333)<.2f,"UI node frequency binding");auto image=editor->createComponentSnapshot(editor->getLocalBounds());{juce::File file=juce::File::getCurrentWorkingDirectory().getChildFile(argc>1?argv[1]:"GILLSPECTRA PREVIEW-UI-default.png");juce::FileOutputStream out(file);require(out.openedOk(),"editor image stream");out.setPosition(0);out.truncate();require(juce::PNGImageFormat().writeImageToStream(image,out),"editor image write");}require(image.getWidth()==620&&image.getHeight()==660,"editor render");std::cout<<"PREVIEW_INTEGRATION_PASS\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}







