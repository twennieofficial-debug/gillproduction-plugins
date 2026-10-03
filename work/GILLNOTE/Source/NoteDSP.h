#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <numeric>
#include <vector>

namespace gill::note {
constexpr double maxSeconds=300, analysisRate=12000, hop=.01;
struct PitchFrame { float midi=0,confidence=0,rms=0; };
struct Note { int id=0;double start=0,end=0;float detected=60,target=60,strength=0,confidence=0; };
struct Plan { double duration=0,start=0;std::vector<PitchFrame> frames;std::vector<Note> notes; };
inline float midiFor(double frequency){return float(69+12*std::log2(frequency/440));}
inline double frequencyFor(float midi){return 440*std::exp2((midi-69)/12.);}
inline float median(std::vector<float> x){if(x.empty())return 0;auto i=x.begin()+x.size()/2;std::nth_element(x.begin(),i,x.end());return *i;}
// Original normalised autocorrelation detector. FFT padding prevents circular
// wrap; lag-specific energies avoid amplitude/window-length bias. Not polyphonic.
class Detector {
 static constexpr int size=2048,window=768;
 std::array<std::complex<float>,size> spectrum{},roots{};
 std::array<int,size> reversed{};
 std::array<double,window+1> energy{};
 std::array<float,202> corr{};
 void fft(bool inverse){
  for(int i=0;i<size;++i)if(i<reversed[i])std::swap(spectrum[i],spectrum[reversed[i]]);
  for(int len=2;len<=size;len*=2)for(int start=0;start<size;start+=len)for(int j=0;j<len/2;++j){auto w=roots[j*(size/len)];if(inverse)w=std::conj(w);auto a=spectrum[start+j],b=spectrum[start+j+len/2]*w;spectrum[start+j]=a+b;spectrum[start+j+len/2]=a-b;}
  if(inverse)for(auto&v:spectrum)v/=float(size);
 }
public:
 Detector(){for(int i=0;i<size;++i){roots[i]=std::polar(1.f,float(-2*3.141592653589793*i/size));int n=i,r=0;for(int j=0;j<11;++j){r=(r<<1)|(n&1);n>>=1;}reversed[i]=r;}}
 PitchFrame detect(const std::vector<float>&audio,int centre){
  spectrum.fill({});energy.fill(0);double mean=0;int begin=centre-window/2;for(int i=0;i<window;++i)if(begin+i>=0&&begin+i<int(audio.size()))mean+=audio[std::size_t(begin+i)];mean/=window;
  for(int i=0;i<window;++i){float x=begin+i>=0&&begin+i<int(audio.size())?audio[std::size_t(begin+i)]:0;x-=float(mean);spectrum[i]=x;energy[i+1]=energy[i]+double(x)*x;}
  PitchFrame result;result.rms=float(std::sqrt(energy[window]/window));if(result.rms<.0015f)return result;
  fft(false);for(auto&v:spectrum)v={std::norm(v),0};fft(true);
  for(int lag=11;lag<=201;++lag){double den=std::sqrt(energy[window-lag]*(energy[window]-energy[lag]));corr[lag]=den>1.e-12?float(spectrum[lag].real()/den):0;}
  int best=0;float score=0;for(int lag=12;lag<=200;++lag)if(corr[lag]>=corr[lag-1]&&corr[lag]>corr[lag+1]){float s=corr[lag]-.00008f*lag;if(s>score){best=lag;score=s;}}
  if(!best||corr[best]<.80f)return result;
  // Prefer the first equally convincing period, preventing octave-down choices.
  for(int lag=12;lag<best;++lag)if(corr[lag]>=corr[lag-1]&&corr[lag]>corr[lag+1]&&corr[lag]>.90f&&corr[lag]>=corr[best]-.025f){best=lag;break;}
  float den=corr[best-1]-2*corr[best]+corr[best+1],delta=std::abs(den)>1.e-8f?.5f*(corr[best-1]-corr[best+1])/den:0;
  result.midi=midiFor(analysisRate/(best+std::clamp(delta,-.5f,.5f)));result.confidence=std::clamp(corr[best],0.f,1.f);return result;
 }
};
inline Plan analyse(const std::vector<float>&audio,double duration,const std::function<bool()>&cancel={}){
 Plan p;p.duration=duration;Detector detector;const int count=int(std::ceil(duration/hop));p.frames.reserve(count);
 for(int i=0;i<count;++i){if(cancel&&cancel())return {};p.frames.push_back(detector.detect(audio,int(std::llround((i+.5)*hop*analysisRate))));}
 auto original=p.frames;for(int i=1;i+1<count;++i)if(original[i-1].confidence>.8&&original[i].confidence>.8&&original[i+1].confidence>.8)p.frames[i].midi=median({original[i-1].midi,original[i].midi,original[i+1].midi});
 int begin=-1;std::vector<float>pitches,conf;
 auto finish=[&](int end){if(begin>=0&&end-begin>=6&&!pitches.empty()){float m=median(pitches);p.notes.push_back({int(p.notes.size()),begin*hop,std::min(duration,end*hop),m,m,0,median(conf)});}begin=-1;pitches.clear();conf.clear();};
 for(int i=0;i<count;++i){auto f=p.frames[i];if(f.confidence<.8f){finish(i);continue;}
  float reference=pitches.empty()?f.midi:median(std::vector<float>(pitches.begin(),pitches.begin()+std::min<std::size_t>(32,pitches.size())));
  bool change=begin>=0&&i-begin>=6&&std::abs(f.midi-reference)>.72f;
  if(change&&i+2<count){float next=median({p.frames[i].midi,p.frames[i+1].midi,p.frames[i+2].midi});change=p.frames[i+1].confidence>.8f&&p.frames[i+2].confidence>.8f&&std::abs(next-reference)>.72f;}
  if(change)finish(i);if(begin<0)begin=i;pitches.push_back(f.midi);conf.push_back(f.confidence);
 }finish(count);return p;
}
inline float shiftAt(const Plan&p,double time){
 auto found=std::upper_bound(p.notes.begin(),p.notes.end(),time,[](double t,const Note&n){return t<n.start;});if(found==p.notes.begin())return 0;const auto&n=*--found;if(time<n.start||time>=n.end)return 0;
 int i=std::clamp(int(time/hop),0,std::max(0,int(p.frames.size())-1));float current=n.detected;if(!p.frames.empty()&&p.frames[std::size_t(i)].confidence>.8f)current=p.frames[std::size_t(i)].midi;
 float change=n.target-n.detected+n.strength*(n.detected-current);double fade=std::min({1.,(time-n.start)/.015,(n.end-time)/.015});return std::clamp(change,-24.f,24.f)*float(std::clamp(fade,0.,1.));
}
inline bool neutral(const Plan&p){for(auto&n:p.notes)if(std::abs(n.target-n.detected)>.00001f||n.strength>.00001f)return false;return true;}
inline float snap(float midi,int key,int scale){static constexpr int major[]{0,2,4,5,7,9,11},minor[]{0,2,3,5,7,8,10};if(scale==0)return std::round(midi);float best=std::round(midi),distance=100;for(int n=int(std::floor(midi))-12;n<int(std::ceil(midi))+12;++n){int c=((n-key)%12+12)%12;const auto*allowed=scale==1?major:minor;if(std::find(allowed,allowed+7,c)!=allowed+7&&std::abs(midi-n)<distance){best=float(n);distance=std::abs(midi-n);}}return best;}
}
