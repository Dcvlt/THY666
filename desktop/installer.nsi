Unicode true
!include "MUI2.nsh"
Name "THY666 Client"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\THY666"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "THY666 Client"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "THY666 contributors"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOAD}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Section "Client"
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\THY666 Client.lnk" "$INSTDIR\thy666-desktop.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\THY666" "DisplayName" "THY666 Client"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\THY666" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\THY666" "UninstallString" '$"$INSTDIR\Uninstall.exe$"'
SectionEnd
Section "Uninstall"
  Delete "$SMPROGRAMS\THY666 Client.lnk"
  Delete "$INSTDIR\thy666-desktop.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\THIRD_PARTY_LICENSES.txt"
  Delete "$INSTDIR\FONT_LICENSES.txt"
  Delete "$INSTDIR\QUICKSTART.txt"
  Delete "$INSTDIR\PROJECT_NOTICES.md"
  Delete "$INSTDIR\THY666-MIT-legacy.txt"
  Delete "$INSTDIR\resources\README.txt"
  RMDir "$INSTDIR\resources"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\THY666"
SectionEnd
