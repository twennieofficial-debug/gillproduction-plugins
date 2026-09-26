# GILLPRODUCTION – UPDATE 09

47 VST3-Plugins: die vorhandenen 39 plus acht neue Master-Werkzeuge.

## Installation in FL Studio

Projekt speichern und FL Studio vor der Installation schließen. Unter Windows das Setup starten; unter Mac die DMG öffnen und das enthaltene Installationspaket starten. Danach in FL Studio **Options → Manage plugins → Find installed plugins** ausführen. Anschließend im Mixer unter **More plugins** nach **GILL** suchen.

Die neuen Namen: **GILLCEILING, GILLLOW, GILLGLUE, GILLWIDTH, GILLPUNCH, GILLWEIGHT, GILLDELTA und GILLDELIVER**. Es sind Effekte für den Mixer, keine Instrumente für den Channel Rack. Die Stichpunktübersicht erklärt alle 47 Plugins. Unter **Ansichten** sind echte Programmaufnahmen zu sehen.

Windows: VST3 x64 unter `C:\Program Files\Common Files\VST3\GILLPRODUCTION`. Mac: Universal VST3 für Apple Silicon und Intel ab macOS 11 unter `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`.

Die vorhandenen Windows-Module und ihre Kennungen bleiben unverändert. Vorhandene Projekte behalten ihre Zuordnung. Das Setup ersetzt keine gerade verwendeten Dateien zwangsweise. Bei einem Installationsfehler die Meldung prüfen, FL Studio schließen und erneut starten.

## Sinnvolle Startpunkte

- GILLLOW für unruhigen Bass, GILLGLUE für sanfte gemeinsame Kompression und GILLPUNCH für gezielte Anschläge.
- GILLWEIGHT für Bass-Obertöne, GILLWIDTH für dosierte Stereobreite. Lautstärke beim Vergleichen möglichst angleichen.
- GILLCEILING am Ende der Klangbearbeitung, beispielsweise mit CLEAN MASTER, MIX 100 % und -1 dB Ceiling beginnen. Das ist ein Startpunkt, kein für jeden Song verpflichtender Zielwert.
- GILLDELIVER dahinter: ANALYZE vor dem vollständigen Durchlauf, danach STOP und REPORT. Der Bericht verändert den Song nicht.
- Für GILLDELTA eine SOURCE-Instanz vor und eine RETURN-Instanz nach der Kette einsetzen, beide auf dieselbe PAIR-Nummer stellen. Abspielen und auf RETURN LEARN drücken. AFTER bleibt der normale bearbeitete Mix. Nach Änderungen in der Kette neu lernen.

Alle acht haben sechs Startpresets. Sie ersetzen keine Entscheidung nach Gehör. Audition-Funktionen sind zum Vergleichen gedacht: vor einem Echtzeit-Export ausschalten. Bei erkanntem Offline-Export werden die Abhörsonderwege umgangen.

## LIVE und PRO

Bei diesen acht Plugins fügt LIVE keine zusätzliche Audioverzögerung hinzu. Interface-Puffer und andere Effekte bleiben wirksam. PRO verwendet bei GILLCEILING Oversampling und Lookahead (176 Samples bei 48 kHz), bei GILLWEIGHT vierfaches Oversampling (32 Samples). Die anderen sechs benötigen keine zusätzlichen Audiosamples. Messwerte erscheinen erst nach ihrer jeweiligen Analysezeit.

GILLCONTROL schaltet erreichbare Instanzen im selben DAW-Prozess um. Getrennt gebridgte oder sandboxed Prozesse teilen die Verbindung nicht.

Die Oberflächen bleiben kompakt und rechteckig, mit unterschiedlichen Hoch- und Querformaten. Mac und Windows verwenden dieselben logischen Fenstergrößen; Retina- oder Windows-Skalierung erhöht die physische Pixelzahl.

## Testversion und Grenzen

Die Mac-Ausgabe ist wie vereinbart ohne Apple Developer ID und ohne Apple-Notarisierung. Die Windows-EXE hat kein Herausgeberzertifikat. Die Freigabeberichte unterscheiden automatisierte native Tests und echte FL-Erkennung. Ein FL-Studio-Hörtest auf einem Mac wurde nicht durchgeführt.

Lautheits- und True-Peak-Messung sind eigenständige Implementierungen mit endlicher Genauigkeit, keine EBU-Zertifizierung. GILLDELTA kann bei ungeeignetem oder stark verändertem Material keine verlässliche Ausrichtung lernen. In diesem Fall bleibt AFTER aktiv.

Vollständiger entsprechender Quellcode und Lizenzhinweise sind enthalten. Die Plugins verwenden keinen übernommenen proprietären Code der genannten Vergleichsprodukte.
