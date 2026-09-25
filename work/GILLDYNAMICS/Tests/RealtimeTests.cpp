#include "PluginProcessor.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

// Bounded, headless processor checks. Only the current audio-test thread is
// counted, so unrelated JUCE message-thread work cannot contaminate the result.
// This observes C++ new/new[] (including JUCE String storage), not all CRT malloc.
namespace allocation_probe {
thread_local bool active=false;
std::atomic<std::size_t> count{0};
}
void* operator new(std::size_t n){
    if(allocation_probe::active)allocation_probe::count.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

namespace {
int failures=0,checks=0;
void check(bool success,const char* message,double detail=0){
    ++checks;if(!success)++failures;
    std::printf("%s %s %.10g\n",success?"PASS":"FAIL",message,detail);
    std::fflush(stdout);
}
void prepare(GillDynamicsProcessor& p,int block=257){
    p.setPlayConfigDetails(2,2,48000,block);p.prepareToPlay(48000,block);
}
float input(int sample,int channel){
    const double t=sample/48000.;
    return float((channel?.09:.18)*std::sin(2*juce::MathConstants<double>::pi*317*t)
                 +.04*std::sin(2*juce::MathConstants<double>::pi*1733*t+.3*channel));
}
void fill(juce::AudioBuffer<float>& b,int start){
    for(int c=0;c<b.getNumChannels();++c)for(int i=0;i<b.getNumSamples();++i)b.setSample(c,i,input(start+i,c));
}
void zeroGroups(GillDynamicsProcessor& p){
    // First synchronize so a locally stale zero cannot conceal a live nonzero.
    p.syncGroups();
    for(int g=1;g<=8;++g)for(const char* field:{"drive","trim","noise","bypass"})
        p.setValue("g"+juce::String(g)+field,0,false);
}
struct HostChanges final:juce::AudioProcessorListener{
    int changes=0;
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float)override{++changes;}
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails&)override{}
};
struct HostParameterCache final:juce::AudioProcessorListener{
    std::array<float,128> values{};
    std::array<bool,128> notified{};
    int changes=0;
    void clearNotifications(){notified.fill(false);changes=0;}
    void audioProcessorParameterChanged(juce::AudioProcessor*,int index,float value)override{
        if(index>=0&&index<int(values.size())){values[size_t(index)]=value;notified[size_t(index)]=true;}
        ++changes;
    }
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails&)override{}
};
void booleanStateRecall(){
    // Reproduces the validator failure without relying on random input: JUCE's
    // AudioParameterBool retains arbitrary host-normalised values, whereas the
    // APVTS mirror snaps them to 0/1. Both storage and host cache must be restored.
    const float fractions[2][4]={{.39821f,.104139f,.372881f,.499999f},
                                  {.60179f,.895861f,.627119f,.5f}};
    for(auto kind:{DynKind::Vox,DynKind::Opta,DynKind::Buss,DynKind::Quad,DynKind::Stage}){
        auto p=std::make_unique<GillDynamicsProcessor>(kind);
        if(kind==DynKind::Buss)zeroGroups(*p);
        HostParameterCache host;p->addListener(&host);
        bool restored=true,fixture=true,saveIsReadOnly=true;int trials=0;
        for(const auto& spec:p->definitions){
            auto* parameter=p->apvts.getParameter(spec.id);
            if(!parameter->isBoolean())continue;
            const auto index=size_t(parameter->getParameterIndex());
            for(int state=0;state<2;++state){
                parameter->setValueNotifyingHost(float(state));
                juce::MemoryBlock saved;p->getStateInformation(saved);
                for(float fraction:fractions[state]){
                    parameter->setValueNotifyingHost(fraction);
                    fixture=fixture&&parameter->getValue()==fraction&&p->value(spec.id)==state;
                    host.clearNotifications();
                    juce::MemoryBlock offGridSave;p->getStateInformation(offGridSave);
                    saveIsReadOnly=saveIsReadOnly&&host.changes==0&&parameter->getValue()==fraction;
                    p->setStateInformation(saved.getData(),int(saved.getSize()));
                    restored=restored&&parameter->getValue()==state&&p->value(spec.id)==state
                             &&host.notified[index]&&host.values[index]==state;
                    ++trials;
                }
            }
        }
        p->removeListener(&host);
        std::printf("BOOLEAN RECALL %s (%d off-grid cases)\n",p->getName().toRawUTF8(),trials);
        check(fixture&&trials>0,"off-grid host writes reproduce unchanged canonical APVTS mirror");
        check(restored,"OFF/ON recall restores parameter storage, DSP mirror and notified host cache");
        check(saveIsReadOnly,"saving off-grid boolean state never mutates parameters or notifies host");
    }
}
void partialBooleanStateRecall(){
    auto p=std::make_unique<GillDynamicsProcessor>(DynKind::Quad);
    auto* restored=p->apvts.getParameter("b1solo");
    auto* invalid=p->apvts.getParameter("b1bypass");
    auto* missing=p->apvts.getParameter("b2solo");
    restored->setValueNotifyingHost(.39821f);
    invalid->setValueNotifyingHost(.372881f);
    missing->setValueNotifyingHost(.104139f);
    const auto full=p->apvts.copyState();juce::ValueTree partial(full.getType());
    const auto add=[&](const char* id,const juce::var& value){
        auto item=full.getChildWithProperty("id",id).createCopy();
        item.setProperty("value",value,nullptr);partial.appendChild(item,nullptr);
    };
    add("b1solo",1);add("b1solo",0); // Last valid duplicate wins.
    add("b1bypass","NaN");
    juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(*partial.createXml(),state);
    HostParameterCache host;p->addListener(&host);
    p->setStateInformation(state.getData(),int(state.getSize()));
    check(restored->getValue()==0&&host.notified[size_t(restored->getParameterIndex())]
          &&host.values[size_t(restored->getParameterIndex())]==0,
          "partial boolean recall reasserts last valid duplicate in storage and host cache");
    check(invalid->getValue()==.372881f&&missing->getValue()==.104139f
          &&!host.notified[size_t(invalid->getParameterIndex())]
          &&!host.notified[size_t(missing->getParameterIndex())],
          "partial boolean recall leaves missing and non-finite fields untouched");
    p->removeListener(&host);
}
void unchangedLocalGroupRecall(){
    auto a=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);
    zeroGroups(*a);
    auto b=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);
    b->setValue("group",1,false);
    juce::MemoryBlock savedZero;b->getStateInformation(savedZero);
    a->setValue("g1drive",10,false); // B's APVTS intentionally remains locally 0.
    check(b->value("g1drive")==0,"recall fixture has a locally unchanged group parameter");
    b->setStateInformation(savedZero.getData(),int(savedZero.getSize()));
    a->syncGroups();
    check(std::abs(a->value("g1drive"))<1e-5,
          "state recall republishes saved group value even when local APVTS is unchanged",a->value("g1drive"));
    zeroGroups(*a);
}
void stateSaveDoesNotEmitAutomation(){
    auto a=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);zeroGroups(*a);
    auto b=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);
    HostChanges observer;b->addListener(&observer);
    a->setValue("g2trim",-5,false);
    juce::MemoryBlock state;b->getStateInformation(state);
    check(observer.changes==0,"state serialization does not send parameter changes to the host",observer.changes);
    auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()));
    const auto saved=juce::ValueTree::fromXml(*xml);
    const float savedTrim=float(saved.getChildWithProperty("id","g2trim").getProperty("value"));
    check(savedTrim==-5&&b->value("g2trim")==0,
          "saved state captures global groups without mutating the local parameter view",savedTrim);
    b->removeListener(&observer);zeroGroups(*a);
}
void partialGroupRecall(){
    auto a=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);zeroGroups(*a);
    auto b=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);
    a->setValue("g2drive",6,false);a->setValue("g2trim",4,false);
    const auto full=b->apvts.copyState();juce::ValueTree partial(full.getType());
    auto valid=full.getChildWithProperty("id","g1drive").createCopy();
    valid.setProperty("value",100,nullptr);partial.appendChild(valid,nullptr);
    auto invalid=full.getChildWithProperty("id","g2drive").createCopy();
    invalid.setProperty("value","NaN",nullptr);partial.appendChild(invalid,nullptr);
    juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(*partial.createXml(),state);
    b->setStateInformation(state.getData(),int(state.getSize()));a->syncGroups();
    check(a->value("g1drive")==12&&a->value("g2drive")==6&&a->value("g2trim")==4,
          "partial recall clamps valid group fields and preserves missing/invalid live fields");
    zeroGroups(*a);
}
void groupLifetime(){
    auto a=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);zeroGroups(*a);
    for(int g=1;g<=8;++g){
        a->setValue("g"+juce::String(g)+"drive",float(g),false);
        a->setValue("g"+juce::String(g)+"trim",float(-g),false);
        a->setValue("g"+juce::String(g)+"noise",1,false);
        a->setValue("g"+juce::String(g)+"bypass",1,false);
    }
    auto b=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);
    const auto retained=[&]{
        b->syncGroups();bool same=true;
        for(int g=1;g<=8;++g)
            same=same&&b->value("g"+juce::String(g)+"drive")==g
                     &&b->value("g"+juce::String(g)+"trim")==-g
                     &&b->value("g"+juce::String(g)+"noise")==1
                     &&b->value("g"+juce::String(g)+"bypass")==1;
        return same;
    };
    check(retained(),"new BUSS instance joins the currently live shared groups");
    a.reset();
    {auto unrelated=std::make_unique<GillDynamicsProcessor>(DynKind::Opta);}
    check(retained(),"destroying a peer or another plugin leaves surviving BUSS groups intact");
    b.reset();
    auto fresh=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);bool cleared=true;
    for(int g=1;g<=8;++g)for(const char* field:{"drive","trim","noise","bypass"})
        cleared=cleared&&fresh->value("g"+juce::String(g)+field)==0;
    check(cleared,"groups reset after the final BUSS instance is destroyed and recreated");
}
void processorAllocations(){
    for(auto kind:{DynKind::Vox,DynKind::Opta,DynKind::Buss,DynKind::Quad,DynKind::Stage}){
        auto p=std::make_unique<GillDynamicsProcessor>(kind);
        if(kind==DynKind::Buss)zeroGroups(*p);
        p->selectPreset(0,false);prepare(*p);
        juce::AudioBuffer<float> b(2,257);juce::MidiBuffer midi;
        fill(b,0);p->processBlock(b,midi); // Warm any lazy native facilities.
        const auto before=allocation_probe::count.load();
        allocation_probe::active=true;
        for(int block=0;block<200;++block){
            fill(b,block*257);
            if(block%7==0)p->processBlockBypassed(b,midi);else p->processBlock(b,midi);
        }
        allocation_probe::active=false;
        const auto count=allocation_probe::count.load()-before;
        std::printf("PROCESSOR %s\n",p->getName().toRawUTF8());
        check(count==0,"native processing/host bypass use no C++ heap allocation",double(count));
    }
    auto p=std::make_unique<GillDynamicsProcessor>(DynKind::Buss);zeroGroups(*p);prepare(*p);
    std::vector<juce::RangedAudioParameter*> parameters;
    for(const auto& spec:p->definitions)
        if(spec.id.size()>2&&spec.id[0]=='g'&&spec.id[1]>='1'&&spec.id[1]<='8')
            parameters.push_back(p->apvts.getParameter(spec.id));
    for(auto* parameter:parameters)parameter->setValueNotifyingHost(.25f);
    const auto before=allocation_probe::count.load();
    allocation_probe::active=true;
    for(int block=0;block<64;++block)
        for(auto* parameter:parameters)parameter->setValueNotifyingHost(block%2?.25f:.75f);
    allocation_probe::active=false;
    const auto count=allocation_probe::count.load()-before;
    check(count==0,"cached group automation callbacks use no C++ heap allocation",double(count));
    zeroGroups(*p);
}
void metering(){
    for(auto kind:{DynKind::Vox,DynKind::Opta,DynKind::Buss,DynKind::Quad,DynKind::Stage}){
        auto p=std::make_unique<GillDynamicsProcessor>(kind);
        if(kind==DynKind::Buss)zeroGroups(*p);
        p->selectPreset(11,false);prepare(*p,257);
        juce::AudioBuffer<float> b(2,257);juce::MidiBuffer midi;
        double expectedIn=0,expectedOut=0;
        std::array<double,2> expectedChannels{};
        // Independent 300-ms reference is integrated over the actual input and
        // actual returned output, so a swapped meter or block-average update is
        // detected without duplicating any audio processing algorithm.
        const double retain=std::exp(-1/(48000.*.3));
        int sample=0;
        for(int block=0;block<180;++block){
            fill(b,sample);
            std::array<double,257> powers{};
            for(int i=0;i<257;++i)for(int c=0;c<2;++c){
                const double x=b.getSample(c,i);powers[i]+=.5*x*x;
            }
            p->processBlock(b,midi);
            for(int i=0;i<257;++i){
                double outputPower=0;
                for(int c=0;c<2;++c){const double x=b.getSample(c,i);outputPower+=.5*x*x;expectedChannels[c]=retain*expectedChannels[c]+(1-retain)*x*x;}
                expectedIn=retain*expectedIn+(1-retain)*powers[i];
                expectedOut=retain*expectedOut+(1-retain)*outputPower;
            }
            sample+=257;
        }
        const double error=std::max({std::abs(p->inputRms.load()-std::sqrt(expectedIn)),
                                   std::abs(p->outputRms.load()-std::sqrt(expectedOut)),
                                   std::abs(p->channelOutputRms[0].load()-std::sqrt(expectedChannels[0])),
                                   std::abs(p->channelOutputRms[1].load()-std::sqrt(expectedChannels[1]))});
        std::printf("METER %s\n",p->getName().toRawUTF8());
        check(error<2e-7,"processor input/output/channel RMS agree with samplewise reference",error);
    }
}
void stageBinding(){
    auto p=std::make_unique<GillDynamicsProcessor>(DynKind::Stage);
    p->selectPreset(11,false);
    p->setValue("x",-.42f,false);p->setValue("distance",.38f,false);
    p->setValue("spread",23,false);p->setValue("doubler",71,false);
    p->setValue("mix",62,false);p->setValue("output",-3,false);p->setValue("mono",0,false);
    prepare(*p,257);
    gill::StageDSP reference;
    reference.setParameters(p->value("x"),p->value("distance"),p->value("spread"),
                            p->value("doubler"),p->value("mix"),p->value("output"),false);
    reference.prepare(48000,257,2);
    juce::AudioBuffer<float> b(2,257),r(2,257);juce::MidiBuffer midi;
    double worst=0;
    for(int block=0;block<120;++block){
        fill(b,block*257);r.makeCopyOf(b,true);p->processBlock(b,midi);
        reference.process(r.getArrayOfWritePointers(),2,257);
        for(int c=0;c<2;++c)for(int i=0;i<257;++i)
            worst=std::max(worst,std::abs(double(b.getSample(c,i)-r.getSample(c,i))));
    }
    check(worst<1e-7,"STAGE wrapper binds position/distance/spread/doubler/mix/output in correct order",worst);
    p->setValue("direct",0,false);
    reference.setParameters(p->value("x"),p->value("distance"),p->value("spread"),
                            p->value("doubler"),p->value("mix"),p->value("output"),false,false);
    worst=0;
    for(int block=120;block<140;++block){
        fill(b,block*257);r.makeCopyOf(b,true);p->processBlock(b,midi);
        reference.process(r.getArrayOfWritePointers(),2,257);
        for(int c=0;c<2;++c)for(int i=0;i<257;++i)
            worst=std::max(worst,std::abs(double(b.getSample(c,i)-r.getSample(c,i))));
    }
    check(worst<1e-7,"STAGE DIRECT OFF reaches the engine with its actual parameter transition",worst);
    p->setValue("mono",1,false);
    for(int block=0;block<10;++block){fill(b,block*257);p->processBlock(b,midi);}
    bool mono=true;for(int i=0;i<257;++i)mono=mono&&std::abs(b.getSample(0,i)-b.getSample(1,i))<1e-7f;
    check(mono,"STAGE mono parameter reaches the actual processing engine");
}
void stageDirectMigration(){
    auto p=std::make_unique<GillDynamicsProcessor>(DynKind::Stage);
    p->setValue("direct",0,false);p->setValue("mix",63,false);
    juce::MemoryBlock saved;p->getStateInformation(saved);
    auto xml=juce::AudioProcessor::getXmlFromBinary(saved.getData(),int(saved.getSize()));
    auto current=juce::ValueTree::fromXml(*xml);
    check(int(current.getProperty("version"))==2,"STAGE saves the DIRECT-aware state version");
    p->setValue("direct",1,false);p->setStateInformation(saved.getData(),int(saved.getSize()));
    check(p->value("direct")==0,"STAGE version2 round trip retains deliberately muted direct path");
    auto missing=current.createCopy();missing.removeChild(missing.getChildWithProperty("id","direct"),nullptr);
    const auto recall=[&](const juce::ValueTree& tree){juce::MemoryBlock bytes;juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),bytes);p->setStateInformation(bytes.getData(),int(bytes.getSize()));};
    recall(missing);
    check(p->value("direct")==0,"STAGE version2 partial state preserves an omitted DIRECT value");
    missing.setProperty("version",1,nullptr);recall(missing);
    check(p->value("direct")==1&&std::abs(p->value("mix")-63)<1e-5,"STAGE legacy state restores original direct-on sound into a direct-off instance");
    p->setValue("direct",0,false);missing.removeProperty("version",nullptr);recall(missing);
    check(p->value("direct")==1,"STAGE unversioned legacy state also restores its original direct path");
    p->setValue("direct",0,false);juce::ValueTree empty(current.getType());empty.setProperty("version",1,nullptr);recall(empty);
    check(p->value("direct")==0,"empty legacy state cannot spuriously alter STAGE direct routing");
}
void wetToHostBypassPdc(){
    for(auto kind:{DynKind::Vox,DynKind::Opta,DynKind::Buss,DynKind::Quad,DynKind::Stage}){
        auto p=std::make_unique<GillDynamicsProcessor>(kind);
        if(kind==DynKind::Buss)zeroGroups(*p);
        prepare(*p,257);juce::AudioBuffer<float> b(2,257);juce::MidiBuffer midi;
        for(int block=0;block<20;++block){fill(b,block*257);p->processBlock(b,midi);}
        bool aligned=true;const int delay=p->getLatencySamples();
        for(int block=20;block<25;++block){
            fill(b,block*257);p->processBlockBypassed(b,midi);
            // Skip only the declared 5-ms crossfade, not the DSP delay.
            for(int i=0;i<257;++i)if(block>20)
                for(int c=0;c<2;++c)
                    aligned=aligned&&b.getSample(c,i)==input(block*257+i-delay,c);
        }
        std::printf("PDC %s\n",p->getName().toRawUTF8());
        check(aligned,"active effect to host bypass retains sample-exact reported delay");
    }
}
}
int main(){
    juce::ScopedJuceInitialiser_GUI gui;
    booleanStateRecall();partialBooleanStateRecall();
    unchangedLocalGroupRecall();stateSaveDoesNotEmitAutomation();partialGroupRecall();groupLifetime();processorAllocations();
    metering();stageBinding();stageDirectMigration();wetToHostBypassPdc();
    std::printf("RESULT %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
