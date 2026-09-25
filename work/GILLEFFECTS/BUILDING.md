# GILLEFFECTS 0.3.0 erstellen

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Das CMake-Projekt erzeugt vier getrennte Windows-x64-VST3-Produkte mit eigenen
Plugin-Kennungen: **GILLAIR**, **GILLSPACE**, **GILLECHO**, **GILLBALANCE**.

## Quellpaket und Voraussetzungen

```text
GILLEFFECTS/
  CMakeLists.txt
  Source/
  Assets/
  Tests/
  LICENSE
  LICENSE-NOTICE.md
  THIRD-PARTY.md
dependencies/JUCE/
```

GILLEFFECTS und dependencies bleiben Geschwisterordner. Der normale Build
verwendet `../dependencies/JUCE` und erstellt die JUCE-Laufzeit selbst.
**GILL_JUCE_EXPORT** und **GILL_PREBUILT_RUNTIME** nicht setzen: Diese Optionen
sind ausschließlich für einen vorbereiteten lokalen Entwicklungsaufbau gedacht.

Benötigt werden CMake ab 3.22, Ninja, C++17, MSVC für x64 und das Windows SDK.
Der geprüfte lokale Compiler ist MSVC **19.44.35229**, Toolset
**14.44.35207**, mit Windows SDK **10.0.22621.0**. MinGW wird für die nativen
JUCE-Windows-VST3-Wrapper nicht verwendet; die eigenständigen DSP-Tests können
auch mit anderen geeigneten C++17-Compilern gebaut werden.

JUCE ist auf **8.0.12**, Commit
`29396c22c93392d6738e021b83196283d6e4d850`, festgelegt. Den vollständigen
Quellbaum einschließlich Lizenzen und Build-Helfern unter `extras` behalten.
Diese vier Produkte verwenden **kein Signalsmith**. Es sind keine externen
Audio-DSP-Header oder Python-Pakete für ihre Verarbeitung erforderlich.

Einen kurzen Paketpfad wie `C:\GillSrc` verwenden. Ein Laufwerksalias muss
das gesamte Paket einschließlich dependencies abbilden; ein Alias nur für
den Build-Ordner reicht für relative Abhängigkeiten nicht aus.

## Windows-Build

Eine x64 Native Tools-Eingabeaufforderung öffnen. CMake und Ninja müssen im
PATH liegen. Aus dem obersten Paketordner:

```bat
cmake -S GILLEFFECTS -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --parallel 2 --target GILLAIR_VST3 GILLSPACE_VST3 GILLECHO_VST3 GILLBALANCE_VST3 GillAirDspTests GillSpaceDspTests GillEchoDspTests GillBalanceDspTests GillUpdateDspTests GillEffectIntegrationTests
ctest --test-dir build -C Release --output-on-failure
```

`GillEffectIntegrationTests` wird angelegt, wenn `Tests/PluginTests.cpp` im
Paket enthalten ist. Entsprechend wird `GillEffectVst3HostTests` bei vorhandenem
`Tests/Vst3HostTests.cpp` angeboten; diese separate Prüfung lädt fertige VST3-
Pakete und ist keine Voraussetzung für einen isolierten Engine-Test.

Die nativen Oberflächentests benötigen eine Windows-Sitzung. Zwei parallele
Compilerprozesse begrenzen den Speicherbedarf. Die Anleitung beschreibt den
Buildweg und behauptet keinen bestandenen Lauf auf einem anderen Rechner.

Die fertigen Plugin-Pakete entstehen unter:

```text
build/GILLAIR_artefacts/Release/VST3/GILLAIR.vst3
build/GILLSPACE_artefacts/Release/VST3/GILLSPACE.vst3
build/GILLECHO_artefacts/Release/VST3/GILLECHO.vst3
build/GILLBALANCE_artefacts/Release/VST3/GILLBALANCE.vst3
```

Es erfolgt keine automatische Installation. Jeweils den ganzen `.vst3`-Ordner
mit seiner `Contents`-Struktur kopieren. Dieses Projekt erstellt hier weder
macOS-Binärdateien noch eigenständige Audioanwendungen.

## Engine- und Integrationstests

CTest registriert `Air_DSP`, `Space_DSP`, `Echo_DSP`, `Balance_DSP` sowie, bei
vorhandenem Wrapper-Test, `EFFECTS_INTEGRATION`. Arbeitsordner ist jeweils
`GILLEFFECTS/Tests`. Die Konsolenausgaben lassen sich aus diesem Ordner etwa
so speichern, wenn der Build wie oben neben GILLEFFECTS liegt:

```bat
..\..\build\GillAirDspTests.exe > AirTests-report.txt
..\..\build\GillSpaceDspTests.exe > SpaceTests-report.txt
..\..\build\GillEchoDspTests.exe > EchoTests-report.txt
..\..\build\GillBalanceDspTests.exe > BalanceTests-report.txt
..\..\build\GillEffectIntegrationTests.exe > PluginTests-report.txt
```

Balance schreibt zusätzlich `balance-dsp-report.json`. Dokumentierte
Engine-Verfahren stehen in `Tests/Air-Echo-DSP-notes.md`,
`Tests/SpaceDSP-validation.md` und `Tests/BALANCE-DSP-VERIFICATION.md`.
Die tatsächlichen Host-Presets stammen aus `Source/Presets.h`; frühere
Presetvorschläge in Testnotizen sind kein Ersatz für diese aktuelle Tabelle.

Air und Echo wurden zusätzlich isoliert mit demselben MSVC-Compiler und
statischer Release-Laufzeit wie der Plugin-Build geprüft. Unter `Tests` liegen
`AirTests-msvc-report.txt` und `EchoTests-msvc-report.txt`, dazu jeweils
`*-msvc-build.txt` und `*-msvc-metadata.json` mit Compilerbanner, Optionen,
Ergebnis und SHA-256 der getesteten Quellen. Beispiel aus `GILLEFFECTS/Tests`:

```bat
cl /nologo /Bv /std:c++17 /O2 /Ob2 /DNDEBUG /EHsc /MT /W4 AirTests.cpp /Fe:AirTests-msvc.exe /Fo:AirTests-msvc.obj
AirTests-msvc.exe
cl /nologo /Bv /std:c++17 /O2 /Ob2 /DNDEBUG /EHsc /MT /W4 EchoTests.cpp /Fe:EchoTests-msvc.exe /Fo:EchoTests-msvc.obj
EchoTests-msvc.exe
```

Alle vier DSP-Tests sind unabhängig von JUCE. Vorhandener Testquellcode allein
belegt noch keinen bestandenen Lauf; vorhandene Berichte gelten für die darin
bezeichneten Quellen, Compiler und Konfigurationen. Die Engine-Tests ersetzen
keine native Prüfung der endgültigen VST3-Dateien im Validator oder im Host.

## Weitergabe

Zum vollständigen entsprechenden Quellcode gehören CMake-Dateien, Source,
Assets, Tests, Dokumentation und der vollständige gepinnte JUCE-Quellstand
mit allen enthaltenen Hinweisen. Compiler-/SDK-Installer, deren Programme,
Git-Datenbanken und lokale Objektdateien sind nicht Teil des vorgesehenen
Quellpakets. Hinweise: [LICENSE-NOTICE.md](LICENSE-NOTICE.md) und
[THIRD-PARTY.md](THIRD-PARTY.md).
