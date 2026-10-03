# GILLNOTE 0.13.0

Original monophonic vocal transfer editor for the GILLPRODUCTION bundle. This is a VST3 audio effect, not an ARA extension. It does not alter DAW clips automatically.

Build with CMake 3.22+, a C++17 compiler and the pinned JUCE 8.0.12 source in `../dependencies/JUCE` (commit `29396c22c93392d6738e021b83196283d6e4d850`). Shared GILL headers in `../GILLCommon` must accompany this group. Signalsmith headers and both MIT licenses are included in `ThirdParty/signalsmith-stretch`.

```text
cmake -S work/GILLNOTE -B build/NOTE -DCMAKE_BUILD_TYPE=Release
cmake --build build/NOTE --config Release --parallel 2
ctest --test-dir build/NOTE -C Release --output-on-failure
```

Windows local builds optionally reuse the pinned neutral runtime with `GILL_JUCE_EXPORT` and `GILL_PREBUILT_RUNTIME`, as documented by the suite build tooling. Neither shortcut is supported on macOS. The CMake source supports JUCE macOS compilation, but this update's verified deliverable is Windows; no Mac validation is claimed.

Targets: `GILLNOTE_VST3`, `GillNoteTests`. Product code `Gnt1`; manufacturer `Gill`; bundle ID `com.gillproduction.gillnote`. Test `NOTE_INTEGRATION` creates isolated fixtures under its working directory and native editor PNGs at 100%, 150%, and 200% pixel scale.

## Editing workflow

1. Insert on an isolated monophonic vocal, before time-based effects. Select LEARN and play the host. STOP, a transport stop/seek, or 300 seconds ends capture. Monitoring remains dry.
2. Alternatively import a mono/stereo WAV, AIFF or FLAC (8–192 kHz). Only the first 300 seconds are imported. The original file is never modified.
3. Select a detected note. Drag vertically to transpose; Shift gives fine pitch control. Drag the side handles or use NOTE START/END to change the correction region. These boundaries do not stretch time or move audio events.
4. PITCH changes the note centre by up to ±24 semitones. CORRECTION blends the detected intra-note pitch contour toward its centre. Zero preserves the contour; it is not a bypass for a transposed note. SNAP NOTE/ALL use the chosen key and scale. UNDO/REDO retain 60 edit steps.
5. RENDER creates a fresh source-rate, source-channel-count 24-bit WAV with the exact source duration. Neutral edits use a direct sample-copy path. FORMANTS requests Signalsmith spectral formant compensation; extreme shifts may remain audible.
6. AUDITION switches to PRO and previews the current rendered take through the plugin's output. Stop the host or mute its original route while listening if another track also plays the vocal. LIVE and host bypass always interrupt preview and pass the input unchanged. There is no automatic replacement of the host vocal during ordinary playback.
7. DRAG WAV or SAVE WAV transfers the rendered take to the project. The status shows the original timeline start; BWF time reference is included for a known nonnegative start. FL Studio may require placing the WAV manually at that start. Exported files are unique and are never overwritten by edits or project recall.

## State, bounds, and limits

Audio cache: the user's application-data `GILLPRODUCTION/GILLNOTE/Transfer/<32-hex-token>/source.wav`. Project state contains only an owned token, timeline anchor, parameters and edits. It never follows arbitrary paths from project state. Missing cache audio is reported and must be imported/captured again. Keep the cache or exported WAV when moving a project to another computer. Original and exported audio are preserved; cache cleanup is manual.

The audio callback uses fixed capture and audition FIFOs and atomics; analysis, file IO and rendering run on a worker. LIVE/PRO both report zero PDC because note editing is offline. LIVE is exact dry; PRO adds explicit audition only. Five-minute sources are streamed from disk; analysis stores a bounded 12 kHz mono copy and 10 ms pitch frames, rather than five minutes of full-rate stereo in RAM. Changing host rate stops audition/capture safely. Capture requires valid host position; it stops at a transport discontinuity and rejects incomplete FIFO/disk transfers. Finalise with STOP before saving/closing to retain a complete recording. Native QA sets the optional process environment variable `GILL_NOTE_AUDIO_ROOT` to keep test cache audio inside the build directory; production defaults to the application-data cache described above.

The detector is an original normalised autocorrelation implementation, tuned for isolated voiced fundamentals around 60–1000 Hz. It cannot separate chords, overlapping vocals, strong room reflections, or a beat. Short notes and ambiguous/breathy material may need manual boundary/centre correction; detected notes are editable suggestions, not guaranteed transcription. Audition resamples on the worker for host-rate playback; exported audio remains at source rate. All processing is local, without network calls or external models.
