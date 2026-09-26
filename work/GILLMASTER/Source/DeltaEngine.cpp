#include "DeltaEngine.h"
#include <cstring>
#include <limits>

namespace gill::master {
namespace {
void fft(std::vector<std::complex<double>>& a,bool inverse){
    const auto n=a.size();for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t len=2;len<=n;len<<=1){const double angle=2*math::pi/len*(inverse?1:-1);const std::complex<double>wlen(std::cos(angle),std::sin(angle));for(std::size_t i=0;i<n;i+=len){std::complex<double>w(1,0);for(std::size_t j=0;j<len/2;++j){const auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;}}}if(inverse)for(auto&v:a)v/=double(n);
}
}
std::uint64_t DeltaEngine::pack(float l,float r) noexcept { std::uint32_t a=0,b=0;std::memcpy(&a,&l,4);std::memcpy(&b,&r,4);return std::uint64_t(a)|(std::uint64_t(b)<<32); }
Stereo DeltaEngine::unpack(std::uint64_t p) noexcept {const std::uint32_t a=std::uint32_t(p),b=std::uint32_t(p>>32);float l=0,r=0;std::memcpy(&l,&a,4);std::memcpy(&r,&b,4);return {math::input(l),math::input(r)};}
bool DeltaEngine::read(const Cell* ring,std::uint64_t position,Stereo&x) noexcept {
    const auto&cell=ring[position&mask];const auto expected=position+1;
    if(delta09::wireLoad(&cell.stamp)!=expected)return false;
    const auto pcm=delta09::wireLoad(&cell.pcm);if(delta09::wireLoad(&cell.stamp)!=expected)return false;
    x=unpack(pcm);return true;
}
void DeltaEngine::write(Cell*ring,std::uint64_t position,Stereo x) noexcept {
    auto&cell=ring[position&mask];delta09::wireStore(&cell.stamp,0);delta09::wireStore(&cell.pcm,pack(float(x[0]),float(x[1])));delta09::wireStore(&cell.stamp,position+1);
}
DeltaEngine::DeltaEngine():mapping(sizeof(Bus)),post(new Cell[capacity]()) {
    bus=static_cast<Bus*>(mapping.data());token=delta09::newId().lo;if(token==0)token=1;
    worker=std::thread([this]{workerLoop();});
}
DeltaEngine::~DeltaEngine(){exit=true;if(worker.joinable())worker.join();releaseSource();}
void DeltaEngine::releaseSource() noexcept {
    if(bus&&claimedPair>=0)delta09::wireCompare(&bus->pairs[claimedPair].owner,token,0);claimedPair=-1;
}
void DeltaEngine::prepare(double rate){
    releaseSource();fs=std::clamp(math::finite(rate,48000),8000.,192000.);sampleRate=fs;
    // Never reuse a ring's old stamp range after reprepare or source replacement.
    clock=std::max(clock+capacity,delta09::nowMs()*std::uint64_t(192000)+1);lastSourceEnd=0;observedOwner=0;
    ++revision;postRevision=revision;postEnd=0;postOwner=0;connected=false;resultOwner=0;
    matchGain.set(1);matchGain.prepare(fs,.02);previous=anchor={};auditionMode=fadeRemaining=0;fadeLength=std::max(1,int(fs*.005));
}
void DeltaEngine::configure(bool source,int pair) noexcept {sourceMode=source;pairChoice=std::clamp(pair,0,7);}
DeltaEngine::Alignment DeltaEngine::alignment() const noexcept {
    Alignment result;for(int i=0;i<3;++i){const auto before=resultSequence.load(std::memory_order_acquire);if(before&1)continue;
        const auto owner=resultOwner.load();result={owner!=0&&owner==postOwner.load()&&resultRevision.load()==postRevision.load()&&resultPair.load()==pairChoice.load(),resultDelay.load(),resultGain.load(),resultConfidence.load()};
        if(before==resultSequence.load(std::memory_order_acquire))return result;
    }return {};
}
void DeltaEngine::process(float*const* audio,int channels,int frames,int audition,bool match,bool offline,bool bypass) noexcept {
    if(!bus||!audio||channels<1||frames<1)return;channels=std::min(channels,2);
    const bool source=sourceMode.load();const int selected=pairChoice.load();auto&pair=bus->pairs[selected];
    if(source!=lastSource||selected!=lastPair){releaseSource();lastSource=source;lastPair=selected;lastSourceEnd=0;++revision;postRevision=revision;postEnd=0;postOwner=0;}
    if(source){
        if(claimedPair<0&&delta09::wireCompare(&pair.owner,0,token))claimedPair=selected;
        const bool owns=delta09::wireLoad(&pair.owner)==token;sourceConflict=!owns;connected=owns;
        if(!owns)return;
        for(int n=0;n<frames;++n)write(pair.samples,clock++,{audio[0][n],channels==2?audio[1][n]:audio[0][n]});
        delta09::wireStore(&pair.rate,std::uint64_t(fs));delta09::wireStore(&pair.heartbeat,delta09::nowMs());delta09::wireStore(&pair.end,clock);return;
    }
    sourceConflict=false;const auto owner=delta09::wireLoad(&pair.owner),end=delta09::wireLoad(&pair.end);
    const bool available=owner&&end>=std::uint64_t(frames)&&end!=lastSourceEnd&&delta09::wireLoad(&pair.rate)==std::uint64_t(fs)&&delta09::nowMs()-delta09::wireLoad(&pair.heartbeat)<2000;
    connected=available;
    if(!available){
        if(auditionMode!=0){anchor=previous;fadeRemaining=fadeLength;auditionMode=0;}
        if(offline||bypass)fadeRemaining=0;
        for(int n=0;n<frames;++n){const double blend=fadeRemaining>0?double(fadeRemaining)/fadeLength:0;for(int c=0;c<channels;++c){const double x=audio[c][n];const double y=blend?x+blend*(anchor[c]-x):x;audio[c][n]=float(y);previous[c]=y;}if(fadeRemaining>0)--fadeRemaining;}return;
    }
    if(owner!=observedOwner||(lastSourceEnd&&end!=lastSourceEnd+std::uint64_t(frames))){++revision;postRevision=revision;postEnd=0;}
    observedOwner=owner;postOwner=owner;lastSourceEnd=end;const auto start=end-std::uint64_t(frames);
    const auto aligned=alignment();const bool listen=aligned.valid&&!bypass&&!offline&&audition!=0;
    const int selectedAudition=listen?audition:0;if(selectedAudition!=auditionMode){anchor=previous;fadeRemaining=fadeLength;auditionMode=selectedAudition;}
    if(offline||bypass)fadeRemaining=0;matchGain.set(match&&aligned.valid?aligned.gain:1);
    for(int n=0;n<frames;++n){
        Stereo out{audio[0][n],channels==2?audio[1][n]:audio[0][n]},reference{};
        write(post.get(),start+n,out);
        const double gain=matchGain.next();Stereo wanted=out;
        if(listen&&start+std::uint64_t(n)>=std::uint64_t(aligned.delay)&&read(pair.samples,start+n-aligned.delay,reference)){
            for(int c=0;c<channels;++c)wanted[c]=audition==1?reference[c]*gain:out[c]-reference[c]*gain;
        }
        const double blend=fadeRemaining>0?double(fadeRemaining)/fadeLength:0;
        for(int c=0;c<channels;++c){const double y=blend?wanted[c]+blend*(anchor[c]-wanted[c]):wanted[c];audio[c][n]=float(y);previous[c]=y;}if(fadeRemaining>0)--fadeRemaining;
    }
    postEnd.store(end,std::memory_order_release);
}
DeltaEngine::Alignment DeltaEngine::estimate(const std::vector<double>&pre,const std::vector<double>&post,int maximumDelay){
    const auto count=post.size();if(count<128||maximumDelay<0||pre.size()!=count+std::size_t(maximumDelay))return{};
    std::size_t size=1;while(size<pre.size()+count-1)size<<=1;
    std::vector<std::complex<double>> a(size),b(size);std::vector<double> prefix(pre.size()+1);
    double postEnergy=0;for(std::size_t i=0;i<pre.size();++i){a[i]=pre[i];prefix[i+1]=prefix[i]+pre[i]*pre[i];}
    for(std::size_t i=0;i<count;++i){b[i]=post[count-1-i];postEnergy+=post[i]*post[i];}
    if(postEnergy/count<1e-10)return{};
    fft(a,false);fft(b,false);for(std::size_t i=0;i<size;++i)a[i]*=b[i];fft(a,true);
    double best=0,energy=0;int delay=0;
    for(int lag=0;lag<=maximumDelay;++lag){const auto offset=std::size_t(maximumDelay-lag);const double e=prefix[offset+count]-prefix[offset];if(e/count<1e-10)continue;const double score=a[offset+count-1].real()/std::sqrt(e*postEnergy);if(score>best){best=score;delay=lag;energy=e;}}
    if(best<.55||energy<=0)return{};
    const double gain=std::clamp(std::sqrt(postEnergy/energy),math::gain(-12),math::gain(12));
    return{true,delay,gain,std::clamp(best,0.,1.)};
}
void DeltaEngine::workerLoop(){
    unsigned handled=0,seen=0;std::uint64_t started=0;
    while(!exit.load()){
        std::this_thread::sleep_for(std::chrono::milliseconds(60));const unsigned request=learnRequest.load();if(request==handled)continue;
        learningView=true;
        if(request!=seen){seen=request;started=delta09::nowMs();}if(delta09::nowMs()-started>25000){handled=request;learningView=false;continue;}
        if(!bus||sourceMode.load()){handled=request;learningView=false;continue;}
        const int selected=pairChoice.load();auto&pair=bus->pairs[selected];const auto end=postEnd.load(),owner=postOwner.load(),generation=postRevision.load();
        const auto rate=sampleRate.load();const int lag=int(rate*.5);const auto count=std::size_t(rate*.45);
        if(!owner||end<count+lag||!connected.load())continue;
        std::vector<double>before(count+lag),after(count);bool valid=true;
        for(std::size_t i=0;i<before.size();++i){Stereo x{};if(!read(pair.samples,end-count-lag+i,x)){valid=false;break;}before[i]=x[0]+.61803398875*x[1];}
        for(std::size_t i=0;valid&&i<count;++i){Stereo x{};if(!read(post.get(),end-count+i,x)){valid=false;break;}after[i]=x[0]+.61803398875*x[1];}
        if(!valid||owner!=delta09::wireLoad(&pair.owner)||generation!=postRevision.load())continue;
        const auto result=estimate(before,after,lag);
        if(request!=learnRequest.load()||selected!=pairChoice.load()||generation!=postRevision.load())continue;
        resultSequence.fetch_add(1,std::memory_order_acq_rel);resultDelay=result.delay;resultGain=result.gain;resultConfidence=result.confidence;resultPair=selected;resultRevision=generation;resultOwner=result.valid?owner:0;resultSequence.fetch_add(1,std::memory_order_release);
        handled=request;learningView=false;
    }
}
}
