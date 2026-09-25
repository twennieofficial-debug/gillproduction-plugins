#include "PluginProcessor.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new GillToolsProcessor(static_cast<ToolsKind>(GILL_TOOLS_KIND));
}
