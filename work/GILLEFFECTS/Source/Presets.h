#pragma once
#include <array>
namespace gill {
struct SpacePreset {const char* name;float mix,decay,pre,tone,size,width;int style;};
inline constexpr std::array<SpacePreset,12> spacePresets{{
 {"VOCAL ROOM",18,.75f,18,58,35,72,0}, {"TIGHT BOOTH",10,.3f,0,50,12,45,0},
 {"WARM CHAMBER",22,1.35f,12,42,50,78,0}, {"DRUM ROOM",16,.6f,4,70,30,85,0},
 {"SHORT PLATE",20,.95f,14,65,40,90,2}, {"VOCAL PLATE",24,1.8f,32,62,58,95,2},
 {"BRIGHT PLATE",26,2.7f,22,88,65,100,2}, {"LUSH HALL",28,3.8f,40,60,76,100,1},
 {"WIDE STAGE",22,2.6f,30,68,65,100,1}, {"DARK SPACE",35,6.5f,60,24,88,100,1},
 {"AMBIENT BLOOM",50,10,90,48,94,100,1}, {"LONG CATHEDRAL",55,14,110,35,100,100,1}
}};
struct EchoPreset {const char* name;bool sync;int division;float time,feedback,mix,color,width;int style;};
// Division index: 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2, 1/1.
inline constexpr std::array<EchoPreset,12> echoPresets{{
 {"RAP QUARTER",true,4,500,28,18,38,80,0}, {"TRAP PING",true,3,375,42,23,45,100,2},
 {"VOCAL SLAP",false,0,95,8,14,45,45,1}, {"TIGHT DOUBLE",false,0,28,0,15,20,70,0},
 {"EIGHTH CLEAN",true,2,250,25,20,10,80,0}, {"DOTTED AIR",true,3,375,35,24,20,100,2},
 {"TRIPLET FLOW",true,1,167,38,20,40,85,0}, {"WARM TAPE",true,4,500,40,22,65,70,1},
 {"DARK THROW",true,5,750,55,30,80,100,1}, {"WIDE HALF",true,6,1000,45,25,40,100,2},
 {"DREAM REPEATS",true,7,2000,68,35,58,100,2}, {"LONG TRAIL",false,4,1400,72,38,70,90,1}
}};
}
