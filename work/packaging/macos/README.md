# GILL Mac20 builds

74 current Universal VST3 products (73 GILL plus separate MOBINA), from current Update19 source. `products.json` freezes factory identity, version and current logical UI size. Two native macOS jobs build and run all registered CTests; real VST3 QualityHost checks all products at 44.1/48/96/192kHz. Both native architectures then validate the exact Universal bytes with pluginval strictness10.

`individuals.package(args)` requires all source and dual-native validation evidence, then produces 74 individual DMG/PKGs plus the separate 73-product GILL bundle. Every final PKG is actually installed on a disposable Mac runner and byte-compared; every final DMG is mounted read-only and its visible contents compared. The final source ZIP is distributed separately and inside the bundle.

The initial Mac20 delivery is ad-hoc VST3 signing, unsigned PKG/DMG, no Apple notarization, no FL Studio listening claim. Native evidence does not prove every macOS/host combination. The deployment target is macOS11. LIVE reports zero additional PDC; analysis windows and host/interface buffers still exist.

Workflow is manually dispatched in the authorized public repository. No private audio, credentials or Windows binaries belong in the source allowlist. See MAC-INSTALLATION.txt.
