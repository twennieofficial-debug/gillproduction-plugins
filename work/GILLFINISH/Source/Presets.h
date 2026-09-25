#pragma once
#include <string>
#include <vector>
#include <utility>
enum class FinishKind {Silk,Spark,Strip,Gold,Dive};
namespace gillfinish {
struct ParamSpec {const char*id;const char*name;float lo,hi,step,def,skew;const char*unit;std::vector<const char*>choices{};};
inline std::vector<ParamSpec> specs(FinishKind k){
    std::vector<ParamSpec> v;
    if(k==FinishKind::Silk||k==FinishKind::Spark){v={{"depth","DEPTH",0,100,.1f,50,1," %"},{"sensitivity","SENSITIVITY",0,100,.1f,50,1," %"},{"low","LOW FOCUS",20,18000,1,120,.3f," HZ"},{"high","HIGH FOCUS",100,20000,1,12000,.4f," HZ"},{"attack","ATTACK",.1f,k==FinishKind::Silk?80.f:40.f,.1f,k==FinishKind::Silk?8.f:1.f,.35f," MS"},{"release",k==FinishKind::Silk?"RELEASE":"DECAY",10,k==FinishKind::Silk?600.f:400.f,1,140,.45f," MS"},{"mix","MIX",0,100,.1f,100,1," %"},{"output","OUTPUT",-18,6,.1f,0,1," DB"}};if(k==FinishKind::Spark)v.push_back({"mode","MODE",0,1,1,0,1,"",{"CUT","BOOST"}});v.push_back({"delta","DELTA",0,1,1,0,1,"",{"OFF","LISTEN"}});}
    else if(k==FinishKind::Strip)v={{"input","INPUT",-18,18,.1f,0,1," DB"},{"bass","BODY",-12,12,.1f,0,1," DB"},{"treble","PRESENCE",-12,12,.1f,0,1," DB"},{"compress","COMPRESS",0,100,.1f,35,1," %"},{"deess","DE-ESS",0,100,.1f,20,1," %"},{"space","SPACE",0,100,.1f,10,1," %"},{"echo","ECHO",0,100,.1f,8,1," %"},{"width","DOUBLE",0,100,.1f,0,1," %"},{"output","OUTPUT",-18,6,.1f,0,1," DB"},{"style","STYLE",0,2,1,0,1,"",{"FOCUS","TIGHT","WIDE"}},{"bpm","TEMPO",20,300,.1f,120,1," BPM"}};
    else if(k==FinishKind::Gold)v={{"lowboost","LOW BOOST",0,12,.1f,0,1," DB"},{"lowcut","LOW ATTEN",0,18,.1f,0,1," DB"},{"lowfreq","LOW FREQ",0,3,1,2,1,"",{"20 HZ","30 HZ","60 HZ","100 HZ"}},{"highboost","HIGH BOOST",0,12,.1f,0,1," DB"},{"highcut","HIGH ATTEN",0,18,.1f,0,1," DB"},{"highfreq","HIGH FREQ",0,6,1,3,1,"",{"3 KHZ","4 KHZ","5 KHZ","8 KHZ","10 KHZ","12 KHZ","16 KHZ"}},{"bandwidth","BANDWIDTH",0,100,.1f,50,1," %"},{"cutfreq","ATTEN FREQ",0,2,1,1,1,"",{"5 KHZ","10 KHZ","20 KHZ"}},{"mix","MIX",0,100,.1f,100,1," %"},{"output","OUTPUT",-18,6,.1f,0,1," DB"}};
    else v={{"depth","DEPTH",0,100,.1f,50,1," %"},{"resonance","RESONANCE",0,100,.1f,15,1," %"},{"motion","MOTION",0,100,.1f,0,1," %"},{"rate","RATE",.01f,10,.01f,.5f,.4f," HZ"},{"envelope","FOLLOW",-100,100,.1f,0,1," %"},{"mix","MIX",0,100,.1f,100,1," %"},{"output","OUTPUT",-18,6,.1f,0,1," DB"},{"sync","SYNC",0,1,1,0,1,"",{"FREE","SYNC"}},{"division","DIVISION",0,5,1,3,1,"",{"1/16","1/8","1/4","1/2","1 BAR","2 BARS"}},{"bpm","TEMPO",20,300,.1f,120,1," BPM"}};
    v.push_back({"bypass","BYPASS",0,1,1,0,1,"",{"OFF","ON"}});return v;
}
struct Preset{const char*name;std::vector<std::pair<const char*,float>> values;};
inline std::vector<Preset> presets(FinishKind k){
    using P=Preset;
    if(k==FinishKind::Silk)return{
      P{"VOCAL SILK",{{"depth",40.f},{"sensitivity",48.f},{"low",180.f},{"high",14000.f},{"attack",8.f},{"release",150.f}}},
      P{"RAP EDGE TAMER",{{"depth",58.f},{"sensitivity",58.f},{"low",1500.f},{"high",10000.f},{"attack",4.f},{"release",100.f}}},
      P{"TRAP AIR SMOOTH",{{"depth",45.f},{"sensitivity",55.f},{"low",5000.f},{"high",18000.f},{"attack",2.f},{"release",90.f}}},
      P{"NASAL CONTROL",{{"depth",62.f},{"sensitivity",62.f},{"low",650.f},{"high",2200.f},{"attack",12.f},{"release",180.f}}},
      P{"BOXY ROOM",{{"depth",65.f},{"sensitivity",65.f},{"low",180.f},{"high",850.f},{"attack",14.f},{"release",230.f}}},
      P{"HARSH UPPER MIDS",{{"depth",72.f},{"sensitivity",70.f},{"low",2200.f},{"high",6500.f},{"attack",3.f},{"release",120.f}}},
      P{"DARK LEAD",{{"depth",60.f},{"sensitivity",60.f},{"low",3500.f},{"high",20000.f},{"attack",5.f},{"release",160.f}}},
      P{"GENTLE DOUBLES",{{"depth",32.f},{"sensitivity",42.f},{"low",250.f},{"high",12000.f},{"attack",16.f},{"release",200.f}}},
      P{"ADLIB POLISH",{{"depth",67.f},{"sensitivity",63.f},{"low",1200.f},{"high",16000.f},{"attack",3.f},{"release",90.f}}},
      P{"PHONE RESONANCE",{{"depth",75.f},{"sensitivity",75.f},{"low",600.f},{"high",4500.f},{"attack",2.f},{"release",100.f}}},
      P{"SOFT TOP",{{"depth",35.f},{"sensitivity",40.f},{"low",7000.f},{"high",20000.f},{"attack",10.f},{"release",170.f}}},
      P{"DEEP CLEAN",{{"depth",90.f},{"sensitivity",80.f},{"low",100.f},{"high",18000.f},{"attack",1.f},{"release",180.f}}}};
    if(k==FinishKind::Spark)return{
      P{"REMOVE MOUTH CLICKS",{{"mode",0.f},{"depth",70.f},{"sensitivity",65.f},{"low",1500.f},{"high",18000.f},{"attack",.1f},{"release",45.f}}},
      P{"GENTLE MOUTH CLICKS",{{"mode",0.f},{"depth",45.f},{"sensitivity",45.f},{"low",1800.f},{"high",16000.f},{"attack",.5f},{"release",35.f}}},
      P{"RAP CONSONANTS",{{"mode",0.f},{"depth",45.f},{"sensitivity",50.f},{"low",2200.f},{"high",10000.f},{"attack",1.f},{"release",70.f}}},
      P{"HARD CLICK CLEANUP",{{"mode",0.f},{"depth",90.f},{"sensitivity",82.f},{"low",700.f},{"high",20000.f},{"attack",.1f},{"release",55.f}}},
      P{"LIP SMACKS",{{"mode",0.f},{"depth",78.f},{"sensitivity",70.f},{"low",700.f},{"high",15000.f},{"attack",.1f},{"release",85.f}}},
      P{"SOFT PLUCKS",{{"mode",0.f},{"depth",60.f},{"sensitivity",58.f},{"low",250.f},{"high",11000.f},{"attack",2.f},{"release",120.f}}},
      P{"DE-CRUNCH",{{"mode",0.f},{"depth",75.f},{"sensitivity",75.f},{"low",3000.f},{"high",20000.f},{"attack",.1f},{"release",25.f}}},
      P{"DOUBLES CLEAN",{{"mode",0.f},{"depth",50.f},{"sensitivity",55.f},{"low",1200.f},{"high",16000.f},{"attack",1.f},{"release",70.f}}},
      P{"RAP ATTACK",{{"mode",1.f},{"depth",35.f},{"sensitivity",50.f},{"low",1000.f},{"high",8000.f},{"attack",1.f},{"release",60.f}}},
      P{"PERCUSSION SNAP",{{"mode",1.f},{"depth",60.f},{"sensitivity",60.f},{"low",1500.f},{"high",16000.f},{"attack",.1f},{"release",80.f}}},
      P{"VOWEL SOFTEN",{{"mode",0.f},{"depth",42.f},{"sensitivity",50.f},{"low",150.f},{"high",1800.f},{"attack",3.f},{"release",100.f}}},
      P{"BRIGHT ADLIB",{{"mode",1.f},{"depth",30.f},{"sensitivity",45.f},{"low",3500.f},{"high",14000.f},{"attack",1.f},{"release",45.f}}}};
    if(k==FinishKind::Strip)return{
      P{"MODERN RAP LEAD",{{"bass",-1.f},{"treble",2.5f},{"compress",52.f},{"deess",38.f},{"space",9.f},{"echo",7.f},{"width",0.f},{"style",0.f}}},
      P{"TRAP LEAD",{{"bass",0.f},{"treble",3.f},{"compress",63.f},{"deess",48.f},{"space",16.f},{"echo",20.f},{"width",10.f},{"style",2.f}}},
      P{"DRY FORWARD",{{"bass",-2.f},{"treble",2.f},{"compress",65.f},{"deess",35.f},{"space",0.f},{"echo",0.f},{"width",0.f},{"style",0.f}}},
      P{"WARM LOW VOICE",{{"bass",2.f},{"treble",.5f},{"compress",40.f},{"deess",22.f},{"space",8.f},{"echo",5.f},{"width",0.f},{"style",0.f}}},
      P{"AIRY MELODY",{{"bass",-2.f},{"treble",4.f},{"compress",40.f},{"deess",55.f},{"space",23.f},{"echo",20.f},{"width",20.f},{"style",2.f}}},
      P{"TIGHT DOUBLES",{{"bass",-4.f},{"treble",1.f},{"compress",58.f},{"deess",50.f},{"space",4.f},{"echo",5.f},{"width",40.f},{"style",1.f}}},
      P{"WIDE ADLIBS",{{"bass",-6.f},{"treble",3.f},{"compress",70.f},{"deess",40.f},{"space",32.f},{"echo",40.f},{"width",75.f},{"style",2.f}}},
      P{"SLAP VOCAL",{{"bass",-1.f},{"treble",2.f},{"compress",45.f},{"deess",30.f},{"space",10.f},{"echo",45.f},{"width",5.f},{"style",1.f}}},
      P{"SOFT SINGER",{{"bass",1.f},{"treble",1.f},{"compress",24.f},{"deess",25.f},{"space",20.f},{"echo",5.f},{"width",10.f},{"style",0.f}}},
      P{"AGGRESSIVE RAP",{{"bass",-1.f},{"treble",4.f},{"compress",85.f},{"deess",55.f},{"space",3.f},{"echo",8.f},{"width",0.f},{"style",0.f}}},
      P{"DARK BACKGROUND",{{"bass",-3.f},{"treble",-5.f},{"compress",55.f},{"deess",40.f},{"space",30.f},{"echo",30.f},{"width",55.f},{"style",2.f}}},
      P{"CLEAN START",{{"bass",0.f},{"treble",0.f},{"compress",0.f},{"deess",0.f},{"space",0.f},{"echo",0.f},{"width",0.f},{"style",0.f}}}};
    if(k==FinishKind::Gold)return{
      P{"RAP BODY + AIR",{{"lowboost",2.f},{"lowcut",1.5f},{"lowfreq",3.f},{"highboost",3.f},{"highcut",1.f},{"highfreq",4.f},{"bandwidth",65.f},{"cutfreq",2.f}}},
      P{"SILKY TOP",{{"highboost",4.f},{"highcut",1.5f},{"highfreq",5.f},{"bandwidth",80.f},{"cutfreq",2.f}}},
      P{"DARK WARMTH",{{"lowboost",3.f},{"lowcut",1.f},{"lowfreq",3.f},{"highcut",4.f},{"cutfreq",0.f}}},
      P{"LOW VOICE WEIGHT",{{"lowboost",4.f},{"lowcut",2.5f},{"lowfreq",3.f},{"highboost",1.f},{"highfreq",3.f}}},
      P{"TRAP SHINE",{{"lowcut",2.f},{"lowfreq",3.f},{"highboost",5.f},{"highcut",1.f},{"highfreq",5.f},{"bandwidth",75.f},{"cutfreq",2.f}}},
      P{"SMOOTH PRESENCE",{{"highboost",2.5f},{"highcut",2.f},{"highfreq",1.f},{"bandwidth",70.f},{"cutfreq",1.f}}},
      P{"THIN VOICE BODY",{{"lowboost",5.f},{"lowcut",1.f},{"lowfreq",3.f},{"highboost",1.5f},{"bandwidth",90.f}}},
      P{"CUT BOOM",{{"lowcut",6.f},{"lowfreq",3.f},{"highboost",1.5f},{"highfreq",3.f}}},
      P{"AIRY DOUBLES",{{"lowcut",4.f},{"lowfreq",3.f},{"highboost",4.f},{"highfreq",6.f},{"bandwidth",90.f}}},
      P{"VINTAGE ADLIB",{{"lowcut",4.f},{"lowfreq",3.f},{"highboost",4.f},{"highfreq",0.f},{"highcut",7.f},{"cutfreq",0.f},{"bandwidth",25.f}}},
      P{"LOW END TRICK",{{"lowboost",6.f},{"lowcut",5.f},{"lowfreq",2.f}}},
      P{"FLAT",{}}};
    return{
      P{"UNDERWATER",{{"depth",67.f},{"resonance",10.f},{"motion",0.f},{"envelope",0.f}}},
      P{"DEEP DIVE",{{"depth",90.f},{"resonance",15.f},{"motion",0.f},{"envelope",0.f}}},
      P{"MUFFLED INTRO",{{"depth",55.f},{"resonance",0.f},{"motion",0.f}}},
      P{"TRAP SUBMERGE",{{"depth",72.f},{"resonance",30.f},{"motion",18.f},{"sync",1.f},{"division",4.f}}},
      P{"SLOW SURFACE",{{"depth",48.f},{"resonance",22.f},{"motion",55.f},{"rate",.08f}}},
      P{"QUARTER PULSE",{{"depth",55.f},{"resonance",18.f},{"motion",55.f},{"sync",1.f},{"division",2.f}}},
      P{"HALF TIME SWELL",{{"depth",58.f},{"resonance",25.f},{"motion",42.f},{"sync",1.f},{"division",3.f}}},
      P{"VOICE OPENS WATER",{{"depth",72.f},{"resonance",12.f},{"envelope",65.f},{"motion",0.f}}},
      P{"VOICE SINKS WATER",{{"depth",35.f},{"resonance",10.f},{"envelope",-60.f},{"motion",0.f}}},
      P{"WATERY ADLIB",{{"depth",62.f},{"resonance",48.f},{"motion",35.f},{"rate",1.3f}}},
      P{"DARK DOUBLE",{{"depth",58.f},{"resonance",0.f},{"mix",75.f}}},
      P{"OPEN WATER",{{"depth",0.f},{"resonance",0.f},{"motion",0.f},{"envelope",0.f}}}};
}
}
