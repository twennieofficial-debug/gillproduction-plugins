# GILLAIR / GILLSPACE / GILLECHO / GILLBALANCE 0.3.0

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Vier eigenständige Windows-x64-VST3-Effekte im gemeinsamen Oak-Sage-Design:
helle Eiche, elfenbeinweiße Bedienelemente, Salbeigrün und kleines GP-Logo.
Diese Datei beschreibt den Quellstand und die Bedienung. Build-, Host- und
Installationsprüfungen werden separat protokolliert.

| Plugin | Aufgabe | Eigene Latenz bei 48 kHz |
|---|---|---:|
| **GILLAIR** | Obertonanreicherung für Präsenz und Höhen | 24 Samples / 0,5 ms |
| **GILLSPACE** | Algorithmischer Raum-, Hall- und Plate-Nachhall | 0 Samples |
| **GILLECHO** | Freies oder temposynchrones Delay | 0 Samples |
| **GILLBALANCE** | Lernender spektraler und dynamischer Vocal-EQ | 0 Samples |

Die Wrapper unterstützen Mono/Stereo, 32-Bit-Float-Verarbeitung und
Samplerates von **8 bis 384 kHz**. Außerhalb dieses Bereichs wird das zeitlich
ausgerichtete Original ausgegeben. Pluginlatenz kommt zu Hostpuffer,
Audiogerät und weiteren Effekten hinzu. Bei Space und Echo ist die gewünschte
Hall-/Echoverzögerung Teil des Effekts; der direkte Dry-Pfad wird nicht verzögert.

## GILLAIR

**MID AIR** und **HIGH AIR** erzeugen zusätzliche gerade und ungerade
Obertöne in den oberen Präsenz- und Höhenregionen. Die beiden weichen,
überlappenden Quellbereiche orientieren sich an ungefähr 1,8 und 6,5 kHz;
bei niedriger Samplerate werden sie an die verfügbare Bandbreite angepasst.
Die Anreicherung reagiert auf das Eingangssignal. Version 0.2 erweitert den oberen
Reglerbereich deutlich: zusätzliche harmonische Anreicherung und mehr lineare
Präsenz bei hohen Werten. Der untere Bereich erlaubt weiter eine feine Dosierung.

**MIX** dosiert die zusätzliche Anreicherung. **OUTPUT** regelt danach das
gesamte Signal von −18 bis +6 dB. Ausgangswerte: MID AIR 15 %, HIGH AIR 15 %,
MIX 100 %, OUTPUT 0 dB. Beide AIR-Regler auf 0 oder MIX auf 0 ergeben nach
dem kurzen Glätten das verzögerte Original; OUTPUT wirkt weiterhin.

Der zusätzliche Signalpfad arbeitet intern achtfach oversampled. Die feste
Latenz beträgt 24 Samples. Hochpassfilter entfernen erzeugte Gleichspannung
und tiefe Anteile aus dem Zusatzsignal. Dies ist eine eigene Exciter-Schaltung
in Software, keine Rauschreparatur und kein automatischer De-Esser.

## GILLSPACE

Die Charaktere **ROOM**, **HALL** und **PLATE** verwenden einen eigenen
algorithmischen Nachhall mit Eingangsdiffusion, frühen Reflexionen und acht
gekoppelten Verzögerungswegen. Die Namen beschreiben Klangrichtungen;
es werden keine aufgenommenen Raumimpulsantworten verwendet.

**DECAY** stellt die nominelle tieffrequente RT60-Abklingzeit von 0,2 bis
15 Sekunden ein. **PREDELAY** verzögert den Halleinsatz um 0–200 ms.
**TONE** bestimmt die Helligkeit, **SIZE** die interne Raumskala und
**WIDTH** die Breite des Wet-Signals. **MIX** ist ein linearer Dry/Wet-Regler:
Bei DRY 100 % ist MIX 0 % trocken, MIX 100 % vollständig Wet.
Der neue separate **DRY**-Regler senkt nur den Direktanteil ab: **DRY 0 %**
entfernt die direkte Stimme und erhält den bisherigen Hallpegel. Für einen Send
DRY 0 % und MIX 100 % einstellen. Beide auf 0 ergibt Stille. Der Wet-Zweig
wird weiter mit dem Eingangssignal gespeist; DRY ist kein Eingangsmute.
Änderungen werden geglättet; nach einem Presetwechsel kann der bestehende
Hall hörbar in den neuen Charakter übergehen.

Die untere Presetleiste bietet zwölf Host-Presets. **MIX LOCK** erhält beim
Laden den aktuellen MIX-Wert. DRY und Bypass bleiben bei jedem Preset erhalten.
Alte Projektzustände ohne DRY-Parameter laden mit DRY 100 % und behalten damit
den bisherigen Klang und die bisherigen Automationskennungen.

| Preset | Charakter | DECAY | MIX |
|---|---|---:|---:|
| VOCAL ROOM | ROOM | 0,75 s | 18 % |
| TIGHT BOOTH | ROOM | 0,30 s | 10 % |
| WARM CHAMBER | ROOM | 1,35 s | 22 % |
| DRUM ROOM | ROOM | 0,60 s | 16 % |
| SHORT PLATE | PLATE | 0,95 s | 20 % |
| VOCAL PLATE | PLATE | 1,80 s | 24 % |
| BRIGHT PLATE | PLATE | 2,70 s | 26 % |
| LUSH HALL | HALL | 3,80 s | 28 % |
| WIDE STAGE | HALL | 2,60 s | 22 % |
| DARK SPACE | HALL | 6,50 s | 35 % |
| AMBIENT BLOOM | HALL | 10,00 s | 50 % |
| LONG CATHEDRAL | HALL | 14,00 s | 55 % |

Der gemeldete Tail berücksichtigt konservativ die längste Abklingzeit seit
dem letzten Reset. Dadurch kann der Host einen bereits laufenden langen
Hall nach dem Wechsel auf ein kurzes Preset weiter ausgeben.

## GILLECHO

Bei ausgeschaltetem **SYNC** regelt **TIME** die Verzögerung direkt von
1 bis 8000 ms. Mit SYNC folgt **DIVISION** dem Hosttempo: 1/16, 1/8-Triolen,
1/8, punktierte 1/8, 1/4, punktierte 1/4, 1/2 oder ganze Note.
Eine Viertelnote entspricht `60000 / BPM` Millisekunden.

Fehlt ein gültiges Hosttempo, verwendet das Plugin den manuellen **TEMPO**-
Wert von 20–300 BPM; dessen Ausgangswert ist **120 BPM**. Sobald ein gültiges
Hosttempo vorliegt, hat es für SYNC Vorrang. Presets überschreiben weder
das Hosttempo noch diesen manuellen Ersatzwert. SYNC selbst und die Division
gehören zum jeweiligen Preset.

Auch synchronisierte Verzögerungen haben eine **8-Sekunden-Obergrenze**.
Ergibt die gewählte Division bei sehr langsamem Tempo mehr, wird die echte
Verzögerung auf 8000 ms begrenzt. Zeitwechsel blenden zwei feste Lesepositionen
über ungefähr 35 ms über; sehr schnelle Folgeänderungen werden zusammengefasst.

**FEEDBACK** bestimmt die Rückkopplung bis 90 %. **COLOR** macht Wiederholungen
bei höheren Werten dunkler. **WIDTH** regelt die Wet-Breite. **CLEAN** bleibt
bei COLOR 0 linear, **TAPE** fügt weiche Sättigung hinzu. Bei **PINGPONG** und
voller Breite beginnt die zu Mono zusammengefasste Wiederholung links und
wechselt anschließend zwischen links und rechts. WIDTH 0 zentriert sie;
in Mono arbeitet eine normale einzelne Rückkopplung. **MIX** ist linear Dry/Wet.
**DRY** senkt den vorhandenen Direktanteil unabhängig von den Wiederholungen:
0 % lässt nur das Echo stehen. Für Sends DRY 0 % und MIX 100 % verwenden.
DRY bleibt bei jedem Presetwechsel erhalten; alte Projekte laden DRY 100 %.

Die untere Presetleiste enthält diese zwölf Host-Presets. MIX LOCK erhält MIX;
Bypass, MIX LOCK und der manuelle TEMPO-Wert bleiben beim Laden erhalten.

| Preset | Zeitbasis | Charakter | FEEDBACK | MIX |
|---|---|---|---:|---:|
| RAP QUARTER | 1/4 Sync | CLEAN | 28 % | 18 % |
| TRAP PING | punktierte 1/8 Sync | PINGPONG | 42 % | 23 % |
| VOCAL SLAP | 95 ms | TAPE | 8 % | 14 % |
| TIGHT DOUBLE | 28 ms | CLEAN | 0 % | 15 % |
| EIGHTH CLEAN | 1/8 Sync | CLEAN | 25 % | 20 % |
| DOTTED AIR | punktierte 1/8 Sync | PINGPONG | 35 % | 24 % |
| TRIPLET FLOW | 1/8-Triolen Sync | CLEAN | 38 % | 20 % |
| WARM TAPE | 1/4 Sync | TAPE | 40 % | 22 % |
| DARK THROW | punktierte 1/4 Sync | TAPE | 55 % | 30 % |
| WIDE HALF | 1/2 Sync | PINGPONG | 45 % | 25 % |
| DREAM REPEATS | ganze Note Sync | PINGPONG | 68 % | 35 % |
| LONG TRAIL | 1400 ms | TAPE | 72 % | 38 % |

Hohe Rückkopplung kann sehr lange hörbar bleiben. Die Tail-Meldung deckt
konservativ einen Abfall bis −80 dB ab: Bei 8 Sekunden und 90 % Feedback sind
das ungefähr **712 Sekunden**. Der direkte Dry-Pfad bleibt latenzfrei.
Die vollständigen aktuellen Presetwerte beider Effekte stehen in
[Source/Presets.h](Source/Presets.h).

## GILLBALANCE

Eine repräsentative einzelne Vocalspur abspielen und **LEARN** starten.
Das Plugin sammelt ungefähr zehn Sekunden aktives Material. Stille und
ungeeignete starke Einzelspitzen zählen nicht; bei zu wenig Material endet
der Versuch nach ungefähr 30 Sekunden. Mindestens drei brauchbare
Frequenzbereiche sind für ein fertiges Profil nötig. **CANCEL** bricht die
laufende Analyse ab und erhält ein zuvor fertig gelerntes Profil.

**TARGET** bietet **NATURAL**, **POP**, **RAP LEAD**, **TRAP AIR** und
**DARK RAP**. Ausgangspunkt ist RAP LEAD bei **AMOUNT 60 %**. Ohne fertiges
Profil bleibt die EQ-Korrektur neutral. Mit einem Profil dosiert AMOUNT die
Korrektur von 0 bis 100 %; ein Targetwechsel verwendet dasselbe Profil.

Version 0.2 kombiniert eine **4096-Punkt-Spektralanalyse** mit den robusten
Statistiken von acht breiten Vocalbereichen. Beide Stereokanäle tragen getrennt
zur Spektralenergie bei. Das Profil speichert 256 logarithmische Messpunkte
zwischen 20 Hz und 20 kHz beziehungsweise der verfügbaren Bandbreite.
Bei hohen Sampleraten wird nur die Analyse auf maximal 48 kHz reduziert;
der Audiopfad bleibt in der Samplerate des Hosts.

Aus dem gemessenen Spektrum entstehen **bis zu zwölf schmale Resonanz-Cuts**
mit individuellen Frequenzen und Q-Werten. Sie ergänzen die bisherige breite
Klangkorrektur. Außerdem reagieren **acht dynamische EQ-Bänder** auf spätere
spektrale Überbetonungen gegenüber dem gelernten Profil. Die Detektoren
berücksichtigen den Gesamtpegel: Eine lediglich lauter gesungene Passage
wird nicht automatisch stärker gefiltert. AMOUNT dosiert alle drei Stufen.
Es gibt keinen Lookahead und keine zusätzliche Audiolatenz.

Das Diagramm zeigt die gesamte aktuelle Filterkurve, erkannte Resonanzen
und das gelernte Spektrum. Die kleinen Pegelbalken messen weiterhin echte
Bandpegel vor und nach der Verarbeitung. Das gespeicherte Profil selbst
bleibt fest; die dynamischen Absenkungen reagieren während der Wiedergabe.
Die neue LEARN-Taste hat eine Holzoberfläche.

**Alte v1-Profile behalten ihre bisherige statische Verarbeitung.** Ein erneuter
LEARN-Durchlauf erzeugt das neue spektrale Profil. Projektzustand, Reset und
Sampleratenwechsel erhalten das gespeicherte Profil. Fehlgeschlagene oder
abgebrochene Analyse ersetzt kein brauchbares vorheriges Profil.

Die Targets sind eigene moderate Klangrichtungen. Sie enthalten keine
Künstlerprofile oder trainierten Referenzstimmen. Die Analyse unterscheidet
Stimme, Instrumente und Rauschen nicht sicher. BALANCE entfernt daher weder
zuverlässig Hintergrundgeräusche noch macht es jede Aufnahme automatisch
zu einer professionellen Produktion. Für ein sinnvolles Profil die eigentliche
Vocalspur verwenden und das Ergebnis im Mix beurteilen.

## Gemeinsame Bedienung und Installation

Regler/Fader lassen sich ziehen oder über ihren Zahlenwert einstellen.
Doppelklick setzt den jeweiligen Ausgangswert zurück. Die Fenster lassen
sich proportional skalieren. Native- und Host-Bypass verwenden denselben
zeitlich passenden Dry-Pfad mit kurzer Überblendung. Die vollständigen
Parameter und fertige Balance-Profile werden im Projektzustand gespeichert.

Bei Space und Echo zeigt die untere Presetleiste **CUSTOM**, wenn relevante
Werte vom gewählten Preset abweichen. Bei aktivem MIX LOCK wird eine
abweichende MIX-Einstellung dabei absichtlich nicht als Presetänderung gewertet.

Die vollständigen VST3-Pakete einschließlich `Contents` in den gewünschten
VST3-Ordner kopieren, beispielsweise unter:

```text
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLAIR.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLSPACE.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLECHO.vst3
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLBALANCE.vst3
```

Danach im Plugin-Manager des Hosts suchen und als Mixer-Effekt laden.
Diese Hinweise bestätigen keine bereits erfolgte Installation oder Hörprüfung.
Der vorhandene Audiogerätetreiber bleibt im Host ausgewählt.

Numerische Tests verwenden eigene synthetische Signale. Ihre Messwerte gelten
für die ausdrücklich getesteten Fälle und ersetzen keine Prüfung vieler echter
Aufnahmen. Build- und Testanleitung: [BUILDING.md](BUILDING.md).

Die eigenen Dateien stehen unter **AGPL-3.0-only**. JUCE wird über seine
AGPL-v3-Option genutzt. Lizenztexte und Abhängigkeiten:
[LICENSE](LICENSE), [LICENSE-NOTICE.md](LICENSE-NOTICE.md),
[THIRD-PARTY.md](THIRD-PARTY.md).
