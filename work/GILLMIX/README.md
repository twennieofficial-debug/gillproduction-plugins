# GILLMIX + GILLLINK 0.10.0

GILLMIX proposes bounded static levels for explicitly connected GILLLINK instances. GILLLINK applies one stored local gain per track. The MIX audio path is dry. Neither plugin controls FL Studio mixer faders or discovers complete host routing.

## Use

1. Put LINK as the **last insert of each selected source track** and set those FL Studio track faders to **0 dB**. Adjust their levels through LINK. Put MIX before the master limiter. Do not also put a LINK on a bus carrying the same already linked sources. A later FL fader, bus gain, pan or send can change what reaches the master; LINK cannot measure those downstream operations. The plugin never moves your FL faders. A role-bus-only workflow is an alternative only when its downstream level is also unity and its sources are not separately linked.
2. Name the links, tick them in MIX, and press CONNECT SELECTED. Set MAIN / DOUBLE / ADLIB / BEAT manually whenever a hint says CHECK. Click anywhere in a row after its checkbox to open the role/lock/manual-level menu (left click also works). One confirmed MAIN and a confirmed BEAT are required.
3. Select a starting preset and play at least eight seconds with aligned main and beat. LEARN waits for playback and measures the complete song, up to five minutes. Transport stop or a seek finishes the current pass; STOP can finish it manually. No 22-second wall-clock timeout interrupts a pass. STOP presents the preview; no gain changes until APPLY.
4. Review role offsets and CHANGE LIMIT. Unticked or locked links are excluded from APPLY. MAIN stays the anchor. UNDO is conditional: it cannot overwrite an intervening local gain edit, recall, lock or metadata change.
5. Replay the result and inspect the actual master peak. A sum peak cannot be predicted exactly from separate RMS readings.

The local LINK LOCK also blocks remote manual changes. NEW ID resolves an intentionally copied LINK. NEW SESSION resolves an intentionally copied controller. After project recall, stored local gains work immediately; runtime remote rights require another explicit CONNECT. Session names are editable in the master header; they are labels, not identity tokens.

## Defined limits

- MIX 760x480; LINK 360x210 logical pixels. Original GP/wood reference artwork and shared cream material controls. Up to 64 connected links per controller; discovery capacity 256 links and 32 controllers in the same host process.
- TRACK LEVEL -24 to +6 dB; learn change limit 0-6 dB, default 3; role offsets -18 to +6 dB. Causal 30 ms gain ramps; both LIVE and PRO report zero added latency.
- LIVE uses inexpensive activity/energy clues. PRO adds bounded periodicity analysis. Gain output is identical at equal settings. Role confidence is a heuristic, not a measured probability or artistic judgement; manual roles win. Audio-only ambiguous roles never silently authorize gain.
- Proposals use the median RMS of active windows, stored in a bounded histogram with 0.1 dB resolution over at most 300 seconds. This is **not** an integrated LUFS meter, semantic vocal model or automatically finished mix. Silence, dropped telemetry, transport discontinuity, bypassed analysis and insufficient aligned context cannot produce an authorized learned set.
- The mapping is local, user/process scoped and contains metadata only, never PCM. Separate host/sandbox processes cannot share a session. Project identity is not inferred from the host PID. Saved UUIDs aid selection but never silently grant remote control.
- APPLY is a confirmed set of individual conditional commits, not a sample-accurate or all-or-none distributed transaction. A receiver timeout or conflict is shown as partial. Already completed changes have conditional UNDO; no blind rollback overwrites later host automation.
- Recall revokes pending controller commands through a lock-free shared lease. A local CAS already completing concurrently remains a completed local edit. Recall invalidates preview/UNDO authority. Historical remote rights are never rearmed from saved state.
- A LINK keeps its last local gain when the controller, message pump or connection disappears. Offline render uses that local parameter and does not depend on live IPC. Creating a new proposal requires an active message pump and aligned transport telemetry.
- On macOS the private temporary mapping remains while its host process exists; a later constructor performs bounded cleanup of files belonging only to dead or reused PIDs. No active mapping is unlinked.

## Build and tests

The group uses pinned JUCE in ../dependencies/JUCE, read-only ../GILLCommon headers, and its own copy of the established bundle artwork. Native source builds support Windows x64 and macOS arm64/x86_64. Only Windows validation is available at this stage; native Mac validation and FL Studio listening/routing acceptance remain separate release gates.

CMake registers MIX_CORE, MIX_INTEGRATION and MIX_CROSS_DLL. Core checks DSP, allocation bounds and protocol state. Integration covers real processors/editors/learning/conditional apply/recall. Cross-DLL loads two independently compiled native modules and a separate child process. Tests/HostCMake builds GillMixRealHost separately: it loads the actual product VST3 bundles, uses their native editor buttons and checks processor state, host caches, learning, APPLY, UNDO and zero-latency output.

The native shared telemetry layout remains compatible; local five-minute analysis statistics are not placed in the shared mapping.

