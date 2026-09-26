# GILLTOOLS

Three compact vocal and mix tools in the GILLPRODUCTION wood interface family. All names and control labels are uppercase. Each includes six distinct factory starting points, direct numeric entry, bypass and the shared LIVE/PRO selector. Default logical sizes are 540×440 (HARMONY), 660×410 (REFERENCE) and 540×390 (RESCUE); display scaling changes physical pixels rather than opening an oversized editor.

## GILLHARMONY

Creates up to three backing voices from a monophonic vocal or instrument. Select the song's KEY and SCALE, then choose a scale interval or a fixed interval up to one octave for each voice. LEVEL and PAN place the generated voices. DIRECT removes or includes the original voice with one click. NATURAL controls preservation of the spectral envelope; it does not synthesize a different human singer.

Presets: THIRD ABOVE, THIRD BELOW, OCTAVE SHADOW, WIDE HOOK, DARK STACK and THREE VOICE CHOIR. Preset changes preserve the selected key and scale. Scale intervals follow the selected scale; pentatonic steps are explicitly named as scale notes. Unvoiced or unreliable input is not treated as a confident note. Polyphonic chords and whole mixes are not intended inputs.

At 48 kHz, actual added latency is 1,280 samples (26.67 ms) in LIVE and 3,377 samples (70.35 ms) in PRO. This delay is reported to the host. Changing mode changes the processing delay; perform changes between takes where possible. Interface and host buffer latency is additional.

## GILLREFERENCE

Place on the master for monitoring. LOAD your own WAV, AIFF or FLAC in one of three slots, choose a comparison loop, then play the song. MIX / REF switches the monitored signal with a fade. The worker analyses the entire selected reference region, up to 300 seconds. MATCH begins a provisional comparison after three active seconds, then keeps refining across the current song pass until host STOP, a seek or 300 seconds. It attenuates the louder side with smoothed gain; this is K-weighted RMS comparison, not an integrated-LUFS compliance meter. The status distinguishes MEASURING from MATCH READY. Completed measurements remain fixed on subsequent playback. Toggle MATCH off and on to learn another pass. MATCH TRIM permits a ±6 dB adjustment to the reference.

FULL, VOICE, LOW and AIR listening bands and MONO/MID/SIDE checks apply equally to both sources. FOLLOW uses the host timeline; disabling it permits progression independent of seeks. Changing files, loops or recalled state returns reference monitoring to MIX. Files remain on disk; projects store paths and identification, not complete songs. Restore a moved or missing file with LOAD. Supported files are limited to 30 minutes and 2 GiB.

LIVE and PRO add zero samples. The processor forces unaltered MIX during host-declared offline export. A host's real-time export cannot always be distinguished from playback: select MIX and BYPASS before such an export. Listening filters and level matching are monitoring decisions and should not accidentally become part of a print.

## GILLRESCUE

Estimates short peaks flattened by hard clipping. Press LEARN LEVEL, then play the song to estimate separate positive and negative clip boundaries over the full pass, up to five minutes. Press FINISH or stop the host to complete a shorter pass; a seek also finishes the contiguous analysis. The learner waits for playback when armed while stopped. It retains the previous clipping result if an armed pass is canceled and applies newly measured boundaries only when actual repeated flat peaks support them. REPAIR blends the estimated correction; MAX REPAIR bounds its strength. OUTPUT supplies headroom. LISTEN REPAIRS auditions only the changed portion. The display compares aligned samples before output trim.

Only short repeated hard-clipping plateaus are changed. Clean samples are retained; ambiguous boundaries or plateaus longer than the processing budget are left alone. Six presets range from mild correction to a more aggressive hot recording and difference monitoring. This cannot reliably recover lost original detail, soft saturation, or an extensively damaged recording. Re-recording remains the most faithful option when available.

LIVE adds 4 ms and PRO 12 ms (rounded up to whole samples). Both delays are reported to the host. At 48 kHz these are 192 and 576 samples. Automatic input-level learning requires actual evidence of repeated flat peaks and does not invent a clipping level from silence.

## Licensing and testing

See `LICENSE-NOTICE.md`, `LICENSE` and `THIRD-PARTY.md`. Native numeric, state and UI tests live in `Tests`. Release checks apply to exact built binaries; historical test logs alone are not a validation of a modified build. Native Mac host tests do not establish a completed FL Studio test on Mac.
