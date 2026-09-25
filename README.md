# GILLPRODUCTION Plugins

Public source code and build automation for 29 GILLPRODUCTION vocal and mix effects. Build targets are Windows x64 VST3 and macOS Universal VST3 for Apple Silicon and Intel.

[Download the Mac DMG — unsigned test version](https://github.com/twennieofficial-debug/gillproduction-plugins/releases/download/mac-test-05/GILL-PLUGINS-05-MAC-UNSIGNIERT-TESTVERSION.dmg) · [Release notes and checksums](https://github.com/twennieofficial-debug/gillproduction-plugins/releases/tag/mac-test-05). The DMG contains all 29 Universal VST3 plugins for Apple Silicon and Intel.

Sources, presets, tests and artwork are in `work/GILL*`. Each product group includes `BUILDING.md`. JUCE 8.0.12 is pinned as a submodule under `work/dependencies/JUCE`; initialize submodules before building. See the [Mac build and packaging guide](work/packaging/macos/README.md) for the native build, validation and installer workflow.

The unsigned Mac build is a **test version**: it uses ad-hoc signing, is not Developer-ID-signed or Apple-notarized, and has not been verified in FL Studio on macOS. The pipeline produces a DMG only after the native tests, validation of the same Universal bundles on both architectures, and installer checks pass.

Editors use compact logical window sizes, recorded in [products.json](work/packaging/macos/products.json). Retina scaling changes physical pixels while preserving the intended interface size; generated screenshots support layout review.

Licensing is documented per product in `LICENSE`, `LICENSE-NOTICE.md` and `THIRD-PARTY.md` or `THIRD-PARTY-NOTICES.md`, beside `CMakeLists.txt`. Included third-party libraries retain their own notices. Speech test fixtures have separate attribution and licensing in their `Tests/fixtures/ORIGIN-AND-LICENSE.md` files.
