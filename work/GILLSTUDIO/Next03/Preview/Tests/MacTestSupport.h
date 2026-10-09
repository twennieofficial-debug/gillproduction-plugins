#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#if JUCE_MAC
extern "C" void gillInitialiseMacTestApplication();
#endif
inline void gillInitialiseTestApplication() {
#if JUCE_MAC
    gillInitialiseMacTestApplication();
#endif
}
