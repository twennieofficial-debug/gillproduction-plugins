#include "PluginProcessor.h"
#ifndef GILL_TUNE_LIVE
#define GILL_TUNE_LIVE 0
#endif
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GillVocalProcessor(static_cast<GillKind>(GILL_VOCAL_KIND),GILL_TUNE_LIVE!=0); }
