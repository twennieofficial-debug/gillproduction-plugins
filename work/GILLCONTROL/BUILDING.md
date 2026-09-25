# GILLCONTROL 0.6

C++17, CMake 3.22+, JUCE 8.0.12 at `../dependencies/JUCE`, plus sibling `../GILLCommon` are required. Windows: MSVC x64 and Windows SDK. macOS: Xcode command-line tools and macOS 11+ SDK deployment; native or Universal `arm64;x86_64` builds are supported.

```
cmake -S GILLCONTROL -B build-control -DCMAKE_BUILD_TYPE=Release
cmake --build build-control --config Release --parallel 2
ctest --test-dir build-control -C Release --output-on-failure
```

The complete VST3 bundle is under `GILLCONTROL_artefacts/Release/VST3`. Keep the whole bundle intact. Windows-only `GILL_JUCE_EXPORT` and `GILL_PREBUILT_RUNTIME` reuse a matching neutral JUCE runtime for local development; omit both for a source build and on macOS.

Tests load two separately compiled native libraries, check bidirectional shared-mapping visibility, repeated commands, unregister/reopen/stale state, and execute a child process to verify DAW-process isolation. The JUCE integration test checks actual APVTS automation/state, message-thread host gestures and latency notification, compact UI rendering, and bit-exact float/double dry processing.
