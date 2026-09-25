# GILL-DE-ESSER 0.2.0 aus dem Quellcode erstellen

Aktueller Quellstand der Release-Runde 04: **0.2.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Das CMake-Projekt **GILLDEESSER** baut das Windows-x64-VST3-Produkt
**GILL-DE-ESSER**. Andere GILL-Plugins werden dafür nicht benötigt.

## Verzeichnisstruktur

```text
GILLDEESSER/
  CMakeLists.txt
  Source/
  Assets/
  Tests/
    fixtures/
  README.md
  LICENSE
  LICENSE-NOTICE.md
  THIRD-PARTY.md
dependencies/
  JUCE/
```

Die beiden Verzeichnisse bleiben Geschwister. CMake lädt JUCE normalerweise
aus `../dependencies/JUCE`. **GILL_JUCE_EXPORT nicht setzen**: Diese optionale
Variable verweist auf einen bereits vorbereiteten lokalen Entwicklungsaufbau.
Der reguläre Quellcode-Build benötigt keinen solchen Export.

Enthaltener JUCE-Stand: **8.0.12**, Commit
`29396c22c93392d6738e021b83196283d6e4d850`.
Das Quellpaket umfasst alle verfolgten Dateien dieses unveränderten Stands,
einschließlich Build-Helfern unter `extras` und sämtlichen Lizenzhinweisen.

## Windows-Build

Benötigt werden CMake ab 3.22, Ninja und eine x64-MSVC-Entwicklungsumgebung
mit Windows SDK. C++17 ist erforderlich. Der lokale Entwicklungsaufbau
verwendet MSVC 14.44.35207 / Compiler 19.44 und Windows SDK 10.0.22621.0.
MinGW ist kein unterstützter Weg für den JUCE-Windows-VST3-Wrapper.

Das Paket in einen kurzen Pfad entpacken, etwa `C:\GillSrc`, da zu lange
Objektdateipfade unter Windows Compilerfehler verursachen können. Ein
optionaler kurzer Laufwerksalias muss den gesamten Paketordner einschließlich
`dependencies` abbilden, nicht nur das Build-Verzeichnis.

In einer x64 Native Tools-Eingabeaufforderung mit CMake und Ninja im PATH,
aus dem obersten Paketverzeichnis:

```bat
cmake -S GILLDEESSER -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --parallel 2 --target GILLDEESSER_VST3 GillDeEsserDspTests GillDeEsserIntegrationTests
ctest --test-dir build -C Release --output-on-failure
```

Die geringe Parallelität hält den Speicherbedarf auf kleineren Rechnern
niedriger. Diese Befehle sind eine Build-Anleitung, kein behaupteter Testlauf
auf einem anderen Rechner. Die Oberflächenprüfung benötigt eine native
Windows-Sitzung.

Das vollständige Paket entsteht unter:

```text
build/GILLDEESSER_artefacts/Release/VST3/GILL-DE-ESSER.vst3
```

CMake installiert es nicht automatisch. Für die Installation den gesamten
`.vst3`-Ordner einschließlich `Contents` kopieren. Dieses Projekt erstellt
keine macOS-Binärdateien und keine eigenständige Standalone-Anwendung.

## Testläufe und erzeugte Dateien

CTest führt **DEESSER_DSP** und **DEESSER_INTEGRATION** mit
`GILLDEESSER/Tests` als Arbeitsordner aus. Der DSP-Test erzeugt dort
`deesser-cross-rate-quality.json` und die synthetischen WAVs unter `fixtures`.
Seinen Hauptbericht gibt er als JSON auf der Standardausgabe aus. Er lässt
sich aus dem Paketverzeichnis zusätzlich dauerhaft speichern:

```bat
cd GILLDEESSER\Tests
..\..\build\GillDeEsserDspTests.exe > deesser-dsp-report.json
..\..\build\GillDeEsserIntegrationTests.exe
```

Die Integrationsprüfung schreibt `plugin-integration-report.json` und native
Oberflächenbilder in den aktuellen Arbeitsordner. Sie prüft den echten
AudioProcessor und Editor, lädt jedoch nicht selbst die kompilierte VST3-Datei
in FL Studio. Ein separater Plugin-Validator oder Hosttest ist zusätzlich
gegen das endgültige VST3-Paket auszuführen und gesondert zu dokumentieren.

Der isolierte DSP-Test benötigt weder JUCE noch vorab heruntergeladene
Audiodateien und lässt sich auch mit einem eigenständigen C++17-Compiler bauen:

```bat
clang++ -std=c++17 -O2 GILLDEESSER\Tests\DeEsserTests.cpp -o DeEsserTests.exe
DeEsserTests.exe > deesser-dsp-report.json
```

Dabei entstehen `fixtures` und der ergänzende Qualitätsbericht relativ zum
aktuellen Arbeitsordner. Eine geeignete LLVM/MinGW-Toolchain kann diesen
isolierten Test bauen; daraus folgt keine Eignung für den JUCE-VST3-Wrapper.
Testsignale und deren Sollanteile werden vollständig im C++-Test erzeugt.
Python ist weder für die Tests noch für das Plugin erforderlich.

## Quellpaket

`package_deesser_source.py` gehört zur ursprünglichen Entwicklungsumgebung.
Es verpackt bestehende Quellen und Dokumente, ohne sie zu verändern oder einen
Build auszuführen, und verlangt bestandene endgültige DSP- und
Integrationsberichte. JUCE wird als vollständiger Satz unveränderter Git-Blobs
des gepinnten, sauberen Stands aufgenommen. Der enthaltene
`SOURCE-MANIFEST.json` verzeichnet Herkunft und SHA-256-Prüfsummen.

Nicht zum Quellpaket gehören Compiler, SDK-Installer, Git-Datenbanken,
kompilierte Testprogramme, Plugin-Binärdateien und lokale Build-Verzeichnisse.
Die synthetischen WAVs und ihre Erzeugung im C++-Test bleiben enthalten.

## Das fertige VST3 in einem separaten Host prüfen

`Tests/Host` enthält den tatsächlich verwendeten Suite-Hosttest als eigenständig
baubare Quelle. Er lädt das fertige GILL-DE-ESSER-Paket und prüft Identität,
Zustand sowie gemeldete und gemessene Latenz von **0 Samples** bei
44,1 / 48 / 96 / 192 kHz. AMOUNT steht für den exakten Impulstest auf 0.
Aus dem obersten Paketverzeichnis:

```bat
cmake -S GILLDEESSER/Tests/Host -B build-vst3-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build-vst3-host --config Release --parallel 2
cd GILLDEESSER\Tests
..\..\build-vst3-host\GillSuiteVst3HostTests.exe "..\..\build\GILLDEESSER_artefacts\Release\VST3\GILL-DE-ESSER.vst3"
```

Der Bericht heißt `GILL-DE-ESSER-vst3-host-report.json` im aktuellen Ordner.
Exit-Code 1 bedeutet fehlgeschlagene Prüfungen. Die zusätzlich im Suite-Host
enthaltenen GILLEQ-Upgrademodi gehören nicht zum De-Esser-Test. Der eigenständige
CMake-Einstieg ist eine Wiederholungsanleitung; dokumentierte Entwicklungsläufe
verwendeten dieselbe Hosttest-Quelle im gemeinsamen Suite-Build. Er benötigt
keine andere GILL-Pluginquelle und ersetzt keinen FL-Studio-Scan oder Hörtest.
