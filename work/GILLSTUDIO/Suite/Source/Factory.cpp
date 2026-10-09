#include <juce_audio_processors/juce_audio_processors.h>
juce::AudioProcessor* makeStudioProcessor(int kind);
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return makeStudioProcessor(GILL_STUDIO_KIND);
}
