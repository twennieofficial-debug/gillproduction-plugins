# GILLPRODUCTION – UPDATE 10

49 VST3-Plugins, gemeinsam auf Version 0.10.0: die vorhandenen 47 sowie GILLRISE und GILLASSIST. Die Kennungen bestehender Plugins bleiben erhalten, damit Projekte ihre Instanzen wiederfinden.

## Installation

Projekt speichern und FL Studio schließen. Windows: Setup starten. Mac: DMG öffnen und das enthaltene Installationspaket starten. Danach in FL Studio **Options → Manage plugins → Find installed plugins** ausführen und unter **More plugins** nach **GILL** suchen. Alle sind Mixer-Effekte.

Windows installiert VST3 x64 nach `C:\Program Files\Common Files\VST3\GILLPRODUCTION`. Mac installiert Universal VST3 für Apple Silicon und Intel ab macOS 11 nach `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`. Die Stichpunktübersicht beschreibt alle 49 Plugins.

## Einen ganzen Song lernen

FLOW, BALANCE, SMARTDEESSER, FINISH, RESCUE, MIX und die kreativen Song-Effekte können jetzt bis zu fünf Minuten berücksichtigen. LEARN bei angehaltenem Transport bereitet die Aufnahme vor; PLAY startet sie. Den gewünschten Songbereich einmal ohne Sprünge abspielen. STOP beziehungsweise FINISH beendet die Analyse; bei 300 Sekunden endet sie automatisch. Eine Pause im Audiomaterial beendet den Durchlauf nicht. Ein Sprung im Transport beendet den bisherigen zusammenhängenden Durchlauf.

Einige Plugins wenden ihre ermittelten Werte direkt an, andere bieten weiterhin APPLY und UNDO/REVERT an. Stille oder zu wenig geeignetes Material ergeben keinen verlässlichen Vorschlag. Ein gelernter Vorschlag bleibt veränderbar und ersetzt den Vergleich nach Gehör nicht. GILLMIX benötigt weiterhin GILLLINK auf den bewusst verbundenen Spuren; es übernimmt keine direkte Steuerung der FL-Mixerfader.

## GILLPHRASE, GILLDIRECTOR und GILLREPLY

Den Songbereich aufnehmen, anschließend Marker und Effektstärke bearbeiten. Bei PHRASE lassen sich Hallverlauf und Hallparameter anpassen; DIRECTOR steuert sein eigenes Effekt-Rack. REPLY verwendet Ausschnitte der aufgenommenen Stimme und erzeugt keine neuen Wörter. Diese Analyse erkennt Signalverläufe, keine Texte oder musikalischen Bedeutungen.

Die Exportfunktion rendert eine echte WAV-Datei. Je nach Auswahl enthält sie nur den Effekt oder die vollständige Bearbeitung, einschließlich ausklingender Effekte. Erst nach Abschluss der Berechnung die Exportfläche in die Playlist ziehen. Nach einer Änderung erneut rendern. Für einen reinen Hall-/Echo-Layer WET exportieren und die ursprüngliche Vocal separat behalten; bei einem vollständigen Export nicht versehentlich beide Originalstimmen gleichzeitig abspielen.

## GILLRISE – Vocal-Riser aus der ersten Silbe

ARM aktivieren und die Vocal abspielen. GILLRISE nimmt die erste erkannte Silbe auf und erzeugt daraus Reverse Reverb. Bei ungünstigem Rauschen oder Atem vor dem Einsatz Trim und Erkennung kontrollieren. NORMAL erzeugt einen durchgehenden Aufbau; TREMOLO unterteilt ihn rhythmisch. Länge, Hall, Klang und Pegel einstellen, Berechnung abwarten und die WAV-Datei in die Playlist ziehen.

Den Riser so platzieren, dass sein Ende am gewünschten Vocal-Einsatz liegt. Bei bekannter Hostposition und nichtnegativer Startzeit enthält die Datei Zeitpositions-Metadaten; ob der Host diese automatisch nutzt, hängt vom Import ab. GILLRISE reicht das laufende Eingangssignal unverändert durch. Der vor einem noch unbekannten Einsatz liegende Riser ist ein vorab gerenderter Clip, kein vorhersagender Echtzeiteffekt.

## GILLASSIST – editierbare Vocal-Vorbereitung

TRANSFER starten und den Songbereich bis zu fünf Minuten abspielen, oder eine Audiodatei importieren. Nach der Analyse RIDE, GATE, BREATH und SIBILANCE einzeln dosieren oder ausschalten. Die Zeitleiste zeigt die Pegelbearbeitung; Bereiche lassen sich korrigieren und schützen. Mit Undo/Redo und A/B vergleichen, anschließend WAV exportieren und in die Playlist ziehen.

Der Assistent nutzt Pegel- und spektrale Merkmale mit vorsichtigen Regeln. Er ist kein trainiertes Sprachmodell und kann Atem und Zischlaute nicht immer sicher von gewollten Details unterscheiden. Die Korrekturen sind breitbandige Pegeländerungen; für gezieltes frequenzabhängiges De-Essing zusätzlich GILLSMARTDEESSER verwenden. Bei verändertem oder verschobenem Take neu aufnehmen beziehungsweise importieren. Der gelernte Verlauf gehört zum erfassten Audiomaterial.

## Dateien und Projekte

Gerenderte und aufgenommene Audiodateien aufbewahren. Bereits aus einem Plugin in die DAW gezogene Dateien werden nicht durch spätere Bearbeitungen überschrieben. Beim Weitergeben oder Verschieben eines Projekts die verwendeten Audiodateien mitnehmen; auf einem anderen Rechner sind lokale Dateipfade allein nicht ausreichend. GILLREFERENCE benötigt ebenfalls seine Referenzdateien. GILLALIGN behält seinen für einzelne Takes ausgelegten Workflow und die Grenze von 20 Sekunden.

## LIVE / PRO und Oberflächen

LIVE verwendet bei geeigneten Effekten einen Pfad ohne zusätzliche Audiosamples Verzögerung. PRO kann Lookahead, Oversampling oder längere Analysefenster verwenden. GILLCONTROL schaltet erreichbare Instanzen im selben DAW-Prozess gemeinsam um. Getrennte Bridge-/Sandbox-Prozesse werden nicht gemeinsam erreicht. Interface-Puffer und andere Effekte bleiben Teil der gesamten Abhörverzögerung.

LIVE/PRO möglichst bei angehaltenem Transport auswählen. Bei einer Änderung der Latenz muss die DAW ihre Verzögerungskompensation neu einstellen; ein Wechsel während der Wiedergabe ist keine samplegenaue, phasenstabile Automation.

Tonhöhenbearbeitung und die Reparatur abgeschnittener Spitzen brauchen weiterhin zukünftiges Audiomaterial: TUNE, FORM, HARMONY und RESCUE haben deshalb auch in LIVE eine tatsächlich gemeldete Restlatenz. Eine Anzeige von null würde diese Verzögerung nicht entfernen. GILLRISE und GILLASSIST fügen bei der laufenden Wiedergabe keine Audiosamples Verzögerung hinzu; Aufnahme, Analyse und Export benötigen trotzdem Rechenzeit.

Die rechteckigen Oberflächen verwenden dieselben logischen Größen auf Windows und Mac. Weiße Regler und Flächen haben überarbeitete Reflexionen, Wölbung und Schatten. Retina-/Windows-Skalierung verändert die physische Pixelzahl. Die fünf neuen GILLTUNE-Bilder sind Wahlkonzepte; das bestehende Command-Wheel-Layout bleibt bis zur Auswahl erhalten.

## Testversion

Windows ist ohne Herausgeberzertifikat, Mac wie vereinbart ohne Apple Developer ID und Apple-Notarisierung. Freigabeberichte unterscheiden automatisierte native Tests, echte Plugin-Dateien und FL-Erkennung. Ein Hörtest in FL Studio auf einem Mac wird damit nicht behauptet. Automatisierte Prüfungen können nicht jede Aufnahme oder jeden Rechner abdecken.

Quellcode und Lizenzhinweise gehören zum Paket. Die Plugins sind eigene Implementierungen; Funktionsreferenzen bedeuten keinen identischen Klang und keinen übernommenen proprietären Code.
