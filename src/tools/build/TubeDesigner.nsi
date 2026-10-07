; Generated payload includes come from build_tube_designer_installer.ps1.
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef CONFIG_INCLUDE
  !error "Pass /DCONFIG_INCLUDE=<generated installer-config.nsh>."
!endif
!include "${CONFIG_INCLUDE}"

!define PRODUCT_NAME "TubeDesigner"
!define PRODUCT_VERSION "0.1.0"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\iCAX.TubeDesigner"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${INSTALLER_OUTPUT}"
InstallDir "$PROGRAMFILES64\TubeDesigner"
BrandingText "TubeDesigner"
VIProductVersion "0.1.0.0"
VIAddVersionKey /LANG=2052 "ProductName" "TubeDesigner"
VIAddVersionKey /LANG=2052 "ProductVersion" "0.1.0"
VIAddVersionKey /LANG=2052 "FileVersion" "0.1.0.0"
VIAddVersionKey /LANG=2052 "FileDescription" "TubeDesigner 安装程序"
VIAddVersionKey /LANG=2052 "LegalCopyright" "TubeDesigner"
ManifestSupportedOS Win10
ShowInstDetails show
ShowUninstDetails show

!ifdef PACKAGE_TEST_ONLY
  ; Same payload and uninstall boundary; isolated verification without elevation.
  RequestExecutionLevel user
  SetCompress off
!else
  RequestExecutionLevel admin
  SetCompressor /SOLID lzma
  SetCompressorDictSize 32
!endif

!ifdef PRODUCT_ICON
  Icon "${PRODUCT_ICON}"
  UninstallIcon "${PRODUCT_ICON}"
!endif
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "欢迎安装 TubeDesigner"
!define MUI_WELCOMEPAGE_TEXT "此程序将安装 TubeDesigner 0.1.0（x64）。$\r$\n$\r$\n安装前请保存工作并退出正在运行的 TubeDesigner。"
!define MUI_FINISHPAGE_TITLE "TubeDesigner 安装完成"
!define MUI_FINISHPAGE_TEXT "您可以通过桌面或开始菜单快捷方式打开 TubeDesigner。"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "TubeDesigner 需要 64 位 Windows 10 或 Windows 11。"
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "TubeDesigner 需要 Windows 10 或 Windows 11。"
    Abort
  ${EndIf}
  SetRegView 64
!ifndef PACKAGE_TEST_ONLY
  SetShellVarContext all
  ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "InstallLocation"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
!endif
FunctionEnd

Section "TubeDesigner" SEC_APPLICATION
  SetOverwrite on
  ; Explicit files only: no recursive wildcard can pull in projects or caches.
  !include "${INSTALL_FILES_INCLUDE}"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
!ifndef PACKAGE_TEST_ONLY
  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\TubeDesigner"
  SetOutPath "$INSTDIR"
  CreateShortcut "$SMPROGRAMS\TubeDesigner\TubeDesigner.lnk" "$INSTDIR\TubeDesigner.exe"
  CreateShortcut "$DESKTOP\TubeDesigner.lnk" "$INSTDIR\TubeDesigner.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "TubeDesigner"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "TubeDesigner"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\TubeDesigner.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" ${PAYLOAD_SIZE_KB}
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
!endif
SectionEnd

Function un.onInit
  SetRegView 64
!ifndef PACKAGE_TEST_ONLY
  SetShellVarContext all
!endif
FunctionEnd

Section "Uninstall"
  ; Remove only the declared payload and empty directories, never user data.
  !include "${UNINSTALL_FILES_INCLUDE}"
  Delete "$INSTDIR\Uninstall.exe"
!ifndef PACKAGE_TEST_ONLY
  Delete "$SMPROGRAMS\TubeDesigner\TubeDesigner.lnk"
  RMDir "$SMPROGRAMS\TubeDesigner"
  Delete "$DESKTOP\TubeDesigner.lnk"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
!endif
  RMDir "$INSTDIR"
SectionEnd
