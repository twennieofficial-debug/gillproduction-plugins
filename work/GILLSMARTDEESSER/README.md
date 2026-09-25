# GILLSMARTDEESSER

Automatischer Vocal-De-Esser im GILL-Holzdesign. Windows/macOS VST3, Mono/Stereo, 32-/64-Bit-Audioverarbeitung, 480 × 360 logische Pixel (bis 2× skalierbar).

1. Eine einzelne Vocal-Spur abspielen und LEARN drücken. Die Analyse sammelt acht Sekunden aktives Audio; Stille zählt nicht. STOP wertet bereits aufgenommenes Material aus.
2. APPLY übernimmt den gemessenen Sibilanzbereich, den Pegel-Threshold und die dosierte Reduktionsstärke. UNDO stellt das vorherige Profil wieder her.
3. AMOUNT bestimmt die Stärke. LISTEN REMOVED lässt exakt das entfernte Signal hören. Danach wieder ausschalten.

Mindestens vier Sekunden aktives Signal, 2,5 Sekunden stimmhafter Kontext und 180 ms passende Sibilanz sind nötig. Ohne ausreichendes Material entsteht kein anwendbares Profil. Die maximal 30 Sekunden lange Aufnahmeauswertung ist speicherfrei: Es werden Filterstatistiken erfasst, keine Sprachaufnahme gespeichert.

Fünf Startpunkte: NATURAL RAP, SOFT S, BRIGHT VOCAL, DARK VOCAL, STRONG S. Diese Presets sind als unlernte Startpunkte gekennzeichnet; ein LEARN-Profil wird erst aus tatsächlichem Audio gebildet. Profile und Parameter werden mit dem DAW-Projekt gespeichert. Beschädigte Zustände mit ungültigen Pegeln/Frequenzen werden zurückgewiesen.

LIVE und PRO verwenden hier denselben kausalen Filter in Double-Precision. Beide melden exakt 0 Samples zusätzliche Plug-in-Latenz. Die Auswahl bleibt mit GILLCONTROL synchronisierbar. Die Audiogeräte-/DAW-Pufferlatenz bleibt davon unabhängig.

Die Erkennung verwendet Energie, Frequenzverteilung und Vorhersagbarkeit des Signals. Sie ist keine trainierte Sprach- oder Phonemerkennung. Starke Übersprechung, permanente Zischgeräusche oder bereits stark bearbeitete Aufnahmen können eine manuelle Kontrolle mit LISTEN REMOVED erfordern. Unterstützte Sampleraten: 16–384 kHz. Andere Raten werden unverändert durchgeleitet (nicht endliche Samples werden zu null).

GILL-DE-ESSER bleibt als separates manuelles Plug-in erhalten. Dieses Produkt verwendet keinen kommerziellen De-Esser-Quellcode und beansprucht keine identische Klangqualität zu fremden Produkten.
