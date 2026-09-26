#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillMasterProcessor(static_cast<MasterKind>(GILL_MASTER_KIND));}
