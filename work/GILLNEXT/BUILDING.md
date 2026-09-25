# Build Windows x64

Aktueller Quellstand der Release-Runde 04: **0.1.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

CMake 3.22+, Ninja oder Visual Studio 2022 mit MSVC x64/C++17 und Windows SDK. JUCE 8.0.12 ist unter ../dependencies/JUCE enthalten, Commit 29396c22c93392d6738e021b83196283d6e4d850. Compiler, SDK und kommerzielle Pluginreferenzen sind nicht Teil des Quellpakets.

Aus einer x64 Developer-Konsole:

```
cmake -S GILLNEXT -B build-next -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-next --parallel 2
ctest --test-dir build-next --output-on-failure
```

Die Projekte GILLNEXT und dependencies liegen dabei nebeneinander. Die sechs vollständigen VST3-Verzeichnisse entstehen unter build-next/<PRODUKT>_artefacts/Release/VST3. Den kompletten jeweiligen .vst3-Ordner einschließlich Contents installieren.

GillNextIntegrationTests und GillAlignWrapperTests prüfen Audio, Bedienung und persistente Alignment-Takes. DSP-Tests sind zusätzliche eigenständige C++17-Programme in Tests/.

GILL_JUCE_EXPORT und GILL_PREBUILT_RUNTIME sind optionale lokale Build-Beschleunigungen. Für den normalen vollständigen Quellbuild beide weglassen.
