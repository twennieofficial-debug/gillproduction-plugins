#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <algorithm>

namespace gill {
// Coordinates bounded statistics learners with the DAW, without storing audio.
// Hosts without transport information support explicit LEARN / FINISH instead.
class SongLearnTransport {
public:
    void reset() noexcept { armed = running = havePosition = false; }
    template <class Engine>
    void before(int command, juce::AudioPlayHead* playHead, int samples, double fs, Engine& engine) noexcept {
        auto position = playHead ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
        if (command == 1) { engine.cancelLearning(); armed = true; running = havePosition = false; }
        if (command == 2) {
            if (running) engine.finishLearning(); else if (armed) engine.cancelLearning();
            reset(); return;
        }
        if (running && engine.learningState() != 1) reset();
        const bool playing = !position || position->getIsPlaying();
        if (running && !playing) { engine.finishLearning(); reset(); return; }
        const auto time = position ? position->getTimeInSeconds() : juce::Optional<double>{};
        if (running && time && havePosition && std::isfinite(*time)
            && std::abs(*time - expectedSeconds) > std::max(0.002, 2.0 / std::max(1.0, fs))) {
            // A loop or seek ends this analysis pass; don't combine unrelated takes.
            engine.finishLearning(); reset(); return;
        }
        if (armed && playing) { engine.startLearning(true); armed = false; running = true; }
        if (running && time && std::isfinite(*time)) {
            expectedSeconds = *time + samples / std::max(1.0, fs); havePosition = true;
        }
    }
    template <class Engine> int state(const Engine& engine) const noexcept {
        return armed ? 4 : engine.learningState();
    }
private:
    bool armed = false, running = false, havePosition = false;
    double expectedSeconds = 0;
};
}
