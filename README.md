# GILLPRODUCTION Plugins

Development sources and build automation for 39 GILLPRODUCTION vocal and mix effects (update 07). The 34 existing products retain version 0.6.0; GILLMIX, GILLLINK, GILLHARMONY, GILLREFERENCE and GILLRESCUE use 0.7.0. Build targets are Windows x64 VST3 and macOS Universal VST3 for Apple Silicon and Intel. A new installer is produced only after its native verification gates pass.

[Download Update 07 — 39 plugins, Windows installer and unsigned Mac DMG](https://github.com/twennieofficial-debug/gillproduction-plugins/releases/tag/bundle-test-07). Windows installation and the FL Studio 2025 scan passed. Both native Mac architectures, the same Universal bundles, and the actual Mac package installation passed their automated gates. The Mac build remains an unsigned test version; it has not been tested in FL Studio on macOS.

Sources, presets, tests and artwork are in `work/GILL*`; shared quality and interface helpers are in `work/GILLCommon` and must be included when building any group. Each product group includes `BUILDING.md`. JUCE 8.0.12 is pinned as a submodule under `work/dependencies/JUCE`; initialize submodules before building. See the [Mac build and packaging guide](work/packaging/macos/README.md) for the native build, validation and installer workflow.

The unsigned Mac build is a **test version**: it uses ad-hoc signing, is not Developer-ID-signed or Apple-notarized, and has not been verified in FL Studio on macOS. The pipeline produces a DMG only after the native tests, validation of the same Universal bundles on both architectures, and installer checks pass.

Editors use compact logical window sizes, recorded in [products.json](work/packaging/macos/products.json). Retina scaling changes physical pixels while preserving the intended interface size; generated screenshots support layout review.

Licensing is documented per product in `LICENSE`, `LICENSE-NOTICE.md` and `THIRD-PARTY.md` or `THIRD-PARTY-NOTICES.md`, beside `CMakeLists.txt`. Included third-party libraries retain their own notices. Speech test fixtures have separate attribution and licensing in their `Tests/fixtures/ORIGIN-AND-LICENSE.md` files.
