# GILL-DE-ESSER 0.2.0

Aktueller Quellstand der Release-Runde 04: **0.2.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Ein frequenzselektiver De-Esser für Windows x64 / VST3: helle Eiche,
elfenbeinweiße Regler, Salbeigrün und das kleine originale GP-Logo.
Er soll scharfe, rauschähnliche S-Laute im gewählten Frequenzbereich absenken.

## Bedienung

- **AMOUNT:** Stärke und Empfindlichkeit der Bearbeitung, 0–100 %. Ausgangswert: 55 %.
- **FREQUENCY:** Mitte des überwachten Filterbands, 2,5–12 kHz. Ausgangswert: 6,50 kHz.
- **LISTEN S:** Das gewählte Filterband zum Einstellen abhören. Anschließend wieder ausschalten.
- **REDUCTION:** Momentane Absenkung am Bandzentrum. Das ist weder eine Messung der gesamten Ausgangslautstärke noch die tatsächlich isolierte Lautstärke eines S-Lauts.

Die Regler ziehen oder den Wert direkt eingeben. Dezimalkomma und Dezimalpunkt
werden unterstützt. Frequenzen können in Hz oder kHz eingegeben werden.
Doppelklick setzt den jeweiligen Regler auf seinen Ausgangswert zurück.
Pfeiltasten ändern fokussierte Bedienelemente. Die Bandmarkierung im Spektrum
kann ebenfalls gezogen werden; ihr Doppelklick stellt 6,50 kHz wieder her.

Mit LISTEN S die störende Zischfrequenz suchen, danach LISTEN S ausschalten
und AMOUNT nach Gehör einstellen. Eine höhere Einstellung kann die Stimme
matter machen oder Lispeln hervorrufen. Alle vier Hostparameter — AMOUNT,
FREQUENCY, LISTEN S und BYPASS — werden automatisiert und im Projekt gespeichert.
Den Bypass bedient der Host-Wrapper.

Das Fenster startet mit **640 × 420 Pixeln** und ist zwischen ungefähr
560 × 368 und 1280 × 840 Pixeln mit festem Seitenverhältnis skalierbar.
Das Spektrum zeigt PRE / POST des **linken Kanals** in dBFS; die schattierte
Zone entspricht den −3-dB-Grenzen des ausgewählten Bandpassfilters.
Die Anzeige ist ein endliches, gefenstertes Spektrum und keine Messung aller
Signalanteile mit unbegrenzter Frequenzauflösung.

## Verarbeitung und Grenzen

Die eigene klassische DSP-Implementierung verwendet einen Bandpass und eine
dynamische Absenkung seines Anteils im Originalsignal. Die Erkennung berücksichtigt
Bandenergie, Gesamtenergie und zeitliche Vorhersagbarkeit des Signals.
So sollen rauschähnliche Zischlaute eher bearbeitet werden als gleichmäßige Töne.
Das ist eine Heuristik, keine Spracherkennung oder sichere Trennung einzelner Laute.

Zeitkonstanten und Bandbreite sind automatisch beziehungsweise fest vorgegeben;
es gibt keine zusätzlichen Attack-, Release-, Threshold- oder Q-Regler.
Die maximale momentane Absenkung am Bandzentrum beträgt **12 dB** bei 100 %
AMOUNT. Die Absenkung eines breit verteilten Zischlauts fällt abhängig von
Frequenzverteilung und Erkennung geringer aus. Die Stereoerkennung verwendet
eine gemeinsame Absenkung, damit die Kanäle nicht unabhängig pumpen.

**LISTEN S gibt das komplette gewählte Filterband aus.** Darin können auch
Stimmanteile, Atem, Instrumente und Hintergrundgeräusche liegen. Es ist weder
ein isolierter S-Laut noch ausschließlich das entfernte Signal. Bereits
verzerrte oder geclippte Zischlaute werden nicht rekonstruiert. Hall, Klicks,
Knistergeräusche und allgemeines Rauschen sind nicht der Zweck dieses Plugins.
Eine perfekt unveränderte Stimme oder vollständige Entfernung aller scharfen
Laute wird nicht zugesichert.

Mono und Stereo sowie 32- und 64-Bit-Audioverarbeitung sind implementiert.
Das Plugin meldet **0 Samples zusätzliche Latenz**. Die gemeldete kurze
Filterausklingzeit beträgt 50 ms und ist keine Verzögerung des Hauptsignals.
Nach Stille sammelt der Detektor zunächst etwa 8 ms Signalstatistik, um einen
neu einsetzenden gleichmäßigen hohen Ton besser zu schützen. Das Audio wird
dabei nicht aufgehalten; der erste Teil eines neuen S-Lauts kann daher noch
unbearbeitet durchlaufen.
Hostpuffer, Audiointerface und andere Plugins können weiterhin Verzögerung
verursachen. Ein eigener Focusrite- oder anderer Gerätetreiber ist für das
Plugin nicht nötig; das Audiogerät bleibt im Host eingestellt.

Bei 0 % AMOUNT liefert der normale Signalpfad nach einer laufenden
Parameteränderung das unveränderte Original. Bypass liefert das Original;
LISTEN S und Bypass werden beim Umschalten über etwa 5 ms überblendet.
Bei sehr niedrigen Samplerates wird das tatsächliche Bandzentrum zusätzlich
auf höchstens 45 % der Samplerate begrenzt.

## Installation

Das vollständige VST3-Paket mit seiner enthaltenen `Contents`-Ordnerstruktur
in den VST3-Ordner kopieren, beispielsweise:

```text
C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILL-DE-ESSER.vst3
```

Anschließend im Plugin-Manager von FL Studio nach Plugins suchen und
**GILL-DE-ESSER** als Mixer-Effekt laden. Pro Produkt nur eine Kopie in den
durchsuchten Pluginordnern belassen. Diese Hinweise bestätigen keine bereits
ausgeführte Installation oder Hörprüfung auf einem bestimmten Rechner.

## Prüfungen, Quellcode und Lizenz

`Tests/DeEsserTests.cpp` enthält numerische Filterprüfungen, synthetische
Stimmharmonische und unabhängig erzeugte, frequenzbegrenzte Rauschbursts als
S-Laut-Modell. `Tests/PluginTests.cpp` prüft den echten Prozessor und seine
Bedienelemente, Audiopfade, Zustandsspeicherung und Darstellung.
Die enthaltenen WAVs werden lokal aus dem C++-Test erzeugt. Es sind keine
Aufnahmen echter Sprecher und keine privaten Nutzerdateien.

Die Messungen beschreiben diese definierten Signale. Sie ersetzen weder eine
breite Prüfung realer Sprecher noch einen subjektiven Hörvergleich oder die
Prüfung eines vollständigen FL-Studio-Projekts. Die aktuellen maschinenlesbaren
Ergebnisse stehen bei den Tests; native Host-Ergebnisse werden separat
dokumentiert. Aus dem Vorhandensein eines Tests folgt kein bestandener Testlauf.

Build-Anleitung: [BUILDING.md](BUILDING.md). Die eigenen Programmdateien werden
unter **AGPL-3.0-only** bereitgestellt; JUCE 8.0.12 wird über seine AGPL-v3-Option
verwendet. Lizenztexte und Hinweise: [LICENSE](LICENSE),
[LICENSE-NOTICE.md](LICENSE-NOTICE.md), [THIRD-PARTY.md](THIRD-PARTY.md).
Zum entsprechenden Quellpaket gehören Projekt, Tests, Assets und der
vollständige verwendete JUCE-Quellstand mit seinen Lizenzhinweisen.
