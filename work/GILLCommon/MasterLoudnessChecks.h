#pragma once
// Original deterministic, quiet rap-like fixture, not a commercial recording.
// Kick/808 transients, voiced harmonics and high-frequency pulses exercise
// loudness reserve at fixed ceiling, including the unchanged zero-boost path.
inline void masterLoudnessReserveChecks(float maximumDrive) {
    using namespace test;
    constexpr double fs=48000;
    std::vector<float> original(48000);
    for (size_t i=0;i<original.size();++i) {
        const double t=i/fs, beat=std::fmod(t,60./140.);
        const double kick=std::exp(-beat*27)*std::sin(2*pi*(53*t+1.4*std::exp(-beat*35)));
        const double vocal=(.35+.25*std::sin(2*pi*3*t))*(std::sin(2*pi*173*t)+.3*std::sin(2*pi*519*t));
        const double hat=std::exp(-std::fmod(t,60./140./4)*130)*std::sin(2*pi*9100*t);
        original[i]=float(.004*(.65*kick+.3*vocal+.05*hat));
    }
    for(bool live:{false,true}) {
        double previous=-160,first=0,last=0;bool smoothIncrease=true,finite=true,bounded=true;
        for(float boost:{0.f,3.f,6.f,9.f,12.f,15.f,18.f}) {
            auto dsp=std::make_unique<gillnext::FinishDSP>();
            gillnext::FinishParameters p;p.driveDb=maximumDrive;p.boostDb=boost;p.ceilingDb=-1;
            p.toneEnabled=p.compEnabled=p.stereoEnabled=false;p.clip=20;
            dsp->setParameters(p);dsp->setLiveMode(live);dsp->prepare(fs,127,2);
            auto l=original,r=original;run(*dsp,l,r,127);
            const double level=20*std::log10(rms(l,24000));
            if(boost==0)first=level;last=level;
            smoothIncrease &= level>previous+.5 && (boost==0||level-previous<3.15);
            previous=level;
            for(float x:l){finite &= std::isfinite(x);bounded &= std::abs(x)<=std::pow(10.,-1./20)*(1+1e-6);}
            check(dsp->latencySamples()==(live?0:176),"BOOST keeps LIVE/PRO latency unchanged");
        }
        check(smoothIncrease&&last-first>12,"BOOST provides progressive measured loudness above legacy DRIVE maximum",last-first);
        check(finite&&bounded,"BOOST sweep remains finite and below fixed -1 dB sample ceiling");
    }
}
