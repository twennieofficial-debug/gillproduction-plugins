#include "Signals.h"
int main(){using test::check;
 for(double fs:{16000.,44100.,48000.,96000.,192000.}){
    auto original=test::noise(fs,static_cast<int>(fs*.6)),wet=original;
    gillsmart::SmartEngine engine;engine.setAmount(1);engine.setProfile(6500,1.3,-48,14);engine.prepare(fs);float* ptr[]{wet.data()};engine.process(ptr,1,static_cast<int>(wet.size()));
    const double gain=10*std::log10(test::energy(wet,static_cast<size_t>(fs*.1))/test::energy(original,static_cast<size_t>(fs*.1)));
    std::cout<<fs<<" Hz S gain="<<gain<<" dB, reduction="<<engine.getReductionDb()<<'\n';check(gain<-1.0,"sibilant noise receives meaningful reduction");check(engine.getLatencySamples()==0,"causal DSP has zero latency");
    wet=original;engine.reset();engine.setProfile(6500,1.3,-6,14);engine.prepare(fs);engine.process(ptr,1,static_cast<int>(wet.size()));check(test::energy(wet)/test::energy(original)>.999,"real threshold prevents reduction below threshold");
    wet=original;engine.setAmount(0);engine.prepare(fs);engine.process(ptr,1,static_cast<int>(wet.size()));check(wet==original,"zero amount exactly reconstructs input");
    wet=original;engine.setAmount(1);engine.setProfile(6500,1.3,-48,14);engine.prepare(fs);engine.process(ptr,1,static_cast<int>(wet.size()));
    for(int block:{1,17,256,4096}){auto split=original;gillsmart::SmartEngine same;same.setAmount(1);same.setProfile(6500,1.3,-48,14);same.prepare(fs);for(size_t i=0;i<split.size();i+=block){float* data[]{split.data()+i};same.process(data,1,static_cast<int>(std::min<size_t>(block,split.size()-i)));}check(split==wet,"DSP invariant to callback block partition");}
    for(double hz:{220.,std::min(6500.,fs*.4)}){std::vector<float> tone(static_cast<size_t>(fs*.6));for(size_t i=0;i<tone.size();++i)tone[i]=static_cast<float>(.2*std::sin(2*gillsmart::pi*hz*i/fs));auto dry=tone;gillsmart::SmartEngine clean;clean.setAmount(1);clean.setProfile(6500,1.3,-60,18);clean.prepare(fs);float* data[]{tone.data()};clean.process(data,1,static_cast<int>(tone.size()));double error=0;for(size_t i=static_cast<size_t>(fs*.2);i<tone.size();++i)error=std::max(error,std::abs(static_cast<double>(tone[i]-dry[i])));check(error<.002,"sustained low/high sinusoid is preserved by noise detector");}
    auto left=original,right=original;for(auto& x:right)x=-x;gillsmart::SmartEngine stereo;stereo.setAmount(1);stereo.setProfile(6500,1.3,-48,14);stereo.prepare(fs);float* both[]{left.data(),right.data()};stereo.process(both,2,static_cast<int>(left.size()));bool coherent=true;for(size_t i=0;i<left.size();++i)coherent&=std::abs(left[i]+right[i])<1e-7;check(coherent,"linked stereo retains opposite phase");
    std::vector<float> invalid(4096,.1f);invalid[0]=std::numeric_limits<float>::infinity();invalid[1]=std::numeric_limits<float>::quiet_NaN();float* bad[]{invalid.data()};engine.process(bad,1,static_cast<int>(invalid.size()));bool finite=true;for(auto x:invalid)finite&=std::isfinite(x);check(finite,"nonfinite audio cannot poison DSP");
 }
 return test::finish();
}
