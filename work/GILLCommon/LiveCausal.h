#pragma once
// Original GILLPRODUCTION causal monitoring algorithms. AGPL-3.0-only.
// No lookahead, FFT, allocation, host calls or locks in process(). LIVE trades
// spectral selectivity for immediate monitoring; PRO remains the detailed path.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gill::live {
inline double finite(double x,double fallback=0) noexcept{return std::isfinite(x)?x:fallback;}
inline double clean(double x) noexcept{return std::clamp(finite(x),-32.,32.);}
inline double alpha(double seconds,double fs) noexcept{return 1-std::exp(-1/(std::max(.00001,seconds)*fs));}
inline double db(double x) noexcept{return 20*std::log10(std::max(1e-10,x));}
enum class Kind {Room,Silk,Spark,Clean};
struct Settings {
 double amount=.5,sensitivity=.5,low=80,high=16000,attack=.008,release=.14,mix=1,output=1;
 double noise=.3,plosives=.3,breaths=.2,preserve=.75;bool boost=false;int listen=0;
};
struct Biquad {
 double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
 double process(double x)noexcept{const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
 void reset()noexcept{z1=z2=0;}
 void bandpass(double hz,double fs,double q)noexcept{const double w=6.283185307179586*std::clamp(hz,20.,fs*.45)/fs,a=std::sin(w)/(2*q),den=1+a;b0=a/den;b1=0;b2=-b0;a1=-2*std::cos(w)/den;a2=(1-a)/den;}
 void peak(double sine,double cosine,double gain,double q)noexcept{const double a=std::sqrt(std::max(.05,gain)),alpha=sine/(2*q),den=1+alpha/a;b0=(1+alpha*a)/den;b1=-2*cosine/den;b2=(1-alpha*a)/den;a1=b1;a2=(1-alpha/a)/den;}
};

class CausalBands {
public:
 static constexpr int bands=18;
 void prepare(double fs,int channels,Kind kind) noexcept {
  fs_=std::clamp(finite(fs,48000),8000.,768000.);ch_=std::clamp(channels,1,2);kind_=kind;
  const double top=std::min(18000.,fs_*.43);
  for(int k=0;k<bands-1;++k){const double hz=70*std::pow(top/70.,double(k)/(bands-2));cuts_[k]=hz;coef_[k]=1-std::exp(-6.283185307179586*hz/fs_);}
  for(int k=0;k<bands;++k)hz_[k]=k==0?40:k==bands-1?top*1.2:std::sqrt(cuts_[k-1]*cuts_[k]);
  for(int k=0;k<bands;++k){const double w=6.283185307179586*std::min(hz_[k],fs_*.45)/fs_;sine_[k]=std::sin(w);cosine_[k]=std::cos(w);for(int c=0;c<2;++c)detectors_[c][k].bandpass(hz_[k],fs_,4);}
  fastA_=alpha(.003,fs_);slowA_=alpha(.12,fs_);gainA_=alpha(.005,fs_);reset();
 }
 void reset() noexcept {lp_={};power_={};slow_={};hold_={};floor_.fill(1e-10);gains_.fill(1);wanted_.fill(1);target_.fill(0);for(auto&g:stageGains_)g={1,1,1};for(auto&g:stageWanted_)g={1,1,1};cleanTargets_={};for(auto&ch:detectors_)for(auto&f:ch)f.reset();for(auto&ch:peaks_)for(auto&f:ch)f=Biquad{};reduction_={};view_={};clock_=0;mix_=p_.mix;output_=p_.output;weights_={};weights_[std::clamp(p_.listen,0,3)]=1;bodyHistory_=0;}
 void set(Settings p) noexcept {
  p.amount=std::clamp(finite(p.amount),0.,1.);p.sensitivity=std::clamp(finite(p.sensitivity),0.,1.);
  p.low=std::clamp(finite(p.low,80),20.,20000.);p.high=std::clamp(finite(p.high,16000),p.low,24000.);
  p.attack=std::clamp(finite(p.attack,.008),.0001,.2);p.release=std::clamp(finite(p.release,.14),.005,1.5);
  p.mix=std::clamp(finite(p.mix,1),0.,1.);p.output=std::clamp(finite(p.output,1),.01,4.);
  p.noise=std::clamp(finite(p.noise),0.,1.);p.plosives=std::clamp(finite(p.plosives),0.,1.);p.breaths=std::clamp(finite(p.breaths),0.,1.);
  p.preserve=std::clamp(finite(p.preserve,.75),0.,1.);p.listen=std::clamp(p.listen,0,3);p_=p;
  attackA_=alpha(p.attack,fs_);releaseA_=alpha(p.release,fs_);
 }
 template<class T>void process(T*const*audio,int channels,int samples,T*const*dry=nullptr) noexcept {
  if(!audio||samples<=0)return;const int active=std::clamp(channels,0,ch_);if(!active)return;
  for(int c=0;c<active;++c)if(!audio[c])return;
  for(int n=0;n<samples;++n){std::array<std::array<double,bands>,2>parts{};std::array<double,2>x{};
   for(int c=0;c<active;++c){x[c]=clean(audio[c][n]);if(dry&&dry[c])dry[c][n]=T(x[c]);double last=0;
    for(int k=0;k<bands-1;++k){auto&s=lp_[c][k];s+=coef_[k]*(x[c]-s);parts[c][k]=s-last;last=s;}parts[c][bands-1]=x[c]-last;
   }
   for(int k=0;k<bands;++k){double e=0;for(int c=0;c<active;++c){const double signal=kind_==Kind::Silk?detectors_[c][k].process(x[c]):parts[c][k];e=std::max(e,signal*signal);}power_[k]+=fastA_*(e-power_[k]);slow_[k]+=slowA_*(power_[k]-slow_[k]);hold_[k]=std::max(power_[k],hold_[k]*std::exp(-1/(fs_*.20)));}
   const bool update=(clock_++&15)==0;if(update)analyse();
   mix_+=gainA_*(p_.mix-mix_);output_+=gainA_*(p_.output-output_);
   if(std::abs(mix_-p_.mix)<1e-12)mix_=p_.mix;if(std::abs(output_-p_.output)<1e-12)output_=p_.output;
   for(int j=0;j<4;++j)weights_[j]+=gainA_*((p_.listen==j?1.:0.)-weights_[j]);
   for(int k=0;k<bands;++k){if(kind_==Kind::Clean){for(int j=0;j<3;++j){auto&g=stageGains_[k][j];const double want=stageWanted_[k][j];g+=(want<g?attackA_:releaseA_)*(want-g);}gains_[k]=stageGains_[k][0]*stageGains_[k][1]*stageGains_[k][2];}else gains_[k]+=(wanted_[k]<gains_[k]?attackA_:releaseA_)*(wanted_[k]-gains_[k]);
    if(update&&kind_==Kind::Silk)for(int c=0;c<active;++c)peaks_[c][k].peak(sine_[k],cosine_[k],gains_[k],3);}
   for(int c=0;c<active;++c){double residual=0;std::array<double,3>removed{};
    for(int k=0;k<bands;++k){const double r=parts[c][k]*(1-gains_[k]);residual+=r;
     if(kind_==Kind::Clean){const auto&g=stageGains_[k];removed[0]+=parts[c][k]*(1-g[0]);removed[1]+=parts[c][k]*g[0]*(1-g[1]);removed[2]+=parts[c][k]*g[0]*g[1]*(1-g[2]);}}
    double filtered=x[c]-residual;if(kind_==Kind::Silk){filtered=x[c];for(auto&f:peaks_[c])filtered=f.process(filtered);}
    const double wet=(x[c]+mix_*(filtered-x[c]))*output_;
    const double result=kind_==Kind::Clean?weights_[0]*wet+weights_[1]*removed[0]+weights_[2]*removed[1]+weights_[3]*removed[2]:wet;
    audio[c][n]=T(clean(result));
   }
  }
 }
 double reductionDb()const noexcept {double maximum=0;for(auto g:gains_)maximum=std::max(maximum,std::abs(db(g)));return maximum;}
 std::array<float,3> reductionsDb()const noexcept{return reduction_;}
 std::array<float,128> reductionView()const noexcept{return view_;}
private:
 void analyse() noexcept {
  double low=0,body=0,high=0,oldLow=0;for(int k=0;k<bands;++k){if(hz_[k]<200){low+=power_[k];oldLow+=slow_[k];}else if(hz_[k]<2500)body+=power_[k];else high+=power_[k];}
  bodyHistory_=std::max(body,bodyHistory_*std::exp(-16/(fs_*.65)));reduction_={};view_={};
  const double plosive=std::clamp((low/(body+high+1e-9)-1.5)*.5,0.,1.)*std::clamp((low/(oldLow+1e-8)-1.2)*.5,0.,1.);
  const double breath=std::clamp(high/(body+1e-9)-1.,0.,1.)*std::clamp(1-body/(bodyHistory_*.7+1e-8),0.,1.);
  for(int k=0;k<bands;++k){const double hz=hz_[k];const double focus=std::clamp((hz-p_.low*.6)/(p_.low*.6),0.,1.)*std::clamp((p_.high*1.3-hz)/(p_.high*.3),0.,1.);double cut=0;
   if(kind_==Kind::Room){const double tail=std::clamp(1-power_[k]/(hold_[k]+1e-12),0.,1.);cut=p_.amount*(18-9*p_.preserve)*tail*focus;}
   if(kind_==Kind::Silk){double neighbours=0;int count=0;for(int j=std::max(0,k-3);j<=std::min(bands-1,k+3);++j)if(std::abs(j-k)>1){neighbours+=power_[j];++count;}
    const double prominence=10*std::log10((power_[k]+1e-10)/(neighbours/std::max(1,count)+1e-10));cut=p_.amount*std::clamp((prominence-(14-11*p_.sensitivity))*.6,0.,12.)*focus;}
   if(kind_==Kind::Spark){const double novelty=10*std::log10((power_[k]+1e-8)/(slow_[k]+1e-8));cut=p_.amount*std::clamp((novelty-(10-8*p_.sensitivity))*1.5,0.,p_.boost?9.:18.)*focus*(p_.boost?-1:1);}
   if(kind_==Kind::Clean){const double a=1-std::exp(-16/(fs_*(power_[k]<floor_[k]?.05:5.)));floor_[k]+=a*(power_[k]-floor_[k]);const double snr=power_[k]/std::max(floor_[k],1e-12);
    cleanTargets_[k]={18*p_.noise*std::clamp((3-snr)/2.,0.,1.),20*p_.plosives*plosive*std::clamp((280-hz)/180.,0.,1.),12*p_.breaths*breath*std::clamp((hz-500)/2500.,0.,1.)};
    for(int j=0;j<3;++j){cut+=cleanTargets_[k][j];stageWanted_[k][j]=std::pow(10.,-cleanTargets_[k][j]/20);reduction_[j]=std::max(reduction_[j],float(-db(stageGains_[k][j])));}}
   target_[k]=cut;wanted_[k]=std::pow(10.,-cut/20);const int index=std::clamp(int(127*std::log(std::max(40.,hz)/40.)/std::log(500.)),0,127);view_[index]=float(-db(gains_[k]));
  }
 }
 Settings p_;Kind kind_=Kind::Room;double fs_=48000;int ch_=2;std::uint64_t clock_=0;
 std::array<double,bands-1>coef_{},cuts_{};std::array<double,bands>hz_{},power_{},slow_{},hold_{},floor_{},gains_{},wanted_{},target_{},sine_{},cosine_{};
 std::array<std::array<double,bands-1>,2>lp_{};std::array<std::array<double,3>,bands>cleanTargets_{};
 std::array<std::array<double,3>,bands>stageGains_{},stageWanted_{};std::array<std::array<Biquad,bands>,2>detectors_{},peaks_{};
 std::array<float,3>reduction_{};std::array<float,128>view_{};std::array<double,4>weights_{1,0,0,0};
 double fastA_=0,slowA_=0,gainA_=0,attackA_=.01,releaseA_=.001,mix_=1,output_=1,bodyHistory_=0;
};

// Causal conservative impulse repair. Without future samples LIVE cannot
// reconstruct a long damaged span as accurately as the PRO interpolator.
class ClickRepair {
public:
 void prepare(double fs,bool crackle)noexcept{fs_=std::clamp(finite(fs,48000),8000.,192000.);crackle_=crackle;smooth_=alpha(.005,fs_);reset();}
 void reset()noexcept{state_={};amount_=target_;}
 void setAmount(double amount)noexcept{target_=std::clamp(finite(amount),0.,1.);}
 template<class T>void process(T*const*audio,int channels,int samples,T*const*dry=nullptr)noexcept{
  if(!audio||samples<=0)return;const int active=std::clamp(channels,0,2);for(int c=0;c<active;++c)if(!audio[c])return;
  for(int i=0;i<samples;++i){amount_+=smooth_*(target_-amount_);for(int c=0;c<active;++c){auto&s=state_[c];const double x=clean(audio[c][i]);if(dry&&dry[c])dry[c][i]=T(x);
    const double prediction=s.last+std::clamp(s.last-s.previous,-s.slope*2-.003,s.slope*2+.003);
    const double error=x-prediction,threshold=std::max(crackle_?.012:.025,s.error*(crackle_?7:10)+s.level*.06);
    const bool suspect=std::abs(error)>threshold&&s.age>16;
    const double repaired=suspect?prediction+std::clamp(error,-threshold,threshold):x;
    const double y=x+amount_*(repaired-x);audio[c][i]=T(clean(y));
    // Limit contamination of the robust prediction statistics by the click.
    const double accepted=suspect?repaired:x;s.error+=smooth_*(std::min(std::abs(error),threshold)-s.error);s.level+=smooth_*(std::abs(x)-s.level);s.slope+=smooth_*(std::abs(accepted-s.last)-s.slope);s.previous=s.last;s.last=accepted;++s.age;
  }}
 }
private:
 struct State{double last=0,previous=0,error=.001,level=0,slope=.001;std::uint64_t age=0;};std::array<State,2>state_{};
 double fs_=48000,smooth_=.005,amount_=.55,target_=.55;bool crackle_=false;
};
}
