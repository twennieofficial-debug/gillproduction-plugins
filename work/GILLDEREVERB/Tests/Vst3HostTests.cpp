#include <JuceHeader.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

int impulseMain(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Expected compiled VST3 bundle path\n";return 2;}
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    const auto file=juce::File(juce::String(argv[1])).getFullPathName();
    format.findAllTypesForFile(types,file);
    if(types.size()!=1){std::cerr<<"Expected one VST3 type, found "<<types.size()<<"\n";return 1;}
    juce::String error;
    auto plugin=format.createInstanceFromDescription(*types[0],48000,127,error);
    if(!plugin){std::cerr<<error<<"\n";return 1;}
    int failures=0,checks=0;
    auto check=[&](bool ok,const char* what){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<what<<"\n";}};
    check(plugin->getName()=="GILLDEREVERB","actual binary identity");
    for(auto* p:plugin->getParameters())if(p->getName(100)=="AMOUNT")p->setValueNotifyingHost(0.f);
    plugin->setPlayConfigDetails(2,2,48000,127);
    plugin->prepareToPlay(48000,127);
    const int latency=plugin->getLatencySamples();
    check(latency==2048,"actual VST3 host-reported latency after prepare");
    juce::AudioBuffer<float> buffer(2,127);juce::MidiBuffer midi;
    int offset=0,nonzero=0,peakPosition=-1;float peak=0;
    for(int block=0;block<40;++block){
        buffer.clear();if(block==0)buffer.setSample(0,0,0.25f);
        plugin->processBlock(buffer,midi);
        for(int i=0;i<127;++i){const auto x=buffer.getSample(0,i);check(std::isfinite(x),"finite binary output");
            if(x!=0.f)++nonzero;if(std::abs(x)>peak){peak=std::abs(x);peakPosition=offset+i;}
            check(buffer.getSample(1,i)==0.f,"actual binary channel isolation");}
        offset+=127;
    }
    check(nonzero==1 && peakPosition==2048 && peak==0.25f,"actual binary exact delayed impulse with amount0");
    plugin->releaseResources();
    std::ofstream out("vst3-host-report.json");
    out<<"{\"passed\":"<<(failures?"false":"true")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"reported_latency_after_prepare\":"<<latency<<",\"measured_impulse_delay\":"<<peakPosition<<"}\n";
    std::cout<<"VST3 host checks: "<<checks<<", failures: "<<failures<<", reported/measured latency: "<<latency<<"/"<<peakPosition<<"\n";
    return failures?1:0;
}

namespace {
constexpr double sampleRate=48000.0, pi=3.14159265358979323846, tolerance=1.e-7;
constexpr int blockSize=127, channels=2, frames=144000;
struct Checks {
    int count=0, failures=0;
    void require(bool ok,const juce::String& message) {
        ++count; if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
    }
};
juce::var object(){return juce::var(new juce::DynamicObject());}
void put(juce::var& o,const juce::Identifier& key,const juce::var& value){o.getDynamicObject()->setProperty(key,value);}
bool saveJson(const juce::File& f,const juce::var& value){return f.replaceWithText(juce::JSON::toString(value,false)+"\n");}
bool saveBlock(const juce::File& f,const juce::MemoryBlock& b){return f.replaceWithData(b.getData(),b.getSize());}

// Public host extension, not a synthesized APVTS state. In the SDK preset
// header, bytes 8..39 hold the complete 128-bit class ID as 32 ASCII characters.
struct PresetVisitor final : juce::ExtensionsVisitor {
    juce::MemoryBlock preset;
    void visitVST3Client(const VST3Client& client) override{preset=client.getPreset();}
};
juce::MemoryBlock getPreset(juce::AudioPluginInstance& p){PresetVisitor v;p.getExtensions(v);return v.preset;}
juce::String classId(const juce::MemoryBlock& b){
    if(b.getSize()<48 || std::memcmp(b.getData(),"VST3",4)!=0)return {};
    return juce::String::fromUTF8(static_cast<const char*>(b.getData())+8,32);
}
juce::var snapshot(juce::AudioPluginInstance& plugin){
    juce::Array<juce::var> result;
    for(int i=0;i<plugin.getParameters().size();++i){
        auto* p=plugin.getHostedParameter(i);auto row=object();
        put(row,"index",i);put(row,"id",p->getParameterID());put(row,"name",p->getName(128));
        put(row,"normalized",p->getValue());put(row,"default_normalized",p->getDefaultValue());
        put(row,"text",p->getText(p->getValue(),128));put(row,"is_automatable",p->isAutomatable());
        put(row,"is_discrete",p->isDiscrete());put(row,"steps",p->getNumSteps());result.add(row);
    }
    return juce::var(result);
}
void compareParameters(Checks& c,juce::AudioPluginInstance& plugin,const juce::var& expected,const juce::String& stage){
    const auto* a=expected.getArray();c.require(a!=nullptr && a->size()==9,stage+": nine reference parameters");if(!a)return;
    c.require(plugin.getParameters().size()==a->size(),stage+": parameter count");
    for(int i=0;i<std::min(plugin.getParameters().size(),a->size());++i){
        auto* p=plugin.getHostedParameter(i);const auto& e=a->getReference(i);auto label=stage+": parameter "+juce::String(i)+" ";
        c.require(p->getParameterID()==e["id"].toString(),label+"stable ID/order");
        c.require(p->getName(128)==e["name"].toString(),label+"name");
        c.require(std::abs(p->getValue()-static_cast<double>(e["normalized"]))<=1.e-6,label+"restored value");
        c.require(std::abs(p->getDefaultValue()-static_cast<double>(e["default_normalized"]))<=1.e-7,label+"default");
        c.require(p->isAutomatable()==static_cast<bool>(e["is_automatable"]),label+"automation flag");
        c.require(p->isDiscrete()==static_cast<bool>(e["is_discrete"]),label+"discrete flag");
        c.require(p->getNumSteps()==static_cast<int>(e["steps"]),label+"steps");
        c.require(p->getText(p->getValue(),128)==e["text"].toString(),label+"physical value mapping");
    }
}
juce::AudioProcessorParameter* parameter(juce::AudioPluginInstance& plugin,const juce::String& name){
    for(auto* p:plugin.getParameters())if(p->getName(128)==name)return p;return nullptr;
}
void prepare(juce::AudioPluginInstance& p){p.setPlayConfigDetails(channels,channels,sampleRate,blockSize);p.prepareToPlay(sampleRate,blockSize);}
void flush(juce::AudioPluginInstance& p){
    juce::AudioBuffer<float> silence(channels,blockSize);juce::MidiBuffer midi;
    for(int i=0;i<8;++i){silence.clear();p.processBlock(silence,midi);}
}
void restore(juce::AudioPluginInstance& p,const juce::MemoryBlock& state){
    p.releaseResources();p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    prepare(p);flush(p);p.releaseResources();prepare(p);
}
struct Point{int block;float amount;};
constexpr std::array<Point,4> automation{{{60,.73f},{250,.21f},{500,.91f},{760,.44f}}};
std::vector<float> render(juce::AudioPluginInstance& p,Checks& checks){
    std::vector<float> out(static_cast<size_t>(frames*channels));
    juce::AudioBuffer<float> b(channels,blockSize);juce::MidiBuffer midi;
    auto* amount=parameter(p,"AMOUNT");checks.require(amount!=nullptr,"AMOUNT for host automation");
    std::uint32_t rng=0x91ab37c5u;double coloured=0;int offset=0,block=0;
    while(offset<frames){
        for(const auto& point:automation)if(point.block==block && amount)amount->setValueNotifyingHost(point.amount);
        const int count=std::min(blockSize,frames-offset);b.setSize(channels,count,false,false,true);
        for(int i=0;i<count;++i){
            const double t=(offset+i)/sampleRate;rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            const double noise=static_cast<double>(rng)/4294967295.0*2.0-1.0;coloured=.94*coloured+.06*noise;
            const double gate=t>=.15 && t<1.85?.55+.45*std::pow(std::sin(7*pi*t),2.0):0.0;
            const double tail=t>=1.85?std::exp(-8*(t-1.85)):0.0;
            const double common=gate*(.10*std::sin(2*pi*223*t)+.065*std::sin(2*pi*1733*t)+.20*coloured)
                                +tail*(.055*std::sin(2*pi*411*t)+.10*coloured);
            b.setSample(0,i,static_cast<float>(common));
            b.setSample(1,i,static_cast<float>(.73*common+gate*.027*std::sin(2*pi*977*t)));
        }
        p.processBlock(b,midi);
        for(int i=0;i<count;++i)for(int ch=0;ch<channels;++ch)out[static_cast<size_t>((offset+i)*channels+ch)]=b.getSample(ch,i);
        offset+=count;++block;
    }
    checks.require(std::all_of(out.begin(),out.end(),[](float v){return std::isfinite(v);}),"finite render");return out;
}
juce::var compareAudio(Checks& c,const std::vector<float>& actual,const juce::MemoryBlock& expected,const juce::String& stage){
    auto result=object();const bool sizeOK=expected.getSize()==actual.size()*sizeof(float);c.require(sizeOK,stage+": frame count");
    double maximum=0,squared=0;bool finite=true;
    if(sizeOK)for(size_t i=0;i<actual.size();++i){
        float ref=0;std::memcpy(&ref,static_cast<const char*>(expected.getData())+i*sizeof(float),sizeof(float));
        finite=finite && std::isfinite(ref) && std::isfinite(actual[i]);const double delta=static_cast<double>(actual[i])-ref;
        maximum=std::max(maximum,std::abs(delta));squared+=delta*delta;
    }
    c.require(sizeOK && finite && maximum<=tolerance,stage+": old audio preserved including automation");
    put(result,"maximum_absolute_error",maximum);put(result,"rms_error",std::sqrt(squared/std::max(size_t(1),actual.size())));
    put(result,"bit_exact",sizeOK && std::memcmp(actual.data(),expected.getData(),expected.getSize())==0);put(result,"tolerance",tolerance);return result;
}
int legacy(juce::VST3PluginFormat& format,const juce::PluginDescription& desc,std::unique_ptr<juce::AudioPluginInstance>& plugin,
           const juce::File& folder,bool capture){
    Checks c;const auto metaFile=folder.getChildFile("legacy-metadata.json"),stateFile=folder.getChildFile("legacy-host-state.bin"),
                        presetFile=folder.getChildFile("legacy.vstpreset"),goldFile=folder.getChildFile("legacy-stereo-golden.f32");
    if(capture){
        c.require(desc.version=="0.1.0","capture released v0.1.0 binary");
        c.require(!metaFile.exists() && !stateFile.exists() && !goldFile.exists(),"do not overwrite existing capture");
        c.require(folder.createDirectory().wasOk(),"fixture directory");c.require(plugin->getParameters().size()==9,"nine old parameters");
        if(c.failures)return 1;
        struct Setting{const char* name;const char* text;};
        const Setting settings[]={{"AMOUNT","37"},{"ROOM","830"},{"VOICE PROTECT","88"},{"LOW","180"},
                                  {"HIGH","6200"},{"MIX","63"},{"OUTPUT","-3.25"},{"REMOVED","0"},{"BYPASS","0"}};
        prepare(*plugin);
        for(const auto& s:settings){auto* p=parameter(*plugin,s.name);c.require(p!=nullptr,juce::String("capture ")+s.name);if(p)p->setValueNotifyingHost(p->getValueForText(s.text));}
        flush(*plugin);juce::MemoryBlock state;plugin->getStateInformation(state);const auto preset=getPreset(*plugin);
        c.require(state.getSize()>0,"actual opaque host state");c.require(classId(preset).length()==32,"full VST3 class ID");
        const auto params=snapshot(*plugin);restore(*plugin,state);compareParameters(c,*plugin,params,"old binary roundtrip");
        const auto golden=render(*plugin,c);const juce::MemoryBlock gold(golden.data(),golden.size()*sizeof(float));
        restore(*plugin,state);const auto repeat=compareAudio(c,render(*plugin,c),gold,"old binary repeatability");
        if(c.failures)return 1;
        auto meta=object();put(meta,"schema",1);put(meta,"captured_at_utc",juce::Time::getCurrentTime().toISO8601(true));
        put(meta,"origin","Released v0.1.0 VST3; deterministic synthetic tones and seeded noise; no user audio");
        put(meta,"plugin_name",desc.name);put(meta,"manufacturer",desc.manufacturerName);put(meta,"plugin_version",desc.version);
        put(meta,"plugin_unique_id",desc.uniqueId);put(meta,"plugin_deprecated_uid",desc.deprecatedUid);put(meta,"vst3_class_id",classId(preset));
        put(meta,"plugin_description_xml",desc.createXml()->toString());put(meta,"parameters",params);
        put(meta,"sample_rate",sampleRate);put(meta,"channels",channels);put(meta,"frames",frames);put(meta,"block_size",blockSize);
        put(meta,"latency",plugin->getLatencySamples());put(meta,"golden_format","Interleaved stereo IEEE float32 little-endian, including plugin latency");
        put(meta,"generator","Vst3HostTests.cpp render(), xorshift32 seed 0x91ab37c5, three seconds");
        juce::Array<juce::var> points;for(const auto& point:automation){auto o=object();put(o,"block",point.block);put(o,"normalized_amount",point.amount);points.add(o);}
        put(meta,"amount_automation",juce::var(points));put(meta,"repeat_comparison",repeat);
        c.require(saveBlock(stateFile,state),"write opaque state");c.require(saveBlock(presetFile,preset),"write VST3 preset");
        c.require(saveBlock(goldFile,gold),"write golden audio");c.require(saveJson(metaFile,meta),"write metadata");
        auto report=object();put(report,"passed",c.failures==0);put(report,"checks",c.count);put(report,"failures",c.failures);
        put(report,"mode","capture-legacy");put(report,"old_binary_repeatability",repeat);
        c.require(saveJson(folder.getChildFile("legacy-capture-report.json"),report),"write capture report");
    }else{
        const auto meta=juce::JSON::parse(metaFile.loadFileAsString());juce::MemoryBlock state,gold,preset;
        c.require(meta.isObject() && static_cast<int>(meta["schema"])==1,"read metadata");
        c.require(stateFile.loadFileAsData(state) && state.getSize()>0,"read old opaque state");c.require(goldFile.loadFileAsData(gold),"read golden audio");
        c.require(presetFile.loadFileAsData(preset),"read old preset");if(c.failures)return 1;
        c.require(desc.name==meta["plugin_name"].toString(),"same name");c.require(desc.manufacturerName==meta["manufacturer"].toString(),"same vendor");
        c.require(desc.uniqueId==static_cast<int>(meta["plugin_unique_id"]),"same PluginDescription unique ID");
        c.require(desc.deprecatedUid==static_cast<int>(meta["plugin_deprecated_uid"]),"same legacy identity");
        c.require(classId(preset)==meta["vst3_class_id"].toString(),"fixture full ID");restore(*plugin,state);
        c.require(classId(getPreset(*plugin))==meta["vst3_class_id"].toString(),"same complete 128-bit VST3 class ID");
        c.require(plugin->getLatencySamples()==static_cast<int>(meta["latency"]),"same latency");
        compareParameters(c,*plugin,meta["parameters"],"old state in current binary");const auto first=compareAudio(c,render(*plugin,c),gold,"old state render");
        // Save legacy settings through the new wrapper, then load into a fresh
        // instance. This detects v2 state accidentally forgetting legacy mode.
        restore(*plugin,state);juce::MemoryBlock resaved;plugin->getStateInformation(resaved);
        juce::String error;auto reopened=format.createInstanceFromDescription(desc,sampleRate,blockSize,error);
        c.require(reopened!=nullptr,"fresh instance for re-save/reopen");auto second=object();
        if(reopened){restore(*reopened,resaved);compareParameters(c,*reopened,meta["parameters"],"re-saved legacy state");
            second=compareAudio(c,render(*reopened,c),gold,"re-saved legacy render");reopened->releaseResources();}
        auto report=object();put(report,"passed",c.failures==0);put(report,"checks",c.count);put(report,"failures",c.failures);
        put(report,"mode","verify-legacy");put(report,"tested_version",desc.version);put(report,"baseline_version",meta["plugin_version"]);
        put(report,"old_state_render",first);put(report,"resaved_legacy_render",second);
        put(report,"scope","Actual VST3 identity, all nine parameter metadata/values, opaque old state, host Amount automation and re-save/reopen audio. No FL project/UI claim.");
        c.require(saveJson(juce::File::getCurrentWorkingDirectory().getChildFile("vst3-upgrade-report.json"),report),"write upgrade report");
    }
    plugin->releaseResources();std::cout<<"Legacy "<<(capture?"capture":"verification")<<": "<<c.count<<" checks, "<<c.failures<<" failures.\n";return c.failures?1:0;
}
}

int main(int argc,char** argv){
    if(argc==2)return impulseMain(argc,argv);
    if(argc!=4){std::cerr<<"Usage: host-test <VST3 bundle> [--capture-legacy|--verify-legacy <fixture folder>]\n";return 2;}
    const juce::String mode(argv[2]);if(mode!="--capture-legacy" && mode!="--verify-legacy")return 2;
    juce::ScopedJuceInitialiser_GUI gui;juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types,juce::File(juce::String(argv[1])).getFullPathName());
    if(types.size()!=1){std::cerr<<"Expected one VST3 type, found "<<types.size()<<'\n';return 1;}
    juce::String error;auto plugin=format.createInstanceFromDescription(*types[0],sampleRate,blockSize,error);
    if(!plugin){std::cerr<<error<<'\n';return 1;}
    return legacy(format,*types[0],plugin,juce::File(juce::String(argv[3])),mode=="--capture-legacy");
}
