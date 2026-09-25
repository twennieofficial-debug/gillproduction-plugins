#pragma once
#include "NextDSPCommon.h"
#include <vector>
#include <string>
#include <limits>

namespace gillnext {
using AlignAudio=std::vector<std::vector<float>>;
struct AlignParameters {float tightness=75,maxShiftMs=120;};
struct AlignPoint {double outputSeconds=0,sourceSeconds=0;float confidence=0;};
struct AlignResult {AlignAudio audio;std::vector<AlignPoint>mapping;float confidence=0;bool success=false;std::string message;};

// Offline only: allocates and analyses entire isolated vocal takes. The root
// wrapper must call align on a worker and publish an immutable result to audio.
class AlignDSP {
public:
    static AlignResult align(const AlignAudio&guide,const AlignAudio&dub,double fs,const AlignParameters&p){
        AlignResult result;
        if(!valid(guide)||!valid(dub)||!std::isfinite(fs)||fs<8000||fs>192000){result.message="Invalid audio format";return result;}
        const size_t length=dub[0].size();
        if(length>size_t(fs*120)||guide[0].size()>size_t(fs*120)){result.message="Take exceeds 120 seconds";return result;}
        result.audio=dub;for(auto&channel:result.audio)for(auto&x:channel)x=float(detail::input(x));
        const double amount=std::clamp(detail::finite(p.tightness),0.,100.)*.01;
        const int maximum=int(std::round(std::clamp(detail::finite(p.maxShiftMs),0.,250.)*.001*fs));
        if(amount==0||maximum==0){result.mapping={{0,0,1},{length/fs,length/fs,1}};result.confidence=1;result.success=true;result.message="Original timing";return result;}
        const int hop=std::max(1,int(std::round(fs*.005)));const int frames=int((length+hop-1)/hop);const int band=std::min(frames-1,int(std::ceil(double(maximum)/hop)));
        if(frames<12||band<1){result.message="Take is too short";return result;}
        const auto g=features(guide,fs,hop,frames),d=features(dub,fs,hop,frames);
        if(g.active<6||d.active<6||g.variation<.015||d.variation<.015){result.message="Not enough vocal timing information";return result;}
        const int width=band*2+1;const double inf=std::numeric_limits<double>::infinity();
        std::vector<double>prev(width,inf),curr(width,inf);std::vector<unsigned char>trace(size_t(frames)*width,255);
        auto cost=[&](int i,int j){const auto&a=g.values[i];const auto&b=d.values[j];double e=.65*std::pow(a[0]-b[0],2)+2.0*std::pow(a[1]-b[1],2)+.08*std::pow(a[2]-b[2],2);e+=.0015*std::pow(double(j-i)/std::max(1,band),2);return e;};
        for(int i=0;i<frames;++i){std::fill(curr.begin(),curr.end(),inf);for(int k=0;k<width;++k){const int j=i+k-band;if(j<0||j>=frames)continue;const double own=cost(i,j);double best=inf;unsigned char step=255;
                if(i==0){best=.012*std::abs(j);step=3;}
                else{
                    if(std::isfinite(prev[k])){best=prev[k];step=0;}
                    if(k+1<width&&prev[k+1]+.045<best){best=prev[k+1]+.045;step=1;}
                    if(k>0&&curr[k-1]+.045<best){best=curr[k-1]+.045;step=2;}
                }
                curr[k]=best+own;trace[size_t(i)*width+k]=step;
            }prev.swap(curr);}
        int end=-1;double best=inf;for(int k=0;k<width;++k){const int j=frames-1+k-band;if(j>=0&&j<frames){const double c=prev[k]+.012*(frames-1-j);if(c<best){best=c;end=k;}}}
        if(end<0||!std::isfinite(best)){result.message="No bounded timing match";return result;}
        std::vector<double>positions(frames),counts(frames);int i=frames-1,j=i+end-band;
        while(i>=0&&j>=0){const int k=j-i+band;if(k<0||k>=width)break;positions[i]+=j;++counts[i];const auto step=trace[size_t(i)*width+k];if(step==3)break;if(step==0){--i;--j;}else if(step==1)--i;else if(step==2)--j;else break;}
        double last=0;for(int n=0;n<frames;++n){if(counts[n]>0)last=positions[n]/counts[n];else last=std::max(0.,last+1);positions[n]=last;}
        // A smooth monotone map avoids abrupt phoneme cuts. Each analysis hop
        // changes source time by 0.75..1.25 hops; maxShift remains a hard bound.
        std::vector<double>shift(frames),smoothed(frames);for(int n=0;n<frames;++n)shift[n]=std::clamp((positions[n]-n)*hop,-double(maximum),double(maximum));
        // Do not wait until the next word has already started before changing
        // the map. DTW is underdetermined in silence: interpolate between the
        // measured attack anchors so the bounded warp can anticipate each word.
        std::vector<int>anchors;for(int n=3;n<frames-3;++n){if(g.values[n][1]<.08)continue;bool peak=true;for(int a=-3;a<=3;++a)if(g.values[n+a][1]>g.values[n][1])peak=false;if(!peak)continue;if(!anchors.empty()&&n-anchors.back()<16){if(g.values[n][1]>g.values[anchors.back()][1])anchors.back()=n;}else anchors.push_back(n);}
        if(anchors.size()>=2){const auto measured=shift;size_t next=0;for(int n=0;n<frames;++n){while(next+1<anchors.size()&&n>anchors[next+1])++next;if(n<=anchors.front())shift[n]=measured[anchors.front()];else if(next+1>=anchors.size())shift[n]=measured[anchors.back()];else{const int a=anchors[next],b=anchors[next+1];const double f=double(n-a)/(b-a);shift[n]=measured[a]+f*(measured[b]-measured[a]);}}}
        constexpr int radius=5;for(int n=0;n<frames;++n){double sum=0,weight=0;for(int a=-radius;a<=radius;++a){const int k=std::clamp(n+a,0,frames-1);const double w=radius+1-std::abs(a);sum+=shift[k]*w;weight+=w;}smoothed[n]=sum/weight;}
        const double maxStep=.25*hop;for(int n=1;n<frames;++n)smoothed[n]=std::clamp(smoothed[n],smoothed[n-1]-maxStep,smoothed[n-1]+maxStep);for(int n=frames-2;n>=0;--n)smoothed[n]=std::clamp(smoothed[n],smoothed[n+1]-maxStep,smoothed[n+1]+maxStep);
        double error=0,identityError=0,activity=0;for(int n=0;n<frames;++n){const int at=std::clamp(int(std::round(n+smoothed[n]/hop)),0,frames-1);const double weight=.1+std::max(g.values[n][0],d.values[at][0]);error+=weight*cost(n,at);identityError+=weight*cost(n,n);activity+=weight;}
        const double mean=error/std::max(1.,activity);const double confidence=std::clamp(1-std::sqrt(mean)/.7,0.,1.);
        result.confidence=float(confidence);
        if(confidence<.3||error>identityError*1.10+.01){result.message="Timing match uncertain; original retained";return result;}
        // Confidence judges the detected match. Tightness is the user's amount
        // of that trusted correction, not a reason to reject a softer setting.
        for(auto&v:smoothed)v*=amount;
        renderWsola(dub,fs,hop,maximum,smoothed,result,confidence);
        result.success=true;result.message="Aligned captured double";return result;
    }
private:
    struct Features {std::vector<std::array<double,3>>values;int active=0;double variation=0;};
    static bool valid(const AlignAudio&a){if(a.empty()||a.size()>2||a[0].empty())return false;for(const auto&c:a)if(c.size()!=a[0].size())return false;return true;}
    static Features features(const AlignAudio&a,double fs,int hop,int frames){
        std::vector<double>power(frames),bright(frames);std::array<double,2>low{};const double alpha=detail::alpha(1/(2*detail::pi*900),fs);
        for(int n=0;n<frames*hop;++n){double e=0,h=0;for(size_t c=0;c<a.size();++c){double x=size_t(n)<a[c].size()?detail::input(a[c][n]):0;low[c]+=alpha*(x-low[c]);e+=x*x/a.size();h+=(x-low[c])*(x-low[c])/a.size();}power[n/hop]+=e/hop;bright[n/hop]+=h/hop;}
        auto sorted=power;std::sort(sorted.begin(),sorted.end());const double reference=std::max(1e-10,sorted[size_t((frames-1)*.95)]);
        Features f;f.values.resize(frames);double sum=0,sum2=0;
        for(int n=0;n<frames;++n){const double e=(power[std::max(0,n-1)]+2*power[n]+power[std::min(frames-1,n+1)])*.25;const double level=std::clamp((10*std::log10((e+1e-12)/reference)+45)/45.,0.,1.2);f.values[n][0]=level;f.values[n][2]=e>reference*.001?std::clamp(bright[n]/(power[n]+1e-12),0.,1.):0;f.active+=e>std::max(1e-8,reference*.01);sum+=level;sum2+=level*level;}
        for(int n=0;n<frames;++n){const double before=f.values[std::max(0,n-2)][0],after=f.values[std::min(frames-1,n+2)][0];f.values[n][1]=after-before;}
        f.variation=std::max(0.,sum2/frames-std::pow(sum/frames,2));return f;
    }
    static void renderWsola(const AlignAudio&input,double fs,int mapHop,int maximum,const std::vector<double>&shift,AlignResult&result,double confidence){
        const int length=int(input[0].size());int window=256;while(window<fs*.032)window*=2;const int hop=window/4,half=window/2;
        const int seek=std::max(1,int(fs*.006));std::vector<double>weights(length);std::vector<double>win(window);for(int k=0;k<window;++k)win[k]=.5-.5*std::cos(2*detail::pi*k/(window-1));
        for(auto&c:result.audio)std::fill(c.begin(),c.end(),0.f);result.mapping.clear();
        int previous=std::numeric_limits<int>::min();
        // One source offset is selected for all channels, preserving stereo time.
        int reference=0;if(input.size()==2){double a=0,b=0;for(int n=0;n<length;++n){a+=double(input[0][n])*input[0][n];b+=double(input[1][n])*input[1][n];}reference=b>a?1:0;}
        for(int centre=0;centre<length+half;centre+=hop){const double p=double(std::min(centre,length-1))/mapHop;const int k=std::min(int(p),int(shift.size())-1);const double frac=p-k;const double wantedShift=shift[k]+frac*(shift[std::min(k+1,int(shift.size())-1)]-shift[k]);const int wanted=int(std::round(centre+wantedShift));
            int lower=std::max(centre-maximum,wanted-seek),upper=std::min(centre+maximum,wanted+seek);
            if(previous!=std::numeric_limits<int>::min()){lower=std::max(lower,previous+hop*3/4);upper=std::min(upper,previous+hop*5/4);}
            if(lower>upper){lower=upper=std::clamp(wanted,centre-maximum,centre+maximum);}
            int chosen=std::clamp(wanted,lower,upper);double best=-2;
            if(centre>0){for(int candidate=lower;candidate<=upper;candidate+=4){double dot=0,a2=0,b2=0;for(int at=hop/2;at<window-hop;at+=8){const int out=centre-half+at,src=candidate-half+at;if(out<0||out>=length||src<0||src>=length||weights[out]<.01)continue;const double a=result.audio[reference][out]/weights[out],b=detail::input(input[reference][src]);dot+=a*b;a2+=a*a;b2+=b*b;}const double corr=a2*b2>1e-15?dot/std::sqrt(a2*b2):0;const double penalty=.015*std::abs(candidate-wanted)/std::max(1,seek);if(corr-penalty>best){best=corr-penalty;chosen=candidate;}}}
            previous=chosen;
            if(centre<length)result.mapping.push_back({centre/fs,chosen/fs,float(confidence)});
            for(int at=0;at<window;++at){const int out=centre-half+at,src=chosen-half+at;if(out<0||out>=length)continue;const double w=win[at];weights[out]+=w;for(size_t c=0;c<input.size();++c){const double sample=src>=0&&src<length?detail::input(input[c][src]):0;result.audio[c][out]+=float(w*sample);}}
        }
        for(size_t c=0;c<input.size();++c)for(int n=0;n<length;++n)result.audio[c][n]=weights[n]>1e-12?float(result.audio[c][n]/weights[n]):0;
        if(!result.mapping.empty()){const auto last=result.mapping.back();result.mapping.push_back({length/fs,length/fs+(last.sourceSeconds-last.outputSeconds),float(confidence)});}
    }
};
}
