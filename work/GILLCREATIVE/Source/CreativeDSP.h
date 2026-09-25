#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace gill::creative {
static_assert(std::atomic<float>::is_always_lock_free&&std::atomic<double>::is_always_lock_free,"64-bit GILL audio requires lock-free numeric atomics");
constexpr double pi=3.14159265358979323846;
constexpr int maxMarkers=32, wavePoints=128, maxFrames=6000;
inline float finite(float x) noexcept{return std::isfinite(x)?std::clamp(x,-16.f,16.f):0.f;}
inline float db(float x) noexcept{return 20.f*std::log10(std::max(1.e-9f,x));}
enum class Kind{Phrase,Director,Reply};
struct Marker{float startBeat=0,endBeat=0,startSec=0,endSec=0,strength=.5f;};
struct Plan{
    int count=0,capture=-1;unsigned captureEpoch=0;bool commit=false;float durationBeats=0,durationSeconds=0;double originPPQ=0;float bpm=120;
    std::array<Marker,maxMarkers> markers{};std::array<float,wavePoints> waveform{};
    bool valid()const noexcept{return count>0&&count<=maxMarkers&&std::isfinite(originPPQ)&&durationBeats>0&&durationBeats<2000&&durationSeconds>0&&durationSeconds<=60.1f;}
};
// Atomic fields avoid C++ data races even when the bounded seqlock read retries.
// There is exactly one producer for each mailbox. UI/state producers are
// serialized outside processBlock; the audio consumer never waits or locks.
class PlanMailbox{
public:
    void store(const Plan&p)noexcept{
        version.fetch_add(1,std::memory_order_acq_rel);count=p.count;capture=p.capture;captureEpoch=p.captureEpoch;commit=p.commit;origin=p.originPPQ;
        duration=p.durationBeats;seconds=p.durationSeconds;bpm=p.bpm;
        for(int i=0;i<maxMarkers;++i){const auto&m=p.markers[i];fields[i*5]=m.startBeat;fields[i*5+1]=m.endBeat;fields[i*5+2]=m.startSec;fields[i*5+3]=m.endSec;fields[i*5+4]=m.strength;}
        for(int i=0;i<wavePoints;++i)wave[i]=p.waveform[i];version.fetch_add(1,std::memory_order_release);
    }
    unsigned serial()const noexcept{return version.load(std::memory_order_acquire);}
    bool read(Plan&p,unsigned*revision=nullptr)const noexcept{
        for(int attempt=0;attempt<3;++attempt){const auto before=version.load(std::memory_order_acquire);if(before&1)continue;
            Plan q;q.count=count.load();q.capture=capture.load();q.captureEpoch=captureEpoch.load();q.commit=commit.load();q.originPPQ=origin.load();q.durationBeats=duration.load();q.durationSeconds=seconds.load();q.bpm=bpm.load();
            for(int i=0;i<maxMarkers;++i)q.markers[i]={fields[i*5].load(),fields[i*5+1].load(),fields[i*5+2].load(),fields[i*5+3].load(),fields[i*5+4].load()};
            for(int i=0;i<wavePoints;++i)q.waveform[i]=wave[i].load();
            if(before==version.load(std::memory_order_acquire)){p=q;if(revision)*revision=before;return true;}
        }return false;
    }
private:
    std::atomic<unsigned>version{0},captureEpoch{0};std::atomic<bool>commit{false};std::atomic<int>count{0},capture{-1};std::atomic<double>origin{0};
    std::atomic<float>duration{0},seconds{0},bpm{120};std::array<std::atomic<float>,maxMarkers*5>fields{};std::array<std::atomic<float>,wavePoints>wave{};
};
struct Controls{float amount=.55f,length=1.6f,tone=.55f,width=1,mix=.25f,dry=1,sensitivity=-40;int variant=0;bool pro=true,bypass=false;};
struct Transport{double ppq=0,bpm=120;bool hasPPQ=false,playing=true;};

class Reverb{
    std::array<std::vector<float>,8> lines;std::array<int,8>pos{};std::array<float,8>lp{},feedback{};
    double fs=48000;float hp=0,lastLength=-1,hpCoefficient=.01f,proBlend=0;
public:
    void prepare(double rate){fs=rate;lastLength=-1;hpCoefficient=static_cast<float>(1-std::exp(-2*pi*90/fs));const double times[]{.0297,.0371,.0411,.0437,.0533,.0617,.0719,.0793};for(int i=0;i<8;++i)lines[i].assign(static_cast<size_t>(std::ceil(times[i]*fs)),0);reset();}
    void reset()noexcept{for(auto&line:lines)std::fill(line.begin(),line.end(),0.f);pos={};lp={};hp=0;proBlend=0;}
    std::array<float,2> process(float input,float length,float tone,bool pro)noexcept{
        constexpr int n=8;const float damp=.05f+.7f*tone;float sum=0,second=0;std::array<float,8>r{};
        if(length!=lastLength){lastLength=length;for(int i=0;i<8;++i)feedback[i]=std::pow(.001f,static_cast<float>(lines[i].size()/fs)/std::max(.15f,length));}
        for(int i=0;i<n;++i){r[i]=lines[i][static_cast<size_t>(pos[i])];lp[i]+=damp*(r[i]-lp[i]);if(i<4)sum+=lp[i];else second+=lp[i];}
        hp+=hpCoefficient*(input-hp);const float excitation=(input-hp)*.35f;
        for(int i=0;i<n;++i){lines[i][static_cast<size_t>(pos[i])]=finite(excitation+(.5f*(i<4?sum:second)-lp[i])*feedback[i]);if(++pos[i]>=static_cast<int>(lines[i].size()))pos[i]=0;}
        proBlend+=static_cast<float>(1/(fs*.02))*((pro?1.f:0.f)-proBlend);
        return{(r[0]+r[2]-r[1])*.5f+(r[4]-r[6])*.25f*proBlend,(r[1]+r[3]-r[0])*.5f+(r[5]-r[7])*.25f*proBlend};
    }
};

class Engine{
    struct AntiAlias{
        double b0=1,b1=0,b2=0,a1=0,a2=0,s1=0,s2=0;
        void prepare(double rate,double cutoff,double q){const double w=2*pi*cutoff/rate,c=std::cos(w),alpha=std::sin(w)/(2*q),a0=1+alpha;b0=(1-c)*.5/a0;b1=(1-c)/a0;b2=b0;a1=-2*c/a0;a2=(1-alpha)/a0;s1=s2=0;}
        float process(float x)noexcept{const double y=b0*x+s1;s1=b1*x-a1*y+s2;s2=b2*x-a2*y;return static_cast<float>(y);}
    };
public:
    explicit Engine(Kind type):kind(type){}
    void prepare(double rate){
        fs=std::isfinite(rate)&&rate>=8000&&rate<=384000?rate:48000;captureRate=std::min(48000.,fs);frameLength=std::max(32,static_cast<int>(fs*.01));
        constexpr double q[]{.509795579,.601344886,.899976223,2.56291545};for(auto&channel:antiAlias)for(int i=0;i<4;++i)channel[i].prepare(fs,std::min(20000.,fs*.4),q[i]);
        if(kind==Kind::Reply)for(auto&b:banks)if(!b.data){b.capacity=48000*60;b.data=std::make_unique<std::atomic<float>[]>(static_cast<size_t>(b.capacity)*2);b.role=0;b.used=0;b.rate=captureRate;}
        delay[0].assign(static_cast<size_t>(fs*2.1)+4,0);delay[1].assign(delay[0].size(),0);reverb.prepare(fs);resetAudio();
        if(learning)abortLearn();if(!prepared){state=0;progress=0;candidate=Plan{};active=Plan{};undo=Plan{};published.store(candidate);applied=false;captureBank=-1;activeBank=-1;command=0;seenIncoming=0;prepared=true;}
        totalSamples=0;freePPQ=0;lastHostExpected=0;hadHost=false;wasPlaying=false;
    }
    void resetAudio()noexcept{reverb.reset();for(auto&d:delay)std::fill(d.begin(),d.end(),0.f);delayPos=0;voices={};detectorLP=0;detectorPrev=0;envelope=0;lowL=lowR=0;wetSmooth=0;lastBeat=-1.e9;}
    void request(int c)noexcept{command.store(c,std::memory_order_release);}
    // Called only on a non-audio producer thread.
    void postPlan(const Plan&p,bool shouldCommit){Plan old;if(incoming.read(old)&&old.capture>=0&&old.capture<3&&old.capture!=p.capture&&banks[old.capture].epoch.load()==old.captureEpoch){int expected=4;banks[old.capture].role.compare_exchange_strong(expected,0);}auto next=p;next.commit=shouldCommit;incoming.store(next);}
    Plan snapshot()const noexcept{Plan p;published.read(p);return p;}
    Plan savedPlan()const noexcept{Plan p;if(applied.load())publishedActive.read(p);else published.read(p);return p;}
    int learningState()const noexcept{return state.load();}
    bool isApplied()const noexcept{return applied.load();}
    bool isPreviewing()const noexcept{return previewDisplay.load();}
    float learningProgress()const noexcept{return progress.load();}
    float duration()const noexcept{return capturedSeconds.load();}
    float currentPosition()const noexcept{return playPosition.load();}
    float confidence()const noexcept{return vocalConfidence.load();}
    bool hasSidechain()const noexcept{return sideSeen.load();}
    // 0=start, 1=audio-owned, 2=ready, 3=active, 4=state-import producer.
    struct CaptureBank{std::unique_ptr<std::atomic<float>[]>data;int capacity=0;std::atomic<int>role{0},used{0};std::atomic<unsigned>epoch{0};std::atomic<double>rate{48000};};
    const CaptureBank* capture(int index)const noexcept{return index>=0&&index<3?&banks[static_cast<size_t>(index)]:nullptr;}
    int importCapture(const float*interleaved,int frames,double rate){
        if(kind!=Kind::Reply||frames<=0||!std::isfinite(rate)||rate<8000||rate>48000)return-1;
        for(int pass=0;pass<2;++pass)for(int i=0;i<3;++i){auto&b=banks[i];int expected=pass==0?0:2;if(frames<=b.capacity&&b.role.compare_exchange_strong(expected,4)){
            b.epoch.fetch_add(1);for(int n=0;n<frames*2;++n)b.data[n].store(finite(interleaved[n]),std::memory_order_relaxed);b.used=frames;b.rate=rate;return i;}}
        return-1;
    }
    void process(float*left,float*right,const float*sideL,const float*sideR,int count,const Controls&c,const Transport&t)noexcept{
        if(count<=0||!left)return;
        const double bpm=std::isfinite(t.bpm)?std::clamp(t.bpm,20.,400.):120.,step=bpm/(60*fs);
        double blockBeat=t.hasPPQ&&std::isfinite(t.ppq)?t.ppq:freePPQ;
        const bool jumped=t.hasPPQ&&hadHost&&(std::abs(blockBeat-lastHostExpected)>.04||t.playing!=wasPlaying);
        if(jumped){resetAudio();if(learning){abortLearn();state=4;}}
        hadHost=t.hasPPQ;wasPlaying=t.playing;lastHostExpected=blockBeat+(t.playing?count*step:0);
        const int cmd=command.exchange(0,std::memory_order_acq_rel);
        if(cmd==1)startLearn(blockBeat,bpm);else if(cmd==2)finishLearn();else if(cmd==3)applyCandidate();else if(cmd==4)undoApply();
        else if(cmd==5){preview=validPlan(candidate);previewBeat=candidate.originPPQ;resetAudio();}else if(cmd==6){preview=false;resetAudio();}
        else if(cmd==7){abortLearn();active=Plan{};candidate=Plan{};undo=Plan{};applied=false;activeBank=-1;for(auto&bank:banks){int previous=bank.role.load();if(previous!=4)bank.role.compare_exchange_strong(previous,0);}published.store(candidate);publishedActive.store(active);seenIncoming=incoming.serial();resetAudio();}
        const auto revision=incoming.serial();
        if(revision!=seenIncoming){Plan p;unsigned readRevision=0;if(incoming.read(p,&readRevision)&&validPlan(p)){seenIncoming=readRevision;bool available=kind!=Kind::Reply;if(p.capture>=0&&p.capture<3&&banks[p.capture].epoch.load()==p.captureEpoch){auto&bank=banks[p.capture];int expected=4;const bool claimed=bank.role.compare_exchange_strong(expected,2);if(bank.epoch.load()!=p.captureEpoch){if(claimed){expected=2;bank.role.compare_exchange_strong(expected,4);}}else{const int role=bank.role.load();available=role==2||role==3;}}if(available){if(learning)abortLearn();candidate=p;published.store(candidate);state=2;if(p.commit)applyCandidate();}}}
        const auto&plan=preview?candidate:active;
        if(preview){blockBeat=previewBeat;}else if(!t.hasPPQ&&!applied.load()&&candidate.valid())freePPQ=candidate.originPPQ;
        // A seek/apply starts at the new position; never fire every past reply
        // simultaneously merely because the trigger cursor was reset.
        if(lastBeat<-1.e8)lastBeat=blockBeat-step*.5;
        float maximum=0,outputMaximum=0;const float follow=static_cast<float>(1-std::exp(-1/(fs*.012)));
        const float threshold=std::pow(10.f,c.sensitivity/20.f);const float lowCoefficient=static_cast<float>(1-std::exp(-2*pi*1500/fs));
        for(int n=0;n<count;++n){
            const float l=finite(left[n]),r=right?finite(right[n]):l,mono=.5f*(l+r);maximum=std::max(maximum,std::max(std::abs(l),std::abs(r)));
            const double beat=blockBeat+n*step;detectorLP+=lowCoefficient*(mono-detectorLP);envelope+=follow*(std::abs(mono)-envelope);
            const float side=sideL?.5f*(std::abs(finite(sideL[n]))+std::abs(finite(sideR?sideR[n]:sideL[n]))):0;
            if(learning)analyse(l,r,mono,side,beat,bpm,threshold);
            const float voice=std::clamp((std::abs(detectorLP)+envelope*.4f)/(envelope+1.e-6f),0.f,1.f);
            float local=.35f;int markerIndex=-1;
            if(plan.valid()){local=0;const double rel=beat-plan.originPPQ;
                for(int i=0;i<plan.count;++i){const auto&m=plan.markers[i];if(rel>=m.startBeat&&rel<=m.endBeat){local=m.strength;markerIndex=i;}}
                playPosition=static_cast<float>(std::clamp(rel/std::max(.01f,plan.durationBeats),0.,1.));
            }
            float wl=0,wr=0;
            if(kind==Kind::Phrase){
                float send=voice*.08f;if(plan.valid()){send=0;const double rel=beat-plan.originPPQ;
                    const float window=.22f+.55f*c.amount;for(int i=0;i<plan.count;++i){const auto&m=plan.markers[i];if(rel>=m.endBeat-window&&rel<=m.endBeat+.06f){const float ramp=std::clamp(static_cast<float>((rel-(m.endBeat-window))/window),0.f,1.f);send=std::max(send,m.strength*(.25f+.75f*ramp)*voice);}}}
                const float softness=c.pro?std::clamp((voice-.25f)/.75f,0.f,1.f):voice;auto wet=reverb.process(mono*send*softness*(.5f+2*c.amount)*(c.variant==2?1.15f:1),c.length*(c.variant==1?1.3f:c.variant==2?.72f:1),c.tone,c.pro);wl=wet[0];wr=wet[1];
            }else if(kind==Kind::Director){
                float activity=plan.valid()?local:.55f;
                if(c.variant==1&&plan.valid())activity=std::clamp(.2f+.45f*activity+.4f*static_cast<float>(std::clamp((beat-plan.originPPQ)/plan.durationBeats,0.,1.)),0.f,1.f);
                else if(c.variant==2)activity*=.55f+.45f*static_cast<float>(.5+.5*std::sin(beat*pi*.5));
                const float intensity=c.amount*(.2f+.8f*activity);
                const float envDb=db(envelope);const float over=std::max(0.f,envDb-(-24+intensity*7));const float compression=std::pow(10.f,-over*(intensity*.55f)/20.f);
                const float eqCoefficient=static_cast<float>(1-std::exp(-2*pi*850/fs));lowL+=eqCoefficient*(l-lowL);lowR+=eqCoefficient*(r-lowR);
                const float tilt=(c.tone-.5f)*intensity*.9f;float a=(l+(l-lowL)*tilt-lowL*tilt*.4f)*compression,b=(r+(r-lowR)*tilt-lowR*tilt*.4f)*compression;
                const float mid=.5f*(a+b),sideAudio=.5f*(a-b)*(1+(c.width-1)*intensity);a=mid+sideAudio;b=mid-sideAudio;
                const float gap=plan.valid()?(markerIndex<0?1.f:.2f):.3f;auto wet=reverb.process(.5f*(a+b)*(.1f+.25f*intensity),c.length,c.tone,c.pro);
                const double div=c.variant==2?1./3:(c.variant==1?.75:.5);const int delaySamples=std::clamp(static_cast<int>(fs*60/bpm*div),1,static_cast<int>(delay[0].size())-1);
                int read=delayPos-delaySamples;if(read<0)read+=static_cast<int>(delay[0].size());const float dl=delay[0][read],dr=delay[1][read];
                delay[0][delayPos]=finite(a*.17f*intensity+dr*.28f);delay[1][delayPos]=finite(b*.17f*intensity+dl*.28f);
                wl=a+wet[0]*(.25f+gap*.55f)+dl*gap;wr=b+wet[1]*(.25f+gap*.55f)+dr*gap;
                if(++delayPos>=static_cast<int>(delay[0].size()))delayPos=0;
            }else{
                const bool running=preview||!t.hasPPQ||t.playing;
                if(plan.valid()&&plan.capture>=0&&plan.capture<3&&banks[plan.capture].epoch.load()==plan.captureEpoch&&running){const double rel=beat-plan.originPPQ;
                    for(int i=0;i<plan.count;++i){const auto&m=plan.markers[i];if((i*37+17)%100>=static_cast<int>(c.amount*100))continue;
                        const double grid=c.variant==2?1./3:.25,first=std::ceil((m.endBeat+.08)/grid)*grid;
                        for(int repeat=0;repeat<=c.variant;++repeat){const double at=plan.originPPQ+first+repeat*grid;if(lastBeat<at&&beat>=at){
                            const float seconds=std::min(c.length,std::max(.025f,m.endSec-m.startSec));const float from=std::max(m.startSec,m.endSec-seconds);
                            const double untilNext=i+1<plan.count?plan.markers[i+1].startBeat:plan.durationBeats+4;
                            if(first+repeat*grid+seconds*bpm/60<untilNext+.02)trigger(plan.capture,plan.captureEpoch,from,m.endSec,m.strength,repeat,c);}}
                    }
                }
                for(auto&v:voices)if(v.active){auto&bank=banks[v.bank];const int role=bank.role.load();const int index=static_cast<int>(v.position),limit=bank.used.load(std::memory_order_relaxed);if((role!=2&&role!=3)||v.epoch!=bank.epoch.load()||index+1>=limit||v.position>=v.end){v.active=false;continue;}
                    const float fraction=static_cast<float>(v.position-index),fade=std::min(1.f,std::min(static_cast<float>((v.position-v.begin)/std::max(1.,v.rate*(c.pro?.008:.004))),static_cast<float>((v.end-v.position)/std::max(1.,v.rate*(c.pro?.018:.008)))));
                    const float a=bank.data[index*2].load(std::memory_order_relaxed)*(1-fraction)+bank.data[(index+1)*2].load(std::memory_order_relaxed)*fraction;
                    const float b=bank.data[index*2+1].load(std::memory_order_relaxed)*(1-fraction)+bank.data[(index+1)*2+1].load(std::memory_order_relaxed)*fraction;
                    if(v.epoch!=bank.epoch.load()){v.active=false;continue;}
                    const float gain=v.gain*std::max(0.f,fade)/(1+envelope*8);wl+=a*gain*(1-v.pan*.4f);wr+=b*gain*(1+v.pan*.4f);v.position+=v.rate/fs;
                }
                const float toneCoefficient=static_cast<float>(1-std::exp(-2*pi*(1200+c.tone*10000)/fs));lowL+=toneCoefficient*(wl-lowL);lowR+=toneCoefficient*(wr-lowR);wl=lowL;wr=lowR;
            }
            const float wm=.5f*(wl+wr),ws=.5f*(wl-wr)*c.width;wl=wm+ws;wr=wm-ws;
            wetSmooth+=follow*(c.mix-wetSmooth);if(std::abs(c.mix-wetSmooth)<1.e-4f)wetSmooth=c.mix;
            drySmooth+=follow*(c.dry-drySmooth);if(std::abs(c.dry-drySmooth)<1.e-4f)drySmooth=c.dry;
            const float bypassTarget=c.bypass?1.f:0.f;bypassSmooth+=follow*(bypassTarget-bypassSmooth);if(std::abs(bypassTarget-bypassSmooth)<1.e-4f)bypassSmooth=bypassTarget;
            const float direct=kind==Kind::Director?(1-wetSmooth)*drySmooth:drySmooth;
            float outL=finite((l*direct+wl*wetSmooth)*(1-bypassSmooth)+l*bypassSmooth),outR=finite((r*direct+wr*wetSmooth)*(1-bypassSmooth)+r*bypassSmooth);
            if(!right)outL=.5f*(outL+outR);left[n]=outL;if(right)right[n]=outR;outputMaximum=std::max(outputMaximum,std::max(std::abs(outL),std::abs(outR)));lastBeat=beat;
        }
        if(preview){previewBeat=blockBeat+count*step;if(candidate.valid()&&previewBeat>candidate.originPPQ+candidate.durationBeats+8){preview=false;}}
        previewDisplay=preview;freePPQ=blockBeat+count*step;inputPeak=maximum;outputPeak=outputMaximum;
    }
    static bool validPlan(const Plan&p)noexcept{
        if(!p.valid()||!std::isfinite(p.bpm)||p.bpm<20||p.bpm>400)return false;float prev=-1;
        for(int i=0;i<p.count;++i){const auto&m=p.markers[i];if(!std::isfinite(m.startBeat)||!std::isfinite(m.endBeat)||!std::isfinite(m.startSec)||!std::isfinite(m.endSec)||!std::isfinite(m.strength)||m.startBeat<0||m.endBeat<=m.startBeat||m.endBeat>p.durationBeats+.1f||m.startBeat<prev||m.startSec<0||m.endSec<=m.startSec||m.endSec>p.durationSeconds+.02f||m.strength<0||m.strength>1)return false;prev=m.startBeat;}return true;
    }
    std::atomic<float>inputPeak{0},outputPeak{0};
private:
    struct Frame{float rms=0,voiced=0,side=0;};
    struct Voice{bool active=false;int bank=0;unsigned epoch=0;double position=0,begin=0,end=0,rate=48000;float gain=1,pan=0;};
    void trigger(int bank,unsigned expectedEpoch,float start,float end,float strength,int repeat,const Controls&c)noexcept{
        if(bank<0||bank>=3)return;const int role=banks[bank].role.load();if((role!=2&&role!=3)||banks[bank].epoch.load()!=expectedEpoch)return;
        auto*voice=&voices[0];for(auto&v:voices)if(!v.active){voice=&v;break;}
        const double rate=banks[bank].rate.load();*voice={true,bank,expectedEpoch,start*rate,start*rate,end*rate,rate,(.7f+strength*.6f)/(1+repeat*.5f),repeat%2?-.7f:.7f};
        (void)c;
    }
    void startLearn(double ppq,double bpm)noexcept{
        if(learning)abortLearn();undo=Plan{};preview=false;voices={};candidate=Plan{};candidate.originPPQ=ppq;candidate.bpm=static_cast<float>(bpm);captureBank=-1;
        if(kind==Kind::Reply){for(int i=0;i<3;++i){int expected=2;banks[i].role.compare_exchange_strong(expected,0);}for(int i=0;i<3;++i){int expected=0;if(banks[i].role.compare_exchange_strong(expected,1)){captureBank=i;banks[i].epoch.fetch_add(1);banks[i].used=0;banks[i].rate=captureRate;break;}}
            if(captureBank<0){state=4;return;}}
        learning=true;state=1;progress=0;capturedSeconds=0;frameCount=0;frameSamples=0;sumSquare=sumLow=sideSquare=0;crossings=0;totalSamples=0;capturePhase=0;previousCaptureL=previousCaptureR=0;phraseOpen=false;phraseBegin=lastVoicedBeat=0;phraseStartSec=lastVoicedSec=0;gapFrames=0;phraseStrength=0;framePrevious=0;sideSeen=false;for(auto&channel:antiAlias)for(auto&filter:channel)filter.s1=filter.s2=0;
        published.store(candidate);
    }
    void abortLearn()noexcept{learning=false;if(captureBank>=0&&banks[captureBank].role==1)banks[captureBank].role=0;captureBank=-1;progress=0;state=0;}
    void closePhrase()noexcept{
        if(!phraseOpen)return;phraseOpen=false;if(lastVoicedSec-phraseStartSec<.075f||candidate.count>=maxMarkers)return;
        candidate.markers[candidate.count++]={phraseBegin,std::max(phraseBegin+.01f,lastVoicedBeat),phraseStartSec,std::max(phraseStartSec+.01f,lastVoicedSec),std::clamp(phraseStrength,0.15f,1.f)};
    }
    void finishLearn()noexcept{
        if(!learning)return;closePhrase();learning=false;candidate.durationSeconds=static_cast<float>(totalSamples/fs);candidate.durationBeats=static_cast<float>(std::max(.01,lastAnalysedBeat-candidate.originPPQ));candidate.capture=captureBank;if(captureBank>=0)candidate.captureEpoch=banks[captureBank].epoch.load();
        for(int i=0;i<wavePoints;++i){const int from=i*frameCount/wavePoints,to=std::max(from+1,(i+1)*frameCount/wavePoints);float peak=0;for(int j=from;j<std::min(frameCount,to);++j)peak=std::max(peak,frames[j].rms);candidate.waveform[i]=peak;}
        if(!validPlan(candidate)){candidate=Plan{};if(captureBank>=0)banks[captureBank].role=0;state=4;progress=0;}
        else{if(captureBank>=0)banks[captureBank].role.store(2,std::memory_order_release);state=2;progress=1;}
        published.store(candidate);
    }
    bool claimBank(const Plan&p)noexcept{
        if(kind!=Kind::Reply)return true;
        if(p.capture<0||p.capture>=3)return false;auto&bank=banks[p.capture];
        if(bank.used.load()<2||bank.epoch.load()!=p.captureEpoch)return false;
        if(p.capture==activeBank&&bank.role.load()==3)return true;
        int expected=2;if(!bank.role.compare_exchange_strong(expected,3))return false;
        if(bank.epoch.load()!=p.captureEpoch){expected=3;bank.role.compare_exchange_strong(expected,2);return false;}
        return true;
    }
    void applyCandidate()noexcept{
        if(!validPlan(candidate)||learning||!claimBank(candidate))return;
        undo=active;active=candidate;if(activeBank>=0&&activeBank!=active.capture)banks[activeBank].role=2;activeBank=active.capture;publishedActive.store(active);applied=true;state=3;preview=false;resetAudio();
    }
    void undoApply()noexcept{
        if(kind==Kind::Reply&&undo.valid()&&(undo.capture<0||undo.capture>=3||banks[undo.capture].epoch.load()!=undo.captureEpoch)){undo=Plan{};return;}
        if(!undo.valid()){active=Plan{};publishedActive.store(active);applied=false;if(activeBank>=0)banks[activeBank].role=2;activeBank=-1;state=candidate.valid()?2:0;resetAudio();return;}
        if(!claimBank(undo)){undo=Plan{};return;}
        std::swap(active,undo);if(activeBank>=0&&activeBank!=active.capture)banks[activeBank].role=2;activeBank=active.capture;candidate=active;published.store(candidate);publishedActive.store(active);applied=true;state=3;resetAudio();
    }
    void analyse(float l,float r,float mono,float side,double beat,double bpm,float threshold)noexcept{
        if(totalSamples>=static_cast<std::int64_t>(fs*60)){finishLearn();return;}lastAnalysedBeat=beat;
        if(kind==Kind::Reply&&captureBank>=0){float a=l,b=r;if(fs>captureRate)for(int i=0;i<4;++i){a=antiAlias[0][i].process(a);b=antiAlias[1][i].process(b);}const double ratio=captureRate/fs;capturePhase+=ratio;if(capturePhase>=1){capturePhase-=1;const float fraction=static_cast<float>(capturePhase/ratio);auto&bank=banks[captureBank];int at=bank.used.load(std::memory_order_relaxed);if(at<bank.capacity){bank.data[at*2].store(a*(1-fraction)+previousCaptureL*fraction,std::memory_order_relaxed);bank.data[at*2+1].store(b*(1-fraction)+previousCaptureR*fraction,std::memory_order_relaxed);bank.used.store(at+1,std::memory_order_release);}}previousCaptureL=a;previousCaptureR=b;}
        ++totalSamples;sumSquare+=mono*mono;sumLow+=detectorLP*detectorLP;sideSquare+=side*side;if((mono>=0)!=(framePrevious>=0))++crossings;framePrevious=mono;
        if(++frameSamples<frameLength)return;
        const float rms=static_cast<float>(std::sqrt(sumSquare/frameSamples)),low=static_cast<float>(std::sqrt(sumLow/frameSamples));
        const float zcr=static_cast<float>(crossings)/frameSamples,ratio=low/(rms+1.e-8f);const float voiced=std::clamp((ratio-.15f)*1.4f,0.f,1.f)*std::clamp(1-zcr*2.8f,0.f,1.f);
        const float sc=static_cast<float>(std::sqrt(sideSquare/frameSamples));if(sc>.003f)sideSeen=true;
        if(frameCount<maxFrames)frames[frameCount++]={rms,voiced,sc};const float sec=static_cast<float>(totalSamples/fs),relative=static_cast<float>(beat-candidate.originPPQ);vocalConfidence=voiced;
        if(rms>threshold&&voiced>.23f){if(!phraseOpen){phraseOpen=true;phraseBegin=std::max(0.f,relative-static_cast<float>(bpm/6000));phraseStartSec=std::max(0.f,sec-.01f);phraseStrength=0;}
            lastVoicedBeat=relative;lastVoicedSec=sec;gapFrames=0;const float pressure=std::clamp((db(rms)+45)/33,0.f,1.f);const float masking=std::clamp(sc/(rms+.001f),0.f,2.f);phraseStrength=std::max(phraseStrength,std::clamp(.25f+.65f*pressure-.08f*masking,.15f,1.f));
        }else if(phraseOpen&&++gapFrames>=16)closePhrase();
        capturedSeconds=sec;progress=std::min(1.f,sec/60);frameSamples=0;sumSquare=sumLow=sideSquare=0;crossings=0;
    }
    Kind kind;double fs=48000,captureRate=48000,freePPQ=0,lastHostExpected=0,lastBeat=-1.e9,previewBeat=0,lastAnalysedBeat=0,capturePhase=0;
    bool prepared=false,hadHost=false,wasPlaying=false,learning=false,preview=false,phraseOpen=false;int frameLength=480,frameSamples=0,frameCount=0,crossings=0,gapFrames=0,captureBank=-1,activeBank=-1,delayPos=0;
    std::int64_t totalSamples=0;double sumSquare=0,sumLow=0,sideSquare=0;float framePrevious=0,phraseBegin=0,lastVoicedBeat=0,phraseStartSec=0,lastVoicedSec=0,phraseStrength=0;
    float detectorLP=0,detectorPrev=0,envelope=0,lowL=0,lowR=0,wetSmooth=0,drySmooth=1,bypassSmooth=0,previousCaptureL=0,previousCaptureR=0;
    Plan candidate,active,undo;PlanMailbox published,publishedActive,incoming;unsigned seenIncoming=0;
    std::atomic<int>command{0},state{0};std::atomic<float>progress{0},capturedSeconds{0},playPosition{0},vocalConfidence{0};std::atomic<bool>applied{false},sideSeen{false},previewDisplay{false};
    std::array<CaptureBank,3>banks;std::array<std::array<AntiAlias,4>,2>antiAlias;std::array<Frame,maxFrames>frames{};std::array<Voice,6>voices{};std::array<std::vector<float>,2>delay;Reverb reverb;
};
}
