#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillFinishProcessor(static_cast<FinishKind>(GILL_FINISH_KIND));}
