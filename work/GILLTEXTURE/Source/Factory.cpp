#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new GillTextureProcessor(static_cast<TextureKind>(GILL_TEXTURE_KIND));
}
