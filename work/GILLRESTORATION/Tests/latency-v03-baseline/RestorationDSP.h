#pragma once
// Original localized impulse restoration. Research background (not copied code):
// L. Oudre, IPOL 2015, doi:10.5201/ipol.2015.64, detection followed by interpolation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace gillrestoration {
enum class Mode { Declick, Decrackle };
inline double finiteAmount(double x) noexcept { return std::isfinite(x)?std::clamp(x,0.0,1.0):0.0; }
inline double validSampleRate(double fs) noexcept { return std::isfinite(fs)&&fs>=8000.0&&fs<=192000.0?fs:48000.0; }

class RestorationEngine {
public:
    void prepare(double fs,Mode selectedMode) {
        sampleRate=validSampleRate(fs);mode=selectedMode;
        latency=static_cast<int>(std::ceil(sampleRate*(mode==Mode::Declick?0.004:0.008)));
        stride=std::max(1,static_cast<int>(std::lround(sampleRate/48000.0)));
        maximumGap=std::max(2,static_cast<int>(std::ceil(sampleRate*(mode==Mode::Declick?0.00065:0.00035))));
        if(!storage)storage=std::make_unique<Storage>();
        reset();
    }
    void setAmount(double amount) noexcept {
        requested=finiteAmount(amount);
        if(!started)current=requested;
    }
    int getLatencySamples() const noexcept { return latency; }
    void reset() noexcept {
        if(storage){for(auto& c:storage->channel){c.raw.fill(0);c.repaired.fill(0);c.coveredUntil=-1;c.noise=0;c.residual=0;c.level=0;c.nextContext=0;}}
        position=0;current=requested;started=false;lastChannels=0;
    }
    void process(float** data,int nChannels,int nSamples,float** delayedDry=nullptr) noexcept { processImpl(data,nChannels,nSamples,delayedDry); }
    void process(double** data,int nChannels,int nSamples,double** delayedDry=nullptr) noexcept { processImpl(data,nChannels,nSamples,delayedDry); }
private:
    static constexpr int ringSize=8192,mask=ringSize-1;
    struct Channel {
        std::array<double,ringSize> raw{},repaired{};
        std::int64_t coveredUntil=-1,nextContext=0;
        double noise=0,residual=0,level=0;
    };
    struct Storage { std::array<Channel,2> channel; };
    std::unique_ptr<Storage> storage;
    Mode mode=Mode::Declick;
    double sampleRate=48000,requested=.55,current=.55;
    int latency=192,stride=1,maximumGap=32,lastChannels=0;
    std::int64_t position=0;
    bool started=false;

    static int index(std::int64_t n) noexcept { return static_cast<int>(static_cast<std::uint64_t>(n)&mask); }
    static double raw(const Channel& c,std::int64_t n) noexcept { return n<0?0.0:c.raw[static_cast<size_t>(index(n))]; }
    static double repaired(const Channel& c,std::int64_t n) noexcept { return n<0?0.0:c.repaired[static_cast<size_t>(index(n))]; }
    static double median(std::array<double,24>& a) noexcept {
        std::nth_element(a.begin(),a.begin()+12,a.end());return a[12];
    }
    static double strongerSideMedian(const std::array<double,24>& a) noexcept {
        std::array<double,12> left{},right{};
        for(size_t j=0;j<12;++j){left[j]=a[2*j];right[j]=a[2*j+1];}
        std::nth_element(left.begin(),left.begin()+6,left.end());
        std::nth_element(right.begin(),right.begin()+6,right.end());
        return std::max(left[6],right[6]);
    }
    void updateContext(Channel& c,std::int64_t t) noexcept {
        // Robust statistics from BOTH sides, excluding the whole possible
        // repair. Natural consonants/attacks therefore raise their own threshold.
        std::array<double,24> differences{},curvature{},levels{};
        for(int j=0;j<12;++j){
            const int offset=std::min((8+3*j)*stride,latency-maximumGap-2);
            const std::int64_t points[2]={t-offset,t+maximumGap+offset};
            for(int side=0;side<2;++side){
                const auto q=points[side];const double x=raw(c,q),a=raw(c,q-1),b=raw(c,q+1);
                const size_t k=static_cast<size_t>(2*j+side);
                differences[k]=std::abs(x-a);curvature[k]=std::abs(x-.5*(a+b));levels[k]=std::abs(x);
            }
        }
        // Taking the stronger side protects a natural attack next to silence;
        // pooling silence with its onset would halve the median spuriously.
        c.noise=strongerSideMedian(differences);c.residual=strongerSideMedian(curvature);c.level=median(levels);
        c.nextContext=t+8*stride;
    }
    void repairGap(Channel& c,std::int64_t first,int length) noexcept {
        const double left=repaired(c,first-1),right=raw(c,first+length);
        const double distance=static_cast<double>(length+1);
        const double slopeLimit=std::max(1.e-8,4*c.noise+std::abs(right-left)/distance);
        const double a=std::clamp(left-repaired(c,first-2),-slopeLimit,slopeLimit)*distance;
        const double b=std::clamp(raw(c,first+length+1)-right,-slopeLimit,slopeLimit)*distance;
        const double bound=std::max(std::abs(left),std::abs(right))+std::max(4*c.noise,.1*c.level);
        for(int j=0;j<length;++j){
            const double u=(j+1)/distance,u2=u*u,u3=u2*u;
            double y=(2*u3-3*u2+1)*left+(u3-2*u2+u)*a+(-2*u3+3*u2)*right+(u3-u2)*b;
            y=std::clamp(y,-std::max(bound,1.e-9),std::max(bound,1.e-9));
            c.repaired[static_cast<size_t>(index(first+j))]=std::isfinite(y)?y:0.0;
        }
        c.coveredUntil=first+length-1;
    }
    void detect(Channel& c,std::int64_t t) noexcept {
        if(t<=c.coveredUntil)return;
        if(t>=c.nextContext)updateContext(c,t);
        const double x=raw(c,t),left=repaired(c,t-1),step=x-left;
        const double factor=mode==Mode::Declick?12.0-7.0*current:7.5-4.0*current;
        const double threshold=std::max({2.e-5,c.noise*factor,c.level*(mode==Mode::Declick?.035:.015)});
        const double previousStep=left-repaired(c,t-2);
        const double innovation=step-previousStep;
        if(std::abs(step)>threshold && std::abs(innovation)>.70*std::abs(step)
           && std::abs(innovation)>std::max(2.e-5,c.residual*factor*1.5)){
            for(int length=1;length<=maximumGap;++length){
                const double right=raw(c,t+length),endStep=right-raw(c,t+length-1);
                if(step*endStep>=0 || std::abs(endStep)<std::max(.5*threshold,.32*std::abs(step)))continue;
                if(std::abs(endStep-(raw(c,t+length+1)-right))<.65*std::abs(endStep))continue;
                double peak=0;
                for(int j=0;j<length;++j){const double predicted=left+(right-left)*(j+1.0)/(length+1.0);peak=std::max(peak,std::abs(raw(c,t+j)-predicted));}
                if(peak<threshold)continue;
                repairGap(c,t,length);return;
            }
        }
        if(mode==Mode::Decrackle){
            // Fine-crackle path: a short two-sided prediction innovation, not
            // merely the longer isolated-edge detector with a renamed knob.
            // Agreement of forward/backward innovations prevents flagging a
            // clean sample immediately adjacent to a future impulse.
            const double a=repaired(c,t-2),r=raw(c,t+1),b=raw(c,t+2);
            const double forward=x-(2*left-a),backward=x-(2*r-b);
            const double residual=x-.5*(left+r);
            const double limit=std::max({2.e-5,c.residual*(10.0-5.5*current),c.level*.009});
            if(forward*backward>0 && std::min(std::abs(forward),std::abs(backward))>limit*1.3
               && std::abs(residual)>limit && (x-left)*(x-r)>0)
                repairGap(c,t,1);
        }
    }
    template<class T> void processImpl(T** data,int nChannels,int nSamples,T** delayedDry) noexcept {
        if(!data || nSamples<=0 || nChannels<=0)return;
        const int count=std::min(nChannels,2);
        if(!storage){for(int c=0;c<count;++c)if(data[c])for(int i=0;i<nSamples;++i){if(delayedDry&&delayedDry[c])delayedDry[c][i]=T{};data[c][i]=T{};}return;}
        if(lastChannels!=0 && lastChannels!=count)reset();lastChannels=count;started=true;
        const double smoothing=1.0-std::exp(-1.0/(.015*sampleRate));
        for(int i=0;i<nSamples;++i,++position){
            current+=smoothing*(requested-current);
            for(int ch=0;ch<count;++ch){
                if(!data[ch])continue;
                auto& c=storage->channel[static_cast<size_t>(ch)];
                double input=static_cast<double>(data[ch][i]);if(!std::isfinite(input))input=0.0;
                // Bound only malformed extreme inputs so internal arithmetic
                // cannot overflow; ordinary audio and amount-zero remain exact.
                if(std::abs(input)>1.e100)input=std::copysign(1.e100,input);
                const auto write=static_cast<size_t>(index(position));c.raw[write]=input;c.repaired[write]=input;
                const auto target=position-latency;const double original=raw(c,target);
                if(target>=0)detect(c,target);
                const double fixed=repaired(c,target);
                const double strength=requested==0.0?0.0:std::min(1.0,current*4.0);
                double out=strength==0?original:original+strength*(fixed-original);
                if(!std::isfinite(out))out=0;
                T cast=static_cast<T>(out);if(!std::isfinite(static_cast<double>(cast)))cast=T{};
                if(delayedDry && delayedDry[ch])delayedDry[ch][i]=static_cast<T>(original);
                data[ch][i]=cast;
            }
        }
    }
};
} // namespace gillrestoration
