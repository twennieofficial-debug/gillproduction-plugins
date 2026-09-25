#include "PluginProcessor.h"
juce::AudioProcessor*JUCE_CALLTYPE createPluginFilter(){return new GillCreativeProcessor(static_cast<CreativeKind>(GILL_CREATIVE_KIND));}
