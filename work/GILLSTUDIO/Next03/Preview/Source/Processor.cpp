#include "Processor.h"
namespace preview {
namespace { int treeRouting(const juce::ValueTree& tree){for(auto child:tree)if(child.getProperty("id").toString()=="routing")return int(child.getProperty("value"));return 0;} }

void Processor::timerCallback(){
 // JUCE's VST3 wrapper holds this same lock around each process callback.
 // Commit the engine before notifying the host, never in the realtime callback.
 // Unknown/headless transports retain the prepared mode until prepareToPlay.
 if(!modePending())return;
 const juce::ScopedLock callbackGuard(getCallbackLock());
 if(!prepared||!transportKnown.load(std::memory_order_acquire)||transportPlaying.load(std::memory_order_acquire))return;
 const int desired=int(controls[0]->load());
 if(desired==effectiveMode())return;
 auto next=parameters();next.mode=desired?sp::Mode::Live:sp::Mode::Pro;
 engine.setParameters(next);
 lastMode=desired;lastRouting=int(next.routing);fade=128;bypassRing={};bypassPos=0;
 appliedMode.store(desired,std::memory_order_release);
 profilePresent.store(engine.hasProfile());publish();
 setLatencySamples(int(engine.latencySamples()));
}

const juce::String Processor::getProgramName(int index) {
 static constexpr const char* names[]={"Clean Start","Gentle Vocal Hiss","Bright Microphone","Computer Fan","Harsh Vocal","Instrument Resonances","Level Tamer","Bright Level Control","Low-Mid Control"};
 return names[juce::jlimit(0,getNumPrograms()-1,index)];
}
void Processor::setCurrentProgram(int index) {
 if(index<0||index>=getNumPrograms())return;
 // Presets describe the processing target; a recording-specific learned noise
 // profile and the selected routing/latency mode remain owned by the session.
 for(auto* parameter:getParameters())if(auto* ranged=dynamic_cast<juce::RangedAudioParameter*>(parameter))
  if(ranged->paramID!="mode"&&ranged->paramID!="routing")ranged->setValueNotifyingHost(ranged->getDefaultValue());
 const float settings[9][10]={
  {0,0,24,0,6,20,20000,.5f,5,100},
  {.45f,0,12,-2,6,1200,20000,.8f,10,130},
  {.7f,0,18,0,6,3500,20000,.7f,10,150},
  {.65f,0,18,1,6,80,1800,.8f,20,220},
  {0,.5f,8,0,6,1500,10000,.8f,5,100},
  {0,.65f,12,0,8,180,12000,.7f,8,150},
  {0,0,12,0,6,20,20000,.5f,8,150},
  {0,0,10,0,6,1800,20000,.5f,3,100},
  {0,0,10,0,6,150,2000,.5f,12,180}
 };
 const char* ids[]={"denoise","resonance","depth","bias","threshold","low","high","preserve","attack","release"};
 for(int i=0;i<10;++i)set(ids[i],settings[index][i]);
 if(index>0&&index<6){const float frequencies[]={0,7000,5000,300,3800,1000};set("n0_on",1);set("n0_freq",frequencies[index]);set("n0_gain",index==1||index==5?3.f:4.f);set("n0_q",index==3||index==5?.5f:.7f);if(index==1||index==2)set("n0_shape",float(int(sp::Shape::HighShelf)));}
 if(index>=6){set("strength",.65f);set("levelthreshold",-36);set("d0_on",1);set("d0_freq",index==7?5500.f:index==8?450.f:2500.f);set("d0_gain",index==6?0.f:6.f);set("d0_q",.7f);}
 currentProgram.store(index);
}
Processor::Processor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withInput("Sidechain",juce::AudioChannelSet::stereo(),false).withOutput("Output",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"SpectraPreview",layout()){controls[0]=state.getRawParameterValue("mode");controls[1]=state.getRawParameterValue("routing");controls[2]=state.getRawParameterValue("denoise");controls[3]=state.getRawParameterValue("resonance");controls[4]=state.getRawParameterValue("depth");controls[5]=state.getRawParameterValue("bias");controls[6]=state.getRawParameterValue("threshold");controls[7]=state.getRawParameterValue("low");controls[8]=state.getRawParameterValue("high");controls[10]=state.getRawParameterValue("sharpness");controls[11]=state.getRawParameterValue("preserve");controls[12]=state.getRawParameterValue("attack");controls[13]=state.getRawParameterValue("release");controls[14]=state.getRawParameterValue("link");controls[15]=state.getRawParameterValue("mix");controls[16]=state.getRawParameterValue("output");controls[17]=state.getRawParameterValue("learn");controls[18]=state.getRawParameterValue("track");controls[19]=state.getRawParameterValue("delta");const char*extra[]={"strength","levelthreshold","levelslope","externalkey","keygain"};for(int i=0;i<5;++i)controls[size_t(20+i)]=state.getRawParameterValue(extra[i]);for(int i=0;i<12;++i){const char* ids[]={"on","shape","freq","gain","q","slope"};for(int j=0;j<6;++j){nodes[size_t(i)][size_t(j)]=state.getRawParameterValue("n"+juce::String(i)+"_"+ids[j]);detectorNodes[size_t(i)][size_t(j)]=state.getRawParameterValue("d"+juce::String(i)+"_"+ids[j]);}}startTimerHz(30);}
bool Processor::isBusesLayoutSupported(const BusesLayout& l)const{
 if(l.inputBuses.size()!=2||l.outputBuses.size()!=1)return false;
 const auto main=l.getMainOutputChannelSet(),key=l.getChannelSet(true,1);
 return l.getMainInputChannelSet()==main&&(main==juce::AudioChannelSet::mono()||main==juce::AudioChannelSet::stereo())&&(key.isDisabled()||key==juce::AudioChannelSet::mono()||key==juce::AudioChannelSet::stereo());
}
juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout(){
 juce::AudioProcessorValueTreeState::ParameterLayout l;
 auto number=[&](const char*id,const char*name,float lo,float hi,float v,float skew=1.f){juce::NormalisableRange<float> r(lo,hi);r.skew=skew;l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},name,r,v));};
 auto boolean=[&](const char*id,const char*name){l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id,1},name,false));};
 l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode",1},"Engine",juce::StringArray{"PRO","LIVE"},0));
 l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"routing",1},"Routing",juce::StringArray{"L/R","M/S"},0));
 number("denoise","Denoise",0,1,0);number("resonance","Resonance",0,1,0);number("depth","Maximum reduction dB",0,60,24);
 number("bias","Noise bias dB",-24,24,0);number("threshold","Resonance threshold dB",0,40,6);
 number("low","Focus edge A Hz",20,20000,20,.25f);number("high","Focus edge B Hz",20,20000,20000,.25f);
 number("sharpness","Sharpness",0,1,.5f);number("preserve","Preserve transients",0,1,.5f);
 number("attack","Attack ms",.05f,1000,5,.3f);number("release","Release ms",1,5000,100,.3f);
 number("link","Stereo link",0,1,1);number("mix","Mix",0,1,1);number("output","Output dB",-36,24,0);
 boolean("learn","Learn noise only");boolean("track","Adaptive noise tracking");boolean("delta","Removed signal");
 number("strength","Level strength",0,1,0);number("levelthreshold","Level threshold dBFS",-100,0,-30);number("levelslope","Threshold slope dB per octave",-12,12,0);boolean("externalkey","External sidechain");number("keygain","Sidechain gain dB",-24,24,0);
 for(int bank=0;bank<2;++bank)for(int i=0;i<12;++i){auto base=juce::String(bank?"d":"n")+juce::String(i)+"_";auto name=juce::String(bank?"Detector ":"Node ")+juce::String(i+1)+" ";
  l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{base+"on",1},name+"enabled",false));
  l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{base+"shape",1},name+"shape",juce::StringArray{"Bell","Low shelf","High shelf","Tilt","Notch","Low cut","High cut","Bandpass","Gain"},0));
  number((base+"freq").toRawUTF8(),(name+"Hz").toRawUTF8(),20,20000,float(40*std::pow(400.,i/11.)),.25f);
  number((base+"gain").toRawUTF8(),(name+"weight dB").toRawUTF8(),-48,24,0);
  number((base+"q").toRawUTF8(),(name+"Q").toRawUTF8(),.1f,20,1,.4f);
  number((base+"slope").toRawUTF8(),(name+"slope dB/oct").toRawUTF8(),6,96,12);
 }return l;
}
sp::Parameters Processor::parameters()const {sp::Parameters p;p.mode=appliedMode.load(std::memory_order_acquire)?sp::Mode::Live:sp::Mode::Pro;p.routing=controls[1]->load()>.5f?sp::Channels::MidSide:sp::Channels::Stereo;
 p.denoise=controls[2]->load();p.resonance=controls[3]->load();p.maxReductionDb=controls[4]->load();p.noiseBiasDb=controls[5]->load();p.resonanceThresholdDb=controls[6]->load();p.lowHz=std::min(controls[7]->load(),controls[8]->load());p.highHz=std::max(controls[7]->load(),controls[8]->load());p.sharpness=controls[10]->load();p.preserveTransient=controls[11]->load();p.attackMs=controls[12]->load();p.releaseMs=controls[13]->load();p.stereoLink=controls[14]->load();p.mix=controls[15]->load();p.outputDb=controls[16]->load();p.learn=controls[17]->load()>.5f;p.trackNoise=controls[18]->load()>.5f;p.delta=controls[19]->load()>.5f;
 p.levelStrength=controls[20]->load();p.levelThresholdDb=controls[21]->load();p.levelSlopeDbPerOct=controls[22]->load();p.externalKey=controls[23]->load()>.5f;p.keyGainDb=controls[24]->load();
 for(int bank=0;bank<2;++bank)for(int i=0;i<12;++i){auto&n=(bank?p.detectorNodes:p.nodes)[size_t(i)];const auto&source=(bank?detectorNodes:nodes)[size_t(i)];n.enabled=source[0]->load()>.5f;n.shape=sp::Shape(int(source[1]->load()));n.frequency=source[2]->load();n.gainDb=source[3]->load();n.q=source[4]->load();n.slope=source[5]->load();}return p;}
void Processor::prepareToPlay(double rate,int block){const juce::ScopedLock callbackGuard(getCallbackLock());transportKnown.store(false);transportPlaying.store(true);displayRate.store(std::isfinite(rate)?juce::jlimit(8000.,192000.,rate):48000.);appliedMode.store(int(controls[0]->load()));sp::Profile retained;if(prepared)retained=engine.exportProfile();engine.prepare(rate,size_t(block),size_t(getMainBusNumInputChannels()),2048);engine.setParameters(parameters());if(prepared)engine.importProfile(retained);lastMode=effectiveMode();lastRouting=int(controls[1]->load());audioGeneration=stateGeneration.load();bypassMix=0;setLatencySamples(int(engine.latencySamples()));fade=0;bypassRing={};bypassPos=0;prepared=true;publish();}
void Processor::publish(){for(size_t i=0;i<snapshots.size();++i){auto&slot=snapshots[(size_t(serial)+i)%snapshots.size()];int expected=0;if(slot.ownership.compare_exchange_strong(expected,1,std::memory_order_acquire)){if(engine.copyProfileInto(slot.profile)){slot.generation=audioGeneration;slot.serial.store(++serial,std::memory_order_release);}slot.ownership.store(0,std::memory_order_release);return;}}}
void Processor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,false);}
void Processor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&){process(b,true);}
void Processor::process(juce::AudioBuffer<float>& buffer,bool bypassed){juce::ScopedNoDenormals guard;auto b=getBusBuffer(buffer,false,0);if(!prepared){b.clear();return;}
 auto keyBus=getBusBuffer(buffer,true,1);const int keyChannels=keyBus.getNumChannels();
 bool known=false,playing=true;
 if(auto* playHead=getPlayHead())if(auto position=playHead->getPosition()){known=true;playing=position->getIsPlaying()||position->getIsRecording();}
 transportPlaying.store(playing,std::memory_order_release);transportKnown.store(known,std::memory_order_release);
 audioGeneration=stateGeneration.load(std::memory_order_acquire);
 auto p=parameters();engine.setParameters(p);const int mode=p.mode==sp::Mode::Live?1:0;const int routing=int(p.routing);
 if(mode!=lastMode||routing!=lastRouting){lastMode=mode;lastRouting=routing;fade=128;bypassRing={};bypassPos=0;}
 for(auto&slot:incoming){int expected=2;if(slot.ownership.compare_exchange_strong(expected,1,std::memory_order_acquire)){if(slot.generation==audioGeneration){if(!engine.importProfile(slot.profile))engine.clearProfile();slot.ownership.store(0,std::memory_order_release);}else slot.ownership.store(slot.generation>audioGeneration?2:0,std::memory_order_release);}}
 if(clearRequested.exchange(false))engine.clearProfile();
 // Fixed chunks keep callback storage bounded for hosts with arbitrarily large blocks.
 // Both paths keep running so reversing a host bypass ramp never resets history.
 std::array<std::array<float,512>,2> dry{};
 const float target=bypassed?1.f:0.f;
 for(int start=0;start<b.getNumSamples();start+=512){const int count=std::min(512,b.getNumSamples()-start);std::array<float*,2> ptr{};
  for(int c=0;c<b.getNumChannels();++c)ptr[size_t(c)]=b.getWritePointer(c)+start;
  for(int i=0;i<count;++i){for(int c=0;c<b.getNumChannels();++c){const float input=float(sp::bounded(ptr[size_t(c)][i],-1e6,1e6));dry[size_t(c)][size_t(i)]=mode?input:bypassRing[size_t(c)][bypassPos];bypassRing[size_t(c)][bypassPos]=input;}bypassPos=(bypassPos+1)%2048;}
  std::array<const float*,2>key{};for(int c=0;c<keyChannels;++c)key[size_t(c)]=keyBus.getReadPointer(c)+start;
  engine.process(ptr.data(),size_t(count),keyChannels?key.data():nullptr,size_t(keyChannels));
  for(int i=0;i<count;++i){bypassMix+=juce::jlimit(-1.f/128,1.f/128,target-bypassMix);const float resetGain=fade>0?1-float(fade--)/128:1.f;
   for(int c=0;c<b.getNumChannels();++c){const float wet=ptr[size_t(c)][i];ptr[size_t(c)][i]=(wet+(dry[size_t(c)][size_t(i)]-wet)*bypassMix)*resetGain;}}
 }
 profilePresent.store(engine.hasProfile());reduction.store(float(engine.reductionDb()));publish();
}
void Processor::getStateInformation(juce::MemoryBlock&dest){std::lock_guard<std::mutex> lock(stateMutex);auto tree=state.copyState();sp::Profile profile;bool copied=false;
 // Readers own a slot while copying. The audio producer skips owned slots and never waits.
 uint64_t newest=0;for(auto&s:snapshots){int expected=0;if(s.ownership.compare_exchange_strong(expected,1,std::memory_order_acquire)){const auto generation=s.serial.load(std::memory_order_acquire);if(generation>newest&&s.generation==stateGeneration.load()){profile=s.profile;newest=generation;copied=true;}s.ownership.store(0,std::memory_order_release);}}
 for(auto&s:incoming){int expected=2;if(s.ownership.compare_exchange_strong(expected,1,std::memory_order_acquire)){if(s.generation==stateGeneration.load()){profile=s.profile;copied=true;}s.ownership.store(2,std::memory_order_release);}}
 if(copied&&!clearRequested.load()&&int(profile.routing)==treeRouting(tree)){juce::MemoryOutputStream bytes;bytes.writeDouble(profile.sampleRate);bytes.writeInt(int(profile.routing));bytes.writeInt(int(profile.fftSize));bytes.writeInt64(juce::int64(profile.frames));bytes.writeInt64(juce::int64(profile.liveSamples));for(auto& d:profile.density)for(auto v:d)bytes.writeDouble(v);for(auto& d:profile.livePower)for(auto v:d)bytes.writeDouble(v);tree.setProperty("profile",juce::var(bytes.getMemoryBlock()),nullptr);}
 tree.setProperty("stateSchema",2,nullptr);tree.setProperty("factoryProgram",currentProgram.load(),nullptr);juce::MemoryOutputStream out(dest,false);tree.writeToStream(out);
}
void Processor::setStateInformation(const void*data,int size){std::lock_guard<std::mutex> lock(stateMutex);auto tree=juce::ValueTree::readFromData(data,size_t(std::max(0,size)));if(!tree.isValid()||tree.getType()!=state.state.getType())return;
 // Legacy 0.1/0.2 states have no schema. Refuse unknown future or malformed
 // versions atomically, before replacing parameters or queued learned data.
 const auto schema=tree.getProperty("stateSchema",0);
 if(!(schema.isInt()||schema.isInt64())||int(schema)<0||int64_t(schema)>2)return;
 tree.removeProperty("stateSchema",nullptr);
 // APVTS retains values for absent entries. Fill defaults before replacement so
 // loading a legacy project into an already-used instance cannot retain new DSP.
 for(auto*parameter:getParameters())if(auto*ranged=dynamic_cast<juce::RangedAudioParameter*>(parameter)){
  bool found=false;for(auto child:tree)if(child.getProperty("id").toString()==ranged->paramID){found=true;break;}
  if(!found){juce::ValueTree child("PARAM");child.setProperty("id",ranged->paramID,nullptr);child.setProperty("value",ranged->convertFrom0to1(ranged->getDefaultValue()),nullptr);tree.addChild(child,-1,nullptr);}
 }
 sp::Profile profile;bool valid=false;const auto profileValue=tree.getProperty("profile");if(auto*bytes=profileValue.getBinaryData()){if(bytes->getSize()==32+8*(2050+64)){juce::MemoryInputStream in(*bytes,false);profile.sampleRate=in.readDouble();profile.routing=sp::Channels(in.readInt());profile.fftSize=size_t(in.readInt());profile.frames=uint64_t(in.readInt64());profile.liveSamples=uint64_t(in.readInt64());valid=std::isfinite(profile.sampleRate)&&profile.sampleRate>=8000&&profile.sampleRate<=192000&&profile.frames<=sp::Processor::maxProfileUpdates&&profile.liveSamples<=sp::Processor::maxProfileUpdates&&profile.fftSize==2048&&(profile.routing==sp::Channels::Stereo||profile.routing==sp::Channels::MidSide);for(auto&d:profile.density){d.resize(1025);for(auto&v:d){v=in.readDouble();valid&=std::isfinite(v)&&v>=0&&v<=1e12;}}for(auto&d:profile.livePower)for(auto&v:d){v=in.readDouble();valid&=std::isfinite(v)&&v>=0&&v<=1e12;}}}
 valid=valid&&int(profile.routing)==treeRouting(tree);const auto generation=stateGeneration.load()+1;
 currentProgram.store(juce::jlimit(0,getNumPrograms()-1,int(tree.getProperty("factoryProgram",0))));tree.removeProperty("factoryProgram",nullptr);tree.removeProperty("profile",nullptr);state.replaceState(tree);for(auto&s:incoming){int expected=2;s.ownership.compare_exchange_strong(expected,0);}if(valid){clearRequested.store(false);for(auto&s:incoming){int expected=0;if(s.ownership.compare_exchange_strong(expected,1,std::memory_order_acquire)){s.profile=profile;s.generation=generation;s.ownership.store(2,std::memory_order_release);break;}}}else clearRequested.store(true);stateGeneration.store(generation,std::memory_order_release);
}
}





