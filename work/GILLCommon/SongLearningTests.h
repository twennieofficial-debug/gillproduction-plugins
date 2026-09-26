#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <memory>

namespace gill::testing {
struct SongTestHead final : juce::AudioPlayHead {
    bool playing=false; double seconds=37; double rate=16000;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;p.setIsPlaying(playing);p.setTimeInSeconds(seconds);
        p.setTimeInSamples(static_cast<juce::int64>(std::llround(seconds*rate)));return p;
    }
};

// Real processors, a positioned DAW transport, and a complete five-minute pass.
// Long silent gaps and a substantially different late passage catch accidental
// short timers, silence timeouts and discarded captures at transport STOP.
template<class Factory, class Check>
void songLearning(Factory factory, Check check) {
    auto p=factory();SongTestHead head;p->setPlayHead(&head);
    constexpr int block=512;constexpr double rate=16000;
    p->setRateAndBufferSizeDetails(rate,block);p->prepareToPlay(rate,block);
    juce::AudioBuffer<float> audio(2,block);juce::MidiBuffer midi;
    const auto render=[&](double duration,bool signal){
        auto samples=static_cast<juce::int64>(std::llround(duration*rate));
        while(samples>0){const int n=static_cast<int>(std::min<juce::int64>(samples,block));
            audio.setSize(2,n,false,false,true);
            for(int i=0;i<n;++i){const double t=head.seconds+i/rate;
                float v=0;if(signal){for(double hz:{120.,240.,400.,800.,1600.,3200.,5800.})v+=static_cast<float>(.035*std::sin(t*hz*juce::MathConstants<double>::twoPi));}
                audio.setSample(0,i,v);audio.setSample(1,i,v*.9f);}
            p->processBlock(audio,midi);if(head.playing)head.seconds+=n/rate;samples-=n;
        }
    };
    p->requestLearning(true);render(.5,false);
    check(p->learnState.load()==4&&!p->savedProfile().valid,"LEARN while stopped arms capture without learning silence");
    head.playing=true;render(12,true);
    check(p->learnState.load()==1&&!p->savedProfile().valid,"whole-song capture stays open beyond former ten-second limit");
    head.playing=false;render(.032,false);
    check(p->learnState.load()==2&&p->savedProfile().valid,"DAW STOP finalizes and retains measured profile");
    const auto early=p->savedProfile();
    p->requestLearning(true);render(.032,false);p->requestLearning(false);render(.032,false);
    check(p->learnState.load()==2&&p->savedProfile().valid,"cancelling armed transfer preserves prior profile");
    head.seconds=71;head.playing=true;p->requestLearning(true);render(2,true);
    head.seconds+=.08;render(.032,true);
    check(p->learnState.load()==2,"timeline seek finalizes coherent pass instead of mixing song positions");
    head.seconds=37;p->requestLearning(true);render(12,true);render(200,false);
    check(p->learnState.load()==1,"full-song learner survives a long silent middle section");
    render(88,true);
    check(p->learnState.load()==2&&p->savedProfile().valid,"complete 300-second song automatically finalizes successfully");
    check(p->getLatencySamples()==0,"full-song statistics learning adds zero audio latency");
    juce::MemoryBlock state;p->getStateInformation(state);auto recalled=factory();
    recalled->setStateInformation(state.getData(),static_cast<int>(state.getSize()));recalled->prepareToPlay(rate,block);
    juce::AudioBuffer<float> silence(2,block);silence.clear();recalled->processBlock(silence,midi);
    check(recalled->savedProfile().valid&&recalled->learnState.load()==2,"five-minute analysis survives project state recall");
    p->setPlayHead(nullptr);p->releaseResources();recalled->releaseResources();(void)early;
}
}
