# Building GILLTOOLS 0.7.0

This group builds GILLHARMONY, GILLREFERENCE and GILLRESCUE as Windows x64 or macOS Universal VST3 plugins. Include the adjacent `GILLCommon` directory and pinned JUCE 8.0.12 checkout at `../dependencies/JUCE` (commit `29396c22c93392d6738e021b83196283d6e4d850`). The implementation uses C++17 and CMake; no third-party service or API key is required.

For Windows, run in an x64 Visual Studio developer environment:

```sh
cmake -S work/GILLTOOLS -B build-tools -DCMAKE_BUILD_TYPE=Release
cmake --build build-tools --config Release --parallel 2
ctest --test-dir build-tools -C Release --output-on-failure
```

For macOS, install Xcode and CMake, initialize the JUCE submodule, and use a clean build directory:

```sh
cmake -S work/GILLTOOLS -B build-tools -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-tools --config Release --parallel 2
ctest --test-dir build-tools -C Release --output-on-failure
```

The Mac release pipeline builds and tests each native architecture independently before merging and validating the Universal bundles. The Apple visibility settings are essential to prevent interposition between separate JUCE-based plugin modules. Do not pass the optional Windows-only prebuilt-runtime shortcuts to a Mac build.

Outputs are in each product's `_artefacts/Release/VST3` directory. Tests include the processor/editor integration, Harmony DSP, Rescue DSP and Reference DSP. Reference tests generate synthetic audio in a temporary directory. The optional Rescue speech evaluation additionally uses the attributed LibriSpeech fixture in `GILLDEREVERB/Tests/fixtures`; see its license notice.

The distributed Mac build is ad-hoc signed, without Developer ID or Apple notarization. The shared LIVE/PRO protocol remains compatible with the 0.6.0 bundle. See `README.md` for actual latency and functional limits.
