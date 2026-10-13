#include "Processor.h"
#include <iostream>
#include <map>
#include <functional>
#include <fstream>
#include <stdexcept>
using namespace studio;
thread_local bool counting=false; thread_local size_t allocations=0;
void* operator new(size_t n){if(counting) ++allocations;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);} void operator delete(void*p,size_t)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}void operator delete[](void*p)noexcept{std::free(p);}
juce::AudioProcessorEditor* Processor::createEditor(){return nullptr;}
int checks=0; bool old=false;
void check(bool x,const std::string&m){++checks;if(!x)throw std::runtime_error(m);}
using Params=std::map<std::string,float>;
struct Config{Kind kind=Kind::Rack;int module=17,main=2,keys=2,q=0,block=127,pattern=1,mainPattern=1;double fs=48000,mainAmp=.1,keyHz=997;bool silent=false,bursts=false,recall=false,automate=false,bypassed=false;Params params;};
void configure(Processor&p,const Config&c){
 for(auto&s:p.definition.params)p.set(s.id,s.def);
 p.set("gillQuality",float(c.q));
 if(c.kind==Kind::Rack){for(int i=1;i<=8;++i)p.set("b"+juce::String(i)+"_module",0);p.set("b1_module",float(c.module));
  for(auto [k,v]:Params{{"external",1},{"threshold",-36},{"ratio",10},{"attack",.1f},{"release",25},{"range",36},{"gatehold",30},{"amount",0},{"attackamount",12},{"sustain",-6},{"gatethreshold",-80},{"gaterange",0}})p.set("b1_"+juce::String(k),v);
 }else if(c.kind==Kind::Alpha){p.set("external",1);p.set("sclow",20);p.set("schigh",20000);for(int i=1;i<=2;++i)for(auto [k,v]:Params{{"threshold",-36},{"ratio",10},{"attack",.1f},{"release",25},{"range",36},{"adaptive",0}})p.set("b"+juce::String(i)+"_"+juce::String(k),v);
 }else if(c.kind==Kind::Multi||c.kind==Kind::Precision||c.kind==Kind::Deess){int n=c.kind==Kind::Multi?6:c.kind==Kind::Precision?5:2;for(int i=1;i<=n;++i){auto b="b"+juce::String(i)+"_";p.set(b+"enable",i==1?1:0);p.set(b+"external",i==1?1:0);}for(auto[k,v]:Params{{"threshold",-36},{"ratio",10},{"attack",.1f},{"release",25},{"range",36},{"adapt",0},{"rms",0},{"low",20},{"high",20000},{"keylow",20},{"keyhigh",20000}})p.set("b1_"+juce::String(k),v);}
 for(auto[k,v]:c.params){check(p.state.getParameter(k)!=nullptr,"unknown parameter "+k);p.set(k,v);}
}
std::vector<float> render(Config c){
 auto p=std::make_unique<Processor>(c.kind);configure(*p,c);
 if(c.automate){p->set("b1_module",17);p->set("b1_external",1);p->set("b1_listen",1);if(c.recall)p->set("b1_module",float(c.module));}
 if(c.recall){juce::MemoryBlock b;p->getStateInformation(b);auto r=std::make_unique<Processor>(c.kind);r->setStateInformation(b.getData(),int(b.getSize()));for(auto*s:p->getParameters()){auto*rp=dynamic_cast<juce::AudioProcessorParameterWithID*>(s);if(rp)check(std::abs(p->value(rp->paramID)-r->value(rp->paramID))<.001,"recall "+rp->paramID.toStdString());}p=std::move(r);}
 auto layout=p->getBusesLayout();layout.inputBuses.set(0,c.main==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());layout.outputBuses.set(0,layout.inputBuses[0]);layout.inputBuses.set(1,c.keys==0?juce::AudioChannelSet::disabled():c.keys==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());check(p->setBusesLayout(layout),"layout rejected");p->prepareToPlay(c.fs,c.block);
 int total=int(c.fs*.25);std::vector<float> result;result.reserve(size_t(total*c.main));
 bool switched=false;for(int pos=0;pos<total;pos+=c.block){if(c.automate&&!c.recall&&!switched&&pos>=int(c.fs*.04)){p->set("b1_module",float(c.module));switched=true;}int n=std::min(c.block,total-pos);juce::AudioBuffer<float>b(c.main+c.keys,n);b.clear();std::vector<float>key;key.reserve(size_t(c.keys*n));for(int i=0;i<n;++i){double t=(pos+i)/c.fs;float x=c.silent?0:float(c.mainAmp*std::sin(2*pi*731*t));for(int a=0;a<c.main;++a)b.setSample(a,i,c.mainPattern==2?(a?-x:x):c.mainPattern==3?(a?x:0):c.mainPattern==4?(a?0:x):x);double burst=!c.bursts||std::fmod(t,.06)<.013?1:0;float k=float(.6*burst*std::sin(2*pi*c.keyHz*t));for(int a=0;a<c.keys;++a){float value=c.pattern==0?0:c.pattern==2?(a?-k:k):c.pattern==3?(a?k:0):c.pattern==4?(a?0:k):k;b.setSample(c.main+a,i,value);}}
 for(int a=0;a<c.keys;++a)for(int i=0;i<n;++i)key.push_back(b.getSample(c.main+a,i));juce::MidiBuffer midi;allocations=0;counting=true;if(c.bypassed)p->processBlockBypassed(b,midi);else p->processBlock(b,midi);counting=false;check(allocations==0,"audio allocation");int z=0;for(int a=0;a<c.keys;++a)for(int i=0;i<n;++i)check(b.getSample(c.main+a,i)==key[size_t(z++)],"key mutated");for(int i=0;i<n;++i)for(int a=0;a<c.main;++a){float v=b.getSample(a,i);check(std::isfinite(v),"nonfinite audio");result.push_back(v);}}
 return result;
}
double rms(const std::vector<float>&v,int channel=-1,int channels=2){double e=0;int n=0;for(size_t i=v.size()/2;i<v.size();++i)if(channel<0||int(i%channels)==channel){e+=double(v[i])*v[i];++n;}return std::sqrt(e/std::max(1,n));}
double diff(const std::vector<float>&a,const std::vector<float>&b){check(a.size()==b.size(),"length");double e=0;for(size_t i=0;i<a.size();++i)e=std::max(e,std::abs(double(a[i])-b[i]));return e;}
double tailDiff(const std::vector<float>&a,const std::vector<float>&b){check(a.size()==b.size(),"tail length");double e=0;for(size_t i=a.size()/2;i<a.size();++i)e=std::max(e,std::abs(double(a[i])-b[i]));return e;}
void metric(const std::string&s,double v){std::cout<<s<<" "<<v<<"\n";}
std::vector<Config> supported(){std::vector<Config>r;for(Kind k:{Kind::Multi,Kind::Precision,Kind::Deess,Kind::Alpha}){Config c;c.kind=k;r.push_back(c);}for(int m:{2,3,4,8,17,18,19,20,21,22,23,24,25,26,27,28,39}){Config c;c.module=m;c.bursts=m==8||m==28||m==39;r.push_back(c);}return r;}
std::string label(const Config&c){return std::string(names[int(c.kind)])+"/"+std::to_string(c.module);}
void reproductions(){
 for(int m:{9,31,29,40})for(bool recall:{false,true}){Config c;c.module=m;c.silent=true;c.automate=true;c.recall=recall;double e=rms(render(c));metric("hidden-listen/"+std::to_string(m)+"/"+std::to_string(recall),e);check(old?e>.01:e<1e-8,"hidden listen leak");}
 Config lift;lift.module=38;lift.params={{"b1_amount",100},{"b1_external",0}};lift.bursts=true;auto a=render(lift);lift.params["b1_external"]=1;lift.pattern=0;auto b=render(lift);double e=diff(a,b);metric("lift-hidden-external",e);check(old?e>1e-4:e<1e-7,"lift hidden external");lift.params["b1_external"]=0;lift.params["b1_feedback"]=1;lift.params["b1_keylow"]=9000;lift.params["b1_keyhigh"]=100;double f=diff(a,render(lift));metric("lift-hidden-filter-feedback",f);check(old?f>1e-4:f<1e-7,"lift hidden filter/feedback");
 Config gate;gate.module=4;gate.pattern=4;gate.params={{"b1_link",100}};a=render(gate);gate.params["b1_link"]=0;e=diff(a,render(gate));metric("gate-hidden-link",e);check(old?e>.01:e<1e-7,"gate hidden link");gate.pattern=1;gate.params={{"b1_external",0},{"b1_feedback",0},{"b1_threshold",-25}};a=render(gate);gate.params["b1_feedback"]=1;e=diff(a,render(gate));metric("gate-hidden-feedback",e);check(old?e>1e-4:e<1e-7,"gate hidden feedback");
 for(auto c:supported()){c.main=1;auto in=render(c);c.pattern=2;auto anti=render(c);c.pattern=0;auto zero=render(c);double action=diff(in,zero);metric("mono-action/"+label(c),action);check(action>1e-4,"no positive key action "+label(c));check(old?diff(anti,zero)<1e-7:diff(anti,in)<2e-6,"mono antiphase "+label(c));c.pattern=3;double rightError=diff(render(c),in);metric("mono-right-difference/"+label(c),rightError);if(!old)check(rightError<2e-6,"mono right key "+label(c));else if(c.kind!=Kind::Rack)check(rightError>1e-5,"baseline one-sided attenuation "+label(c));}
 std::cout<<(old?"BASELINE_DEFECTS_REPRODUCED":"REPAIRS_REPRODUCED")<<"\n";
}
void matrix(){
 for(auto base:supported())for(double fs:{44100.,48000.,96000.})for(int q:{0,1})for(int main:{1,2}){auto c=base;c.fs=fs;c.q=q;c.main=main;c.block=main==1?31:257;auto loud=render(c);if(main==1){c.pattern=2;check(diff(loud,render(c))<2e-6,"rate/mode antiphase "+label(c));c.pattern=3;check(diff(loud,render(c))<2e-6,"rate/mode right-only "+label(c));}c.pattern=0;auto silent=render(c);check(diff(loud,silent)>1e-4,"external inactive "+label(c));c.keys=0;check(diff(silent,render(c))<1e-7,"disabled/silent "+label(c));c.keys=1;c.pattern=1;auto mono=render(c);c.keys=2;check(diff(mono,render(c))<2e-6,"mono/dual key "+label(c));c.silent=true;check(rms(render(c))<1e-8,"normal key leakage "+label(c));c.silent=false;c.params[c.kind==Kind::Alpha?"external":"b1_external"]=0;c.pattern=1;auto internal=render(c);c.pattern=0;check(diff(internal,render(c))<1e-7,"internal depends on key "+label(c));}
 std::cout<<"SUPPORTED_MATRIX_PASS\n";
 for(auto base:supported())for(int q:{0,1}){auto c=base;c.q=q;c.block=1;auto a=render(c);for(int block:{17,32,127,257}){c.block=block;check(diff(a,render(c))<3e-6,"partition "+label(c));}c.recall=true;check(diff(a,render(c))<3e-6,"state audio "+label(c));c.recall=false;c.bypassed=true;auto b=render(c);c.pattern=0;check(tailDiff(b,render(c))<1e-7,"settled bypass key dependence");check(rms(b)>.06,"bypass missing dry");}
 std::cout<<"STATE_BLOCK_BYPASS_PASS\n";
 for(int k:{0,1,3,4,5,7,9}){Config c;c.kind=Kind(k);auto a=render(c);c.pattern=0;check(diff(a,render(c))<1e-7,"unsupported key consumed");}
 for(int m=1;m<rackModelCount;++m){Config c;c.module=m;c.silent=true;c.automate=true;const bool listen=rackControlUsed(m,"listen")&&rackControlUsed(m,"external");double r=rms(render(c));check(listen?r>.01:r<1e-8,"model listen capability "+std::to_string(m));}
 std::cout<<"ALL_MODELS_AND_IGNORED_BUSES_PASS\n";
}
void routing(){
 for(Kind k:{Kind::Multi,Kind::Precision,Kind::Deess,Kind::Alpha}){Config c;c.kind=k;c.pattern=4;c.params[k==Kind::Alpha?"link":"b1_link"]=0;auto un=render(c);metric("unlinked-left/"+label(c),rms(un,0));metric("unlinked-right/"+label(c),rms(un,1));check(rms(un,1)>rms(un,0)*2,"unlinked separation");c.params[k==Kind::Alpha?"link":"b1_link"]=100;auto linked=render(c);check(std::abs(rms(linked,0)-rms(linked,1))<1e-6,"linked separation");c.params["routing"]=1;c.params[k==Kind::Alpha?"link":"b1_link"]=0;c.pattern=2;auto side=render(c);c.pattern=1;auto mid=render(c);check(rms(side)>rms(mid)*2,"MS key selection");}
 for(Kind k:{Kind::Multi,Kind::Precision,Kind::Deess}){Config c;c.kind=k;c.params={{"b1_listen",1}};c.silent=true;check(rms(render(c))>.01,"band listen positive");c.main=1;c.pattern=2;check(rms(render(c))<1e-8,"mono listen rectification");c.params["b1_listen"]=0;c.silent=false;c.main=2;c.pattern=1;c.keyHz=6000;c.params["b1_keylow"]=4000;c.params["b1_keyhigh"]=10000;auto hi=render(c);c.keyHz=100;auto lo=render(c);check(rms(lo)>rms(hi)*1.12,"key filter independent");}
 Config c;c.module=17;c.silent=true;c.main=1;c.pattern=2;c.params={{"b1_listen",1}};check(rms(render(c))<1e-8,"rack listen rectification");for(int m:{5,41}){c.module=m;c.main=2;c.silent=false;c.params={{"b1_listen",1}};auto a=render(c);c.pattern=0;check(diff(a,render(c))<1e-7,"internal deess key leakage");check(rms(a)>1e-4,"internal deess audition absent");}
 for(Kind kind:{Kind::Multi,Kind::Precision,Kind::Deess})for(int select:{1,2,3,4}){Config band;band.kind=kind;band.params={{"b1_channel",float(select)},{"b1_link",0}};if(select==4){band.mainPattern=2;band.pattern=2;}auto acted=render(band);band.pattern=0;auto dry=render(band);if(select<=2){int chosen=select-1,other=1-chosen;check(rms(dry,chosen)>rms(acted,chosen)*2,"selected LR channel");check(std::abs(rms(dry,other)-rms(acted,other))<1e-7,"unselected LR channel");}else check(rms(dry)>rms(acted)*2,"selected MS channel");}
 std::cout<<"ROUTING_FILTER_LISTEN_PASS\n";
}
void slotAndBandCoverage(){
 for(Kind kind:{Kind::Multi,Kind::Precision,Kind::Deess}){int count=kind==Kind::Multi?6:kind==Kind::Precision?5:2;for(int band=1;band<=count;++band){Config c;c.kind=kind;c.params["b1_enable"]=0;c.params["b1_external"]=0;auto prefix="b"+std::to_string(band)+"_";for(auto[k,v]:Params{{"enable",1},{"external",1},{"threshold",-36},{"ratio",10},{"range",36},{"attack",.1f},{"low",20},{"high",20000},{"keylow",20},{"keyhigh",20000}})c.params[prefix+k]=v;auto a=render(c);c.pattern=0;check(diff(a,render(c))>1e-3,"independent band key "+prefix);}}
 for(int slot=1;slot<=8;++slot){Config c;c.module=0;auto prefix="b"+std::to_string(slot)+"_";for(auto[k,v]:Params{{"module",17},{"external",1},{"threshold",-36},{"ratio",10},{"range",36},{"attack",.1f},{"amount",0}})c.params[prefix+k]=v;auto a=render(c);c.pattern=0;check(diff(a,render(c))>1e-3,"independent slot key "+prefix);}
 Config c;c.silent=true;c.params={{"b1_listen",1}};auto listened=render(c);for(const auto&key:{"b1_enable","b1_wet","b1_module"}){auto disabled=c;disabled.params[key]=0;check(rms(render(disabled))<1e-8,"listen disabled/wet/empty");}
 c.params["b2_module"]=40;c.params["b2_output"]=-6;auto after=render(c);check(std::abs(rms(after)/rms(listened)-std::pow(10.,-6./20.))<1e-5,"downstream trim after listen");c.params["b1_order"]=7;c.params["b2_order"]=0;check(std::abs(rms(render(c))-rms(listened))<1e-6,"slot order key audition");c.params["b2_solo"]=1;check(rms(render(c))<1e-8,"solo isolation");
 Config alpha;alpha.kind=Kind::Alpha;alpha.main=1;alpha.params={{"b1_autoattack",1},{"b2_autoattack",1},{"link",0}};auto a=render(alpha);alpha.pattern=3;check(diff(a,render(alpha))<2e-6,"mono autoattack lane policy");std::cout<<"SLOTS_BANDS_AUTOATTACK_PASS\n";
}
void reference(const char*path){std::ofstream stream(path,std::ios::binary);for(int k=0;k<12;++k)for(int q:{0,1})for(int main:{1,2}){Config c;c.kind=Kind(k);c.q=q;c.main=main;c.keys=0;if(k==2||k==8||k==10||k==11)c.params["b1_external"]=0;else if(k==6)c.params["external"]=0;auto a=render(c);stream.write(reinterpret_cast<const char*>(a.data()),a.size()*sizeof(float));}for(int m=1;m<rackModelCount;++m){Config c;c.module=m;c.params={{"b1_external",0}};auto a=render(c);stream.write(reinterpret_cast<const char*>(a.data()),a.size()*sizeof(float));}check(bool(stream),"reference write");std::cout<<"INTERNAL_REFERENCE_PASS\n";}
int main(int argc,char**argv){juce::ScopedJuceInitialiser_GUI gui;std::cout<<std::unitbuf;old=argc>1&&std::string(argv[1])=="--baseline";try{counting=true;allocations=0;auto*p=::operator new(100);counting=false;check(allocations==1,"allocation oracle");::operator delete(p);if(argc==2&&std::string(argv[1])=="--focused"){routing();slotAndBandCoverage();std::cout<<"FOCUSED_PASS\n";return 0;}if(argc==3&&std::string(argv[1])=="--reference"){reference(argv[2]);return 0;}reproductions();if(!old){matrix();routing();slotAndBandCoverage();}std::cout<<"checks "<<checks<<"\n"<<(old?"SIDE22_BASELINE_NATIVE_PASS":"SIDE22_SUITE_NATIVE_PASS")<<"\n";}catch(std::exception&e){counting=false;std::cerr<<e.what()<<"\n";return 1;}}

