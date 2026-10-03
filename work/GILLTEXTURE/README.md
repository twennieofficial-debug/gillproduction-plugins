# GILLTEXTURE 0.14.0

Three independent VST3 effects for mono or stereo vocals, with the shared PRISM BLUE interface, six factory presets per product, host-automatable controls, complete parameter recall and the GILLCONTROL LIVE/PRO link.

GILLVOCODE is a 16-band envelope vocoder. SAW, PULSE and CHORD use a fixed carrier note (MIDI 24-84); they do not detect, follow or correct the singer's pitch. EXTERNAL uses an optional mono or stereo CARRIER sidechain routed by the host. With no carrier signal, fully wet EXTERNAL output is silent, including the consonant path. FORMANT shifts the synthesis bands, RELEASE changes envelope response, and CONSONANTS preserves speech detail for internal carriers. Presets: CLASSIC ROBOT, DEEP TALK, BRIGHT CIRCUIT, CHORD HOOK, SOFT SYNTH, EXTERNAL CARRIER.

GILLGRAIN creates overlapping windowed grains from recent input, with size, density, pitch, scatter, bounded feedback and stereo width. MIX blends this intentional timing texture with the undelayed original. LIVE uses linear interpolation and PRO uses bounded cubic interpolation. Both process causally and report zero added buffering. Presets: SOFT HALO, OCTAVE SHIMMER, LOW CLOUD, SCATTERED WORDS, DENSE TEXTURE, WIDE SPARKLES.

GILLPULSE shapes the current signal with sixteen editable volume steps. It does not record or retrigger the voice. Four pattern starters change the step levels; STEP RATE, DEPTH, SMOOTH, SWING and PHASE adjust the groove. Available host PPQ determines the pattern position, including seeks and loops; without PPQ the pattern runs freely at host BPM or MANUAL BPM. A stopped host with a known PPQ holds the pattern position. Presets: STRAIGHT SIXTEENTHS, OFFBEAT HOOK, SYNCOPATED RAP, SWUNG GATE, GENTLE PUMP, FAST FLICKER.

All three retain zero additional algorithmic buffer latency in LIVE and PRO. Vocal envelopes and granular timing are audible effect behavior, not compensation buffers. Interface and DAW buffers still contribute to monitoring latency. VOCODE and PULSE use the same causal algorithm in both modes. Native bypass is smoothed; host bypass outputs the original immediately and ramps gently when processing resumes. Conservative reported tail bounds allow DAWs to retain feedback and envelope decay during export.

Original GILL code and the distributed JUCE build are provided under AGPL-3.0; see LICENSE and THIRD-PARTY.md. This group contains no Signalsmith or proprietary third-party DSP.
