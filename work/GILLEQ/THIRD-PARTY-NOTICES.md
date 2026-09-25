# GILLEQ 0.4.0 third-party notices

## JUCE

- Upstream: https://github.com/juce-framework/JUCE
- Version: 8.0.12
- Exact commit: `29396c22c93392d6738e021b83196283d6e4d850`
- This distribution uses the GNU AGPL v3 licensing option for JUCE modules.
- Upstream license statement: `../dependencies/JUCE/LICENSE.md`.

The complete corresponding JUCE source tree is included beside the GILLEQ
directory, without its Git database or local build products. Original copyright
and license notices are preserved in the dependency files. JUCE's own LICENSE.md
lists its bundled dependencies and links to their individual license texts.
Those notices and licenses remain applicable; the project license does not
replace them. VST3 SDK files included in this JUCE revision carry their own MIT
license notices at
`../dependencies/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`.

## Filter equations

GILLEQ's filter implementation is original project code based on publicly
documented biquad equations in the W3C Audio EQ Cookbook, adapted from Robert
Bristow-Johnson's Audio EQ Cookbook:

https://www.w3.org/TR/audio-eq-cookbook/

Version 0.2's band-limited RMS detectors, signed dynamic-gain mapping, envelope
smoothing, routing and tests are original GILLEQ project code. The detectors use
the same publicly documented band-pass/low-pass/high-pass equations. FabFilter
Pro-Q 4 was a user-interface/function reference only; its official user guide is
available at https://www.fabfilter.com/help/pro-q/using/dynamic-eq . The project
does not incorporate a FabFilter implementation and does not claim functional
equivalence.

No proprietary FabFilter, Antares or other commercial plugin implementation is
included. Product names used to describe design references do not imply
affiliation or endorsement.

## Archived GILLEQ test reference

`Tests/DynamicLegacyEqDSP.h` is the unchanged GILLEQ 0.1 DSP header extracted from
the previously distributed GILLEQ source archive. It is compiled only into the
regression test as an independent static-audio reference. It remains covered by
the original project's AGPL-3.0-only notice; it is not an external proprietary
dependency. Its provenance and SHA-256 are recorded in
`Tests/DynamicVerification.md`.

## Build tools and operating-system libraries

MSVC, the Windows SDK, CMake, Ninja and the operating-system runtime are build or
system dependencies. Their installer packages and compiler executables are not
included in this source archive. Obtain them from their respective publishers
under the applicable licenses. The supplied build instructions identify the
versions used for this Windows build.
