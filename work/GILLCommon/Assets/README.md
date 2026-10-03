# PRISM BLUE logo asset

`GP-JADE-transparent.png` is the transparent gold GP monogram used by Update13.
It was generated with the built-in image tool from the user's selected
`02-jade-ceramic.png` concept, requesting the isolated top-left GP mark without
the plugin chassis, text, background or external shadow. The original concept
is part of this project's generated concept gallery, not a third-party logo.

Alpha was checked as ranging from0 to255, including transparent outside regions.
`../PrismLogoData.h` embeds these exact PNG bytes so an installed VST3 needs no
external image path. JUCE renders the logo at the editor's current scale.

The common PRISM BLUE chassis and interactive controls are drawn by PrismUi.h,
using the selected `20-prism-blue.png` concept as their visual reference. Audio
curves and meters remain live UI elements; no concept screenshot is used as a
false display of audio data.
