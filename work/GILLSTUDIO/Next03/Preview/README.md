# GILLSPECTRA isolated Windows 0.3 candidate

Original GILL development; no vendor equivalence or complete DSEQ coverage is claimed. This Next03 tree does not modify the installed Preview02 or its source/evidence. Scripts here build and test only. VST3 identity stays Gill / Zsp1 / com.gillproduction.preview.spectra.

## Engine-mode contract

PRO buffers 2048 samples. LIVE has no added FIFO/lookahead delay, but coarser frequency selectivity and a causal detector response. The selector stores REQUESTED mode; status separately shows APPLIED mode and a pending message.

prepareToPlay commits the requested mode and matching engine delay synchronously. Subsequent requests during known playback or recording remain pending: current audio delay and reported latency remain unchanged. If an audio callback has observed stopped host transport, the message-thread timer may commit a request. It takes JUCE's callback lock, commits/resets the engine, publishes applied mode, then notifies the host. JUCE's VST3 wrapper uses the same lock, excluding old-mode audio callbacks during the commit. No PDC notification is issued from processBlock.

This guarantees the plugin's ordering. The host still controls when its full project compensation graph is rebuilt. Without transport information or a serviced message loop, requests remain pending until host re-prepare/reload. Stopped status is from the latest callback; the plugin cannot predict future host transport changes. Select mode before recording/playback. Rapid requests may be coalesced or cancelled. The mode switch resets/refills history and is not a seamless effect.

L/R–M/S still has the previous immediate reset/refill policy: change it while stopped. PRO refills 2048 samples; changing basis clears incompatible learning. This remaining constraint is separate from engine/PDC ordering.

## State and realtime boundary

Schema 2 preserves all existing IDs, meanings and fixed 2048-point profile payload, and adds the opt-in level detector and its own node bank. Missing schema and schemas 0/1 are accepted as legacy; newly added processing defaults to zero strength when old state is restored, even into a previously used instance. Future, negative, noninteger or malformed schema is rejected before parameters/profile queues mutate. Variable quality/FFT still needs explicit migration.

Audio owns engine mutation except host-suspended preparation and the callback-locked control transaction above. Profile transfer uses bounded preallocated ownership/generation slots. Independent automated parameters are not a whole-state atomic transaction. Bypass retains continuously processed, aligned dry/wet histories, a 128-sample ramp and finite-input guard.

## Existing processing

Noise-only LEARN accumulates until CLEAR. Disable LEARN before raising DENOISE. LIVE and PRO learn separate representations. NOISE TRACK follows existing noise, and DENOISE TRANS. protects only denoise transients. Resonance works independently; legacy THRESHOLD remains relative prominence, not absolute dBFS.

Twelve reduction nodes and twelve separate detector nodes weight processing, not output EQ. Gain is vertical-only; cuts/bandpass horizontal-only; other shapes both axes. Selecting a node does not enable it. Notch displays negative magnitude, with graph editing -48..0 dB. Effective frequency is clamped for sample rate. DIFFERENCE is aligned dry minus wet before MIX/OUTPUT. The original six presets preserve their previous sound; three added level-dynamics presets demonstrate the opt-in path.

PRISM blue/metal 800x620 layout, all parameter identities and Space-key host policy are retained.

## Verification

Tests/run-transitions.mjs checks actual impulse delay during host latency notification, a deliberately premature-notification negative control, headless pending/reprepare, play/record/stop/cancel/unknown cases, threaded automation and legacy/current/future/malformed schema.

Tests/run-regressions.mjs keeps active bypass, malformed input/profile, recall, graph, presets, callback allocation, raw latency and actual VST3/Pedalboard checks. Reports bind source and binary hashes. Manual FL acceptance of this isolated candidate has not yet happened.

The wrapper exposes opt-in absolute-level threshold, strength, slope, key gain, a separate detector-weighting bank, and an optional mono/stereo external-key bus. Its actual-audio tests and limitations are recorded in ../Source/Spectral/LEVEL-DYNAMICS.md. LEVEL / SIDECHAIN and REDUCTION / DETECTOR select the two control and graph banks. A connected silent key remains silent. LevelHost.cpp compares host-bus processing against the core, checks mono/stereo layouts and sidechain-buffer preservation, state migration and both UI banks. Old presets retain their sound with new strength at zero.

Remaining reference functions include learned threshold curves, additional quality/phase choices and richer analysis/workflow. These stay open. The other twelve requested products now have separate Windows implementations under ../../Suite, with their specific remaining limitations documented there. Tests cover stated cases; they do not establish perfection on every recording.
