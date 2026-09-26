# GILLRISE 0.10.0

First-syllable reverse-reverb designer for mono or stereo vocal tracks. The
original signal passes through unchanged while the plugin captures or renders.
LIVE and PRO both report zero added latency: this effect is generated offline.

## Use

1. Insert GILLRISE, play from shortly before the desired first syllable. The
   initially armed detector waits for playback. ARM / RECAPTURE starts again.
2. Capture stops after a 120 ms gap or 1.5 seconds of voiced material. FINISH
   can end a connected phrase manually. It retains 100 ms preroll and never
   stores more than 2.5 seconds. A quiet room-noise floor and a 25 ms onset
   gate reject low background hum and isolated clicks. This is an amplitude
   and duration detector, not semantic speech recognition. If it selects a
   breath, noise or the wrong syllable, recapture or adjust START / END.
3. Choose NORMAL or TREMOLO, then LENGTH (0.25–8 s), REVERB, TONE and LEVEL.
   TREMOLO adds adjustable pulse rate and depth. START / END trim the captured
   syllable. Every change re-renders automatically on a worker thread.
4. AUDITION temporarily previews the generated sound through the plugin output.
   STOP PREVIEW or the end of the file returns to the original. Offline host
   exports never include audition playback.
5. DRAG WAV into FL Studio's Playlist, or SAVE WAV. Both create the actual
   stereo 24-bit WAV in `Documents/Jill Plugins/Renders/GILLRISE`. Unchanged
   renders reuse their existing file; changed exports receive a new unique
   name. Files are durable and are never silently deleted by the plugin.

Align the rendered file's **end** to the selected original syllable. When the
host provides a sample position the UI displays exact start/end seconds and
the WAV carries a BWF time reference if that start is non-negative. A generic
VST3 cannot dictate the DAW's drop location. If the rise would begin before
time zero, move the song right to make room and align by its end. A transport
jump ends the current syllable rather than joining unrelated song locations.

The captured source and all controls are saved in the plugin's project state,
so reopening the project can regenerate the rise. A new host sample rate
retains the original capture rate, rendering natively at that rate and
resampling only the audition. Export retains the captured sample rate.

## Presets

- SOFT ENTRANCE — restrained two-second vocal lead-in.
- HOOK LIFT — brighter, longer chorus lift.
- SHORT PICKUP — short phrase transition.
- DARK SWELL — filtered ambience that sits behind the lead.
- TREMOLO RISE — pulsing eight-Hz motion.
- SLOW CINEMATIC — six-second build.

## Rendering and bounds

The original DSP uses an eight-delay Householder feedback network with damping,
90 Hz input high-pass, explicit reverberation decay, reversed output and shaped
fades. Both stereo channels excite the network, so anti-phase input does not
vanish in a mono sum. LEVEL is a sample-peak ceiling with a further 3 dB
reconstruction allowance and a maximum 32× normalization gain. It is not a
promise of perfect loudness matching, true-peak certification or suitability
for every mix. Very quiet/empty trim selections are rejected visibly.

Capture and preview storage are allocated during prepare, never in the audio
callback. Worker renders support cancellation and publish only complete,
current results. Preview ownership uses atomic state transitions. WAV writes,
project serialization and UI waveform construction are off the audio path.
The host audio remains bit-exact in normal capture/idle/bypass operation.

Supported sample rates: 8–192 kHz. Compact rectangular interface: 660×465
logical pixels, with fixed-aspect resizing and native high-DPI rendering.

## Verification

The native suites check detector timing, five-minute armed waiting, bounded
memory, absence of capture heap allocations, noise/click rejection, fades,
ceiling/duration, all render controls, cancellation, deterministic rendering,
stereo cancellation resistance, project recall, real WAV/BWF export, zero-PDC
passthrough, offline audition exclusion, six presets and four native UI scales.
Actual Windows VST3 loading is additionally checked with pluginval level 10.
macOS must still be compiled and tested by the bundle's native CI workflow;
source portability alone is not a claim that a Mac binary was tested.
