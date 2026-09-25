# Speech fixture origin and license

The unchanged `2277-149896-0026.flac` is from LibriSpeech dev-clean / OpenSLR
SLR12, archive member `LibriSpeech/dev-clean/2277/149896/2277-149896-0026.flac`.
It was already obtained for this project's GILLDEREVERB tests and copied here;
no private user recording was accessed, recorded or uploaded.

- Official dataset: https://www.openslr.org/12/
- Official archive: https://www.openslr.org/resources/12/dev-clean.tar.gz
- Authors: Vassil Panayotov, Guoguo Chen, Daniel Povey and Sanjeev Khudanpur,
  *LibriSpeech: An ASR corpus based on public domain audio books*, ICASSP 2015.
- License: **Creative Commons Attribution 4.0 International**,
  https://creativecommons.org/licenses/by/4.0/

Original FLAC SHA-256:
`3909a6c5ae93112518681fcbf0a74df43615a4f88bd6d5cd788775255296e3f6`.

`dry_48k.wav` retains the complete 4.92-second excerpt, with known gain
−4.6901554786 dB (peak normalized to −12 dBFS), 0.5 seconds of leading silence
and 1.8 seconds of trailing silence. The original 16 kHz recording was
windowed-sinc interpolated to 48 kHz. It has no genuine extra source information
above 8 kHz. This copied reference is byte-identical to the previous fixture:
SHA-256 `82ea61f15a4f54764d3c099190ec6382a5917c4c01453d4ef7cf5912d6963327`.
It is a clean audiobook recording, not a certified anechoic recording.

## New deterministic corruptions and outputs

`Tests/RestorationTests.cpp` generates all new corruptions locally and evaluates
them against the known reference. It does not use plugin output to choose
corruption positions or levels.

- `DECLICK-corrupted.wav`: 26 isolated rectangular disturbances of 1–22 samples,
  amplitudes 0.12–0.42, varied sign; Xorshift32 seed `0x8c5137ad`.
- `DECRACKLE-corrupted.wav`: 500 dense fine impulses of 1–4 samples, amplitudes
  0.014–0.114, varied sign; seed `0x24b86e51`.
- `mouth-click-trains.wav`: 38 irregular bursts, each containing 2–6 short
  bipolar packets. Positive/negative portions each occupy 2–7 samples, with
  short intervening gaps and varied sign/amplitude; seed `0x334f85c1`.
  These are **synthetic impulsive mouth-like click trains**, not recordings
  of real lip smacks or proof of general mouth-noise removal.
- `*-restored.wav`, `*-clean-processed.wav` and `*-mouth-restored.wav`: outputs
  from the corresponding engine mode at Amount 0.55, latency compensated to
  the original reference length.

The generated WAVs are mono IEEE float32, 48 kHz. All files containing this
speech, including corrupted and processed derivatives, retain **CC BY 4.0**
and this attribution/changes notice. Their numerical details may differ at
floating-point rounding level between C++ standard libraries; the generator,
input hash and metric definitions are supplied for reproducibility.

Additional sample-rate tests use a normalized, 64-tap cosine-windowed sinc
interpolator with 512 fractional phases and a 0.95 Nyquist guard. Fresh click
positions and lengths are created after resampling (LCG seed `0x9671acf3`).
The percussion and randomized robustness inputs are original synthetic test
signals with no external recording. The project license applies to original
test code, subject to its terms; it does not relabel the speech as AGPL data.

This small corpus contains one speaker. Consonants and attacks are represented
within the excerpt and synthetic percussion, but many speakers, instruments,
real hardware clicks and real mouth recordings remain untested.
