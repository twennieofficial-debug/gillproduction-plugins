#define main originalSpectralMain
#include "../../GILLSTUDIO/Next03/Tests/Spectral/spectral_tests.cpp"
#undef main
#include <fstream>
double rms(const std::vector<float>&x,std::size_t start){double s=0;for(auto i=start;i<x.size();++i)s+=double(x[i])*x[i];return std::sqrt(s/(x.size()-start));}
int main(){try{
 // Integrate each actual Q=3 filter response, independently of the closed-form
 // white-noise bandwidth used by production. This checks the units of N/P.
 for(double fs:{44100.,48000.,96000.})for(double hz:{25.,150.,3000.,12000.}){
  Filter f;f.bandpass(hz,fs,3);double integral=0;constexpr int bins=262144;
  for(int k=0;k<bins;++k){auto z=std::polar(1.,-2*pi*(k+.5)/bins);integral+=std::norm((f.b0+f.b1*z+f.b2*z*z)/(1.+f.a1*z+f.a2*z*z));}
  integral/=bins;const double a=std::sin(2*pi*hz/fs)/6,closed=a/(1+a);require(std::abs(integral/closed-1)<1e-6,"detector bandwidth calibration");
 }
 for(double fs:{44100.,48000.,96000.})for(bool pink:{false,true}){
  Processor p;p.prepare(fs,257,2);Parameters par;par.mode=Mode::Live;par.learn=true;par.denoise=1;par.preserveTransient=0;par.attackMs=1;par.releaseMs=50;par.lowHz=2000;par.highHz=6000;p.setParameters(par);
  std::uint32_t rng=123497;auto noiseAudio=[&](double amplitude){auto a=zeros(std::size_t(2*fs));for(int c=0;c<2;++c){std::size_t n=1;while(n<a[c].size())n*=2;std::vector<std::complex<double>> bins(n);for(auto& x:bins)x=noise(rng);if(pink){FFT fft;fft.prepare(n);fft.transform(bins.data(),false);for(std::size_t b=0;b<n;++b)bins[b]/=std::sqrt(std::max(20.,std::min(b,n-b)*fs/n));fft.transform(bins.data(),true);}double energy=0;for(std::size_t i=0;i<a[c].size();++i)energy+=std::pow(bins[i].real(),2);double gain=amplitude/std::sqrt(3*energy/a[c].size());for(std::size_t i=0;i<a[c].size();++i)a[c][i]=float(gain*bins[i].real());}return a;};
  auto learn=noiseAudio(.02);render(p,learn);par.learn=false;p.setParameters(par);
  for(double scale:{0.,.5,1.,2.}){auto dry=noiseAudio(.02*scale);for(std::size_t i=0;i<dry[0].size();++i)for(int c=0;c<2;++c)dry[c][i]+=float(.1*std::sin(2*pi*150*i/fs));auto wet=dry;p.reset();render(p,wet);double remote=20*std::log10(tone(wet[0],fs,150,std::size_t(fs),std::size_t(fs))/tone(dry[0],fs,150,std::size_t(fs),std::size_t(fs)));double attenuation=scale?10*std::log10(noiseBand(wet[0],fs,std::size_t(fs),std::size_t(fs))/noiseBand(dry[0],fs,std::size_t(fs),std::size_t(fs))):0;std::cout<<"NOISE fs="<<fs<<" pink="<<pink<<" scale="<<scale<<" selectedDb="<<attenuation<<" remoteDb="<<remote<<"\n";require(std::abs(remote)<1.5,"remote tone damaged");if(scale==1){require(attenuation<-3,"learned noise not suppressed");for(double seconds:{.05,.15,.3,.6,1.}){auto start=std::size_t(seconds*fs),count=std::size_t(.1*fs);double db=10*std::log10(noiseBand(wet[0],fs,start,count)/noiseBand(dry[0],fs,start,count));std::cout<<"ATTACK fs="<<fs<<" pink="<<pink<<" time="<<seconds<<" selectedDb="<<db<<"\n";}}if(scale==0)require(std::abs(remote)<.1,"absent noise damaged clean tone");}
  // A voiced harmonic stack with no broadband bed must not be treated as learned noise.
  auto dry=zeros(std::size_t(2*fs));for(std::size_t i=0;i<dry[0].size();++i)for(int h=1;h<=30;++h)dry[0][i]+=float(.1/h*std::sin(2*pi*150*h*i/fs));dry[1]=dry[0];auto wet=dry;p.reset();render(p,wet);double loss=20*std::log10(rms(wet[0],std::size_t(fs))/rms(dry[0],std::size_t(fs)));double worst=0;for(int h=14;h<=30;++h){double db=20*std::log10(tone(wet[0],fs,150*h,std::size_t(fs),std::size_t(fs))/tone(dry[0],fs,150*h,std::size_t(fs),std::size_t(fs)));worst=std::min(worst,db);}std::cout<<"VOICE fs="<<fs<<" pink="<<pink<<" rmsDb="<<loss<<" worstSelectedHarmonicDb="<<worst<<"\n";require(std::abs(loss)<1,"clean voiced stack damaged");require(worst>-1,"selected voiced harmonics damaged");
  Processor neutral;neutral.prepare(fs,257,2);neutral.setParameters(par);auto a=dry;render(neutral,a);require(peakError(a,dry,0)<2e-7,"no-profile control changed audio");
 }
 {
  std::ifstream file(VOICE_FILE,std::ios::binary|std::ios::ate);require(bool(file),"voice fixture missing");const auto bytes=file.tellg();file.seekg(0);Audio clean=zeros(std::size_t(bytes)/sizeof(float));file.read(reinterpret_cast<char*>(clean[0].data()),bytes);clean[1]=clean[0];
  Processor p;p.prepare(48000,257,2);Parameters par;par.mode=Mode::Live;par.learn=true;par.denoise=1;par.preserveTransient=0;par.attackMs=1;par.releaseMs=50;par.lowHz=2000;par.highHz=6000;p.setParameters(par);auto learn=zeros(96000);std::uint32_t rng=85413;for(auto&c:learn)for(auto&x:c)x=float(.02*noise(rng));render(p,learn);par.learn=false;p.setParameters(par);p.reset();auto wet=clean;render(p,wet);double signal=0,error=0;for(std::size_t i=24000;i<260160;++i){signal+=double(clean[0][i])*clean[0][i];error+=std::pow(wet[0][i]-clean[0][i],2);}double relative=std::sqrt(error/signal);std::cout<<"LIBRISPEECH clean relativeError="<<relative<<" rmsDb="<<20*std::log10(rms(wet[0],24000)/rms(clean[0],24000))<<"\n";require(relative<.1,"clean real speech changed excessively");
  auto noisy=clean;for(auto&c:noisy)for(auto&x:c)x+=float(.02*noise(rng));auto denoised=noisy;p.reset();render(p,denoised);const auto start=std::size_t(6*48000),count=std::size_t(48000);double tail=10*std::log10(noiseBand(denoised[0],48000,start,count)/noiseBand(noisy[0],48000,start,count));std::cout<<"LIBRISPEECH noise-only tail selectedDb="<<tail<<"\n";require(tail<-3,"speech fixture noise tail not suppressed");
 }
 std::cout<<"NOISE_CONTROLS_PASS\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}



