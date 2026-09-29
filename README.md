# GILLPRODUCTION Plugins

**Update 12 · 56 VST3-Plugins · Version 0.12.0**

- [Windows-Installer herunterladen](https://github.com/twennieofficial-debug/gillproduction-plugins/releases/download/bundle-test-12/GILLPRODUCTION-SETUP-WINDOWS-12.exe)
- [Mac-DMG herunterladen — Apple Silicon und Intel](https://github.com/twennieofficial-debug/gillproduction-plugins/releases/download/bundle-test-12/GILL-PLUGINS-12-MAC-UNSIGNIERT-TESTVERSION.dmg)
- [Alle Downloads, Quellcode und Prüfberichte](https://github.com/twennieofficial-debug/gillproduction-plugins/releases/tag/bundle-test-12)

GILLCEILING und GILLFINISH bieten zusätzlich BOOST bis +18 dB vor dem Limiter.
DRIVE und BOOST erhöhen die Lautheit; CEILING begrenzt die Ausgangsspitzen.
GAIN MATCH für den tatsächlichen Lautheitsgewinn ausschalten. Bestehende
Projektzustände behalten ihre bisherigen Werte und laden BOOST mit 0 dB.

Unter Windows den Installer ausführen. Auf dem Mac die DMG öffnen und das
enthaltene PKG installieren. Anschließend in FL Studio unter **Options → Manage
plugins → Find installed plugins** suchen und die Pluginliste nach GILL filtern.
Die Fenster verwenden auf beiden Systemen dieselben kompakten logischen Größen.

Alle 56 Plugins wurden unter Windows und nativ auf beiden Mac-Architekturen
geprüft, einschließlich strenger VST3-Validierung. Die Windows-Installation und
Erkennung in FL Studio wurden lokal bestätigt. Die Mac-DMG enthält die geprüften
Universal-Bundles; auch PKG-Installation und DMG-Integrität wurden geprüft.

Die Mac-Testversion ist ad-hoc signiert und **nicht Apple-notarisiert**; der
Windows-Installer hat kein Herausgeberzertifikat. Ein FL-Studio-Hörtest auf einem
Mac wurde nicht durchgeführt. Automatische Tests ersetzen das Gegenhören mit
dem eigenen Material. Details stehen in den beiliegenden Anleitungen.

Der Release-Tag bleibt an den tatsächlich geprüften Quellstand gebunden.
Build-Rezepte und Produktkatalog stehen unter `work/packaging/macos`; zum Bauen
das gepinnte JUCE-8.0.12-Submodul initialisieren. Quellen, Lizenzen und Hinweise
zu Drittanbieterkomponenten sind im Repository und im Quellcode-Archiv enthalten.
