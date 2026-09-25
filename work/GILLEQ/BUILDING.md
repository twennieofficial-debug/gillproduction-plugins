# GILLEQ 0.4.0 aus dem Quellcode erstellen

Aktueller Quellstand der Release-Runde 04: **0.4.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Dieses Paket enthält den entsprechenden Quellcode einschließlich des verwendeten
JUCE-Stands. Eine bloße Download-Anweisung für JUCE ersetzt diese enthaltenen
Quellen nicht.

## Verzeichnisstruktur

```text
GILLEQ/
  CMakeLists.txt
  Source/
  Assets/
  Tests/
  LICENSE
  LICENSE-NOTICE.md
  THIRD-PARTY-NOTICES.md
dependencies/
  JUCE/
SOURCE-MANIFEST.json
```

GILLEQ und dependencies müssen Geschwisterordner bleiben, weil CMake JUCE über
`../dependencies/JUCE` einbindet. Die Build-Helfer unter JUCE/extras gehören zum
Quellpaket und sollen erhalten bleiben.

## Werkzeuge

Windows x64, CMake ab 3.22, Ninja und ein x64-MSVC-Entwicklungsumfeld mit Windows
SDK. Für die aktuelle Entwicklung wurden MSVC 14.44.35207 (Compiler 19.44) und
Windows SDK 10.0.22621.0 verwendet. JUCE ist Version 8.0.12, Commit
`29396c22c93392d6738e021b83196283d6e4d850`.

Unter Windows einen kurzen Entpackpfad verwenden, beispielsweise `C:\GilleqSrc`,
damit die erzeugten Objektdateipfade nicht an die Windows-Pfadlängengrenze stoßen.

Eine x64 Native Tools-Eingabeaufforderung öffnen oder das passende
`setup_x64.bat` einer portablen Toolchain aufrufen. CMake und Ninja müssen im PATH
liegen. Dann im obersten entpackten Verzeichnis:

```bat
cmake -S GILLEQ -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --target GILLEQ_VST3 GILLEQ_Standalone GilleqDspTests GilleqDynamicTests GilleqIntegrationTests
ctest --test-dir build -C Release --output-on-failure
```

Das Plugin-Paket entsteht unter
`build/GILLEQ_artefacts/Release/VST3/GILLEQ.vst3`. Die eigenständige Anwendung liegt
unter `build/GILLEQ_artefacts/Release/Standalone`. Bei Übernahme in einen
Pluginordner das vollständige VST3-Paket mit Unterordnern kopieren.

Die Tests erzeugen zum Teil Berichte und Ansichten im jeweiligen
Arbeitsverzeichnis. Diese Anleitung nennt auszuführende Befehle und enthält
keine Behauptung über deren Ergebnis auf einem anderen System.

CTest registriert `DSP`, `DYNAMIC_DSP` und `PLUGIN_INTEGRATION`. Die beiden
DSP-Programme verwenden nur C++17 und benötigen selbst keine JUCE-Verknüpfung;
die Plugin-Integration prüft die tatsächliche JUCE-Prozessorklasse und Oberfläche.
`Tests/DynamicTests.cpp` bindet `Tests/DspTests.cpp` als Helferbibliothek sowie
`Tests/DynamicLegacyEqDSP.h` als unabhängig übersetzte archivierte
v0.1-Referenz ein. Diese Dateien müssen gemeinsam erhalten bleiben. Der
dynamische Test schreibt JSON auf stdout und beendet sich bei Fehlern mit
einem Fehlerstatus. Die vorhandenen JSON-Berichte dokumentieren konkrete
Entwicklungsläufe; nach einem eigenen Build sind die Tests erneut auszuführen.

Version 0.4.0 verwendet dieselbe Plugin-Identität wie 0.1.0. Bei manueller
Installation einer neuen Fassung den Host vorher schließen und das vollständige
VST3-Paket am bisherigen Ort ersetzen, statt parallel mehrere Kopien derselben
Identität in unterschiedlichen Suchpfaden abzulegen. Der Build kopiert das
Plugin nicht automatisch in einen System- oder FL-Studio-Ordner.

Die mitgelieferte Manifestdatei dokumentiert die Versionsbindung und die
SHA-256-Prüfsummen der enthaltenen GILLEQ-Projektdateien. Der Paketexport selbst
liegt als `package_release_v04_source.py` im obersten Verzeichnis.

## Das fertige VST3 und alte Zustände prüfen

`Tests/Host` enthält einen separaten JUCE-VST3-Host samt eigenem CMake-Projekt.
Er lädt das gebaute Plugin und prüft dessen Latenz sowie Zustände aus Version
0.1.0 und 0.2.0 einschließlich dynamischer Automation und Audiovergleich nach
erneutem Speichern/Öffnen. Die beiden unveränderlichen Referenzordner liegen
unter `Tests/legacy-vst3` und `Tests/legacy-vst3-v0.2`. Sie enthalten synthetische
Testsignale und echte alte Hostzustände, keine Nutzeraufnahmen. Aufbau, Befehle,
Herkunft und Berichtsnamen stehen in [Tests/Host/README.md](Tests/Host/README.md).
Das Projekt-CMake benötigt dafür keine Änderung. Der separate Bau-Einstieg ist
eine Wiederholungsanleitung; der dokumentierte Hosttest wurde mit derselben
Quelle im gemeinsamen Suite-Build ausgeführt.
