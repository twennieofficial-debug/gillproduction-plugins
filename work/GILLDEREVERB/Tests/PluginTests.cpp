#include "../../GILLCommon/QualityTests.h"
#include "PluginProcessor.h"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {
int checks=0, failures=0, automationCases=0, uiChanges=0;
void check(bool pass,const char* name) {
    ++checks;
    if(!pass){++failures;static juce::StringArray reported;if(!reported.contains(name)){reported.add(name);std::cerr<<"FAIL: "<<name<<"\n";}}
}
void parameterGridRegression() {
    // Decimal grids with negative starts must preserve the exact neutral point.
    // On Apple Clang, implicit FMA contraction previously turned OUTPUT=0 into
    // -2.682209e-7 dB after a state round-trip. The real dry-route checks below
    // deliberately keep their bit-exact comparisons in both audio precisions.
    volatile float decimalStep=0.01f;
    const float fusedZero=std::fma(decimalStep,1200.0f,-12.0f);
    check(fusedZero!=0.0f,"regression fixture exercises an actual float FMA residual");
    for(const auto config:std::array<std::array<float,3>,4>{{{{-12.f,12.f,.01f}},{{-24.f,24.f,.01f}},{{-24.f,12.f,.1f}},{{-18.f,6.f,.1f}}}}) {
        volatile float low=config[0],high=config[1],step=config[2];
        juce::NormalisableRange<float> range(low,high,step);
        for(float expected:{-12.f,-6.f,0.f,6.f}) {
            const float snapped=range.snapToLegalValue(expected);
            check(snapped==expected,"decimal gain grid preserves exact integer and zero dB values");
            if(snapped!=expected)std::cerr<<"GRID low="<<config[0]<<" step="<<config[2]<<" expected="<<expected<<" actual="<<std::scientific<<snapped<<std::defaultfloat<<"\n";
        }
    }
}
void set(GillDereverbAudioProcessor& p,const juce::String& id,float value) {
    auto* parameter=p.apvts.getParameter(id);check(parameter!=nullptr,"parameter exists");
    if(parameter)parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
float raw(GillDereverbAudioProcessor& p,const juce::String& id) {
    auto* value=p.apvts.getRawParameterValue(id);check(value!=nullptr,"raw parameter exists");return value?value->load():0.f;
}
void renderPng(juce::AudioProcessorEditor& editor,int w,int h,const char* name) {
    editor.setSize(w,h);auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.f);
    juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile(name));
    if(output.openedOk()){output.setPosition(0);output.truncate();}
    juce::PNGImageFormat png;check(output.openedOk()&&png.writeImageToStream(image,output),"editor PNG snapshot");
}
void collect(juce::Component& c,std::vector<juce::Slider*>& sliders,std::vector<juce::Button*>& buttons) {
    // LIVE/PRO has a separate full interaction test in QualityTests.h.
    if(dynamic_cast<gill::QualitySelector*>(&c))return;

    if(auto* slider=dynamic_cast<juce::Slider*>(&c))sliders.push_back(slider);
    if(auto* button=dynamic_cast<juce::Button*>(&c))buttons.push_back(button);
    for(auto* child:c.getChildren())collect(*child,sliders,buttons);
}
void restoreTree(GillDereverbAudioProcessor& p,const juce::ValueTree& tree) {
    juce::MemoryBlock data;if(auto xml=tree.createXml())juce::AudioProcessor::copyXmlToBinary(*xml,data);
    p.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
}
void enterLegacy(GillDereverbAudioProcessor& p) {
    auto tree=p.apvts.copyState();tree.setProperty("version",1,nullptr);tree.removeProperty("automatic",nullptr);
    restoreTree(p,tree);check(!p.isAutomaticMode(),"version 1 restore enters legacy mode");
}
juce::ValueTree savedTree(GillDereverbAudioProcessor& p) {
    juce::MemoryBlock data;p.getStateInformation(data);
    if(auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),static_cast<int>(data.getSize())))return juce::ValueTree::fromXml(*xml);
    check(false,"saved state is valid XML");return {};
}
class StateListener final : public juce::AudioProcessorListener {
public:
    int nonParameterChanges=0,gestureStarts=0,gestureEnds=0;
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {}
    void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails& details) override {if(details.nonParameterStateChanged)++nonParameterChanges;}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*,int) override {++gestureStarts;}
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*,int) override {++gestureEnds;}
};
uint64_t componentHash(juce::Component& component) {
    const auto image=component.createComponentSnapshot(component.getLocalBounds());uint64_t result=1469598103934665603ull;
    for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x){result^=image.getPixelAt(x,y).getARGB();result*=1099511628211ull;}
    return result;
}
using Configure=std::function<void(GillDereverbAudioProcessor&)>;
template<typename T> using Signal=std::array<std::vector<T>,2>;
template<typename T> Signal<T> signal(int count) {
    Signal<T> input;for(auto& channel:input)channel.resize(static_cast<size_t>(count));
    std::mt19937 random(0xD3E3B);std::uniform_real_distribution<double> noise(-1,1);
    for(int n=0;n<count;++n)for(int c=0;c<2;++c) {
        const double envelope=n<4096?1.0:std::exp(-(n-4096)/5500.0);
        input[static_cast<size_t>(c)][static_cast<size_t>(n)]=static_cast<T>(envelope*(0.19*noise(random)+0.10*std::sin(2.0*gilldereverb::pi*(837.0+c*319.0)*n/48000.0)));
    }
    return input;
}
template<typename T> Signal<T> render(const Signal<T>& input,const Configure& configure,bool hostBypassed=false,int channels=2,bool automatic=false) {
    GillDereverbAudioProcessor p;if(!automatic)enterLegacy(p);p.setPlayConfigDetails(channels,channels,48000,512);p.prepareToPlay(48000,512);
    check(p.getLatencySamples()==2048,"reported latency is exactly 2048 samples");
    juce::AudioBuffer<T> audio(channels,128);juce::MidiBuffer midi;audio.clear();p.processBlock(audio,midi);
    configure(p);
    // Apply changes after initialization and allow the 20 ms ramps to settle.
    for(int block=0;block<32;++block){audio.clear();if(hostBypassed)p.processBlockBypassed(audio,midi);else p.processBlock(audio,midi);}
    Signal<T> result;const auto count=static_cast<int>(input[0].size());for(auto& channel:result)channel.resize(static_cast<size_t>(count));
    const std::array<int,5> blockSizes{1,31,127,257,512};int blockNumber=0;
    for(int offset=0;offset<count;) {
        const int size=std::min(blockSizes[static_cast<size_t>(blockNumber++%5)],count-offset);audio.setSize(channels,size,false,false,true);
        for(int c=0;c<channels;++c)for(int i=0;i<size;++i)audio.setSample(c,i,input[static_cast<size_t>(c)][static_cast<size_t>(offset+i)]);
        if(hostBypassed)p.processBlockBypassed(audio,midi);else p.processBlock(audio,midi);
        for(int c=0;c<channels;++c)for(int i=0;i<size;++i){const auto v=audio.getSample(c,i);check(std::isfinite(static_cast<double>(v)),"rendered sample is finite");result[static_cast<size_t>(c)][static_cast<size_t>(offset+i)]=v;}
        offset+=size;
    }
    p.releaseResources();return result;
}
template<typename T> void testAudioRoutes() {
    const auto input=signal<T>(12288);
    const std::array<Configure,4> dryRoutes{
        [](GillDereverbAudioProcessor& p){set(p,"amount",100);set(p,"output",12);set(p,"removed",1);set(p,"mix",43);set(p,"bypass",1);},
        [](GillDereverbAudioProcessor& p){set(p,"amount",100);set(p,"mix",0);},
        [](GillDereverbAudioProcessor& p){set(p,"amount",0);},
        [](GillDereverbAudioProcessor& p){set(p,"amount",100);set(p,"output",12);set(p,"removed",1);set(p,"mix",43);}
    };
    const std::array<const char*,4> names{"plug-in BYPASS exact delayed dry","MIX zero exact delayed dry","AMOUNT zero exact delayed dry","host BYPASS exact delayed dry"};
    for(size_t route=0;route<dryRoutes.size();++route) {
        const auto output=render(input,dryRoutes[route],route==3);
        for(size_t c=0;c<2;++c)for(size_t i=0;i<input[c].size();++i)check(output[c][i]==(i<2048?T{}:input[c][i-2048]),names[route]);
    }
    const auto trim=render(input,[](GillDereverbAudioProcessor& p){set(p,"amount",0);set(p,"output",6);});
    const double linearTrim=std::pow(10.0,6.0/20.0);
    const double tolerance=std::is_same<T,float>::value?2.e-7:1.e-13;
    for(size_t c=0;c<2;++c)for(size_t i=0;i<input[c].size();++i) {
        const auto expected=i<2048?T{}:static_cast<T>(static_cast<double>(input[c][i-2048])*linearTrim);
        check(std::abs(static_cast<double>(trim[c][i])-static_cast<double>(expected))<tolerance,"OUTPUT trim applies correct linear gain");
    }
    auto reduction=[](GillDereverbAudioProcessor& p){set(p,"amount",100);set(p,"room",1500);set(p,"preserve",0);};
    const auto wet=render(input,reduction);
    const auto removed=render(input,[&](GillDereverbAudioProcessor& p){reduction(p);set(p,"removed",1);});
    const auto removedMix0=render(input,[&](GillDereverbAudioProcessor& p){reduction(p);set(p,"removed",1);set(p,"mix",0);});
    const auto removedTrim=render(input,[&](GillDereverbAudioProcessor& p){reduction(p);set(p,"removed",1);set(p,"mix",37);set(p,"output",12);});
    double removedEnergy=0;
    for(size_t c=0;c<2;++c)for(size_t i=0;i<input[c].size();++i) {
        const auto dry=i<2048?T{}:input[c][i-2048];
        const double expected=static_cast<double>(dry)-static_cast<double>(wet[c][i]);
        check(std::abs(static_cast<double>(removed[c][i])-expected)<tolerance,"REMOVED equals aligned dry minus processed before mix");
        check(std::abs(static_cast<double>(removed[c][i])-static_cast<double>(removedMix0[c][i]))<tolerance,"REMOVED monitor is independent of MIX");
        check(std::abs(static_cast<double>(removed[c][i])-static_cast<double>(removedTrim[c][i]))<tolerance,"REMOVED monitor is independent of OUTPUT trim");
        removedEnergy+=static_cast<double>(removed[c][i])*static_cast<double>(removed[c][i]);
    }
    check(removedEnergy>1.e-5,"REMOVED test exercises a nonzero difference");
    const auto mono=render(input,[](GillDereverbAudioProcessor& p){set(p,"mix",0);},false,1);
    for(size_t i=0;i<input[0].size();++i)check(mono[0][i]==(i<2048?T{}:input[0][i-2048]),"mono dry route has exact latency");
    const auto automaticDry=render(input,[](GillDereverbAudioProcessor& p){set(p,"amount",0);set(p,"mix",23);set(p,"output",12);set(p,"removed",1);},false,2,true);
    for(size_t c=0;c<2;++c)for(size_t i=0;i<input[c].size();++i)check(automaticDry[c][i]==(i<2048?T{}:input[c][i-2048]),"automatic amount zero is exact delayed dry in both precisions");
    const auto automaticDefault=render(input,[](GillDereverbAudioProcessor& p){set(p,"amount",73);},false,2,true);
    const auto automaticHidden=render(input,[](GillDereverbAudioProcessor& p){set(p,"amount",73);set(p,"room",1500);set(p,"preserve",0);set(p,"low",1000);set(p,"high",1000);set(p,"mix",0);set(p,"output",12);set(p,"removed",1);},false,2,true);
    for(size_t c=0;c<2;++c)for(size_t i=0;i<input[c].size();++i)check(automaticDefault[c][i]==automaticHidden[c][i],"automatic processing ignores hidden legacy controls");
}
}

int main() {
    const auto started=std::chrono::steady_clock::now();juce::ScopedJuceInitialiser_GUI gui;
    // Update06: exercise real LIVE/PRO host state, audio timing and UI.
    gill::testing::qualityRoutes([]{return std::make_unique<GillDereverbAudioProcessor>();},[](bool ok,const std::string& why){check(ok,why.c_str());});

    parameterGridRegression();
    GillDereverbAudioProcessor p;
    check(p.getParameters().size()==10,"nine retained parameter IDs plus appended LIVE/PRO");
    check(p.getName()=="GILLDEREVERB","uppercase product name");check(p.supportsDoublePrecisionProcessing(),"double precision supported");
    check(p.getBypassParameter()==p.apvts.getParameter("bypass"),"host bypass parameter retained");
    check(p.isAutomaticMode(),"new instances start in automatic mode");
    auto freshState=savedTree(p);check(static_cast<int>(freshState.getProperty("version"))==2,"new state uses version 2");
    check(freshState.hasProperty("automatic")&&static_cast<bool>(freshState.getProperty("automatic")),"new state explicitly saves automatic true");
    testAudioRoutes<float>();testAudioRoutes<double>();
    p.setPlayConfigDetails(2,2,48000,256);p.prepareToPlay(48000,256);
    set(p,"amount",81.3f);set(p,"room",937);set(p,"preserve",48.2f);set(p,"low",172.3f);set(p,"high",13749);set(p,"mix",78.1f);set(p,"output",-3.25f);set(p,"removed",1);
    enterLegacy(p);set(p,"amount",67.2f);check(!p.isAutomaticMode(),"host amount automation leaves legacy processing active");
    p.copyAtoB();set(p,"amount",22.2f);p.swapAB();check(std::abs(raw(p,"amount")-67.2f)<0.002,"retained A/B API restores legacy settings");
    p.swapAB();check(std::abs(raw(p,"amount")-22.2f)<0.002,"retained A/B API returns edited settings");
    juce::MemoryBlock legacyState;p.getStateInformation(legacyState);std::vector<float> remembered;
    for(auto* parameter:p.getParameters())remembered.push_back(parameter->getValue());
    auto legacyTree=savedTree(p);
    check(static_cast<int>(legacyTree.getProperty("version"))==2,"legacy re-save upgrades format to version 2");
    check(legacyTree.hasProperty("automatic")&&!static_cast<bool>(legacyTree.getProperty("automatic")),"legacy re-save explicitly retains automatic false");
    p.resetAllParameters();check(p.isAutomaticMode(),"reset API restores automatic mode");
    for(auto* parameter:p.getParameters())check(std::abs(parameter->getValue()-parameter->getDefaultValue())<1.e-6,"reset API restores parameter defaults");
    p.setStateInformation(legacyState.getData(),static_cast<int>(legacyState.getSize()));
    check(!p.isAutomaticMode(),"version 2 explicit false restores legacy mode");
    for(int i=0;i<p.getParameters().size();++i)check(std::abs(p.getParameters()[i]->getValue()-remembered[static_cast<size_t>(i)])<1.e-6,"legacy state round trip preserves all old parameter values");
    auto secondLegacy=savedTree(p);restoreTree(p,secondLegacy);check(!p.isAutomaticMode(),"legacy false survives repeated version 2 round trips");
    p.setStateInformation(nullptr,20);p.setStateInformation("junk",4);p.setStateInformation(legacyState.getData(),0);p.setStateInformation(legacyState.getData(),1024*1024+1);
    for(int i=0;i<p.getParameters().size();++i)check(std::abs(p.getParameters()[i]->getValue()-remembered[static_cast<size_t>(i)])<1.e-6,"malformed state preserves valid parameters");
    check(!p.isAutomaticMode(),"malformed state does not migrate legacy mode");
    juce::ValueTree flagOnly("GILLDEREVERB_STATE");flagOnly.setProperty("version",2,nullptr);flagOnly.setProperty("automatic",true,nullptr);restoreTree(p,flagOnly);
    check(!p.isAutomaticMode(),"state without known parameters cannot change mode");
    auto poisoned=p.apvts.copyState();poisoned.setProperty("version",2,nullptr);poisoned.setProperty("automatic",false,nullptr);
    poisoned.getChildWithProperty("id","amount").setProperty("value",std::numeric_limits<double>::quiet_NaN(),nullptr);
    poisoned.getChildWithProperty("id","room").setProperty("value",999999.0,nullptr);
    poisoned.getChildWithProperty("id","output").setProperty("value",-999999.0,nullptr);
    juce::ValueTree unknown("PARAM");unknown.setProperty("id","unknown_parameter",nullptr);unknown.setProperty("value",123,nullptr);poisoned.appendChild(unknown,nullptr);restoreTree(p,poisoned);
    check(std::isfinite(raw(p,"amount"))&&raw(p,"amount")>=0&&raw(p,"amount")<=100,"nonfinite state cannot poison amount");
    check(raw(p,"room")==1500&&raw(p,"output")==-12,"legacy state clamps hidden values to valid ranges");check(p.getParameters().size()==10,"unknown state parameter is ignored");
    restoreTree(p,freshState);check(p.isAutomaticMode(),"automatic true restores from saved version 2 state");
    std::mt19937 random(0xD3E3B00);std::uniform_real_distribution<float> uniform(0,1);
    juce::AudioBuffer<float> audio(2,64);juce::MidiBuffer midi;
    for(int iteration=0;iteration<10000;++iteration) {
        if(iteration==5000)enterLegacy(p);
        for(auto* parameter:p.getParameters()) {
            float v=uniform(random);if(iteration%257==0)v=0;if(iteration%263==0)v=1;parameter->setValueNotifyingHost(v);
            check(std::isfinite(parameter->getValue())&&parameter->getValue()>=0&&parameter->getValue()<=1,"automation remains in parameter ranges");
        }
        for(int c=0;c<2;++c)for(int i=0;i<audio.getNumSamples();++i)audio.setSample(c,i,(uniform(random)-0.5f)*0.3f);
        p.processBlock(audio,midi);
        for(int c=0;c<2;++c)for(int i=0;i<audio.getNumSamples();++i)check(std::isfinite(audio.getSample(c,i)),"automatic and legacy automated audio remain finite");
        check(std::isfinite(p.getInputDb())&&std::isfinite(p.getOutputDb())&&std::isfinite(p.getReductionDb())&&p.getReductionDb()>=0,"automated metering stays finite");
        check(p.getLatencySamples()==2048,"latency stays constant under automation");
        check(p.isAutomaticMode()==(iteration<5000),"host parameter updates never migrate processing mode");++automationCases;
    }
    p.resetAllParameters();enterLegacy(p);set(p,"amount",47.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());check(editor!=nullptr,"CORE editor creates");
    check(!p.isAutomaticMode(),"opening CORE editor does not migrate old projects");
    if(editor) {
        std::vector<juce::Slider*> sliders;std::vector<juce::Button*> buttons;collect(*editor,sliders,buttons);
        check(sliders.size()==1,"CORE has exactly one continuous control");check(buttons.empty(),"CORE has no extra effect buttons beyond separately tested LIVE/PRO");
        check(editor->getWidth()==320&&editor->getHeight()==300,"CORE default dimensions are compact 320 by 300");
        auto* constrainer=editor->getConstrainer();check(constrainer!=nullptr,"CORE has a resize constrainer");
        if(constrainer){check(std::abs(constrainer->getFixedAspectRatio()-320./300.)<1.e-9,"CORE resize aspect ratio matches its compact panel");check(constrainer->getMinimumWidth()==320&&constrainer->getMinimumHeight()==300,"CORE minimum size is 320 by 300");check(constrainer->getMaximumWidth()==640&&constrainer->getMaximumHeight()==600,"CORE maximum size is twice the native panel");}
        if(sliders.size()==1) {
            auto* amount=sliders.front();check(amount->getName()=="AMOUNT","sole knob is AMOUNT");
            check(std::abs(amount->getMinimum())<1.e-9&&std::abs(amount->getMaximum()-100)<1.e-9,"knob covers zero to one hundred percent");
            check(std::abs(amount->getDoubleClickReturnValue()-55)<1.e-9,"double-click reset value is 55 percent");
            StateListener listener;p.addListener(&listener);
            set(p,"amount",47.3f);check(std::abs(amount->getValue()-47.3)<0.002,"host automation updates the visible knob");
            check(!p.isAutomaticMode()&&listener.nonParameterChanges==0,"host knob synchronization does not migrate or mark legacy state dirty");
            {
                juce::Slider::ScopedDragNotification noOpGesture(*amount);
                check(!p.isAutomaticMode(),"mouse-down or no-op gesture does not migrate legacy mode");
                set(p,"amount",47.5f);
                check(!p.isAutomaticMode()&&std::abs(amount->getValue()-47.5)<0.002,"host automation during an active drag stays legacy and updates the UI");
                amount->setValue(amount->getValue(),juce::sendNotificationSync);
            }
            check(!p.isAutomaticMode()&&listener.nonParameterChanges==0,"ending a no-op gesture does not migrate or dirty state");
            const int startsBeforeArrow=listener.gestureStarts,endsBeforeArrow=listener.gestureEnds;
            const float amountBeforeArrow=raw(p,"amount");
            const bool arrowAccepted=amount->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));++uiChanges;
            check(arrowAccepted&&raw(p,"amount")>amountBeforeArrow,"actual right-arrow edit changes the bound amount");
            check(p.isAutomaticMode(),"actual arrow key migrates a legacy instance");
            check(listener.nonParameterChanges==1,"migration notifies host of non-parameter state change");
            check(listener.gestureStarts==startsBeforeArrow+1&&listener.gestureEnds==endsBeforeArrow+1,"arrow key emits a balanced host automation gesture");
            auto migrated=savedTree(p);check(static_cast<bool>(migrated.getProperty("automatic")),"user migration is serialized as automatic true");
            p.beginUserAmountGesture();check(listener.nonParameterChanges==1,"repeated user gestures do not repeatedly dirty automatic state");
            enterLegacy(p);set(p,"amount",50);const auto beforeModified=raw(p,"amount");
            amount->keyPressed(juce::KeyPress(juce::KeyPress::rightKey,juce::ModifierKeys::ctrlModifier,0));
            check(!p.isAutomaticMode()&&raw(p,"amount")==beforeModified,"modified non-edit arrow key does not migrate");
            amount->keyPressed(juce::KeyPress('x'));check(!p.isAutomaticMode(),"unhandled key does not migrate");
            set(p,"amount",100);amount->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
            check(!p.isAutomaticMode()&&raw(p,"amount")==100,"arrow at the limit does not change or migrate amount");
            {
                juce::Slider::ScopedDragNotification changedGesture(*amount);
                set(p,"amount",36);check(!p.isAutomaticMode(),"host update during a second drag does not migrate");
                amount->setValue(37,juce::sendNotificationSync);++uiChanges;
                check(p.isAutomaticMode()&&std::abs(raw(p,"amount")-37)<0.002,"a subsequent real local edit in the same drag does migrate");
            }
            enterLegacy(p);
            set(p,"amount",40);juce::Label* valueLabel=nullptr;
            for(auto* child:amount->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))valueLabel=label;
            check(valueLabel!=nullptr,"editable percentage is part of the same slider");
            if(valueLabel){valueLabel->setText("63,5 %",juce::sendNotificationSync);++uiChanges;check(std::abs(raw(p,"amount")-63.5f)<0.002,"numeric value commit binds decimal comma percent");check(p.isAutomaticMode(),"numeric value commit migrates legacy mode");}
            enterLegacy(p);set(p,"amount",79);
            const auto time=juce::Time::getCurrentTime();const juce::Point<float> point{186,129};
            juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(),1,0,0,0,0,amount,amount,time,point,time,2,false);
            amount->mouseDoubleClick(doubleClick);++uiChanges;
            check(std::abs(raw(p,"amount")-55)<0.002&&p.isAutomaticMode(),"actual double click resets to 55 and migrates old projects");
            enterLegacy(p);amount->mouseDoubleClick(doubleClick);
            check(!p.isAutomaticMode()&&std::abs(raw(p,"amount")-55)<0.002,"double click already at the reset value does not migrate");
            amount->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));++uiChanges;
            check(p.isAutomaticMode()&&raw(p,"amount")<55,"a real edit after a no-op reset still migrates");
            const int notificationCount=listener.nonParameterChanges;
            for(int i=0;i<10000;++i) {
                juce::Slider::ScopedDragNotification gesture(*amount);
                amount->setValue(amount->proportionOfLengthToValue((i%1001)/1000.0),juce::sendNotificationSync);
                check(std::isfinite(amount->getValue()),"single knob stays finite across 10000 user edits");
                check(std::abs(raw(p,"amount")-amount->getValue())<0.002,"single knob updates actual APVTS amount");++uiChanges;
            }
            check(listener.gestureStarts==listener.gestureEnds,"all tested user gestures are balanced");
            check(listener.nonParameterChanges==notificationCount,"automatic knob edits do not emit redundant mode changes");
            set(p,"amount",0);const auto zeroHash=componentHash(*amount);
            set(p,"amount",100);check(componentHash(*amount)!=zeroHash,"live knob arc pointer and percentage respond to the parameter");
            set(p,"amount",0);check(componentHash(*amount)==zeroHash,"live knob rendering is deterministic at the same amount");
            p.removeListener(&listener);
        }
        p.resetAllParameters();
        renderPng(*editor,320,300,"GILLDEREVERB-UI-320.png");renderPng(*editor,480,450,"GILLDEREVERB-UI-480.png");renderPng(*editor,640,600,"GILLDEREVERB-UI-640.png");
        editor.reset();
    }
    p.releaseResources();
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::ofstream report("plugin-integration-report.json");
    report<<"{\n  \"passed\": "<<(failures==0?"true":"false")<<",\n  \"checks\": "<<checks<<",\n  \"failures\": "<<failures<<",\n  \"automated_configurations\": "<<automationCases<<",\n  \"ui_control_changes\": "<<uiChanges<<",\n  \"visible_continuous_controls\": 1,\n  \"latency_samples\": 2048,\n  \"elapsed_seconds\": "<<seconds<<",\n  \"scope\": \"CORE single-control bindings, automatic and legacy routing, user-only migration, state compatibility and robustness; not universal dereverberation quality\"\n}\n";
    std::cout<<"Integration checks: "<<checks<<", failures: "<<failures<<", UI changes: "<<uiChanges<<", seconds: "<<seconds<<"\n";
    return failures?1:0;
}
