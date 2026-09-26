# GILLASSIST 0.10.0

Original GILLPRODUCTION vocal transfer editor, Windows/macOS VST3. Rectangular 820 × 520 default UI, shared ivory/wood material renderer, LIVE/PRO quality bus. Both modes report and incur zero processing delay: the whole-song curve is calculated in advance.

## Workflow

1. Insert first on the individual vocal track, click **LEARN**, then play the song. Transfer waits for valid host playback and sample position.
2. Play up to five minutes. Stop the transport or press **STOP**. Seeking while learning ends the contiguous transfer; replay/learn the desired region rather than silently joining unrelated positions.
3. Inspect the real waveform and gain curve. **RIDE**, **GATE**, **BREATH** and **SIBILANCE** independently enable their sections. Parameters rebuild the result in the background; the previous curve continues processing during calculation.
4. Drag a timeline interval vertically in **GAIN EDIT** mode for a local gain correction. Select **PROTECT** to preserve a misclassified breath or consonant; other region modes force gate/breath/sibilance attenuation. UNDO/REDO apply to manual edits, falling back to parameter undo. A/B switches parameter states and manual edit sets.
5. **WAV INS PROJEKT ZIEHEN** drags the fully rendered 24-bit WAV to the DAW. **EXPORT** saves a copy. Place the clip at the transfer start shown in the status; BWF metadata also stores that offset. Bypass/remove GILLASSIST on the rendered clip to avoid processing it twice.

An explicit **IMPORT** chooser accepts mono/stereo WAV, AIFF or FLAC at 8–192 kHz. Import anchors to the last known DAW position and is limited to the first five minutes. Source files are copied to this plugin's durable cache. New takes or rearranged clips require another transfer; a VST3 cannot automatically know that the audio content at an unchanged host position was replaced.

## Processing

- RIDE uses stereo-linked 10-ms energy measurements, centered 80–2000-ms smoothing and a global active-voice reference. The target is explicitly **RMS dBFS**, not LUFS. Range and amount constrain the gain. This does not claim exact LUFS normalization.
- GATE estimates a low-level baseline, uses opening/closing hysteresis, a 30-ms pre-roll, hold and soft tails. It is an acoustic noise gate, not a voice-recognition neural model.
- BREATH and SIBILANCE propose conservative regions using level, high-frequency energy, zero crossings and nearby vocal activity. Confidence limits attenuation. These are acoustic candidates, not semantic/AI classifications; voiced fricatives and breathy singing can require PROTECT/manual correction. Sibilance reduction is broadband clip gain over candidate intervals, not a frequency-selective de-esser.
- A source-based offline sample-peak guard keeps planned output peaks below −1 dBFS. It is not a true-peak-certified limiter and cannot predict later edits to the underlying DAW audio.
- Outside the learned timeline, with unavailable host position, during stopped transport, or under bypass, the original signal passes unchanged.
- LIVE and PRO both use the same precomputed editing curve. The quality switch participates in the bundle controller, but neither mode invents lookahead or processing latency.

## Persistence and memory

Transfer WAV and rendered versions remain in the user application-data folder `GILLPRODUCTION/GILLASSIST/Transfer/<owned-token>/`. Plugin state stores only a validated 32-hex owned-cache token, anchor and manual edits. State does not authorize reading arbitrary filesystem paths. If the cache is absent on another computer, the UI requests a new transfer/import; DAW project files do not embed five minutes of audio. Save exported WAVs with the project when moving machines.

An autosave during capture preserves its actual timeline start and cache identity; after that transfer finishes, the earlier snapshot can restore the completed WAV. Stop/finalize LEARN before closing the project or moving its cache: a live recording is not a completed, portable take, and a crash can leave only the frames already written to disk. Recalling another state cancels the current transfer. Recalling an empty state clears its gain curve, edits and export selection. Previously exported or dragged WAVs are never deleted by this reset.

The audio thread writes to a fixed 1,048,576-frame ring (8 MiB), never to disk. A worker drains it into WAV, analyses streaming frames and renders streaming blocks. Curve buffers are bounded at 30,002 points; no whole five-minute 192-kHz stereo recording is held in RAM. A full 300-second, 192-kHz stereo 24-bit transfer occupies about 346 MB per WAV. If disk throughput cannot keep up or storage fails, capture reports a failed transfer and does not silently present an incomplete result as complete.

## Functional references

The public workflow inspiration is [NoiseWorks Transfer Mode](https://noiseworksaudio.com/blog/how-to-easily-use-dynassist-without-ara/) and [DynAssist feature overview](https://noiseworksaudio.com/blog/gainaimpro-a-short-overview/). Implementation and interface are original. No NoiseWorks proprietary code, model or artwork is used. This is not an ARA implementation or a claim of identical AI results.

## Build

Pinned JUCE 8.0.12, CMake 3.22+, C++17, MSVC on Windows or Xcode/Clang on macOS. `cmake -S work/GILLASSIST -B build/ASSIST -DCMAKE_BUILD_TYPE=Release`; build, then run CTest. Universal macOS defaults to arm64+x86_64 and macOS 11; CI may build/test each slice separately. Optional Windows-only prebuilt neutral JUCE runtime follows the other suite groups.

Native tests cover five-minute analysis/export, late edits, transport, seek, project cache recall, zero-latency dry behavior, presets and native UI at multiple scales. Production status depends on the release's actual recorded CTest/pluginval/macOS validation results.
