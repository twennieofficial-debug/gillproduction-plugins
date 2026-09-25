#include "Signals.h"
int main(){using test::check;for(double rate:{44100.,48000.,96000.}){
    auto voice=test::vocal(rate);gillsmart::Learner learner;learner.prepare(rate);const float* ptr[]{voice.data()};learner.process(ptr,1,static_cast<int>(voice.size()));const auto profile=learner.result();
    std::cout<<rate<<" Hz: active="<<profile.activeSeconds<<" voice="<<profile.voiceSeconds<<" S="<<profile.sibilantSeconds<<" frequency="<<profile.frequency<<" Q="<<profile.q<<" threshold="<<profile.threshold<<'\n';
    check(gillsmart::validProfile(profile),"voiced material with S bursts learns a valid profile");check(profile.frequency>4500&&profile.frequency<8500,"learned range follows actual high-band energy");
    auto processed=voice;gillsmart::SmartEngine automatic;automatic.setAmount(profile.amount*.01);automatic.setProfile(profile.frequency,profile.q,profile.threshold,profile.maximum);automatic.prepare(rate);float* output[]{processed.data()};automatic.process(output,1,static_cast<int>(processed.size()));
    double sIn=0,sOut=0,sError=0,voiceError=0;size_t sCount=0,voiceCount=0;
    for(size_t i=static_cast<size_t>(rate);i<voice.size();++i){const double t=i/rate,local=t-std::floor(t),error=processed[i]-voice[i];if(local>.66&&local<.80){sIn+=voice[i]*static_cast<double>(voice[i]);sOut+=processed[i]*static_cast<double>(processed[i]);sError+=error*error;++sCount;}else if(local>.15&&local<.45){voiceError+=error*error;++voiceCount;}}
    const double automaticGain=10*std::log10(sOut/sIn);std::cout<<"learned S-burst gain="<<automaticGain<<" dB\n";
    check(automaticGain<-.25,"learned threshold and amount measurably reduce actual S bursts");check(voiceError/voiceCount<sError/sCount*.1,"learned processing preserves intervening voiced material");
    for(int block:{1,37,512,4096}){gillsmart::Learner split;split.prepare(rate);for(size_t i=0;i<voice.size();i+=block){const float* data[]{voice.data()+i};split.process(data,1,static_cast<int>(std::min<size_t>(block,voice.size()-i)));}auto a=split.result();check(a.valid==profile.valid&&std::abs(a.frequency-profile.frequency)<1e-9&&std::abs(a.threshold-profile.threshold)<1e-9,"learning independent of block partition");}
    auto inverted=voice;for(auto& x:inverted)x=-x;gillsmart::Learner stereo;stereo.prepare(rate);const float* channels[]{voice.data(),inverted.data()};stereo.process(channels,2,static_cast<int>(voice.size()));check(std::abs(stereo.result().frequency-profile.frequency)<1e-5,"antiphase stereo voice does not cancel in analyzer");
    learner.reset();std::vector<float> silence(static_cast<size_t>(rate*9),0);const float* quiet[]{silence.data()};learner.process(quiet,1,static_cast<int>(silence.size()));check(!learner.result().valid&&learner.activeSeconds()==0,"silence cannot create a profile");
    for(size_t i=0;i<silence.size();++i)silence[i]=static_cast<float>(.2*std::sin(2*gillsmart::pi*220*i/rate));learner.reset();learner.process(quiet,1,static_cast<int>(silence.size()));check(!learner.result().valid,"voiced tone without S content is rejected");
    auto hiss=test::noise(rate,static_cast<int>(rate*9));const float* hissPtr[]{hiss.data()};learner.reset();learner.process(hissPtr,1,static_cast<int>(hiss.size()));check(!learner.result().valid,"high-frequency noise without vocal context is rejected");
    learner.reset();learner.process(ptr,1,static_cast<int>(rate));check(!learner.result().valid,"short vocal sample cannot create a profile");
    voice[0]=std::numeric_limits<float>::quiet_NaN();voice[1]=std::numeric_limits<float>::infinity();learner.reset();learner.process(ptr,1,static_cast<int>(voice.size()));check(gillsmart::validProfile(learner.result()),"nonfinite input does not poison later learning");
  }
  gillsmart::Learner dark,bright;dark.prepare(48000);bright.prepare(48000);auto a=test::vocal(48000,9,3800),b=test::vocal(48000,9,9500);const float* pa[]{a.data()};const float* pb[]{b.data()};dark.process(pa,1,static_cast<int>(a.size()));bright.process(pb,1,static_cast<int>(b.size()));check(dark.result().valid&&bright.result().valid&&bright.result().frequency>dark.result().frequency*1.45,"learning changes range with different sibilance spectra");
  return test::finish();
}
