# Third-party material

JUCE 8.0.12: https://github.com/juce-framework/JUCE, pinned commit 29396c22c93392d6738e021b83196283d6e4d850, AGPL-v3 option. Preserve the complete dependency tree and its notices, including the VST3 SDK MIT license under modules/juce_audio_processors_headless/format_types/VST3_SDK, graphics libraries and build helpers.

The shared quality bus uses platform APIs from Windows and macOS; it does not embed additional libraries. Native graphics use the host operating system's default sans-serif font; no font files are redistributed. Tests synthesize their own audio and contain no user recordings. Compiler toolchains, SDKs and pluginval are external build/test tools.
