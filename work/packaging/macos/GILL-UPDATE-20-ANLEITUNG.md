# GILL Mac20

Diese Ausgabe übernimmt die aktuellen Klangkorrekturen aus Update19 und die freigegebenen Oberflächen. 73 GILL-Plugins und das separat ausgelieferte MOBINA, je Universal VST3 für Apple Silicon/Intel. Das gemeinsame Paket und jedes einzelne Plugin haben eine DMG mit PKG-Installer.

Zusätzlich wird die PRO-Verarbeitung des De-Reverb korrigiert: Raumanteile werden pro Frequenz beurteilt, damit ein neuer Laut nicht die Reduktion des Nachhalls eines vorherigen Lauts verhindert. Die Erkennung trennt außerdem ausklingende Raumresonanzen von gehaltenen Tönen. Parameter und Presets bleiben erhalten. Die zugehörigen Tests prüfen trockene Sprache, mehrere Räume und überlappende Laute bei 44,1, 48, 96 und 192 kHz.

Die LIVE-Raumerkennung greift früher bei fallenden Hüllkurven, damit gewöhnliche Raumfahnen tatsächlich reduziert werden. Die Prüfungen unterscheiden den unmittelbaren Direktanteil von absichtlich erzeugten Pitch-, Formant- und Harmoniestimmen. GILLNOTE bekommt deterministische Prüfungen seiner Render-Abbrüche; deren Synchronisationshilfen werden ausschließlich in das Testprogramm eingebaut.

Bei GILLFLOW beendet FINISH die Analyse und übernimmt ein ausreichendes Lernprofil. Der zusätzliche CANCEL-Knopf verwirft die laufende Analyse und behält ein vorheriges Profil bei. Die ursprüngliche Fenstergröße bleibt erhalten.

Die korrigierte Ausgabe unter `mac-bundle-20.2` enthält zusätzlich die GILLSILK-Korrektur für wechselnde Resonanzen in PRO und LIVE. Die vorherige Erkennung konnte bei kurzen, wechselnden Vocal-Passagen vollständig ausbleiben. Die Analyse vergleicht lokale Spitzen mit der benachbarten spektralen Hüllkurve; LIVE verwendet eine feinere kausale Filterbank. Neue Audiotests prüfen die Reduktion, die Stärke-Regler, unveränderte Frequenzbereiche und bewegte Vocal-Harmonien. Ein zusätzlicher Test mit lizenzierter Sprache prüft vier Sampleraten, LIVE und PRO sowie vier künstlich hinzugefügte schmalbandige Resonanzen. GILLSPARK behält seine Verarbeitung und die Preset-Werte von REMOVE MOUTH CLICKS; Testsignale werden gegen die frühere Ausgabe verglichen. Der gemeinsame LIVE-Baustein wird in einem vollständigen neuen Mac-Build geprüft.

SILK begrenzt die PRO-Maske bei normalen DEPTH-Werten sanfter und erlaubt am oberen Ende stärkere Reduktion bis 7,25 dB. Zusätzliche Prüfungen kontrollieren die zuvor zu stark abgesenkten schmalen Stimmspitzen. SILK kann trotzdem eine natürliche schmale Stimmspitze mit einer störenden Resonanz verwechseln. Die automatisierten Prüfungen ersetzen keine Hörprüfung mit der eigenen Aufnahme. Hohe DEPTH- und SENSITIVITY-Werte verändern die Klangfarbe stärker; mit dem Frequenzbereich lässt sich die Reduktion gezielt eingrenzen.

Vor der Installation die DAW schließen; danach den FL Studio Plugin Manager neu suchen lassen. GILL-Plugins liegen unter `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`, MOBINA unter `/Library/Audio/Plug-Ins/VST3/MOBINA`.

Die Ausgabe ist ad-hoc signiert; die Installer und DMGs sind unsigniert und nicht Apple-notarisiert. Eine FL-Studio-Hörprüfung auf einem Mac ist noch erforderlich. Native CI-Prüfungen und Paketinhaltsprüfungen sind getrennt dokumentiert.

LIVE meldet null zusätzliche Host-PDC, nicht null gesamte physikalische Latenz. Puffer und Analysefenster können die tatsächliche Wet-Reaktion verzögern. PRO verwendet je nach Plugin zusätzliche Analyse-/Verarbeitungszeit.

Die bestehenden Plugin-Identitäten und Parameter werden erhalten. Die ausführlichen bisherigen Bedienungsanleitungen im Quellcode bleiben gültig. GILLMIX benötigt GILLLINK auf den gewünschten Quellen; es verändert LINK-Verstärkungen, nicht die Host-Mixerfader. Whole-song-Lernen endet mit STOP oder nach bis zu fünf Minuten, GILLALIGN bleibt auf einzelne Takes bis 20 Sekunden begrenzt. Offline erzeugte WAVs mit dem Projekt speichern.
