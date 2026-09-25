# GILLDEREVERB aus dem Quellcode erstellen

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Das entsprechende Quellpaket enthält das Projekt und den vollständigen
verwendeten JUCE-Stand. Eine Installation oder ein Quellordner von GILLEQ ist
für diesen Build nicht erforderlich.

```text
GILLDEREVERB/
  CMakeLists.txt
  Source/
  Assets/
  Tests/
  LICENSE
  LICENSE-NOTICE.md
  THIRD-PARTY-NOTICES.md
dependencies/
  JUCE/
```

GILLDEREVERB und dependencies müssen Geschwisterordner bleiben. Der normale
CMake-Weg lädt JUCE aus `../dependencies/JUCE`. Die optionale interne Variable
`GILL_JUCE_EXPORT` ist für diesen eigenständigen Build **nicht** zu setzen.
JUCE/extras enthält auch benötigte Build-Helfer und bleibt im Quellpaket.

## Windows-Werkzeuge

Windows x64, CMake ab 3.22, Ninja sowie ein x64-MSVC-Entwicklungsumfeld mit
Windows SDK. Für die Entwicklung wurde MSVC 14.44.35207 (Compiler 19.44) mit
Windows SDK 10.0.22621.0 verwendet. JUCE: Version 8.0.12, Commit
`29396c22c93392d6738e021b83196283d6e4d850`.

Einen kurzen Entpackpfad verwenden, beispielsweise `C:\GillSrc`. Sehr lange
Objektdateipfade können beim Windows-Compiler fehlschlagen. Falls ein kurzer
Laufwerksalias verwendet wird, muss er den **gesamten Paketordner** abbilden,
damit relative Verweise zwischen Projekt, Build und dependencies gültig bleiben.

Eine x64 Native Tools-Eingabeaufforderung öffnen oder das `setup_x64.bat` einer
passenden portablen Toolchain aufrufen. CMake und Ninja müssen im PATH liegen.
Im obersten entpackten Verzeichnis:

```bat
cmake -S GILLDEREVERB -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --target GILLDEREVERB_VST3 GILLDEREVERB_Standalone GillDereverbDspTests GillDereverbAutoQualityTests GillDereverbIntegrationTests GillDereverbVst3HostTests
ctest --test-dir build -C Release --output-on-failure
```

Das VST3-Paket entsteht unter
`build/GILLDEREVERB_artefacts/Release/VST3/GILLDEREVERB.vst3`. Die eigenständige
Anwendung liegt unter `build/GILLDEREVERB_artefacts/Release/Standalone`.
Bei Übernahme in einen Pluginordner immer das vollständige VST3-Paket kopieren.

## Testmaterial

Die Tests enthalten 17 vorbereitete WAV-Referenzen sowie die ursprüngliche
LibriSpeech-FLAC-Datei und deren Herkunfts-/Lizenzhinweise. Die WAVs enthalten
keine privaten Nutzeraufnahmen. Den lokalen Hilfsordner
`Tests/fixtures/_python` nicht mit ausliefern: Er ist ein Decoder-Cache zur
Fixture-Erzeugung, keine Laufzeit- oder Build-Abhängigkeit des Plugins.

Die vorbereiteten WAVs können ohne Python verwendet werden. Um sie erneut mit
`prepare_fixtures.py` zu erzeugen, werden Python, NumPy und SoundFile benötigt.
Für die wissenschaftlichen Einschränkungen der synthetischen Räume siehe
Tests/DEREVERB-RESEARCH.md und Tests/fixtures/ORIGIN-AND-LICENSE.md.

Die genannten Befehle und Qualitätskriterien sind ausführbare Anleitung und
Testumfang. Sie behaupten kein bestimmtes Testergebnis auf einem anderen PC.

## Update 0.2.0

Der zusätzliche Test AUTO_QUALITY prüft die Ein-Regler-Automatik mit sechs
synthetischen Räumen, derselben Sprachquelle und separaten Tönen. Grenzen und
Messwerte stehen in Tests/AUTO-QUALITY.md. Er ist kein unabhängiger Hörtest.

Die bereits erfasste echte 0.1.0-VST3-Referenz liegt unter
Tests/fixtures/legacy-v0.1. Die neue Datei gegen diese Referenz prüfen:

```bat
build\GillDereverbVst3HostTests.exe build\GILLDEREVERB_artefacts\Release\VST3\GILLDEREVERB.vst3 --verify-legacy GILLDEREVERB\Tests\fixtures\legacy-v0.1
```

Der Capture-Modus ist für die ursprüngliche 0.1.0-Datei vorgesehen und verweigert
das Überschreiben bestehender Referenzen. Einzelheiten: Tests/UPGRADE-REVIEW.md.
