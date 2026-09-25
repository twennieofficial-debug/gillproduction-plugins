#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

int impulseMain(int argc,char** argv) {
    if(argc!=2)return 2;
    juce::ScopedJuceInitialiser_GUI gui;juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types,juce::File(juce::String(argv[1])).getFullPathName());
    if(types.size()!=1){std::cerr<<"Expected one type\n";return 1;}
    const auto name=types[0]->name;int checks=0,failures=0;
    auto check=[&](bool ok,const char* text){++checks;if(!ok){++failures;if(failures<20)std::cerr<<"FAIL: "<<text<<'\n';}};
    check(name=="GILLEQ"||name=="GILLDECLICK"||name=="GILLDECRACKLE"||name=="GILL-DE-ESSER","known suite product identity");
    juce::String error;
    for(double fs:{44100.,48000.,96000.,192000.}) {
        auto plugin=format.createInstanceFromDescription(*types[0],fs,127,error);
        if(!plugin){check(false,"actual VST3 opens");continue;}
        const int expected=(name=="GILLEQ"||name=="GILL-DE-ESSER")?0:static_cast<int>(std::ceil(fs*(name=="GILLDECLICK"?.004:.008)));
        plugin->setPlayConfigDetails(2,2,fs,127);plugin->prepareToPlay(fs,127);
        check(plugin->getLatencySamples()==expected,"actual VST3 reports expected latency");
        for(auto* p:plugin->getParameters())if(p->getName(100)=="AMOUNT")p->setValueNotifyingHost(0.f);
        juce::AudioBuffer<float> b(2,127);juce::MidiBuffer midi;
        for(int i=0;i<64;++i){b.clear();plugin->processBlock(b,midi);}
        int offset=0,count=0,peakPosition=-1;float peak=0;
        for(int block=0;block<40;++block){b.clear();if(block==0)b.setSample(0,0,.25f);plugin->processBlock(b,midi);
            for(int i=0;i<127;++i){const auto x=b.getSample(0,i);check(std::isfinite(x),"finite output");if(x!=0)++count;if(std::abs(x)>peak){peak=std::abs(x);peakPosition=offset+i;}
                check(b.getSample(1,i)==0,"binary channel isolation");}offset+=127;}
        check(count==1&&peakPosition==expected&&peak==.25f,"measured exact impulse and reported latency agree");
        juce::MemoryBlock state;plugin->getStateInformation(state);check(state.getSize()>0,"binary serializes host state");plugin->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        plugin->releaseResources();
    }
    const auto destination=juce::File::getCurrentWorkingDirectory().getChildFile(name+"-vst3-host-report.json");
    std::ofstream report(destination.getFullPathName().toStdString());report<<"{\"passed\":"<<(failures?"false":"true")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"name\":\""<<name<<"\",\"version\":\""<<types[0]->version<<"\",\"sample_rates\":[44100,48000,96000,192000]}\n";
    std::cout<<name<<": "<<checks<<" host checks, "<<failures<<" failures\n";return failures?1:0;
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
void compareParameters(Checks& c,juce::AudioPluginInstance& plugin,const juce::var& expected,const juce::String& stage,int expectedCount=59){
    const auto* a=expected.getArray();c.require(a!=nullptr && a->size()==expectedCount,stage+": "+juce::String(expectedCount)+" reference parameters");if(!a)return;
    c.require(expectedCount==99?plugin.getParameters().size()==99:(plugin.getParameters().size()==59 || plugin.getParameters().size()==99),stage+": compatible parameter count");
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
    for(int i=a->size();i<plugin.getParameters().size();++i){auto* p=plugin.getParameters()[i];c.require(std::abs(p->getValue()-p->getDefaultValue())<=1.e-6,stage+": new dynamics at static defaults");}
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
struct DynamicPoint{int block;const char* name;float normalized;};
constexpr std::array<DynamicPoint,6> dynamicAutomation {{{120,"BAND 1 DYNAMIC RANGE",.25f},{310,"BAND 1 THRESHOLD",.62f},
    {420,"BAND 1 ATTACK",.34f},{610,"BAND 1 RELEASE",.68f},{820,"BAND 3 DYNAMIC",0.f},{930,"BAND 3 DYNAMIC",1.f}}};
std::vector<float> render(juce::AudioPluginInstance& p,Checks& checks,bool dynamics=false){
    std::vector<float> out(static_cast<size_t>(frames*channels));
    juce::AudioBuffer<float> b(channels,blockSize);juce::MidiBuffer midi;
    auto* amount=parameter(p,"BAND 1 GAIN");checks.require(amount!=nullptr,"AMOUNT for host automation");
    std::uint32_t rng=0x91ab37c5u;double coloured=0;int offset=0,block=0;
    while(offset<frames){
        for(const auto& point:automation)if(point.block==block && amount)amount->setValueNotifyingHost(point.amount);
        if(dynamics)for(const auto& point:dynamicAutomation)if(point.block==block){auto* target=parameter(p,point.name);checks.require(target!=nullptr,"dynamic host automation target exists");if(target)target->setValueNotifyingHost(point.normalized);}
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
        c.require(folder.createDirectory().wasOk(),"fixture directory");c.require(plugin->getParameters().size()==59,"59 old parameters");
        if(c.failures)return 1;
        struct Setting{const char* name;const char* text;};
        const Setting settings[]={{"BAND 1 FREQUENCY","320"},{"BAND 1 GAIN","6"},{"BAND 1 Q","1.7"},{"BAND 1 CHANNEL","LEFT"},
            {"BAND 2 TYPE","LOW SHELF"},{"BAND 2 FREQUENCY","140"},{"BAND 2 GAIN","-3"},
            {"BAND 3 FREQUENCY","2300"},{"BAND 3 GAIN","-4"},{"BAND 3 CHANNEL","MID"},{"OUTPUT","-1.25"},{"BYPASS","0"},{"DELTA LISTEN","0"}};
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
        juce::Array<juce::var> points;for(const auto& point:automation){auto o=object();put(o,"block",point.block);put(o,"normalized_band_gain",point.amount);points.add(o);}
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
        put(report,"scope","Actual VST3 identity, original 59 parameter metadata/values, added dynamics at static defaults, opaque old state, host band-gain automation and re-save/reopen audio. No FL project/UI claim.");
        c.require(saveJson(juce::File::getCurrentWorkingDirectory().getChildFile("GILLEQ-vst3-upgrade-report.json"),report),"write upgrade report");
    }
    plugin->releaseResources();std::cout<<"Legacy "<<(capture?"capture":"verification")<<": "<<c.count<<" checks, "<<c.failures<<" failures.\n";return c.failures?1:0;
}

int dynamicUpgrade(juce::VST3PluginFormat& format,const juce::PluginDescription& desc,
                   std::unique_ptr<juce::AudioPluginInstance>& plugin,const juce::File& folder,bool capture){
    Checks c;
    const auto metaFile=folder.getChildFile("dynamic-metadata.json"),stateFile=folder.getChildFile("dynamic-host-state.bin"),
               presetFile=folder.getChildFile("dynamic.vstpreset"),goldFile=folder.getChildFile("dynamic-stereo-golden.f32");
    c.require(desc.name=="GILLEQ","dynamic upgrade tests the GILLEQ product");
    c.require(plugin->getParameters().size()==99,"all 99 parameters exposed by the actual VST3");
    c.require(!folder.getChildFile("legacy-metadata.json").exists() && !folder.getChildFile("legacy-host-state.bin").exists(),
              "dynamic fixtures have a separate folder from v0.1 fixtures");
    if(c.failures)return 1;

    if(capture){
        c.require(desc.version=="0.2.0","capture the released v0.2.0 binary");
        c.require(!metaFile.exists() && !stateFile.exists() && !presetFile.exists() && !goldFile.exists()
                  && !folder.getChildFile("dynamic-capture-report.json").exists(),"never overwrite a dynamic baseline");
        if(c.failures)return 1;
        c.require(folder.createDirectory().wasOk(),"create separate dynamic fixture directory");
        if(c.failures)return 1;
        prepare(*plugin);
        auto setText=[&](const juce::String& name,const juce::String& text){
            auto* p=parameter(*plugin,name);c.require(p!=nullptr,"capture setting "+name);
            if(p)p->setValueNotifyingHost(p->getValueForText(text));
        };
        struct Setting{const char* name;const char* text;};
        const Setting settings[]={
            {"BAND 1 FREQUENCY","223"},{"BAND 1 GAIN","2.5"},{"BAND 1 Q","1.4"},
            {"BAND 2 TYPE","LOW SHELF"},{"BAND 2 FREQUENCY","180"},{"BAND 2 GAIN","-1.5"},{"BAND 2 CHANNEL","LEFT"},
            {"BAND 3 TYPE","HIGH SHELF"},{"BAND 3 FREQUENCY","1800"},{"BAND 3 GAIN","1"},{"BAND 3 CHANNEL","SIDE"},
            {"BAND 4 FREQUENCY","977"},{"BAND 4 GAIN","-2"},{"BAND 4 CHANNEL","RIGHT"},
            {"BAND 5 FREQUENCY","1733"},{"BAND 5 GAIN","1.5"},{"BAND 5 CHANNEL","MID"},
            {"OUTPUT","-2"},{"BYPASS","0"},{"DELTA LISTEN","0"}};
        for(const auto& s:settings)setText(s.name,s.text);
        // Every new continuous parameter is nondefault, including inactive
        // bands, so state coverage cannot pass by merely restoring defaults.
        constexpr std::array<float,8> thresholds{{-38.f,-42.f,-55.f,-40.f,-36.f,-31.f,-29.f,-47.f}};
        constexpr std::array<float,8> ranges{{-8.f,4.f,-5.f,-3.5f,3.f,-7.f,2.f,-11.f}};
        constexpr std::array<float,8> attacks{{3.5f,8.f,1.2f,12.f,6.f,17.f,24.f,2.f}};
        constexpr std::array<float,8> releases{{145.f,320.f,87.f,210.f,260.f,370.f,440.f,95.f}};
        for(int band=0;band<8;++band){
            const auto prefix="BAND "+juce::String(band+1)+" ";
            setText(prefix+"DYNAMIC",band<5?"1":"0");
            setText(prefix+"THRESHOLD",juce::String(thresholds[static_cast<size_t>(band)]));
            setText(prefix+"DYNAMIC RANGE",juce::String(ranges[static_cast<size_t>(band)]));
            setText(prefix+"ATTACK",juce::String(attacks[static_cast<size_t>(band)]));
            setText(prefix+"RELEASE",juce::String(releases[static_cast<size_t>(band)]));
        }
        if(c.failures){plugin->releaseResources();return 1;}
        flush(*plugin);
        c.require(plugin->getLatencySamples()==0,"released dynamic EQ has zero latency");
        juce::MemoryBlock state;plugin->getStateInformation(state);const auto preset=getPreset(*plugin);
        c.require(state.getSize()>0,"capture actual opaque dynamic host state");
        c.require(classId(preset).length()==32,"capture complete VST3 class identity");
        const auto params=snapshot(*plugin);
        restore(*plugin,state);compareParameters(c,*plugin,params,"released dynamic state roundtrip",99);
        const auto golden=render(*plugin,c,true);const juce::MemoryBlock gold(golden.data(),golden.size()*sizeof(float));
        restore(*plugin,state);
        const auto repeat=compareAudio(c,render(*plugin,c,true),gold,"released dynamic binary repeatability");
        c.require(static_cast<bool>(repeat["bit_exact"]),"dynamic baseline is bit-exactly repeatable");

        // Prove this fixture exercises audible processing, rather than only
        // persisting switches that never cross a threshold.
        restore(*plugin,state);
        for(int band=0;band<8;++band)setText("BAND "+juce::String(band+1)+" DYNAMIC","0");
        flush(*plugin);plugin->releaseResources();prepare(*plugin);
        const auto staticRender=render(*plugin,c,false);
        double differenceMax=0,differenceSquares=0;
        for(size_t i=0;i<golden.size();++i){const double d=static_cast<double>(golden[i])-staticRender[i];
            differenceMax=std::max(differenceMax,std::abs(d));differenceSquares+=d*d;}
        const double differenceRms=std::sqrt(differenceSquares/static_cast<double>(golden.size()));
        c.require(differenceMax>1.e-4 && differenceRms>1.e-5,"captured dynamics change the synthetic audio");
        if(c.failures){plugin->releaseResources();return 1;}

        auto meta=object();put(meta,"schema",2);put(meta,"fixture_kind","gilleq-dynamic-upgrade");
        put(meta,"captured_at_utc",juce::Time::getCurrentTime().toISO8601(true));
        put(meta,"origin","Released v0.2.0 VST3; deterministic synthetic tones and seeded noise; no user audio");
        put(meta,"plugin_name",desc.name);put(meta,"manufacturer",desc.manufacturerName);put(meta,"plugin_version",desc.version);
        put(meta,"plugin_unique_id",desc.uniqueId);put(meta,"plugin_deprecated_uid",desc.deprecatedUid);put(meta,"vst3_class_id",classId(preset));
        put(meta,"plugin_description_xml",desc.createXml()->toString());put(meta,"parameters",params);
        put(meta,"sample_rate",sampleRate);put(meta,"channels",channels);put(meta,"frames",frames);put(meta,"block_size",blockSize);
        put(meta,"latency",0);put(meta,"golden_format","Interleaved stereo IEEE float32 little-endian");
        put(meta,"generator","Vst3HostTests.cpp render(dynamics=true), xorshift32 seed 0x91ab37c5, three seconds");
        juce::Array<juce::var> points;
        for(const auto& point:automation){auto o=object();put(o,"block",point.block);put(o,"name","BAND 1 GAIN");put(o,"normalized",point.amount);points.add(o);}
        for(const auto& point:dynamicAutomation){auto o=object();put(o,"block",point.block);put(o,"name",point.name);put(o,"normalized",point.normalized);points.add(o);}
        put(meta,"host_automation",juce::var(points));put(meta,"repeat_comparison",repeat);
        put(meta,"dynamic_vs_static_maximum_difference",differenceMax);put(meta,"dynamic_vs_static_rms_difference",differenceRms);
        c.require(saveBlock(stateFile,state),"write dynamic opaque state");c.require(saveBlock(presetFile,preset),"write dynamic VST3 preset");
        c.require(saveBlock(goldFile,gold),"write dynamic golden audio");c.require(saveJson(metaFile,meta),"write dynamic metadata");
        auto report=object();put(report,"passed",c.failures==0);put(report,"checks",c.count+1);put(report,"failures",c.failures);
        put(report,"mode","capture-dynamic");put(report,"baseline_version",desc.version);put(report,"parameter_count",99);
        put(report,"repeat_comparison",repeat);put(report,"dynamic_vs_static_maximum_difference",differenceMax);
        put(report,"dynamic_vs_static_rms_difference",differenceRms);
        c.require(saveJson(folder.getChildFile("dynamic-capture-report.json"),report),"write dynamic capture report");
    }else{
        const auto meta=juce::JSON::parse(metaFile.loadFileAsString());juce::MemoryBlock state,gold,preset;
        c.require(meta.isObject() && static_cast<int>(meta["schema"])==2
                    && meta["fixture_kind"].toString()=="gilleq-dynamic-upgrade","read separate dynamic metadata");
        c.require(meta["plugin_version"].toString()=="0.2.0","dynamic baseline comes from v0.2.0");
        c.require(static_cast<double>(meta["sample_rate"])==sampleRate && static_cast<int>(meta["channels"])==channels
                    && static_cast<int>(meta["frames"])==frames && static_cast<int>(meta["block_size"])==blockSize,
                  "dynamic fixture uses the expected deterministic rendering configuration");
        c.require(stateFile.loadFileAsData(state) && state.getSize()>0,"read dynamic opaque state");
        c.require(goldFile.loadFileAsData(gold) && gold.getSize()==static_cast<size_t>(frames*channels)*sizeof(float),"read full dynamic golden audio");
        c.require(presetFile.loadFileAsData(preset),"read dynamic VST3 preset");
        if(c.failures)return 1;
        c.require(desc.name==meta["plugin_name"].toString(),"dynamic upgrade preserves name");
        c.require(desc.manufacturerName==meta["manufacturer"].toString(),"dynamic upgrade preserves manufacturer");
        c.require(desc.uniqueId==static_cast<int>(meta["plugin_unique_id"]),"dynamic upgrade preserves PluginDescription unique ID");
        c.require(desc.deprecatedUid==static_cast<int>(meta["plugin_deprecated_uid"]),"dynamic upgrade preserves legacy identity");
        c.require(classId(preset)==meta["vst3_class_id"].toString() && classId(preset).length()==32,"dynamic fixture full class ID");
        restore(*plugin,state);
        c.require(classId(getPreset(*plugin))==meta["vst3_class_id"].toString(),"dynamic upgrade preserves complete 128-bit VST3 class ID");
        c.require(plugin->getLatencySamples()==0 && static_cast<int>(meta["latency"])==0,"dynamic upgrade retains zero latency");
        compareParameters(c,*plugin,meta["parameters"],"v0.2 dynamic state in current binary",99);
        const auto first=compareAudio(c,render(*plugin,c,true),gold,"v0.2 dynamic state render");
        c.require(static_cast<bool>(first["bit_exact"]),"dynamic state render is bit exact after the UI-only upgrade");
        restore(*plugin,state);juce::MemoryBlock resaved;plugin->getStateInformation(resaved);
        c.require(resaved.getSize()>0,"current wrapper re-saves dynamic host state");
        juce::String error;auto reopened=format.createInstanceFromDescription(desc,sampleRate,blockSize,error);
        c.require(reopened!=nullptr,"fresh instance for dynamic re-save/reopen");auto second=object();
        if(reopened){
            restore(*reopened,resaved);compareParameters(c,*reopened,meta["parameters"],"re-saved v0.2 dynamic state",99);
            c.require(reopened->getLatencySamples()==0,"reopened dynamic state retains zero latency");
            second=compareAudio(c,render(*reopened,c,true),gold,"re-saved dynamic render");
            c.require(static_cast<bool>(second["bit_exact"]),"re-saved dynamic render is bit exact");reopened->releaseResources();
        }
        auto report=object();put(report,"passed",c.failures==0);put(report,"checks",c.count+1);put(report,"failures",c.failures);
        put(report,"mode","verify-dynamic");put(report,"tested_version",desc.version);put(report,"baseline_version",meta["plugin_version"]);
        put(report,"parameter_count",99);put(report,"fixture_directory",folder.getFullPathName());
        put(report,"old_dynamic_state_render",first);put(report,"resaved_dynamic_render",second);
        put(report,"scope","Actual VST3 identity, all 99 parameter IDs/order/metadata/values, opaque released v0.2 state with nondefault dynamics on every band, five active dynamic bands using all channel modes, gain and dynamic host automation, exact audio and fresh-instance re-save/reopen. No FL project/UI claim.");
        c.require(saveJson(juce::File::getCurrentWorkingDirectory().getChildFile("GILLEQ-vst3-dynamic-upgrade-report.json"),report),"write dynamic upgrade report");
    }
    plugin->releaseResources();std::cout<<"Dynamic "<<(capture?"capture":"verification")<<": "<<c.count<<" checks, "<<c.failures<<" failures.\n";
    return c.failures?1:0;
}
}

int main(int argc,char** argv){
    if(argc==2)return impulseMain(argc,argv);
    if(argc!=4){std::cerr<<"Usage: host-test <VST3 bundle> [--capture-legacy|--verify-legacy|--capture-dynamic|--verify-dynamic <fixture folder>]\n";return 2;}
    const juce::String mode(argv[2]);
    const bool dynamicMode=mode=="--capture-dynamic" || mode=="--verify-dynamic";
    if(mode!="--capture-legacy" && mode!="--verify-legacy" && !dynamicMode)return 2;
    juce::ScopedJuceInitialiser_GUI gui;juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types,juce::File(juce::String(argv[1])).getFullPathName());
    if(types.size()!=1){std::cerr<<"Expected one VST3 type, found "<<types.size()<<'\n';return 1;}
    juce::String error;auto plugin=format.createInstanceFromDescription(*types[0],sampleRate,blockSize,error);
    if(!plugin){std::cerr<<error<<'\n';return 1;}
    if(dynamicMode)return dynamicUpgrade(format,*types[0],plugin,juce::File(juce::String(argv[3])),mode=="--capture-dynamic");
    return legacy(format,*types[0],plugin,juce::File(juce::String(argv[3])),mode=="--capture-legacy");
}
