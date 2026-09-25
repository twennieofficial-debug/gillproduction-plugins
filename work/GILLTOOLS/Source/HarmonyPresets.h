#pragma once
#include "HarmonyDSP.h"

namespace gill::tools {
struct HarmonyPreset {const char* name;HarmonyParameters parameters;};
inline std::array<HarmonyPreset,6> harmonyPresets() noexcept {
    std::array<HarmonyPreset,6> result{};
    for(auto& preset:result)for(auto& voice:preset.parameters.voices)voice.enabled=false;
    result[0].name="THIRD ABOVE";result[0].parameters.voices[0]={true,0,2,-9,0};
    result[1].name="THIRD BELOW";result[1].parameters.voices[0]={true,0,-2,-9,0};
    result[2].name="OCTAVE SHADOW";result[2].parameters.voices[0]={true,1,-12,-12,0};
    result[3].name="WIDE HOOK";result[3].parameters.voices[0]={true,0,2,-12,-70};result[3].parameters.voices[1]={true,0,4,-12,70};
    result[4].name="DARK STACK";result[4].parameters.voices[0]={true,0,-2,-12,-45};result[4].parameters.voices[1]={true,1,-12,-15,45};
    result[5].name="THREE VOICE CHOIR";result[5].parameters.voices[0]={true,0,2,-12,-80};result[5].parameters.voices[1]={true,0,4,-12,80};result[5].parameters.voices[2]={true,1,12,-18,0};
    return result;
}
}
