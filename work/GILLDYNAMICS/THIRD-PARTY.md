# Third-party material

JUCE 8.0.12: https://github.com/juce-framework/JUCE, pinned commit 29396c22c93392d6738e021b83196283d6e4d850, AGPL-v3 option. Preserve the complete dependency tree and all notices, including the VST3 SDK MIT license under modules/juce_audio_processors_headless/format_types/VST3_SDK, graphics libraries and build helpers. The source distribution includes these tracked upstream files.

The five processing engines are original local implementations. The synthetic tests use locally generated tones, envelopes, impulses and random sequences; they contain no user vocal recordings. RVox, CLA-2A, NLS and C4 are functional reference names only. No Waves, Antares, sonible, FabFilter or oeksound code or trained model is included; there is no affiliation or identical-sound claim.

The UI uses the existing generated core_reference.png texture/GP mark. Its controls and meters are native graphics. Segoe UI is requested through Windows font fallback; no Microsoft font files are redistributed. MSVC, Windows SDK, CMake, Ninja and pluginval are external build/test tools and are not included in the source archive.
