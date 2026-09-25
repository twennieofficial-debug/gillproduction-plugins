# GILLRESTORATION aus dem Quellcode erstellen

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Dieses Projekt baut **GILLDECLICK 0.3.0** und **GILLDECRACKLE 0.3.0** als
getrennte Windows-x64-VST3-Produkte. Beide verwenden gemeinsame Bibliotheken,
haben aber unterschiedliche Plugin-Kennungen und Verarbeitung. GILLEQ und
GILLDEREVERB sind keine Build-Abhängigkeiten.

## Verzeichnisstruktur

```text
GILLRESTORATION/
  CMakeLists.txt
  Source/
  Assets/
  Tests/
    fixtures/
  LICENSE
  LICENSE-NOTICE.md
  THIRD-PARTY.md
dependencies/
  JUCE/
```

Projekt und dependencies bleiben Geschwisterordner. CMake lädt JUCE regulär
aus `../dependencies/JUCE`. **GILL_JUCE_EXPORT nicht setzen**: Diese optionale
Variable dient nur einem internen bereits konfigurierten Entwicklungsaufbau.
Der normale Build benötigt keine vorbereiteten JUCE-Binärdateien.

Verwendeter JUCE-Stand: **8.0.12**, Commit
`29396c22c93392d6738e021b83196283d6e4d850`. Das entsprechende Quellpaket soll
diesen vollständigen Stand einschließlich seiner Lizenztexte enthalten.
Auch `JUCE/extras` behalten, da dort benötigte Build-Helfer liegen.

## Windows-Werkzeuge

Erforderlich sind CMake ab 3.22, Ninja sowie ein x64-MSVC-Entwicklungsumfeld
mit Windows SDK. Der vorhandene Entwicklungsstand verwendet MSVC
14.44.35207 / Compiler 19.44 und Windows SDK 10.0.22621.0. C++17 ist erforderlich.
MinGW ist kein unterstützter Weg für diese JUCE-Windows-VST3-Wrapper.

Einen kurzen Paketpfad verwenden, etwa `C:\GillSrc`. Lange Windows-
Objektdateipfade können Compilerfehler verursachen. Ein optionaler kurzer
Laufwerksalias muss den gesamten Paketordner einschließlich dependencies
abbilden, nicht nur das Build-Verzeichnis.

Eine x64 Native Tools-Eingabeaufforderung öffnen; CMake und Ninja müssen im
PATH liegen. Aus dem obersten Paketverzeichnis:

```bat
cmake -S GILLRESTORATION -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --parallel 2 --target GILLDECLICK_VST3 GILLDECRACKLE_VST3 GillRestorationDspTests GillRestorationIntegrationTests GillSuiteVst3HostTests
ctest --test-dir build -C Release --output-on-failure
```

Die Begrenzung auf zwei parallele Compilerprozesse ist für Rechner mit wenig
RAM gedacht. Die Befehle sind eine Anleitung, kein behauptetes Testergebnis
auf einem anderen Rechner. Eine native Windows-Sitzung wird für die
JUCE-Oberflächenprüfung benötigt.

## Ergebnisse und Installation

Die vollständigen VST3-Pakete entstehen unter:

```text
build/GILLDECLICK_artefacts/Release/VST3/GILLDECLICK.vst3
build/GILLDECRACKLE_artefacts/Release/VST3/GILLDECRACKLE.vst3
```

CMake installiert sie nicht automatisch. Für die Installation jeweils den
gesamten `.vst3`-Ordner mit `Contents` kopieren; Zielbeispiele stehen in
README.md. Dieses CMake-Projekt baut keine eigenständige Standalone-Anwendung
und keine macOS-Binärdateien.

## Testläufe

CTest führt `RESTORATION_DSP` und `RESTORATION_INTEGRATION` mit
`GILLRESTORATION/Tests` als Arbeitsordner aus. Der DSP-Test liest
`fixtures/dry_48k.wav`, erzeugt deterministische beschädigte/verarbeitete WAVs
und schreibt `restoration-dsp-report.json`. Die Integrationsprüfung schreibt
`plugin-integration-report.json` und Oberflächenbilder in diesen Arbeitsordner.

Der zusätzliche tatsächliche VST3-Ladetest wird für jedes Produkt separat
aufgerufen; er gehört nicht zu den zwei CTest-Einträgen. Ebenfalls aus dem
obersten Paketverzeichnis, in einer Windows-Eingabeaufforderung:

```bat
build\GillSuiteVst3HostTests.exe "%CD%\build\GILLDECLICK_artefacts\Release\VST3\GILLDECLICK.vst3"
build\GillSuiteVst3HostTests.exe "%CD%\build\GILLDECRACKLE_artefacts\Release\VST3\GILLDECRACKLE.vst3"
```

Diese Aufrufe schreiben `GILLDECLICK-vst3-host-report.json` beziehungsweise
`GILLDECRACKLE-vst3-host-report.json` in den aktuellen Ordner. Sie prüfen unter
anderem Laden, Identität, Zustandsserialisierung sowie gemeldete und gemessene
Impulsverzögerung. Sie sind keine FL-Studio-Projektprüfung. Die im gemeinsam
genutzten Testprogramm vorhandenen Legacy-Capture-Optionen sind für diese
neuen 0.1.0-Produkte nicht Teil der Anleitung.

Der DSP-Test lässt sich unabhängig von JUCE mit einem C++17-Compiler bauen:

```bat
clang++ -std=c++17 -O2 GILLRESTORATION\Tests\RestorationTests.cpp -o RestorationTests.exe
RestorationTests.exe GILLRESTORATION\Tests\fixtures
```

Hier entsteht das JSON im aktuellen Arbeitsordner. Dieser isolierte Test ist
auch mit dem zuvor verwendeten portablen LLVM/MinGW möglich; daraus folgt
keine Eignung dieser Toolchain für die JUCE-Wrapper.

## Testdaten und Weitergabe

Die vorbereitete WAV-Referenz und ihre unveränderte ursprüngliche FLAC-Datei
sind mit Herkunfts-, Änderungs- und Lizenzhinweisen enthalten. Die Tests
benötigen weder Python noch einen Decoder-Cache; die zusätzlichen Störungen
werden im C++-Test erzeugt. Es handelt sich um öffentliche LibriSpeech-Daten,
nicht um private Nutzeraufnahmen.

Für einen entsprechenden Quellcode-Download Projektdateien, Assets, Tests,
Fixtures, Dokumentation und den vollständigen verwendeten JUCE-Stand samt
Lizenzhinweisen zusammen bereitstellen. Lokale Build-Verzeichnisse,
Compiler-/SDK-Installer, Git-Datenbank und Test-Executables müssen dafür nicht
dupliziert werden. Einzelheiten: LICENSE-NOTICE.md und THIRD-PARTY.md.
