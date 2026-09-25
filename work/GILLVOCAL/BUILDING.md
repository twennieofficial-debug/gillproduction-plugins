# GILLVOCAL 0.4.0 erstellen

Aktueller Quellstand der Release-Runde 04: **0.4.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Das gemeinsame CMake-Projekt baut vier getrennte Windows-x64-VST3-Produkte
**GILLFLOW**, **GILLHEAT**, **GILLTUNE** und **GILLTUNE LIVE** mit eigenen
Plugin-Kennungen. Release-Runde 04 enthält alle vier Produkte als Version 0.4.0, einschließlich GILLFLOW.

## Quellpaket und Voraussetzungen

```text
GILLVOCAL/
  CMakeLists.txt
  Source/
  Assets/
  Tests/
  ThirdParty/signalsmith-stretch/
  LICENSE
  LICENSE-NOTICE.md
  THIRD-PARTY.md
dependencies/JUCE/
```

GILLVOCAL und dependencies bleiben Geschwisterordner. Der reguläre Build
lädt JUCE aus `../dependencies/JUCE` und erstellt seine Laufzeitbibliothek
selbst. **GILL_JUCE_EXPORT** und **GILL_PREBUILT_RUNTIME** beim normalen Build
nicht setzen: Diese optionalen Variablen dienen einem vorbereiteten lokalen
Entwicklungsaufbau, der nicht Voraussetzung für den Quellcode-Build ist.

Benötigt werden CMake ab 3.22, Ninja, C++17, MSVC für x64 und das Windows SDK.
Der Entwicklungsaufbau verwendet MSVC 14.44.35207 / Compiler 19.44 und Windows
SDK 10.0.22621.0. MinGW wird für die JUCE-Windows-VST3-Wrapper nicht unterstützt;
die isolierten DSP-Tests lassen sich jedoch mit geeigneten C++17-Compilern bauen.

Der zugehörige JUCE-Stand ist **8.0.12**, Commit
`29396c22c93392d6738e021b83196283d6e4d850`. Vollständige Quellen und Hinweise
behalten, einschließlich Build-Helfern unter `extras`. Die lokal mitgelieferten
Signalsmith-Dateien unter ThirdParty und ihre MIT-Lizenzen werden für die
historischen Vergleichstests mitgeliefert. Die aktuellen Plugin-Engines sowie
TuneTests.cpp und TuneLiveTests.cpp benötigen sie nicht mehr; TuneQualityTests.cpp,
TuneArtifactTests.cpp und die Vorher/Nachher-Vergleiche verwenden die alte Referenz.
Es ist kein zusätzliches Python-Paket für die Audioverarbeitung nötig.

Einen kurzen Pfad wie `C:\GillSrc` verwenden. Falls ein kurzer Laufwerksalias
genutzt wird, muss er das gesamte Paket einschließlich dependencies abbilden.
Ein Alias ausschließlich für den Build-Ordner kann relative Abhängigkeiten
unbrauchbar machen.

## Windows-Build

Eine x64 Native Tools-Eingabeaufforderung öffnen. CMake und Ninja müssen im
PATH sein. Aus dem obersten Paketordner:

```bat
cmake -S GILLVOCAL -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --parallel 2 --target GILLFLOW_VST3 GILLHEAT_VST3 GILLTUNE_VST3 GILLTUNELIVE_VST3 GillVocalIntegrationTests GillVocalVst3HostTests GillFlowDspTests GillHeatDspTests GillTuneDspTests
ctest --test-dir build -C Release --output-on-failure
```

Zwei parallele Compilerprozesse halten den Speicherbedarf niedriger. Die
Oberflächentests brauchen eine native Windows-Sitzung. Diese Anleitung
beschreibt den Buildweg und behauptet keinen bestandenen Lauf auf einem
anderen Rechner.

Die vollständigen Plugin-Pakete entstehen unter:

```text
build/GILLFLOW_artefacts/Release/VST3/GILLFLOW.vst3
build/GILLHEAT_artefacts/Release/VST3/GILLHEAT.vst3
build/GILLTUNE_artefacts/Release/VST3/GILLTUNE.vst3
build/GILLTUNELIVE_artefacts/Release/VST3/GILLTUNE LIVE.vst3
```

Es erfolgt keine automatische Installation. Jeweils den ganzen `.vst3`-Ordner
einschließlich `Contents` kopieren. Das Projekt erstellt hier keine macOS-
Binärdateien oder Standalone-Anwendungen.

## Testberichte

CTest führt `Flow_DSP`, `Heat_DSP`, `Tune_DSP` und `VOCAL_INTEGRATION` mit
`GILLVOCAL/Tests` als Arbeitsordner aus. Um Konsolenausgaben zusätzlich zu
speichern, aus diesem Tests-Ordner beispielsweise:

```bat
..\..\build\GillFlowDspTests.exe > FlowTests-report.txt
..\..\build\GillHeatDspTests.exe > HeatTests-report.txt
..\..\build\GillTuneDspTests.exe > TuneTests-report.txt
..\..\build\GillVocalIntegrationTests.exe > PluginTests-report.txt
```

Der native Integrationstest prüft STUDIO und LIVE einschließlich des echten
kreisförmigen RETUNE-Ziehens. Der separate VST3-Hosttest erwartet alle vier
gebauten Bundles, einschließlich des FLOW-Testkandidaten. Aus dem obersten Paketordner:

```bat
build\GillVocalVst3HostTests.exe "%CD%\build\GILLFLOW_artefacts\Release\VST3\GILLFLOW.vst3" "%CD%\build\GILLHEAT_artefacts\Release\VST3\GILLHEAT.vst3" "%CD%\build\GILLTUNE_artefacts\Release\VST3\GILLTUNE.vst3" "%CD%\build\GILLTUNELIVE_artefacts\Release\VST3\GILLTUNE LIVE.vst3"
```

Er speichert Berichte im aktuellen Arbeitsordner. Factoryversionen, getrennte
Class-IDs, Parameter, Presets, State und vier Raten mit tatsächlichen PDC-Impulsen
werden geprüft. Es wird keine Audiohardware für hörbare Wiedergabe geöffnet.

Flow schreibt außerdem `flow-dsp-report.json` in den aktuellen Arbeitsordner.
Der tatsächlich ausgeführte Test und seine Version bestimmen die Ergebnisse;
vorhandene Quelltests sind für sich kein Beleg eines bestandenen Laufs.
Die native Prozessor-/UI-Prüfung ersetzt keine separate Prüfung der endgültigen
VST3-Dateien im Validator und in FL Studio.

Der gemeinsame Wrapper akzeptiert 8–384 kHz; andere Raten schalten auf den
gemeldeten, zeitlich passenden Dry-Pfad. Für die Freigabe normale Arbeitsraten
wie 44,1, 48, 88,2, 96 und 192 kHz im nativen Plugin prüfen. Die Flow-Engine-
Tests enthalten zusätzlich die Randfälle 8 und 384 kHz. Einzelne Engine-
Testergebnisse sind keine native Zertifizierung aller Produkte an jeder Rate.

Isolierte Flow-Engine, ohne JUCE, aus dem obersten Paketordner:

```bat
clang++ -std=c++17 -O2 GILLVOCAL\Tests\FlowTests.cpp -o FlowTests.exe
FlowTests.exe
```

Für eine LLVM/MinGW-Toolchain kann `-static` erforderlich sein, damit der
Test ohne separat im PATH liegende Laufzeit-DLLs startet. Entsprechend lassen
sich HeatTests.cpp und TuneTests.cpp bauen. Zusätzliche isolierte LIVE- und
Artefaktprüfungen mit MSVC aus dem obersten Paketordner:

```bat
cl /nologo /std:c++17 /O2 /EHsc /MT GILLVOCAL\Tests\TuneLiveTests.cpp /Fobuild\TuneLiveTests.obj /Febuild\TuneLiveTests.exe
build\TuneLiveTests.exe
cl /nologo /std:c++17 /O2 /EHsc /MT GILLVOCAL\Tests\TuneQualityTests.cpp /Fobuild\TuneQualityTests.obj /Febuild\TuneQualityTests.exe
build\TuneQualityTests.exe
```

Beide Tune-Produkte verwenden den eigenen Zeitbereichskern. LIVE wählt vor
`prepare` den festen Qualitätsmodus 1: aufgerundet 16 ms und 24-Tap-Interpolation.
STUDIO nutzt Modus 0 mit 64 Taps und unveränderter älterer PDC. Ein Wechsel
während des Audiobetriebs ist nicht vorgesehen; die kürzere LIVE-Interpolation
hat andere Hochfrequenz-/Aliasgrenzen. RETUNE 0 bedeutet nicht null Latenz.
Aktuelle Zahlen und Grenzen stehen in `Tests/TUNE-0.3.0-VALIDATION.md` und
`Tests/NATIVE-VOCAL-RESTORATION-V03-VALIDATION.md`; historische TuneDSP-Berichte
sind keine Beschreibung des 0.3.0-Kerns.

## Weitergabe

Zum vollständigen entsprechenden Quellcode gehören CMake-Dateien, Source,
Assets, Tests, Dokumentation, die für historische Tests verwendeten Signalsmith-Dateien samt
MIT-Hinweisen und der vollständige gepinnte JUCE-Quellstand. Compiler,
SDK-Installer, Git-Datenbanken, lokale Objektdateien und Plugin-Binärdateien
sind dafür nicht zu duplizieren. Lizenzbedingungen: LICENSE-NOTICE.md und
THIRD-PARTY.md.
