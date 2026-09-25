#pragma once
#include <vector>
#include <utility>
#include <string>
enum class DynKind { Vox, Opta, Buss, Quad, Stage };
namespace gilldyn {
struct ParamSpec {std::string id,name,unit;float lo=0,hi=100,step=.1f,def=0,skew=1;std::vector<const char*>choices;};
struct Preset {const char*name;std::vector<std::pair<std::string,float>>values;};
inline std::vector<ParamSpec> specs(DynKind kind){
 std::vector<ParamSpec>d;
 if(kind==DynKind::Vox)d={{"gate","GATE"," DB",-90,-10,.1f,-90},{"comp","COMP"," %",0,100,.1f,40},{"output","OUTPUT"," DB",-24,12,.1f,-3}};
 else if(kind==DynKind::Opta)d={{"gain","GAIN"," DB",-20,20,.1f,0},{"reduction","PEAK REDUCTION"," %",0,100,.1f,40},{"hf","HF"," %",0,100,.1f,0},{"limit","MODE","",0,1,1,0,1,{"COMP","LIMIT"}},{"meter","METER","",0,2,1,1,1,{"IN","GR","OUT"}},{"noise","NOISE","",0,1,1,0,1,{"OFF","ON"}}};
 else if(kind==DynKind::Buss){
  d={{"drive","DRIVE"," DB",0,24,.1f,0},{"trim","TRIM"," DB",-24,12,.1f,0},{"style","CHARACTER","",0,2,1,0,1,{"CLEAN","IRON","VELVET"}},{"noise","NOISE","",0,1,1,0,1,{"OFF","ON"}},{"group","VCA GROUP","",0,8,1,0,1,{"NONE","1","2","3","4","5","6","7","8"}}};
  for(int i=1;i<=8;++i){const auto n=std::to_string(i);d.push_back({"g"+n+"drive","DRIVE"," DB",-12,12,.1f,0});d.push_back({"g"+n+"trim","TRIM"," DB",-12,12,.1f,0});d.push_back({"g"+n+"bypass","BYPASS","",0,1,1,0,1,{"OFF","ON"}});d.push_back({"g"+n+"noise","NOISE","",0,1,1,0,1,{"OFF","ON"}});}
 }else if(kind==DynKind::Quad){
  d={{"cross1","LOW CROSSOVER"," HZ",20,1000,1,180,.35f},{"cross2","MID CROSSOVER"," HZ",200,6000,1,1400,.4f},{"cross3","HIGH CROSSOVER"," HZ",1000,18000,1,6000,.45f},{"output","OUTPUT"," DB",-18,12,.1f,0}};
  for(int i=1;i<=4;++i){const auto n="b"+std::to_string(i);d.push_back({n+"threshold","THR"," DB",-60,0,.1f,-24});d.push_back({n+"range","RANGE"," DB",-24,12,.1f,0});d.push_back({n+"gain","GAIN"," DB",-12,12,.1f,0});d.push_back({n+"attack","ATT"," MS",1,200,.1f,12,.4f});d.push_back({n+"release","REL"," MS",10,1000,1,140,.4f});d.push_back({n+"solo","SOLO","",0,1,1,0,1,{"OFF","ON"}});d.push_back({n+"bypass","BYP","",0,1,1,0,1,{"OFF","ON"}});}
 }else d={{"x","POSITION","",-1,1,.001f,0},{"distance","DISTANCE","",0,1,.001f,0},{"doubler","DOUBLER"," %",0,100,.1f,0},{"spread","SPREAD"," %",0,100,.1f,100},{"mix","MIX"," %",0,100,.1f,100},{"output","OUTPUT"," DB",-24,12,.1f,0},{"mono","MONO CHECK","",0,1,1,0,1,{"OFF","ON"}}};
 if(kind==DynKind::Stage)d.push_back({"direct","DIRECT","",0,1,1,1,1,{"OFF","ON"}});
 d.push_back({"bypass","BYPASS","",0,1,1,0,1,{"OFF","ON"}});return d;
}
inline std::vector<Preset> presets(DynKind kind){
 if(kind==DynKind::Stage)return{
 {"WIDE DOUBLES",{{"doubler",48},{"spread",100}}},{"CLOSE LEAD",{{"spread",90},{"doubler",5}}},{"LEFT WHISPER",{{"x",-.6f},{"distance",.2f},{"doubler",15},{"spread",65}}},{"RIGHT ANSWER",{{"x",.6f},{"distance",.18f},{"doubler",20},{"spread",65}}},{"DISTANT ADLIB",{{"distance",.85f},{"doubler",35},{"spread",100}}},{"BACKING CLOUD",{{"distance",.6f},{"doubler",70},{"spread",100}}},{"INTIMATE CENTER",{{"spread",25}}},{"GUITAR LEFT",{{"x",-.5f},{"distance",.3f},{"doubler",25},{"spread",60}}},{"KEYS RIGHT",{{"x",.4f},{"distance",.4f},{"doubler",30},{"spread",85}}},{"WIDE SYNTH",{{"distance",.15f},{"doubler",65},{"spread",100}}},{"SOFT DEPTH",{{"distance",.35f},{"doubler",15},{"spread",70}}},{"CLEAN START",{}}};
 if(kind==DynKind::Buss)return{
 {"RAP BUS GLUE",{{"style",1},{"drive",6},{"trim",-1}}},{"CLEAN CONSOLE",{{"drive",2}}},{"IRON VOCAL",{{"style",1},{"drive",9},{"trim",-1}}},{"VELVET HOOK",{{"style",2},{"drive",8},{"trim",-1}}},{"DRUM DENSITY",{{"style",1},{"drive",12},{"trim",-2}}},{"SOFT MIX BUS",{{"style",2},{"drive",3}}},{"808 WEIGHT",{{"style",1},{"drive",7}}},{"WARM KEYS",{{"style",2},{"drive",10},{"trim",-1}}},{"CRUNCH PARALLEL",{{"style",1},{"drive",22},{"trim",-4}}},{"AIR CONSOLE",{{"style",0},{"drive",5}}},{"VINTAGE TEXTURE",{{"style",1},{"drive",7},{"noise",1}}},{"CLEAN START",{}}};
 if(kind==DynKind::Quad){
  auto make=[](const char*name,std::array<float,4>ranges,std::array<float,4>thr,std::array<float,4>gain){Preset p{name,{}};for(int b=0;b<4;++b){const auto n="b"+std::to_string(b+1);p.values.push_back({n+"range",ranges[b]});p.values.push_back({n+"threshold",thr[b]});p.values.push_back({n+"gain",gain[b]});}return p;};
  return{make("TRAP AIR TAMER",{-2,-2,-4,-6},{-28,-30,-28,-34},{0,0,0,1}),make("RAP FOCUS",{-3,-5,-3,-2},{-28,-32,-26,-30},{-1,-1,1,0}),make("BOOM CONTROL",{-9,-3,0,0},{-32,-30,-24,-24},{0,0,0,0}),make("MUD TAMER",{0,-9,-2,0},{-24,-34,-28,-24},{0,0,0,0}),make("HARSHNESS SOFTEN",{0,0,-8,-3},{-24,-24,-32,-32},{0,0,0,0}),make("BRIGHT HOOK",{-2,-4,-3,-4},{-28,-32,-30,-34},{0,-1,1,2}),make("GENTLE GLUE",{-2,-2,-2,-2},{-28,-28,-28,-28},{0,0,0,0}),make("BASS EVEN",{-10,-4,0,0},{-32,-30,-24,-24},{0,0,0,0}),make("DRUM PUNCH",{3,2,2,1},{-28,-30,-30,-34},{0,0,0,0}),make("DYNAMIC PRESENCE",{0,-2,4,0},{-24,-30,-32,-24},{0,0,0,0}),make("DARK VOCAL",{-2,-2,-3,-4},{-28,-30,-28,-30},{0,0,-1,-3}),make("CLEAN START",{0,0,0,0},{-24,-24,-24,-24},{0,0,0,0})};
 }
 if(kind==DynKind::Vox)return{
  {"TIGHT RAP",{{"gate",-48},{"comp",60},{"output",-5}}},
  {"RAP LEAD",{{"gate",-55},{"comp",48},{"output",-3}}},
  {"SOFT VERSE",{{"gate",-90},{"comp",25},{"output",-1}}},
  {"TRAP LEAD",{{"gate",-52},{"comp",65},{"output",-6}}},
  {"DENSE HOOK",{{"gate",-50},{"comp",76},{"output",-7}}},
  {"WHISPER SUPPORT",{{"gate",-90},{"comp",36},{"output",1}}},
  {"SPOKEN WORD",{{"gate",-56},{"comp",42},{"output",-2}}},
  {"GENTLE LEVEL",{{"gate",-90},{"comp",18},{"output",0}}},
  {"DRY CLOSE VOCAL",{{"gate",-46},{"comp",38},{"output",-2}}},
  {"ADLIB PUSH",{{"gate",-48},{"comp",85},{"output",-9}}},
  {"GATE ONLY",{{"gate",-45},{"comp",0},{"output",0}}},
  {"CLEAN START",{{"gate",-90},{"comp",0},{"output",0}}}};
 return{
  {"SMOOTH RAP",{{"gain",3},{"reduction",44},{"hf",20}}},
  {"VELVET LEAD",{{"gain",3},{"reduction",38},{"hf",10}}},
  {"OPTICAL GLUE",{{"gain",2},{"reduction",30},{"hf",0}}},
  {"TRAP LEVEL",{{"gain",4},{"reduction",58},{"hf",30}}},
  {"SOFT VERSE",{{"gain",1},{"reduction",23},{"hf",10}}},
  {"DENSE CHORUS",{{"gain",5},{"reduction",65},{"hf",15}}},
  {"BREATHY LEAD",{{"gain",2},{"reduction",34},{"hf",55}}},
  {"EDGE CONTROL",{{"gain",2},{"reduction",42},{"hf",85}}},
  {"STRONG LIMIT",{{"gain",3},{"reduction",62},{"limit",1},{"hf",20}}},
  {"BASS LEVEL",{{"gain",3},{"reduction",46},{"hf",0}}},
  {"VINTAGE ROOM",{{"gain",2},{"reduction",37},{"noise",1}}},
  {"CLEAN START",{{"gain",0},{"reduction",0},{"hf",0}}}};
}
}

