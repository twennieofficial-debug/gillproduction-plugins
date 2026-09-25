# GILLEQ 0.4.0 · GILLPRODUCTION

Aktueller Quellstand der Release-Runde 04: **0.4.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

GILLEQ ist ein parametrischer Equalizer für Windows 64 Bit im **Oak-Sage-Design von Konzept 07**: helle Eiche, Salbeigrün, dunkle Anzeigen und das ursprüngliche GP-Logo. Seit Version **0.3.0** ist die Dynamik direkt im Frequenzdiagramm bedienbar. Version **0.4.0** behält diese Funktion in kompakterer Oberfläche. Die Bedienung macht die Dynamik direkt im Frequenzdiagramm bedienbar. Alle acht Bänder behalten ihren eigenen **STATIC / DYNAMIC**-Schalter und ihre Einstellungen. Die Pluginoberfläche verwendet Großbuchstaben.

## In FL Studio verwenden

Im Mixer einen Effekt-Slot öffnen, **More Plugins** wählen und nach **GILLEQ** suchen. Falls es noch nicht erscheint, im Plugin Manager eine Suche nach installierten Plugins starten. Bei einer manuellen Übernahme das vollständige **GILLEQ.vst3**-Paket zusammenhalten.

GILLEQ verarbeitet digitales Audio innerhalb von FL Studio. Es braucht **keine besondere Focusrite-Version**: Audiointerface und Treiber werden unabhängig vom Plugin in den Audioeinstellungen von FL Studio ausgewählt. Diese Ausgabe ist für Windows; eine macOS-Ausgabe gehört noch nicht dazu.

## Die wichtigsten Regler

| Bedienung | Wirkung |
|---|---|
| **BAND 1–8 / ON** | Eines von acht Bändern auswählen und einzeln ein- oder ausschalten. |
| **FREQUENCY** | 20 Hz bis 20 kHz; bei niedrigen Sampleraten auf 49 % der Samplerate begrenzt. Zahlenwerte lassen sich direkt eingeben. |
| **GAIN** | Anhebung/Absenkung von −24 bis +24 dB für Bell und Shelves, in 0,01-dB-Schritten. |
| **Q** | Bandbreite beziehungsweise Resonanz von 0,10 bis 18,00. Größere Werte wirken schmaler beziehungsweise resonanter. |
| **TYPE** | BELL, LOW SHELF, HIGH SHELF, LOW CUT, HIGH CUT oder NOTCH. |
| **SLOPE** | Für LOW CUT und HIGH CUT: 12, 24 oder 48 dB/Oktave. |
| **CHANNEL** | Das jeweilige Band bearbeitet STEREO, MID, SIDE, LEFT oder RIGHT. |
| **STATIC / DYNAMIC** | Für das ausgewählte Bell-/Shelf-Band eine feste oder pegelabhängige EQ-Kurve wählen. |
| **OUTPUT** | Gesamtpegel von −24 bis +24 dB. |
| **BYPASS** | Das Eingangssignal ohne EQ, Output-Trim oder Delta abhören. |

Einen Punkt in der Kurve ziehen, um Frequenz und bei Bell/Shelves den Gain zu ändern. **Shift + Ziehen** oder das **Mausrad über dem Punkt** verändert Q. Ein **Rechtsklick** schaltet das Band ein oder aus. Ein Doppelklick auf einen Punkt setzt seinen Gain auf 0 dB. Beim ausgewählten Band sind die Zahlen unter den Reglern direkt bearbeitbar.

## Ein Band dynamisch bearbeiten

Ein Band beziehungsweise seinen Punkt auswählen und **STATIC** auf **DYNAMIC** umschalten. Direkt beim ausgewählten Punkt erscheint eine kompakte Bandkarte; andere Bänder behalten ihre eigenen Einstellungen. Den **RANGE-Diamanten** senkrecht ziehen, um die zusätzliche Verstärkung zu ändern. **THRESHOLD** an der separaten dBFS-Skala der Bandkarte ziehen. Die exakten Werte sowie **ATTACK** und **RELEASE** lassen sich direkt in dieser Karte eingeben. Es gibt dafür keine untere Reglerleiste mehr. BELL, LOW SHELF und HIGH SHELF unterstützen Dynamik. Bei LOW CUT, HIGH CUT und NOTCH zeigt der Schalter **STATIC ONLY**.

| Regler | Wirkung |
|---|---|
| **THRESHOLD** | Schwelle des frequenzabhängigen Eingangspegels: −80 bis 0 dBFS RMS. Oberhalb beginnt die zusätzliche Bearbeitung. |
| **RANGE** | Maximale zusätzliche Änderung: −24 bis +24 dB. Negative Werte senken laute Anteile ab, positive heben sie an. |
| **ATTACK** | Zeitkonstante der zunehmenden Verstärkungsänderung: 0,1 bis 200 ms. |
| **RELEASE** | Zeitkonstante der Rückkehr: 10 bis 2.000 ms. |

**GAIN bleibt die Grundkurve.** Ein Beispiel: THRESHOLD −24 dBFS und RANGE −6 dB bewirken bei einem Detektorpegel von −18 dBFS nach dem Einschwingen zusätzlich −3 dB. Ab −12 dBFS wird die Grenze von −6 dB erreicht. Die zusätzliche Änderung entspricht der halben Schwellenüberschreitung, begrenzt durch RANGE; beim Absenken entspricht das 2:1-Kompression. Grundverstärkung und Dynamik zusammen sind pro Band auf −24 bis +24 dB begrenzt.

**IN** zeigt den gemessenen Pegel im Frequenzbereich des Bandes in dBFS, **LIVE** die tatsächlich angewendete Änderung in dB. Der normale Punkt bleibt bei der eingestellten Grundverstärkung; der kleine Live-Punkt und die Kurve zeigen die laufende Bearbeitung. Der RANGE-Diamant markiert die eingestellte dynamische Grenze. Die dBFS-Skala der Bandkarte gehört zum Detektor; die senkrechte dB-Achse des großen Graphen gehört zur EQ-Verstärkung.

Der Detektor misst RMS, nicht Spitzenpegel: Ein Sinuston mit Spitze 0 dBFS hat etwa −3,01 dBFS RMS. Jedes Band wertet den ursprünglichen Plugin-Eingang aus. Bei STEREO reagieren beide Kanäle gemeinsam auf den stärkeren Kanal; MID, SIDE, LEFT und RIGHT verwenden jeweils ihren passenden Signalanteil. Im Monobetrieb wirken SIDE und RIGHT nicht.

Die Pegelmessung mittelt das Signal, bei tiefen Frequenzen länger. ATTACK und RELEASE wirken anschließend auf die Verstärkung; die gesamte Reaktion auf einen neuen Ton umfasst daher zusätzlich die Messzeit. Nach einer Zeitkonstante sind bei konstantem Zielwert ungefähr 63 % der Änderung erreicht. Der Plugin-Audiopfad erhält dadurch keine zusätzliche Latenz. Details und Messwerte stehen in [DynamicVerification.md](Tests/DynamicVerification.md).

## Hören und vergleichen

**DELTA LISTEN** spielt die Differenz „bearbeiteter Ausgang minus Eingang“ ab, einschließlich OUTPUT. Damit lässt sich hören, was die Einstellung verändert. Für normales Abhören Delta wieder ausschalten.

**A > B** legt die aktuelle Einstellung im Vergleichsspeicher ab. **A / B** wechselt zwischen der aktuellen und der gespeicherten Einstellung. **RESET** setzt alle Parameter auf ihre Ausgangswerte zurück.

**ANALYZER** zeigt das Spektrum vor und nach der Bearbeitung. Er analysiert ausschließlich den linken Kanal, im Monobetrieb den vorhandenen Kanal. **FREEZE** hält die Spektrumanzeige an; die Dynamik arbeitet weiter. Die EQ-Kurven verwenden denselben Filterentwurf wie die Audioverarbeitung und berücksichtigen die zuletzt gemeldete dynamische Änderung. Bei unterschiedlicher Kanalbearbeitung ist der angezeigte Kanalmodus entscheidend. Während der kurzen Überblendung von Filtertypen oder Kanalmodi zeigt die Kurve den aktuellen Zielzweig.

GILLEQ meldet **0 Samples zusätzliche Pluginlatenz**. Die Pufferlatenz des Audiointerfaces bleibt davon unabhängig. Parameteränderungen werden geglättet. Starke Anhebungen können den Ausgang über 0 dBFS bringen; bei Bedarf OUTPUT reduzieren. Es gibt keinen eingebauten Limiter, der die gewünschte EQ-Kurve verändert.

Bei gemischter Kanalbearbeitung zeigt **MID REF** die Antwort auf ein mittiges Signal (links = rechts), **SIDE REF** auf ein seitliches Signal (links = minus rechts), **LEFT REF/RIGHT REF** auf ein Signal im jeweiligen Eingangskanal. Eine einzige, vom Eingang unabhängige Gesamt-Stereokurve wäre hier irreführend. Die gestrichelte Linie zeigt zusätzlich das ausgewählte Einzelband.

## Quellcode und Lizenz

Die Audioverarbeitung einschließlich der Dynamik ist eine eigene Implementierung auf Grundlage öffentlich dokumentierter Filtergleichungen. Pro-Q 4 dient als Bedienreferenz; GILLEQ übernimmt weder proprietären FabFilter-Code noch beansprucht es identisches Verhalten oder denselben Funktionsumfang. Diese Ausgabe enthält beispielsweise keinen externen Sidechain, keine automatische Schwellenwertfindung und keinen Linear-Phase-Modus.

Diese Fassung nutzt **JUCE 8 unter GNU AGPL v3**. Die originalen GILLEQ-Projektdateien werden in dieser Fassung unter **AGPL-3.0-only** bereitgestellt. Bei Weitergabe der Software müssen auch der entsprechende Quellcode, die Lizenz und die erforderlichen Hinweise zugänglich bleiben. Privates Verwenden verpflichtet nicht dazu, das eigene Musikprojekt zu veröffentlichen. Eine spätere geschlossene Produktveröffentlichung erfordert vorher eine passende alternative JUCE-Lizenz. Siehe [JUCE-Lizenzhinweise](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md) und [AGPL v3](https://www.gnu.org/licenses/agpl-3.0.html).

**GILLEQ-QUELLCODE.zip** enthält die Projektdateien, vollständige JUCE-Quellen mit festgehaltener Version, Lizenzen und eine Build-Anleitung. Ergebnisse der tatsächlich ausgeführten Tests und der Installation stehen im separaten Abschlussbericht.

## Alte Projekte und Nachweise

Pluginname und Identität bleiben erhalten. Die bisherigen 59 Parameter behalten ihre IDs und Reihenfolge. Die 40 Dynamikparameter folgen dahinter. Beim Laden eines alten v0.1-Zustands starten die neuen Parameter ausdrücklich im statischen Standardzustand; ein zuvor geöffnetes dynamisches Preset darf diesen Zustand nicht beeinflussen. A/B-Vergleiche und neue gespeicherte Zustände umfassen auch die Dynamikparameter.

Der unabhängige DSP-Test verglich die neue statische Verarbeitung 500-mal mit dem archivierten v0.1-Quellstand und erhielt samplegenau identische Ergebnisse. Die dynamische Prüfung bestand 73.913 Assertions, darunter 12.000 tatsächlich verarbeitete Parameterkonfigurationen; der größte gemessene stationäre Gainfehler betrug 0,00954 dB. Die bestehende statische Suite bestand weitere 1.246.260 Assertions. Das sind automatisierte technische Prüfungen, keine Behauptung, jeder Bedienknopf sei zehntausendmal von Hand getestet worden. Sie beweisen auch keine Fehlerfreiheit für alle Audiosignale oder Hosts. Siehe [dynamische Prüfung](Tests/DynamicVerification.md), [statische Prüfung](Tests/DSP-VERIFICATION.md) und [Build-Anleitung](BUILDING.md). Native VST3-, GUI- und FL-Studio-Ergebnisse werden im finalen Lieferbericht gesondert ausgewiesen.
