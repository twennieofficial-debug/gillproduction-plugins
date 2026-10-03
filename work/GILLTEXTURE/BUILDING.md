# Building GILLTEXTURE

Use CMake 3.22 or newer, a C++17 compiler, `../dependencies/JUCE` and `../GILLCommon`. The default native build compiles the pinned JUCE source. On macOS, the default architectures are arm64 and x86_64 with macOS 11.0 as the minimum target.

```sh
cmake -S GILLTEXTURE -B build-texture -DCMAKE_BUILD_TYPE=Release
cmake --build build-texture --config Release --parallel 2
ctest --test-dir build-texture -C Release --output-on-failure
```

Targets are GILLVOCODE_VST3, GILLGRAIN_VST3 and GILLPULSE_VST3. TEXTURE_DSP runs the independent engine tests; TEXTURE_INTEGRATION runs processor, state, sidechain, bypass, UI and audio-callback allocation checks. The integration suite saves actual editor PNG captures in the Tests directory at 1x and 1.5x scale. The desktop build helpers may use the pinned prebuilt Windows JUCE runtime; those shortcuts are intentionally rejected on other platforms.
