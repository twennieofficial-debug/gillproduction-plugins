#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace gillfinish {
inline constexpr double pi=3.14159265358979323846;
inline double bounded(double x,double lo,double hi,double fallback=0) {return std::isfinite(x)?std::clamp(x,lo,hi):std::clamp(fallback,lo,hi);}
inline double gain(double db) {return std::pow(10.,db*.05);}
inline double db(double v) {return 20*std::log10(std::max(1.e-12,v));}
struct Smooth {
    double value=0,target=0,a=.99;
    void prepare(double fs,double seconds=.02) {a=std::exp(-1./(fs*seconds));value=target;}
    void set(double v){target=v;}
    double next(){value=target+a*(value-target);if(std::abs(value-target)<1.e-10)value=target;return value;}
};
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    std::array<double,2> z1{},z2{};
    void reset(){z1={};z2={};}
    double tick(double x,int c){const double y=b0*x+z1[c];z1[c]=b1*x-a1*y+z2[c];z2[c]=b2*x-a2*y;if(!std::isfinite(y)){z1[c]=z2[c]=0;return 0;}return y;}
    void set(int type,double fs,double hz,double q,double dB=0){
        hz=bounded(hz,5,fs*.475,1000);q=bounded(q,.1,12,.70710678);
        const double w=2*pi*hz/fs,c=std::cos(w),s=std::sin(w),alpha=s/(2*q),A=std::pow(10.,dB/40.);
        double n0=1,n1=0,n2=0,d0=1,d1=0,d2=0;
        if(type==0){n0=(1-c)*.5;n1=1-c;n2=n0;d0=1+alpha;d1=-2*c;d2=1-alpha;}
        else if(type==1){n0=(1+c)*.5;n1=-(1+c);n2=n0;d0=1+alpha;d1=-2*c;d2=1-alpha;}
        else if(type==2){n0=1+alpha*A;n1=-2*c;n2=1-alpha*A;d0=1+alpha/A;d1=-2*c;d2=1-alpha/A;}
        else {const double beta=2*std::sqrt(A)*s/std::sqrt(2.); // RBJ shelf, slope S=1.
            if(type==3){n0=A*((A+1)-(A-1)*c+beta);n1=2*A*((A-1)-(A+1)*c);n2=A*((A+1)-(A-1)*c-beta);d0=(A+1)+(A-1)*c+beta;d1=-2*((A-1)+(A+1)*c);d2=(A+1)+(A-1)*c-beta;}
            else{n0=A*((A+1)+(A-1)*c+beta);n1=-2*A*((A-1)+(A+1)*c);n2=A*((A+1)+(A-1)*c-beta);d0=(A+1)-(A-1)*c+beta;d1=2*((A-1)-(A+1)*c);d2=(A+1)-(A-1)*c-beta;}
        }
        b0=n0/d0;b1=n1/d0;b2=n2/d0;a1=d1/d0;a2=d2/d0;
    }
    double magnitude(double fs,double hz)const{const auto z=std::polar(1.,-2*pi*hz/fs);return std::abs((b0+b1*z+b2*z*z)/(1.+a1*z+a2*z*z));}
};

struct GoldParameters{float lowBoost=0,lowCut=0,lowHz=60,highBoost=0,highCut=0,highHz=8000,bandwidth=50,cutHz=10000,mix=1,outputDb=0;};
class GoldDSP {
public:
    void setParameters(const GoldParameters& v){p=v;const double targets[]{bounded(p.lowBoost,0,12),bounded(p.lowCut,0,18),bounded(p.lowHz,20,200,60),bounded(p.highBoost,0,12),bounded(p.highCut,0,18),bounded(p.highHz,1000,20000,8000),bounded(p.bandwidth,0,100,50),bounded(p.cutHz,1000,20000,10000),bounded(p.mix,0,1,1),bounded(p.outputDb,-18,6)};for(int i=0;i<10;++i)s[i].set(targets[i]);}
    void prepare(double rate,int,int){fs=bounded(rate,8000,384000,48000);for(auto& v:s)v.prepare(fs);reset();}
    void reset(){for(auto& f:filters)f.reset();for(auto&v:s)v.value=v.target;counter=0;configure();}
    int latencySamples()const{return 0;}
    void process(float*const* data,int n,int channels){for(int i=0;i<n;++i){for(auto&v:s)v.next();if(counter++%16==0)configure();const double out=gain(s[9].value),mix=s[8].value;for(int c=0;c<std::min(2,channels);++c){const double x=bounded(data[c][i],-100,100);double y=x;for(auto& f:filters)y=f.tick(y,c);data[c][i]=static_cast<float>((x+mix*(y-x))*out);}}}
    double magnitude(double hz)const{double h=1;for(const auto&f:filters)h*=f.magnitude(fs,hz);return h;}
    static double targetMagnitude(const GoldParameters&p,double rate,double hz){GoldDSP d;d.setParameters(p);d.prepare(rate,1,1);return d.magnitude(hz);}
private:
    void configure(){filters[0].set(3,fs,s[2].value,.707,s[0].value);filters[1].set(3,fs,s[2].value*2.4,.707,-s[1].value);filters[2].set(2,fs,s[5].value,2.8-2.45*s[6].value*.01,s[3].value);filters[3].set(4,fs,s[7].value,.707,-s[4].value);}
    double fs=48000;unsigned counter=0;GoldParameters p;std::array<Smooth,10>s;std::array<Biquad,4>filters;
};

struct DiveParameters{float depth=50,resonance=15,motion=0,rateHz=.5f,envelope=0,mix=1,outputDb=0;};
class DiveDSP{
public:
    void setParameters(const DiveParameters&v){p=v;const double t[]{bounded(v.depth,0,100,50),bounded(v.resonance,0,100),bounded(v.motion,0,100),bounded(v.rateHz,.01,20,.5),bounded(v.envelope,-100,100),bounded(v.mix,0,1,1),bounded(v.outputDb,-18,6)};for(int i=0;i<7;++i)s[i].set(t[i]);}
    void prepare(double rate,int,int){fs=bounded(rate,8000,384000,48000);for(auto&v:s)v.prepare(fs);reset();}
    void reset(){for(auto&v:s)v.value=v.target;for(auto&f:filt)f.reset();phase=env=0;counter=0;cutoff=baseCutoff(s[0].value,fs);configure();}
    int latencySamples()const{return 0;}
    float cutoffHz()const{return static_cast<float>(cutoff);}
    void process(float*const*data,int n,int channels){const int ch=std::min(2,channels);const double ea=std::exp(-1./(fs*.008)),er=std::exp(-1./(fs*.12));for(int i=0;i<n;++i){for(auto&v:s)v.next();double peak=0;for(int c=0;c<ch;++c)peak=std::max(peak,std::abs(bounded(data[c][i],-100,100)));env=(peak>env?ea:er)*(env-peak)+peak;phase+=s[3].value/fs;phase-=std::floor(phase);if(counter++%16==0)configure();const double out=gain(s[6].value),mix=s[5].value;for(int c=0;c<ch;++c){const double x=bounded(data[c][i],-100,100);double y=x;for(auto&f:filt)y=f.tick(y,c);data[c][i]=static_cast<float>((x+mix*(y-x))*out);}}}
    static double baseCutoff(double depth,double rate){return std::min(20000.,rate*.44)*std::pow(180./std::min(20000.,rate*.44),depth*.01);}
private:
    void configure(){const double oct=3*s[2].value*.01*std::sin(2*pi*phase)+3*s[4].value*.01*std::clamp(env*5.,0.,1.);cutoff=std::clamp(baseCutoff(s[0].value,fs)*std::pow(2.,oct),30.,fs*.44);const double res=s[1].value*.01;filt[0].set(0,fs,cutoff,.5411961);filt[1].set(0,fs,cutoff,1.306563+res*1.4);}
    DiveParameters p;std::array<Smooth,7>s;std::array<Biquad,2>filt;double fs=48000,phase=0,env=0,cutoff=1500;unsigned counter=0;
};

struct StripParameters{float inputDb=0,bass=0,treble=0,compress=35,deess=20,space=10,echo=8,width=0,outputDb=0;int style=0;float bpm=120;};
class StripDSP{
public:
    void setParameters(const StripParameters&v){p=v;const double t[]{bounded(v.inputDb,-18,18),bounded(v.bass,-12,12),bounded(v.treble,-12,12),bounded(v.compress,0,100),bounded(v.deess,0,100),bounded(v.space,0,100),bounded(v.echo,0,100),bounded(v.width,0,100),bounded(v.outputDb,-18,6)};for(int i=0;i<9;++i)s[i].set(t[i]);style=std::clamp(v.style,0,2);bpm=bounded(v.bpm,20,300,120);}
    void prepare(double rate,int,int){fs=bounded(rate,8000,384000,48000);for(auto&v:s)v.prepare(fs);for(int c=0;c<2;++c){delay[c].assign(static_cast<size_t>(fs*3.2)+4,0);doubleRing[c].assign(static_cast<size_t>(fs*.08)+4,0);}const double times[]{.0297,.0371,.0411,.0437};for(int k=0;k<4;++k)room[k].assign(static_cast<size_t>(fs*times[k])+1,0);reset();}
    void reset(){for(auto&v:s)v.value=v.target;for(auto&v:delay)std::fill(v.begin(),v.end(),0.f);for(auto&v:doubleRing)std::fill(v.begin(),v.end(),0.f);for(auto&v:room)std::fill(v.begin(),v.end(),0.f);roomPos={};roomDamp={};delayLP={};dpos=wpos=0;phase=env=gr=ess=0;counter=0;low.reset();high.reset();essLow={};configure();delayCurrent=oldDelay=desiredDelay();delayFade=1;}
    int latencySamples()const{return 0;}
    float reductionDb()const{return static_cast<float>(gr);}
    double tailSeconds()const{return 30;}
    void process(float*const*data,int n,int channels){const int ch=std::min(2,channels);const double attack=std::exp(-1./(fs*.008)),release=std::exp(-1./(fs*.13)),deA=std::exp(-1./(fs*.001)),deR=std::exp(-1./(fs*.05));for(int i=0;i<n;++i){for(auto&v:s)v.next();if(counter++%32==0)configure();std::array<double,2>x{},bright{};double power=0,essPeak=0;const double in=gain(s[0].value);for(int c=0;c<ch;++c){x[c]=high.tick(low.tick(bounded(data[c][i],-100,100)*in,c),c);power=std::max(power,x[c]*x[c]);essLow[c]=x[c]+essPole*(essLow[c]-x[c]);bright[c]=x[c]-essLow[c];essPeak=std::max(essPeak,std::abs(bright[c]));}
        const double ec=power>env?attack:release;env=power+ec*(env-power);const double amount=s[3].value*.01,threshold=-14-20*amount,ratio=1+7*amount,over=db(std::sqrt(std::max(0.,env)))-threshold;double reduction=over<=-3?0:over>=3?over*(1-1/ratio):(over+3)*(over+3)/12*(1-1/ratio);reduction=std::min(24.,reduction);gr=reduction+(reduction>gr?attack:release)*(gr-reduction);const double cg=gain(-gr+amount*5.5);
        ess=essPeak+(essPeak>ess?deA:deR)*(ess-essPeak);const double eg=gain(-std::clamp((db(ess)+32)*s[4].value*.01,0.,15.));for(int c=0;c<ch;++c)x[c]=(x[c]+(eg-1)*bright[c])*cg;
        // A stable four-line orthogonal FDN supplies a compact stereo room send.
        std::array<double,4>r{};for(int k=0;k<4;++k){r[k]=room[k][roomPos[k]];roomDamp[k]+=.22*(r[k]-roomDamp[k]);}
        const double mono=ch==2?(x[0]+x[1])*.5:x[0],feedback=style==2?.81:style==1?.69:.75;
        const double h[]{(roomDamp[0]+roomDamp[1]+roomDamp[2]+roomDamp[3])*.5,(roomDamp[0]-roomDamp[1]+roomDamp[2]-roomDamp[3])*.5,(roomDamp[0]+roomDamp[1]-roomDamp[2]-roomDamp[3])*.5,(roomDamp[0]-roomDamp[1]-roomDamp[2]+roomDamp[3])*.5};
        for(int k=0;k<4;++k){room[k][roomPos[k]]=static_cast<float>(mono*.22+feedback*h[k]);roomPos[k]=(roomPos[k]+1)%room[k].size();}
        if(delayFade>=1 && std::abs(desiredDelay()-delayCurrent)>.25){oldDelay=delayCurrent;delayCurrent=desiredDelay();delayFade=0;}delayFade=std::min(1.,delayFade+1./(fs*.03));phase+=.31/fs;if(phase>=1)phase-=1;
        std::array<double,2>repeats{},doubled{};for(int c=0;c<ch;++c){repeats[c]=(1-delayFade)*read(delay[c],dpos,oldDelay)+delayFade*read(delay[c],dpos,delayCurrent);delayLP[c]+=.3*(repeats[c]-delayLP[c]);doubleRing[c][wpos]=static_cast<float>(x[c]);const double d=(c==0?.013:.021)+.0015*std::sin(2*pi*(phase+c*.33));doubled[c]=read(doubleRing[c],wpos,d*fs);}
        const double out=gain(s[8].value),roomSend=s[5].value*.01*.7,echoSend=s[6].value*.01*.65,w=s[7].value*.01*.38;
        for(int c=0;c<ch;++c){const int other=ch==2?1-c:c;delay[c][dpos]=static_cast<float>(x[c]+delayLP[style==2?other:c]*(style==1?.12:.35));const double rv=c==0?(r[0]+r[2]-r[1]-r[3])*.5:(r[0]+r[1]-r[2]-r[3])*.5;data[c][i]=static_cast<float>((x[c]+roomSend*rv+echoSend*repeats[c]+w*doubled[c])*out);}
        dpos=(dpos+1)%delay[0].size();wpos=(wpos+1)%doubleRing[0].size();
    }}
private:
    static double read(const std::vector<float>&r,size_t pos,double samples){double p=static_cast<double>(pos)-samples;while(p<0)p+=r.size();auto a=static_cast<size_t>(p)%r.size();const auto b=(a+1)%r.size();return r[a]+(r[b]-r[a])*(p-std::floor(p));}
    double desiredDelay()const{return std::clamp((style==1?.095:60./bpm*(style==2?.75:1))*fs,1.,static_cast<double>(delay[0].empty()?2:delay[0].size()-2));}
    void configure(){low.set(3,fs,160,.707,s[1].value);high.set(4,fs,4500,.707,s[2].value);essPole=std::exp(-2*pi*std::min(5800.,fs*.3)/fs);}
    StripParameters p;double fs=48000,bpm=120,env=0,gr=0,ess=0,phase=0,delayCurrent=24000,oldDelay=24000,delayFade=1;int style=0;unsigned counter=0;size_t dpos=0,wpos=0;std::array<Smooth,9>s;Biquad low,high;double essPole=.47;std::array<double,2>essLow{};std::array<std::vector<float>,2>delay,doubleRing;std::array<std::vector<float>,4>room;std::array<size_t,4>roomPos{};std::array<double,4>roomDamp{};std::array<double,2>delayLP{};
};
}
