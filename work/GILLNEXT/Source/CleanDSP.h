#pragma once
#include "NextDSPCommon.h"
#include <vector>
namespace gillnext {
struct CleanParameters {float noise=30,plosives=30,breaths=20;int listen=0;};
// Linked stereo spectral attenuation. Breath/plosive classification is a
// conservative acoustic heuristic; it is not speech recognition or separation.
class CleanDSP {
public:
 void prepare(double rate,int,int channels){fs=std::clamp(detail::finite(rate,48000),8000.,192000.);ch=std::clamp(channels,1,2);size=256;while(size<fs*.018&&size<4096)size*=2;hop=size/4;
  for(auto&a:input)a.assign(size,0);for(auto&stage:removed)for(auto&a:stage)a.assign(size*2,0);for(auto&a:spectrum)a.resize(size);scratch.resize(size);masks.resize(size/2+1);
  floor.assign(size/2+1,1e-10);power.resize(size/2+1);smooth.assign(size/2+1,{1,1,1});win.resize(size);reverse.resize(size);twiddle.resize(size/2);
  int bits=0;while((1<<bits)<size)++bits;for(int i=0;i<size;++i){win[i]=std::sin(detail::pi*i/size);int v=i,r=0;for(int b=0;b<bits;++b){r=(r<<1)|(v&1);v>>=1;}reverse[i]=r;}for(int i=0;i<size/2;++i)twiddle[i]=std::polar(1.,-2*detail::pi*i/size);meter.prepare(fs);reset();
 }
 void reset()noexcept{for(auto&a:input)std::fill(a.begin(),a.end(),0);for(auto&stage:removed)for(auto&a:stage)std::fill(a.begin(),a.end(),0);std::fill(floor.begin(),floor.end(),1e-10);for(auto&a:smooth)a={1,1,1};pos=out=until=frames=0;bodyHistory=lowHistory=0;for(auto&a:reductions)a=0;weights={};weights[p.listen]=1;meter.reset();}
 void setParameters(const CleanParameters&v)noexcept{p=v;p.noise=std::clamp(detail::finite(p.noise),0.,100.);p.plosives=std::clamp(detail::finite(p.plosives),0.,100.);p.breaths=std::clamp(detail::finite(p.breaths),0.,100.);p.listen=std::clamp(p.listen,0,3);}
 void process(float*const* audio,int channels,int count,const float*const* =nullptr,int=0)noexcept{
  if(input[0].empty()||!audio)return;const int active=std::min(ch,channels);for(int c=0;c<active;++c)if(!audio[c])return;
  for(int n=0;n<count;++n){double ip=0,op=0,pkIn=0,pkOut=0;const double fade=detail::alpha(.005,fs);for(int j=0;j<4;++j)detail::follow(weights[j],p.listen==j?1:0,fade);for(int c=0;c<ch;++c){const double x=c<active?detail::input(audio[c][n]):0,original=input[c][pos];std::array<double,3>residual{};for(int j=0;j<3;++j){residual[j]=removed[j][c][out];removed[j][c][out]=0;}input[c][pos]=x;const float y=float(detail::input(weights[0]*(original-residual[0]-residual[1]-residual[2])+weights[1]*residual[0]+weights[2]*residual[1]+weights[3]*residual[2]));if(c<active){audio[c][n]=y;ip+=x*x/active;op+=double(y)*y/active;pkIn=std::max(pkIn,std::abs(x));pkOut=std::max(pkOut,std::abs(double(y)));}}
   meter.sample(ip,op,pkIn,pkOut);pos=(pos+1)&(size-1);if(++until==hop){until=0;frame(active);}out=(out+1)&(size*2-1);
  }meter.publish();
 }
 int latencySamples()const noexcept{return size;}double tailSeconds()const noexcept{return size/fs;}
 float inputRms()const noexcept{return meter.in.load();}float outputRms()const noexcept{return meter.out.load();}
 float gainReductionDb()const noexcept{return std::max({reductions[0].load(),reductions[1].load(),reductions[2].load()});}
 std::array<float,3> reductionsDb()const noexcept{return{reductions[0].load(),reductions[1].load(),reductions[2].load()};}
private:
 void fft(std::vector<std::complex<double>>&a,bool inverse)noexcept{for(int i=0;i<size;++i)if(i<reverse[i])std::swap(a[i],a[reverse[i]]);for(int length=2;length<=size;length*=2)for(int at=0;at<size;at+=length)for(int j=0;j<length/2;++j){auto w=twiddle[j*size/length];if(inverse)w=std::conj(w);const auto x=a[at+j],y=a[at+j+length/2]*w;a[at+j]=x+y;a[at+j+length/2]=x-y;}if(inverse)for(auto&x:a)x/=size;}
 void frame(int active)noexcept{
  const int bins=size/2;std::fill(power.begin(),power.end(),0);for(int c=0;c<ch;++c){for(int i=0;i<size;++i)spectrum[c][i]=input[c][(pos+i)&(size-1)]*win[i];fft(spectrum[c],false);if(c<active)for(int k=0;k<=bins;++k)power[k]=std::max(power[k],std::norm(spectrum[c][k])*4/(size*size));}
  double lo=0,body=0,hi=0,logs=0,sum=0;int flatBins=0;for(int k=1;k<bins;++k){const double hz=k*fs/size;if(hz<180)lo+=power[k];if(hz>=180&&hz<2200)body+=power[k];if(hz>=2200)hi+=power[k];if(hz>=250&&hz<std::min(14000.,fs*.45)){logs+=std::log(power[k]+1e-20);sum+=power[k];++flatBins;}}
  const double flatness=flatBins?std::exp(logs/flatBins)/(sum/flatBins+1e-20):0;
  const double rate=hop/fs;bodyHistory=std::max(body,bodyHistory*std::exp(-rate/.65));
  const double plosive=std::clamp((lo/(body+hi+1e-10)-1.3)*.7,0.,1.)*std::clamp((lo/(lowHistory+1e-8)-1.05)*.6,0.,1.);
  lowHistory+= (1-std::exp(-rate/.16))*(lo-lowHistory);
  const double breath=std::clamp((flatness-.15)*3,0.,1.)*std::clamp((hi/(body+1e-10)-.4),0.,1.)*std::clamp(1-body/(bodyHistory*.75+1e-8),0.,1.);
  std::array<float,3> maxima{};
  for(int k=0;k<=bins;++k){const double hz=k*fs/size;
   // Noise floor follows minima rapidly, and rises slowly only on diffuse frames.
   const double rise=(flatness>.2?.45:8.0),a=1-std::exp(-rate/(power[k]<floor[k]?.045:rise));floor[k]+=a*(power[k]-floor[k]);
   const double snr=power[k]/std::max(floor[k],1e-12);const double noiseDb=(p.noise*.22)*std::clamp((4-snr)/3.,0.,1.);
   const double plosiveDb=p.plosives*.24*plosive*std::clamp((240-hz)/180.,0.,1.);
   const double breathDb=p.breaths*.18*breath*std::clamp((hz-500)/2000.,0.,1.);
   const std::array<double,3> targets{detail::gain(-noiseDb),detail::gain(-plosiveDb),detail::gain(-breathDb)};
   for(int j=0;j<3;++j){const double aGain=1-std::exp(-rate/(targets[j]<smooth[k][j]?.012:.10));smooth[k][j]+=aGain*(targets[j]-smooth[k][j]);if(std::abs(smooth[k][j]-targets[j])<1e-9)smooth[k][j]=targets[j];maxima[j]=std::max(maxima[j],float(-detail::db(smooth[k][j])));}
   const double gn=smooth[k][0],gp=smooth[k][1],gb=smooth[k][2];masks[k]={1-gn,gn*(1-gp),gn*gp*(1-gb)};
  }
  for(int j=0;j<3;++j)reductions[j]=maxima[j];
  for(int c=0;c<ch;++c)for(int j=0;j<3;++j){for(int k=0;k<size;++k)scratch[k]=spectrum[c][k]*masks[k<=bins?k:size-k][j];fft(scratch,true);for(int i=0;i<size;++i)removed[j][c][(out+1+i)&(size*2-1)]+=scratch[i].real()*win[i]*.5;}
  ++frames;
 }
 CleanParameters p;double fs=48000,bodyHistory=0,lowHistory=0;int ch=2,size=1024,hop=256,pos=0,out=0,until=0,frames=0;
 std::array<std::vector<double>,2>input;std::array<std::array<std::vector<double>,2>,3>removed;std::array<double,4>weights{1,0,0,0};std::vector<std::complex<double>>scratch;std::vector<std::array<double,3>>masks;std::array<std::vector<std::complex<double>>,2>spectrum;
 std::vector<double>floor,power,win;std::vector<int>reverse;std::vector<std::complex<double>>twiddle;std::vector<std::array<double,3>>smooth;
 std::array<std::atomic<float>,3>reductions{};detail::Meter meter;
};
}
