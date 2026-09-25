# GILLPRODUCTION – UPDATE 07

Das Paket ergänzt GILLMIX mit dem Spurhelfer GILLLINK sowie GILLHARMONY, GILLREFERENCE und GILLRESCUE. Die vorhandenen 34 Plugins bleiben enthalten; insgesamt sind es 39 VST3-Einträge. Die neuen Werkzeuge haben kompakte Holzoberflächen, Presets und LIVE/PRO.

**GILLMIX: Main, Doubles, Adlibs und Beat gemeinsam ausbalancieren**

1. Setze GILLMIX auf den Master. Setze GILLLINK als letzten Effekt auf jede Quellspur, die mitmachen soll. Stelle ihre FL-Mixerfader zunächst auf 0 dB. Erfasse dieselbe Quelle nicht zusätzlich noch einmal auf ihrem gemeinsamen Bus.
2. Gib den Links verständliche Namen und wähle MAIN, DOUBLE, ADLIB oder BEAT. GILLMIX kann Rollen vorschlagen; kontrolliere die Zuordnung. Du kannst in GILLMIX auf eine Spurzeile neben ihrem Häkchen klicken, um Rolle oder Sperre zu ändern.
3. Kreuze die gewünschten Spuren an und klicke CONNECT SELECTED. Nur diese Verbindung erlaubt Pegeländerungen. Nach Projektwechsel oder Wiederherstellung bewusst neu verbinden.
4. Wähle beispielsweise RAP FRONT. CHANGE LIMIT begrenzt die Änderung; DOUBLE, ADLIB und BEAT bestimmen die gewünschten Abstände zur Main. LOCK schützt einzelne Pegel.
5. Klicke LEARN und spiele einen repräsentativen Abschnitt mit allen beteiligten Stimmen und dem Beat ab. Stoppe die Analyse, kontrolliere den Vorschlag und klicke APPLY. Höre den Abschnitt anschließend erneut.
6. UNDO nimmt die übernommenen Änderungen zurück, soweit die betreffenden Pegel nicht inzwischen anderweitig verändert wurden. Danach kannst du TRACK LEVEL in GILLLINK selbst fein einstellen.

Die FL-Mixerfader werden dabei nicht fernbedient: GILLLINK verändert seinen eigenen Pegel. GILLMIX stellt einen statischen Ausgangsmix her; unterschiedliche Songabschnitte können weiter Automation und eigene Entscheidungen brauchen. Unbekannte Pegeländerungen hinter einem Link kann der Masterassistent nicht zuverlässig auslesen. Beide Plugins fügen keine Audiosamples Verzögerung hinzu. Verbindungen funktionieren innerhalb desselben Hostprozesses; getrennt gebrückte Plugins sind nicht gemeinsam erreichbar.

**GILLHARMONY: aus einer Stimme musikalische Begleitstimmen machen**

Stelle KEY und SCALE passend zum Song ein. THIRD ABOVE ergänzt eine Terz, OCTAVE SHADOW eine tiefe Oktave und WIDE HOOK zwei breite Stimmen. Jede der bis zu drei Stimmen hat eigene Lautstärke und Panorama. DIRECT schaltet die Originalstimme mit einem Klick aus. NATURAL steuert die Erhaltung ihrer Klanghülle. Für eine einzelne erkennbare Stimme oder ein einstimmiges Instrument gedacht; kein sicherer Harmoniegenerator für einen kompletten Mix.

Bei 48 kHz beträgt die zusätzliche Verarbeitungslatenz 26,67 ms in LIVE und 70,35 ms in PRO. Sie wird FL Studio korrekt gemeldet. Die Tonhöhe braucht echte Analysezeit; LIVE bedeutet hier deshalb keine absolute Null-Latenz.

**GILLREFERENCE: hören, ob der Mix wirklich besser wird**

Lade eine eigene Referenzdatei über LOAD und wähle den passenden Ausschnitt. Drei Slots halten verschiedene Referenzen bereit. Spiele deinen Mix ab und schalte mit MIX / REF um. MATCH lernt drei aktive Sekunden gewichteten RMS und senkt die lautere Seite ab. Zum Neulernen MATCH aus- und einschalten. VOICE, LOW, AIR sowie MONO und MID/SIDE helfen bei konkreten Fragen zu Stimme, Bass oder Breite.

Die Dateien bleiben an ihrem Speicherort; sie werden nicht vollständig ins Projekt eingebettet. Vor einem Echtzeit-Export MIX und BYPASS wählen. Ein vom Host als offline gekennzeichneter Export gibt automatisch den unveränderten Mix aus. Es entsteht keine zusätzliche Verarbeitungslatenz. Die Anzeige ist ein Vergleich mit gewichteten RMS-Pegeln, keine integrierte LUFS-Messung.

**GILLRESCUE: kurze übersteuerte Stellen vorsichtig reparieren**

Spiele die betroffene Aufnahme ab und klicke LEARN LEVEL. Wenn wiederholte harte Clipgrenzen erkennbar sind, werden die positiven und negativen Grenzen übernommen. REPAIR dosiert die Korrektur, MAX REPAIR begrenzt sie. OUTPUT schafft Headroom; LISTEN REPAIRS lässt dich nur den Eingriff hören. Beginne mit MILD ADC CLIP oder CAREFUL REPAIR.

Das Plugin schätzt kurze abgeschnittene Spitzen und lässt intakte Samples erhalten. Lange oder uneindeutige Verzerrungen werden nicht blind ersetzt; verlorene Originaldetails lassen sich nicht sicher zurückholen. LIVE benötigt 4 ms, PRO 12 ms zusätzlichen Kontext.

**Installation und LIVE/PRO**

Windows: EXE öffnen. Mac: DMG öffnen und das enthaltene PKG installieren. Danach in FL Studio unter **Options → Manage plugins → Find installed plugins** suchen; in **More plugins** nach GILL suchen. Die Standardfenster bleiben kompakt. Audiointerface- und DAW-Puffer kommen zur angegebenen Plugin-Latenz hinzu.

Die Mac-Ausgabe ist wie vereinbart zunächst ohne Apple Developer ID und Notarisierung. Native Plugin- und Installerprüfungen sind vom tatsächlichen Einsatz in FL Studio auf einem Mac zu unterscheiden. Die jeweiligen abschließenden Releasehinweise nennen den belegten Prüfstand.
