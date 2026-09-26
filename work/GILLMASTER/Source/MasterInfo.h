#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
enum class MasterKind { Ceiling,Low,Glue,Width,Punch,Weight,Delta,Deliver };
struct MasterInfo {const char*name;const char*subtitle;int width,height;};
inline MasterInfo masterInfo(MasterKind kind){
    static constexpr MasterInfo info[]{
        {"GILLCEILING","TRUE PEAK LIMITER",760,480},{"GILLLOW","LOW END CONTROL",620,340},
        {"GILLGLUE","MASTER BUS COMPRESSION",420,550},{"GILLWIDTH","STEREO FIELD",680,420},
        {"GILLPUNCH","MULTIBAND TRANSIENTS",680,360},{"GILLWEIGHT","BASS HARMONICS",400,320},
        {"GILLDELTA","LINKED MASTER COMPARISON",700,330},{"GILLDELIVER","MASTER DELIVERY CHECK",440,560}};
    return info[std::clamp(int(kind),0,7)];
}
struct MasterParam {juce::String id,label;float lo=0,hi=100,step=.1f,initial=0;juce::String suffix;juce::StringArray choices;bool flag=false;};
inline std::vector<MasterParam> masterParams(MasterKind kind){
    using P=MasterParam;std::vector<P>p;
    auto n=[&](const char*id,const char*label,float lo,float hi,float initial,const char*unit,float step=.1f){p.push_back({id,label,lo,hi,step,initial,unit,{},false});};
    auto b=[&](const char*id,const char*label,bool initial){p.push_back({id,label,0,1,1,initial?1.f:0.f,"",{},true});};
    auto c=[&](const char*id,const char*label,juce::StringArray choices,int initial){p.push_back({id,label,0,float(choices.size()-1),1,float(initial),"",choices,false});};
    switch(kind){
    case MasterKind::Ceiling:n("drive","DRIVE",0,24,0," dB");n("ceiling","CEILING",-12,0,-1," dB",.01f);n("release","RELEASE",20,500,150," ms",1);c("character","CHARACTER",{"CLEAN","PUNCH","LOUD"},0);b("match","GAIN MATCH",false);break;
    case MasterKind::Low:n("amount","TIGHT",0,100,30," %");n("frequency","LOW BAND",40,300,140," Hz",1);n("threshold","THRESHOLD",-48,0,-18," dB");n("protect","PUNCH PROTECT",0,100,60," %");n("width","BASS WIDTH",0,100,100," %");b("listen","SUB LISTEN",false);break;
    case MasterKind::Glue:n("amount","GLUE",0,100,30," %");n("attack","ATTACK",1,100,30," ms");n("release","RELEASE",30,1000,180," ms",1);n("detector","BASS FILTER",20,300,90," Hz",1);c("character","MODE",{"TRANSPARENT","GROOVE","DENSE"},0);b("match","GAIN MATCH",false);break;
    case MasterKind::Width:n("low","LOW",0,150,100," %");n("mid","MID",0,200,100," %");n("high","HIGH",0,200,100," %");n("lowHz","LOW SPLIT",60,500,160," Hz",1);n("highHz","HIGH SPLIT",1500,12000,4000," Hz",1);b("guard","CORRELATION GUARD",true);b("mono","MONO CHECK",false);break;
    case MasterKind::Punch:n("low","LOW ATTACK",-100,100,0," %");n("mid","MID ATTACK",-100,100,0," %");n("high","HIGH ATTACK",-100,100,0," %");n("sustain","SUSTAIN",-100,100,0," %");n("lowHz","LOW SPLIT",60,500,180," Hz",1);n("highHz","HIGH SPLIT",1500,12000,3500," Hz",1);break;
    case MasterKind::Weight:n("amount","WEIGHT",0,100,25," %");n("frequency","FUNDAMENTAL",40,220,110," Hz",1);c("colour","TONE",{"WARM","ROUND","BOLD"},0);b("speaker","SMALL SPEAKER",false);break;
    case MasterKind::Delta:c("role","ROLE",{"RETURN","SOURCE"},0);c("pair","PAIR",{"1","2","3","4","5","6","7","8"},0);c("audition","LISTEN",{"AFTER","BEFORE","DELTA"},0);b("match","LEVEL MATCH",true);break;
    case MasterKind::Deliver:n("target","LOUDNESS TARGET",-30,-5,-14," LUFS");n("peakTarget","PEAK TARGET",-6,0,-1," dB",.1f);n("silence","SILENCE BELOW",-90,-30,-60," dB",1);break;
    }
    return p;
}
