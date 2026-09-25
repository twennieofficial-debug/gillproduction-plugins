# Build Windows x64

Aktueller Quellstand der Release-Runde 04: **0.2.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

CMake 3.22+, Ninja oder Visual Studio 2022 mit MSVC x64/C++17 und Windows SDK. JUCE 8.0.12 ist unter ../dependencies/JUCE enthalten, Commit 29396c22c93392d6738e021b83196283d6e4d850. Compiler, SDK und kommerzielle Pluginreferenzen sind nicht Teil des Quellpakets.

Aus einer x64 Developer-Konsole:

```
cmake -S GILLDYNAMICS -B build-dynamics -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-dynamics --parallel 2
ctest --test-dir build-dynamics --output-on-failure
```

Die Projekte GILLDYNAMICS und dependencies liegen dabei nebeneinander. Die fünf vollständigen VST3-Verzeichnisse entstehen unter build-dynamics/<PRODUKT>_artefacts/Release/VST3. Den kompletten jeweiligen .vst3-Ordner einschließlich Contents installieren.

GillDynamicsVst3HostTests erhält den absoluten Buildordner als einziges Argument. Die nativen Integrationstests rendern die echten Pluginoberflächen als PNG in GILLDYNAMICS/Tests. Pluginval ist ein optionales externes Prüfwerkzeug; die Releaseprüfung verwendet Version 1.0.4, Strictness 10.

Die optionalen CMake-Variablen GILL_JUCE_EXPORT und GILL_PREBUILT_RUNTIME beschleunigen nur den lokalen Entwicklerbuild. Für einen vollständigen Build aus diesem Quellpaket beide weglassen; der vollständige JUCE-Quellcode wird dann mitgebaut.
