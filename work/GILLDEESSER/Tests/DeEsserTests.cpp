#include "../Source/DeEsserDSP.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace allocationWatch {bool enabled=false;std::size_t count=0;}
void* operator new(std::size_t n){if(allocationWatch::enabled)++allocationWatch::count;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
namespace {
constexpr double pi=gilldeesser::pi;
constexpr std::array<double,6> rates{8000,22050,44100,48000,96000,192000};
std::mt19937_64 randomEngine(0x47494c4c44535352ULL);
std::size_t checks=0,failures=0,configurations=0,audioSamples=0;
double worstFixedGainBoost=0,worstBandFftError=0,maximumRandomOutput=0;
double uniform(double a,double b){return std::uniform_real_distribution<double>(a,b)(randomEngine);}
void require(bool ok,const std::string& text){++checks;if(!ok){if(failures<30)std::cerr<<"FAIL: "<<text<<'\n';++failures;}}
double db(double amplitude){return 20*std::log10(std::max(1e-150,std::abs(amplitude)));}
template<class T>void process(gilldeesser::DeEsserEngine& e,T** c,int channels,int n,T** listen=nullptr){allocationWatch::enabled=true;e.process(c,channels,n,listen);allocationWatch::enabled=false;audioSamples+=static_cast<std::size_t>(channels)*n;}
double energy(const std::vector<double>& x,std::size_t begin=0,std::size_t end=0){if(end==0)end=x.size();double sum=0;for(auto i=begin;i<end;++i)sum+=x[i]*x[i];return sum;}
double errorEnergy(const std::vector<double>& a,const std::vector<double>& b){double sum=0;for(size_t i=0;i<a.size();++i){const auto delta=a[i]-b[i];sum+=delta*delta;}return sum;}
double siSdr(const std::vector<double>& output,const std::vector<double>& target){double dot=0,e=0;for(size_t i=0;i<output.size();++i){dot+=output[i]*target[i];e+=target[i]*target[i];}const double scale=dot/std::max(1e-30,e);double diff=0;for(size_t i=0;i<output.size();++i){const double x=output[i]-scale*target[i];diff+=x*x;}return 10*std::log10(std::max(1e-30,scale*scale*e)/std::max(1e-30,diff));}
void wav(const std::filesystem::path& path,const std::vector<double>& samples,int fs){
    std::ofstream f(path,std::ios::binary);auto word=[&](std::uint32_t v,int bytes){for(int i=0;i<bytes;++i)f.put(static_cast<char>((v>>(8*i))&255));};
    f.write("RIFF",4);word(36+static_cast<std::uint32_t>(samples.size()*4),4);f.write("WAVEfmt ",8);word(16,4);word(3,2);word(1,2);word(fs,4);word(fs*4,4);word(4,2);word(32,2);f.write("data",4);word(static_cast<std::uint32_t>(samples.size()*4),4);
    for(double d:samples){float x=static_cast<float>(d);f.write(reinterpret_cast<const char*>(&x),4);}require(f.good(),"write reproducible audition WAV");
}
struct Render {std::vector<double> audio,reduction,band;};
Render render(const std::vector<double>& input,double fs,double amount,double frequency,bool capture=false){
    Render result;result.audio=input;result.band.resize(input.size());if(capture)result.reduction.resize(input.size());gilldeesser::DeEsserEngine e;e.prepare(fs);e.setAmount(amount);e.setFrequency(frequency);
    for(size_t offset=0;offset<input.size();){const auto count=std::min(input.size()-offset,capture?std::size_t(1):std::size_t(127));double* c[]{result.audio.data()+offset};double* b[]{result.band.data()+offset};process(e,c,1,static_cast<int>(count),b);if(capture)result.reduction[offset]=e.getReductionDb();offset+=count;}return result;
}
void fft(std::vector<std::complex<double>>& x){const auto n=x.size();for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(x[i],x[j]);}for(size_t len=2;len<=n;len*=2){const auto step=std::polar(1.0,-2*pi/len);for(size_t i=0;i<n;i+=len){std::complex<double>w(1,0);for(size_t j=0;j<len/2;++j){const auto a=x[i+j],b=x[i+j+len/2]*w;x[i+j]=a+b;x[i+j+len/2]=a-b;w*=step;}}}}
void responseAndReconstruction(){
    for(double fs:rates)for(double center:{2500.0,6500.0,12000.0}){
        const double effective=gilldeesser::effectiveFrequency(center,fs);const auto edges=gilldeesser::bandEdges(center,fs);
        require(edges[0]<effective&&edges[1]>effective&&edges[1]<fs*.5,"ordered digital band edges below Nyquist");
        require(std::abs(db(std::abs(gilldeesser::bandResponse(center,fs,effective))))<1e-10,"band centre unity gain");
        for(double edge:edges)require(std::abs(db(std::abs(gilldeesser::bandResponse(center,fs,edge)))+3.010299956639812)<1e-9,"exact minus3dB band edges");
        for(int i=0;i<=2000;++i){const double hz=fs*.5*i/2000;const auto band=gilldeesser::bandResponse(center,fs,hz);require(std::abs(band.real()-std::norm(band))<1e-10,"residual split orthogonal transfer identity");for(double reduction:{0.0,3.0,6.6,12.0}){const auto wet=gilldeesser::processedResponse(center,fs,hz,reduction);worstFixedGainBoost=std::max(worstFixedGainBoost,db(std::abs(wet)));require(std::abs(wet)<=1+1e-10,"frozen residual split has no out-of-band gain boost");}}
        for(double reduction:{0.0,3.0,6.6,12.0})require(std::abs(db(std::abs(gilldeesser::processedResponse(center,fs,effective,reduction)))+reduction)<1e-9,"centre attenuation calibration");
        std::vector<double> impulse(8192);impulse[0]=1;const auto y=render(impulse,fs,0,center);
        require(y.audio==impulse,"Amount0 impulse exact with zero latency");std::vector<std::complex<double>> spectrum(impulse.size());for(size_t i=0;i<impulse.size();++i)spectrum[i]=y.band[i];fft(spectrum);
        for(size_t k=0;k<=spectrum.size()/2;++k){const auto actual=spectrum[k],expected=gilldeesser::bandResponse(center,fs,fs*k/spectrum.size());const double error=std::abs(actual-expected);worstBandFftError=std::max(worstBandFftError,error);require(error<1e-9,"actual LISTEN impulse complex response matches design");}
        const auto tail=static_cast<size_t>(std::ceil(.05*fs));if(tail<y.band.size())require(energy(y.band,tail)<1e-18,"LISTEN audio tail below minus180dB energy after reported50ms");
    }
}
std::vector<double> filteredNoise(int fs,int n,double low,double high,std::uint64_t seed){
    // Independent offline windowed-sinc FIR fixture; it does not use the
    // production IIR detector and therefore does not embed its filter shape.
    constexpr int taps=193;std::array<double,taps> h{};
    for(int i=0;i<taps;++i){const int k=i-(taps-1)/2;const double a=k==0?2*high/fs:std::sin(2*pi*high*k/fs)/(pi*k);const double b=k==0?2*low/fs:std::sin(2*pi*low*k/fs)/(pi*k);h[i]=(a-b)*(.5-.5*std::cos(2*pi*i/(taps-1)));}
    std::mt19937_64 local(seed);std::uniform_real_distribution<double> u(-1,1);std::vector<double> source(n+taps),out(n);for(auto& x:source)x=u(local);
    for(int i=0;i<n;++i)for(int k=0;k<taps;++k)out[i]+=source[static_cast<size_t>(i+taps-1-k)]*h[k];
    const double scale=1/std::sqrt(energy(out)/n);for(auto& x:out)x*=scale;return out;
}
std::vector<double> voice(int fs,int n,double fundamental){
    std::vector<double> x(n);for(int i=0;i<n;++i){const double t=static_cast<double>(i)/fs;double v=0;for(int k=1;k*fundamental<std::min(14000.0,fs*.46);++k){const double hz=k*fundamental;const double formant=1+1.7*std::exp(-std::pow((hz-700)/220,2))+1.3*std::exp(-std::pow((hz-1250)/350,2))+.7*std::exp(-std::pow((hz-2700)/650,2));v+=formant/std::pow(static_cast<double>(k),1.45)*std::sin(2*pi*hz*t+.21*k*k+.06*std::sin(2*pi*5*t));}x[i]=v*(.7+.2*std::sin(2*pi*2.7*t))*std::min(1.0,t/.025)*std::min(1.0,(static_cast<double>(n)/fs-t)/.025);}
    const double scale=.07/std::sqrt(energy(x)/n);for(auto& v:x)v*=scale;return x;
}
struct Quality {double f0,center,amount,sibilanceReduction,voiceLevel,cleanLevel,cleanSdr,targetImprovement,maximumReduction,componentError;};
std::vector<Quality> qualityRows;
void quality(){
    constexpr int fs=48000,n=120000;
    std::filesystem::create_directories("fixtures");
    for(int fixture=0;fixture<3;++fixture){const double f0=fixture==0?110:(fixture==1?173:260),center=fixture==0?6500:(fixture==1?4700:9200);
        const auto clean=voice(fs,n,f0);auto s=filteredNoise(fs,n,center*.72,std::min(center*1.28,fs*.46),0x534942494cULL+fixture);std::vector<double> input(n),target(n);std::vector<bool> burst(n);
        for(int i=0;i<n;++i){const double t=static_cast<double>(i)/fs;double envelope=0;for(double start:{.45,1.10,1.75}){const double phase=(t-start)/.22;if(phase>=0&&phase<1)envelope=std::min({1.0,phase/.08,(1-phase)/.12});}s[i]*=.14*envelope;burst[i]=envelope>.9;input[i]=clean[i]+s[i];target[i]=clean[i]+.45*s[i];}
        const auto voiceBand=render(clean,fs,0,center).band,sibilanceBand=render(s,fs,0,center).band;
        double previous=-1;
        for(double amount:{0.0,.25,.55,.8,1.0}){const auto result=render(input,fs,amount,center,true);const auto cleanResult=render(clean,fs,amount,center);double inS=0,outS=0,inVoice=0,outVoice=0,maxReduction=0,componentError=0;
            for(int i=0;i<n;++i){const double attenuation=1-std::pow(10.0,-result.reduction[i]/20);const double outV=clean[i]-attenuation*voiceBand[i],outNoise=s[i]-attenuation*sibilanceBand[i];if(burst[i]){inS+=s[i]*s[i];outS+=outNoise*outNoise;inVoice+=clean[i]*clean[i];outVoice+=outV*outV;}maxReduction=std::max(maxReduction,result.reduction[i]);componentError=std::max(componentError,std::abs(result.audio[i]-outV-outNoise));}
            Quality q{f0,center,amount,10*std::log10(inS/outS),10*std::log10(outVoice/inVoice),10*std::log10(energy(cleanResult.audio)/energy(clean)),siSdr(cleanResult.audio,clean),10*std::log10(errorEnergy(input,target)/std::max(1e-30,errorEnergy(result.audio,target))),maxReduction,componentError};qualityRows.push_back(q);
            require(componentError<1e-12,"measured output decomposes into voice and sibilance under actual applied gain trajectory");
            require(q.sibilanceReduction+1e-8>=previous,"Amount monotonically increases measured sibilance attenuation");previous=q.sibilanceReduction;
            require(std::abs(q.cleanLevel)<.15 && q.cleanSdr>35,"clean voiced harmonic fixture preserved");
            require(std::abs(q.voiceLevel)<.6,"voiced component preserved even during simultaneous strong S bursts");
            if(amount==.55){require(q.sibilanceReduction>2.0,"default Amount materially reduces noisy S component");require(q.targetImprovement>1,"default improves known excessive-S target error");}
            if(amount==1){require(q.sibilanceReduction>4,"maximum Amount strongly reduces noisy S component");require(q.maximumReduction<=12.000001,"bounded12dB centre reduction");}
            if(fixture==0 && (amount==0||amount==.55||amount==1))wav("fixtures/synthetic-deessed-"+std::to_string(static_cast<int>(std::lround(amount*100)))+".wav",result.audio,fs);
        }
        if(fixture==0){wav("fixtures/synthetic-voice.wav",clean,fs);wav("fixtures/synthetic-excessive-s.wav",input,fs);wav("fixtures/synthetic-target.wav",target,fs);}
    }
}
void tonalAndStereo(){
    for(double fs:rates)for(double hz:{100.0,1000.0,3000.0,6500.0,12000.0,16000.0}){if(hz>fs*.45)continue;const int n=static_cast<int>(fs*.5);std::vector<double>x(n);for(int i=0;i<n;++i)x[i]=.2*std::sin(2*pi*hz*i/fs);auto y=render(x,fs,1,std::clamp(hz,2500.0,12000.0),true);require(siSdr(y.audio,x)>30,"tonal attack SI-SDR fs="+std::to_string(fs)+" hz="+std::to_string(hz)+" measured="+std::to_string(siSdr(y.audio,x)));const size_t from=static_cast<size_t>(fs*.1);require(std::abs(10*std::log10(energy(y.audio,from)/energy(x,from)))<.3,"steady clean tonal HF timbre not mistaken for noisy S");double mean=0;for(size_t i=from;i<y.reduction.size();++i)mean+=y.reduction[i];mean/=y.reduction.size()-from;require(mean<.3,"tonal predictability protects sustained voice/air tone");}
    auto noise=filteredNoise(48000,24000,4500,8500,1231);
    for(double polarity:{-1.0,1.0})for(double amount:{0.0,.55,1.0}){gilldeesser::DeEsserEngine e;e.prepare(48000);e.setAmount(amount);std::vector<double> l=noise,r=noise;for(auto& x:l)x*=.1;for(auto& x:r)x*=.1*polarity;double* c[]{l.data(),r.data()};process(e,c,2,static_cast<int>(l.size()));bool linked=true;for(size_t i=0;i<l.size();++i)linked=linked&&r[i]==polarity*l[i];require(linked,"shared gain preserves exact stereo/antiphase relation");}
    {gilldeesser::DeEsserEngine e;e.prepare(48000);auto l=noise;for(auto& x:l)x*=.1;std::vector<double>r(l.size());double* c[]{l.data(),r.data()};process(e,c,2,static_cast<int>(l.size()));require(std::all_of(r.begin(),r.end(),[](double x){return x==0;}),"no crosstalk to silent channel");}
}
void qualityAcrossRates(){
    std::ofstream report("deesser-cross-rate-quality.json");report<<std::setprecision(12)<<"{\"measurements\":[\n";bool first=true;
    for(double fs:rates){const int rate=static_cast<int>(fs),n=rate;const double center=std::min(6500.0,fs*.43);auto clean=voice(rate,n,173);auto noise=filteredNoise(rate,n,center*.75,std::min(center*1.25,fs*.47),81731+rate);std::vector<double>input(n);for(int i=0;i<n;++i){noise[i]*=.14*std::min(1.0,static_cast<double>(i)/(rate*.02));input[i]=clean[i]+noise[i];}const auto voiceBand=render(clean,fs,0,center).band,noiseBand=render(noise,fs,0,center).band;
        for(double amount:{.55,1.0}){const auto output=render(input,fs,amount,center,true),dry=render(clean,fs,amount,center);double inS=0,outS=0,inV=0,outV=0,fixedS=0;for(int i=rate/5;i<n;++i){const auto a=1-std::pow(10.0,-output.reduction[i]/20);const auto s=noise[i]-a*noiseBand[i],v=clean[i]-a*voiceBand[i];inS+=noise[i]*noise[i];outS+=s*s;inV+=clean[i]*clean[i];outV+=v*v;const auto forced=noise[i]-(1-std::pow(10.0,-12.0/20))*noiseBand[i];fixedS+=forced*forced;}
            const double fixedCeiling=10*std::log10(inS/fixedS),sReduction=10*std::log10(inS/outS),vLevel=10*std::log10(outV/inV),cleanLevel=10*std::log10(energy(dry.audio)/energy(clean));
            require(sReduction>(amount==1?std::min(3.5,.85*fixedCeiling):2.0),"noisy-S reduction across rates relative to finite selected-band ceiling");require(std::abs(vLevel)<.6&&std::abs(cleanLevel)<.15,"voice component and clean voice preserved across rates");
            if(!first)report<<",\n";first=false;report<<"{\"sample_rate\":"<<fs<<",\"center_hz\":"<<center<<",\"amount\":"<<amount<<",\"sibilance_reduction_db\":"<<sReduction<<",\"constant_12db_reference_reduction_db\":"<<fixedCeiling<<",\"simultaneous_voice_level_db\":"<<vLevel<<",\"clean_voice_level_db\":"<<cleanLevel<<"}";
        }
    }
    report<<"\n]}\n";require(report.good(),"write cross-rate quality report");
}
void randomAndBlocks(){
    for(int test=0;test<12000;++test){const double fs=rates[static_cast<size_t>(test)%rates.size()];gilldeesser::DeEsserEngine e;e.prepare(fs);e.setAmount(test%7==0?0:uniform(0,1));e.setFrequency(test%11==0?12000:(test%13==0?2500:uniform(2500,12000)));std::array<double,512>l{},r{},bl{},br{};for(size_t i=0;i<l.size();++i){l[i]=uniform(-.3,.3);r[i]=uniform(-.3,.3);}const auto dryL=l,dryR=r;double* c[]{l.data(),r.data()};double* listen[]{bl.data(),br.data()};process(e,c,2,512,listen);bool finite=true;for(size_t i=0;i<l.size();++i){finite=finite&&std::isfinite(l[i])&&std::isfinite(r[i])&&std::isfinite(bl[i])&&std::isfinite(br[i]);maximumRandomOutput=std::max({maximumRandomOutput,std::abs(l[i]),std::abs(r[i])});}require(finite,"random actual audio/listen output finite");require(e.getReductionDb()>=0&&e.getReductionDb()<=12&&std::isfinite(e.getSibilanceDb()),"random reduction and detector meters bounded");if(test%7==0)require(l==dryL&&r==dryR,"Amount0 sample-exact stereo passthrough");require(e.getFrequency()<=fs*.45+1e-6,"effective centre respects current samplerate");++configurations;}
    constexpr int n=32000;std::vector<float>fl(n),fr(n);std::vector<double>dl(n),dr(n),sl(n),sr(n);for(int i=0;i<n;++i){fl[i]=static_cast<float>(uniform(-.2,.2));fr[i]=static_cast<float>(uniform(-.2,.2));dl[i]=sl[i]=fl[i];dr[i]=sr[i]=fr[i];}gilldeesser::DeEsserEngine f,d,s;f.prepare(48000);d.prepare(48000);s.prepare(48000);float* fc[]{fl.data(),fr.data()};double* dc[]{dl.data(),dr.data()};process(f,fc,2,n);process(d,dc,2,n);for(int offset=0;offset<n;){const int count=std::min(n-offset,1+offset%271);s.setAmount(.55);s.setFrequency(6500);double* sc[]{sl.data()+offset,sr.data()+offset};process(s,sc,2,count);offset+=count;}require(dl==sl&&dr==sr,"DSP and smoothing independent of block partition/repeated setters");bool exact=true;for(int i=0;i<n;++i)exact=exact&&fl[i]==static_cast<float>(dl[i])&&fr[i]==static_cast<float>(dr[i]);require(exact,"float processing exactly equals rounded double processing");
    gilldeesser::DeEsserEngine e;e.prepare(48000);std::array<double,257>x{},listen{};double* c[]{x.data()};double* band[]{listen.data()};for(int block=0;block<3000;++block){e.setAmount(block%2?0:1);e.setFrequency(block%2?2500:12000);for(auto& sample:x)sample=uniform(-.1,.1);process(e,c,1,257,band);require(std::all_of(x.begin(),x.end(),[](double v){return std::isfinite(v)&&std::abs(v)<1;}),"rapid full-range automation remains bounded");}e.setAmount(0);for(int i=0;i<40;++i){x.fill(.0123);process(e,c,1,257);}require(std::all_of(x.begin(),x.end(),[](double v){return v==.0123;}),"Amount settles to exact dry after active processing");
    e.prepare(std::numeric_limits<double>::quiet_NaN());e.setAmount(std::numeric_limits<double>::infinity());e.setFrequency(std::numeric_limits<double>::quiet_NaN());require(e.getSampleRate()==48000&&std::isfinite(e.getFrequency()),"invalid parameters and samplerate sanitized");e.reset();x.fill(0);x[0]=std::numeric_limits<double>::quiet_NaN();x[1]=std::numeric_limits<double>::infinity();x[2]=-std::numeric_limits<double>::infinity();process(e,c,1,257);require(std::all_of(x.begin(),x.end(),[](double v){return v==0;}),"invalid audio cannot poison silence");require(e.getReductionDb()==0&&e.getSibilanceDb()==-160,"silent meter floors and no false S activity");
    require(allocationWatch::count==0,"no heap allocation from any observed audio process call");
}
}
int main(){const auto start=std::chrono::steady_clock::now();responseAndReconstruction();quality();tonalAndStereo();qualityAcrossRates();randomAndBlocks();std::cout<<std::setprecision(12)<<"{\n\"suite\":\"GILLDEESSER DSP\",\n\"passed\":"<<(failures?"false":"true")<<",\n\"checks\":"<<checks<<",\n\"failures\":"<<failures<<",\n\"random_actual_audio_configurations\":"<<configurations<<",\n\"audio_channel_samples\":"<<audioSamples<<",\n\"process_heap_allocations\":"<<allocationWatch::count<<",\n\"worst_fixed_gain_boost_db\":"<<worstFixedGainBoost<<",\n\"worst_listen_band_fft_complex_error\":"<<worstBandFftError<<",\n\"maximum_random_output\":"<<maximumRandomOutput<<",\n\"quality\":[\n";for(size_t i=0;i<qualityRows.size();++i){if(i)std::cout<<",\n";const auto&q=qualityRows[i];std::cout<<"{\"fundamental_hz\":"<<q.f0<<",\"center_hz\":"<<q.center<<",\"amount\":"<<q.amount<<",\"sibilance_component_reduction_db\":"<<q.sibilanceReduction<<",\"simultaneous_voice_component_level_db\":"<<q.voiceLevel<<",\"clean_voice_level_db\":"<<q.cleanLevel<<",\"clean_voice_si_sdr_db\":"<<q.cleanSdr<<",\"known_target_error_improvement_db\":"<<q.targetImprovement<<",\"maximum_center_reduction_db\":"<<q.maximumReduction<<",\"component_reconstruction_error\":"<<q.componentError<<"}";}std::cout<<"\n],\n\"elapsed_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<",\n\"scope\":\"Synthetic voiced harmonics and independently FIR-shaped noisy S bursts; no real-speaker labeling or subjective listening claim\"\n}\n";return failures?1:0;}
