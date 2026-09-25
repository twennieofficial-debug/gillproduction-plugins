# GILLEFFECTS third-party notices

## JUCE

- Upstream: https://github.com/juce-framework/JUCE
- Version: **8.0.12**
- Commit: `29396c22c93392d6738e021b83196283d6e4d850`
- Licensing option used: GNU AGPL v3.
- Full upstream notice in the corresponding source: `../dependencies/JUCE/LICENSE.md`.

Keep the complete pinned JUCE tree and its upstream copyright/license
notices, including graphics, compression, codecs, plugin-format dependencies
and tracked build helpers under `extras`. This project's notice does not
replace any dependency's individual terms. The VST3 SDK in this exact JUCE
revision carries the MIT License; its full notice remains under
`modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`.

## Original DSP and synthetic tests

These four products use original DSP code in their Source headers:

- GILLAIR: presence/high-band harmonic generation, oversampling filters,
  a delayed clean path and smoothed controls.
- GILLSPACE: input diffusion, early reflections and an eight-line orthogonal
  feedback-delay network, with original room/hall/plate parameter choices.
- GILLECHO: fractional delay lines, read-head crossfades, bounded feedback,
  stereo routing, filtering and antiderivative-antialiased soft saturation.
- GILLBALANCE: fixed-memory spectral statistics and bounded broad-band EQ
  correction toward original tonal targets.

Signalsmith Stretch and Signalsmith Linear are not used by GILLEFFECTS.
No proprietary Slate Digital, FabFilter, sonible or other commercial DSP
source or trained model is included. General workflow inspiration implies
neither affiliation nor identical algorithms or sound quality. Target/style
names do not identify measured models of artists, reference voices or hardware.

The tests generate their own tones, modulated harmonics, impulses and noise.
They contain no private user recording or external speech dataset. Such
fixtures support numerical checks for stated conditions; they are not evidence
of matching an artist or perfectly restoring arbitrary recordings.

## Artwork, fonts and tools

The existing local `Assets/core_reference.png` supplies the Oak Sage wood
surface and small GP mark. The plugin renders its controls, meters and
displays separately. Inclusion of generated artwork does not assert exclusive
rights in that material.

The interface requests Segoe UI through Windows/JUCE font fallback. No
Microsoft font file is bundled. MSVC, Windows SDK, CMake, Ninja and optional
standalone-test compilers remain external tools with their publishers' terms;
their executables and installers are not part of the source package.
