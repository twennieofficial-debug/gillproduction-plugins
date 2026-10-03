# Vendored pitch-shifting dependencies

Only the required C++ headers, original license texts, and upstream Stretch README are vendored. No binary dependency, package installation, or checkout is required. Vendor code has not been modified.

| Component | Upstream source | Pinned commit | License |
|---|---|---|---|
| Signalsmith Stretch 1.3.2 | https://github.com/Signalsmith-Audio/signalsmith-stretch | `57b93f4e9206a089a45387eaa39bdc9f310d3308` | MIT, copyright 2022 Geraint Luff / Signalsmith Audio Ltd. |
| Signalsmith Linear | https://github.com/Signalsmith-Audio/linear | `547f4a6c55b4243191a9180f39849d67cc66aa0d` | MIT, copyright 2025 Signalsmith Audio |

Downloaded 2026-09-24 from `https://raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>` after resolving the commit using the official GitHub repository API.

Stretch files: `signalsmith-stretch.h`, `LICENSE.txt`, `README.md`.

Linear files in `signalsmith-linear/`: `approx.h`, `fft.h`, `linear.h`, `stft.h`, `LICENSE.txt`.

`SHA256.json` records hashes of these downloaded source files. Include both original LICENSE.txt files when redistributing either source or compiled binaries containing this library.

Upstream algorithm and API: https://signalsmith-audio.com/code/stretch/ and the pinned README. The API offers approximate spectral formant compensation. This is not a guarantee of transparent monophonic formant preservation.
