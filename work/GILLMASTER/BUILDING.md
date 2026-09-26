# GILLMASTER – building and verification

Eight master effects are implemented for Windows x64 and macOS Universal VST3. The release pipeline tests the actual binaries before packaging. Version 0.9.0 adds eight unique products while preserving the original 39 bundle plugin IDs.

Build with the repository's pinned JUCE sources, CMake and MSVC/Apple Clang. Shared quality headers reside in ../GILLCommon. Build outputs should use the data drive on this workstation. Native DSP, state/preset, quality-mode, UI and actual VST3 host validation must pass before packaging.

From an x64 MSVC developer prompt, or a Mac with Xcode command line tools:

```
git submodule update --init --recursive
cmake -S work/GILLMASTER -B build-master -DCMAKE_BUILD_TYPE=Release
cmake --build build-master --config Release --parallel 2
ctest --test-dir build-master -C Release --output-on-failure
```

The default Mac build includes arm64 and x86_64 with a macOS 11 deployment target. CI builds and tests each architecture natively, then verifies the merged Universal bundles on both architectures. Windows requires the x64 generator/toolchain. VST3 bundles are under `build-master/<PRODUCT>_artefacts/Release/VST3`.

On this workstation the optional neutral JUCE runtime shortcut is used only for Windows; it is never passed to a Mac build. The standalone DSP subset can also be configured from `work/GILLMASTER/Tests`. The complete suite registers MASTER_INTEGRATION, MASTER_CORE, MASTER_DELTA and MASTER_CEILING. Mac packaging is described in `work/packaging/macos/README.md`.

Loudness/peak implementation references: [ITU-R BS.1770-5](https://www.itu.int/rec/R-REC-BS.1770-5-202311-I), [EBU Tech3341](https://tech.ebu.ch/publications/tech3341). Finite reconstruction is a true-peak estimate; the software does not claim EBU certification. New limiter tests independently reconstruct output peaks rather than comparing a meter with itself.
