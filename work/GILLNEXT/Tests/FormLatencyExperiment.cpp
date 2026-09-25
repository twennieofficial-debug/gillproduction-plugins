#include "../Source/NextDSPCommon.h"
#include "../Source/FormantEnvelopeDSP.h"
#include "../ThirdParty/signalsmith-stretch/signalsmith-stretch.h"
#define private public
#include "../Source/FormDSP.h"
#undef private
#define main FormAlignOriginalMain
#include "FormAlignTests.cpp"
#undef main
int main(){for(double hz:{75.,120.,180.,330.})for(float semitones:{-12.f,-7.f,7.f,12.f}){auto x=vowel(48000,hz,1.5);FormDSP d;d.prepare(48000,127,1);const int n=d.stretch_.blockSamples()/2;d.stretch_.configure(1,n,n/4,false);d.stretch_.setFormantFactor(1,false);d.latency_=d.stretch_.inputLatency()+d.stretch_.outputLatency()+d.formants_.latencySamples()+64;for(auto&v:d.dry_)v.assign(d.latency_,0);d.transientDelay_.assign(d.latency_,0);d.reset();FormParameters p;p.pitchSemitones=semitones;p.preserveTransients=false;d.setParameters(p);for(int i=0;i<int(x.size());i+=127){float*a[]{x.data()+i};d.process(a,1,std::min(127,int(x.size())-i));}const double target=hz*std::exp2(semitones/12),actual=pitch(x,48000,target);const double cents=actual>0?1200*std::log2(actual/target):999;check(std::abs(cents)<8,"shorter pitch window at same quality bound",cents,hz);}
std::printf("RESULT %d checks %d failures\n",checks,failures);return failures?1:0;}
