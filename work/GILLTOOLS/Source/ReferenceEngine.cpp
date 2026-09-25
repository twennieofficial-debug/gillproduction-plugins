#include "ReferenceEngine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace gill::tools {
namespace {
static_assert(std::atomic<double>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free,
              "The real-time monitor requires lock-free 64-bit atomics.");
constexpr double pi = 3.14159265358979323846;
constexpr int framesPerBank = ReferenceEngine::maximumBufferedFrames;
constexpr int decodeFrames = 8192;
constexpr double maximumDuration = 1800.0;
float finiteSample(float value) noexcept { return std::isfinite(value) ? juce::jlimit(-32.0f, 32.0f, value) : 0.0f; }
double safeDb(double value) noexcept { return 20.0 * std::log10(std::max(1.0e-12, value)); }
bool stateNumber(const juce::var& value,double& output){
    if(!(value.isInt()||value.isInt64()||value.isDouble()||value.isString()))return false;
    const auto text=value.toString().trim().toStdString();if(text.empty())return false;char* end=nullptr;
    output=std::strtod(text.c_str(),&end);return end==text.c_str()+text.size()&&std::isfinite(output);
}
bool stateInteger(const juce::var& value,int minimum,int maximum,int& output){
    double number=0;if(!stateNumber(value,number)||number<minimum||number>maximum||std::floor(number)!=number)return false;
    output=static_cast<int>(number);return true;
}
struct Biquad {
    double b0=1, b1=0, b2=0, a1=0, a2=0, z1=0, z2=0;
    void reset() noexcept { z1=z2=0; }
    double tick(double x) noexcept { const double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
    void pass(double rate, double frequency, bool high) noexcept {
        const double w=2*pi*std::min(frequency,rate*.45)/rate, c=std::cos(w), a=std::sin(w)/std::sqrt(2.0), d=1+a;
        b0=(high ? 1+c : 1-c)*.5/d; b1=(high ? -(1+c) : 1-c)/d; b2=b0; a1=-2*c/d; a2=(1-a)/d;
    }
    void weighting(double rate,bool high) noexcept {
        // The same BS.1770 48-kHz K-weighting sections as GILLNEXT/Loudness.h,
        // mapped to the host rate by an inverse/forward bilinear transform.
        // The surrounding frozen-RMS/gate logic is not an integrated-LUFS meter.
        const std::array<double,5> c=high?std::array<double,5>{1,-2,1,-1.99004745483398,.99007225036621}
            :std::array<double,5>{1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,.73248077421585};
        const double a=1-rate/48000,b=1+rate/48000;
        const auto transform=[&](double p,double q,double r){return std::array<double,3>{p*b*b+q*a*b+r*a*a,2*p*a*b+q*(a*a+b*b)+2*r*a*b,p*a*a+q*a*b+r*b*b};};
        const auto n=transform(c[0],c[1],c[2]),d=transform(1,c[3],c[4]);
        b0=n[0]/d[0];b1=n[1]/d[0];b2=n[2]/d[0];a1=d[1]/d[0];a2=d[2]/d[0];
    }
};
struct WeightedMeter {
    Biquad shelf[2], high[2];
    double sum=0; std::uint64_t count=0, minimum=144000;
    void prepare(double rate) noexcept { minimum=static_cast<std::uint64_t>(std::ceil(rate*3)); sum=0; count=0; for(int c=0;c<2;++c){shelf[c].weighting(rate,false);high[c].weighting(rate,true);shelf[c].reset();high[c].reset();} }
    void add(float left,float right) noexcept {
        if(count>=minimum) return;
        const double l=high[0].tick(shelf[0].tick(left)), r=high[1].tick(shelf[1].tick(right));
        const double energy=(l*l+r*r)*.5;
        // Silence does not manufacture a stable match. The threshold is fixed
        // and is not labelled as EBU's relative gate.
        if(energy>1.0e-9 && std::isfinite(energy)){sum+=energy;++count;}
    }
    bool valid()const noexcept{return count>=minimum && sum>0;}
    double rms()const noexcept{return count?std::sqrt(sum/static_cast<double>(count)):0;}
};
}

struct ReferenceEngine::Impl {
    struct Bank {
        // 0 free, 1 worker-writing, 2 published, 3 audio-owned. Storage never
        // moves and only the current owner touches ordinary data/metadata.
        std::atomic<int> role{0};
        std::uint64_t epoch=0;
        std::int64_t start=0;
        std::array<std::array<float,framesPerBank>,2> samples{};
    };
    std::array<Bank,playbackBanks> banks;
    mutable std::mutex modelMutex;
    std::array<SlotInfo,3> slots;
    int selected=0;
    std::atomic<std::uint64_t> generation{1}, loadedGeneration{0}, armedGeneration{0}, meterRevision{1};
    std::atomic<bool> requested{false}, stopping{false};
    std::atomic<double> rate{48000}, duration{0}, loopStart{0}, loopEnd{0};
    std::atomic<std::int64_t> wantedFrame{0};
    std::atomic<float> mixRms{0}, refRms{0}, matchDb{0}, mixDb{0};
    std::atomic<bool> refMeterValid{false}, bothMetersValid{false}, activeDisplay{false};
    std::atomic<double> positionDisplay{0};
    std::atomic<std::uint64_t> underruns{0};
    std::thread worker;
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioBuffer<float> decoded{2,decodeFrames};
    std::int64_t decodedStart=-1;
    int decodedCount=0;
    double workerRate=48000, sourceRate=48000, startSeconds=0, endSeconds=0;
    std::uint64_t workerEpoch=0;
    int radius=12, taps=24;
    std::vector<float> kernels;
    int audioBank=-1;
    std::uint64_t audioEpoch=0, audioMeterRevision=0;
    std::int64_t freeFrame=0;
    float fade=0, mixGain=1, referenceGain=1, lastRef[2]{};
    bool missing=false, previousMatch=true;
    WeightedMeter mixMeter;
    Biquad viewHigh[2],viewLow[2];
    int previousBand=-1;

    Impl(){formats.registerBasicFormats();mixMeter.prepare(48000);worker=std::thread([this]{work();});}
    ~Impl(){stopping.store(true);if(worker.joinable())worker.join();}
    void forceMix() noexcept {armedGeneration.store(0,std::memory_order_release);requested.store(true,std::memory_order_release);}
    void invalidate() {
        forceMix(); loadedGeneration.store(0,std::memory_order_release); refMeterValid.store(false);
        generation.fetch_add(1,std::memory_order_acq_rel); meterRevision.fetch_add(1); wantedFrame.store(0);
    }
    bool current(std::uint64_t epoch)const noexcept{return !stopping.load()&&generation.load(std::memory_order_acquire)==epoch;}
    void setStatus(std::uint64_t epoch,int slot,const juce::String& text,bool loaded=false){
        std::lock_guard<std::mutex> lock(modelMutex);if(current(epoch)&&selected==slot){slots[slot].status=text;slots[slot].loaded=loaded;}
    }
    float readNative(int channel,std::int64_t index){
        index=juce::jlimit<std::int64_t>(0,reader->lengthInSamples-1,index);
        if(index<decodedStart||index>=decodedStart+decodedCount){
            decodedStart=std::max<std::int64_t>(0,index-radius-2);
            decodedCount=static_cast<int>(std::min<std::int64_t>(decodeFrames,reader->lengthInSamples-decodedStart));
            decoded.clear();
            if(!reader->read(&decoded,0,decodedCount,decodedStart,true,true)) throw std::runtime_error("Audio read failed");
            if(reader->numChannels==1)decoded.copyFrom(1,0,decoded,0,0,decodedCount);
        }
        return finiteSample(decoded.getSample(channel,static_cast<int>(index-decodedStart)));
    }
    void makeKernels(){
        const double cutoff=std::min(1.0,workerRate/sourceRate)*.94;
        radius=std::min(128,static_cast<int>(std::ceil(12.0/cutoff))); taps=2*radius;
        kernels.resize(static_cast<std::size_t>(1024*taps));
        for(int phase=0;phase<1024;++phase){
            const double fraction=static_cast<double>(phase)/1024;double sum=0;
            for(int k=0;k<taps;++k){const double x=(k-radius+1)-fraction, z=x*cutoff;
                const double sinc=std::abs(z)<1e-12?1.0:std::sin(pi*z)/(pi*z);
                const double window=.42+.5*std::cos(pi*x/radius)+.08*std::cos(2*pi*x/radius);
                const auto value=static_cast<float>(cutoff*sinc*window);kernels[static_cast<std::size_t>(phase*taps+k)]=value;sum+=value;}
            for(int k=0;k<taps;++k)kernels[static_cast<std::size_t>(phase*taps+k)]/=static_cast<float>(sum);
        }
    }
    void resample(double position,float& left,float& right){
        const auto index=static_cast<std::int64_t>(std::floor(position));
        const int phase=juce::jlimit(0,1023,static_cast<int>((position-index)*1024));
        const auto* kernel=kernels.data()+phase*taps;double l=0,r=0;
        for(int k=0;k<taps;++k){const auto at=index+k-radius+1;l+=readNative(0,at)*kernel[k];r+=readNative(1,at)*kernel[k];}
        left=finiteSample(static_cast<float>(l));right=finiteSample(static_cast<float>(r));
    }
    juce::String fingerprint(const juce::File& file,std::uint64_t epoch){
        auto stream=file.createInputStream();if(!stream||stream->getStatus().failed())throw std::runtime_error("File cannot be read");
        // A content identity for relinking, not a security signature.
        std::uint64_t hash=1469598103934665603ull;std::array<char,16384> bytes{};
        for(;;){if(!current(epoch))return {};const int count=stream->read(bytes.data(),static_cast<int>(bytes.size()));if(count<=0)break;
            for(int i=0;i<count;++i){hash^=static_cast<unsigned char>(bytes[static_cast<std::size_t>(i)]);hash*=1099511628211ull;}}
        if(stream->getStatus().failed())throw std::runtime_error("File read failed");
        return juce::String::toHexString(static_cast<juce::int64>(hash));
    }
    void load(std::uint64_t epoch){
        int slot;SlotInfo requestedSlot;
        {std::lock_guard<std::mutex> lock(modelMutex);slot=selected;requestedSlot=slots[slot];for(auto& s:slots)s.loaded=false;}
        reader.reset();decodedStart=-1;decodedCount=0;workerEpoch=epoch;refRms.store(0);refMeterValid.store(false);
        if(requestedSlot.path.isEmpty()){setStatus(epoch,slot,"EMPTY");return;}
        setStatus(epoch,slot,"LOADING");
        const juce::File file(requestedSlot.path);
        if(!file.hasFileExtension("wav;wave;aif;aiff;flac")){setStatus(epoch,slot,"WAV / AIFF / FLAC ONLY");return;}
        if(!file.existsAsFile()){setStatus(epoch,slot,"FILE MISSING");return;}
        if(file.getSize()>2ll*1024*1024*1024){setStatus(epoch,slot,"FILE TOO LARGE");return;}
        reader.reset(formats.createReaderFor(file));
        if(!reader||reader->lengthInSamples<=0||reader->numChannels<1||reader->numChannels>64||!std::isfinite(reader->sampleRate)
           ||reader->sampleRate<8000||reader->sampleRate>384000){reader.reset();setStatus(epoch,slot,"UNSUPPORTED AUDIO");return;}
        sourceRate=reader->sampleRate;workerRate=rate.load();const double seconds=reader->lengthInSamples/sourceRate;
        if(seconds>maximumDuration){reader.reset();setStatus(epoch,slot,"MAX 30 MINUTES");return;}
        const auto identity=fingerprint(file,epoch);if(!current(epoch))return;
        if(requestedSlot.fingerprint.isNotEmpty()&&identity!=requestedSlot.fingerprint){reader.reset();setStatus(epoch,slot,"FILE CHANGED - RELOAD");return;}
        startSeconds=juce::jlimit(0.0,std::max(0.0,seconds-.02),requestedSlot.loopStartSeconds);
        endSeconds=requestedSlot.loopEndSeconds>startSeconds?std::min(seconds,requestedSlot.loopEndSeconds):seconds;
        if(endSeconds-startSeconds<.001){reader.reset();setStatus(epoch,slot,"AUDIO TOO SHORT");return;}
        makeKernels();
        WeightedMeter referenceMeter;referenceMeter.prepare(sourceRate);
        const auto first=static_cast<std::int64_t>(startSeconds*sourceRate), end=static_cast<std::int64_t>(endSeconds*sourceRate);
        const auto analysisEnd=std::min(end,first+static_cast<std::int64_t>(sourceRate*30));
        for(auto i=first;i<analysisEnd&&!referenceMeter.valid();++i){
            if((i&4095)==0&&!current(epoch))return;
            referenceMeter.add(readNative(0,i),readNative(1,i));
        }
        if(!current(epoch))return;
        {std::lock_guard<std::mutex> lock(modelMutex);if(!current(epoch))return;
            auto& s=slots[slot];s.fileName=file.getFileName();s.fingerprint=identity;s.durationSeconds=seconds;
            s.loopStartSeconds=startSeconds;s.loopEndSeconds=endSeconds;s.status="BUFFERING";s.loaded=false;}
        duration.store(seconds);loopStart.store(startSeconds);loopEnd.store(endSeconds);
        refRms.store(static_cast<float>(referenceMeter.rms()));refMeterValid.store(referenceMeter.valid());
        loadedGeneration.store(epoch,std::memory_order_release);
    }
    void fill(Bank& bank,std::int64_t first,std::uint64_t epoch){
        const auto length=std::max<std::int64_t>(1,static_cast<std::int64_t>(std::llround((endSeconds-startSeconds)*workerRate)));
        const auto edge=std::min<std::int64_t>(static_cast<std::int64_t>(workerRate*.003),length/4);
        bank.start=first;bank.epoch=epoch;
        for(int i=0;i<framesPerBank;++i){
            if((i&1023)==0&&!current(epoch)){bank.role.store(0,std::memory_order_release);return;}
            const auto phase=(first+i)%length;float l=0,r=0;
            resample(startSeconds*sourceRate+phase*sourceRate/workerRate,l,r);
            // A short seam taper avoids a discontinuity when looping arbitrary
            // cropped audio. Loop length/host alignment do not drift.
            double gain=1;
            if(edge>0){const auto distance=std::min(phase,length-1-phase);if(distance<edge)gain=.5-.5*std::cos(pi*distance/edge);}
            bank.samples[0][static_cast<std::size_t>(i)]=l*static_cast<float>(gain);
            bank.samples[1][static_cast<std::size_t>(i)]=r*static_cast<float>(gain);
        }
        bank.role.store(2,std::memory_order_release);
    }
    void work() noexcept {
        while(!stopping.load()){
            try{
                const auto epoch=generation.load(std::memory_order_acquire);
                if(workerEpoch!=epoch)load(epoch);
                if(reader&&loadedGeneration.load()==epoch&&current(epoch)){
                    const auto position=std::max<std::int64_t>(0,wantedFrame.load());
                    const auto aligned=(position/framesPerBank)*framesPerBank;
                    bool filled=false;
                    for(int ahead=0;ahead<playbackBanks&&!filled;++ahead){
                        const auto wanted=aligned+static_cast<std::int64_t>(ahead)*framesPerBank;
                        bool have=false;
                        for(auto& bank:banks){const int role=bank.role.load(std::memory_order_acquire);
                            if((role==2||role==3)&&bank.epoch==epoch&&bank.start==wanted){have=true;break;}}
                        if(have)continue;
                        for(auto& bank:banks){int expected=bank.role.load(std::memory_order_acquire);
                            if(expected==3||expected==1)continue;
                            if(expected==2&&bank.epoch==epoch&&bank.start>=aligned&&bank.start<aligned+static_cast<std::int64_t>(playbackBanks)*framesPerBank)continue;
                            if(!bank.role.compare_exchange_strong(expected,1,std::memory_order_acq_rel))continue;
                            try{fill(bank,wanted,epoch);}catch(...){bank.role.store(0,std::memory_order_release);throw;}
                            filled=true;break;}
                    }
                    if(filled){std::lock_guard<std::mutex> lock(modelMutex);if(current(epoch)){slots[selected].status="READY";slots[selected].loaded=true;}}
                }
            }catch(const std::exception&){loadedGeneration.store(0);reader.reset();int slot;{std::lock_guard<std::mutex> lock(modelMutex);slot=selected;}setStatus(workerEpoch,slot,"FILE READ ERROR");}
            catch(...){loadedGeneration.store(0);reader.reset();}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void releaseAudioBank()noexcept{if(audioBank>=0){banks[static_cast<std::size_t>(audioBank)].role.store(0,std::memory_order_release);audioBank=-1;}}
    bool sample(std::int64_t frame,std::uint64_t epoch,float& left,float& right)noexcept{
        if(audioBank>=0){auto& b=banks[static_cast<std::size_t>(audioBank)];if(b.epoch!=epoch||frame<b.start||frame>=b.start+framesPerBank)releaseAudioBank();}
        if(audioBank<0){for(int i=0;i<playbackBanks;++i){auto& b=banks[static_cast<std::size_t>(i)];int expected=2;
            if(!b.role.compare_exchange_strong(expected,3,std::memory_order_acq_rel))continue;
            if(b.epoch==epoch&&frame>=b.start&&frame<b.start+framesPerBank){audioBank=i;break;}
            b.role.store(2,std::memory_order_release);}}
        if(audioBank<0)return false;
        auto& b=banks[static_cast<std::size_t>(audioBank)];const auto offset=static_cast<std::size_t>(frame-b.start);
        left=b.samples[0][offset];right=b.samples[1][offset];return true;
    }
};

ReferenceEngine::ReferenceEngine():impl(std::make_unique<Impl>()){}
ReferenceEngine::~ReferenceEngine()=default;
void ReferenceEngine::prepare(double sampleRate,int maximumBlockSize){
    juce::ignoreUnused(maximumBlockSize);
    if(!std::isfinite(sampleRate)||sampleRate<8000||sampleRate>192000)sampleRate=48000;
    impl->rate.store(sampleRate);{std::lock_guard<std::mutex> lock(impl->modelMutex);impl->invalidate();}reset();
}
void ReferenceEngine::requestLoad(int slot,const juce::File& file){
    if(slot<0||slot>=3)return;std::lock_guard<std::mutex> lock(impl->modelMutex);
    impl->slots[slot]=SlotInfo{};impl->slots[slot].path=file.getFullPathName();impl->slots[slot].fileName=file.getFileName();
    impl->slots[slot].status="LOADING";impl->selected=slot;impl->invalidate();
}
void ReferenceEngine::selectSlot(int slot){if(slot<0||slot>=3)return;std::lock_guard<std::mutex> lock(impl->modelMutex);if(impl->selected!=slot){impl->selected=slot;impl->invalidate();}}
void ReferenceEngine::clearSlot(int slot){if(slot<0||slot>=3)return;std::lock_guard<std::mutex> lock(impl->modelMutex);impl->slots[slot]=SlotInfo{};if(impl->selected==slot)impl->invalidate();}
void ReferenceEngine::setLoop(double start,double end){
    if(!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>maximumDuration)return;
    std::lock_guard<std::mutex> lock(impl->modelMutex);auto& s=impl->slots[impl->selected];
    s.loopStartSeconds=start;s.loopEndSeconds=end;impl->invalidate();
}
void ReferenceEngine::setReferenceEnabled(bool enabled)noexcept{
    // Capture before the request edge: an invalidation that races this call
    // either consumes that edge or makes its captured generation obsolete.
    const auto epoch=impl->generation.load(std::memory_order_acquire);
    const bool previous=impl->requested.exchange(enabled,std::memory_order_acq_rel);
    if(!enabled)impl->armedGeneration.store(0,std::memory_order_release);
    else if(!previous)impl->armedGeneration.store(epoch,std::memory_order_release);
}
void ReferenceEngine::reset()noexcept{
    impl->forceMix();impl->releaseAudioBank();impl->fade=0;impl->freeFrame=0;impl->wantedFrame.store(0);impl->missing=false;
    impl->mixGain=impl->referenceGain=1;impl->lastRef[0]=impl->lastRef[1]=0;impl->mixMeter.prepare(impl->rate.load());
    impl->mixRms.store(0);impl->bothMetersValid.store(false);impl->activeDisplay.store(false);impl->previousBand=-1;
}
void ReferenceEngine::process(juce::AudioBuffer<float>& audio,const Transport& transport,const Parameters& parameters)noexcept{
    auto& s=*impl;const int channels=audio.getNumChannels(),count=audio.getNumSamples();if(channels<1||count<1)return;
    const double fs=s.rate.load();const auto epoch=s.generation.load(std::memory_order_acquire);
    const auto revision=s.meterRevision.load();
    if(s.audioEpoch!=epoch||s.audioMeterRevision!=revision){s.releaseAudioBank();s.audioEpoch=epoch;s.audioMeterRevision=revision;s.mixMeter.prepare(fs);s.bothMetersValid.store(false);}
    if(transport.discontinuity)s.releaseAudioBank();
    if(parameters.match&&!s.previousMatch){s.mixMeter.prepare(fs);s.bothMetersValid.store(false);}
    s.previousMatch=parameters.match;
    auto frame=parameters.follow&&transport.hasPosition?std::max<std::int64_t>(0,transport.positionSamples):s.freeFrame;
    frame=std::min<std::int64_t>(frame,std::numeric_limits<std::int64_t>::max()-count-framesPerBank*4ll);
    s.wantedFrame.store(frame,std::memory_order_release);
    const bool loaded=s.loadedGeneration.load(std::memory_order_acquire)==epoch;
    const bool requested=s.armedGeneration.load(std::memory_order_acquire)==epoch;
    const bool bypass=parameters.bypass||transport.offline;
    if(transport.offline){s.fade=0;s.mixGain=1;s.referenceGain=1;s.lastRef[0]=s.lastRef[1]=0;}
    const bool wantRef=requested&&loaded&&transport.playing&&!bypass;
    const int band=juce::jlimit(0,3,parameters.listenBand);
    if(s.previousBand!=band){s.previousBand=band;for(int c=0;c<2;++c){s.viewHigh[c].pass(fs,band==3?7000:180,true);s.viewLow[c].pass(fs,band==2?180:6000,false);s.viewHigh[c].reset();s.viewLow[c].reset();}}
    const float trim=std::isfinite(parameters.trimDb)?juce::jlimit(-6.0f,6.0f,parameters.trimDb):0;
    const float trimGain=std::pow(10.0f,trim/20);
    const float step=static_cast<float>(1.0/std::max(1.0,fs*.012));
    const float smooth=static_cast<float>(1-std::exp(-1/(fs*.03)));
    float* left=audio.getWritePointer(0);float* right=channels>1?audio.getWritePointer(1):nullptr;
    for(int i=0;i<count;++i){
        float l=finiteSample(left[i]),r=right?finiteSample(right[i]):l;
        s.mixMeter.add(l,r);
        const bool matchReady=loaded&&s.refMeterValid.load()&&s.mixMeter.valid();
        float targetMix=1,targetRef=trimGain;
        if(parameters.match&&matchReady&&!bypass){const double m=s.mixMeter.rms(),f=s.refRms.load();const double common=std::min(m,f);
            targetMix=static_cast<float>(common/std::max(m,1e-12));targetRef=trimGain*static_cast<float>(common/std::max(f,1e-12));}
        if(bypass){targetMix=1;targetRef=1;}
        s.mixGain+=(targetMix-s.mixGain)*smooth;s.referenceGain+=(targetRef-s.referenceGain)*smooth;
        if(std::abs(s.mixGain-1)<1e-6f&&targetMix==1)s.mixGain=1;
        float rl=0,rr=0;bool available=false;
        if(wantRef)available=s.sample(frame+i,epoch,rl,rr);
        if(available){s.lastRef[0]=rl;s.lastRef[1]=rr;}
        else{rl=s.lastRef[0]*.997f;rr=s.lastRef[1]*.997f;s.lastRef[0]=rl;s.lastRef[1]=rr;}
        if(wantRef&&!available){if(!s.missing){s.underruns.fetch_add(1);s.missing=true;}}else s.missing=false;
        const float target=available&&wantRef?1.0f:0.0f;s.fade+=juce::jlimit(-step,step,target-s.fade);
        const float weight=s.fade==0?0:s.fade==1?1:static_cast<float>(.5-.5*std::cos(pi*s.fade));
        if(weight!=0||s.mixGain!=1){l=l*s.mixGain*(1-weight)+rl*s.referenceGain*weight;r=r*s.mixGain*(1-weight)+rr*s.referenceGain*weight;}
        if(!bypass){
            const float mid=(l+r)*.5f,side=(l-r)*.5f;
            if(parameters.channelView==1||parameters.mono){l=mid;r=mid;}
            else if(parameters.channelView==2){l=side;r=-side;}
            if(band==1){l=static_cast<float>(s.viewLow[0].tick(s.viewHigh[0].tick(l)));r=static_cast<float>(s.viewLow[1].tick(s.viewHigh[1].tick(r)));}
            else if(band==2){l=static_cast<float>(s.viewLow[0].tick(l));r=static_cast<float>(s.viewLow[1].tick(r));}
            else if(band==3){l=static_cast<float>(s.viewHigh[0].tick(l));r=static_cast<float>(s.viewHigh[1].tick(r));}
        }
        left[i]=finiteSample(l);if(right)right[i]=finiteSample(r);
    }
    if(transport.playing)s.freeFrame=frame+count;
    s.wantedFrame.store(frame+(transport.playing?count:0),std::memory_order_release);
    s.mixRms.store(static_cast<float>(s.mixMeter.rms()));s.bothMetersValid.store(loaded&&s.refMeterValid.load()&&s.mixMeter.valid());
    s.matchDb.store(static_cast<float>(safeDb(s.referenceGain)));s.mixDb.store(static_cast<float>(safeDb(s.mixGain)));
    s.activeDisplay.store(s.fade>0);
    const double length=s.loopEnd.load()-s.loopStart.load();s.positionDisplay.store(length>0?s.loopStart.load()+std::fmod(static_cast<double>(frame)/fs,length):0);
}
ReferenceEngine::Snapshot ReferenceEngine::snapshot()const{
    Snapshot result;{std::lock_guard<std::mutex> lock(impl->modelMutex);result.slots=impl->slots;result.activeSlot=impl->selected;const auto& s=impl->slots[impl->selected];
        result.fileName=s.fileName;result.status=s.status;result.loaded=s.loaded;result.durationSeconds=s.durationSeconds;result.loopStartSeconds=s.loopStartSeconds;result.loopEndSeconds=s.loopEndSeconds;}
    result.positionSeconds=impl->positionDisplay.load();result.rmsMix=impl->mixRms.load();result.rmsRef=impl->refRms.load();result.matchGainDb=impl->matchDb.load();result.mixGainDb=impl->mixDb.load();
    result.loudnessValid=impl->bothMetersValid.load();result.referenceActive=impl->activeDisplay.load();result.underruns=impl->underruns.load();return result;
}
juce::ValueTree ReferenceEngine::getState()const{
    std::lock_guard<std::mutex> lock(impl->modelMutex);juce::ValueTree tree("GILLREFERENCE_ENGINE");tree.setProperty("schema",1,nullptr);tree.setProperty("slot",impl->selected,nullptr);
    for(int i=0;i<3;++i){const auto& s=impl->slots[i];juce::ValueTree child("SLOT");child.setProperty("index",i,nullptr);child.setProperty("path",s.path,nullptr);child.setProperty("fingerprint",s.fingerprint,nullptr);
        child.setProperty("start",s.loopStartSeconds,nullptr);child.setProperty("end",s.loopEndSeconds,nullptr);tree.addChild(child,-1,nullptr);}return tree;
}
void ReferenceEngine::setState(const juce::ValueTree& tree){
    impl->forceMix();int schema=0,active=-1;
    if(!tree.hasType("GILLREFERENCE_ENGINE")||!stateInteger(tree.getProperty("schema"),1,1,schema)||tree.getNumChildren()!=3
       ||!stateInteger(tree.getProperty("slot"),0,2,active))return;
    std::array<SlotInfo,3> slots;bool seen[3]{};
    for(int i=0;i<3;++i){const auto child=tree.getChild(i);int index=-1;
        if(!child.hasType("SLOT")||!stateInteger(child.getProperty("index"),0,2,index)||seen[index])return;seen[index]=true;
        auto& s=slots[index];s.path=child.getProperty("path").toString();s.fingerprint=child.getProperty("fingerprint").toString();
        if(!stateNumber(child.getProperty("start",0),s.loopStartSeconds)||!stateNumber(child.getProperty("end",0),s.loopEndSeconds))return;
        if(s.path.length()>8192||s.fingerprint.length()>64
           ||s.loopStartSeconds<0||s.loopEndSeconds<0||s.loopEndSeconds>maximumDuration||(s.loopEndSeconds>0&&s.loopEndSeconds<=s.loopStartSeconds))return;
        if(s.path.isNotEmpty()){if(!juce::File::isAbsolutePath(s.path))return;s.fileName=juce::File(s.path).getFileName();s.status="LOADING";}}
    std::lock_guard<std::mutex> lock(impl->modelMutex);impl->slots=std::move(slots);impl->selected=active;impl->invalidate();
}

} // namespace gill::tools
