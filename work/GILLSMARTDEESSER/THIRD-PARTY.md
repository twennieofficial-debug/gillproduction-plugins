# Third-party notices

JUCE 8.0.12, commit `29396c22c93392d6738e021b83196283d6e4d850`, is used under its AGPL v3 option. Full notices remain in `../dependencies/JUCE/LICENSE.md` and the bundled dependency trees. The included VST3 SDK retains its MIT notices. Upstream: https://github.com/juce-framework/JUCE .

The isolated `Source/SmartDeEsserDSP.h` extends the original GILL-DE-ESSER implementation. It adds smoothed absolute threshold, Q and maximum reduction controls. RBJ constant-peak band-pass equations are described in https://www.w3.org/TR/audio-eq-cookbook/ . Learning, state validation, statistics and GUI integration are original GILL code. No commercial plug-in code, external voice dataset or trained model is included.

`Assets/core_reference.png` is the existing GILL project wood/logo reference. The GUI samples the quiet wood and small GP emblem; controls and typography are rendered separately. No Microsoft font file is redistributed. `Source/GillPlatform.h` selects a native system font.

`../GILLCommon/QualityBus.h`, `QualityUi.h`, and `MaterialUi.h` are required original bundle source, provided under the bundle AGPL distribution terms. Corresponding source must include them and the complete pinned JUCE source tree. Tests generate deterministic synthetic harmonics and noise in memory; no private user audio is captured or packaged.
