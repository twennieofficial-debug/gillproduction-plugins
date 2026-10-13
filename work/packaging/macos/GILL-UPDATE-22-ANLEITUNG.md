# GILL Mac22 – Universal VST3

74 Plugins für Apple Silicon und Intel, macOS 11 oder neuer. Die GILL-Bundle-DMG enthält 73 GILL-Plugins; MOBINA wird separat installiert. Jedes Plugin hat außerdem eine eigene DMG. Die vorhandenen Plugin-Identitäten, Parameter und gespeicherten Produktversionen bleiben erhalten; „22“ bezeichnet diese Auslieferung.

Vor der Installation die DAW schließen. DMG öffnen und die enthaltene PKG mit dem macOS Installer ausführen. Danach im FL Studio Plugin Manager erneut nach Plugins suchen. GILL liegt unter `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`, MOBINA unter `/Library/Audio/Plug-Ins/VST3/MOBINA`. Zum Entfernen nur das jeweilige VST3-Bundle löschen; Projekte und Aufnahmen behalten.

Dies ist eine **unsignierte Testversion**: VST3-Bundles sind ad-hoc signiert; PKG und DMG haben keine Apple Developer ID und keine Notarisierung. Gatekeeper kann die Installation blockieren. Keine Sicherheitsprüfungen abschalten. Eine eigene Hör- und Routingprüfung in FL Studio auf einem Mac bleibt erforderlich. Automatisierte native Tests sind keine physische FL-Studio-Prüfung.

Diese Ausgabe übernimmt die aktuellen Windows22-Produktquellen und Oberflächen. Suite-Dynamik erhält bei Mono-Hauptsignal beide Kanäle eines Stereo-Keys für die Detektion. Phasengedrehte Key-Kanäle löschen den Detektor nicht aus; hörbares LISTEN bleibt vorzeichenbehaftetes Audio und kann in Mono entsprechend auslöschen. Explizite M/S-Wahl behält ihre Bedeutung. Im Rack bleiben nicht unterstützte, gespeicherte Sidechain-Regler erhalten, beeinflussen aber keine unpassenden Module. Die LIVE-Rauschreduktion von SPECTRA verwendet das gelernte Profil im passenden Detektorbereich.

Externes Sidechain-Audio muss in der DAW an den zusätzlichen Key-Eingang des Plugins geroutet werden. Ein externer Key ist kein zweiter hörbarer Hauptkanal. EXTERNAL nur für unterstützte Detektoren einschalten; LISTEN dient der Kontrolle des gefilterten Key-Signals. Bei nicht unterstützten Modulen ergibt die zusätzliche Buchse keine externe Steuerfunktion. DAW-Routing, Wiedergabe, Mono/Stereo und gespeicherten Projektzustand mit der eigenen Sitzung prüfen.

LIVE meldet null zusätzliche Host-PDC; Audiointerface, Hostpuffer und Analysefenster können weiterhin Zeit benötigen. PRO kann zusätzliche Verarbeitungslatenz melden. GILLMIX benötigt GILLLINK auf den gewünschten Quellen und verändert LINK-Verstärkungen, keine Host-Mixerfader. Whole-song-Lernen mit STOP beenden; GILLALIGN bleibt auf einzelne Takes bis 20 Sekunden begrenzt. Offline erzeugte WAV-Dateien mit dem Projekt sichern.

Downloads und entsprechender vollständiger Quellcode: https://github.com/twennieofficial-debug/gillproduction-plugins/releases/tag/mac-bundle-22

`SHA256SUMS.txt` bindet die veröffentlichten Dateien. Die Source-ZIP enthält die zugehörigen Quellen, JUCE und Regressionstests. Die Bundle-DMG enthält dieselbe Source-ZIP. Native Buildberichte, beidseitige Universal-Prüfung und tatsächliche Installer-Ausführung werden separat dokumentiert; lokale Rezepttests allein beweisen keine funktionierenden Mac-Binaries.
