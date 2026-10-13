#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include "RenderCases.h"
#include "BoolCases.h"
int main(int argc,char**argv){
  juce::ScopedJuceInitialiser_GUI gui;
  if(argc!=4)return 2;
  juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> descriptions;
  format.findAllTypesForFile(descriptions,argv[1]);if(descriptions.size()!=1)return 3;
  juce::String error;auto create=[&](){return format.createInstanceFromDescription(*descriptions[0],48000,128,error);};
  if(juce::String(argv[3])=="bool"){
    auto p=create();if(!p)return 6;
    auto result=runBoolCases(*p);
    if(!juce::File(juce::String(argv[2])).replaceWithText(juce::JSON::toString(result,true)))return 5;
    if(!bool(result["passed"]))return 7;
    std::cout<<"HOST_BOOL_STATE_PASS\n";return 0;
  }
  auto config=juce::JSON::parse(juce::File(juce::String(argv[3])));
  if(!config.isArray()||config.getArray()->isEmpty())return 4;
  auto result=runRenderCases(create,config,juce::File(juce::String(argv[2])));
  if(!juce::File(juce::String(argv[2])).replaceWithText(juce::JSON::toString(result,true)))return 5;
  std::cout<<"HOST_RENDER_COMPLETE\n";
}
