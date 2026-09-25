# GILLDECLICK / GILLDECRACKLE 0.3.0

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Zwei eigenständige Effekt-Plugins für Windows x64 / VST3 im gemeinsamen
CORE-Design: helles Eichenholz, weißer Regler, Salbeigrün und kleines GP-Logo.
Jedes Plugin hat einen großen Regler und eine direkt editierbare Prozentanzeige.

| Plugin | Gedacht für | Eigene Verzögerung bei 48 kHz |
|---|---|---:|
| **GILLDECLICK** | Einzelne kurze digitale Klicks und scharfe Impulsstörungen | 75 Samples / 1,5625 ms |
| **GILLDECRACKLE** | Feines Knistern und Folgen sehr kurzer Klicks, auch kurze impulsartige Mundklicks | 60 Samples / 1,25 ms |

Version 0.2.0 verkürzt den Lookahead auf den tatsächlich benötigten Kontext.
Kennungen, Bedienelemente und gespeicherte AMOUNT-Werte bleiben erhalten.
Erkennung und Reparatur bei konstanten Einstellungen bleiben unverändert:
Nach Ausgleich der unterschiedlichen Verzögerung sind die geprüften alten
und neuen Audiosignale bitgenau identisch.

## Bedienung

Den Regler ziehen oder den Prozentwert anklicken und eingeben. Pfeiltasten
ändern den Wert; Doppelklick setzt ihn auf **55 %** zurück. Das quadratische
Fenster lässt sich zwischen 340 und 840 Pixeln skalieren. Der Hostparameter
heißt bei beiden Plugins **AMOUNT**; der sichtbare Text lautet **KLICKS
REDUZIEREN** beziehungsweise **KNISTERN REDUZIEREN**.

Bei etwa 35–55 % anfangen und nur so weit erhöhen, wie die Aufnahme es braucht.
Mehr Amount erhöht die Erkennungsempfindlichkeit; im unteren Viertel steigt
auch der Reparaturanteil. Hohe Werte können gewollte Anschläge oder Konsonanten
verändern. Eine höhere Einstellung ist deshalb nicht automatisch besser.

**0 %** liefert das unveränderte, zeitlich verzögerte Original. Der Host-Bypass
verwendet denselben zeitlich ausgerichteten Originalpfad; beim Umschalten gibt
es einen kurzen Übergang. Bypass wird im Plugin-Wrapper des Hosts bedient.
Der Amount-Wert kann automatisiert und im Projekt gespeichert werden.

## Einsatz und Grenzen

Die Plugins erkennen lokal auffällige kurze Impulse und ersetzen nur die
erkannten Stellen aus ihrer Umgebung. GILLDECRACKLE hat neben anderen
Schwellen und kürzeren Reparaturfenstern einen zusätzlichen Erkennungspfad für
kleine, wiederholte Impulse. Die Verarbeitung ist eine eigene klassische
DSP-Implementierung ohne KI-Modell, Cloud oder fremden Restaurationscode.

Längere feuchte Schmatzgeräusche, Atem, Rauschen, Clipping und Raumhall werden
nicht zuverlässig entfernt. Auch echte musikalische Anschläge können wie
Störungen aussehen. Bei deutlichen Stimmveränderungen Amount zurücknehmen.
Die Mundklick-Prüfung verwendet synthetische kurze Impulsfolgen, keine breite
Sammlung echter Schmatzaufnahmen; vollständige Mundgeräuschentfernung ist damit
nicht nachgewiesen.

Für die Bearbeitung einer Aufnahme die Restauration möglichst früh einsetzen,
vor Tonhöhenkorrektur, EQ und Kompression. Beide Plugins sind nur dann
nacheinander nötig, wenn die Aufnahme beide Störungsarten enthält; dann
addiert sich auch ihre Verzögerung.

Mono/Stereo und 32-/64-Bit-Audioverarbeitung bei 8–192 kHz sind implementiert.
Die Pluginlatenz ist pro Abtastrate fest und entspricht dem benötigten
Reparaturkontext. Die Werte werden dem Host gemeldet:

| Samplerate | GILLDECLICK Samples | GILLDECRACKLE Samples |
|---|---:|---:|
| 8 kHz | 32 | 46 |
| 11,025 kHz | 45 | 47 |
| 22,05 kHz | 58 | 51 |
| 32 kHz | 64 | 55 |
| 44,1 kHz | 72 | 59 |
| 48 kHz | 75 | 60 |
| 88,2 kHz | 142 | 115 |
| 96 kHz | 147 | 118 |
| 192 kHz | 291 | 234 |

Zum Vergleich: Version 0.1.0 meldete bei 48 kHz 192 beziehungsweise 384 Samples.
Die neuen Werte bei 44,1/48/96/192 kHz wurden auch an den tatsächlichen
VST3-Dateien als Impulsposition gemessen. Hinzu kommen Hostpuffer,
Audiogerät und gegebenenfalls weitere Plugins. Der Host erhält die
Pluginlatenz zur Kompensation. Das vorhandene Audiointerface bleibt das
Audiogerät des Hosts; kein separater Plugin-Treiber ist nötig.

Bei vorhandener AMOUNT-Automation ändert die kürzere Verzögerung deren
Zuordnung zu den bearbeiteten Audiosamples. Identische Kurven zur identischen
Hostzeit müssen deshalb nicht bitgleich zum alten Plugin klingen. Im
Vergleichstest stimmt auch automatisierte Verarbeitung exakt überein, wenn
die Kurve um die jeweilige Latenzdifferenz passend zum Audiosignal zugeordnet
wird. Bestehende automatisierte Übergänge nach dem Update kontrollieren;
es gibt keine automatische Migration der Zeitpositionen in FL-Projekten.

## Installation in FL Studio

Das vollständige jeweilige VST3-Paket in den üblichen VST3-Ordner kopieren,
beispielsweise:

```text
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLDECLICK.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLDECRACKLE.vst3
```

Dabei die enthaltene Ordnerstruktur einschließlich `Contents` erhalten.
Anschließend im Plugin-Manager von FL Studio nach Plugins suchen und den
entsprechenden Namen als Mixer-Effekt laden. Pro Produkt nur eine Kopie in
den durchsuchten Pluginordnern belassen. Das sind Installationshinweise,
keine Bestätigung einer bereits geprüften Installation auf diesem Rechner.

## Prüfungen und Quellcode

Messverfahren, Ergebnisse des Standard-C++-DSP-Prüfstands und seine Grenzen
stehen in [Tests/RESTORATION-VERIFICATION.md](Tests/RESTORATION-VERIFICATION.md).
Dieser Bericht ersetzt keine Prüfung des fertigen VST3 in FL Studio und
keinen subjektiven Hörvergleich. Native Host-Ergebnisse stehen in
`Tests/GILLDECLICK-vst3-host-report.json` und `Tests/GILLDECRACKLE-vst3-host-report.json`.
Die Latenz-Regression ist in `Tests/latency-v03-report.json`,
`Tests/latency-v03-console.txt` und `Tests/latency-v03-provenance.json` dokumentiert.
Der interne Dateiname `v03` bezeichnet die Bundle-Prüfrunde; diese beiden
Pluginversionen sind **0.2.0**. Der Vergleich umfasst 18.234 Signalkonfigurationen
bei neun Raten, alle AMOUNT-Schritte von 0 bis 100 % in 0,1-%-Abständen und
die vorhandenen Sprach-/Impulsfixtures. Die 59.007.744 verglichenen Samples
sind keine ebenso große Zahl unabhängiger Hörprüfungen.

Die Build-Anleitung steht in [BUILDING.md](BUILDING.md). Die eigenen
Programmdateien werden unter **AGPL-3.0-only** bereitgestellt; JUCE 8.0.12
wird über seine AGPL-v3-Option verwendet. Vollständige Bedingungen:
[LICENSE](LICENSE), [LICENSE-NOTICE.md](LICENSE-NOTICE.md) und
[THIRD-PARTY.md](THIRD-PARTY.md). Bei Weitergabe der Binärdateien gehört der
entsprechende vollständige Quellcode mit den Lizenzhinweisen dazu. Die
Sprach-Testdateien behalten ihre separate CC-BY-4.0-Lizenz.
