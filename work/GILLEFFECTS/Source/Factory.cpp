#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillEffectProcessor(static_cast<GillKind>(GILL_EFFECT_KIND));}
