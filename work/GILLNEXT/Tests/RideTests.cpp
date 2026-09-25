#include "../Source/RideDSP.h"
#include "TestSupport.h"
int main(){using namespace test;using namespace gillnext;
    RideParameters p;p.speed=70;common<RideDSP>(p,"RIDE");
    bool bounded=true,direction=true,neutral=true,silence=true;double loudGain=0,quietGain=0;
    for(double fs:rates){
        for(double level:{.012,.6}){auto l=sine(fs,int(fs*4),317,level),r=l;RideDSP d;d.setParameters(p);d.prepare(fs,257,2);run(d,l,r,257);const double change=20*std::log10(rms(l,size_t(fs*3))/(level/std::sqrt(2.)));bounded=bounded&&change>=-p.rangeDownDb-.01&&change<=p.rangeUpDb+.01;direction=direction&&(level<.1?change>5:change< -7);if(level<.1)quietGain=change;else loudGain=change;
            std::fill(l.begin(),l.end(),1e-5f);r=l;run(d,l,r,127);silence=silence&&peak(l)<1e-4&&std::abs(l.back())<=1.000001e-5;
        }
        auto l=sine(fs,8192,717,.2),r=l,original=l;RideDSP d;auto off=p;off.rangeUpDb=off.rangeDownDb=0;d.setParameters(off);d.prepare(fs,127,2);run(d,l,r,127);neutral=neutral&&difference(l,original)==0&&d.latencySamples()==0;
    }
    check(bounded,"riding stays inside independent upward/downward limits");check(direction,"quiet phrases rise and loud phrases fall",quietGain);std::printf("RIDE steady quiet %.4f dB loud %.4f dB\n",quietGain,loudGain);check(neutral,"zero ranges produce bit-exact zero-latency identity");check(silence,"quiet tails stop receiving upward gain");
    RideDSP d;d.setParameters(p);d.prepare(48000,257,2);auto l=sine(48000,144000,317,.02),r=l;run(d,l,r,257);const float held=d.gainDb();p.hold=true;d.setParameters(p);l=sine(48000,144000,317,.4);r=l;run(d,l,r,257);check(std::abs(d.gainDb()-held)<1e-5,"HOLD preserves acquired fader position through loud phrases",d.gainDb()-held);auto h=d.history();check(std::abs(h.gainDb.back()-held)<1e-5&&h.inputDb.back()>-20,"history contains actual level and fader position");
    p.hold=false;p.rangeUpDb=18;p.rangeDownDb=18;d.setParameters(p);d.reset();l=sine(48000,96000,200,.002);r=l;run(d,l,r,257);bool finite=true;for(auto x:l)finite=finite&&std::isfinite(x);check(finite&&d.gainDb()<=18,"near-floor input remains bounded");
    return result();
}
