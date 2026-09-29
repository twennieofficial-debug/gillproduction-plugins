# GILLPRODUCTION UPDATE 12

56 VST3-Plugins für Windows x64 und macOS (Apple Silicon und Intel).

## Mehr Lautheitsreserve

GILLCEILING: DRIVE 0–24 dB und zusätzlich BOOST 0–18 dB vor dem Limiter.
GILLFINISH: DRIVE 0–18 dB und zusätzlich BOOST 0–18 dB vor dem Limiter.
CEILING begrenzt die Ausgangsspitzen bis maximal 0 dB; sie ist kein LUFS-Ziel.
GAIN MATCH dient dem Vergleich bei ähnlicher Lautstärke. Für den tatsächlichen
Lautheitsgewinn ausschalten. BOOST zuerst bei 0 lassen und schrittweise erhöhen.
RAP IMPACT, TRAP MASTER, RAP PUNCH, DENSE TRAP und LOUD PREVIEW/DEMO sind
kräftigere Startpunkte. Sie garantieren keine feste Lautheit für jeden Mix.
Die GILLFINISH-LEARN-Empfehlung kann jetzt bis18 dB DRIVE vorschlagen.
Nach APPLY erneut den ganzen Song messen und nach Gehör anpassen; Limiting und
Kompression verändern den tatsächlich erreichten LUFS-Wert.

Bestehende Projektzustände laden BOOST=0. Vorhandene DRIVE- und CEILING-
Bereiche und Parameterkennungen bleiben erhalten. Neue Presets werden erst
beim ausdrücklichen Auswählen geladen.

PRO nutzt Lookahead und Oversampling; LIVE hat bei diesen beiden Limitern
keine zusätzliche Pufferlatenz und begrenzt Sample-Spitzen. PRO-True-Peak ist
eine endliche Rekonstruktion, keine Garantie für jedes spätere Codec-Format.
GILLCEILING auf MIX100%, OUTPUT0 dB und BYPASS aus verwenden, wenn die ganze
Spur begrenzt werden soll. Bei GILLFINISH muss LIMITER eingeschaltet sein.

## Alle aktuellen Effekte enthalten

Auch GILLBRAKE, GILLWIRE, GILLGHOST, GILLTRAIL, GILLMETAL, GILLSTUTTER und
GILLCROWD sind Teil dieser Ausgabe. GILLPHRASE enthält separate Hall- und
Delay-Throw-WAV-Ausgaben. Die Leertaste wird bei gewöhnlichen Reglern an den
Host weitergereicht; während der Texteingabe gehört sie zum Textfeld.

## Installation

Projekt speichern und FL Studio schließen. Auf Windows die EXE starten.
Auf Mac die DMG öffnen und das enthaltene PKG ausführen. Danach in FL Studio:
Options → Manage plugins → Find installed plugins; anschließend nach GILL suchen.
Die Fenster verwenden dieselben kompakten logischen Größen auf beiden Systemen.

Die Mac-Testversion ist nur ad-hoc signiert, nicht Apple-notarisiert.
Details und sichere Freigabe über macOS: MAC-INSTALLATION.txt.
Die automatisierten Mac-Prüfungen ersetzen keinen Hörtest in FL Studio auf Mac.

## Technische Orientierung und Grenzen

Aktuelle Charttitel wurden recherchiert, nicht als unkomprimierte Masterdateien
analysiert. Die neuen Presets sind eigene Ausgangspunkte, keine Kopien oder
gemessenen Lautheitsprofile bestimmter Künstler. Die Audioprüfung verwendet
eigene deterministische Signale mit Kick, Bass, Stimmharmonischen und Hi-Hats.

- Aktuelle Hip-Hop/R&B-Titel: https://www.officialcharts.com/charts/official-hip-hop-and-r-and-b-singles-chart/20260925/114/
- Gain/Output-Workflow: https://www.fabfilter.com/help/pro-l/using/recommendedworkflow
- True-Peak-Begrenzung: https://www.fabfilter.com/help/pro-l/using/truepeaklimiting

Die vollständige Plugin-Liste steht in GILL-PLUGINS-UEBERSICHT.txt.
