# Building GILLMIX and GILLLINK 0.7.0

Include the adjacent `GILLCommon` directory and JUCE 8.0.12 at `../dependencies/JUCE`, pinned to commit `29396c22c93392d6738e021b83196283d6e4d850`. Use CMake 3.22 or newer, C++17, and an x64 Visual Studio developer environment on Windows or Xcode on macOS.

```sh
cmake -S work/GILLMIX -B build-mix -DCMAKE_BUILD_TYPE=Release
cmake --build build-mix --config Release --parallel 2
ctest --test-dir build-mix -C Release --output-on-failure
```

On Mac, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a Universal build. The distribution pipeline instead builds and tests each native architecture, then merges the verified modules. Do not use Windows prebuilt-runtime options on Mac. Keep the configured hidden symbol visibility to isolate JUCE implementations in different modules.

The two VST3 bundles are in their respective `_artefacts/Release/VST3` directories. Registered tests cover the core DSP and protocol, actual processor/editor behavior, and communication between independently loaded native libraries. `Tests/HostCMake` builds a separate host which exercises the actual VST3 bundles with CONNECT, LEARN, APPLY and UNDO. The host requires native GUI support; see the release pipeline for its arguments.

Both plugins report zero added audio latency. MIX passes audio through; LINK applies a smoothed stored gain. See `README.md` for routing, explicit connection and role assignment, and limitations.
