# Drittanbieter

- JUCE8.0.12, Commit29396c22c93392d6738e021b83196283d6e4d850; vollständige Quellen und Lizenztexte im Paket unter dependencies/JUCE.
- Die GILLHARMONY-, GILLREFERENCE- und GILLRESCUE-Audiofunktionen sind eigene Implementierungen. Die gemeinsamen GILL-Oberflächen und der LIVE/PRO-Client werden im entsprechenden Quellpaket mitgeliefert.
- Der zusätzliche Sprachvergleich für GILLRESCUE verwendet die bestehende LibriSpeech-Datei 2277-149896-0026 aus dev-clean (OpenSLR SLR12), CC BY 4.0. Attribution: Vassil Panayotov, Guoguo Chen, Daniel Povey und Sanjeev Khudanpur, „LibriSpeech: An ASR corpus based on public domain audio books“, ICASSP 2015. Quelle: https://www.openslr.org/12/ ; Lizenz: https://creativecommons.org/licenses/by/4.0/ . Die 48-kHz-Interpolation stammt aus GILLDEREVERB/Tests/fixtures; deren ORIGIN-AND-LICENSE.md und provenance.json dokumentieren die Ableitung. Im neuen Vergleich wurde sie normalisiert und künstlich hart geclippt. Kein privates Nutzeraudio wird für diesen Test benötigt oder übertragen.
- Weitere Testtöne, Impulse und Multitonsignale werden lokal synthetisch erzeugt; sie enthalten keine fremden Musikaufnahmen.
