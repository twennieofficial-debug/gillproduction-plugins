# GILLRESTORATION third-party notices

## JUCE and bundled libraries

- Upstream: https://github.com/juce-framework/JUCE
- Version: **8.0.12**
- Commit: `29396c22c93392d6738e021b83196283d6e4d850`
- Licensing option used for JUCE modules: GNU AGPL v3.
- Upstream licensing statement: `../dependencies/JUCE/LICENSE.md`, also at
  https://github.com/juce-framework/JUCE/blob/29396c22c93392d6738e021b83196283d6e4d850/LICENSE.md .

Retain the complete upstream license and copyright notices with the supplied
JUCE source. Its LICENSE.md enumerates bundled dependencies and their separate
terms, including graphics, compression, codec and plugin-format components.
This project's AGPL notice does not replace those terms. The VST3 SDK in this
specific revision carries the MIT License; its full notices remain under
`modules/juce_audio_processors_headless/format_types/VST3_SDK`.

The corresponding source layout places the complete pinned JUCE tree beside
GILLRESTORATION, without local build products or the Git database. Retain
`extras`, which contains build helpers.

## Speech test recordings

`Tests/fixtures/2277-149896-0026.flac` is the unchanged LibriSpeech dev-clean
recording from OpenSLR SLR12: https://www.openslr.org/12/ .

Attribution: Vassil Panayotov, Guoguo Chen, Daniel Povey and Sanjeev Khudanpur,
*LibriSpeech: An ASR corpus based on public domain audio books*, ICASSP 2015.
License: **Creative Commons Attribution 4.0 International**,
https://creativecommons.org/licenses/by/4.0/ .

The derived dry WAV has documented gain, silence padding and sample-rate
conversion. The new corrupted and processed speech WAVs add synthetic
impulses or restoration processing. All speech-containing derivatives retain
CC BY 4.0 with attribution and the description of changes. Original/dry file
hashes, transformation details and deterministic corruption seeds are in
`Tests/fixtures/ORIGIN-AND-LICENSE.md`. No private user recording is included.
The other numerical/percussion inputs are original synthetic test signals.

## Research background and original implementation

The restoration engine is original code using local robust sample-difference
statistics, transient checks and bounded cubic interpolation. Laurent Oudre,
*Automatic Detection and Removal of Impulsive Noise in Audio Signals*, IPOL
2015, https://www.ipol.im/pub/art/2015/64/ , was consulted for background on
impulse detection and interpolation. Neither its implementation nor its
autoregressive model is copied into these plugins.

No trained model and no Waves, iZotope, Antares or FabFilter source code is
included. References to other products do not imply affiliation, endorsement
or equivalent restoration quality.

## Build tools and fonts

MSVC, Windows SDK, CMake, Ninja and the optional standalone-test compiler are
external build tools with their own publisher terms. Their executables and
installers are not part of the intended source distribution. No Python
dependency is required by these restoration tests or plugins.

The interface requests Segoe UI from Windows, with JUCE's normal font
fallback behavior. No Microsoft font file is copied into the project.
