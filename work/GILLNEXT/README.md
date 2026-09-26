# GILLNEXT 0.1.0

Aktueller Quellstand der Release-Runde 04: **0.1.0**. Historische Versionsangaben in Testberichten beziehen sich auf deren jeweiligen Messlauf.

Sechs eigenständige Windows-x64-VST3-Effekte: GILLRIDE, GILLCLEAN, GILLPOCKET, GILLALIGN, GILLFORM und GILLFINISH. Die gewählten Oberflächen stammen aus den Konzepten BRIDGE, SHIELD, BRIDGE CUT, PARALLEL und VOICE PAD. GILLFINISH bearbeitet den kompletten Master aus Beat und Vocals.

GILLPOCKET: Auf dem Beat einsetzen, Vocal als externen Sidechain routen. Ohne aktiven Sidechain bleibt die Bearbeitung neutral.

GILLALIGN: Plugin auf der Double-Spur, Guide-Vocal als Sidechain. Dieselbe Phrase vom selben Transportstart aus getrennt als GUIDE und DOUBLE erfassen; maximal20 Sekunden. ALIGN analysiert im Hintergrund, PREVIEW folgt der Position der erfassten Double-Phrase. Im gestoppten Host wird die Vorschau ausgeblendet. Fertige Ergebnisse werden als32-Bit-Float-WAV unter Documents/Jill Plugins/Align Takes gespeichert; Projektzustände verweisen auf diese Dateien. Beim Übertragen eines FL-Projekts auch diese Audiodateien erhalten. Keine automatische DAW-Clipverschiebung und keine Quelltrennung.

GILLFORM: Unabhängige Tonhöhen- und Formantentransformation. LINK lässt die Formanten der Tonhöhe folgen. Die feste Verarbeitungslatenz ist bei48kHz6208 Samples (129,33ms); der Host erhält diese Meldung zur Kompensation. Eine kürzere getestete Variante wurde wegen schlechterer Klangmessungen verworfen.

GILLFINISH: Tone-EQ, Stereo/Bass-Mono, Bus-Kompression, Drive, optionaler Softclipper und8x-überwachter Limiter. LEARN analysiert12 Sekunden aktiver Musik und schlägt moderate, editierbare Änderungen vor; keine nachgebildeten Künstler-Master oder garantierte Ziellautheit. APPLY/REVERT und pegelangepasster Vergleich. LUFS nach K-Gewichtung/gatierten Blöcken; True Peak ist eine endliche8x-Rekonstruktionsschätzung, keine Zertifizierung.

GILLCLEAN: Spektrale Heuristiken für Rauschen, Plosive und Atemanteile. Nicht jede Störung lässt sich ohne Veränderung der Stimme entfernen. LISTEN macht die entfernten Anteile separat hörbar.

Tests und gemessene Grenzen stehen in Tests/*.md. Referenzprodukte wurden funktional betrachtet; kein fremder proprietärer Plugin-Code wurde übernommen.

## Update 10: complete-song FINISH analysis

GILLFINISH LEARN now arms until playback, follows the whole song for up to 300
seconds, and finishes on transport stop, a seek/loop, or a manual FINISH click.
The clock includes musical pauses. It no longer commits after twelve seconds.
With no host transport information, use LEARN then FINISH explicitly. Analysis
uses bounded loudness/tone statistics, not a stored copy of the complete song.
Empty or insufficiently audible passes are rejected. APPLY and REVERT remain
explicit, and all existing parameter IDs and saved processing settings remain
compatible. Learned suggestions are conservative mastering starting points,
not a promise of a perfect master. The new FINISH_SONG suite covers a real
five-minute pass, arming, stop/seek, manual finish, silence and apply/revert.
