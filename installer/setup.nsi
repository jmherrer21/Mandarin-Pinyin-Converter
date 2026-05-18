; Mandarin Pinyin Converter — NSIS Installer Script
; Build from any directory: makensis installer\setup.nsi
; Produces: installer\MandarinPinyinConverter-Setup.exe

!include "MUI2.nsh"
!include "LogicLib.nsh"

SetCompressor /SOLID lzma

; ── Metadata ──────────────────────────────────────────────────────────────────
Name "Mandarin Pinyin Converter"
OutFile "MandarinPinyinConverter-Setup.exe"
InstallDir "$PROGRAMFILES64\MandarinPinyinConverter"
InstallDirRegKey HKLM "Software\MandarinPinyinConverter" "InstallDir"
RequestExecutionLevel admin
Unicode True

VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName"     "Mandarin Pinyin Converter"
VIAddVersionKey "FileVersion"     "1.0.0.0"
VIAddVersionKey "FileDescription" "Mandarin Pinyin Converter Installer"
VIAddVersionKey "LegalCopyright"  "2026"

; ── MUI Configuration ─────────────────────────────────────────────────────────
!define MUI_ABORTWARNING
!define MUI_ICON   "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\mandarin-pdf-reader.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Mandarin Pinyin Converter"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Sections ──────────────────────────────────────────────────────────────────

Section "Main Application" SecMain
  SectionIn RO

  ; Install Visual C++ Redistributable silently (skip if already present)
  SetOutPath "$INSTDIR"
  File "vc_redist.x64.exe"
  ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" "Installed"
  ${If} $0 != 1
    ExecWait '"$INSTDIR\vc_redist.x64.exe" /install /quiet /norestart' $1
    ${If} $1 != 0
      MessageBox MB_OK|MB_ICONSTOP "Visual C++ Redistributable installation failed (code $1). The application may not run correctly."
    ${EndIf}
  ${EndIf}
  Delete "$INSTDIR\vc_redist.x64.exe"

  ; Main executable and PDF export tool
  File "..\build\Release\mandarin-pdf-reader.exe"
  File "..\build\Release\wkhtmltopdf.exe"

  ; MuPDF dependency DLLs
  File "..\build\Release\brotlicommon.dll"
  File "..\build\Release\brotlidec.dll"
  File "..\build\Release\bz2.dll"
  File "..\build\Release\freetype.dll"
  File "..\build\Release\harfbuzz.dll"
  File "..\build\Release\jpeg62.dll"
  File "..\build\Release\libpng16.dll"
  File "..\build\Release\openjp2.dll"
  File "..\build\Release\z.dll"

  ; Data directory — CC-CEDICT dictionary and jieba segmenter dicts
  SetOutPath "$INSTDIR"
  File /r "..\build\Release\data"

  ; Write uninstaller and register in Add/Remove Programs
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter" \
    "DisplayName" "Mandarin Pinyin Converter"
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter" \
    "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter" \
    "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter" \
    "DisplayVersion" "1.0.0"
  WriteRegDWORD HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter" \
    "NoModify" 1
  WriteRegDWORD HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter" \
    "NoRepair" 1
  WriteRegStr HKLM "Software\MandarinPinyinConverter" "InstallDir" "$INSTDIR"

  ; Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\Mandarin Pinyin Converter"
  CreateShortcut \
    "$SMPROGRAMS\Mandarin Pinyin Converter\Mandarin Pinyin Converter.lnk" \
    "$INSTDIR\mandarin-pdf-reader.exe"
  CreateShortcut \
    "$SMPROGRAMS\Mandarin Pinyin Converter\Uninstall.lnk" \
    "$INSTDIR\Uninstall.exe"

SectionEnd

Section "Piper TTS (offline audio, ~80 MB)" SecPiper

  SetOutPath "$INSTDIR\piper"
  File "..\piper\piper.exe"
  File "..\piper\espeak-ng.dll"
  File "..\piper\libtashkeel_model.ort"
  File "..\piper\onnxruntime.dll"
  File "..\piper\onnxruntime_providers_shared.dll"
  File "..\piper\piper_phonemize.dll"
  File "..\piper\zh_CN-huayan-medium.onnx"
  File "..\piper\zh_CN-huayan-medium.onnx.json"
  File /r "..\piper\espeak-ng-data"

SectionEnd

Section "Desktop Shortcut" SecDesktop
  CreateShortcut \
    "$DESKTOP\Mandarin Pinyin Converter.lnk" \
    "$INSTDIR\mandarin-pdf-reader.exe"
SectionEnd

; ── Section descriptions ──────────────────────────────────────────────────────
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} \
    "The main application, CC-CEDICT dictionary, jieba segmenter data, and wkhtmltopdf for PDF export. Required."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecPiper} \
    "Piper TTS engine with zh_CN-huayan voice model for offline audio generation (~80 MB)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} \
    "Add a shortcut to the Desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ── Uninstall Section ─────────────────────────────────────────────────────────
Section "Uninstall"

  Delete "$INSTDIR\mandarin-pdf-reader.exe"
  Delete "$INSTDIR\wkhtmltopdf.exe"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\brotlicommon.dll"
  Delete "$INSTDIR\brotlidec.dll"
  Delete "$INSTDIR\bz2.dll"
  Delete "$INSTDIR\freetype.dll"
  Delete "$INSTDIR\harfbuzz.dll"
  Delete "$INSTDIR\jpeg62.dll"
  Delete "$INSTDIR\libpng16.dll"
  Delete "$INSTDIR\openjp2.dll"
  Delete "$INSTDIR\z.dll"

  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\piper"
  RMDir /r "$INSTDIR\audio_cache"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Mandarin Pinyin Converter\Mandarin Pinyin Converter.lnk"
  Delete "$SMPROGRAMS\Mandarin Pinyin Converter\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\Mandarin Pinyin Converter"
  Delete "$DESKTOP\Mandarin Pinyin Converter.lnk"

  DeleteRegKey HKLM \
    "Software\Microsoft\Windows\CurrentVersion\Uninstall\MandarinPinyinConverter"
  DeleteRegKey HKLM "Software\MandarinPinyinConverter"

SectionEnd
