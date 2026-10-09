# GILL Mac20

Diese Ausgabe übernimmt die aktuellen Klangkorrekturen aus Update19 und die freigegebenen Oberflächen. 73 GILL-Plugins und das separat ausgelieferte MOBINA, je Universal VST3 für Apple Silicon/Intel. Das gemeinsame Paket und jedes einzelne Plugin haben eine DMG mit PKG-Installer.

Zusätzlich wird die PRO-Verarbeitung des De-Reverb korrigiert: Raumanteile werden pro Frequenz beurteilt, damit ein neuer Laut nicht die Reduktion des Nachhalls eines vorherigen Lauts verhindert. Die Erkennung trennt außerdem ausklingende Raumresonanzen von gehaltenen Tönen. Parameter, Presets und LIVE-Verarbeitung bleiben erhalten. Die zugehörigen Tests prüfen trockene Sprache, mehrere Räume und überlappende Laute bei 44,1, 48, 96 und 192 kHz.

Vor der Installation die DAW schließen; danach den FL Studio Plugin Manager neu suchen lassen. GILL-Plugins liegen unter `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`, MOBINA unter `/Library/Audio/Plug-Ins/VST3/MOBINA`.

Die Ausgabe ist ad-hoc signiert; die Installer und DMGs sind unsigniert und nicht Apple-notarisiert. Eine FL-Studio-Hörprüfung auf einem Mac ist noch erforderlich. Native CI-Prüfungen und Paketinhaltsprüfungen sind getrennt dokumentiert.

LIVE meldet null zusätzliche Host-PDC, nicht null gesamte physikalische Latenz. Puffer und Analysefenster können die tatsächliche Wet-Reaktion verzögern. PRO verwendet je nach Plugin zusätzliche Analyse-/Verarbeitungszeit.

Die bestehenden Plugin-Identitäten und Parameter werden erhalten. Die ausführlichen bisherigen Bedienungsanleitungen im Quellcode bleiben gültig. GILLMIX benötigt GILLLINK auf den gewünschten Quellen; es verändert LINK-Verstärkungen, nicht die Host-Mixerfader. Whole-song-Lernen endet mit STOP oder nach bis zu fünf Minuten, GILLALIGN bleibt auf einzelne Takes bis 20 Sekunden begrenzt. Offline erzeugte WAVs mit dem Projekt speichern.
