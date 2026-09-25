# GILLVOCAL third-party notices

## JUCE

- Upstream: https://github.com/juce-framework/JUCE
- Version: **8.0.12**
- Commit: `29396c22c93392d6738e021b83196283d6e4d850`
- Licensing option used: GNU AGPL v3.
- Full upstream notice in the corresponding source: `../dependencies/JUCE/LICENSE.md`.

Preserve the full JUCE tree and its upstream copyright/license notices,
including its graphics, compression, codec and plugin-format dependencies.
This project's notice does not replace their individual terms. The VST3 SDK
in this specific revision carries the MIT License; its notices remain under
`modules/juce_audio_processors_headless/format_types/VST3_SDK`.
Keep the tracked build helpers under `extras` as well.

## Signalsmith Stretch and Signalsmith Linear

The shipping GILLTUNE and GILLTUNE LIVE **0.3.0 do not use Signalsmith**.
Their pitch correction is original period-synchronous time-domain code in
`Source/TuneDSP.h` and `Source/TuneLiveDSP.h`.

The locally supplied Signalsmith pitch/time-stretching library and its Linear
header dependency remain for historical comparison tests using
`Tests/TuneDSP-v020-reference.h`. They were used by the former spectral engine.
The supplied Stretch header declares **version 1.3.2**.

- Signalsmith Stretch: https://github.com/Signalsmith-Audio/signalsmith-stretch
- Signalsmith Linear: https://github.com/Signalsmith-Audio/linear
- Local Stretch license: `ThirdParty/signalsmith-stretch/LICENSE.txt`.
- Local Linear license: `ThirdParty/signalsmith-stretch/signalsmith-linear/LICENSE.txt`.
- Stretch notice: Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.
- Linear notice: Copyright (c) 2025 Signalsmith Audio.
- License for both: **MIT**. Preserve the complete supplied texts.

The included README documents the upstream library API. A release-source
manifest should identify the exact included files and their checksums;
the declared version is not a substitute for preserving the actual historical
source used by those comparison tests. Current plugin-only builds do not need
these headers; the complete source package keeps them so the comparisons remain
reproducible. Preserve their notices when distributing them.

## Original DSP and synthetic tests

GILLFLOW uses a level-dependent gain computer, mode-specific envelope times,
stereo linking, fixed-memory learning statistics and bounded automatic makeup.
GILLHEAT uses complementary band splitting, original waveshaping curves,
oversampling filters, continuously blended distortion and smoothed controls.
GILLTUNE and GILLTUNE LIVE use original pitch detection, scale selection,
time-aligned control and a period-synchronous dual-read-head shifter with
bandlimited interpolation. They do not implement a separate formant model.
Their mode names describe behavior
and do not identify measured reproductions of particular hardware.

The tests construct their own tones, modulated harmonics, impulses and noise.
No private user recording or external speech dataset is included. These
constructed fixtures support numerical tests, not claims about every real
speaker or recording condition.

No proprietary sonible, Antares, Waves, FabFilter or iZotope source code or
trained model is included. Workflow inspiration does not imply affiliation,
endorsement, identical algorithms or equivalent sound quality.

## Artwork, fonts and tools

The existing local `Assets/core_reference.png` supplies the Oak Sage wood
surface and original small GP mark. Controls, meters and displays are
separately rendered by the plugin. Inclusion of generated artwork is not an
assertion of exclusive rights in that material.

The interface requests Segoe UI from Windows using JUCE's font fallback;
no Microsoft font files are bundled. MSVC, Windows SDK, CMake, Ninja and
optional standalone-test compilers are external tools with their own publisher
terms. Their executables and installers are not part of this source package.
