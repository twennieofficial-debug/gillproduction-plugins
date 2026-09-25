#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillDynamicsProcessor(static_cast<DynKind>(GILL_DYNAMICS_KIND));}
