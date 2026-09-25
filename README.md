# GILLPRODUCTION Plugins

Private source and build automation for the 29 GILLPRODUCTION audio effects.

The existing Windows x64 VST3 modules passed release-04 validation. macOS support is being built for native Apple Silicon and Intel, then combined into Universal VST3 bundles and checked on both platforms. A DMG is produced only after its build and validation gates pass. An unsigned test package is explicitly distinguished from a Developer-ID-signed and Apple-notarized release.

All editors retain their compact logical dimensions. macOS Retina scaling changes physical pixels, not the intended editor size. Generated native screenshots are review artifacts; actual FL Studio UI verification is a separate check.

Sources are in `work/GILL*`. JUCE is pinned as a submodule to 8.0.12 commit `29396c22c93392d6738e021b83196283d6e4d850`. Build instructions and packaging scripts are in `work/packaging/macos`.

Each product's license and third-party notices are included beside its CMakeLists.txt. No personal conversation, account credentials, private recordings, or Windows build binaries are part of this repository.
