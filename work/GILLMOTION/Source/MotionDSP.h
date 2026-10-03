#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>
#include "../../GILLNEXT/ThirdParty/signalsmith-stretch/signalsmith-stretch.h"

namespace gill::motion {
constexpr double pi=3.14159265358979323846;
enum class Kind { Brake, Wire, Ghost, Trail, Metal, Stutter, Crowd };
inline const char* name(Kind k){constexpr const char*n[]{"GILLBRAKE","GILLWIRE","GILLGHOST","GILLTRAIL","GILLMETAL","GILLSTUTTER","GILLCROWD"};return n[int(k)];}
inline bool designer(Kind k){return k==Kind::Brake||k==Kind::Stutter||k==Kind::Crowd;}
inline float clean(float x){return std::isfinite(x)?std::clamp(x,-16.f,16.f):0.f;}
inline double division(int i){constexpr double d[]{1./6,.25,1./3,.5,.75,1.,2.};return d[std::clamp(i,0,6)];}
struct Settings {
 float amount=.65f,tone=.6f,width=.8f,mix=.7f,dry=1,level=-6,beats=4,start=0,end=1,time=.5f;
 int style=0,rhythm=3,syllables=2,voices=12;double bpm=120;bool pro=true,bypass=false;
 bool operator==(const Settings&o)const{return amount==o.amount&&tone==o.tone&&width==o.width&&mix==o.mix&&dry==o.dry&&level==o.level&&beats==o.beats&&start==o.start&&end==o.end&&time==o.time&&style==o.style&&rhythm==o.rhythm&&syllables==o.syllables&&voices==o.voices&&bpm==o.bpm&&pro==o.pro&&bypass==o.bypass;}
};
struct Clip {double rate=48000,bpm=120;std::int64_t onset=0;bool anchored=false;std::vector<float>left,right;};
struct Render:Clip {bool endsAtOnset=false;float peak=0;Settings settings;};
inline float sample(const std::vector<float>&a,double at,bool high=true){
 if(at<0||at>=double(a.size())-1)return 0;const int n=int(at);const float t=float(at-n),b=a[n],c=a[n+1];
 if(!high)return b+(c-b)*t;const float p=a[std::max(0,n-1)],d=a[std::min(int(a.size())-1,n+2)];
 return b+.5f*t*(c-p+t*(2*p-5*b+4*c-d+t*(3*(b-c)+d-p)));
}
inline float fade(int i,int count,int edge){return std::max(0.f,std::min({1.f,float(i)/edge,float(count-1-i)/edge}));}

// Fixed audio-owned storage; the worker only copies after taking exclusive ownership.
class Capture {
public:
 enum State{Idle,Armed,Recording,Ready,Reading,Complete};
 void prepare(double rate){fs=rate;for(auto&v:data)v.assign(size_t(std::ceil(fs*12.1)),0);for(auto&v:pre)v.assign(size_t(std::ceil(fs*.05)),0);state=Idle;command=0;clear();}
 void arm(){command=1;}void finish(){command=2;}int status()const{return state.load();}
 void process(const float*l,const float*r,int n,bool playing,bool host,std::int64_t pos,bool known,double tempo,int syllables,bool phrase,bool enabled){
  int s=state.load(std::memory_order_acquire);if(s==Reading)return;const int cmd=command.exchange(0);
  if(cmd==1){clear();state=Armed;s=Armed;}else if(cmd==2){if(s==Recording)done();else if(s==Armed)state=Idle;return;}
  if(!enabled||s!=Armed&&s!=Recording)return;
  if(host&&!playing){if(s==Recording)done();return;}
  if(known&&hadPosition&&pos!=expected){if(s==Recording){done();return;}clear();}
  hadPosition=known;expected=pos+n;
  const double fall=std::exp(-1/(fs*.004));
  for(int i=0;i<n;++i){const float a=clean(l[i]),b=r?clean(r[i]):a;const double peak=std::max(std::abs(a),std::abs(b));env=std::max(peak,env*fall);const bool voiced=env>std::clamp(std::max(.0063,noise*3.5),.0063,.12);
   if(s==Armed){if(!voiced)noise+=.00003*(peak-noise);candidate=voiced?candidate+1:0;pre[0][prePos]=a;pre[1][prePos]=b;prePos=(prePos+1)%int(pre[0].size());
    if(candidate>=int(fs*.025)){used=std::min(candidate,int(pre[0].size()));for(int j=0;j<used;++j){const int at=(prePos-used+j+int(pre[0].size()))%int(pre[0].size());data[0][j]=pre[0][at];data[1][j]=pre[1][at];}origin=pos+i-used+1;anchor=known;bpm=std::clamp(tempo,20.,400.);count=1;state=Recording;s=Recording;lastVoice=used;}
   }else{data[0][used]=a;data[1][used]=b;++used;if(voiced){if(quiet>=int(fs*.045))++count;quiet=0;lastVoice=used;}else ++quiet;
    const int gap=int(fs*(phrase?.55:.15));if(used>=int(fs*12)||(quiet>=gap&&(phrase||count>=std::clamp(syllables,1,3)))||(!phrase&&used>=int(fs*2.5))){done();return;}}
  }
 }
 bool take(Clip&c){if(command.load()==1)return false;int expectedState=Ready;if(!state.compare_exchange_strong(expectedState,Reading))return false;c.rate=fs;c.bpm=bpm;c.onset=origin;c.anchored=anchor;const int end=std::min(used,lastVoice+int(fs*.025));c.left.assign(data[0].begin(),data[0].begin()+end);c.right.assign(data[1].begin(),data[1].begin()+end);state.store(Complete,std::memory_order_release);return true;}
private:
 void clear(){used=lastVoice=quiet=candidate=prePos=count=0;env=0;noise=.0003;hadPosition=anchor=false;}
 void done(){if(lastVoice>=int(fs*.06))state.store(Ready,std::memory_order_release);else{clear();state=Armed;}}
 double fs=48000,env=0,noise=.0003,bpm=120;int used=0,lastVoice=0,quiet=0,candidate=0,prePos=0,count=0;std::int64_t origin=0,expected=0;bool hadPosition=false,anchor=false;
 std::array<std::vector<float>,2>data,pre;std::atomic<int>state{Idle},command{0};
};

// Real-time effects use bounded delay storage and no allocations in process().
class Engine {
public:
 explicit Engine(Kind k):kind(k){}
 void prepare(double rate){fs=rate;for(auto&v:ring)v.assign(size_t(kind==Kind::Trail?std::ceil(fs*6.2):4),0);reset();}
 // Transport seeks also clear effect memory. Preserve the current bypass ramp
 // so an already bypassed effect cannot reappear when the playhead loops.
 void reset(){for(auto&v:ring)std::fill(v.begin(),v.end(),0);lp={};hp={};envelope={};phase=0;position=0;delaySmooth=0;smoothed=Settings{};seed=0x947ab31u;}
 void process(float*l,float*r,int n,const Settings&s){
  if(designer(kind))return;const float smooth=float(1-std::exp(-1/(fs*.015))),tk=float(1-std::exp(-2*pi*(1100+s.tone*9000)/fs));
  const double requested=fs*60/std::clamp(s.bpm,20.,400.)*division(s.rhythm);if(delaySmooth==0)delaySmooth=requested;
  for(int i=0;i<n;++i){smoothed.amount+=smooth*(s.amount-smoothed.amount);smoothed.mix+=smooth*(s.mix-smoothed.mix);smoothed.dry+=smooth*(s.dry-smoothed.dry);smoothed.width+=smooth*(s.width-smoothed.width);smoothed.level+=smooth*(s.level-smoothed.level);
   // A float accumulator stalls before reaching even 1e-5 at high rates.
   // Keep the 15-ms ramp, then reach an exact, fully dry bypass endpoint.
   const double bypassTarget=s.bypass?1.:0.;bypass+=smooth*(bypassTarget-bypass);
   if(std::abs(bypassTarget-bypass)<1.e-5)bypass=bypassTarget;
   const float a=clean(l[i]),b=r?clean(r[i]):a;std::array<float,2>in{a,b},wet{};
   if(kind==Kind::Wire){const double low=s.style==1?500:s.style==2?650:280,high=s.style==1?2400:s.style==2?1800:3400;
    const float hc=float(1-std::exp(-2*pi*low/fs)),lc=float(1-std::exp(-2*pi*(high*(.65+.7*s.tone))/fs)),drive=1+smoothed.amount*15;
    for(int c=0;c<2;++c){hp[c]+=hc*(in[c]-hp[c]);lp[c]+=lc*(in[c]-hp[c]-lp[c]);wet[c]=std::tanh(lp[c]*drive)/std::sqrt(drive);if(s.style==2){const float steps=std::pow(2.f,10-6*smoothed.amount);wet[c]=std::round(wet[c]*steps)/steps;}}
   }else if(kind==Kind::Ghost){for(int c=0;c<2;++c){const float hc=float(1-std::exp(-2*pi*(500+s.tone*1600)/fs));hp[c]+=hc*(in[c]-hp[c]);const float high=in[c]-hp[c];envelope[c]+=float(1-std::exp(-1/(fs*(std::abs(high)>envelope[c]?.001:.035))))*(std::abs(high)-envelope[c]);seed=seed*1664525u+1013904223u;const float noise=float(int(seed>>8)-8388608)/8388608.f;const float breath=noise*envelope[c]*2.1f;const float x=high*(1-smoothed.amount)+breath*smoothed.amount;const float ghostTone=float(1-std::exp(-2*pi*std::min(fs*.44,(1100+s.tone*9000)*(s.style==1?.5:s.style==2?1.3:1.))/fs));lp[c]+=ghostTone*(x-lp[c]);wet[c]=lp[c];}}
   else if(kind==Kind::Metal){const double hz=(s.style==0?70:s.style==1?230:620)*(.3+2*s.time);phase+=hz/fs;phase-=std::floor(phase);for(int c=0;c<2;++c){const double angle=2*pi*phase+c*smoothed.width*pi*.35;const float carrier=float(std::sin(angle));wet[c]=in[c]*((1-smoothed.amount)+smoothed.amount*carrier);lp[c]+=tk*(wet[c]-lp[c]);wet[c]=lp[c];}}
   else if(kind==Kind::Trail){delaySmooth+=std::min(.0003f,smooth)*(requested-delaySmooth);const double semitones=(s.style==0?-3.:s.style==1?4.:7.)*smoothed.amount,ratio=std::pow(2.,semitones/12.);const double window=fs*(s.pro?.045:.025);phase+=(1-ratio)/window;phase-=std::floor(phase);
    for(int c=0;c<2;++c){auto read=[&](double delay){double at=position-delay;while(at<0)at+=ring[c].size();int first=int(at)%int(ring[c].size());int next=(first+1)%int(ring[c].size());return ring[c][first]+float(at-std::floor(at))*(ring[c][next]-ring[c][first]);};const double p2=std::fmod(phase+.5,1.);const float weight=float(.5-.5*std::cos(2*pi*phase));wet[c]=std::abs(semitones)<.001?read(delaySmooth):read(delaySmooth+(phase-.5)*window)*weight+read(delaySmooth+(p2-.5)*window)*(1-weight);lp[c]+=tk*(wet[c]-lp[c]);}
    const float feedback=std::clamp(s.time,0.f,.75f);ring[0][position]=clean(a*.6f+lp[1]*feedback);ring[1][position]=clean(b*.6f+lp[0]*feedback);if(++position>=int(ring[0].size()))position=0;
   }
   const float mid=(wet[0]+wet[1])*.5f,side=(wet[0]-wet[1])*.5f*smoothed.width;wet={mid+side,mid-side};const float gain=std::pow(10.f,smoothed.level/20.f);
   float out[2];for(int c=0;c<2;++c)out[c]=clean((in[c]*smoothed.dry+wet[c]*smoothed.mix)*gain*(1-bypass)+in[c]*bypass);
   l[i]=r?out[0]:(out[0]+out[1])*.5f;if(r)r[i]=out[1];
  }
 }
private:
 Kind kind;double fs=48000,phase=0,delaySmooth=0,bypass=0;int position=0;unsigned seed=0;Settings smoothed;
 std::array<std::vector<float>,2>ring;std::array<float,2>lp{},hp{},envelope{};
};

inline bool pitchVoice(const std::vector<float>&in,double fs,double semitones,double formant,std::vector<float>&out,const std::function<bool()>&cancel){
 signalsmith::stretch::SignalsmithStretch<float>stretch;stretch.presetDefault(1,fs);stretch.setTransposeSemitones(semitones);stretch.setFormantFactor(formant,true);stretch.setFormantBase(160/fs);
 const int latency=stretch.inputLatency()+stretch.outputLatency(),total=int(in.size())+latency;out.assign(in.size(),0);std::array<float,512>input{},output{};
 for(int at=0;at<total;at+=512){if(cancel&&cancel())return false;const int n=std::min(512,total-at);for(int j=0;j<n;++j)input[j]=at+j<int(in.size())?in[at+j]:0;const float*src[]{input.data()};float*dst[]{output.data()};stretch.process(src,n,dst,n);for(int j=0;j<n;++j){const int index=at+j-latency;if(index>=0&&index<int(out.size()))out[index]=clean(output[j]);}}
 return true;
}
inline bool render(Kind kind,const Clip&clip,const Settings&s,Render&out,const std::function<bool()>&cancel={}){
 if(clip.rate<8000||clip.rate>192000||!std::isfinite(clip.rate)||clip.left.size()!=clip.right.size()||clip.left.size()<2||clip.left.size()>clip.rate*12.1)return false;
 const double fs=clip.rate,bpm=std::clamp(s.bpm,20.,400.);const int begin=std::clamp(int(s.start*clip.left.size()),0,int(clip.left.size())-2),end=std::clamp(int(s.end*clip.left.size()),begin+2,int(clip.left.size())),size=end-begin;
 Clip trimmed;trimmed.rate=fs;trimmed.left.assign(clip.left.begin()+begin,clip.left.begin()+end);trimmed.right.assign(clip.right.begin()+begin,clip.right.begin()+end);
 out=Render{};out.rate=fs;out.bpm=bpm;out.onset=clip.onset+begin;out.anchored=clip.anchored;out.settings=s;out.endsAtOnset=kind==Kind::Stutter;
 int length=size;if(kind==Kind::Stutter||kind==Kind::Brake)length=int(std::round(fs*60/bpm*std::clamp(s.beats,.25f,16.f)));else if(kind==Kind::Crowd)length+=int(fs*.7);else if(kind==Kind::Trail)length+=int(fs*std::min(90.,60/bpm*division(s.rhythm)*(2+std::ceil(std::log(.00001)/std::log(std::clamp(double(s.time),.01,.75))))));else length+=int(fs*.1);
 length=std::max(2,length);out.left.assign(length,0);out.right.assign(length,0);const int edge=std::max(1,int(fs*.004));
 if(kind==Kind::Brake){double pos=0;for(int i=0;i<length;++i){const double t=double(i)/(length-1),speed=s.style==1?.08+1.42*std::pow(t,.4+s.amount*3):std::pow(1-t,.35+s.amount*3);out.left[i]=sample(trimmed.left,pos,s.pro);out.right[i]=sample(trimmed.right,pos,s.pro);pos+=speed;}}
 else if(kind==Kind::Stutter){
  std::vector<std::pair<int,int>>syllables;float peak=0;for(float v:trimmed.left)peak=std::max(peak,std::abs(v));double env=0;int beginSyllable=-1,quiet=0,lastVoiced=0;const double release=std::exp(-1/(fs*.004));
  for(int i=0;i<size;++i){env=std::max(double(std::abs(trimmed.left[i])),env*release);const bool voiced=env>std::max(.001,double(peak)*.07);if(voiced){if(beginSyllable<0)beginSyllable=std::max(0,i-int(fs*.004));lastVoiced=i;quiet=0;}else if(beginSyllable>=0&&++quiet>=int(fs*.04)){if(lastVoiced-beginSyllable>=int(fs*.025))syllables.emplace_back(beginSyllable,std::min(size,lastVoiced+int(fs*.008)));beginSyllable=-1;}}
  if(beginSyllable>=0)syllables.emplace_back(beginSyllable,std::min(size,lastVoiced+int(fs*.008)));if(syllables.empty())syllables.emplace_back(0,size);if(syllables.size()>size_t(s.syllables))syllables.resize(std::clamp(s.syllables,1,3));
  double at=0;int hit=0;while(at<length){if(cancel&&cancel())return false;const double progress=at/length;double step=division(s.rhythm);if(s.style==1)step*=progress<.5?2:progress<.75?1:.5;else if(s.style==2)step*=progress<.75?1:.5;const int first=int(std::llround(at)),last=std::min(length,int(std::llround(at+fs*60/bpm*step))),count=last-first;const double pan=(hit%2?-1.:1.)*s.width,gain=std::pow(.025+.975*progress,s.amount*2.2);const float gl=float(std::cos((pan+1)*pi/4)*gain),gr=float(std::sin((pan+1)*pi/4)*gain);const auto chunk=syllables[size_t(hit)%syllables.size()];const int chunkSize=std::min(count,chunk.second-chunk.first);float chunkPeak=0;for(int j=chunk.first;j<chunk.second;++j)chunkPeak=std::max(chunkPeak,std::abs(.5f*(trimmed.left[j]+trimmed.right[j])));const float balance=std::min(4.f,peak/std::max(.0001f,chunkPeak));for(int j=0;j<count;++j){const int index=chunk.first+std::min(j,chunkSize-1);const float x=j<chunkSize?.5f*(trimmed.left[index]+trimmed.right[index])*fade(j,chunkSize,std::min(edge,std::max(1,chunkSize/4))):0;out.left[first+j]=x*gl*balance;out.right[first+j]=x*gr*balance;}at+=fs*60/bpm*step;++hit;}}
 else if(kind==Kind::Crowd){std::vector<float>mono(size),voice;for(int i=0;i<size;++i)mono[i]=(trimmed.left[i]+trimmed.right[i])*.5f;const int people=std::clamp(s.voices,4,24);unsigned random=0x637217u;auto unit=[&]{random=random*1664525u+1013904223u;return double(random>>8)/16777216.;};
  for(int v=0;v<people;++v){const int role=s.style==1?(v%3==0?2:1):s.style==2?(v%3==0?2:0):v%3;const double jitter=(unit()-.5)*.8,semi=(role==0?3.5:role==1?-5.5:-.8)+jitter,formant=(role==0?1.13:role==1?.81:.96)+(unit()-.5)*.055;if(!pitchVoice(mono,fs,semi,formant,voice,cancel))return false;const int offset=int(fs*(.006+unit()*(.01+.105*s.time)));const double speed=1+(unit()-.5)*.025*s.amount,pan=(2*(v+.5)/people-1)*s.width,level=(.65+unit()*.35)/std::sqrt(double(people))*s.mix;double lo=0;const double coefficient=1-std::exp(-2*pi*(role==1?3800:role==0?10000:6500)/fs);for(int i=offset;i<length;++i){const double pos=(i-offset)*speed;if(pos>=size-1)break;const double x=sample(voice,pos)*(.85+.15*std::sin(2*pi*(.8+v*.13)*i/fs+v));lo+=coefficient*(x-lo);out.left[i]+=float(lo*level*std::cos((pan+1)*pi/4));out.right[i]+=float(lo*level*std::sin((pan+1)*pi/4));}}
  // A short diffuse room belongs to the synthetic group, never the direct voice.
  std::array<std::vector<float>,4>room;std::array<int,4>head{};constexpr double times[]{.027,.041,.053,.071};for(int j=0;j<4;++j)room[j].assign(size_t(fs*times[j]),0);
  const float roomMix=.08f+.12f*(people-4)/20.f;for(int i=0;i<length;++i){if((i&4095)==0&&cancel&&cancel())return false;std::array<float,4>d{};float sum=0;for(int j=0;j<4;++j){d[j]=room[j][head[j]];sum+=d[j];}for(int j=0;j<4;++j){room[j][head[j]]=.25f*(j%2?out.right[i]:out.left[i])+(d[j]-.5f*sum)*.47f;if(++head[j]>=int(room[j].size()))head[j]=0;}const float roomL=(d[0]+d[2])*roomMix,roomR=(d[1]+d[3])*roomMix,roomMid=(roomL+roomR)*.5f,roomSide=(roomL-roomR)*.5f*s.width;out.left[i]+=roomMid+roomSide;out.right[i]+=roomMid-roomSide;}
  for(int i=0;i<size;++i){out.left[i]+=trimmed.left[i]*s.dry;out.right[i]+=trimmed.right[i]*s.dry;}
 }else{Engine engine(kind);engine.prepare(fs);std::copy(trimmed.left.begin(),trimmed.left.end(),out.left.begin());std::copy(trimmed.right.begin(),trimmed.right.end(),out.right.begin());Settings c=s;c.level=0;c.bypass=false;for(int offset=0;offset<length;offset+=512){if(cancel&&cancel())return false;engine.process(out.left.data()+offset,out.right.data()+offset,std::min(512,length-offset),c);}}
 const float tk=float(1-std::exp(-2*pi*(600+s.tone*15000)/fs));std::array<float,2>lp{};float peak=0;
 for(int i=0;i<length;++i){if((i&4095)==0&&cancel&&cancel())return false;const float f=fade(i,length,edge);for(int c=0;c<2;++c){auto&x=c==0?out.left[i]:out.right[i];if(designer(kind)){lp[c]+=tk*(x-lp[c]);x=lp[c];}x=clean(x)*f;peak=std::max(peak,std::abs(x));}}
 if(peak<1e-8)return false;const float gain=std::min(16.f,std::pow(10.f,(s.level-1)/20)/peak);for(int i=0;i<length;++i){out.left[i]*=gain;out.right[i]*=gain;}out.peak=peak*gain;return !(cancel&&cancel());
}
}
