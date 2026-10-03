#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "TextureDSP.h"
#include <vector>

using TextureKind = gill::texture::Kind;
struct TextureInfo { const char* name; const char* subtitle; int width, height; };
inline TextureInfo textureInfo(TextureKind kind) {
    static constexpr TextureInfo info[]{
        {"GILLVOCODE", "VOICE + CARRIER", 640, 470},
        {"GILLGRAIN", "GRANULAR VOCAL TEXTURE", 640, 430},
        {"GILLPULSE", "RHYTHMIC VOLUME SHAPER", 720, 480}};
    return info[std::clamp(int(kind), 0, 2)];
}
struct TextureParam {
    juce::String id, label;
    float lo = 0, hi = 100, step = .1f, initial = 0;
    juce::String suffix;
    juce::StringArray choices;
    bool hidden = false;
};
inline std::vector<TextureParam> textureParams(TextureKind kind) {
    std::vector<TextureParam> result;
    auto number = [&](const char* id, const char* label, float lo, float hi, float step, float initial, const char* unit) {
        result.push_back({id, label, lo, hi, step, initial, unit, {}, false});
    };
    auto choice = [&](const char* id, const char* label, juce::StringArray options, int initial) {
        result.push_back({id, label, 0, float(options.size()-1), 1, float(initial), "", options, false});
    };
    switch(kind) {
    case TextureKind::Vocode:
        choice("carrier", "CARRIER", {"SAW", "PULSE", "CHORD", "EXTERNAL"}, 0);
        number("note", "CARRIER NOTE", 24, 84, 1, 48, " MIDI");
        number("formant", "FORMANT", -12, 12, .1f, 0, " st");
        number("response", "RELEASE", 0, 100, .1f, 50, " %");
        number("brightness", "BRIGHTNESS", 0, 100, .1f, 60, " %");
        number("unvoiced", "CONSONANTS", 0, 100, .1f, 25, " %");
        break;
    case TextureKind::Grain:
        number("size", "GRAIN SIZE", 15, 240, 1, 80, " ms");
        number("density", "DENSITY", 2, 30, .1f, 8, " /s");
        number("pitch", "PITCH", -12, 12, .1f, 0, " st");
        number("scatter", "SCATTER", 0, 100, .1f, 45, " %");
        number("feedback", "FEEDBACK", 0, 70, .1f, 15, " %");
        number("width", "WIDTH", 0, 100, .1f, 75, " %");
        break;
    case TextureKind::Pulse:
        choice("division", "STEP RATE", {"1/4", "1/8", "1/16", "1/32"}, 2);
        number("depth", "DEPTH", 0, 100, .1f, 100, " %");
        number("smooth", "SMOOTH", 1, 50, .1f, 8, " ms");
        number("swing", "SWING", 0, 50, .1f, 0, " %");
        number("phase", "PHASE", 0, 100, .1f, 0, " %");
        number("bpm", "MANUAL BPM", 20, 400, .1f, 120, " BPM");
        for (int i=0; i<16; ++i)
            result.push_back({"step"+juce::String(i+1), "STEP "+juce::String(i+1), 0, 1, .01f, i%2==0 ? 1.f : 0.f, "", {}, true});
        break;
    }
    number("mix", "MIX", 0, 100, .1f, kind==TextureKind::Grain ? 40.f : 100.f, " %");
    number("output", "OUTPUT", -24, 6, .1f, 0, " dB");
    return result;
}
