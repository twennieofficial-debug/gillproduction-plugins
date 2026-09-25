#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GillNextProcessor(static_cast<NextKind>(GILL_NEXT_KIND));}
