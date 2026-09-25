#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/FlowDSP.h"
#include "../Source/HeatDSP.h"
#include "../Source/TuneDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
constexpr int blockSize=127;
enum class Product {Flow,Heat,Tune,TuneLive};
juce::var object(){return juce::var(new juce::DynamicObject());}
void put(juce::var& o,const juce::Identifier& key,const juce::var& value){o.getDynamicObject()->setProperty(key,value);}
struct Report {
    juce::String name,module,classId;int checks=0,failures=0;double maxWetError=0;juce::var data=object();
    void check(bool ok,const juce::String& message){++checks;if(!ok){++failures;if(failures<=30)std::cerr<<name<<" FAIL: "<<message<<'\n';}}
};
struct PresetVisitor final:juce::ExtensionsVisitor {juce::MemoryBlock preset;void visitVST3Client(const VST3Client& client)override{preset=client.getPreset();}};
juce::String classId(juce::AudioPluginInstance& instance){PresetVisitor visitor;instance.getExtensions(visitor);const auto& b=visitor.preset;if(b.getSize()<48||std::memcmp(b.getData(),"VST3",4)!=0)return {};return juce::String::fromUTF8(static_cast<const char*>(b.getData())+8,32);}
juce::AudioProcessorParameter* parameter(juce::AudioPluginInstance& instance,const juce::String& name){for(auto* p:instance.getParameters())if(p->getName(128)==name)return p;return nullptr;}
void set(Report& r,juce::AudioPluginInstance& instance,const juce::String& name,float normalized){auto* p=parameter(instance,name);r.check(p!=nullptr,"parameter exists: "+name);if(p)p->setValueNotifyingHost(normalized);}
float value(juce::AudioPluginInstance& instance,const juce::String& name){auto* p=parameter(instance,name);return p?p->getValue():-999.f;}
void synchronize(juce::AudioPluginInstance& instance){
    // Public host state access flushes JUCE's pending parameter dispatcher;
    // this avoids relying on a desktop message-loop scheduling delay.
    juce::MemoryBlock scratch;instance.getStateInformation(scratch);
}
void prepare(juce::AudioPluginInstance& instance,double fs){instance.setPlayConfigDetails(2,2,fs,blockSize);instance.prepareToPlay(fs,blockSize);}
std::unique_ptr<juce::AudioPluginInstance> create(Report& r,juce::VST3PluginFormat& format,const juce::PluginDescription& description,double fs){juce::String error;auto plugin=format.createInstanceFromDescription(description,fs,blockSize,error);r.check(plugin!=nullptr,"actual VST3 factory creates an instance: "+error);return plugin;}
void silence(juce::AudioPluginInstance& instance,int samples,bool bypass=false){juce::AudioBuffer<float> buffer(2,blockSize);juce::MidiBuffer midi;for(int done=0;done<samples;done+=blockSize){buffer.clear();if(bypass)instance.processBlockBypassed(buffer,midi);else instance.processBlock(buffer,midi);}}
int expectedLatency(Product product,double fs){if(product==Product::Flow)return 0;if(product==Product::Heat)return gill::HeatDSP::fixedLatencySamples;if(product==Product::TuneLive)return static_cast<int>(std::ceil(fs*.016));return fs==44100||fs==48000?2048:fs==96000?4096:8192;}
std::vector<juce::String> expectedNames(Product p){if(p==Product::Flow)return {"AMOUNT","MODE","AUTO GAIN","BYPASS"};if(p==Product::Heat)return {"LOW DRIVE","MID DRIVE","HIGH DRIVE","STYLE","MIX","OUTPUT","BYPASS"};return {"KEY","SCALE","RETUNE","HUMANIZE","MIX","BYPASS","Program"};}
bool pe64(const juce::File& candidate){juce::MemoryBlock bytes;if(!candidate.loadFileAsData(bytes)||bytes.getSize()<64)return false;const auto* data=static_cast<const unsigned char*>(bytes.getData());if(data[0]!='M'||data[1]!='Z')return false;const auto offset=juce::ByteOrder::littleEndianInt(data+0x3c);return offset<=bytes.getSize()-6&&std::memcmp(data+offset,"PE\0\0",4)==0&&juce::ByteOrder::littleEndianShort(data+offset+4)==0x8664;}

void metadata(Report& r,juce::AudioPluginInstance& plugin,const juce::PluginDescription& desc,Product product){
    r.check(sizeof(void*)==8,"test host is 64-bit");r.check(desc.name==r.name&&plugin.getName()==r.name,"factory and loaded processor identify the intended product");r.check(desc.manufacturerName=="GILLPRODUCTION","manufacturer identity");r.check(!desc.isInstrument,"factory exposes an audio effect");r.check(desc.version=="0.3.0","candidate version is 0.3.0");
    const auto names=expectedNames(product);r.check(plugin.getParameters().size()==static_cast<int>(names.size()),"exact exposed VST3 parameter count, including Tune Program selector");std::set<std::string> ids;juce::Array<juce::var> rows;
    for(const auto& name:names){int found=0;for(auto* p:plugin.getParameters())if(p->getName(128)==name)++found;r.check(found==1,"exactly one correctly named parameter: "+name);}
    for(int i=0;i<plugin.getParameters().size();++i){auto* p=plugin.getHostedParameter(i);r.check(p!=nullptr,"hosted parameter metadata exists");if(!p)continue;auto row=object();put(row,"name",p->getName(128));put(row,"id",p->getParameterID());put(row,"default_normalized",p->getDefaultValue());put(row,"steps",p->getNumSteps());rows.add(row);r.check(p->getParameterID().isNotEmpty()&&ids.insert(p->getParameterID().toStdString()).second,"parameter IDs are nonempty and unique");}
    put(r.data,"parameters",juce::var(rows));put(r.data,"parameter_count",plugin.getParameters().size());put(r.data,"host_program_count",plugin.getNumPrograms());put(r.data,"version",desc.version);put(r.data,"description_unique_id",desc.uniqueId);r.classId=classId(plugin);
    r.check(r.classId.length()==32&&r.classId.containsOnly("0123456789abcdefABCDEF"),"factory supplies a complete 128-bit VST3 class ID");r.check(plugin.getBypassParameter()!=nullptr,"VST3 native bypass parameter is exposed");
    if(product==Product::Tune||product==Product::TuneLive){r.check(plugin.getNumPrograms()==5,"five actual VST3 programs are exposed");const char* programs[]{"NATURAL","POP","RAP","TRAP","ROBOT"};const float retune[]{100,40,15,5,0},human[]{80,45,20,5,0};set(r,plugin,"KEY",9.f/11);set(r,plugin,"SCALE",1);synchronize(plugin);
        for(int i=0;i<5;++i){r.check(plugin.getProgramName(i)==programs[i],"correct preset name");plugin.setCurrentProgram(i);synchronize(plugin);silence(plugin,256);synchronize(plugin);r.check(plugin.getCurrentProgram()==i,"host program selection is reflected");r.check(std::abs(value(plugin,"RETUNE")-retune[i]/200)<1e-5&&std::abs(value(plugin,"HUMANIZE")-human[i]/100)<1e-5&&value(plugin,"MIX")==1,"preset changes the real retune humanize and mix parameters");r.check(std::abs(value(plugin,"KEY")-9.f/11)<1e-5&&value(plugin,"SCALE")==1,"every preset preserves the chosen key and scale");}}
    else r.check(plugin.getNumPrograms()<=1,"single-default products expose no unexpected preset banks");
}
void configureWet(Report& r,juce::AudioPluginInstance& plugin,Product product){
    set(r,plugin,"BYPASS",0);
    if(product==Product::Flow){set(r,plugin,"AMOUNT",.73f);set(r,plugin,"MODE",.5f);set(r,plugin,"AUTO GAIN",0);}
    else if(product==Product::Heat){set(r,plugin,"LOW DRIVE",9.f/24);set(r,plugin,"MID DRIVE",12.f/24);set(r,plugin,"HIGH DRIVE",6.f/24);set(r,plugin,"STYLE",.5f);set(r,plugin,"MIX",.75f);set(r,plugin,"OUTPUT",22.f/36);}
    else{set(r,plugin,"KEY",2.f/11);set(r,plugin,"SCALE",1);set(r,plugin,"RETUNE",15.f/200);set(r,plugin,"HUMANIZE",.2f);set(r,plugin,"MIX",1);}
    synchronize(plugin);
}
void roundtrip(Report& r,juce::VST3PluginFormat& format,const juce::PluginDescription& description,juce::AudioPluginInstance& plugin,Product product){
    configureWet(r,plugin,product);silence(plugin,512);synchronize(plugin);std::vector<float> saved;for(auto* p:plugin.getParameters())saved.push_back(p->getValue());const int program=plugin.getCurrentProgram();juce::MemoryBlock state;plugin.getStateInformation(state);r.check(state.getSize()>0,"actual binary serializes opaque host state");
    for(const auto& name:expectedNames(product))if(name!="Program")set(r,plugin,name,value(plugin,name)>.5f?0.f:1.f);synchronize(plugin);plugin.setStateInformation(state.getData(),static_cast<int>(state.getSize()));synchronize(plugin);
    for(int i=0;i<std::min(static_cast<int>(saved.size()),plugin.getParameters().size());++i)r.check(std::abs(plugin.getParameters()[i]->getValue()-saved[i])<1e-5,"opaque state restores every exposed parameter in the same instance");
    auto reopened=create(r,format,description,48000);if(reopened){reopened->setStateInformation(state.getData(),static_cast<int>(state.getSize()));prepare(*reopened,48000);silence(*reopened,512);synchronize(*reopened);r.check(reopened->getParameters().size()==static_cast<int>(saved.size()),"reopened binary has the same parameter schema");for(int i=0;i<std::min(static_cast<int>(saved.size()),reopened->getParameters().size());++i)r.check(std::abs(reopened->getParameters()[i]->getValue()-saved[i])<1e-5,"fresh VST3 instance restores every saved parameter");r.check(reopened->getCurrentProgram()==program,"fresh VST3 instance restores selected program");r.check(classId(*reopened)==r.classId,"factory class ID is stable after state reload");reopened->releaseResources();}
}
void dryRoutes(Report& r,juce::VST3PluginFormat& format,const juce::PluginDescription& description,Product product){juce::Array<juce::var> measurements;
    for(double fs:{44100.,48000.,96000.,192000.}){const auto expected=expectedLatency(product,fs);for(int route=0;route<(product==Product::Heat?4:3);++route){auto plugin=create(r,format,description,fs);if(!plugin)continue;
        if(route==0){if(product==Product::Flow)set(r,*plugin,"AMOUNT",0);else set(r,*plugin,"MIX",0);if(product==Product::Heat)set(r,*plugin,"OUTPUT",24.f/36);}
        else if(route==1)set(r,*plugin,"BYPASS",1);
        else if(route==3){set(r,*plugin,"LOW DRIVE",0);set(r,*plugin,"MID DRIVE",0);set(r,*plugin,"HIGH DRIVE",0);set(r,*plugin,"MIX",1);set(r,*plugin,"OUTPUT",24.f/36);}
        synchronize(*plugin);prepare(*plugin,fs);r.check(plugin->getLatencySamples()==expected,"reported VST3 latency agrees with product contract at this rate");silence(*plugin,std::max(expected+4096,static_cast<int>(fs)),route==2);
        for(int impulseChannel=0;impulseChannel<2;++impulseChannel){juce::AudioBuffer<float> buffer(2,blockSize);juce::MidiBuffer midi;const int total=expected+1024;int nonzero=0,position=-1;float peak=0;
            for(int at=0;at<total;){const int n=std::min(blockSize,total-at);buffer.setSize(2,n,false,false,true);buffer.clear();if(at==0)buffer.setSample(impulseChannel,0,.25f);if(route==2)plugin->processBlockBypassed(buffer,midi);else plugin->processBlock(buffer,midi);
                for(int i=0;i<n;++i){const float sample=buffer.getSample(impulseChannel,i);r.check(std::isfinite(sample),"dry and native bypass outputs stay finite");r.check(sample==(at+i==expected?.25f:0.f),"actual binary dry/bypass impulse is sample-exact at its reported latency");r.check(buffer.getSample(1-impulseChannel,i)==0,"actual binary dry/bypass preserves left/right channel isolation");if(sample!=0)++nonzero;if(std::abs(sample)>peak){peak=std::abs(sample);position=at+i;}}
                at+=n;}
            r.check(nonzero==1&&position==expected&&peak==.25f,"measured impulse amplitude position and count agree with the host delay");auto row=object();put(row,"sample_rate",fs);put(row,"route",route==0?"zero_amount_or_mix":route==1?"native_bypass_parameter":route==2?"host_processBlockBypassed":"zero_drives");put(row,"input_channel",impulseChannel);put(row,"reported_latency",plugin->getLatencySamples());put(row,"measured_impulse_sample",position);put(row,"impulse_peak",peak);measurements.add(row);}
        if(route==1||route==2){
            // A lone impulse is naturally unvoiced in Tune. Sustained detuned
            // harmonics ensure an ignored bypass cannot pass this check merely
            // because the pitch detector returned the normal dry/unvoiced path.
            const int sourceFrames=static_cast<int>(fs*.30),total=sourceFrames+expected;
            const auto original=[fs,sourceFrames](int frame){if(frame<0||frame>=sourceFrames)return 0.f;const double t=frame/fs,f=220*std::pow(2.,.37/12);return static_cast<float>(.24*std::sin(2*pi*f*t)+.065*std::sin(4*pi*f*t));};
            juce::AudioBuffer<float> voice(2,blockSize);juce::MidiBuffer midi;
            for(int at=0;at<total;){const int n=std::min(blockSize,total-at);voice.setSize(2,n,false,false,true);for(int i=0;i<n;++i){const float x=original(at+i);voice.setSample(0,i,x);voice.setSample(1,i,-.63f*x);}if(route==2)plugin->processBlockBypassed(voice,midi);else plugin->processBlock(voice,midi);
                for(int i=0;i<n;++i){const float dry=original(at+i-expected);r.check(voice.getSample(0,i)==dry&&voice.getSample(1,i)==-.63f*dry,"native and host bypass preserve sustained detuned vocal audio sample exactly");}at+=n;}
        }
        plugin->releaseResources();}}
    put(r.data,"dry_route_measurements",juce::var(measurements));
    put(r.data,"sustained_vocal_bypass_checked",true);
}
void wetComparison(Report& r,juce::VST3PluginFormat& format,const juce::PluginDescription& description,Product product){auto plugin=create(r,format,description,48000);if(!plugin)return;configureWet(r,*plugin,product);prepare(*plugin,48000);
    gill::FlowDSP flow;gill::HeatDSP heat;gill::TuneDSP tune;
    if(product==Product::Flow){flow.setParameters(73,1,false);flow.prepare(48000,blockSize,2);}
    else if(product==Product::Heat){heat.setParameters(9,12,6,1,75,-2);heat.prepare(48000,blockSize,2);}
    else{tune.setQualityMode(product==Product::TuneLive?1:0);tune.setParameters(2,2,15,20,100);tune.prepare(48000,blockSize,2);}
    auto direct=[&](juce::AudioBuffer<float>& b){std::array<float*,2> pointers{b.getWritePointer(0),b.getWritePointer(1)};if(product==Product::Flow)flow.process(pointers.data(),2,b.getNumSamples());else if(product==Product::Heat)heat.process(pointers.data(),2,b.getNumSamples());else tune.process(pointers.data(),2,b.getNumSamples());};
    juce::AudioBuffer<float> actual(2,blockSize),reference(2,blockSize);juce::MidiBuffer midi;
    for(int i=0;i<400;++i){actual.clear();reference.clear();plugin->processBlock(actual,midi);direct(reference);}double square=0;std::uint64_t count=0;const int total=120000;
    for(int at=0;at<total;){const int n=std::min(blockSize,total-at);actual.setSize(2,n,false,false,true);reference.setSize(2,n,false,false,true);
        for(int i=0;i<n;++i){const double t=(at+i)/48000.;const double f=220*std::pow(2.,.37/12);const auto envelope=t<.03?t/.03:(t>2.35?std::max(0.,(2.5-t)/.15):.72+.28*std::sin(2*pi*1.3*t));const auto x=static_cast<float>(envelope*(.23*std::sin(2*pi*f*t)+.07*std::sin(4*pi*f*t)+.025*std::sin(6*pi*f*t)));
            for(int c=0;c<2;++c){const float sample=c==0?x:-.73f*x;actual.setSample(c,i,sample);reference.setSample(c,i,sample);}}
        plugin->processBlock(actual,midi);direct(reference);for(int c=0;c<2;++c)for(int i=0;i<n;++i){const double error=std::abs(static_cast<double>(actual.getSample(c,i))-reference.getSample(c,i));r.check(std::isfinite(actual.getSample(c,i))&&std::isfinite(reference.getSample(c,i)),"actual and independent wet renders remain finite");r.maxWetError=std::max(r.maxWetError,error);square+=error*error;++count;}at+=n;}
    r.check(r.maxWetError<=1e-5,"actual VST3 wet audio matches the independently configured direct engine within 1e-5");auto row=object();put(row,"sample_rate",48000);put(row,"channels",2);put(row,"frames",total);put(row,"silence_settling_frames",400*blockSize);put(row,"maximum_absolute_error",r.maxWetError);put(row,"rms_error",std::sqrt(square/std::max<std::uint64_t>(1,count)));put(row,"tolerance",1e-5);put(row,"shared_input",true);put(r.data,"wet_comparison",row);plugin->releaseResources();
}
void test(Report& r,juce::VST3PluginFormat& format,const juce::File& bundle,Product product){r.module=bundle.getFullPathName();r.check(bundle.exists(),"candidate module exists");const auto binary=bundle.isDirectory()?bundle.getChildFile("Contents/x86_64-win/"+bundle.getFileName()):bundle;r.check(pe64(binary),"candidate module is a PE x86-64 binary");
    juce::OwnedArray<juce::PluginDescription> descriptions;format.findAllTypesForFile(descriptions,r.module);r.check(descriptions.size()==1,"VST3 bundle exports exactly one discoverable audio processor");if(descriptions.size()!=1)return;auto plugin=create(r,format,*descriptions[0],48000);if(!plugin)return;prepare(*plugin,48000);metadata(r,*plugin,*descriptions[0],product);roundtrip(r,format,*descriptions[0],*plugin,product);plugin->releaseResources();plugin.reset();dryRoutes(r,format,*descriptions[0],product);wetComparison(r,format,*descriptions[0],product);
}
}
int main(int argc,char** argv){if(argc!=5){std::cerr<<"Usage: GillVocalVst3HostTests GILLFLOW.vst3 GILLHEAT.vst3 GILLTUNE.vst3 ""GILLTUNE LIVE.vst3""\n";return 2;}juce::ScopedJuceInitialiser_GUI gui;juce::VST3PluginFormat format;std::array<Report,4> reports;const char* names[]{"GILLFLOW","GILLHEAT","GILLTUNE","GILLTUNE LIVE"};
    for(int i=0;i<4;++i){reports[i].name=names[i];test(reports[i],format,juce::File(juce::String(argv[i+1])),static_cast<Product>(i));}
    int failures=0;for(int i=0;i<4;++i){auto& r=reports[i];for(int j=0;j<4;++j)if(i!=j)r.check(r.classId.isNotEmpty()&&reports[j].classId.isNotEmpty()&&r.classId!=reports[j].classId,"the four products have distinct complete VST3 factory class IDs");put(r.data,"name",r.name);put(r.data,"module_path",r.module);put(r.data,"class_id",r.classId);put(r.data,"host_bits",static_cast<int>(sizeof(void*)*8));put(r.data,"checks",r.checks);put(r.data,"failures",r.failures);put(r.data,"passed",r.failures==0);put(r.data,"unique_factory_ids_checked",true);put(r.data,"scope","Actual x64 VST3 discovery/factory, parameter metadata, presets, opaque state reload, four-rate dry/native-bypass impulses, stereo isolation, and direct-engine wet comparison. No FL Studio UI or subjective listening claim.");const auto destination=juce::File::getCurrentWorkingDirectory().getChildFile(r.name+"-vst3-host-report.json");if(!destination.replaceWithText(juce::JSON::toString(r.data,true)+"\n")){std::cerr<<"Could not write "<<destination.getFullPathName()<<'\n';++r.failures;}std::cout<<r.name<<": "<<r.checks<<" checks, "<<r.failures<<" failures, max wet error "<<r.maxWetError<<'\n';failures+=r.failures;}return failures?1:0;}
