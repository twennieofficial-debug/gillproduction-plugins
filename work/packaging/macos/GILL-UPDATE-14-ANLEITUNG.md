# GILLPRODUCTION UPDATE 14

60 VST3-Plugins, Version 0.14.0. Die bisherigen 57 Plugin-Identitäten bleiben erhalten. Neu sind GILLVOCODE, GILLGRAIN und GILLPULSE. Alle verwenden das kompakte PRISM-BLUE-Design mit Metallflächen, elektrischen Lichtlinien und dem goldenen GP-Logo.

## Drei neue Effekte

**GILLVOCODE** überträgt die Sprachhüllkurve auf einen Synthesizerklang. Carrier wählen, NOTE zur Tonart setzen und anschließend FORMANT, RELEASE, BRIGHTNESS und CONSONANTS einstellen. EXTERNAL verwendet stattdessen einen über die DAW zugeführten Carrier; ohne diesen Eingang entsteht in diesem Modus kein Vocoder-Klang. MIX mischt die Originalstimme hinzu. Ideal für elektronische Hooks, Roboterstimmen und melodische Adlibs.

**GILLGRAIN** setzt kurze, überlappende Ausschnitte des eingehenden Audios zu einer Textur zusammen. SIZE bestimmt die Länge, DENSITY die Häufigkeit, PITCH die Transposition, SCATTER die Auswahl aus dem aufgenommenen Verlauf, FEEDBACK die Wiederholung und WIDTH die Stereoverteilung. Das trockene Signal bleibt zeitlich direkt; die verzögerten Grain-Kopien sind der gewünschte Effekt. Mit MIX niedrig anfangen und aus einer längeren gehaltenen Silbe eine schwebende Fläche formen.

**GILLPULSE** formt die Lautstärke des laufenden Signals mit einem editierbaren 16-Schritt-Muster. STEP RATE stellt das Taktraster, DEPTH die Stärke, SMOOTH die weichen Übergänge, SWING den Shuffle und PHASE die Musterposition ein. Die Schritte lassen sich direkt verändern. Der Effekt orientiert sich an Songtempo und Transportposition der DAW; ohne Tempoangabe dient MANUAL BPM als Ersatz (voreingestellt auf 120 BPM). Anders als GILLSTUTTER erzeugt er keine Wiederholung einer aufgenommenen Silbe.

Die Presetauswahl bietet bei jedem der drei Plugins sechs Ausgangspunkte. Regleränderungen und Schrittwerte werden mit dem Projekt gespeichert. MIX 0 gibt das Original aus; OUTPUT kontrolliert den Gesamtpegel. BYPASS umgeht den Effekt.

## LIVE und PRO

LIVE verwendet in allen 60 Plugins 0 Samples zusätzliche algorithmische Pufferlatenz. Die Audiohardware, Treiber und der FL-Studio-Puffer verursachen weiterhin ihre eigene Latenz. Gewollte Echos, Hallfahnen oder Grain-Verzögerungen sind Effektbestandteile und verschwinden deshalb nicht in LIVE.

GILLTUNE, GILLTUNE LIVE und GILLFORM geben in LIVE die Originalstimme aus; die eigentliche Tonhöhen-/Formantbearbeitung benötigt PRO. GILLHARMONY erzeugt Zusatzstimmen in PRO; in LIVE bleibt DIRECT, bei DIRECT OFF ist der Ausgang still. GILLRESCUE rekonstruiert Spitzen in PRO. GILLNOTE erlaubt das Abhören gerenderter Notenkorrekturen in PRO. Diese Einschränkungen ermöglichen das ungepufferte Originalmonitoring.

Die drei neuen kausalen Effekte können auch in LIVE arbeiten. Die zeitliche Struktur des Grain-Effekts bleibt erhalten. Der LIVE/PRO-Schalter wird lokal gespeichert oder über GILLCONTROL für Instanzen im selben Host-Prozess umgestellt.

## Mac-Installation

Für Mac ist eine Universal-Ausgabe mit Apple-Silicon- und Intel-Code vorgesehen. Nur eine erfolgreich gebaute und geprüfte DMG zählt als fertige Mac-Lieferung; dieser Text allein ist kein Build-Nachweis.

FL Studio schließen. DMG öffnen, das enthaltene PKG starten und den Apple-Installer ausführen. Ziel ist `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`. Anschließend in FL Studio **Options → Manage plugins → Find installed plugins** und nach GILL suchen.

Diese Ausgabe wird auf ausdrücklichen Wunsch zunächst ohne Apple-Developer-ID und ohne Notarisierung erstellt. Eine lokale Ad-hoc-Signatur ist kein Apple-Zertifikat. macOS kann die Installation deshalb blockieren. Die Sicherheitsprüfungen werden vom Installer nicht abgeschaltet. Eine spätere signierte Ausgabe benötigt eigene Developer-ID-Zertifikate und die Freigabe des Apple-Notary-Dienstes.

Die Build-Untergrenze macOS 11 ist keine Zusicherung eines FL-Studio-Tests auf jeder älteren macOS-Version. Die beigefügten Berichte nennen die tatsächlich verwendeten Testsysteme. Ein nativer Testhost ersetzt keinen Hörtest in FL Studio auf dem eigenen Mac.

## Windows

Bestehende Update-13-Dateien sind getrennt von Update 14 aufbewahrt. Nur als Windows bezeichnete VST3-Dateien gehören in die Windows-Installation. Mac-Bundles können nicht unter Windows verwendet werden und umgekehrt. Vor dem Ersetzen installierter Plugins Projekte speichern und FL Studio schließen; anschließend neu scannen.

## Projekte und Audio

Die Produktnamen und Kennungen der bisherigen Plugins bleiben gleich. Aufnahme- und Exportdateien von GILLNOTE, GILLPHRASE, GILLRISE und anderen Transfer-/WAV-Werkzeugen zusammen mit dem Projekt sichern und auf den zweiten Rechner mitnehmen. Plugin-Zustände können auf externe Audioaufnahmen verweisen.

GILLCEILING und GILLFINISH erlauben CEILING bis +6 dBFS. Das kann einen finalen Ausgang oder Integer-WAV-Export übersteuern; für einen begrenzten finalen Master eine passende negative Spitzengrenze wählen. DRIVE/BOOST erhöhen den Pegel vor der Begrenzung.

Die Datei **GILL-PLUGINS-UEBERSICHT.txt** erklärt alle 60 Produkte. Beiliegende Testberichte gelten für die jeweils angegebenen Binärdateien. Absolute Fehlerfreiheit oder identischer Klang zu kommerziellen Vorbildern wird nicht behauptet.

