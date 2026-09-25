# GILL-DE-ESSER third-party notices

## JUCE and bundled libraries

- Upstream: https://github.com/juce-framework/JUCE
- Version: **8.0.12**
- Commit: `29396c22c93392d6738e021b83196283d6e4d850`
- Licensing option used for JUCE modules: GNU AGPL v3.
- Full upstream licensing statement: `../dependencies/JUCE/LICENSE.md`.
  Pinned online copy:
  https://github.com/juce-framework/JUCE/blob/29396c22c93392d6738e021b83196283d6e4d850/LICENSE.md

The supplied JUCE tree retains its complete upstream license and copyright
notices. Its LICENSE.md lists bundled dependencies and their separate terms,
including graphics, compression, codecs and plugin-format components.
This project's AGPL notice does not replace those terms. The VST3 SDK in this
specific revision carries the MIT License; its notices remain under
`modules/juce_audio_processors_headless/format_types/VST3_SDK`.

The corresponding source layout places the complete pinned JUCE tree beside
GILLDEESSER. It excludes the Git database and local build products but retains
all tracked files, including build helpers under `extras`.

## Filter equations and original implementation

The original DSP uses the public constant-peak bandpass equations described
in Robert Bristow-Johnson's Audio EQ Cookbook, as published by W3C:
https://www.w3.org/TR/audio-eq-cookbook/ . The relevant normalization, digital
band edges and residual-response relationships are implemented in
`Source/DeEsserDSP.h` and checked by `Tests/DeEsserTests.cpp`.

The energy, spectral-ratio and prediction-innovation detector, linked stereo
control and parameter smoothing are original implementation code. No trained
model, cloud service, or Waves, FabFilter, iZotope or Antares source code is
included. Mentioning other products does not imply affiliation, endorsement,
equivalent detection behavior or equivalent audio quality.

## Synthetic audio fixtures

All bundled audio fixtures under `Tests/fixtures` are generated locally by
`Tests/DeEsserTests.cpp`. They contain constructed voiced harmonics and
deterministic, independently FIR-shaped noise bursts, not recordings of
real speakers or private user audio. No external speech dataset is included.

- `synthetic-voice.wav`: constructed voiced-harmonic component.
- `synthetic-excessive-s.wav`: that component plus stronger synthetic noise bursts.
- `synthetic-target.wav`: a defined reference mixture with reduced noise bursts.
- `synthetic-deessed-0.wav`, `synthetic-deessed-55.wav`,
  `synthetic-deessed-100.wav`: the defined input processed at the named amounts.

These fixtures are mono, 48 kHz, 32-bit float WAVs, 2.5 seconds each.
Their exact construction and random seeds are in the included C++ test.
The source manifest contains the hashes of the bundled instances. They share
the original project's licensing to the extent copyright applies.

## Artwork, fonts and build tools

`Assets/core_reference.png` is the existing local Oak Sage / GP project
reference used for the plugin's wood surface and small logo. The code renders
the controls, live spectrum and meters separately. Generated artwork is not
claimed as exclusively owned merely because it is included here.

The interface requests Segoe UI from Windows, with JUCE's normal font fallback.
No Microsoft font file is copied into the project. MSVC, Windows SDK, CMake,
Ninja and optional standalone-test compilers are external tools governed by
their own publisher terms. Their executables and installers are not bundled.
Python is only used by the development-time source-packaging script.
