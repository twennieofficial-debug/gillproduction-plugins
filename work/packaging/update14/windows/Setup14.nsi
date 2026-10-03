Unicode true
!include MUI2.nsh
!include x64.nsh
!include FileFunc.nsh
!include WinVer.nsh
Name "GILLPRODUCTION VST3 Bundle"
OutFile "${GILL_OUTPUT}"
!ifdef TEST_BUILD
 RequestExecutionLevel user
!else
 RequestExecutionLevel admin
!endif
SetCompressor /FINAL zlib
CRCCheck force
ManifestDPIAware true
VIProductVersion "0.14.0.0"
VIAddVersionKey "ProductName" "GILLPRODUCTION VST3 Bundle"
VIAddVersionKey "FileVersion" "0.14.0.0"
VIAddVersionKey "FileDescription" "60 VST3 Plugins / Vocode, Grain and Pulse"
BrandingText "GILLPRODUCTION / 60 VST3 PLUGINS"
Var TestCase
Var ExtraArgs
Var Result
!define MUI_BGCOLOR "EFECE5"
!define MUI_TEXTCOLOR "202C26"
!define MUI_WELCOMEPAGE_TITLE "GILLPRODUCTION"
!define MUI_WELCOMEPAGE_TEXT "$(Welcome10)"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${GILL_LICENSE}"
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "$(Ready10)"
!define MUI_FINISHPAGE_TEXT "$(Finish10)"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE German
!insertmacro MUI_LANGUAGE English
LangString Welcome10 ${LANG_GERMAN} "60 Plugins. Ein Setup.$\r$\n$\r$\nBitte Projekte speichern und FL Studio vor der Installation schließen.$\r$\n$\r$\n57 Plugins werden aktualisiert; GILLVOCODE, GILLGRAIN und GILLPULSE kommen hinzu. PRISM BLUE Design und 0 Samples LIVE-Puffer. Bestehende Plugin-IDs bleiben erhalten."
LangString Welcome10 ${LANG_ENGLISH} "60 plugins. One setup.$\r$\n$\r$\nSave your projects and close FL Studio before installation.$\r$\n$\r$\n57 plugins are updated; GILLVOCODE, GILLGRAIN and GILLPULSE are added. PRISM BLUE and zero additional LIVE buffer latency. Existing plugin identities are preserved."
LangString Ready10 ${LANG_GERMAN} "Bereit für FL Studio"
LangString Ready10 ${LANG_ENGLISH} "Ready for FL Studio"
LangString Finish10 ${LANG_GERMAN} "Options > Manage plugins > Find installed plugins.$\r$\n$\r$\nDanach im Mixer unter More plugins nach GILL suchen."
LangString Finish10 ${LANG_ENGLISH} "Options > Manage plugins > Find installed plugins.$\r$\n$\r$\nThen search for GILL in the mixer's More plugins list."
!macro INIT PREFIX
Function ${PREFIX}.onInit
 ${IfNot} ${RunningX64}
  SetErrorLevel 10
  Quit
 ${EndIf}
 ${IfNot} ${AtLeastWin10}
  SetErrorLevel 10
  Quit
 ${EndIf}
 StrCpy $ExtraArgs ""
 !ifdef TEST_BUILD
  StrCpy $TestCase "main"
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/CASE=" $1
  ${IfNot} ${Errors}
   StrCpy $TestCase $1
  ${EndIf}
  StrCpy $ExtraArgs '--case "$TestCase"'
 !endif
FunctionEnd
!macroend
!insertmacro INIT ""
!insertmacro INIT "un"
Section Install
 InitPluginsDir
 SetOutPath "$PLUGINSDIR"
 File /oname=transaction.exe "${GILL_HELPER}"
 !include "${GILL_FILES}"
 WriteUninstaller "$PLUGINSDIR\Uninstall.exe"
 IfErrors failed
 nsExec::ExecToLog '"$PLUGINSDIR\transaction.exe" --payload "$PLUGINSDIR\payload" --uninstaller "$PLUGINSDIR\Uninstall.exe" $ExtraArgs'
 Pop $Result
 ${If} $Result != 0
  Goto failed
 ${EndIf}
 SetErrorLevel 0
 Goto done
failed:
 MessageBox MB_OK|MB_ICONSTOP "Installation nicht abgeschlossen. Es wurde kein verwendetes Plugin erzwungen ersetzt. Details stehen im Installationsprotokoll. / Installation did not complete; see the log." /SD IDOK
 SetErrorLevel 30
 Quit
done:
SectionEnd
Section Uninstall
 InitPluginsDir
 SetOutPath "$PLUGINSDIR"
 File /oname=transaction.exe "${GILL_HELPER}"
 nsExec::ExecToLog '"$PLUGINSDIR\transaction.exe" --remove $ExtraArgs'
 Pop $Result
 ${If} $Result != 0
  MessageBox MB_OK|MB_ICONSTOP "Deinstallation nicht abgeschlossen. Verwendete oder geänderte Dateien bleiben geschützt. / Uninstall could not complete." /SD IDOK
  SetErrorLevel 31
  Quit
 ${EndIf}
 SetErrorLevel 0
SectionEnd
