#pragma once
#include <vector>
#include <string>
#include <utility>
enum class NextKind { Ride, Clean, Pocket, Align, Form, Finish };
namespace gillnext {
struct ParamSpec {std::string id,name,unit;float lo=0,hi=100,step=.1f,def=0,skew=1;std::vector<const char*>choices;};
struct Preset {const char*name;std::vector<std::pair<std::string,float>>values;};
inline std::vector<ParamSpec> specs(NextKind k){
 std::vector<ParamSpec>d;
 if(k==NextKind::Ride)d={{"target","TARGET"," DB",-36,-6,.1f,-18},{"down","DOWN"," DB",0,18,.1f,12},{"up","UP"," DB",0,18,.1f,9},{"speed","SPEED"," %",0,100,.1f,50},{"hold","HOLD","",0,1,1,0,1,{"OFF","ON"}}};
 if(k==NextKind::Clean)d={{"noise","NOISE"," %",0,100,.1f,30},{"plosives","PLOSIVES"," %",0,100,.1f,30},{"breaths","BREATHS"," %",0,100,.1f,20},{"listen","LISTEN","",0,3,1,0,1,{"OFF","NOISE","PLOSIVES","BREATHS"}}};
 if(k==NextKind::Pocket)d={{"amount","AMOUNT"," %",0,100,.1f,50},{"maxcut","MAX CUT"," DB",0,18,.1f,6},{"low","FOCUS LOW"," HZ",80,4000,1,180,.4f},{"high","FOCUS HIGH"," HZ",1000,16000,1,8000,.5f},{"speed","SPEED"," %",0,100,.1f,50},{"listen","LISTEN","",0,1,1,0,1,{"OFF","ON"}}};
 if(k==NextKind::Align)d={{"tightness","TIGHTNESS"," %",0,100,.1f,75},{"maxshift","MAX SHIFT"," MS",0,250,1,120},{"preview","PREVIEW","",0,1,1,0,1,{"OFF","ON"}}};
 if(k==NextKind::Form)d={{"pitch","PITCH"," ST",-12,12,.01f,0},{"formant","FORMANT"," ST",-12,12,.01f,0},{"mix","MIX"," %",0,100,.1f,100},{"link","LINK","",0,1,1,0,1,{"OFF","ON"}},{"transients","TRANSIENTS","",0,1,1,1,1,{"OFF","ON"}}};
 if(k==NextKind::Finish)d={{"drive","DRIVE"," DB",0,18,.1f,0},{"ceiling","CEILING"," DBTP",-3,0,.1f,-1},{"comp","GLUE"," %",0,100,.1f,20},{"clip","CLIP"," %",0,100,.1f,0},{"width","WIDTH"," %",0,150,.1f,100},{"bassmono","BASS MONO"," HZ",20,250,1,80,.5f},{"low","LOW"," DB",-6,6,.1f,0},{"mid","MID"," DB",-6,6,.1f,0},{"high","HIGH"," DB",-6,6,.1f,0},{"toneon","TONE","",0,1,1,1,1,{"OFF","ON"}},{"compon","GLUE","",0,1,1,1,1,{"OFF","ON"}},{"clipon","CLIP","",0,1,1,1,1,{"OFF","ON"}},{"stereoon","STEREO","",0,1,1,1,1,{"OFF","ON"}},{"limiteron","LIMITER","",0,1,1,1,1,{"OFF","ON"}},{"match","GAIN MATCH","",0,1,1,0,1,{"OFF","ON"}},{"mono","MONO","",0,1,1,0,1,{"OFF","ON"}},{"style","LEARN TARGET","",0,2,1,1,1,{"NATURAL","BALANCED","LOUD"}}};
 d.push_back({"bypass","BYPASS","",0,1,1,0,1,{"OFF","ON"}});return d;
}
inline std::vector<Preset> presets(NextKind k){
 if(k==NextKind::Ride)return{{"RAP LEAD",{}},{"SOFT VERSE",{{"speed",30},{"up",6},{"down",9}}},{"TIGHT TRAP",{{"speed",75},{"up",6},{"down",15}}},{"DYNAMIC HOOK",{{"speed",40},{"up",9},{"down",9}}},{"SPOKEN WORD",{{"target",-20},{"speed",55}}},{"DOWN ONLY",{{"up",0},{"down",12}}},{"GENTLE LIFT",{{"down",4},{"up",4},{"speed",20}}},{"CLEAN START",{{"up",0},{"down",0}}}};
 if(k==NextKind::Clean)return{{"HOME VOCAL",{}},{"LIGHT ROOM NOISE",{{"noise",45},{"plosives",15},{"breaths",0}}},{"CLOSE MIC PLOSIVES",{{"noise",10},{"plosives",80},{"breaths",0}}},{"SOFT BREATHS",{{"noise",10},{"plosives",10},{"breaths",65}}},{"WHISPER SAFE",{{"noise",15},{"plosives",20},{"breaths",0}}},{"RAP CLEANUP",{{"noise",35},{"plosives",60},{"breaths",35}}},{"STRONG NOISE",{{"noise",85},{"plosives",25},{"breaths",20}}},{"CLEAN START",{{"noise",0},{"plosives",0},{"breaths",0}}}};
 if(k==NextKind::Pocket)return{{"RAP VOCAL SPACE",{}},{"TRAP LEAD",{{"amount",70},{"maxcut",8},{"low",250},{"high",6000}}},{"GENTLE MASKING",{{"amount",30},{"maxcut",3}}},{"HOOK CLARITY",{{"amount",60},{"low",1000},{"high",11000}}},{"LOW MID SPACE",{{"low",120},{"high",1800},{"maxcut",5}}},{"BRIGHT VOCAL",{{"low",2000},{"high",14000},{"amount",50}}},{"DENSE BEAT",{{"amount",85},{"maxcut",10},{"speed",65}}},{"CLEAN START",{{"amount",0}}}};
 if(k==NextKind::Align)return{{"NATURAL DOUBLE",{}},{"TIGHT RAP",{{"tightness",95},{"maxshift",100}}},{"SOFT HARMONY",{{"tightness",50},{"maxshift",160}}},{"ADLIB POCKET",{{"tightness",65},{"maxshift",220}}},{"GENTLE NUDGE",{{"tightness",35},{"maxshift",60}}},{"CLEAN START",{{"tightness",0}}}};
 if(k==NextKind::Form)return{{"NATURAL VOICE",{}},{"DARK DOUBLE",{{"formant",-2.5f},{"mix",65}}},{"BRIGHT CHARACTER",{{"formant",2},{"mix",100}}},{"DEEP ADLIB",{{"pitch",-12},{"formant",-3}}},{"HIGH ADLIB",{{"pitch",12},{"formant",2}}},{"TAPE CHARACTER",{{"pitch",-3},{"link",1}}},{"SOFT SHADOW",{{"pitch",-.08f},{"formant",-1},{"mix",35}}},{"ROBOT CHARACTER",{{"pitch",-5},{"formant",4},{"transients",0}}}};
 return{{"BALANCED MASTER",{}},{"NATURAL FINISH",{{"comp",10},{"style",0}}},{"RAP IMPACT",{{"drive",3},{"comp",35},{"clip",12},{"low",.5f},{"high",.5f}}},{"TRAP MASTER",{{"drive",4},{"comp",25},{"clip",18},{"bassmono",100}}},{"WARM GLUE",{{"comp",40},{"high",-.8f}}},{"OPEN AIR",{{"high",1.5f},{"width",108},{"comp",15}}},{"LOUD PREVIEW",{{"drive",6},{"comp",40},{"clip",25},{"style",2}}},{"LIMITER ONLY",{{"comp",0},{"stereoon",0},{"toneon",0},{"clipon",0}}},{"CLEAN START",{{"comp",0},{"limiteron",0},{"stereoon",0}}}};
}
}
