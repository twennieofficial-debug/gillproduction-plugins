# GILL Mac – Build und Installer

Diese Pipeline baut alle 29 Plugins als echte Universal-VST3 für Apple Silicon
und Intel. Die Oberfläche bleibt in den gleichen kompakten logischen Größen wie
unter Windows. Die Abmessungen sind in `products.json` festgeschrieben; frische
Mac-Screenshots der nativen Tests müssen für jedes Plugin genau diese Größe
belegen. Das ersetzt keine abschließende Sicht- und FL-Studio-Prüfung auf Mac.

## Stand dieser Dateien

Die Pipeline wurde auf dem Windows-Rechner vorbereitet. Eine echte Mac-DMG kann
erst aus erfolgreichen Mac-Builds entstehen. Solange `MAC-RELEASE.json` und die
beiden nativen Validierungsberichte fehlen, ist keine Mac-Veröffentlichung fertig.
Windows-DLLs werden niemals nur umbenannt oder als DMG ausgegeben.

## Für Nutzer

DMG öffnen → PKG doppelt anklicken → Installieren → in FL Studio
**Options → Manage plugins → Find installed plugins**. Anschließend stehen alle
29 Plugins in **More plugins** unter GILL. Es wird keine Downloader-App benötigt.
Die Dateien liegen in `/Library/Audio/Plug-Ins/VST3/GILLPRODUCTION`.
Der Installer ändert keine FL-Projekte, Audioeinstellungen oder Gatekeeper-Regeln.

## GitHub Actions

Die Workflowvorlage `github-actions.yml` nach
`.github/workflows/build-macos.yml` im ausdrücklich freigegebenen Repository
kopieren. Die Struktur bleibt `work/GILLEQ`, `work/GILLVOCAL` usw. JUCE liegt als
auf `29396c22c93392d6738e021b83196283d6e4d850` festgelegtes Git-Submodul unter
`work/dependencies/JUCE`. Keine Windows-Builds, alten Gesprächsarchive, privaten
Aufnahmen oder lokalen Benutzerpfade hochladen.

Den Workflow manuell starten. Es gibt keine automatischen Push-/Zeitplanläufe.
Zwei native Buildjobs laufen höchstens 240 Minuten, zwei Validierungsjobs jeweils
höchstens 120 Minuten; Merge und Verpackung sind ebenfalls zeitlich begrenzt.
Maximal zwei Rechner laufen je Matrix gleichzeitig. Das ist ein Kostenlimit
durch Laufzeit, keine Kostenschätzung. Private Repositories können das
Actions-Minutenkontingent bzw. kostenpflichtige Minuten nutzen. Compiler-Cache
ist auf 600 MB begrenzt, Ergebnisartefakte werden 14 Tage aufbewahrt.

Der Ablauf ist:

1. Native ARM- und Intel-Kompilierung, sämtliche registrierten DSP-/GUI-CTest.
2. Versions-, Bundle-ID-, Mach-O-, Systembibliotheken- und UI-Größenprüfung.
3. Ressourcenvergleich beider Builds, Universal-Zusammenführung, Signierung.
4. Genau diese Universal-Bundles durchlaufen pluginval Stufe 10 nativ auf ARM
   und Intel: fünf Abtastraten und acht Puffergrößen, einschließlich GUI-Tests.
5. Erst wenn die Berichte zu den unveränderten Bundle-Hashes passen: PKG/DMG.

Ein GitHub-Job ist kein FL-Studio-Hörtest. Native GUI-Screenshots liegen in den
Build-Artefakten; eine finale Sichtprüfung bleibt erforderlich. Ein fehlender
Mac, falsche Architektur, fehlendes Testbild, veränderte Binärdatei oder
fehlgeschlagener Test stoppt die Pipeline.

## Signierung und Notarisierung

Ohne Apple-Zertifikate ist nur eine klar mit **UNSIGNIERT-TESTVERSION** benannte
DMG vorgesehen. Die internen Mach-O-Dateien erhalten zur Codeintegrität eine
lokale Ad-hoc-Signatur; das ist keine Developer-ID-Freigabe. Gatekeeper kann die
Installation blockieren. Die Pipeline entfernt keine Quarantäneattribute und
deaktiviert keine Sicherheitsprüfung.

Für eine normal verteilbare signierte Version werden folgende GitHub-Secrets
vorab vom Eigentümer eingerichtet. Keine Passwörter oder Zertifikate ins
Repository oder in den Chat kopieren:

- `GILL_APPLICATION_IDENTITY`: vollständiger „Developer ID Application: …“-Name
- `GILL_APP_CERT_P12_BASE64` und `GILL_APP_CERT_PASSWORD`
- `GILL_INSTALLER_IDENTITY`: vollständiger „Developer ID Installer: …“-Name
- `GILL_INSTALLER_CERT_P12_BASE64` und `GILL_INSTALLER_CERT_PASSWORD`
- `GILL_NOTARY_KEY_P8_BASE64`, `GILL_NOTARY_KEY_ID`, `GILL_NOTARY_ISSUER`:
  App-Store-Connect-Team-API-Schlüssel mit Berechtigung für Apples Notary-Dienst

Dann den Workflow mit `notarize=true` starten. Schlüssel werden ausschließlich
im temporären CI-Keychain verwendet. Die Plug-ins werden von innen nach außen
signiert; anschließend wird das PKG signiert, bei Apple eingereicht und das
Ticket angeheftet. Danach dasselbe für die DMG. Apples Status muss jeweils
`Accepted` sein, die Ticket- und Gatekeeper-Prüfungen müssen erfolgreich sein.
Die Ad-hoc-Testversion erhält kein `end_user_release_ready=true`.

## Lokal auf einem Mac

Xcode mit Mac-SDK, CMake ab 3.22, Ninja, ccache und Python 3.11 vorausgesetzt.
Beide nativen Architekturen müssen auf passenden Macs geprüft werden. Die
einzelnen Phasen sind mit `python3 pipeline.py --help` aufgelistet; keine Phase
installiert ungefragt Plugins auf dem Build-Mac.

Der macOS-11-Deployment-Target ist eine Build-Untergrenze. Der Workflow prüft auf
macOS 15. Er behauptet damit keinen realen FL-Test auf jeder älteren macOS-Version.

## Quellen für den Ablauf

- [Apple: Mac-Software verpacken](https://developer.apple.com/documentation/xcode/packaging-mac-software-for-distribution)
- [Apple: Notarisierung](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
- [Apple: Angepasster Notarisierungsablauf](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow)
- [GitHub: Native ARM- und Intel-Runner](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- [Tracktion: pluginval](https://github.com/Tracktion/pluginval)

Der offizielle pluginval-1.0.4-Download ist auf SHA256
`3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f`
festgelegt. Der Workflow prüft diesen Hash vor dem Start.
