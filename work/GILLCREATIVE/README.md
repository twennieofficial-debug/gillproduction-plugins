# GILLCREATIVE 0.6.0

Three native JUCE VST3 effects for Windows x64 and macOS (Intel/Apple Silicon).
The CMake macOS default is Universal arm64+x86_64, minimum deployment target 11.0.
Sources build against the bundle's pinned JUCE 8.0.12. These are original signal
processing implementations; no proprietary competitor code or cloud service is used.

## Products

- **GILLPHRASE** — learns voiced passages and the ends of phrases, then raises a
  reverb send around those ends on subsequent playback. The detector uses energy,
  low-frequency content and zero-crossing rate; it does not understand words.
  FOCUS, BLOOM and WIDE offer different tail treatments. Drag marker endpoints
  and strength, or enter START/END BEAT and MARKER LEVEL in the inspector.
- **GILLDIRECTOR** — learns vocal passages and their relative emphasis, with an
  optional mono/stereo BEAT GUIDE sidechain. Three audible trajectories drive its
  own compression, tonal tilt, stereo processing, reverb and tempo delay rack.
  FRONT follows emphasis, LIFT gradually opens the learned passage, and MOTION
  introduces beat-related variation. It does not control other plug-ins.
- **GILLREPLY** — records up to 60 seconds of the incoming stereo vocal, finds
  voiced passages and replays source chops in rhythmic gaps. SINGLE, DOUBLE and
  TRIPLE are deterministic arrangements; DENSITY controls event selection.
  SOURCE IN/OUT edit the original source window of the selected marker; CHOP
  caps its audible length. No words or synthetic speech are invented.

## Learn and playback

Play the intended vocal passage and press LEARN. STOP ends analysis; capture also
stops at 60 seconds. Silent or unsuitable material produces a visible failure,
not an invented profile. Up to 32 phrase markers are retained. The waveform is
an envelope summary, not a spectrogram or word transcript. Inspect the three
variants, edit the timeline and press APPLY. UNDO restores the preceding applied
plan; starting a new take clears the previous undo history.

Learned marker times follow the original host PPQ position. Replay the same song
section to hear them in context. Tempo changes keep beat positions; the captured
source audio itself is not time-stretched. Transport seeks clear old effect tails
and reply voices. A seek during recording cancels the discontinuous take. Without
host PPQ, the engine has an internal 120-BPM playback clock. PREVIEW starts the
candidate plan from its beginning; PHRASE and DIRECTOR still require the original
vocal to be played, while REPLY can audition the recorded chops over silence.

Wet and dry controls are independent on PHRASE/REPLY. DIRECTOR crossfades to its
complete processed rack with WET; DRY scales the unprocessed contribution.
BYPASS fades to unprocessed unity. All sound parameters are host-automatable;
learned plans, source trims and the committed REPLY capture are stored in the
plugin state. The saved capture is embedded float PCM (up to 48 kHz stereo), so
long takes increase project size. Incoming higher sample rates use anti-alias
filtering and downsampling for captured replay; the live dry path remains at the
host rate, as do all other live effects. No temporary audio file, network, account or external speech model is
required.

## Capture memory

Only REPLY retains PCM. Its three fixed capture banks separate the active take,
recording/review take and state import. Each bank holds at most 60 seconds of
stereo 32-bit samples at 48 kHz. Capture is downsampled above 48 kHz with anti-alias
filtering; rates below 48 kHz retain their native rate. The reservation is fixed.

| Product | PCM at a 48 kHz host rate | PCM at a 192 kHz host rate |
| --- | ---: | ---: |
| GILLPHRASE | 0 bytes | 0 bytes |
| GILLDIRECTOR | 0 bytes | 0 bytes |
| GILLREPLY | 69,120,000 bytes / 65.92 MiB | 69,120,000 bytes / 65.92 MiB |

Every product also reserves 72,000 bytes for 6,000 analysis frames (10 ms each),
small marker/mailbox structures, and sample-rate-dependent effect buffers. These
are separate from the PCM figures above. LEARN always ends after at most 60
seconds. Project serialization may temporarily allocate additional memory on
the non-audio thread; a complete 60-second REPLY capture adds roughly 30 MiB of
encoded audio to the saved state.

## LIVE / PRO

`gillQuality` is the shared LIVE=0 / PRO=1 APVTS choice and uses GILLCommon.
Both modes are causal and report **0 extra processing latency**. Intentional
reverb predelay, echo timing and scheduled replies are musical effects, not PDC
latency. PRO adds a second reflection network on PHRASE/DIRECTOR and longer splice
fades on REPLY. A quality change smoothly blends the reflection contribution.

## Development and checks

Build artifacts belong outside the source tree. The local Windows helper uses
`E:/GILLPRODUCTION/Development06/Creative` and reuses the pinned neutral JUCE
runtime. A normal clean CMake build compiles JUCE on macOS.

- `Creative_DSP`: silence rejection, voiced phrase boundaries, real audible
  effects and replies, all reply variants, density, malformed markers, transport
  seeks, 60-second capture limits, 192-to-48-kHz capture, concurrent state imports,
  sample rates, block lengths, non-finite input and audio allocation watch.
- `CREATIVE_INTEGRATION`: all six presets per product, mono/stereo layouts,
  zero reported latency, LIVE/PRO, bypass, parameter/state roundtrips, embedded
  capture recall before preparation and after sample-rate changes, repeated
  recalls, actual controls and timeline gestures, compact UI screenshots.

Default editors: PHRASE/REPLY 720x460, DIRECTOR 760x480 logical pixels. No audio
thread locks, filesystem calls or allocations are used in the processing path.
State serialization and UI producers may allocate and synchronize outside it.

The detector may confuse breath, noise, unusual articulation or background music
with a vocal event. Pre-listening and source-trim correction remain necessary.
This version does not claim perfect semantic editing or replace a listening pass.

## License

GILLCREATIVE is provided under GNU AGPL-3.0 as contained in LICENSE, consistent
with the GILL bundle's JUCE distribution. The pinned JUCE source and its license
must accompany redistribution. The wood/GP artwork is the existing GILL bundle
asset (`core_reference.png`); no third-party plugin skin is copied.
