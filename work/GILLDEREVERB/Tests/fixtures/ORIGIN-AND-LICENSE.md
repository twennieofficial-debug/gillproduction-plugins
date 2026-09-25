# Speech fixture origin and license

The unchanged file `2277-149896-0026.flac` comes from **LibriSpeech dev-clean**,
OpenSLR resource SLR12. Its archive path is
`LibriSpeech/dev-clean/2277/149896/2277-149896-0026.flac`.

- Dataset: https://www.openslr.org/12/
- Official archive: https://www.openslr.org/resources/12/dev-clean.tar.gz
- License: **Creative Commons Attribution 4.0 International**,
  https://creativecommons.org/licenses/by/4.0/
- Attribution: Vassil Panayotov, Guoguo Chen, Daniel Povey and Sanjeev Khudanpur,
  *LibriSpeech: An ASR corpus based on public domain audio books*, ICASSP 2015.
- Underlying recordings are derived from LibriVox audiobooks. No user recording
  was accessed, recorded, or uploaded.

Only the initial 348,160 compressed bytes of the official archive were streamed
to find this item; the full 322 MiB archive was neither saved nor fetched.
`provenance.json` records the original SHA-256, exact duration, applied gain and
all derived-fixture settings. The archive-wide checksum cannot be verified from
this deliberately partial download; the retained individual-file SHA-256 is a
reproducibility fingerprint, not an upstream authenticity signature.

## Derived files

`dry_16k.wav` contains the source speech, normalized to a peak of -12 dBFS, with
0.5 seconds of leading zeros and 1.8 seconds of trailing zeros. No original
speech was cropped for this particular 4.92-second source. `dry_48k.wav` is a
windowed-sinc interpolation; it does not contain genuine speech information
above the original 8 kHz Nyquist frequency.

`wet_small/medium/large_16k.wav` and their 48 kHz versions are locally convolved
derivatives. Retain the dataset attribution and CC BY 4.0 notice when sharing
them. `early_target_*_16k.wav` and the aligned 48 kHz versions retain the direct
path and reflections before 50 ms as a less aggressive late-reverberation-removal
reference.

`rir_*_16k.wav` are newly generated analytical test signals with four early
reflections and a seeded, frequency-coloured exponentially decaying late field.
They contain no third-party recordings. The nominal late-field RT60 values are
0.25, 0.55 and 0.90 seconds; direct-to-all-reflection energy ratios are +6, 0 and
-3 dB. These are plausible engineering stress fixtures, not measured rooms or
certified acoustic measurements. Reproducible generation is in
`prepare_fixtures.py`; no plugin algorithm or plugin output was used to make
the test inputs.

All generated WAV files are mono IEEE float32. The dataset's word "clean" does
not certify an anechoic recording: original microphone/room colour belongs to
the reference and is not independently known. One speaker and three synthetic
rooms are a bounded regression check, not broad perceptual validation.

The `_python` folder is a fixture-generation-only local Python dependency cache
(SoundFile/CFFI); exclude it from plugin binaries and corresponding-source
packages. The plugin does not depend on those Python files at runtime.
