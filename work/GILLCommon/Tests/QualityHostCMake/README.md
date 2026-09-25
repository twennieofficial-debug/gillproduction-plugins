# Real VST3 quality integration test

This executable loads independent VST3 bundles through JUCE's native VST3 host.
It does not link any GILL processor implementation or the QualityBus internals.

Build this directory with CMake. A normal JUCE source build is supported on
Windows and macOS. The optional Windows `GILL_PREBUILT_RUNTIME` path is only for
the neutral, statically linked JUCE runtime; the host modules are compiled here.

Run `GillQualityHost --list paths.json --sample-rate 48000 --report report.json`.
`paths.json` accepts an array of bundle paths or an object with a `plugins` array
whose entries have a `path` or `bundle` property. Include one GILLCONTROL and at
least two other GILL VST3 bundles. Each bundle must expose version 0.6.0.

The test checks processor-state quality against the hosted parameter cache,
global commands, a local override, the same global button clicked again through
the native editor, controller closure/recreation and saved-state recall. Its
render callback runs on a worker thread. It also checks finite output and the
controller's bit-exact dry pass-through. Reported host latency is checked in both
modes: LIVE is zero except Tune's 16 ms window and Form's positive window below
25 ms. Factory names, version, JUCE-derived VST3 UIDs and observed latencies are
included in the JSON evidence. These UIDs are host identities, not a claim to
decode the manufacturer's four-character plugin code.

Repeat with 44100, 48000, 96000 and 192000 for the supported sample-rate matrix.
Use a native desktop session: the test briefly creates a small offscreen
controller editor to deliver the repeated button click. macOS initialises a real
NSApplication and uses native AppKit event delivery. Platform execution results
must be recorded separately; a Windows result does not verify the Mac binary.
