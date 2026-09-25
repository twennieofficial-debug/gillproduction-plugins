# GILLFINISH 0.3.0 — LIZENZEN, ABHÄNGIGKEITEN UND REFERENZEN

## Eigener Quellcode

GILLSILK, GILLSPARK, GILLSTRIP, GILLGOLD und GILLDIVE verwenden eigenen C++17-DSP-Code. Die Paketlizenz ist GNU AGPL Version 3; der Spektralcode kennzeichnet dies als AGPL-3.0-only. Der vollständige Lizenztext liegt in LICENSE.

SpectralDSP.h implementiert Spektralanalyse, Filtermasken, Rücktransformation, zeitliche Überlappung und Detektoren selbst. CharacterDSP.h enthält die eigenen Filter, Dynamikstufen, Verzögerungsleitungen und den kompakten Nachhall. Hier wird keine externe FFT-, De-Click-, Resonanz- oder Hardwaremodell-Bibliothek eingebunden.

Die Klangziele und Presets sind eigene technische und gestalterische Entscheidungen. Die Namen sind keine Aussage über Messungen an Künstleraufnahmen oder trainierte Artist-Modelle. Numerische Tests erzeugen ihre Signale selbst; es wird kein externes Sprach- oder Gesangsdatenset mitgeliefert.

## JUCE und eingebundene Bestandteile

Die Oberfläche, Host-Anbindung und VST3-Verpackung verwenden den lokalen JUCE-Stand **8.0.12**. Der festgelegte Projektstand ist Commit 29396c22c93392d6738e021b83196283d6e4d850. Upstream: [JUCE](https://github.com/juce-framework/JUCE).

Für dieses Paket wird die AGPL-v3-Option von JUCE verwendet. Der entsprechende vollständige JUCE-Baum samt eigenen Copyright- und Lizenzhinweisen gehört zum Quellpaket. Die Hinweise befinden sich unter dependencies/JUCE/LICENSE.md sowie bei den einzelnen eingebundenen Bestandteilen. Diese Zusammenstellung ersetzt deren jeweilige Texte nicht.

Das VST3 SDK dieses JUCE-Stands enthält die MIT-Lizenz unter dependencies/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt, Copyright 2025 Steinberg Media Technologies GmbH. Die vollständige Notiz im Quellbaum erhalten.

Signalsmith Stretch/Linear sind in diesen fünf Produkten nicht enthalten. Die externen Werkzeuge MSVC, Windows SDK, CMake und Ninja bleiben bei ihren jeweiligen Herstellern; ihre Installer und Programme gehören nicht zum vorgesehenen Quellpaket.

## Gestaltung und Schrift

Assets/core_reference.png enthält die bereits vorhandene Holzoberfläche und das kleine GP-Zeichen aus dem GILL-Bundle. Die Datei ist mit der Familienreferenz identisch; Knöpfe, Fader und Anzeigen werden separat gezeichnet. Die Verwendung generierter Gestaltungselemente behauptet keine exklusiven Rechte daran.

Die Oberfläche fordert die lokal verfügbare Schrift Segoe UI über Windows/JUCE an. Es wird keine Microsoft-Schriftdatei mitgeliefert.

## Funktionale Referenzen

Die folgenden Herstellerbeschreibungen wurden zur Einordnung der vom Nutzer gezeigten Produkte gelesen. Sie beschreiben die jeweiligen Fremdprodukte; sie belegen weder Algorithmusgleichheit noch gleiche Klangqualität der GILL-Plugins.

| Nutzerreferenz | Eingeordnete Funktion und Primärquelle |
|---|---|
| oeksound soothe2 | Dynamische Resonanzabsenkung und fokussiertes Abhören der Bearbeitung; [offizielles soothe2-Handbuch](https://oeksound.com/manuals/soothe2/). |
| oeksound Spiff | Frequenzselektive Transientenbearbeitung mit Cut/Boost und Differenzabhören; [offizielles Spiff-Handbuch](https://oeksound.com/manuals/spiff/). |
| Waves CLA Vocals | Zusammengefasste Vocal-Bearbeitung mit Klang, Dynamik und Effekten; [offizielles CLA-Vocals-Handbuch](https://www.waves.com/1lib/pdf/plugins/cla-vocals.pdf). |
| Waves PuigTec EQs | Bedienidee getrennter Klangregler in einem Vintage-EQ; [offizielle PuigTec-Produktbeschreibung](https://www.waves.com/plugins/puigtec-eqs). |
| Image-Line Fruity Love Philter | Filterbewegung mit Modulation und Eingangshüllkurve; [offizielles Love-Philter-Handbuch](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Love%20Philter.htm). |

Die GILL-Implementierungen sind eigenständig. GOLD verwendet ausdrücklich eigene Filterkurven; DIVE ist ein eigener kompakter Unterwasserfilter und implementiert nicht die acht frei verschaltbaren Bänke von Love Philter. SILK und SPARK übernehmen keine proprietären Modellparameter, Oberflächenbilder oder Programmdateien der Referenzprodukte.

Die Namen der Referenzprodukte und Hersteller dienen allein ihrer Identifikation. Es besteht keine behauptete Kooperation oder Bestätigung durch oeksound, Waves, Image-Line, Steinberg oder JUCE.
