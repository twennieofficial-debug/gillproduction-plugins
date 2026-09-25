#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/AirDSP.h"
#include "../Source/SpaceDSP.h"
#include "../Source/EchoDSP.h"
#include "../Source/BalanceDSP.h"
#include "../Source/Presets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
constexpr int blockSize=127;
enum class Product { Air, Space, Echo, Balance };
juce::var object(){return juce::var(new juce::DynamicObject());}
void put(juce::var& o,const juce::Identifier& k,const juce::var& v){o.getDynamicObject()->setProperty(k,v);}
struct Report {
    juce::String name,module,classId; int checks=0,failures=0; double maxWetError=0; juce::var data=object();
    void check(bool ok,const juce::String& message){++checks;if(!ok){++failures;if(failures<=30)std::cerr<<name<<" FAIL: "<<message<<'\n';}}
};
struct PresetVisitor final:juce::ExtensionsVisitor {
    juce::MemoryBlock preset;
    void visitVST3Client(const VST3Client& c)override{preset=c.getPreset();}
};
juce::String classId(juce::AudioPluginInstance& p){PresetVisitor v;p.getExtensions(v);if(v.preset.getSize()<48||std::memcmp(v.preset.getData(),"VST3",4)!=0)return {};return juce::String::fromUTF8(static_cast<const char*>(v.preset.getData())+8,32);}
juce::AudioProcessorParameter* parameter(juce::AudioPluginInstance& p,const juce::String& name){for(auto* q:p.getParameters())if(q->getName(128)==name)return q;return nullptr;}
void set(Report& r,juce::AudioPluginInstance& p,const juce::String& name,float normalized){auto* q=parameter(p,name);r.check(q!=nullptr,"parameter exists: "+name);if(q)q->setValueNotifyingHost(normalized);}
float value(juce::AudioPluginInstance& p,const juce::String& name){auto* q=parameter(p,name);return q?q->getValue():-999.f;}
float norm(float x,float lo,float hi,float skew=1){return juce::NormalisableRange<float>(lo,hi,0,skew).convertTo0to1(x);}
void physical(Report& r,juce::AudioPluginInstance& p,const char* name,float x,float lo,float hi,float skew=1){set(r,p,name,norm(x,lo,hi,skew));}
void expect(Report& r,juce::AudioPluginInstance& p,const char* name,float x,float lo,float hi,float skew=1){r.check(std::abs(value(p,name)-norm(x,lo,hi,skew))<2e-5f,"preset physical value: "+juce::String(name)+" = "+juce::String(x));}
void synchronize(juce::AudioPluginInstance& p){juce::MemoryBlock scratch;p.getStateInformation(scratch);}
void prepare(juce::AudioPluginInstance& p,double fs){p.setPlayConfigDetails(2,2,fs,blockSize);p.prepareToPlay(fs,blockSize);}
std::unique_ptr<juce::AudioPluginInstance> create(Report& r,juce::VST3PluginFormat& f,const juce::PluginDescription& d,double fs){juce::String error;auto p=f.createInstanceFromDescription(d,fs,blockSize,error);r.check(p!=nullptr,"actual VST3 factory creates an instance: "+error);return p;}
void silence(juce::AudioPluginInstance& p,int samples,bool bypass=false){juce::AudioBuffer<float> b(2,blockSize);juce::MidiBuffer m;for(int at=0;at<samples;){const int n=std::min(blockSize,samples-at);b.setSize(2,n,false,false,true);b.clear();if(bypass)p.processBlockBypassed(b,m);else p.processBlock(b,m);at+=n;}}
int expectedLatency(Product p){return p==Product::Air?24:0;}
std::vector<juce::String> expectedNames(Product p){
    if(p==Product::Air)return {"MID AIR","HIGH AIR","MIX","OUTPUT","BYPASS"};
    if(p==Product::Space)return {"MIX","DECAY","PREDELAY","TONE","SIZE","WIDTH","STYLE","MIX LOCK","BYPASS","Program"};
    if(p==Product::Echo)return {"TIME","FEEDBACK","MIX","COLOR","WIDTH","STYLE","SYNC","DIVISION","TEMPO","MIX LOCK","BYPASS","Program"};
    return {"AMOUNT","TARGET","BYPASS"};
}
bool pe64(const juce::File& file){juce::MemoryBlock b;if(!file.loadFileAsData(b)||b.getSize()<64)return false;const auto* p=static_cast<const unsigned char*>(b.getData());if(p[0]!='M'||p[1]!='Z')return false;const auto offset=juce::ByteOrder::littleEndianInt(p+0x3c);return offset<=b.getSize()-6&&std::memcmp(p+offset,"PE\0\0",4)==0&&juce::ByteOrder::littleEndianShort(p+offset+4)==0x8664;}

// The host owns opaque VST3 state. For the learned-profile test only, decode
// JUCE's public IComponent state container and change documented product XML.
// The tested DLL still performs validation, storage, restoration and DSP.
struct ComponentState {std::unique_ptr<juce::XmlElement> host,processor;};
ComponentState componentState(juce::AudioPluginInstance& p){
    juce::MemoryBlock bytes;p.getStateInformation(bytes);ComponentState s;
    s.host=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
    if(s.host)if(auto* child=s.host->getChildByName("IComponent")){juce::MemoryBlock component;if(component.fromBase64Encoding(child->getAllSubText()))s.processor=juce::AudioProcessor::getXmlFromBinary(component.getData(),static_cast<int>(component.getSize()));}
    return s;
}
bool applyComponent(juce::AudioPluginInstance& p,ComponentState& s){
    if(!s.host||!s.processor)return false;auto* child=s.host->getChildByName("IComponent");if(!child)return false;
    juce::MemoryBlock inner,outer;juce::AudioProcessor::copyXmlToBinary(*s.processor,inner);child->deleteAllChildElements();child->addTextElement(inner.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary(*s.host,outer);p.setStateInformation(outer.getData(),static_cast<int>(outer.getSize()));synchronize(p);return true;
}
void writeProfile(juce::XmlElement& xml,const gill::LearnBalanceProfile& p){
    xml.setAttribute("learnVersion",static_cast<int>(p.version));xml.setAttribute("learnValid",p.valid?1:0);xml.setAttribute("learnRate",p.sampleRate);xml.setAttribute("learnRms",p.activeRmsDb);
    for(int i=0;i<8;++i){xml.setAttribute("learnBand"+juce::String(i),p.bandDb[static_cast<size_t>(i)]);xml.setAttribute("learnUse"+juce::String(i),p.validBands[static_cast<size_t>(i)]?1:0);}
}
bool injectProfile(Report& r,juce::AudioPluginInstance& p,const gill::LearnBalanceProfile& profile){auto s=componentState(p);const bool good=s.processor&&s.processor->hasTagName("GILLBALANCE_STATE");r.check(good,"actual Balance component exposes the documented serialized state");if(!good)return false;writeProfile(*s.processor,profile);const bool applied=applyComponent(p,s);r.check(applied,"learned profile is supplied through the actual VST3 state API");return applied;}
bool sameProfile(juce::AudioPluginInstance& p,const gill::LearnBalanceProfile& profile){
    auto s=componentState(p);if(!s.processor)return false;const auto& x=*s.processor;
    if(x.getIntAttribute("learnVersion")!=static_cast<int>(profile.version)||x.getIntAttribute("learnValid")!=(profile.valid?1:0)||std::abs(x.getDoubleAttribute("learnRate")-profile.sampleRate)>.01||std::abs(x.getDoubleAttribute("learnRms")-profile.activeRmsDb)>1e-4)return false;
    for(int i=0;i<8;++i)if(std::abs(x.getDoubleAttribute("learnBand"+juce::String(i))-profile.bandDb[static_cast<size_t>(i)])>1e-4||x.getIntAttribute("learnUse"+juce::String(i))!=(profile.validBands[static_cast<size_t>(i)]?1:0))return false;return true;
}
gill::LearnBalanceProfile learnedFixture(){
    gill::BalanceDSP engine;engine.prepare(48000,blockSize,2);engine.startLearning();std::array<std::array<float,blockSize>,2> samples{};std::array<float*,2> ptr{samples[0].data(),samples[1].data()};
    for(int at=0;at<490000;at+=blockSize){const int n=std::min(blockSize,490000-at);for(int i=0;i<n;++i){const double t=(at+i)/48000.;double x=0;for(int band=0;band<8;++band)x+=.075*std::pow(.76,band)*std::sin(2*pi*gill::BalanceDSP::centersHz[static_cast<size_t>(band)]*t+.21*band);x+=.08*std::sin(2*pi*431*t);samples[0][static_cast<size_t>(i)]=static_cast<float>(x);samples[1][static_cast<size_t>(i)]=static_cast<float>(-.71*x);}engine.process(ptr.data(),2,n);}
    return engine.learnedProfile();
}

void presetPrograms(Report& r,juce::AudioPluginInstance& p,Product product){
    if(product!=Product::Space&&product!=Product::Echo){r.check(p.getNumPrograms()<=1,"single-default product exposes no unexpected preset bank");return;}
    r.check(p.getNumPrograms()==12,"twelve actual VST3 programs are exposed");
    for(int locked=0;locked<2;++locked){set(r,p,"MIX LOCK",static_cast<float>(locked));set(r,p,"MIX",.371f);if(product==Product::Echo)physical(r,p,"TEMPO",93,20,300);synchronize(p);p.setCurrentProgram(11);synchronize(p);silence(p,256);synchronize(p);
        for(int i=0;i<12;++i){p.setCurrentProgram(i);synchronize(p);silence(p,256);synchronize(p);r.check(p.getCurrentProgram()==i,"host program selection is reflected");
            if(product==Product::Space){const auto& x=gill::spacePresets[static_cast<size_t>(i)];r.check(p.getProgramName(i)==x.name,"Space preset name");expect(r,p,"MIX",locked?37.1f:x.mix,0,100);expect(r,p,"DECAY",x.decay,.2f,15,.45f);expect(r,p,"PREDELAY",x.pre,0,200);expect(r,p,"TONE",x.tone,0,100);expect(r,p,"SIZE",x.size,0,100);expect(r,p,"WIDTH",x.width,0,100);expect(r,p,"STYLE",static_cast<float>(x.style),0,2);}
            else{const auto& x=gill::echoPresets[static_cast<size_t>(i)];r.check(p.getProgramName(i)==x.name,"Echo preset name");expect(r,p,"MIX",locked?37.1f:x.mix,0,100);expect(r,p,"TIME",x.time,1,8000,.4f);expect(r,p,"FEEDBACK",x.feedback,0,90);expect(r,p,"COLOR",x.color,0,100);expect(r,p,"WIDTH",x.width,0,100);expect(r,p,"STYLE",static_cast<float>(x.style),0,2);expect(r,p,"SYNC",x.sync?1.f:0.f,0,1);expect(r,p,"DIVISION",static_cast<float>(x.division),0,7);expect(r,p,"TEMPO",93,20,300);}
            r.check(value(p,"MIX LOCK")==locked,"preset preserves mix-lock state");
        }
    }
    put(r.data,"all_twelve_presets_and_mix_lock_checked",true);
}
void metadata(Report& r,juce::AudioPluginInstance& p,const juce::PluginDescription& desc,Product product){
    r.check(sizeof(void*)==8,"test host is 64-bit");r.check(desc.name==r.name&&p.getName()==r.name,"factory and processor identify intended product");r.check(desc.manufacturerName=="GILLPRODUCTION","manufacturer identity");r.check(!desc.isInstrument,"factory exposes an audio effect");r.check(desc.version=="0.1.0","candidate version is 0.1.0");
    const auto names=expectedNames(product);r.check(p.getParameters().size()==static_cast<int>(names.size()),"exact VST3 parameter count including applicable Program selector");std::set<std::string> ids;juce::Array<juce::var> rows;
    for(const auto& name:names){int found=0;for(auto* q:p.getParameters())if(q->getName(128)==name)++found;r.check(found==1,"exactly one correctly named parameter: "+name);}
    for(int i=0;i<p.getParameters().size();++i){auto* q=p.getHostedParameter(i);r.check(q!=nullptr,"hosted parameter metadata exists");if(!q)continue;auto row=object();put(row,"name",q->getName(128));put(row,"id",q->getParameterID());put(row,"default_normalized",q->getDefaultValue());put(row,"steps",q->getNumSteps());rows.add(row);r.check(q->getParameterID().isNotEmpty()&&ids.insert(q->getParameterID().toStdString()).second,"parameter IDs nonempty and unique");}
    put(r.data,"parameters",juce::var(rows));put(r.data,"parameter_count",p.getParameters().size());put(r.data,"host_program_count",p.getNumPrograms());put(r.data,"version",desc.version);put(r.data,"description_unique_id",desc.uniqueId);r.classId=classId(p);r.check(r.classId.length()==32&&r.classId.containsOnly("0123456789abcdefABCDEF"),"complete 128-bit VST3 factory class ID");r.check(p.getBypassParameter()!=nullptr,"native bypass parameter is exposed");presetPrograms(r,p,product);
}
void configureWet(Report& r,juce::AudioPluginInstance& p,Product product){
    set(r,p,"BYPASS",0);
    if(product==Product::Air){set(r,p,"MID AIR",.63f);set(r,p,"HIGH AIR",.78f);set(r,p,"MIX",.75f);physical(r,p,"OUTPUT",-2,-18,6);}
    else if(product==Product::Space){set(r,p,"MIX",.63f);physical(r,p,"DECAY",2.4f,.2f,15,.45f);physical(r,p,"PREDELAY",31,0,200);set(r,p,"TONE",.68f);set(r,p,"SIZE",.72f);set(r,p,"WIDTH",.93f);set(r,p,"STYLE",.5f);set(r,p,"MIX LOCK",0);}
    else if(product==Product::Echo){physical(r,p,"TIME",237,1,8000,.4f);physical(r,p,"FEEDBACK",42,0,90);set(r,p,"MIX",.63f);set(r,p,"COLOR",.38f);set(r,p,"WIDTH",.91f);set(r,p,"STYLE",1);set(r,p,"SYNC",0);set(r,p,"DIVISION",4.f/7);physical(r,p,"TEMPO",93,20,300);set(r,p,"MIX LOCK",0);}
    else{set(r,p,"AMOUNT",.85f);set(r,p,"TARGET",.75f);}
    synchronize(p);
}
void roundtrip(Report& r,juce::VST3PluginFormat& f,const juce::PluginDescription& d,juce::AudioPluginInstance& p,Product product,const gill::LearnBalanceProfile& profile){
    configureWet(r,p,product);if(product==Product::Balance)injectProfile(r,p,profile);silence(p,512);synchronize(p);if(product==Product::Balance)r.check(sameProfile(p,profile),"actual VST3 accepts and saves the independently learned profile");
    std::vector<float> saved;for(auto* q:p.getParameters())saved.push_back(q->getValue());const int program=p.getCurrentProgram();juce::MemoryBlock state;p.getStateInformation(state);r.check(state.getSize()>0,"actual binary serializes opaque host state");
    for(const auto& name:expectedNames(product))if(name!="Program")set(r,p,name,value(p,name)>.5f?0.f:1.f);synchronize(p);p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));synchronize(p);
    for(int i=0;i<std::min(static_cast<int>(saved.size()),p.getParameters().size());++i)r.check(std::abs(p.getParameters()[i]->getValue()-saved[static_cast<size_t>(i)])<2e-5f,"opaque state restores every parameter in same instance");
    auto reopened=create(r,f,d,48000);if(reopened){reopened->setStateInformation(state.getData(),static_cast<int>(state.getSize()));prepare(*reopened,48000);silence(*reopened,512);synchronize(*reopened);r.check(reopened->getParameters().size()==static_cast<int>(saved.size()),"reopened binary has same parameter schema");for(int i=0;i<std::min(static_cast<int>(saved.size()),reopened->getParameters().size());++i)r.check(std::abs(reopened->getParameters()[i]->getValue()-saved[static_cast<size_t>(i)])<2e-5f,"fresh VST3 instance restores every saved parameter");r.check(reopened->getCurrentProgram()==program,"fresh instance restores program");r.check(classId(*reopened)==r.classId,"factory ID stable after reload");
        if(product==Product::Balance){r.check(sameProfile(*reopened,profile),"fresh actual VST3 restores all learned profile fields");auto malformed=componentState(*reopened);if(malformed.processor){malformed.processor->setAttribute("learnVersion",99);r.check(applyComponent(*reopened,malformed),"malformed profile is presented to actual VST3 state validator");silence(*reopened,256);r.check(sameProfile(*reopened,profile),"invalid learned-profile version does not replace existing valid profile");}put(r.data,"learned_profile_state_and_rejection_checked",true);}
        reopened->releaseResources();}
}

void dryRoutes(Report& r,juce::VST3PluginFormat& f,const juce::PluginDescription& d,Product product,const gill::LearnBalanceProfile& profile){
    juce::Array<juce::var> rows;const int latency=expectedLatency(product);
    for(double fs:{44100.,48000.,96000.,192000.})for(int route=0;route<((product==Product::Air||product==Product::Balance)?4:3);++route){auto p=create(r,f,d,fs);if(!p)continue;configureWet(r,*p,product);if(product==Product::Balance&&route!=3)injectProfile(r,*p,profile);
        if(route==0){set(r,*p,product==Product::Balance?"AMOUNT":"MIX",0);if(product==Product::Air)physical(r,*p,"OUTPUT",0,-18,6);}
        else if(route==1)set(r,*p,"BYPASS",1);
        else if(route==3&&product==Product::Air){set(r,*p,"MID AIR",0);set(r,*p,"HIGH AIR",0);set(r,*p,"MIX",1);physical(r,*p,"OUTPUT",0,-18,6);}
        synchronize(*p);prepare(*p,fs);r.check(p->getLatencySamples()==latency,"reported VST3 latency agrees with contract at this rate");silence(*p,std::max(latency+4096,static_cast<int>(fs*.15)),route==2);
        for(int channel=0;channel<2;++channel){juce::AudioBuffer<float> b(2,blockSize);juce::MidiBuffer midi;const int total=latency+1024;int nonzero=0,position=-1;float peak=0;bool exact=true,isolated=true,finite=true;
            for(int at=0;at<total;){const int n=std::min(blockSize,total-at);b.setSize(2,n,false,false,true);b.clear();if(at==0)b.setSample(channel,0,.25f);if(route==2)p->processBlockBypassed(b,midi);else p->processBlock(b,midi);for(int i=0;i<n;++i){const float x=b.getSample(channel,i);finite=finite&&std::isfinite(x);exact=exact&&x==(at+i==latency?.25f:0.f);isolated=isolated&&b.getSample(1-channel,i)==0;if(x!=0)++nonzero;if(std::abs(x)>peak){peak=std::abs(x);position=at+i;}}at+=n;}
            r.check(finite&&exact,"dry/bypass impulse sample-exact at reported latency");r.check(isolated,"dry/bypass preserves channel isolation");r.check(nonzero==1&&position==latency&&peak==.25f,"impulse amplitude count and position agree with host delay");auto row=object();put(row,"sample_rate",fs);put(row,"route",route==0?"zero_amount_or_mix":route==1?"native_bypass_parameter":route==2?"host_processBlockBypassed":product==Product::Air?"zero_enhancement":"unlearned_profile");put(row,"input_channel",channel);put(row,"reported_latency",p->getLatencySamples());put(row,"measured_impulse_sample",position);put(row,"impulse_peak",peak);rows.add(row);
        }
        if(route==1||route==2){const int frames=static_cast<int>(fs*.30),total=frames+latency;auto original=[fs,frames](int frame){if(frame<0||frame>=frames)return 0.f;const double t=frame/fs;return static_cast<float>(.24*std::sin(2*pi*237*t)+.065*std::sin(2*pi*3791*t));};juce::AudioBuffer<float> b(2,blockSize);juce::MidiBuffer midi;bool exact=true;
            for(int at=0;at<total;){const int n=std::min(blockSize,total-at);b.setSize(2,n,false,false,true);for(int i=0;i<n;++i){const float x=original(at+i);b.setSample(0,i,x);b.setSample(1,i,-.63f*x);}if(route==2)p->processBlockBypassed(b,midi);else p->processBlock(b,midi);for(int i=0;i<n;++i){const float x=original(at+i-latency);exact=exact&&b.getSample(0,i)==x&&b.getSample(1,i)==-.63f*x;}at+=n;}r.check(exact,"native/host bypass preserve sustained processed-band audio sample exactly");}
        p->releaseResources();
    }
    put(r.data,"dry_route_measurements",juce::var(rows));put(r.data,"sustained_audio_bypass_checked",true);
}
void wetComparison(Report& r,juce::VST3PluginFormat& f,const juce::PluginDescription& d,Product product,const gill::LearnBalanceProfile& profile){
    juce::Array<juce::var> rows;
    for(double fs:{44100.,48000.,96000.,192000.}){auto p=create(r,f,d,fs);if(!p)continue;configureWet(r,*p,product);if(product==Product::Balance)injectProfile(r,*p,profile);prepare(*p,fs);
        gill::AirDSP air;gill::SpaceDSP space;gill::EchoDSP echo;gill::BalanceDSP balance;
        if(product==Product::Air){air.setParameters(63,78,75,-2);air.prepare(fs,blockSize,2);}
        else if(product==Product::Space){space.setParameters(63,2.4f,31,68,72,93,1);space.prepare(fs,blockSize,2);}
        else if(product==Product::Echo){echo.setParameters(237,42,63,38,91,2);echo.prepare(fs,blockSize,2);}
        else{balance.setParameters(85,3);balance.prepare(fs,blockSize,2);r.check(balance.setLearnedProfile(profile),"independent direct engine accepts learned fixture");}
        auto direct=[&](juce::AudioBuffer<float>& b){std::array<float*,2> ptr{b.getWritePointer(0),b.getWritePointer(1)};if(product==Product::Air)air.process(ptr.data(),2,b.getNumSamples());else if(product==Product::Space)space.process(ptr.data(),2,b.getNumSamples());else if(product==Product::Echo)echo.process(ptr.data(),2,b.getNumSamples());else balance.process(ptr.data(),2,b.getNumSamples());};
        juce::AudioBuffer<float> actual(2,blockSize),reference(2,blockSize);juce::MidiBuffer midi;for(int k=0;k<200;++k){actual.clear();reference.clear();p->processBlock(actual,midi);direct(reference);}double maxError=0,square=0,wetDifference=0;std::uint64_t count=0;bool finite=true;const int frames=static_cast<int>(fs*2);
        for(int at=0;at<frames;){const int n=std::min(blockSize,frames-at);actual.setSize(2,n,false,false,true);reference.setSize(2,n,false,false,true);std::array<std::array<float,blockSize>,2> input{};
            for(int i=0;i<n;++i){const double t=(at+i)/fs;const double env=t<.03?t/.03:(t>1.7?std::max(0.,(2-t)/.3):.72+.28*std::sin(2*pi*1.3*t));const float x=static_cast<float>(env*(.18*std::sin(2*pi*237*t)+.08*std::sin(2*pi*431*t)+.06*std::sin(2*pi*3791*t)+.035*std::sin(2*pi*7103*t)));for(int c=0;c<2;++c){const float sample=c==0?x:-.73f*x;input[static_cast<size_t>(c)][static_cast<size_t>(i)]=sample;actual.setSample(c,i,sample);reference.setSample(c,i,sample);}}
            p->processBlock(actual,midi);direct(reference);for(int c=0;c<2;++c)for(int i=0;i<n;++i){const double a=actual.getSample(c,i),b=reference.getSample(c,i),error=std::abs(a-b);finite=finite&&std::isfinite(a)&&std::isfinite(b);maxError=std::max(maxError,error);square+=error*error;wetDifference=std::max(wetDifference,std::abs(a-input[static_cast<size_t>(c)][static_cast<size_t>(i)]));++count;}at+=n;
        }
        r.maxWetError=std::max(r.maxWetError,maxError);r.check(finite,"actual/direct wet renders remain finite");r.check(maxError<=1e-5,"actual VST3 matches independent direct engine within 1e-5 at "+juce::String(fs));r.check(wetDifference>1e-4,"wet comparison exercises audible processing, not a dry fallback");auto row=object();put(row,"sample_rate",fs);put(row,"channels",2);put(row,"frames",frames);put(row,"silence_settling_frames",200*blockSize);put(row,"maximum_absolute_error",maxError);put(row,"rms_error",std::sqrt(square/std::max<std::uint64_t>(1,count)));put(row,"maximum_wet_dry_difference",wetDifference);put(row,"tolerance",1e-5);rows.add(row);p->releaseResources();
    }
    put(r.data,"wet_comparisons",juce::var(rows));
}
struct TempoPlayHead final:juce::AudioPlayHead {
    double bpm=120;bool available=true;
    juce::Optional<PositionInfo> getPosition()const override{PositionInfo p;p.setTimeInSamples(0);p.setIsPlaying(true);if(available)p.setBpm(bpm);return p;}
};
void echoTempo(Report& r,juce::VST3PluginFormat& f,const juce::PluginDescription& d){
    struct Case{const char* name;bool host;double bpm;float fallback;int division;bool sync;float freeMs;double expectedMs;};
    const Case cases[]{
        {"host quarter at 120",true,120,73,4,true,237,500},
        {"host quarter at 60",true,60,120,4,true,237,1000},
        {"host eighth triplet at 90",true,90,120,1,true,237,60000./90/3},
        {"host dotted eighth at 120",true,120,120,3,true,237,375},
        {"manual tempo without host",false,0,80,4,true,237,750},
        {"invalid host uses manual tempo",true,1001,100,4,true,237,600},
        {"long sync is bounded to 8000ms",true,20,120,7,true,237,8000},
        {"free milliseconds ignore host tempo",true,90,120,4,false,237,237}
    };
    juce::Array<juce::var> rows;
    for(const auto& test:cases){auto p=create(r,f,d,48000);if(!p)continue;TempoPlayHead head;head.bpm=test.bpm;head.available=test.host;p->setPlayHead(&head);configureWet(r,*p,Product::Echo);set(r,*p,"MIX",1);set(r,*p,"FEEDBACK",0);set(r,*p,"COLOR",0);set(r,*p,"WIDTH",1);set(r,*p,"STYLE",0);set(r,*p,"SYNC",test.sync?1.f:0.f);set(r,*p,"DIVISION",static_cast<float>(test.division)/7);physical(r,*p,"TIME",test.freeMs,1,8000,.4f);physical(r,*p,"TEMPO",test.fallback,20,300);synchronize(*p);prepare(*p,48000);silence(*p,4800);
        const double expected=test.expectedMs*48;const int frames=static_cast<int>(std::ceil(expected))+256;int onset=-1;double peak=0;juce::AudioBuffer<float> b(2,blockSize);juce::MidiBuffer midi;
        for(int at=0;at<frames;){const int n=std::min(blockSize,frames-at);b.setSize(2,n,false,false,true);b.clear();if(at==0)b.setSample(0,0,.25f);p->processBlock(b,midi);for(int i=0;i<n;++i){const double a=std::abs(b.getSample(0,i));if(onset<0&&a>1e-6)onset=at+i;peak=std::max(peak,a);}at+=n;}
        r.check(onset>=0&&std::abs(onset-expected)<=1.01,"actual host tempo produces correct echo onset: "+juce::String(test.name));r.check(peak>1e-4,"host-tempo test produces a real delayed impulse");auto row=object();put(row,"case",test.name);put(row,"host_bpm_present",test.host);put(row,"host_bpm",test.bpm);put(row,"fallback_bpm",test.fallback);put(row,"expected_delay_ms",test.expectedMs);put(row,"expected_onset_sample",expected);put(row,"measured_onset_sample",onset);put(row,"peak",peak);rows.add(row);p->setPlayHead(nullptr);p->releaseResources();
    }
    put(r.data,"host_tempo_impulse_measurements",juce::var(rows));
}
void test(Report& r,juce::VST3PluginFormat& f,const juce::File& bundle,Product product,const gill::LearnBalanceProfile& profile){
    r.module=bundle.getFullPathName();r.check(bundle.exists(),"candidate module exists");const auto binary=bundle.isDirectory()?bundle.getChildFile("Contents/x86_64-win/"+bundle.getFileName()):bundle;r.check(pe64(binary),"candidate module is PE x86-64");juce::OwnedArray<juce::PluginDescription> descriptions;f.findAllTypesForFile(descriptions,r.module);r.check(descriptions.size()==1,"bundle exports exactly one discoverable audio processor");if(descriptions.size()!=1)return;auto p=create(r,f,*descriptions[0],48000);if(!p)return;prepare(*p,48000);metadata(r,*p,*descriptions[0],product);roundtrip(r,f,*descriptions[0],*p,product,profile);p->releaseResources();p.reset();dryRoutes(r,f,*descriptions[0],product,profile);wetComparison(r,f,*descriptions[0],product,profile);if(product==Product::Echo)echoTempo(r,f,*descriptions[0]);
}
}
int main(int argc,char** argv){
    if(argc!=5){std::cerr<<"Usage: GillEffectVst3HostTests GILLAIR.vst3 GILLSPACE.vst3 GILLECHO.vst3 GILLBALANCE.vst3\n";return 2;}
    juce::ScopedJuceInitialiser_GUI gui;juce::VST3PluginFormat format;std::array<Report,4> reports;const char* names[]{"GILLAIR","GILLSPACE","GILLECHO","GILLBALANCE"};const auto profile=learnedFixture();if(!profile.valid){std::cerr<<"Independent ten-second learned fixture did not become valid\n";return 3;}
    for(int i=0;i<4;++i){reports[static_cast<size_t>(i)].name=names[i];test(reports[static_cast<size_t>(i)],format,juce::File(juce::String(argv[i+1])),static_cast<Product>(i),profile);}
    int failures=0;for(int i=0;i<4;++i){auto& r=reports[static_cast<size_t>(i)];for(int j=0;j<4;++j)if(i!=j)r.check(r.classId.isNotEmpty()&&reports[static_cast<size_t>(j)].classId.isNotEmpty()&&r.classId!=reports[static_cast<size_t>(j)].classId,"four products have distinct complete VST3 factory IDs");put(r.data,"name",r.name);put(r.data,"module_path",r.module);put(r.data,"class_id",r.classId);put(r.data,"host_bits",static_cast<int>(sizeof(void*)*8));put(r.data,"checks",r.checks);put(r.data,"failures",r.failures);put(r.data,"passed",r.failures==0);put(r.data,"unique_factory_ids_checked",true);put(r.data,"scope","Actual x64 VST3 factory/discovery, metadata, all presets with MIX LOCK, opaque state roundtrip, four-rate exact dry/native/host bypass and independent wet engine comparisons. Echo host-BPM impulse timing and Balance independently learned profile state persistence/rejection. Balance LEARN UI, FL Studio UI, and subjective listening are not covered.");const auto destination=juce::File::getCurrentWorkingDirectory().getChildFile(r.name+"-vst3-host-report.json");if(!destination.replaceWithText(juce::JSON::toString(r.data,true)+"\n")){std::cerr<<"Could not write "<<destination.getFullPathName()<<'\n';++r.failures;}std::cout<<r.name<<": "<<r.checks<<" checks, "<<r.failures<<" failures, max wet error "<<r.maxWetError<<'\n';failures+=r.failures;}return failures?1:0;
}
