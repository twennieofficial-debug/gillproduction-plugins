// Dynamic verification uses real audio and an independently compiled archived
// static implementation, as well as transfer-function and envelope references.
#define main staticSuiteMain
#include "DspTests.cpp"
#undef main
#define gill legacy_gill
#include "DynamicLegacyEqDSP.h"
#undef gill
#include <fstream>

namespace {
std::size_t dynamicConfigurations = 0, legacyComparisons = 0;
double worstGainError = 0, worstDetectorError = 0, worstEnvelopeError = 0, maximumDynamicOutput = 0;
struct SteadyResult { double detector, change, measuredGain, predictedGain; };
struct Recorded { double fs, hz, inputDb, range, detector, change, expected, measured, predicted; };
std::vector<Recorded> recorded;

void processTone(gill::EqEngine& e, double fs, double hz, double rmsDb, int n, double polarity = 1.0, int channels = 2, bool leftSilent = false, bool rightSilent = false) {
    std::array<double, 256> l{}, r{}; double* c[]{l.data(),r.data()};
    const double amplitude=std::sqrt(2.0)*std::pow(10.0,rmsDb/20.0);
    for(int offset=0;offset<n;offset+=256) {
        const int count=std::min(256,n-offset);
        for(int j=0;j<count;++j) {const double x=amplitude*std::sin(2*gill::pi*hz*(offset+j)/fs);l[j]=leftSilent?0:x;r[j]=rightSilent?0:polarity*x;}
        e.process(c,channels,count);audioSamples+=count*channels;
    }
}
SteadyResult steady(gill::BandParams p,double fs,double hz,double rmsDb) {
    auto bands=emptyBands();bands[0]=p;gill::EqEngine e;e.prepare(fs);e.setBands(bands);
    const int n=static_cast<int>(fs*2.0),measureStart=static_cast<int>(fs*1.75);
    std::vector<double>x(n),y(n);const double amplitude=std::sqrt(2.0)*std::pow(10.0,rmsDb/20.0);
    for(int i=0;i<n;++i)x[i]=y[i]=amplitude*std::sin(2*gill::pi*hz*i/fs);
    double level=0,change=0,predicted=0;int observations=0;
    for(int offset=0;offset<n;offset+=64) {
        const int count=std::min(64,n-offset);double* c[]{y.data()+offset};e.process(c,1,count);audioSamples+=count;
        if(offset>=measureStart) {level+=e.getDetectorDb(0);change+=e.getDynamicGainDb(0);predicted+=db(std::abs(gill::coefficientResponse(e.getCurrentBands()[0],fs,hz)));++observations;}
    }
    Complex input{},output{};
    for(int i=measureStart;i<n;++i){const auto z=std::polar(1.0,-2*gill::pi*hz*i/fs);input+=x[i]*z;output+=y[i]*z;}
    return {level/observations,change/observations,db(std::abs(output/input)),predicted/observations};
}
void steadyAndFrequencyIsolation() {
    for(double fs:rates) for(int type:{gill::Bell,gill::LowShelf,gill::HighShelf}) for(double range:{-6.0,6.0}) for(double input:{-42.0,-24.0,-18.0,-6.0}) {
        gill::BandParams p;p.dynamic=true;p.type=type;p.frequency=1000;p.q=2;p.dynamicRangeDb=range;p.thresholdDb=-24;p.attackMs=10;p.releaseMs=150;
        const double hz=type==gill::LowShelf?200.0:(type==gill::HighShelf?5000.0:1000.0);
        const double detector=input+db(std::abs(gill::sectionResponse(gill::designDetector(p,fs),fs,hz)));
        const double expected=std::copysign(std::min(std::abs(range),std::max(0.0,detector-p.thresholdDb)*0.5),range);
        const auto m=steady(p,fs,hz,input);
        worstGainError=std::max(worstGainError,std::abs(m.change-expected));worstDetectorError=std::max(worstDetectorError,std::abs(m.detector-detector));
        require(std::abs(m.change-expected)<0.075,"RMS threshold and signed range obey independent 2:1 steady-state reference");
        require(std::abs(m.detector-detector)<0.075,"detector dBFS calibration and frequency response");
        require(std::abs(m.measuredGain-m.predictedGain)<0.075,"audio sinusoid gain matches actual dynamic response snapshot");
        recorded.push_back({fs,hz,input,range,m.detector,m.change,expected,m.measuredGain,m.predictedGain});
    }
    for(double fs:rates) {
        for(double center:{20.0,55.0,100.0,10000.0,20000.0}) {
            gill::BandParams low;low.dynamic=true;low.frequency=center;low.q=2;low.thresholdDb=-24;low.dynamicRangeDb=-12;
            const auto m=steady(low,fs,center,-18);
            require(std::abs(m.change+3)<0.075,"bell low/high frequency RMS transfer accurate through 20Hz..20kHz");
            require(std::abs(m.measuredGain-m.predictedGain)<0.075,"low/high frequency actual gain matches live response");
            worstGainError=std::max(worstGainError,std::abs(m.change+3));
        }
        { gill::BandParams low;low.dynamic=true;low.type=gill::LowShelf;low.frequency=1000;low.thresholdDb=-24;low.dynamicRangeDb=-12;
          const auto m=steady(low,fs,20,-18);require(std::abs(m.change+3)<0.075,"low shelf deep bass RMS threshold accuracy");worstGainError=std::max(worstGainError,std::abs(m.change+3)); }
        gill::BandParams p;p.dynamic=true;p.frequency=1000;p.q=4;p.thresholdDb=-24;p.dynamicRangeDb=-12;
        for(double hz:{80.0,10000.0}) {const auto m=steady(p,fs,hz,-12);require(std::abs(m.change)<0.001,"loud far-out-of-band tone does not trigger bell");}
        p.type=gill::LowShelf;require(std::abs(steady(p,fs,12000,-12).change)<0.001,"low shelf detector rejects high tone");
        p.type=gill::HighShelf;require(std::abs(steady(p,fs,60,-12).change)<0.001,"high shelf detector rejects low tone");
        p.type=gill::Bell;p.gainDb=20;p.dynamicRangeDb=24;p.thresholdDb=-80;
        require(std::abs(steady(p,fs,1000,-6).change-4)<0.001,"positive dynamics respect total +24dB cap");
        p.gainDb=-20;p.dynamicRangeDb=-24;
        require(std::abs(steady(p,fs,1000,-6).change+4)<0.001,"negative dynamics respect total -24dB cap");
    }
}
void attackRelease() {
    for(double fs:rates) for(double attack:{0.1,10.0,200.0}) for(double release:{10.0,150.0,2000.0}) {
        auto b=emptyBands();auto& p=b[0];p.enabled=true;p.dynamic=true;p.frequency=2000;p.thresholdDb=-50;p.dynamicRangeDb=0;p.attackMs=attack;p.releaseMs=release;
        gill::EqEngine e;e.prepare(fs);e.setBands(b);processTone(e,fs,2000,-12,static_cast<int>(fs));
        p.dynamicRangeDb=-6;e.setBands(b);
        const int attackN=std::max(16,static_cast<int>(std::lround(attack*.001*fs)));
        processTone(e,fs,2000,-12,attackN);
        // Gain coefficients are refreshed every eight samples. The time
        // constant is exact; permit only that bounded control-rate interval.
        const double actual=e.getDynamicGainDb(0);
        const double lo=-6*(1-std::exp(-std::max(0,attackN-8)/(attack*.001*fs)));
        const double hi=-6*(1-std::exp(-attackN/(attack*.001*fs)));
        require(actual>=hi-1e-7 && actual<=lo+1e-7,"attack one-time-constant interval including <=8 sample coefficient scheduling");
        processTone(e,fs,2000,-12,static_cast<int>(fs*std::max(.5,attack*.012)));
        const double initial=e.getDynamicGainDb(0);p.thresholdDb=0;e.setBands(b);
        const int releaseN=static_cast<int>(std::lround(release*.001*fs));processTone(e,fs,2000,-12,releaseN);
        const double expected=initial*std::exp(-static_cast<double>(releaseN)/(release*.001*fs));
        const double error=std::abs(e.getDynamicGainDb(0)-expected);worstEnvelopeError=std::max(worstEnvelopeError,error);
        require(error<0.04,"release measured gain follows exponential time constant");
    }
}
void routingAndTransparency() {
    for(double fs:rates) for(int mode=0;mode<5;++mode) {
        for(double polarity:{-1.0,1.0}) {
            auto b=emptyBands();auto& p=b[0];p.enabled=true;p.dynamic=true;p.channel=mode;p.frequency=1000;p.thresholdDb=-36;p.dynamicRangeDb=-6;
            gill::EqEngine e;e.prepare(fs);e.setBands(b);processTone(e,fs,1000,-12,static_cast<int>(fs),polarity);
            const bool rejected=(mode==gill::Mid && polarity<0)||(mode==gill::Side && polarity>0);
            require(rejected?std::abs(e.getDynamicGainDb(0))<1e-12:std::abs(e.getDynamicGainDb(0)+6)<0.001,"M/S detector polarity routes correctly");
            std::array<double,512> l{},r{};for(int i=0;i<512;++i){l[i]=.25*std::sin(2*gill::pi*1000*i/fs);r[i]=polarity*l[i];}const auto inL=l,inR=r;double* c[]{l.data(),r.data()};e.process(c,2,512);audioSamples+=1024;
            if(mode==gill::Left) require(r==inR,"left dynamic band never changes right audio");
            else if(mode==gill::Right)require(l==inL,"right dynamic band never changes left audio");
            else {bool linked=true;for(int i=0;i<512;++i)linked=linked && std::abs(r[i]-polarity*l[i])<1e-14;require(linked,"stereo/M/S gain preserves coherent image");}
            if(rejected)require(l==inL && r==inR,"opposite M/S source remains sample-exact");
        }
        auto b=emptyBands();auto& p=b[0];p.enabled=true;p.dynamic=true;p.channel=mode;p.frequency=1000;p.thresholdDb=-36;
        gill::EqEngine e;e.prepare(fs);e.setBands(b);
        const bool leftSilent=mode==gill::Left;const bool rightSilent=mode==gill::Right;
        if(leftSilent||rightSilent){processTone(e,fs,1000,-12,static_cast<int>(fs),1,2,leftSilent,rightSilent);require(e.getDynamicGainDb(0)==0,"opposite isolated channel cannot trigger detector");}
        if(mode==gill::Right||mode==gill::Side){e.reset();e.setBands(b);processTone(e,fs,1000,-12,static_cast<int>(fs),1,1);require(e.getDynamicGainDb(0)==0,"mono side/right detector remains inactive");}
    }
    for(int type=0;type<6;++type) {
        auto b=emptyBands();b[0].enabled=true;b[0].type=type;b[0].gainDb=0;b[0].dynamic=true;b[0].thresholdDb=-20;
        auto plain=b;plain[0].dynamic=false;gill::EqEngine a,s;a.prepare(48000);s.prepare(48000);a.setBands(b);s.setBands(plain);
        std::array<double,4096>x{},y{};for(std::size_t i=0;i<x.size();++i)x[i]=y[i]=uniform(-.0001,.0001);double* ac[]{x.data()};double* sc[]{y.data()};a.process(ac,1,4096);s.process(sc,1,4096);audioSamples+=8192;
        require(x==y,"below-threshold flat dynamics or unsupported cut/notch is exact static signal");
    }
    // Same detector must report the same value even if an earlier EQ band
    // boosts its frequency. This would fail with cascade-fed sidechains.
    auto first=emptyBands();first[1].enabled=true;first[1].dynamic=true;first[1].thresholdDb=-30;first[1].frequency=1000;
    auto second=first;second[0].enabled=true;second[0].gainDb=18;second[0].frequency=1000;
    gill::EqEngine a,b;a.prepare(48000);b.prepare(48000);a.setBands(first);b.setBands(second);processTone(a,48000,1000,-18,96000);processTone(b,48000,1000,-18,96000);
    require(a.getDetectorDb(1)==b.getDetectorDb(1) && a.getDynamicGainDb(1)==b.getDynamicGainDb(1),"detectors reference original input independent of other bands");
}
legacy_gill::Bands legacyBands(const gill::Bands& b) {
    legacy_gill::Bands result{};for(size_t i=0;i<b.size();++i){auto& r=result[i];const auto& p=b[i];r.enabled=p.enabled;r.type=p.type;r.channel=p.channel;r.slope=p.slope;r.frequency=p.frequency;r.gainDb=p.gainDb;r.q=p.q;}return result;
}
void actualLegacyNull() {
    for(double fs:rates) {
        gill::EqEngine current;legacy_gill::EqEngine old;current.prepare(fs);old.prepare(fs);
        for(int iteration=0;iteration<100;++iteration) {
            gill::Bands b{};for(auto& p:b){p.enabled=uniform(0,1)>.2;p.type=static_cast<int>(uniform(0,5.99));p.channel=static_cast<int>(uniform(0,4.99));p.slope=static_cast<int>(uniform(0,2.99));p.frequency=std::exp(uniform(std::log(20),std::log(20000)));p.q=std::exp(uniform(std::log(.1),std::log(18)));p.gainDb=uniform(-24,24);p.dynamic=false;p.dynamicRangeDb=uniform(-24,24);p.thresholdDb=uniform(-80,0);}
            current.setBands(b);old.setBands(legacyBands(b));std::array<double,2048>l{},r{},ol{},orr{};
            for(size_t i=0;i<l.size();++i){l[i]=ol[i]=uniform(-1e-6,1e-6);r[i]=orr[i]=uniform(-1e-6,1e-6);}double* c[]{l.data(),r.data()};double* o[]{ol.data(),orr.data()};current.process(c,2,2048);old.process(o,2,2048);audioSamples+=8192;
            require(l==ol && r==orr,"static path bit-identical to archived 0.1 including continuous and discrete automation");++legacyComparisons;
        }
    }
}
void dynamicRandomized() {
    for(int test=0;test<12000;++test) {
        const double fs=rates[static_cast<size_t>(test)%rates.size()];auto b=emptyBands();auto& p=b[0];p.enabled=true;p.dynamic=true;p.type=test%6;p.channel=(test/6)%5;p.slope=(test/30)%3;
        p.frequency=std::exp(uniform(std::log(20),std::log(20000)));p.q=std::exp(uniform(std::log(.1),std::log(18)));p.gainDb=uniform(-24,24);p.dynamicRangeDb=uniform(-24,24);p.thresholdDb=uniform(-80,0);p.attackMs=std::exp(uniform(std::log(.1),std::log(200)));p.releaseMs=std::exp(uniform(std::log(10),std::log(2000)));
        if(test%7==0)p.frequency=20;if(test%11==0)p.frequency=20000;if(test%13==0)p.q=18;if(test%17==0)p.attackMs=.1;
        gill::EqEngine e;e.prepare(fs);e.setBands(b);std::array<double,512>l{},r{};for(size_t i=0;i<l.size();++i){l[i]=uniform(-.1,.1);r[i]=uniform(-.1,.1);}double* c[]{l.data(),r.data()};e.process(c,2,512);audioSamples+=1024;
        bool finite=true;for(size_t i=0;i<l.size();++i){finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i]);maximumDynamicOutput=std::max({maximumDynamicOutput,std::abs(l[i]),std::abs(r[i])});}
        require(finite,"random actual dynamic audio finite");require(std::isfinite(e.getDetectorDb(0))&&std::isfinite(e.getDynamicGainDb(0)),"dynamic meters finite");
        const auto live=e.getCurrentBands()[0];require(live.gainDb>=-24 && live.gainDb<=24,"live gain stays within coefficient design range");
        for(double endpoint:{p.gainDb,std::clamp(p.gainDb+p.dynamicRangeDb,-24.0,24.0)}) {auto boundary=p;boundary.gainDb=endpoint;const auto f=gill::designFilter(boundary,fs);for(int i=0;i<f.count;++i)require(poleRadius(f.sections[i])<1,"dynamic endpoint pole stability");}
        ++dynamicConfigurations;
    }
    gill::EqEngine e;e.prepare(48000);auto b=emptyBands();for(auto& p:b){p.enabled=true;p.dynamic=true;p.frequency=1000;p.thresholdDb=-24;}
    std::array<double,257>l{},r{};double* c[]{l.data(),r.data()};
    for(int block=0;block<2000;++block) {
        for(auto& p:b){p.type=block%6;p.channel=block%5;p.frequency=std::exp(uniform(std::log(20),std::log(20000)));p.q=uniform(.1,18);p.gainDb=uniform(-12,12);p.dynamicRangeDb=uniform(-24,24);p.thresholdDb=uniform(-80,0);p.attackMs=block%2?.1:200;p.releaseMs=block%2?10:2000;}e.setBands(b);
        for(size_t i=0;i<l.size();++i){l[i]=uniform(-1e-6,1e-6);r[i]=uniform(-1e-6,1e-6);}e.process(c,2,257);audioSamples+=514;
        bool finite=true;for(size_t i=0;i<l.size();++i)finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i]);require(finite,"all-eight-band adversarial dynamic automation remains finite");
    }
    gill::BandParams invalid;invalid.dynamic=true;invalid.thresholdDb=std::numeric_limits<double>::quiet_NaN();invalid.dynamicRangeDb=std::numeric_limits<double>::infinity();invalid.attackMs=-1;invalid.releaseMs=1e9;
    const auto p=gill::sanitize(invalid,48000);require(p.thresholdDb==-24 && p.dynamicRangeDb==-6 && p.attackMs==.1 && p.releaseMs==2000,"nonfinite and out-of-range dynamic parameters sanitized");
}
void partitionAndFloat() {
    gill::Bands b{};for(size_t i=0;i<b.size();++i){b[i].dynamic=true;b[i].type=static_cast<int>(i%3);b[i].frequency=40*std::pow(2.0,i);b[i].gainDb=static_cast<double>(i)-4;b[i].thresholdDb=-36;b[i].dynamicRangeDb=-3;}
    constexpr int n=24000;std::vector<float> fl(n),fr(n);std::vector<double> dl(n),dr(n),sl(n),sr(n);
    for(int i=0;i<n;++i){fl[i]=static_cast<float>(uniform(-.1,.1));fr[i]=static_cast<float>(uniform(-.1,.1));dl[i]=sl[i]=fl[i];dr[i]=sr[i]=fr[i];}
    gill::EqEngine f,d,s;f.prepare(48000);d.prepare(48000);s.prepare(48000);f.setBands(b);d.setBands(b);s.setBands(b);float* fc[]{fl.data(),fr.data()};double* dc[]{dl.data(),dr.data()};f.process(fc,2,n);d.process(dc,2,n);
    for(int offset=0;offset<n;){const int count=std::min(n-offset,1+offset%127);s.setBands(b);double* sc[]{sl.data()+offset,sr.data()+offset};s.process(sc,2,count);offset+=count;}
    require(dl==sl && dr==sr,"dynamic envelope and audio invariant to host blocks and repeated setBands");bool exact=true;for(int i=0;i<n;++i)exact=exact&&fl[i]==static_cast<float>(dl[i])&&fr[i]==static_cast<float>(dr[i]);require(exact,"dynamic float path is exactly rounded double path");audioSamples+=6*n;
}
void extremeRatesAndDrivenDynamics() {
    for(double fs:{8000.0,11025.0,22050.0,352800.0,384000.0,768000.0}) {
        for(int type:{gill::Bell,gill::LowShelf,gill::HighShelf}) for(double frequency:{20.0,20000.0}) {
            auto b=emptyBands();auto& p=b[0];p.enabled=true;p.dynamic=true;p.type=type;p.frequency=frequency;p.q=18;p.thresholdDb=-80;p.dynamicRangeDb=-24;p.attackMs=.1;p.releaseMs=10;
            gill::EqEngine e;e.prepare(fs);e.setBands(b);std::array<double,4096>l{},r{};for(size_t i=0;i<l.size();++i){l[i]=uniform(-.1,.1);r[i]=uniform(-.1,.1);}double* c[]{l.data(),r.data()};e.process(c,2,4096);audioSamples+=8192;
            bool finite=true;for(size_t i=0;i<l.size();++i)finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i]);require(finite,"extended supported-rate dynamic extreme remains finite");
        }
    }
    for(double fs:rates) for(double frequency:{20.0,1000.0,20000.0}) for(double range:{-24.0,24.0}) {
        auto b=emptyBands();auto& p=b[0];p.enabled=true;p.dynamic=true;p.frequency=frequency;p.q=18;p.thresholdDb=-40;p.dynamicRangeDb=range;p.attackMs=.1;p.releaseMs=10;
        gill::EqEngine e;e.prepare(fs);e.setBands(b);const int count=static_cast<int>(fs*1.5);double maximum=0;
        std::array<double,128>l{},r{};double* c[]{l.data(),r.data()};
        for(int offset=0;offset<count;offset+=128){const int n=std::min(128,count-offset);for(int i=0;i<n;++i){const double amp=((offset+i)/static_cast<int>(fs*.037))%2?.2:.0001;l[i]=r[i]=amp*std::sin(2*gill::pi*frequency*(offset+i)/fs);}e.process(c,2,n);audioSamples+=2*n;for(int i=0;i<n;++i)maximum=std::max(maximum,std::abs(l[i]));}
        require(std::isfinite(maximum)&&maximum<10.0,"high-Q fastest dynamics with repeated 37ms audio bursts stays bounded");
        const auto live=e.getCurrentBands()[0];require(live.gainDb>=-24&&live.gainDb<=24,"driven dynamic effective gain bound");
    }
    auto b=emptyBands();b[0].enabled=true;b[0].dynamic=true;b[0].thresholdDb=-80;
    gill::EqEngine e;e.prepare(48000);e.setBands(b);std::array<double,10000>x{};x[0]=std::numeric_limits<double>::quiet_NaN();x[1]=std::numeric_limits<double>::infinity();x[2]=-std::numeric_limits<double>::infinity();double* c[]{x.data()};e.process(c,1,static_cast<int>(x.size()));audioSamples+=x.size();
    require(std::all_of(x.begin(),x.end(),[](double v){return v==0.0;}),"nonfinite host samples cannot poison dynamic silence");
    require(e.getDynamicGainDb(0)==0 && e.getDetectorDb(0)==-160,"dynamic silence has zero gain change and meter floor");
}
}
int main() {
    const auto start=std::chrono::steady_clock::now();actualLegacyNull();steadyAndFrequencyIsolation();attackRelease();routingAndTransparency();dynamicRandomized();partitionAndFloat();extremeRatesAndDrivenDynamics();
    std::cout<<std::setprecision(12)<<"{\n\"suite\":\"GILLEQ dynamic EQ\",\n\"passed\":"<<(failures==0?"true":"false")<<",\n\"checks\":"<<checks<<",\n\"failures\":"<<failures<<",\n\"dynamic_audio_configurations\":"<<dynamicConfigurations<<",\n\"legacy_bit_exact_comparisons\":"<<legacyComparisons<<",\n\"audio_channel_samples\":"<<audioSamples<<",\n\"worst_steady_dynamic_gain_error_db\":"<<worstGainError<<",\n\"worst_detector_error_db\":"<<worstDetectorError<<",\n\"worst_release_error_db\":"<<worstEnvelopeError<<",\n\"maximum_random_output\":"<<maximumDynamicOutput<<",\n\"steady_measurements\":[\n";
    for(size_t i=0;i<recorded.size();++i){if(i)std::cout<<",\n";const auto& r=recorded[i];std::cout<<"{\"fs\":"<<r.fs<<",\"hz\":"<<r.hz<<",\"input_rms_dbfs\":"<<r.inputDb<<",\"range_db\":"<<r.range<<",\"detector_dbfs\":"<<r.detector<<",\"dynamic_gain_db\":"<<r.change<<",\"expected_dynamic_gain_db\":"<<r.expected<<",\"measured_audio_gain_db\":"<<r.measured<<",\"snapshot_gain_db\":"<<r.predicted<<"}";}
    std::cout<<"\n],\n\"elapsed_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\n\"scope\":\"DSP regression, routing, dynamics, automation and numeric tests; not proof of every host or subjective audio condition\"\n}\n";return failures?1:0;
}
