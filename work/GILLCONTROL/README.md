# GILLCONTROL

Put one instance on a convenient mixer track. **GLOBAL LIVE** or **GLOBAL PRO** updates every registered GILL plugin in the same DAW process. Clicking the selected mode again resynchronizes the instances. The status counts effect instances, excluding controller instances. Its own signal is passed through unchanged with zero added latency.

The saved controller choice is restored after a short registration quiet period. Host automation of the controller also sends a global command. Individual plugin automation and saved state remain local. New instances follow the most recent command while a controller is present; their later project state recall can override it. Two controllers follow the last global command.

Communication is local: an OS-backed memory mapping keyed by current user, process ID and process creation time. Windows mappings have a user-only access descriptor. macOS uses a private 0600 mmap file in the user's system temporary directory. There is no network or cloud access. The message thread polls at 50 ms; the audio callback reads only APVTS atomics. Latency requests are atomically queued and reported to the host on the message thread.

VST3 does not provide this implementation with a project identifier. Concurrent projects hosted in the **same process** share the controller scope; separate host processes are isolated. Sandboxed plugins loaded into different helper processes also have separate scope. Registration is limited to 512 participants; unresponsive slots expire after two minutes and re-register when resumed. An unavailable bus leaves each plugin's local quality selector functional.

LIVE latency depends on each effect; pitch correction and analysis effects can require an unavoidable analysis window. GILLCONTROL does not change a processor's DSP itself: integrated processors implement and report their own LIVE/PRO paths. A mode request is not a promise of zero latency for every effect.
