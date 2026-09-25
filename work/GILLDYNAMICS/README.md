# GILL DYNAMICS & STAGE 0.2.0

Aktueller Quellstand der Release-Runde 04: **0.2.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Fünf eigenständige Windows-x64-VST3-Effekte für Mono/Stereo, 32-Bit-Float und 8–192 kHz. Die ausgewählten Designs sind native, skalierbare Oberflächen: GILLVOX ORBIT, GILLOPTA PENDULUM, GILLBUSS SWEEP CONSOLE, GILLQUAD PANORAMA und GILLSTAGE ORBIT ROOM. Kleine GP-Marke, verschiedene Holzformen und elfenbeinfarbene Regler verbinden das Bundle. Jeder Effekt enthält zwölf eigene Presets.

## GILLVOX

GATE blendet leise Abschnitte ab; ganz links OFF. COMP verbindet Kompressionsstärke und feste Pegelkompensation; OUTPUT regelt den Endpegel. Die zentrale GR-Anzeige zeigt Kompression und Limiter, nicht die Gate-Absenkung. Eine gekoppelte Sample-Peak-Begrenzung schützt bis −1 dBFS; das ist kein True-Peak-Limiter. Der Sprachbeginn erhält einen kleinen Lookahead. Bei 48 kHz: 36 Samples / 0,75 ms. COMP 0 und GATE OFF sind sauber, solange der Limiter nicht durch Überpegel eingreift.

## GILLOPTA

PEAK REDUCTION regelt die Kompressionsstärke, GAIN den manuellen Pegelausgleich. COMP ist weicher, LIMIT stärker und schneller; LIMIT ist kein Brickwall-Limiter. Die zweistufige Erholung hängt von der vorherigen Kompressionsdauer ab. HF erhöht die Empfindlichkeit für Höhen. NOISE ist standardmäßig ausgeschaltet. IN/OUT zeigen 300-ms-RMS, 0 VU entspricht −18 dBFS RMS; GR zeigt die tatsächliche Absenkung. Keine zusätzliche PDC-Latenz.

## GILLBUSS

CLEAN, IRON und VELVET wählen drei eigene Sättigungsformen. DRIVE steuert eine kalibrierte Färbung; es ist keine Zusage entsprechend höherer Ausgangslautheit. TRIM ist ein dB-Pegelregler. Achtfaches Oversampling reduziert Aliasanteile; feste Latenz 24 Samples / 0,5 ms bei 48 kHz. Float-Headroom bleibt erhalten; Ausgangspegel und nachfolgende Geräte beachten. NOISE ist optional und zunächst aus.

Die acht Gruppen steuern weitere GILLBUSS-Instanzen, die dieselbe Gruppe wählen und im selben Hostprozess laufen. Die acht Streifen sind keine acht separaten Audioeingänge. Gruppendrive und -trim addieren sich zu den lokalen Werten; Gruppendrive wird insgesamt auf 0–24 begrenzt. Gruppen-BYPASS umgeht auch Trim und Noise. NONE löst die Instanz aus der Gruppensteuerung. Die globale Gruppenbank wird beim Speichern mitgesichert; ein später geladener gespeicherter Zustand stellt seine enthaltenen Gruppenwerte wieder her. Nach Entfernen der letzten Instanz wird die Bank zurückgesetzt. Getrennte Host-/Bridge-Prozesse teilen diese Bank nicht. Wer zwei Projekte gleichzeitig im selben Prozess nutzt, verwendet getrennte Gruppennummern.

## GILLQUAD

Vier Bänder mit THR, RANGE, GAIN, ATT, REL, SOLO und BYP. Negative RANGE begrenzt die Absenkung oberhalb THR, positive RANGE ist Expansion oberhalb THR. GAIN ist der statische Bandpegel. Die drei Trennfrequenzen lassen sich direkt in der Kurve verschieben oder numerisch einstellen. Ihre Reihenfolge und Mindestabstände werden in der Engine gesichert. Die Linie zeigt die aktuelle tatsächlich kombinierte Filterantwort mit dynamischen Bandgewinnen; sie ist kein Eingangsspektrum.

Die komplementären, sanften 6-dB/Okt.-Differenzbänder rekonstruieren in Neutralstellung das Original samplegenau. Die Übergänge überlappen bewusst breit, es sind keine steilen C4-/Linkwitz-Riley-Filter. Keine zusätzliche PDC-Latenz. SOLO blendet andere Bänder aus, BYP umgeht Dynamik und Trim des jeweiligen Bands. Die GR-Anzeigen unterscheiden Absenkung und Expansion.

## GILLSTAGE

Den hellen Quellpunkt auf der runden Karte ziehen: links/rechts positioniert die ganze Stereoquelle. Zu den Seiten wird das Stereobild enger; beide Eingangskanäle kommen in die gewählte Richtung mit. FAR/NEAR verändert die empfundene Entfernung durch Pegel, Höhen und kurze frühe Reflexionen. Die Zahlen auf der Karte sind in Prozent editierbar. DOUBLER fügt zwei unterschiedlich modulierte verzögerte Anteile hinzu. SPREAD regelt die Stereobreite vor der positionsabhängigen Einengung, MIX den Effektanteil, OUTPUT den Endpegel. Gegenphasige Anteile können bei der Mono-Summierung an den Seiten auslöschen; MONO CHECK hilft beim Gegenhören.

Mitte, Entfernung 0, SPREAD 100, DOUBLER 0, MIX 100, OUTPUT 0 und MONO CHECK aus sind exakt neutral. MIX 0 erhält das Original bei OUTPUT 0 und ausgeschaltetem MONO CHECK; beide Funktionen bleiben absichtlich auch auf dem Dry-Anteil wirksam. Das ist eine Stereo-Raumillusion, kein HRTF-, Surround- oder verlässliches Rückwärtsortungssystem. Direkter Signalweg: 0 zusätzliche PDC-Samples. Die kurzen Effektdelays sind beabsichtigt und bleiben hörbare Bestandteile des Doublers/der Entfernung.

Die isolierte STAGE-Prüfung umfasst **119 Prüfpunkte, alle bestanden**, einschließlich rechts-only nach links, links-only nach rechts, asymmetrischem Stereo, unverändertem Zentrum, geglätteter Automation und samplegleichen Ergebnissen bei verschiedenen Puffergrößen. Details: [Tests/STAGE-DSP-VALIDATION.md](Tests/STAGE-DSP-VALIDATION.md).

DIRECT ON erhält den direkten Anteil. DIRECT OFF entfernt den unmittelbaren Originalpfad auch aus dem Dry-Anteil; Reflexionen und Doubler bleiben je nach Einstellung hörbar. Bei MIX 0 und DIRECT OFF entsteht deshalb Stille. Projektzustände vor Version 2 enthalten keinen DIRECT-Wert und werden für ihren bisherigen Klang mit DIRECT ON geladen. Neue Zustände speichern die gewählte Einstellung. Die Neutralbeschreibungen oben setzen DIRECT ON voraus.

Die aktuelle isolierte STAGE-Prüfung wurde auf 148 Checks erweitert; der native Realtime-Test ergänzt die tatsächliche DIRECT-Bindung und die Migration alter Zustände. Die frühere Zahl von 119 Prüfpunkten beschreibt den historischen Bericht.

## Betrieb und Tests

Alle Parameter sind automatisierbar. Presets sind Ausgangspunkte, keine universellen Einstellungen für jede Stimme. Vor dem Vergleich Pegel angleichen. Die eigene Pluginlatenz kommt zur Interface-/Hostpufferzeit hinzu. Bypass ist zeitlich angeglichen; seine Umschaltung wird kurz überblendet. Höhere Sampleraten können mehr CPU beanspruchen.

Die isolierten DSP-Prüfungen und nativen Integrations-/VST3-Hosttests liegen unter Tests. Die Tests prüfen gezielte Eigenschaften an definierten Signalen, keine allgemeine Fehlerfreiheit oder identischen Klang zu kommerziellen Referenzen. Die Referenzprodukte RVox, CLA-2A, NLS und C4 dienten der Funktionsorientierung; proprietärer Code wurde nicht übernommen. Quellcode/Lizenzen: LICENSE-NOTICE.md und THIRD-PARTY.md. Build: BUILDING.md.
