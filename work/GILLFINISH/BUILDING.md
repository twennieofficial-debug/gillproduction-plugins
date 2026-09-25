# GILLFINISH 0.3.0 erstellen

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Das CMake-Projekt erzeugt fünf Windows-x64-VST3-Produkte: GILLSILK, GILLSPARK, GILLSTRIP, GILLGOLD und GILLDIVE. Es baut keine AU-, AAX- oder Standalone-Dateien.

## Paketlayout und Voraussetzungen

```text
C:\GillSrc\
  GILLFINISH\
    CMakeLists.txt
    Source\
    Assets\
    Tests\
    LICENSE
    LICENSE-NOTICE.md
    THIRD-PARTY.md
  dependencies\
    JUCE\
```

GILLFINISH und dependencies müssen Geschwisterordner bleiben. Der normale Build liest JUCE über ../dependencies/JUCE; er lädt keine fehlenden Abhängigkeiten automatisch herunter. Ein kurzer tatsächlicher Pfad wie C:\GillSrc vermeidet unnötig lange Buildpfade. Ein Laufwerksalias wird nicht vorausgesetzt.

Erforderlich sind CMake ab 3.22, Ninja, MSVC mit x64-C++17-Unterstützung und das Windows SDK. Der Entwicklungsaufbau nutzt MSVC Toolset 14.44.35207 / Compiler 19.44 sowie Windows SDK 10.0.22621.0. Der Windows-VST3-Build verwendet die statische MSVC-Laufzeit; Release entspricht /MT. MinGW ist für diesen nativen JUCE-Wrapper nicht der dokumentierte Buildweg.

Der vollständige JUCE-Quellstand ist **8.0.12**, Commit **29396c22c93392d6738e021b83196283d6e4d850**. Seine Lizenzhinweise und benötigten Helfer unter extras müssen erhalten bleiben. GILLFINISH benötigt kein Signalsmith und keine zusätzliche DSP-Bibliothek. Die Audioverarbeitung benötigt keine Python-Pakete.

## Regulärer Windows-Build

Eine **x64 Native Tools Command Prompt** für Visual Studio öffnen. CMake und Ninja müssen im PATH verfügbar sein. In C:\GillSrc ausführen:

```bat
cmake -S GILLFINISH -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --parallel 2 --target GILLSILK_VST3 GILLSPARK_VST3 GILLSTRIP_VST3 GILLGOLD_VST3 GILLDIVE_VST3 GillCharacterDspTests GillSpectralDspTests GillFinishIntegrationTests
ctest --test-dir build -C Release --output-on-failure
```

Die Tests sind im vollständigen Quellpaket enthalten. CMake legt ihre Ziele nur an, wenn die jeweiligen Testdateien vorhanden sind. Zwei Compilerprozesse begrenzen den Speicherbedarf. Die nativen Oberflächentests benötigen eine Windows-Desktopsitzung.

Die vollständigen VST3-Pakete entstehen unter:

```text
build/GILLSILK_artefacts/Release/VST3/GILLSILK.vst3
build/GILLSPARK_artefacts/Release/VST3/GILLSPARK.vst3
build/GILLSTRIP_artefacts/Release/VST3/GILLSTRIP.vst3
build/GILLGOLD_artefacts/Release/VST3/GILLGOLD.vst3
build/GILLDIVE_artefacts/Release/VST3/GILLDIVE.vst3
```

CMake installiert sie nicht automatisch. Für eine manuelle Installation jeweils den vollständigen .vst3-Ordner mit Contents-Struktur verwenden, etwa unter C:\Program Files\Common Files\VST3\GILLPRODUCTION. Die Plugin-Kennungen in CMake beibehalten; insbesondere verwendet GILLSPARK den eigenständigen Code **Gpk1**.

## Tests und Berichte

CTest registriert **Character_DSP**, **Spectral_DSP** und **EFFECTS_INTEGRATION**. Letzterer Name bezeichnet in diesem Projekt das Ziel GillFinishIntegrationTests. Arbeitsordner ist GILLFINISH/Tests.

Konsolenberichte lassen sich aus diesem Tests-Ordner speichern:

```bat
..\..\build\GillCharacterDspTests.exe > CharacterTests-report.txt
..\..\build\GillSpectralDspTests.exe > SpectralTests-report.txt
..\..\build\GillFinishIntegrationTests.exe > finish-integration-console.txt
```

Der Integrationstest schreibt zusätzlich finish-integration-report.json und native Oberflächenbilder. Ergebnisdateien gelten nur für den tatsächlich ausgeführten Stand. Die Integration direkt mit dem Prozessor ersetzt nicht die separate Validierung der endgültigen VST3-Dateien im Host oder Plugin-Validator.

Die beiden Engine-Tests lassen sich auch ohne JUCE bauen. In einer vorbereiteten x64-MSVC-Eingabeaufforderung aus GILLFINISH/Tests:

```bat
cl /nologo /std:c++17 /O2 /Ob2 /DNDEBUG /EHsc /MT CharacterTests.cpp /Fe:CharacterTests.exe /Fo:CharacterTests.obj
CharacterTests.exe
cl /nologo /std:c++17 /O2 /Ob2 /DNDEBUG /EHsc /MT SpectralTests.cpp /Fe:SpectralTests.exe /Fo:SpectralTests.obj
SpectralTests.exe
```

Falls Tests/Vst3HostTests.cpp im Paket vorhanden ist, bietet CMake zusätzlich GillFinishVst3HostTests an. Dieses optionale Ziel wird für den normalen Build oben nicht benötigt.

## Optionale Wiederverwendung einer Laufzeitbibliothek

**GILL_JUCE_EXPORT** und **GILL_PREBUILT_RUNTIME** sind optionale Entwickler-Overrides. Beim regulären Build beide weglassen. Ohne sie fügt CMake den lokalen JUCE-Baum hinzu und baut GillFinishRuntime aus Source/JuceRuntime.cpp selbst.

Ein GILL_JUCE_EXPORT muss zu genau diesem JUCE-Stand passen und die erwarteten JUCE-CMake-Ziele sowie JUCE_MODULES_DIR bereitstellen. GILL_PREBUILT_RUNTIME muss auf eine kompatible x64-Release-Static-Library zeigen: gleicher JUCE-Stand, Compiler/ABI, /MT-Laufzeit und Moduldefinitionen. Beliebige fremde .lib-Dateien sind kein Ersatz. Der Quellcode-Build ist nicht von der ursprünglichen lokalen Cache-Struktur abhängig.

## Weitergabe

Zum entsprechenden Quellpaket gehören CMake-Dateien, Source, Assets, Tests, Dokumentation und der vollständige gepinnte JUCE-Baum samt Lizenzhinweisen. Compiler- und SDK-Installer, lokale Objektdateien und Git-Datenbanken gehören nicht dazu. Einzelheiten stehen in LICENSE-NOTICE.md und THIRD-PARTY.md. Diese Anleitung beschreibt einen reproduzierbaren Buildweg und behauptet keinen bereits bestandenen Lauf auf einem anderen Rechner.

