#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

using namespace gill::master;
namespace {
const char*presets[8][6]{
 {"CLEAN MASTER","RAP PUNCH","DENSE TRAP","GENTLE SAFETY","LOUD DEMO","-2 DB HEADROOM"},
 {"GENTLE CONTROL","TRAP LOW END","TAME DRONE","KICK PROTECT","MONO SUB","SUB CHECK"},
 {"GENTLE MASTER","GROOVE BUS","DENSE RAP","TRANSIENT FRIENDLY","BASS INDEPENDENT","PARALLEL GLUE"},
 {"ORIGINAL FIELD","WIDE HOOK MASTER","MONO LOW END","AIR WIDTH","NARROW MASTER","MONO CHECK"},
 {"NEUTRAL MASTER","DRUMS FORWARD","SOFT TRANSIENTS","TRAP SNAP","MORE SUSTAIN","TIGHT TAILS"},
 {"SUBTLE WEIGHT","808 TRANSLATION","ROUND BASS","BOLD HARMONICS","SMALL SPEAKER CHECK","GENTLE WARMTH"},
 {"MASTER COMPARE","PAIR 2","PAIR 3","PAIR 4","PAIR 5","PAIR 6"},
 {"-14 LUFS CHECK","LOUD RAP CHECK","DYNAMIC MASTER","-23 LUFS CHECK","PEAK HEADROOM","QUIET END CHECK"}
};
juce::String stateId(MasterKind kind){return juce::String(masterInfo(kind).name)+"_STATE";}
}
GillMasterProcessor::GillMasterProcessor(MasterKind k):AudioProcessor(BusesProperties().withInput("INPUT",juce::AudioChannelSet::stereo(),true).withOutput("OUTPUT",juce::AudioChannelSet::stereo(),true)),kind(k),specs(masterParams(k)),apvts(*this,nullptr,stateId(k),layout(k)){
    for(auto*p:getParameters()){auto*id=dynamic_cast<juce::AudioProcessorParameterWithID*>(p);raw.push_back(id?apvts.getRawParameterValue(id->paramID):nullptr);}
    switch(kind){case MasterKind::Ceiling:ceiling=std::make_unique<gillnext::FinishDSP>();loudness=std::make_unique<gillnext::Loudness>();break;
    case MasterKind::Low:low=std::make_unique<LowDSP>();break;case MasterKind::Glue:glue=std::make_unique<GlueDSP>();break;
    case MasterKind::Width:width=std::make_unique<WidthDSP>();break;case MasterKind::Punch:punch=std::make_unique<PunchDSP>();break;
    case MasterKind::Weight:weight=std::make_unique<WeightDSP>();break;case MasterKind::Delta:delta=std::make_unique<DeltaEngine>();break;
    case MasterKind::Deliver:deliver=std::make_unique<DeliverMeter>();break;}
    if(ceiling)finalPeak=std::make_unique<gillnext::OutputPeakMeter>();selectPreset(0,false);
}
juce::AudioProcessorValueTreeState::ParameterLayout GillMasterProcessor::layout(MasterKind kind){
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    for(const auto&p:masterParams(kind)){
        if(p.flag)result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{p.id,1},p.label,p.initial>.5f));
        else if(!p.choices.isEmpty())result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{p.id,1},p.label,p.choices,int(p.initial)));
        else result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{p.id,1},p.label,juce::NormalisableRange<float>(p.lo,p.hi,p.step),p.initial));
    }
    if(int(kind)<6){result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"OUTPUT",juce::NormalisableRange<float>(-18,kind==MasterKind::Ceiling?0.f:12.f,.01f),0));result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix",1},"MIX",juce::NormalisableRange<float>(0,100,.1f),100));}
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"BYPASS",false));result.add(gill::qualityParameter(1));return result;
}
float GillMasterProcessor::param(std::size_t i)const noexcept{if(i>=raw.size()||!raw[i])return 0;const auto v=raw[i]->load(std::memory_order_relaxed);return std::isfinite(v)?v:0;}
float GillMasterProcessor::value(const juce::String&id)const{if(auto*p=apvts.getRawParameterValue(id)){const auto v=p->load();return std::isfinite(v)?v:0;}return 0;}
void GillMasterProcessor::setValue(const juce::String&id,float v,bool gesture){if(!std::isfinite(v))return;if(auto*p=apvts.getParameter(id)){if(gesture)p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(v));if(gesture)p->endChangeGesture();}}
bool GillMasterProcessor::isBusesLayoutSupported(const BusesLayout&layout)const{const auto c=layout.getMainOutputChannelSet();return(c==juce::AudioChannelSet::mono()||c==juce::AudioChannelSet::stereo())&&layout.getMainInputChannelSet()==c;}
void GillMasterProcessor::updateParameters(){
    if(ceiling){gillnext::FinishParameters p;p.driveDb=param(0);p.ceilingDb=param(1);p.releaseMs=param(2);p.clip=param(3)<.5f?0:param(3)<1.5f?20:65;p.toneEnabled=p.compEnabled=p.stereoEnabled=false;ceiling->setParameters(p);}
    if(low)low->parameters({param(0),param(1),param(2),param(3),param(4)});
    if(glue)glue->parameters({param(0),param(3),int(param(4)),param(1),param(2)});
    if(width)width->parameters({{param(0),param(1),param(2)},param(3),param(4),param(5)>.5f});
    if(punch)punch->parameters({{param(0),param(1),param(2)},param(3),param(4),param(5)});
    if(weight)weight->parameters({param(0),param(1),int(param(2))});
    if(delta)delta->configure(param(0)>.5f,int(param(1)));
}
void GillMasterProcessor::prepareToPlay(double rate,int block){
    supported=std::isfinite(rate)&&rate>=8000&&rate<=192000;fs=supported?rate:48000;rateView=fs;const int channels=getTotalNumOutputChannels();lastMode=quality.isPro()?1:0;
    updateParameters();if(ceiling){ceiling->setLiveMode(lastMode==0);ceiling->prepare(fs,block,channels);loudness->prepare(fs,channels);}
    if(low)low->prepare(fs);if(glue)glue->prepare(fs);if(width)width->prepare(fs);if(punch)punch->prepare(fs);if(weight){weight->setPro(lastMode!=0);weight->prepare(fs);}
    if(delta)delta->prepare(fs);if(deliver)deliver->prepare(fs,channels);
    if(finalPeak)finalPeak->prepare(fs,channels);finalMaximum=0;resetMeterRequested=false;
    for(auto&d:dryDelay)d.fill(0);delayPosition=0;for(auto&f:auditionLow)f.reset();for(auto&f:speakerLow)f.reset();
    const auto n=specs.size();mix.set(int(kind)<6?param(n+1)*.01:1);outputGain.set(int(kind)<6?math::gain(param(n)):1);bypassBlend.set(param(n+(int(kind)<6?2:0))>.5f?1:0);matchBlend.set(0);
    monitor.set(!isNonRealtime()&&((low&&param(5)>.5f)||(weight&&param(3)>.5f)||(width&&param(6)>.5f))?1:0);
    for(auto*s:{&mix,&outputGain,&bypassBlend,&matchBlend,&monitor})s->prepare(fs,.015);
    detectorAlpha=math::alpha(.5,fs);holdDecay=std::exp(-1/(fs*.35));inPower=outPower=inputHold=outputHold=historyInput=historyOutput=0;
    historyClock=0;historyHop=std::max(1,int(fs*.025));historyWrite=0;historyPosition=0;for(auto&x:historyIn)x=0;for(auto&x:historyOut)x=0;
    scopeClock=scopeWrite=0;scopePosition=0;for(auto&x:scopeL)x=0;for(auto&x:scopeR)x=0;
    inputPeak=outputPeak=reduction=correlation=0;integrated=momentary=-100;peakDb=-160;for(auto&v:bandMeter)v=0;stereoMeter.prepare(fs);transition.prepare(fs,lastMode);setLatencySamples(supported?(ceiling?ceiling->latencySamples():weight?weight->latencySamples():0):0);
}
void GillMasterProcessor::releaseResources(){if(ceiling)ceiling->reset();if(low)low->reset();if(glue)glue->reset();if(width)width->reset();if(punch)punch->reset();if(weight)weight->reset();}
void GillMasterProcessor::processBlock(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,false);}
void GillMasterProcessor::processBlockBypassed(juce::AudioBuffer<float>&b,juce::MidiBuffer&){process(b,true);}
void GillMasterProcessor::process(juce::AudioBuffer<float>&buffer,bool hostBypass){
    juce::ScopedNoDenormals noDenormals;const int frames=buffer.getNumSamples(),channels=std::min(buffer.getNumChannels(),2);if(frames<=0||channels<1)return;
    if(!supported){for(int c=0;c<channels;++c)for(int i=0;i<frames;++i)if(!std::isfinite(buffer.getSample(c,i)))buffer.setSample(c,i,0);return;}
    const int mode=quality.isPro()?1:0;updateParameters();if(ceiling)ceiling->setLiveMode(mode==0);if(weight)weight->setPro(mode!=0);
    const int latency=ceiling?ceiling->latencySamples():weight?weight->latencySamples():0;
    if(lastMode!=mode){for(auto&d:dryDelay)d.fill(0);delayPosition=0;lastMode=mode;if(finalPeak)finalPeak->reset();finalMaximum=0;}quality.requestLatencySamples(latency);
    if(resetMeterRequested.exchange(false)){if(loudness)loudness->reset();if(finalPeak)finalPeak->reset();finalMaximum=0;}
    const auto n=specs.size();const bool bypass=hostBypass||param(n+(int(kind)<6?2:0))>.5f;
    if(int(kind)<6){mix.set(param(n+1)*.01);outputGain.set(math::gain(param(n)));}bypassBlend.set(bypass?1:0);
    const bool match=(ceiling&&param(4)>.5f)||(glue&&param(5)>.5f);matchBlend.set(match?1:0);
    monitor.set(!isNonRealtime()&&((low&&param(5)>.5f)||(weight&&param(3)>.5f)||(width&&param(6)>.5f))?1:0);if(isNonRealtime())monitor.reset();
    bool playing=false,hasTransport=false;if(deliver)if(auto*head=getPlayHead())if(auto position=head->getPosition()){hasTransport=true;playing=position->getIsPlaying();}
    if(deliver)deliver->beginBlock(playing,hasTransport,mode!=0);
    if(delta){
        double before=0;for(int c=0;c<channels;++c)for(int i=0;i<frames;++i){const auto x=float(math::input(buffer.getSample(c,i)));buffer.setSample(c,i,x);before=std::max(before,std::abs(double(x)));}
        // Preserve the host block boundary: both ends of the link must refer
        // to the same published SOURCE interval, even above 256 frames.
        delta->process(buffer.getArrayOfWritePointers(),channels,frames,int(param(2)),param(3)>.5f,isNonRealtime(),bypass);
        for(int i=0;i<frames;++i){Stereo y{buffer.getSample(0,i),buffer.getSample(channels-1,i)};const double op=std::max(std::abs(y[0]),std::abs(y[1]));outputHold=std::max(op,outputHold*holdDecay);stereoMeter.sample(y);historyInput=std::max(historyInput,before);historyOutput=std::max(historyOutput,op);
            if(++historyClock>=historyHop){historyIn[historyWrite%256]=float(historyInput);historyOut[historyWrite%256]=float(historyOutput);historyPosition=++historyWrite;historyClock=0;historyInput=historyOutput=0;}}
        inputHold=std::max(before,inputHold*std::pow(holdDecay,frames));inputPeak=float(inputHold);outputPeak=float(outputHold);correlation=float(stereoMeter.value());return;
    }
    for(int offset=0;offset<frames;offset+=256){const int count=std::min(256,frames-offset);std::array<std::array<float,256>,2>original{},processed{};
        for(int c=0;c<channels;++c)for(int i=0;i<count;++i)original[c][i]=processed[c][i]=float(math::input(buffer.getSample(c,offset+i)));
        if(channels==1){original[1]=original[0];processed[1]=processed[0];}
        float*ptr[]{processed[0].data(),processed[1].data()};if(ceiling)ceiling->process(ptr,channels,count);
        for(int i=0;i<count;++i){Stereo x{original[0][i],original[1][i]},y{processed[0][i],channels==2?processed[1][i]:processed[0][i]},dry=x;
            if(latency){for(int c=0;c<2;++c){dry[c]=dryDelay[c][delayPosition];dryDelay[c][delayPosition]=x[c];}delayPosition=(delayPosition+1)%latency;}
            if(low)y=low->sample(x);if(glue)y=glue->sample(x);if(width)y=width->sample(x);if(punch)y=punch->sample(x);if(weight)y=weight->sample(x);
            const double listen=monitor.next();
            if(low){const double k=LowPass::coefficient(param(1),fs);for(int c=0;c<2;++c){const auto band=auditionLow[c].process(y[c],k);y[c]+=listen*(band-y[c]);}}
            if(weight){const double k=LowPass::coefficient(180,fs);for(int c=0;c<2;++c){const double hp=y[c]-speakerLow[c].process(y[c],k);y[c]+=listen*(hp-y[c]);}}
            if(width){const auto mono=.5*(y[0]+y[1]);for(int c=0;c<2;++c)y[c]+=listen*(mono-y[c]);}
            const double inputEnergy=channels==2?.5*(dry[0]*dry[0]+dry[1]*dry[1]):dry[0]*dry[0];const double outputEnergy=channels==2?.5*(y[0]*y[0]+y[1]*y[1]):y[0]*y[0];
            math::follow(inPower,inputEnergy,detectorAlpha);math::follow(outPower,outputEnergy,detectorAlpha);
            double matched=1;if(inPower>1e-10&&outPower>1e-10)matched=std::clamp(std::sqrt(inPower/outPower),.25,ceiling?1.:4.);
            const double blend=matchBlend.next(),gain=outputGain.next()*(1+blend*(matched-1)),mixValue=mix.next(),bypassValue=bypassBlend.next(),wet=hostBypass?0:mixValue*(1-bypassValue);
            if(int(kind)<6)for(int c=0;c<2;++c)y[c]=dry[c]+wet*(y[c]*gain-dry[c]);
            if(deliver&&!bypass)deliver->sample(x,param(2));
            double ip=std::max(std::abs(x[0]),channels==2?std::abs(x[1]):0.),op=0;
            for(int c=0;c<channels;++c){buffer.setSample(c,offset+i,float(math::quiet(y[c])));op=std::max(op,std::abs(y[c]));}
            inputHold=std::max(ip,inputHold*holdDecay);outputHold=std::max(op,outputHold*holdDecay);stereoMeter.sample(y);
            if(width&&++scopeClock>=16){scopeL[scopeWrite%256]=float(y[0]);scopeR[scopeWrite%256]=float(y[1]);scopePosition=++scopeWrite;scopeClock=0;}
            historyInput=std::max(historyInput,ip);historyOutput=std::max(historyOutput,op);if(++historyClock>=historyHop){historyIn[historyWrite%256]=float(historyInput);historyOut[historyWrite%256]=float(historyOutput);++historyWrite;historyPosition=historyWrite;historyClock=0;historyInput=historyOutput=0;}
        }
    }
    if(ceiling||weight)transition.process(buffer.getArrayOfWritePointers(),channels,frames,mode,!hostBypass);
    inputPeak=float(inputHold);outputPeak=float(outputHold);correlation=float(stereoMeter.value());
    reduction=ceiling?ceiling->limiterReductionDb():low?float(low->reductionDb()):glue?float(glue->reductionDb()):0;
    if(punch){const auto gains=punch->gainDb();for(int i=0;i<3;++i)bandMeter[i]=float(gains[i]);}
    if(loudness){for(int i=0;i<frames;++i){const float l=buffer.getSample(0,i),r=buffer.getSample(channels-1,i);loudness->sample({l,r},channels);if(mode){finalPeak->process(l,r,channels);finalMaximum=std::max(finalMaximum,double(finalPeak->maximumPeak()));}else finalMaximum=std::max(finalMaximum,double(std::max(std::abs(l),std::abs(r))));}integrated=float(loudness->integrated);momentary=float(loudness->momentary);peakDb=float(math::db(finalMaximum));}
    if(deliver)deliver->publish();
}
const juce::String GillMasterProcessor::getProgramName(int i){return presets[int(kind)][std::clamp(i,0,5)];}
void GillMasterProcessor::selectPreset(int i,bool gesture){
    i=std::clamp(i,0,5);auto set=[&](const char*id,float v){setValue(id,v,gesture);};
    for(const auto&p:specs)if(p.id!="role")setValue(p.id,p.initial,gesture);set("output",0);set("mix",100);set("bypass",0);
    switch(kind){
    case MasterKind::Ceiling:set("drive",i==1?4:i==2?7:i==4?10:0);set("character",i==1?1:i==2||i==4?2:0);set("release",i==1?65:i==2||i==4?35:i==3?250:150);set("ceiling",i==5?-2:i==3?-1.5f:-1);break;
    case MasterKind::Low:set("amount",i==1?55:i==2?75:i==3?40:i==4?0:30);set("frequency",i==1?110:i==2?190:140);set("threshold",i==2?-24:-18);set("protect",i==3?100:60);set("width",i==4?0:i==1?30:100);set("listen",i==5?1:0);break;
    case MasterKind::Glue:set("amount",i==1?45:i==2?65:i==3?30:i==5?85:30);set("character",i==1?1:i==2||i==5?2:0);set("attack",i==3?70:i==2?10:30);set("release",i==1?110:i==2?250:180);set("detector",i==4?200:90);set("mix",i==5?35:100);break;
    case MasterKind::Width:set("low",i==1||i==2?0:i==4?70:100);set("mid",i==1?110:i==4?80:100);set("high",i==1?125:i==3?135:i==4?90:100);set("mono",i==5?1:0);break;
    case MasterKind::Punch:set("low",i==1?30:i==2?-35:i==3?15:0);set("mid",i==1?25:i==2?-30:i==3?40:0);set("high",i==1?15:i==2?-40:i==3?50:0);set("sustain",i==4?40:i==5?-45:0);break;
    case MasterKind::Weight:set("amount",i==1?50:i==2?35:i==3?65:i==5?12:25);set("frequency",i==1?85:i==2?130:110);set("colour",i==2?1:i==3?2:0);set("speaker",i==4?1:0);break;
    case MasterKind::Delta:set("pair",float(i));set("audition",0);set("match",1);break;
    case MasterKind::Deliver:set("target",i==1?-9:i==2?-18:i==3?-23:-14);set("peakTarget",i==4?-2:-1);set("silence",i==5?-75:-60);break;
    }program=i;
}
void GillMasterProcessor::getStateInformation(juce::MemoryBlock&bytes){auto tree=apvts.copyState();tree.setProperty("schema",1,nullptr);tree.setProperty("program",program.load(),nullptr);for(auto child:tree)if(child["id"].toString()=="audition"||child["id"].toString()=="speaker"||child["id"].toString()=="listen"||child["id"].toString()=="mono")child.setProperty("value",0,nullptr);if(auto xml=tree.createXml())copyXmlToBinary(*xml,bytes);}
void GillMasterProcessor::setStateInformation(const void*data,int bytes){
    if(!data||bytes<=0||bytes>2*1024*1024)return;auto xml=getXmlFromBinary(data,bytes);if(!xml||!xml->hasTagName(stateId(kind)))return;auto tree=juce::ValueTree::fromXml(*xml);if(!tree.isValid()||int(tree.getProperty("schema",0))!=1)return;juce::StringArray seen;
    for(auto child:tree){if(!child.hasType("PARAM"))return;const auto id=child["id"].toString();auto*p=apvts.getParameter(id);if(!p||seen.contains(id))return;seen.add(id);const auto text=child["value"].toString();char*end=nullptr;const double v=std::strtod(text.toRawUTF8(),&end);const auto r=p->getNormalisableRange();if(text.isEmpty()||!end||*end||!std::isfinite(v)||v<r.start-.001||v>r.end+.001||(p->isDiscrete()&&v!=std::floor(v)))return;if(id=="audition"||id=="speaker"||id=="listen"||id=="mono")child.setProperty("value",0,nullptr);}
    if(seen.size()!=int(raw.size()))return;apvts.replaceState(tree);for(auto child:tree)if(auto*p=apvts.getParameter(child["id"].toString()))p->setValueNotifyingHost(p->convertTo0to1(float(child["value"])));program=std::clamp(int(tree.getProperty("program",0)),0,5);
}
juce::String GillMasterProcessor::deliveryReport()const{
    if(!deliver)return{};const auto s=deliver->snapshot();juce::String t="GILLDELIVER - MASTER CHECK\n\n";
    t+="Measurement: "+juce::String(s.running?"RUNNING - incomplete":"STOPPED")+"\n";
    t+="Duration: "+juce::String(s.duration,3)+" s\nIntegrated loudness: "+juce::String(s.integrated,2)+" LUFS\nLoudness range: "+juce::String(s.range,2)+" LU\n";
    t+=juce::String(s.truePeak?"Estimated true peak: ":"Sample peak: ")+juce::String(s.peakDb,2)+(s.truePeak?" dBTP\n":" dBFS\n");
    t+="Clipped frames: "+juce::String(juce::int64(s.clipped))+"\nLeading silence: "+juce::String(s.leading,3)+" s\nTrailing silence: "+juce::String(s.trailing,3)+" s\n\n";
    t+="Selected loudness target: "+juce::String(value("target"),1)+" LUFS\nSelected peak target: "+juce::String(value("peakTarget"),1)+" dB\n";
    t+="Peak check: "+juce::String(s.hasAudio&&s.peakDb<=value("peakTarget")?"PASS":"CHECK")+"\nLoudness check (+/- 1 LU): "+juce::String(s.hasAudio&&std::abs(s.integrated-value("target"))<=1?"PASS":"CHECK")+"\n";
    t+="\nStereo/mono BS.1770 K weighting, gated integration; EBU Tech 3342 loudness-range method. No certification claim. PRO uses a finite 8x true-peak estimate; LIVE measures sample peaks. Targets are user-selectable checks, not universal delivery requirements. This plugin does not alter audio.\n";return t;
}
juce::AudioProcessorEditor*GillMasterProcessor::createEditor(){return new GillMasterEditor(*this);}
