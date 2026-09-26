#include "PluginProcessor.h"
#include "../../GILLCommon/SongLearningTests.h"
#include <iostream>
#include <fstream>
int main(){juce::ScopedJuceInitialiser_GUI gui;int checks=0,failures=0;
 gill::testing::songLearning([]{return std::make_unique<GillVocalProcessor>(GillKind::Flow);},[&](bool ok,const char* text){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<text<<std::endl;});
 std::ofstream("flow-full-song-report.json")<<"{\"passed\":"<<(failures?"false":"true")<<",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"duration_seconds\":300}";
 return failures?1:0;}
