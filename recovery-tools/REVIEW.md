# Mac22 packaging recovery review

## Diagnosis and boundaries

The retained attempt1 GILLRACK DMG is 10,445,343 bytes, SHA256
`ccf5c41b5c06576ace6fcd3925f4a62eb15fafd906f2027c46948f7c8c1035d0`.
The preceding successfully mounted GILLPRECISION DMG is 10,446,588 bytes,
SHA256 `e30805b51c9ec12c2fbb295fdf3f146248ab7438a274d09f9442a4c4b33ba8ca`.
`diagnose.py` read both directly from the backed-up failed delivery ZIP. Both
have complete version4 UDIF trailers, exact XML/trailer boundaries, contiguous
decoded block runs, valid compressed streams, and all eight blkx CRC32 checksums
match. UDIF IGNORE chunks correctly contribute no bytes to CRC32. Neither image
is classified as successfully verified or mounted by this offline inspection.

Both images contain Apple_APFS, because the frozen create command omits `-fs`.
The observed native failure is immediate hdiutil verification exit1 ending in
`hdiutil: verify failed - Resource temporarily unavailable`, after create exit0.
The reproducible failure is therefore at Apple's image recognition/verification
layer; it is not explained by a truncated image or broken compressed data. A
specific Apple kernel/helper/resource root cause cannot be proven on Windows.
HFS+ explicitly avoids the automatic APFS container path and is suitable for
the advertised macOS11+ distribution. It is a proposed packaging repair until
the real macOS runner passes all75 original install/verify/mount/byte checks.

## Pass 1 — complete adapter

The adapter imports pipeline.py and individuals.py from an untouched checkout
of frozen74212023dee2cd5581a9db6fec428677c22f533f. It intercepts only hdiutil
create and verify. Create adds `-fs HFS+` to the existing UDZO recipe. Verify
records every actual process exit/output; up to three attempts are allowed
only for exit1 and the exact terminal EAGAIN diagnostic. All other errors and
timeouts abort. No image is regenerated over a partial destination. Native
verify exit0, readonly attach, original shipped-file manifest equality, actual
installer invocation and installed bundle equality remain mandatory.

## Pass 2 — expert improvements

Original upstream artifact IDs, ZIP SHA256 and byte lengths are pinned.
Metadata must identify original run38497542906 and frozen742. Downloaded bytes
are hashed before extraction; unsafe ZIP members and symlinks are rejected;
Universal tar extraction uses Python3.11's data filter. Separately checked-out
tools must be clean and match actual GITHUB_SHA. Source HEAD, clean state and
the exact original source fingerprint are checked before and after packaging.
The original pipeline still creates and verifies the full corresponding source
archive, including its original workflow. Recovery code never enters that source.

## Pass 3 — defects hunted

The native hdiutil EAGAIN observed here has process exit1, not errno35. Retry
classification now uses the exact terminal diagnostic and never treats retry
exhaustion as success. Original test_dmg integration tests exercise a good
readonly mount, failed verify, failed attach and changed PKG bytes. Additional
controls reject wrong source commit/fingerprint, dirty checkout, wrong artifact
identity/run/hash/length and corrupted UDIF data. Actual Universal bundle file
manifests are written before/after and compared. Final recovery provenance binds
the source ZIP, inner Universal tar, universal-build report, tooling file hashes,
workflow hash, native tool logs and final delivery manifest. Provenance is a
separate evidence artifact and does not change the public delivery inventory.

## Pass 4 — clean final review

The adapter has no release publication, remote mutation or default-branch
movement. At root's explicit request the workflow invokes the unchanged frozen
publisher only after adapter success. GitHub only supports job-level permissions;
contents:write is scoped to the packaging/publishing job with no stored checkout
credentials. In particular frozen publish22.py derives
its source commit from GITHUB_SHA and checks it against source.parent HEAD:
the publisher subprocess uses GITHUB_SHA=frozen742, while recovery-report
retains the actual recovery tooling commit. Root must independently compose the
original successful native/validation jobs with this separate successful
packaging/publishing run before final acceptance.

Local recipe tests are not a native Mac install result. G1 proves the tested
adapter and workflow contract; G2 and root's native/delivery gates remain parent
acceptance responsibilities. Requested agent model was gpt-6-astra; no effective
model claim is made without host-confirmed metadata.

## Deployment contract

Root copies these helper files unchanged into `recovery-tools/` on a separate
recovery branch and this template to `.github/workflows/build-macos.yml`.
The source checkout lives at `frozen/`; tooling checkout at `tools/`.
Run output is `$RUNNER_TEMP/mac22-recovery/`. Native evidence is
`recovery-evidence/recovery-report.json`, `frozen-source.json`, both Universal
bundle manifests and `native-tools/`. Delivery is `delivery/`; its source ZIP
is also retained at the output root. No public asset list is expanded.
