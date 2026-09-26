#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gill::assist {
constexpr double maxSeconds=300.;
constexpr double hopSeconds=.01;
constexpr int maxPoints=30002;
inline float db(double x){return float(20*std::log10(std::max(1.e-8,x)));}
inline float gain(float d){return std::pow(10.f,d*.05f);}
struct Settings {
    float target=-20,range=10,speed=400,amount=80,gate=18,breath=4,sibilance=4,output=0;
    bool rideOn=true,gateOn=true,breathOn=true,sibilanceOn=true;
    bool operator==(const Settings&o)const{return target==o.target&&range==o.range&&speed==o.speed&&amount==o.amount&&gate==o.gate&&breath==o.breath&&sibilance==o.sibilance&&output==o.output&&rideOn==o.rideOn&&gateOn==o.gateOn&&breathOn==o.breathOn&&sibilanceOn==o.sibilanceOn;}
    bool operator!=(const Settings&o)const{return !(*this==o);}
};
struct Feature{float rms=0,peak=0,highRatio=0,zcr=0;};
// A correction is local to a time interval. Types: gain=0, protect=1,
// gate=2, breath=3, sibilance=4. Gain corrections are additive dB.
struct Edit{double begin=0,end=0;float db=0;int type=0;};
struct Region{double begin=0,end=0;int type=0;float confidence=0;};
struct Plan{
    std::vector<float> gainDb,waveform,ride,gate,breath,sibilance;
    std::vector<Region> regions;
    double duration=0,startSeconds=0;float noiseDb=-70;std::uint64_t revision=0;
    float at(double seconds)const noexcept{
        const double p=seconds/hopSeconds;
        if(p<0||seconds>=duration||gainDb.empty())return 1;
        const auto i=std::min(std::size_t(p),gainDb.size()-1),j=std::min(i+1,gainDb.size()-1);
        return gain(gainDb[i]+float(p-double(i))*(gainDb[j]-gainDb[i]));
    }
};
inline std::vector<float> average(const std::vector<float>&v,int radius){
    std::vector<double> prefix(v.size()+1);for(std::size_t i=0;i<v.size();++i)prefix[i+1]=prefix[i]+v[i];
    std::vector<float> out(v.size());for(int i=0;i<int(v.size());++i){int a=std::max(0,i-radius),b=std::min(int(v.size()),i+radius+1);out[std::size_t(i)]=float((prefix[b]-prefix[a])/(b-a));}return out;
}
inline Plan analyse(const std::vector<Feature>&f,const Settings&s,const std::vector<Edit>&edits,double duration,double start=0){
    Plan p;p.duration=std::min(maxSeconds,duration);p.startSeconds=start;
    const auto n=std::min<std::size_t>(f.size(),maxPoints);if(!n)return p;
    p.waveform.resize(n);p.gainDb.resize(n);p.ride.resize(n);p.gate.resize(n);p.breath.resize(n);p.sibilance.resize(n);
    std::vector<float> levels(n),power(n),sorted;sorted.reserve(n);
    for(std::size_t i=0;i<n;++i){levels[i]=db(f[i].rms);power[i]=f[i].rms*f[i].rms;p.waveform[i]=f[i].peak;if(f[i].rms>1.e-7f)sorted.push_back(levels[i]);}
    if(!sorted.empty()){std::sort(sorted.begin(),sorted.end());p.noiseDb=std::clamp(std::min(sorted[sorted.size()/10],sorted[sorted.size()*8/10]-30.f),-85.f,-30.f);}
    const float openThreshold=std::clamp(p.noiseDb+12,-60.f,-24.f),closeThreshold=openThreshold-5;
    std::vector<float> active(n);bool open=false;int hold=0;
    for(std::size_t i=0;i<n;++i){if(levels[i]>openThreshold){open=true;hold=12;}else if(open&&levels[i]<closeThreshold){if(--hold<=0)open=false;}active[i]=open?1.f:0.f;}
    // Start the 30-ms fade early enough to be fully open before the attack.
    auto extended=active;for(std::size_t i=0;i<n;++i)if(active[i]>0)for(int k=-6;k<=8;++k){int j=int(i)+k;if(j>=0&&j<int(n))extended[std::size_t(j)]=1;}
    auto gateEnvelope=average(extended,3);
    const int radius=std::clamp(int(s.speed/20),2,100);const auto smoothedPower=average(power,radius);
    std::vector<float> ride(n);
    double activePower=0;int activeCount=0;for(std::size_t i=0;i<n;++i)if(active[i]>0){activePower+=power[i];++activeCount;}
    const float global=activeCount?db(std::sqrt(activePower/activeCount)):-80.f;
    for(std::size_t i=0;i<n;++i){const auto local=db(std::sqrt(std::max(0.f,smoothedPower[i])));ride[i]=std::clamp(s.target-(.75f*local+.25f*global),-s.range,s.range)*(s.amount*.01f)*extended[i];}
    ride=average(ride,5);
    std::vector<int> type(n);std::vector<float> confidence(n);
    for(std::size_t i=0;i<n;++i){
        if(f[i].highRatio>.52f&&f[i].zcr>.11f&&levels[i]>openThreshold-4&&levels[i]<-3){type[i]=4;confidence[i]=std::clamp((f[i].highRatio-.40f)*1.5f,0.f,1.f);}
        else if(f[i].highRatio>.18f&&f[i].highRatio<.65f&&f[i].zcr>.08f&&levels[i]<global-6&&levels[i]>p.noiseDb+5){
            bool nearby=false;for(int k=-30;k<=30;k+=3){int j=int(i)+k;if(j>=0&&j<int(n)&&levels[std::size_t(j)]>global-3){nearby=true;break;}}
            if(nearby){type[i]=3;confidence[i]=std::clamp((global-levels[i]-4)/16.f,.2f,.8f);}
        }
        if(!type[i]&&gateEnvelope[i]<.1f&&levels[i]<closeThreshold){type[i]=2;confidence[i]=std::clamp((closeThreshold-levels[i])/20.f,.2f,1.f);}
        p.ride[i]=s.rideOn?ride[i]:0;p.gate[i]=s.gateOn?-s.gate*(1-gateEnvelope[i]):0;
        p.breath[i]=s.breathOn&&type[i]==3?-s.breath*confidence[i]:0;
        p.sibilance[i]=s.sibilanceOn&&type[i]==4?-s.sibilance*confidence[i]:0;
    }
    p.breath=average(p.breath,2);p.sibilance=average(p.sibilance,1);
    for(std::size_t i=0;i<n;){if(!type[i]){++i;continue;}std::size_t j=i+1;double confidenceSum=confidence[i];while(j<n&&type[j]==type[i])confidenceSum+=confidence[j++];if(j-i>=3)p.regions.push_back({i*hopSeconds,std::min(duration,j*hopSeconds),type[i],float(confidenceSum/(j-i))});i=j;}
    for(std::size_t i=0;i<n;++i)p.gainDb[i]=p.ride[i]+p.gate[i]+p.breath[i]+p.sibilance[i]+s.output;
    for(const auto&e:edits){
        int a=std::clamp(int(e.begin/hopSeconds),0,int(n)),b=std::clamp(int(std::ceil(e.end/hopSeconds)),0,int(n));
        for(int i=a;i<b;++i){const float ramp=std::min(1.f,std::min((i-a+1)/3.f,(b-i)/3.f));
            if(e.type==0)p.gainDb[std::size_t(i)]+=std::clamp(e.db,-24.f,24.f)*ramp;
            else{float replacement=e.type==1?0:e.type==2?-s.gate:e.type==3?-s.breath:-s.sibilance;auto&v=p.gainDb[std::size_t(i)];v+=(replacement+s.output-v)*ramp;}
        }
    }
    // Offline sample-peak guard uses adjacent frames to cover interpolation.
    // This does not claim a true-peak brickwall; source changes require re-learn.
    for(std::size_t i=0;i<n;++i){float peak=f[i].peak;if(i)peak=std::max(peak,f[i-1].peak);if(i+1<n)peak=std::max(peak,f[i+1].peak);if(peak>0)p.gainDb[i]=std::min(p.gainDb[i],-1.f-db(peak));p.gainDb[i]=std::clamp(p.gainDb[i],-90.f,24.f);}
    return p;
}
class FeatureAccumulator{
public:
    explicit FeatureAccumulator(double sampleRate):fs(sampleRate),hop(std::max(1,int(std::llround(sampleRate*hopSeconds)))),alpha(std::exp(-2*3.141592653589793*3500/sampleRate)){}
    void push(float l,float r){
        l=std::isfinite(l)?l:0;r=std::isfinite(r)?r:0;const double mono=(l+r)*.5;low=alpha*low+(1-alpha)*mono;
        energy+=(double(l)*l+double(r)*r)*.5;high+=(mono-low)*(mono-low);peak=std::max(peak,std::max(std::abs(l),std::abs(r)));if((mono>=0)!=(previous>=0))++crossings;previous=mono;
        if(++count>=hop)flush();
    }
    void finish(){if(count)flush();}
    std::vector<Feature> features;
private:
    void flush(){if(features.size()<maxPoints)features.push_back({float(std::sqrt(energy/count)),peak,float(std::clamp(high/std::max(1.e-20,energy),0.,1.)),float(crossings)/count});energy=high=0;peak=0;count=crossings=0;}
    double fs,alpha,low=0,energy=0,high=0,previous=0;int hop,count=0,crossings=0;float peak=0;
};
}
