# GILLDEREVERB 0.3.0

Aktueller Quellstand der Release-Runde 04: **0.3.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Kompakte Hallreduktion für Windows x64 / VST3. Helles Eichenholz, weißer Regler,
Salbeigrün und kleines GP-Logo oben links – ohne große eingeprägte Hintergrundgrafik.

## Bedienung

**HALL REDUZIEREN** ist der einzige Regler. Nach rechts wird die Bearbeitung
stärker; die Abstimmung erfolgt automatisch. Zum Start sind etwa 40–65 % sinnvoll.
Hohe Werte können die Stimme hörbar verändern. 0 % lässt das Signal unverändert,
abgesehen von der vom Host kompensierten Verzögerung. Der Startwert ist 55 %.

Ziehen zum Einstellen, Prozentwert anklicken zum Eingeben,
Pfeiltasten für Schritte, Doppelklick zum Zurücksetzen auf 55 %.
Das Fenster lässt sich quadratisch zwischen 340 und 840 Pixeln skalieren.
Bypass wird im FL-Studio-Wrapper bedient.

## Update in FL Studio

Der bestehende Eintrag heißt weiter **GILLDEREVERB**. Das Update ersetzt die
Datei im gleichen Ordner und verwendet dieselbe Plugin-Kennung. Ein zusätzlicher
Eintrag mit „V2“ oder neuem Namen wird nicht angelegt.

Installationsort:
`C:\Program Files\Common Files\VST3\GILLPRODUCTION\GILLDEREVERB.vst3`

Falls eine alte Instanz noch geöffnet ist, FL Studio nach dem Speichern des
Projekts neu starten. Unter „More Plugins“ nach GILLDEREVERB suchen.
Für eine Installation auf einem anderen Windows-PC das vollständige Paket aus
GILLDEREVERB-WINDOWS-VST3.zip verwenden. Die einzelne GILLDEREVERB.vst3-Datei
ist zusätzlich enthalten; nicht beide Varianten parallel in Suchordner kopieren.

**Bestehende Projekte:** Alte Zustände behalten zunächst ihren bisherigen Klang
einschließlich gespeicherter Zusatzparameter. Erst eine eigene Bedienung des
neuen Reglers aktiviert die neue Automatik. Vorhandene Automation allein tut das
nicht. Ein erneut gespeicherter alter Zustand bleibt ebenfalls kompatibel.

## Audio

Mono/Stereo, 32-/64-Bit-Verarbeitung. Verzögerung: 2048 Samples, entsprechend
42,67 ms bei 48 kHz beziehungsweise 46,44 ms bei 44,1 kHz, zusätzlich zur
Audiogerätelatenz. Deshalb besonders für bereits aufgenommene Stimmen geeignet.
Das Focusrite bleibt das Audiogerät von FL Studio; das Plugin benötigt keinen
eigenen Treiber und arbeitet ohne Cloud.

Raumhall lässt sich vermindern, aber nicht für jede Aufnahme vollständig und
ohne Stimmveränderung entfernen. Gemessene Ergebnisse und Grenzen stehen im
GILLDEREVERB-TESTBERICHT.md. Die Verarbeitung stammt aus eigener Implementierung.

## Enthalten

VST3-Datei und vollständiges Installationspaket, Quellcode einschließlich JUCE,
Prüfprotokolle und aktuelle Oberflächenbilder. Eine weitere Kopie liegt unter
Dokumente\Jill Plugins. Lizenz: AGPL-3.0-only; Details und Fremdlizenzen im
Quellpaket. Die Gesprächsentscheidungen sind im Gesprächsarchiv festgehalten.
