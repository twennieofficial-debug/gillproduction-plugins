# GILLVOCAL 0.4.0 – GILLFLOW, GILLHEAT, GILLTUNE und GILLTUNE LIVE

Aktueller Quellstand der Release-Runde 04: **0.4.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Das Quellprojekt enthält vier Windows-x64-VST3-Effekte im gemeinsamen Oak-Sage-Design:
helle Eiche, elfenbeinweiße Bedienelemente, Salbeigrün und kleines GP-Logo.
Diese Beschreibung dokumentiert den Quellstand. Ergebnisse des endgültigen
Plugin-Builds und der Installation werden separat festgehalten.

Release-Runde 04 enthält alle vier Produkte als Version **0.4.0**, einschließlich GILLFLOW. Die kompakten nativen Oberflächen und die aktuellen Tune-Korrekturen sind Bestandteil dieses Quellstands; frühere Berichte bleiben als historische Nachweise erhalten.

| Plugin | Aufgabe | Eigene Latenz bei 48 kHz |
|---|---|---:|
| **GILLFLOW** | Kompression mit lernbarer Pegelreferenz | 0 Samples |
| **GILLHEAT** | Sättigung in drei Frequenzbändern | 24 Samples / 0,5 ms |
| **GILLTUNE** | Tonhöhenkorrektur für eine einzelne Gesangsstimme | 2048 Samples / ca. 42,7 ms |
| **GILLTUNE LIVE** | Dieselben Controls mit kürzerer Verarbeitungslatenz | 768 Samples / 16 ms |

Die gemeldete Pluginlatenz kommt zu Hostpuffer, Interface und weiteren Plugins
hinzu. Bypass verwendet bei jedem Produkt das zeitlich passende Originalsignal.
Ein eigener Audiogerätetreiber ist für die Plugins nicht nötig; das vorhandene
Interface bleibt im Host ausgewählt. Die Wrapper unterstützen Mono/Stereo
und 32-Bit-Float-Audioverarbeitung.

Der Wrapper verarbeitet Samplerates von **8 bis 384 kHz**. Außerhalb dieses
Bereichs wird das zeitlich ausgerichtete Original ausgegeben und
**SAMPLE RATE UNSUPPORTED / BYPASS** angezeigt. Die aufgeführten Messungen
beziehen sich jeweils auf die ausdrücklich im Bericht getesteten Raten;
Unterstützung einer Rate bedeutet für sich noch keinen geprüften Hostlauf.

## GILLFLOW

Das vertikale TOUCH-Fenster hat einen großen **AMOUNT**-Fader, eine
Reduktionsanzeige und die Modi **NATURAL**, **FOCUS**, **CRUSH**.
NATURAL reagiert weicher und langsamer, FOCUS deutlicher auf Spitzen,
CRUSH schneller und kräftiger. AMOUNT steuert gemeinsam Eingriffstiefe
und Arbeitspunkt; es ist kein nachträglicher Ausgangslautstärkeregler.
Ausgangswert im Plugin: 60 %.

Eine repräsentative einzelne Vocalaufnahme abspielen und **LEARN** drücken.
Das Plugin sammelt ungefähr zehn Sekunden aktives Material; Stille zählt
nicht. Es misst typische und laute Pegel, die Spanne zwischen leisen und
lauten Phrasen sowie die Bewegung der Lautstärkehüllkurve. Daraus entstehen
Schwelle, angepasste Kompressionsstärke, Attack und Release. Sehr kurze hohe Einzelimpulse werden beim Lernen
ausgeklammert. Bei **PROFILE READY** ist das Profil abgeschlossen.
Mit **CANCEL** lässt sich der Versuch abbrechen. Bei zu wenig Material endet
er nach ungefähr 30 Sekunden mit **MORE VOCAL NEEDED**.

Ein bisheriges fertiges Profil bleibt bei Abbruch oder unzureichendem Material
erhalten. Das fertige Profil wird zusammen mit dem Projektzustand gespeichert.
Alte v1-Lernprofile werden mit ihrem bisherigen Verhalten geladen. Erst ein
erneuter LEARN-Durchgang ersetzt sie durch ein v2-Profil. Der LEARN-Knopf ist
als heller Holzknopf gestaltet.

Lernen ist eine lokale Signalstatistik: Es erkennt weder automatisch den Inhalt
eines Songs noch sicher, ob gerade Stimme, Rauschen oder ein Instrument spielt.
Für eine passende Referenz deshalb die eigentliche Vocalspur verwenden.

**AUTO GAIN** gleicht den gemessenen mittleren Leistungsunterschied langsam
aus, höchstens um +9 dB. Das ist keine garantierte wahrgenommene
Lautheitsgleichheit und kein Limiter; die Ausgangsspitzen weiterhin beachten.
Die maximale Kompressionsabsenkung ist auf 24 dB begrenzt. Stereo verwendet
einen gemeinsamen Detektor und gemeinsamen Gain. Bei 0 % AMOUNT erreicht
der Signalpfad nach der kurzen Reglersmoothingzeit das Originalsignal.

Die Engine arbeitet ohne Lookahead und ohne trainiertes Modell. Der Workflow
mit Lernphase ersetzt keinen Nachweis gleicher Technik oder Klangqualität
wie bei einem kommerziellen automatisch einstellenden Kompressor.

## GILLHEAT

**LOW**, **MID** und **HIGH** regeln die Sättigung jeweils von 0 bis 100 %.
Seit 0.3.0 wächst der Effekt stetig über den gesamten Reglerweg: Der verzerrte
Bandanteil wird von null bis vollständig eingeblendet, während sein interner
Drive von 24 bis 48 dB steigt. Eine stetige, auf einen festen Referenzpegel
kalibrierte Pegelkorrektur ersetzt den früheren späten Pegelanstieg. Sie ist
keine automatische Lautheitsregelung; Eingangspegel und Material bestimmen
weiterhin Klang und Lautheit. Bei Bedarf OUTPUT nachstellen.
Die Bandteilung ist fest und weich, ungefähr bei 180 Hz und
3 kHz. **WARM**, **TAPE** und **EDGE** wählen einen globalen Klangcharakter
für alle drei Bänder: leicht asymmetrisch weich, symmetrisch weich oder
kräftiger begrenzt. Die Namen beschreiben Klangrichtungen, keine vermessenen
Nachbauten bestimmter Hardware.

**MIX** dosiert den bearbeiteten Anteil. **OUTPUT** regelt anschließend den
Gesamtpegel von −24 bis +12 dB. Bei drei Drive-Werten von 0 % oder MIX 0 %
bleibt der zeitlich verzögerte saubere Pfad; OUTPUT wirkt weiterhin.
Die Ausgangswerte sind LOW 16,7 %, MID 25 %, HIGH 12,5 %, WARM, MIX 100 %,
OUTPUT 0 dB. Für vorhandene Projekte behalten die Host-Parameter intern ihre
ursprünglichen Kennungen und den Bereich 0 bis 24. 24 entspricht in der
Oberfläche 100 %. Dieselben gespeicherten Einstellungen bleiben lesbar,
klingen mit der überarbeiteten Kurve jedoch anders. Drive, Effektanteil,
Mix, Output und Moduswechsel werden über etwa 15 ms geglättet.

Die Verarbeitung verwendet intern zweiunddreißigfache Abtastrate, um unerwünschte
Aliasanteile der Nichtlinearität zu reduzieren. Die feste Latenz beträgt
24 Samples. Oversampling benötigt zusätzliche Rechenleistung; die tatsächliche
Last hängt von Rechner, Rate und Host ab. Sättigung verändert absichtlich Obertonstruktur und Dynamik;
hoher Drive kann eine Stimme rauer oder kleiner machen. Die Verarbeitung
garantiert weder völlige Aliasfreiheit noch die Reparatur geclippter Aufnahmen.

## GILLTUNE und GILLTUNE LIVE

Beide sind eigenständige Plugins mit denselben Controls und Presets. **GILLTUNE**
behält seine bisherige Kennung und gemeldete Verzögerung. **GILLTUNE LIVE** hat
eine eigene Kennung und kürzere Verzögerung für das Einsprechen. Der Modus ist
pro Plugin fest; die Latenz wird nicht während der Verarbeitung umgeschaltet.

Für eine einzelne, möglichst klare Gesangslinie zuerst **KEY** und **SCALE**
wählen. MAJOR bedeutet Dur, MINOR natürliche Molltonleiter. CHROMATIC lässt
alle Halbtöne zu; die Wahl von KEY ändert diese chromatische Auswahl nicht.
Die automatische Zielnote orientiert sich an der nächstliegenden erlaubten
Note mit leichter Hysterese gegen ständiges Umspringen.

Das neue **COMMAND WHEEL** zeigt die tatsächliche erkannte Eingangsnote,
Zielnote und den Verlauf der Abweichung in CENTS im runden grünen Monitor.
Der weiße Außenring ist der RETUNE-Regler mit eigener 0–200-ms-Skala; im
Monitor selbst wird der Regler nicht versehentlich bedient. Presets sind
direkt über fünf Tasten unten wählbar. Das Fenster startet mit 600 × 560 Pixeln.
RETUNE folgt beim Ziehen dem Winkel auf dem Ring und stoppt bei 0 beziehungsweise
200 ms, ohne am Endanschlag auf den anderen Wert umzuschlagen.

**RETUNE** ist die Reaktionszeit der Korrektur, **HUMANIZE** lässt bei
gehaltenen Tönen mehr natürliche Abweichung zu, **MIX** dosiert Dry/Wet.
RETUNE 0 ms bedeutet schnelle Korrektur, keine latenzfreie Verarbeitung.
Die fünf Presets ändern RETUNE, HUMANIZE und MIX, erhalten aber KEY und SCALE:

| Preset | RETUNE | HUMANIZE | MIX |
|---|---:|---:|---:|
| NATURAL | 100 ms | 80 % | 100 % |
| POP | 40 ms | 45 % | 100 % |
| RAP | 15 ms | 20 % | 100 % |
| TRAP | 5 ms | 5 % | 100 % |
| ROBOT | 0 ms | 0 % | 100 % |

Manuelle Abweichungen werden als CUSTOM angezeigt. Die Erkennung ist für
monophone Grundtöne ungefähr zwischen 70 und 1000 Hz ausgelegt. Akkorde,
mehrere gleichzeitige Sänger, starkes Rauschen, tiefe unregelmäßige Stimmen
oder starke Effekte können zu falschen Noten und hörbaren Artefakten führen.
Unstimmhaft erkannte Anteile werden zum zeitlich ausgerichteten Originalpfad
überblendet. Bereits passend gestimmte Töne innerhalb von etwa 0,5 Cent können
nach dem Übergang auf dem unveränderten verzögerten Originalpfad bleiben.

Seit 0.3.0 verwenden beide Plugins eine eigene periodensynchrone
Zeitbereichsverarbeitung mit bandbegrenzter Interpolation. Zeitlich zugeordnete
Tonhöhenmessungen und geglättete Übergänge reduzieren die in den Tests
nachgewiesenen Nebentöne der alten Engine. Es gibt **kein eigenes Formantmodell**:
größere Korrekturen können den Vokalklang verändern. Vollständige Formanterhaltung
oder Gleichheit mit Antares ist nicht garantiert.

GILLTUNE nutzt eine längere 64-Tap-Interpolation und behält die ursprüngliche
PDC zur Ausrichtung vorhandener Projekte sowie für längeren Detektorvorlauf.
LIVE nutzt 24 Taps und eine auf ganze Samples aufgerundete Verzögerung von 16 ms.
Die kürzere Interpolation hat eine weniger steile Filterwirkung und dämpft
sehr hohe Frequenzen etwas stärker. Tiefe Stimmen und Notenanfänge benötigen
weiterhin mehrere Schwingungen für die Erkennung.

| Samplerate | GILLTUNE Samples | GILLTUNE Zeit | LIVE Samples | LIVE Zeit |
|---|---:|---:|---:|---:|
| 22,05 kHz | 1024 | ca. 46,4 ms | 353 | ca. 16,01 ms |
| 44,1 kHz | 2048 | ca. 46,4 ms | 706 | ca. 16,01 ms |
| 48 kHz | 2048 | ca. 42,7 ms | 768 | 16 ms |
| 88,2 kHz | 4096 | ca. 46,4 ms | 1412 | ca. 16,01 ms |
| 96 kHz | 4096 | ca. 42,7 ms | 1536 | 16 ms |
| 192 kHz | 8192 | ca. 42,7 ms | 3072 | 16 ms |

Auch LIVE ist nicht latenzfrei. Projektwiedergabe profitiert von der
Latenzkompensation des Hosts; beim Einsprechen kommen Interface-/Hostpuffer hinzu.
Signalsmith Stretch wird nur noch für historische Vergleichstests der alten
Engine mitgeliefert und nicht in den aktuellen Tune-Plugins verwendet.

## Bedienung, Installation und Prüfung

Regler/Fader lassen sich ziehen oder über ihren Zahlenwert einstellen.
Doppelklick setzt den jeweiligen Wert zurück. Die Fenster lassen sich
proportional skalieren. Bypass ist im Plugin und über den Host verfügbar.

Die vollständigen VST3-Pakete mit ihrer `Contents`-Struktur in den üblichen
VST3-Ordner kopieren, beispielsweise:

```text
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLFLOW.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLHEAT.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLTUNE.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLTUNE LIVE.vst3
```

Anschließend in FL Studios Plugin-Manager suchen und das gewünschte Produkt
als Mixer-Effekt laden. Dies sind Installationshinweise, keine Bestätigung
einer bereits durchgeführten Installation oder Hörprüfung.

Die C++-Tests verwenden definierte synthetische Signale. Deren Zahlen gelten
für diese Signale und ersetzen weder breite echte Sprachaufnahmen noch einen
subjektiven Hörvergleich. Build-Anleitung und Testaufrufe stehen in
[BUILDING.md](BUILDING.md). Grenzen und Methoden der Flow-Prüfung stehen in
[Tests/FLOW-DSP-VERIFICATION.md](Tests/FLOW-DSP-VERIFICATION.md).
Aktuelle Tune-Ergebnisse stehen in [Tests/TUNE-0.3.0-VALIDATION.md](Tests/TUNE-0.3.0-VALIDATION.md),
die HEAT-Kurvenprüfung in [Tests/HeatFixtures/RESULTS-v03.md](Tests/HeatFixtures/RESULTS-v03.md)
und die native Release-Prüfung in
[Tests/NATIVE-VOCAL-RESTORATION-V03-VALIDATION.md](Tests/NATIVE-VOCAL-RESTORATION-V03-VALIDATION.md).
Ältere TuneDSP-Berichte dokumentieren den damaligen spektralen Ansatz, nicht die aktuelle Engine.

Die eigenen Programmdateien werden unter **AGPL-3.0-only** bereitgestellt.
JUCE wird über seine AGPL-v3-Option genutzt; die für historische Tests
mitgelieferten Signalsmith-Bestandteile behalten ihre MIT-Lizenzen.
Siehe [LICENSE](LICENSE), [LICENSE-NOTICE.md](LICENSE-NOTICE.md)
und [THIRD-PARTY.md](THIRD-PARTY.md).
