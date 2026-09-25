#pragma once
#include <juce_graphics/juce_graphics.h>

// Font height and all component bounds stay in JUCE logical pixels. The native
// macOS sans-serif avoids depending on a Microsoft-only font installation.
inline juce::String gillInterfaceFontName()
{
   #if JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}
