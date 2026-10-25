Unicode True
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
Name "Satr Studio 0.9.0"
!ifndef SATR_OUTPUT
  !define SATR_OUTPUT "../../../outputs/Satr-Studio-0.9.0-Windows-x64-Setup.exe"
!endif
OutFile "${SATR_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Satr Studio"
InstallDirRegKey HKCU "Software\SatrStudio" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID zlib
SetDatablockOptimize off
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "0.9.0.0"
VIAddVersionKey "ProductName" "Satr Studio"
VIAddVersionKey "FileDescription" "Satr Studio Installer"
VIAddVersionKey "FileVersion" "0.9.0"
VIAddVersionKey "LegalCopyright" "Satr Studio; third-party components retain their licenses."
!define MUI_ICON "../src/Satr.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\Satr.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "Arabic"
Var TestInstall
Function .onInit
    SetRegView 64
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Satr Studio requires 64-bit Windows (Intel/AMD)."
        Abort
    ${EndIf}
    SetShellVarContext current
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/TEST=" $TestInstall
    ${If} $TestInstall != "1"
        !insertmacro MUI_LANGDLL_DISPLAY
    ${EndIf}
FunctionEnd
Section "Satr Studio" SEC_MAIN
    SetOutPath "$INSTDIR"
    File /r "../dist/windows/*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    ${If} $TestInstall == "1"
        FileOpen $0 "$INSTDIR\.installer-test" w
        FileWrite $0 "1"
        FileClose $0
    ${Else}
        CreateDirectory "$SMPROGRAMS\Satr Studio"
        CreateShortcut "$SMPROGRAMS\Satr Studio\Satr Studio.lnk" "$INSTDIR\Satr.exe"
        CreateShortcut "$SMPROGRAMS\Satr Studio\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
        CreateShortcut "$DESKTOP\Satr Studio.lnk" "$INSTDIR\Satr.exe"
        WriteRegStr HKCU "Software\SatrStudio" "InstallDir" "$INSTDIR"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "DisplayName" "Satr Studio"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "DisplayVersion" "0.9.0"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "Publisher" "Satr Studio"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "DisplayIcon" "$INSTDIR\Satr.exe"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "UninstallString" '"$INSTDIR\Uninstall.exe"'
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "NoModify" 1
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio" "NoRepair" 1
        IfFileExists "$WINDIR\Sysnative\vcruntime140_1.dll" runtime_ready
        MessageBox MB_YESNO|MB_ICONQUESTION "Install Microsoft Visual C++ for the local coding assistant? Windows may ask for administrator permission." IDNO runtime_ready
        ExecShell "runas" "$INSTDIR\setup\vc_redist.x64.exe" "/install /passive /norestart"
        runtime_ready:
    ${EndIf}
SectionEnd
Section "Uninstall"
    SetRegView 64
    SetShellVarContext current
    IfFileExists "$INSTDIR\.installer-test" skip_registration
    ReadRegStr $0 HKCU "Software\SatrStudio" "InstallDir"
    StrCmp $0 "$INSTDIR" 0 skip_registration
    Delete "$DESKTOP\Satr Studio.lnk"
    Delete "$SMPROGRAMS\Satr Studio\Satr Studio.lnk"
    Delete "$SMPROGRAMS\Satr Studio\Uninstall.lnk"
    RMDir "$SMPROGRAMS\Satr Studio"
    DeleteRegKey HKCU "Software\SatrStudio"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\SatrStudio"
    skip_registration:
    !include "uninstall-files.nsh"
    Delete "$INSTDIR\.installer-test"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
