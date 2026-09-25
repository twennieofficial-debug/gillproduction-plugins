# GILLFINISH 0.3.0

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Fünf eigenständige Windows-x64-VST3-Effekte für Vocals. Jedes Plugin enthält zwölf eigene Presets; insgesamt sind es 60. Holzoberfläche, weiße Regler und kleines GP-Zeichen passen zum GILL-Bundle.

| Plugin | Aufgabe | Eigene Latenz bei 48 kHz |
|---|---|---:|
| GILLSILK | Schmale Resonanzen dynamisch absenken | 2048 Samples / 42,7 ms |
| GILLSPARK | Frequenzselektive Transienten in CUT oder BOOST bearbeiten | 512 Samples / 10,7 ms |
| GILLSTRIP | Klang, Kompression, De-Essing, Raum, Echo und Doubling | 0 Samples |
| GILLGOLD | Klangformung mit unabhängigem Boost und Attenuation | 0 Samples |
| GILLDIVE | Unterwasserfilter mit Bewegung und Reaktion auf die Stimme | 0 Samples |

Mono/Stereo und 8–192 kHz werden unterstützt. Bei 44,1 kHz beträgt die Latenz von SILK/SPARK 46,4/11,6 ms. Die Verzögerung kommt zum Hostpuffer hinzu; insbesondere SILK eignet sich eher für die Bearbeitung als für latenzarmes Einsing-Monitoring.

## Schnellstart

Presets unten auswählen. Für modernen Rap sind RAP EDGE TAMER in SILK, REMOVE MOUTH CLICKS in SPARK, MODERN RAP LEAD in STRIP und RAP BODY + AIR in GOLD geeignete Ausgangspunkte. DIVE startet mit UNDERWATER.

- **SILK/SPARK:** DEPTH dosiert die Wirkung, SENSITIVITY die Erkennung. LOW/HIGH FOCUS lassen sich auch über die weißen Punkte im Diagramm ziehen. ATTACK und RELEASE beziehungsweise DECAY glätten die Bearbeitung. SPARK bietet CUT/BOOST; ATTACK reicht dort von 0,1–40 ms und DECAY von 10–400 ms.
- **DELTA:** Bei SILK/SPARK die Differenz zwischen zeitlich ausgerichtetem Original und bearbeitetem Ausgang hören. Für die Kontrolle MIX 100 % und OUTPUT 0 dB wählen. Anschließend DELTA wieder ausschalten.
- **STRIP:** INPUT, BODY, PRESENCE, COMPRESS, DE-ESS, SPACE, ECHO und DOUBLE formen die Vocal. FOCUS nutzt Viertelnoten-Echo, TIGHT einen 95-ms-Slap, WIDE ein punktiertes Achtel. Gültiges Hosttempo hat Vorrang vor TEMPO. Die konservative Tail-Meldung beträgt 30 Sekunden.
- **GOLD:** LOW BOOST und LOW ATTEN überlappen absichtlich unterschiedlich; ihre Wirkungen heben sich nicht einfach auf. LOW ATTEN arbeitet bei der 2,4-fachen LOW-FREQ-Einstellung. HIGH BOOST ist ein Bell, HIGH ATTEN ein eigener Shelf. Die Anzeige zeigt die reine Filterkurve ohne MIX/OUTPUT.
- **DIVE:** DEPTH schließt den Tiefpass; MOTION bewegt ihn. Positive FOLLOW-Werte öffnen bei lauter Stimme, negative schließen. SYNC koppelt die Geschwindigkeit an das Tempo, nicht die Phase an die Songposition. OPEN WATER ist ein weit geöffneter Filter; exakt neutral sind MIX 0 % oder BYPASS.

Ein neuer Presetaufruf setzt Klangparameter einschließlich MIX/OUTPUT auf den jeweiligen Ausgangspunkt. BYPASS, DELTA und manuelles TEMPO bleiben erhalten. Ein Stern am Presetnamen kennzeichnet Änderungen. Doppelklick setzt einzelne Regler auf ihren Parameter-Standardwert zurück; Einstellungen werden im DAW-Projekt gespeichert.

Eine mögliche Startkette ist SPARK → STRIP → SILK → GOLD; nicht jede Aufnahme benötigt alle Stufen. OUTPUT für einen Vergleich bei ähnlicher Lautstärke nutzen. Starke Einstellungen können auch gewollte Konsonanten und Klangfarbe verändern.

## Eigene Implementierung und Prüfung

Engines, Presets und Oberflächen sind eigene Implementierungen. Es werden keine proprietären Engines, Presetdateien oder Artist-Datensätze mitgeliefert. SILK/SPARK unterscheiden nicht sicher zwischen gewollter Artikulation und jeder unerwünschten Störung. Perfekte Bearbeitung beliebiger Aufnahmen wird nicht zugesagt.

Tests erzeugen eigene synthetische Signale. DSP-, native Host-, UI- und Validator-Berichte gelten jeweils für ihre protokollierten Quellen und Binärdateien. Diese Anleitung bestätigt keine Installation.

Buildanleitung: [BUILDING.md](BUILDING.md). Vollständige Presetdefinitionen: [Source/Presets.h](Source/Presets.h). Lizenz: [LICENSE](LICENSE), [LICENSE-NOTICE.md](LICENSE-NOTICE.md), [THIRD-PARTY.md](THIRD-PARTY.md).

