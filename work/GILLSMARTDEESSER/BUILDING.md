# Building GILLSMARTDEESSER 0.6.0

Source layout: this directory beside `GILLCommon` and the pinned `dependencies/JUCE` tree. Use CMake 3.22+, C++17 and a native Windows MSVC or macOS Xcode/Clang toolchain. Default macOS architectures are arm64;x86_64, deployment target 11.0.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The Windows development build may use `GILL_JUCE_EXPORT` plus an ABI-matching neutral `GILL_PREBUILT_RUNTIME`; these shortcuts are explicitly rejected on macOS. Shipping source includes all dependencies and does not require the local shortcut.

Targets: GILLSMARTDEESSER_VST3; tests DSP, LEARNING, PLUGIN_INTEGRATION. The integration test produces `GILLSMARTDEESSER-UI-480x360.png` in its build working directory. macOS tests initialize NSApplication on the main thread before pumping the real JUCE message loop.

Product code: Gsd1; manufacturer: Gill; bundle ID: com.gillproduction.gillsmartdeesser. All build artifacts should remain outside the source tree.
