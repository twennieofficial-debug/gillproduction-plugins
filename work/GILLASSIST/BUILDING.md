# Building GILLASSIST

C++17 compiler, CMake 3.22 or newer, and the bundle's pinned JUCE 8.0.12 source at `work/dependencies/JUCE`, commit `29396c22c93392d6738e021b83196283d6e4d850`.

From the complete source archive:

```sh
cmake -S work/GILLASSIST -B build/ASSIST -DCMAKE_BUILD_TYPE=Release
cmake --build build/ASSIST --config Release
ctest --test-dir build/ASSIST -C Release --output-on-failure
```

Use Visual Studio 2022/MSVC x64 on Windows, or Xcode command-line tools/Clang on macOS. VST3 is built; no proprietary DSP SDK is needed. A normal source build compiles JUCE. `GILL_JUCE_SOURCE_DIR` can explicitly select the pinned checkout if it is stored elsewhere.

macOS defaults to Universal `arm64;x86_64` and deployment target 11.0. For one tested native slice, configure `-DCMAKE_OSX_ARCHITECTURES=arm64` or `x86_64`. The release pipeline combines independently validated slices and verifies the final Universal files. Do not enable the Windows-only `GILL_JUCE_EXPORT`/`GILL_PREBUILT_RUNTIME` options on Mac.

Native test target: `GillAssistIntegrationTests`; CTest name `ASSIST_INTEGRATION`. Tests write their own generated audio fixtures and editor PNGs inside the Tests directory and use owned test transfer caches. Tests include complete 300-second import and actual streamed capture, exported WAV length/position, editing, state recall, presets, zero-PDC audio and multi-scale UI rendering.

Developer `.cmd` helpers reference this workstation's existing MSVC, Ninja and neutral JUCE runtime; they are convenience recipes, not prerequisites for a clean portable source build. `Tests/validate-windows.py` uses the suite's strict pluginval helper from the full corresponding source archive. Release validation must finish successfully with exit code zero; a printed success followed by a hang is not accepted.

The source is GPLv3 as described in LICENSE and LICENSE-NOTICE.md. JUCE retains its own license; no NoiseWorks model, binary, artwork or proprietary source is a build dependency.
