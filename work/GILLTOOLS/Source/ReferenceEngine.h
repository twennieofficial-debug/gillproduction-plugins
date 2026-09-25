#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <cstdint>
#include <memory>

namespace gill::tools {

// A monitoring player. It never changes the host timeline or reads files in
// process(). LIVE/PRO share this causal monitor path and the same loudness basis.
// Files: WAV/AIFF/FLAC, <=30 minutes, <=2 GiB, one active reader. Three fixed
// stereo playback banks hold 1.5 MiB total; songs are streamed, not embedded.
// MATCH freezes K-weighted RMS after 3 active seconds (no LUFS-conformance
// claim), attenuates the louder side, and restarts when MATCH is re-enabled.
// Snapshot matchGainDb is the actual REF gain including explicit user trim;
// mixGainDb separately reports any attenuation of the user's MIX.
class ReferenceEngine final {
public:
    struct Transport {
        bool playing = false, hasPosition = false, discontinuity = false, offline = false;
        std::int64_t positionSamples = 0;
    };
    struct Parameters {
        bool match = true, mono = false, follow = true, bypass = false;
        float trimDb = 0;
        int channelView = 0; // STEREO / MID / SIDE
        int listenBand = 0;  // FULL / VOICE / LOW / AIR; applied to both sides
    };
    struct SlotInfo {
        juce::String fileName, path, fingerprint, status = "EMPTY";
        bool loaded = false;
        double durationSeconds = 0, loopStartSeconds = 0, loopEndSeconds = 0;
    };
    struct Snapshot {
        std::array<SlotInfo, 3> slots;
        int activeSlot = 0;
        juce::String fileName, status = "EMPTY";
        bool loaded = false, loudnessValid = false, referenceActive = false;
        double durationSeconds = 0, positionSeconds = 0;
        double loopStartSeconds = 0, loopEndSeconds = 0;
        float rmsMix = 0, rmsRef = 0, matchGainDb = 0, mixGainDb = 0;
        std::uint64_t underruns = 0;
    };

    ReferenceEngine();
    ~ReferenceEngine();
    ReferenceEngine(const ReferenceEngine&) = delete;
    ReferenceEngine& operator=(const ReferenceEngine&) = delete;

    // prepare and every file/state operation require a non-audio thread.
    // The host must stop process() before prepare, as for other JUCE processors.
    void prepare(double sampleRate, int maximumBlockSize);
    void requestLoad(int slot, const juce::File&);
    void selectSlot(int slot);
    void clearSlot(int slot);
    void setLoop(double startSeconds, double endSeconds);
    Snapshot snapshot() const;
    juce::ValueTree getState() const;
    void setState(const juce::ValueTree&);

    // Real-time safe: fixed work, atomics, no file I/O, allocation, or locks.
    void setReferenceEnabled(bool enabled) noexcept;
    void reset() noexcept;
    void process(juce::AudioBuffer<float>&, const Transport&, const Parameters&) noexcept;
    static constexpr int latencySamples = 0;
    static constexpr int maximumBufferedFrames = 65536;
    static constexpr int playbackBanks = 3;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gill::tools
