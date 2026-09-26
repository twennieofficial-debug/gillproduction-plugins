# GILLCREATIVE 0.10.0

Three native VST3 effects for Windows x64 and macOS Intel/Apple Silicon, built
with the bundle's pinned JUCE 8.0.12. The processing is original and local. No
competitor code, cloud service, word recognition or generated speech is used.

## Whole-song workflow

1. Put the plugin on the vocal track, move to the intended recording start and
   click LEARN. It arms while the host is stopped; press Play to record.
2. Play the complete song, up to **five minutes**. Host Stop or the plugin's STOP
   keeps the take. A transport seek finalizes the contiguous segment already
   recorded. It never joins different timeline locations into one recording.
3. Review the detected passages. Scroll the mouse wheel over the timeline to
   zoom around the pointer; double-click for the full song. Drag an endpoint to
   change timing and effect strength. The inspector gives precise boundaries.
4. Adjust the controls and a variant. APPLY activates the map for ordinary host
   playback; UNDO restores the preceding applied map. Starting another capture
   starts a new undo history. PREVIEW auditions the candidate timeline; PHRASE
   and DIRECTOR need the original vocal playing, while REPLY uses its chops.
5. RENDER FX makes a WAV containing only the effects, with no dry vocal. RENDER
   MIX makes the complete current dry/wet result. Rendering runs in a background
   worker using PRO quality, includes the effect tail, and attenuates only if
   necessary to keep sample peaks below -0.18 dBFS. This is not true-peak mastering.
6. Drag **DRAG WAV** into the DAW playlist at the capture start. The sidecar text
   records that start in seconds and PPQ beats. The file is capture-relative;
   dragging does not automatically position the clip in the host.

Up to 512 signal-based phrase markers are retained. Silence is not presented as
a successful analysis. The display is an amplitude envelope, not a transcript.
Lower DETECT for a quiet vocal. Background music, breath and noisy consonants can
still need manual marker correction.

## Products and controls

- **GILLPHRASE** raises a reverb send around phrase endings. THROW controls send
  intensity and end-window length. TAIL, TONE, WIDTH, WET and DRY adjust the
  reverb. FOCUS, BLOOM and WIDE provide three tail treatments.
- **GILLDIRECTOR** maps vocal passages and their relative emphasis. Optional
  BEAT GUIDE sidechain information influences learned strengths. Its own rack
  combines compression, tonal tilt, stereo treatment, reverb and tempo delay.
  FRONT follows emphasis, LIFT gradually opens the arrangement, and MOTION adds
  rhythmic variation. RENDER FX exports only reverb/delay contributions; RENDER
  MIX includes the processed vocal. It does not automate other plugins or infer
  semantic verse/chorus labels.
- **GILLREPLY** captures the voice and schedules source chops in gaps. SINGLE,
  DOUBLE and TRIPLE are deterministic rhythmic arrangements. DENSITY chooses
  events; SOURCE IN/OUT sets each original source window, and CHOP caps its
  audible length. No words or voices are synthesized.

All products retain six factory presets, their original plugin IDs and ten
existing parameter IDs. Parameters remain host-automatable. A marker level of
zero disables its contribution. PHRASE/REPLY have independent WET and DRY.
DIRECTOR's WET crossfades the complete rack; DRY scales the unprocessed part.
BYPASS reaches unprocessed unity.

With absolute host song time, the map remains anchored to captured seconds even
after a tempo change. Otherwise host PPQ is used. Reply audio is not time-stretched.
Seeks reset old tails/voices. An internal clock is available without host time.

## Durable audio and recall

Sources and exports remain in `Documents/Jill Plugins/Audio/<PRODUCT>/Sources`
and `.../Exports`. Source WAV is stereo 32-bit float at the native host rate.
The callback writes only to a preallocated SPSC ring; a worker writes files.
Disk/write failures or queue overflow disable the affected capture instead of
presenting a truncated take as complete. The optional `GILL_CREATIVE_AUDIO_ROOT`
environment variable changes storage root; tests use it to isolate their audio.

New project states store markers and the durable source path. Keep those sources
with the project when moving computers. Missing files produce a visible error
requiring LEARN again; parameters still restore. Old schema-1 states with
embedded REPLY audio still load. Saving immediately after stopping, while the
writer is finishing, can temporarily use the legacy embedded fallback for REPLY.

## Memory and latency

PHRASE/DIRECTOR have an approximately 8 MiB ring plus small DSP/analysis buffers;
whole-song PCM goes to disk. REPLY also reserves three bounded float banks for
active/candidate/import takes: 345,600,000 bytes (329.59 MiB) total. Replay storage
is at most 48 kHz with anti-alias filtering above that rate; live dry audio and
WAV sources retain native rate. A background REPLY render uses another bank set.
A five-minute 48 kHz stereo source WAV uses about 115 MB; higher rates use more.

LIVE and PRO implement and report **zero extra processing latency**. Reverb
delays and scheduled replies are effect timing. PRO adds the second reflection
network for PHRASE/DIRECTOR and longer splice fades for REPLY. Reflection quality
changes are smoothly blended. Editors are 720x460 logical pixels (PHRASE/REPLY)
and 760x480 (DIRECTOR), proportionally resizable with shaded ceramic controls.

## Build and tests

`build-update10.cmd` builds into `E:/GILLPRODUCTION/Development10/CREATIVE`.
Normal CMake compiles pinned JUCE from source. macOS defaults to Universal
arm64+x86_64 and minimum macOS 11.

- `Creative_DSP`: all three five-minute limits, final-phrase processing, 300
  phrases, transport arm/stop/seek, variants, density, marker validation,
  anti-alias capture, concurrent imports, sample rates/blocks and allocation watch.
- `CREATIVE_INTEGRATION`: presets/state, source recall, UI gestures, mono/stereo,
  LIVE/PRO, latency, bypass, exact float-WAV capture, wet/full rendering, output
  headroom, durable source recall and native compact screenshots.
- `CREATIVE_FULL_SONG`: real 48 kHz/300-second processor capture, exact
  14,400,000-frame source, 300 markers, time anchor, wet rendering with twelve
  seconds of tail, audible final phrase and complete project recall.

Processing performs no heap allocation, filesystem calls or mutex locking. State
loading/serialization and UI/background workers may allocate, read files and
synchronize outside the callback. Saving immediately after STOP waits for the
source writer to finish its WAV header (bounded at five seconds if disk access
stalls), so a completed analysis does not normally save before its durable audio.
Tests establish measured behavior, not perfect
editing for every recording without listening and adjustment.

## License

The bundle's AGPL-3.0 license and pinned JUCE licensing apply; retain LICENSE and
JUCE sources when redistributing. Wood/GP art is the existing bundle asset. No
third-party plugin skin is copied.
