#include "../Source/PocketDSP.h"
#include "TestSupport.h"
int main(){using namespace test;using namespace gillnext;
    PocketParameters p;p.amount=100;p.maxCutDb=12;p.speed=70;common<PocketDSP>(p,"POCKET");
    bool missingNeutral=true,quietNeutral=true,active=true,graph=true,focus=true,bounded=true,release=true;double maxGraphError=0,cut=0;
    for(double fs:rates){
        auto original=sine(fs,int(fs*2),1000,.2),l=original,r=l,sc=sine(fs,int(fs*2),1000,.2);
        PocketDSP d;d.setParameters(p);d.prepare(fs,127,2);run(d,l,r,127);missingNeutral=missingNeutral&&difference(l,original)==0&&!d.sidechainActive();
        std::fill(sc.begin(),sc.end(),1e-7f);d.reset();l=r=original;run(d,l,r,127,&sc);quietNeutral=quietNeutral&&difference(l,original)==0;
        sc=sine(fs,int(fs*2),1000,.2);d.reset();l=r=original;run(d,l,r,127,&sc);cut=20*std::log10(rms(l,size_t(fs))/rms(original,size_t(fs)));active=active&&cut< -2&&d.sidechainActive();const double error=std::abs(cut-d.responseDb(1000));maxGraphError=std::max(maxGraphError,error);graph=graph&&error<.15;
        focus=focus&&d.responseDb(1000)<d.responseDb(80)-1&&d.responseDb(1000)<d.responseDb(float(fs*.4))-1;
        for(int i=0;i<100;++i){const float f=float(20*std::pow(fs*.45/20,i/99.));const float response=d.responseDb(f);bounded=bounded&&response<=.0001&&response>=-p.maxCutDb-.0001;}
        l=sine(fs,int(fs*5),317,.15);r=l;run(d,l,r,257);release=release&&d.gainReductionDb()<1e-6&&!d.sidechainActive();
    }
    check(missingNeutral,"missing sidechain is bit-exact identity after reset");check(quietNeutral,"silent sidechain never starts ducking");check(active,"real vocal-band sidechain produces frequency-selective reduction",cut);check(graph,"display response agrees with measured steady sine gain within 0.15 dB",maxGraphError);check(focus,"sidechain frequency receives more cut than distant bass/top");check(bounded,"whole response stays between unity and requested cut budget");check(release,"disconnecting sidechain releases back to neutral");
    PocketDSP d;p.amount=0;d.setParameters(p);d.prepare(48000,257,2);auto l=sine(48000,48000,1700,.3),r=l,original=l,sc=l;run(d,l,r,257,&sc);check(difference(l,original)==0,"AMOUNT zero bypasses every dynamic bell exactly");
    return result();
}
