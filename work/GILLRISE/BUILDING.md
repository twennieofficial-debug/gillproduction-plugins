# Build

Use CMake 3.22 or newer, C++17, JUCE 8.0.12 at commit
29396c22c93392d6738e021b83196283d6e4d850. Keep `GILLCommon` next to this group.

```
cmake -S GILLRISE -B build-rise -DCMAKE_BUILD_TYPE=Release -DGILL_JUCE_SOURCE_DIR=/absolute/path/to/JUCE
cmake --build build-rise --config Release --parallel 4
ctest --test-dir build-rise -C Release --output-on-failure
```

On macOS select `-DCMAKE_OSX_ARCHITECTURES=arm64` or `x86_64` for native
per-architecture verification, or `arm64;x86_64` for a Universal build. The
minimum target is macOS 11.0. Windows-only runtime shortcuts must not be passed
to the Mac build. The bundle pipeline combines and validates native slices.

`Tests/CMakeLists.txt` is required by the root build. The local Windows helper
uses the pinned neutral runtime and compiler already supplied in this workspace.
Native screenshot tests write PNGs into `Tests`, and WAV export tests write only
to `Tests/Exports`. Those generated artifacts are evidence, not source assets.
