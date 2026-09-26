# GILL MASTER SERIES

Eight original mastering effects, version 0.9.0. This directory is currently
under release validation. The existence of source files is not an installation.

- **GILLCEILING** — linked stereo master limiter with CLEAN, PUNCH and LOUD
  characters, drive, ceiling, release, optional level match, integrated loudness
  and final-output peak metering. PRO uses 8x interpolation and 3 ms lookahead;
  LIVE uses a causal sample-peak path. MIX below 100% or bypass returns some/all
  original audio, which can exceed the selected processed-signal ceiling.
- **GILLLOW** — linked low-band dynamic control, adjustable detection threshold,
  transient protection and low-band stereo narrowing. SUB LISTEN is an audition.
- **GILLGLUE** — stereo-linked master bus compression, three timing/ratio
  characters, attack/release, detector bass filter, parallel mix and level match.
- **GILLWIDTH** — complementary three-band mid/side width, crossover controls,
  correlation display/guard, live stereo vectorscope and mono audition.
  The guard reduces added widening when correlation becomes negative; it cannot
  guarantee mono compatibility for already phase-cancelled recordings.
- **GILLPUNCH** — linked three-band attack shaping and shared sustain control,
  with complementary crossovers and parallel mix.
- **GILLWEIGHT** — parallel bass harmonics with warm, even-harmonic and bold
  characters. The amount acts continuously across its travel. PRO oversamples
  the nonlinear branch at 4x and aligns the original signal by 32 samples.
- **GILLDELTA** — paired SOURCE/RETURN comparison across an effect chain.
  Add SOURCE before the chain, RETURN after it, and choose the same PAIR number.
  Play representative audio, then press LEARN on RETURN. Correlation estimates
  up to 500 ms positive delay and a bounded +/-12 dB reference gain correction.
  AFTER is the original RETURN input; BEFORE is the aligned SOURCE; DELTA is
  their difference. Both instances must run in the same host process and signal
  chain. Independent sandboxed host processes do not share the link. A duplicate
  SOURCE reports a conflict. Lost links and offline exports use AFTER. Audition
  is not restored when reopening a project; relearn after changing the chain.
- **GILLDELIVER** — transparent capture of integrated loudness, loudness range,
  finite true/sample peak, clipped frames, duration and start/end silence.
  ANALYZE arms the transport or starts manual capture if transport is unavailable.
  STOP ends capture; REPORT saves a text report. A LIVE/PRO change restarts a
  running capture so one report does not mix peak-measurement methods.

All eight expose the shared GILL LIVE/PRO parameter and accept GILLCONTROL's
session switch. LIVE adds zero algorithmic audio delay for these eight products;
this does not remove the audio interface/DAW buffer. At 48 kHz CEILING PRO reports
176 samples and WEIGHT PRO 32 samples. The remaining six report zero in both modes.
Analysis windows affect when measurements become available, not audio latency.

Each product supplies six starting presets and a compact rectangular walnut,
graphite and brass editor. Default logical sizes are 760x480, 620x340, 420x550,
680x420, 680x360, 400x320, 700x330 and 440x560 respectively. The same logical sizes
are used on Windows and Mac; display scaling is independent of those dimensions.

## Metering methods and tests

Mono/stereo loudness uses K weighting and gated 400 ms blocks at 100 ms intervals
following [ITU-R BS.1770-5](https://www.itu.int/rec/R-REC-BS.1770-5-202311-I).
Loudness range uses 3 s / 100 ms blocks, absolute and relative gates and the
10th-to-95th percentile span following
[EBU Tech 3342](https://tech.ebu.ch/docs/tech/tech3342.pdf).
The histogram resolution is 0.01 LU. PRO true peak is a finite 8x sinc estimate.
These implementations do not claim EBU certification or mathematically exact
infinite-bandwidth reconstruction. Delivery targets are editable checks, not
universal requirements for every music release.

Native tests include bit-exact neutral settings, linked stereo gain, independent
harmonic measurements across 101 amount positions, variable-rate processing,
real-time allocation checks, calibrated mono/stereo loudness, four synthetic EBU
LRA fixtures, independent 16x limiter reconstruction, paired delay/gain/null
tests, preset/state recall and native UI captures at four pixel scales.
Independent pluginval and shared real-VST3 host reports are release gates.
