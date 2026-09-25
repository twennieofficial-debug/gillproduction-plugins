#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
#if GILL_RESTORATION_KIND == 0
    return new GillRestorationAudioProcessor(gillrestoration::Mode::Declick);
#else
    return new GillRestorationAudioProcessor(gillrestoration::Mode::Decrackle);
#endif
}
