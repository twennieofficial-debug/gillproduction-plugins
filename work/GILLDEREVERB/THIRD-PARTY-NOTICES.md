# GILLDEREVERB third-party notices

## JUCE

- Upstream: https://github.com/juce-framework/JUCE
- Version: 8.0.12
- Exact commit: `29396c22c93392d6738e021b83196283d6e4d850`
- The GNU AGPL v3 licensing option is used for JUCE modules in this distribution.
- Upstream licensing statement: `../dependencies/JUCE/LICENSE.md`.

The complete corresponding JUCE source is supplied beside the GILLDEREVERB
directory without its Git database and local build products. Retain upstream
copyright and license notices. JUCE's LICENSE.md lists its bundled dependencies
and their separate licenses; this project's license does not replace them.
The bundled VST3 SDK in this JUCE revision carries MIT license notices.

## Speech test fixtures

The included `2277-149896-0026.flac` is from LibriSpeech dev-clean, OpenSLR SLR12:
https://www.openslr.org/12/

Attribution: Vassil Panayotov, Guoguo Chen, Daniel Povey and Sanjeev Khudanpur,
*LibriSpeech: An ASR corpus based on public domain audio books*, ICASSP 2015.
License: Creative Commons Attribution 4.0 International,
https://creativecommons.org/licenses/by/4.0/ .

The original recording is retained unchanged. Derived dry/early/wet WAV
references have documented gain, padding, sample-rate conversion and synthetic
room convolution. These spoken-audio derivatives retain CC BY 4.0 attribution.
See `Tests/fixtures/ORIGIN-AND-LICENSE.md` and `provenance.json` for full origin
and transformation details. The analytical RIRs contain no external recordings.
No private user recording was used.

## Scientific and product references

The dereverberation DSP is original statistical signal-processing code. Its
comments identify the delayed exponentially decaying late-reverberation power
model associated with K. Lebart, J. M. Boucher and P. N. Denbigh, *A New Method
Based on Spectral Subtraction for Speech Dereverberation*, Acta Acustica united
with Acustica 87 (2001), pages 359–366.

No Waves, Antares or FabFilter source code or model is included. No GTCRN model
or NARA-WPE implementation is included merely because they appear in research
notes. Product references describe intended workflow comparisons and do not
imply affiliation, endorsement or equivalent performance.

## Tools and system dependencies

MSVC, Windows SDK, CMake and Ninja are external build/system dependencies; their
installers and compiler executables are not part of the source distribution.
Python, NumPy, SoundFile and CFFI are only used by optional fixture-generation
helpers. The local `Tests/fixtures/_python` cache is excluded from the plugin
and source packages. Obtain tools under their respective publisher licenses.
