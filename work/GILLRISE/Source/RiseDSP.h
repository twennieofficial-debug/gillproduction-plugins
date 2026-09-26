#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace gill::rise {
constexpr double pi=3.14159265358979323846;
inline float clean(float x){return std::isfinite(x)?std::clamp(x,-16.f,16.f):0.f;}
struct Settings {
 double length=2,decay=2.5,tone=7500,level=-6,rate=6,depth=.7,start=0,end=1;
 bool tremolo=false;
 bool operator==(const Settings&o)const{return length==o.length&&decay==o.decay&&tone==o.tone&&level==o.level&&rate==o.rate&&depth==o.depth&&start==o.start&&end==o.end&&tremolo==o.tremolo;}
};
struct Capture {
 double sampleRate=48000;std::vector<float>left,right;std::int64_t onsetSample=0;
 bool hostPositionKnown=false;int defaultStart=0,defaultEnd=0;
};
struct Render {
 double sampleRate=48000;std::vector<float>left,right;std::int64_t endSample=0;
 bool hostPositionKnown=false;double lengthSeconds=0;float peak=0;Settings settings;
};

// One audio writer and one worker reader. Reading state transfers exclusive
// ownership of the fixed capture storage; the audio callback never allocates.
class CaptureEngine {
public:
 enum State{Idle,Armed,Recording,Ready,Reading,Complete};
 void prepare(double rate){fs=rate;capacity=int(std::ceil(fs*2.5));preSize=std::max(1,int(fs*.10));for(auto&v:data)v.assign(capacity,0);for(auto&v:pre)v.assign(preSize,0);state=Idle;armRequest=true;stopRequest=false;clear();}
 void arm(){armRequest=true;stopRequest=false;}
 void stop(){stopRequest=true;}
 int status()const{return state.load(std::memory_order_acquire);}
 double seconds()const{return elapsed.load(std::memory_order_relaxed);}
 void process(const float*l,const float*r,int n,bool playing,bool hasTransport,std::int64_t position,bool positionKnown,bool enabled=true){
   auto s=state.load(std::memory_order_acquire);
   if(s!=Reading&&armRequest.exchange(false)){clear();state=Armed;s=Armed;}
   if(!enabled)return;
   if(stopRequest.exchange(false)){if(s==Recording)finish();else if(s!=Reading)state=Idle;return;}
   if(s!=Armed&&s!=Recording)return;
   if(hasTransport&&!playing){if(s==Recording)finish();return;}
   if(positionKnown&&hadPosition&&position!=expectedPosition){if(s==Recording){finish();return;}clear();}
   hadPosition=positionKnown;expectedPosition=position+n;
   const double release=std::exp(-1/(fs*.003));
   for(int i=0;i<n;++i){
     float a=clean(l[i]),b=r?clean(r[i]):a;
     const double amplitude=std::max(std::abs(double(a)),std::abs(double(b)));
     envelope=std::max(amplitude,envelope*release);
     // Fixed -42 dB floor prevents quiet room noise triggering. A slow noise
     // estimate raises the threshold for steady background sound.
     const double threshold=std::clamp(std::max(.007943,noise*3.5),.007943,.12);
     const bool above=envelope>threshold;
     if(s==Armed){
       if(!above)noise+=.00004*(amplitude-noise);
       if(above){if(candidate==0){candidatePosition=position+i;candidateKnown=positionKnown;}++candidate;}else candidate=0;
       pre[0][prePos]=a;pre[1][prePos]=b;prePos=(prePos+1)%preSize;preCount=std::min(preSize,preCount+1);
       if(candidate>=int(fs*.025)){
         used=preCount;for(int k=0;k<preCount;++k){int at=(prePos-preCount+k+preSize)%preSize;data[0][k]=pre[0][at];data[1][k]=pre[1][at];}
         start=std::max(0,used-candidate);onset=candidatePosition;known=candidateKnown;lastVoiced=used;
         state=Recording;s=Recording;quiet=0;
       }
     }else{
       if(used<capacity){data[0][used]=a;data[1][used]=b;++used;}
       if(above){quiet=0;lastVoiced=used;}else ++quiet;
       if(used>=capacity||used-start>=int(fs*1.5)||(quiet>=int(fs*.12)&&used-start>=int(fs*.12))){finish();return;}
     }
     elapsed.store(s==Recording?double(used)/fs:0,std::memory_order_relaxed);
   }
 }
 bool take(Capture&result){if(armRequest.load(std::memory_order_acquire))return false;int wanted=Ready;if(!state.compare_exchange_strong(wanted,Reading,std::memory_order_acq_rel))return false;
   result.sampleRate=fs;result.left.assign(data[0].begin(),data[0].begin()+used);result.right.assign(data[1].begin(),data[1].begin()+used);result.onsetSample=onset;result.hostPositionKnown=known;result.defaultStart=start;result.defaultEnd=std::clamp(lastVoiced,start+1,used);state.store(Complete,std::memory_order_release);return true;
 }
private:
 void clear(){prePos=preCount=candidate=quiet=used=start=lastVoiced=0;noise=.0003;envelope=0;known=hadPosition=false;elapsed=0;}
 void finish(){if(used-start>=int(fs*.06)){state.store(Ready,std::memory_order_release);}else{state=Armed;clear();}}
 double fs=48000,noise=.0003,envelope=0;int capacity=0,preSize=0,prePos=0,preCount=0,candidate=0,quiet=0,used=0,start=0,lastVoiced=0;
 std::int64_t onset=0,candidatePosition=0,expectedPosition=0;bool known=false,candidateKnown=false,hadPosition=false;
 std::array<std::vector<float>,2>data,pre;std::atomic<int>state{Idle};std::atomic<bool>armRequest{false},stopRequest{false};std::atomic<double>elapsed{0};
};

// Original eight-delay feedback network, rendered offline. Orthogonal feedback
// mixes energy; per-line decay follows 60 dB attenuation at the requested time.
// Reverse a forward reverberated syllable and fade its first and last samples.
inline bool render(const Capture&capture,Settings s,Render&out,const std::function<bool()>&cancel={}){
 const double fs=capture.sampleRate;if(!std::isfinite(fs)||fs<8000||fs>192000||capture.left.empty()||capture.left.size()!=capture.right.size())return false;
 auto bound=[](double x,double lo,double hi,double fallback){return std::isfinite(x)?std::clamp(x,lo,hi):fallback;};
 s.length=bound(s.length,.25,8,2);s.decay=bound(s.decay,.25,8,2.5);s.tone=bound(s.tone,800,18000,7500);s.level=bound(s.level,-30,0,-6);s.rate=bound(s.rate,.5,20,6);s.depth=bound(s.depth,0,1,.7);s.start=bound(s.start,0,.95,0);s.end=bound(s.end,.05,1,1);
 const int autoStart=std::clamp(capture.defaultStart,0,int(capture.left.size())-1),autoEnd=std::clamp(capture.defaultEnd,autoStart+1,int(capture.left.size()));
 const int span=autoEnd-autoStart;const int first=std::clamp(autoStart+int(s.start*span),autoStart,autoEnd-1),last=std::clamp(autoStart+int(s.end*span),first+1,autoEnd);
 const int count=std::max(1,int(std::round(s.length*fs)));out={};out.sampleRate=fs;out.lengthSeconds=double(count)/fs;out.endSample=capture.onsetSample+(first-autoStart);out.hostPositionKnown=capture.hostPositionKnown;out.settings=s;out.left.assign(count,0);out.right.assign(count,0);
 std::array<std::vector<double>,8>delay;std::array<int,8>head{};std::array<double,8>filter{},decay{};
 const std::array<double,8>seconds{.0297,.0371,.0411,.0437,.0531,.0617,.0713,.0791};
 for(int j=0;j<8;++j){int length=std::max(7,int(fs*seconds[j]));delay[j].assign(length,0);decay[j]=std::pow(.001,length/(fs*s.decay));}
 const double toneK=1-std::exp(-2*pi*std::min(s.tone,fs*.44)/fs),highK=1-std::exp(-2*pi*90/fs);std::array<double,2>hpState{};double peak=0;
 for(int i=0;i<count;++i){if((i&2047)==0&&cancel&&cancel())return false;
   const int src=first+i;std::array<double,2>input{};if(src<last){double fade=std::min(1.,double(i)/std::max(1,int(fs*.004)))*std::min(1.,double(last-src-1)/std::max(1,int(fs*.010)));input[0]=clean(capture.left[src])*fade;input[1]=clean(capture.right[src])*fade;}
   for(int c=0;c<2;++c){hpState[c]+=highK*(input[c]-hpState[c]);input[c]-=hpState[c];}
   std::array<double,8>d{};double sum=0;for(int j=0;j<8;++j){d[j]=delay[j][head[j]];filter[j]+=toneK*(d[j]-filter[j]);d[j]=filter[j];sum+=d[j];}
   // Householder reflection I - 2vv^T, v=(1,...,1)/sqrt(8).
   for(int j=0;j<8;++j){delay[j][head[j]]=input[j%2]*.27+(d[j]-.25*sum)*decay[j];head[j]=(head[j]+1)%int(delay[j].size());}
   const double left=(d[0]+d[2]-d[4]+d[6])*.35+input[0]*.12,right=(d[1]+d[3]-d[5]+d[7])*.35+input[1]*.12;
   int reverse=count-1-i;const double t=double(reverse)/std::max(1,count-1),shape=.12+.88*std::pow(t,.8);
   const double fade=std::min(1.,double(reverse)/std::max(1,int(fs*.015)))*std::min(1.,double(count-1-reverse)/std::max(1,int(fs*.006)));
   const double modulation=s.tremolo?1-s.depth*.5*(1+std::cos(2*pi*s.rate*double(reverse)/fs)):1;
   const double factor=shape*fade*modulation;out.left[reverse]=float(left*factor);out.right[reverse]=float(right*factor);peak=std::max({peak,std::abs(left*factor),std::abs(right*factor)});
 }
 if(peak<1e-9)return false;
 // Conservative 3 dB reconstruction headroom; the LEVEL setting is the
 // requested sample-peak ceiling, not a claim of mastering normalization.
 const double target=std::pow(10.,(s.level-3)/20),gain=std::min(32.,target/peak);out.peak=0;
 for(int i=0;i<count;++i){if((i&4095)==0&&cancel&&cancel())return false;out.left[i]=float(out.left[i]*gain);out.right[i]=float(out.right[i]*gain);out.peak=std::max({out.peak,std::abs(out.left[i]),std::abs(out.right[i])});}
 return !(cancel&&cancel());
}
}
