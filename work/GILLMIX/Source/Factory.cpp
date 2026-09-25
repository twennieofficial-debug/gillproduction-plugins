#include "PluginProcessor.h"
#ifndef GILL_LINK_PRODUCT
#define GILL_LINK_PRODUCT 0
#endif
juce::AudioProcessor*JUCE_CALLTYPE createPluginFilter(){return new GillMixProcessor(GILL_LINK_PRODUCT?GillMixKind::link:GillMixKind::master);}
